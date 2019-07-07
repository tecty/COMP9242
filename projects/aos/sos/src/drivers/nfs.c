#include "nfs.h"
#include "../network.h"
#include <adt/dynamic.h>
#include "sos_stat.h"
#include <fcntl.h>
#include <string.h>

#define SOS_NFS_DIR "/var/lib/tftpboot/tecty/"
#include <aos/sel4_zf_logif.h>

// #define NFS_ROOT "/var/lib/tftpboot/tecty"
#define NFS_ROOT "/"
#define NFS_PATH_MAX (1024)

char path_buf[NFS_PATH_MAX];

enum DriverNfs_op {
    OPEN, CLOSE, READ, WRITE, STAT
};

typedef struct driver_nfs_task {
    void * buf;
    uint64_t buf_len;
    driver_nfs_callback_t callback;
    enum DriverNfs_op op;
    void * private_data;
}* driver_nfs_task_t;


static struct {
    DynamicArr_t tasks;
    struct nfs_context * nfs_context; 
    struct nfsdir * root;
} nfs_s;


// enable this driver;
void DriverNfs__initCallback(
    int err, UNUSED struct nfs_context * nfs, void * data, UNUSED void* private_data
){
    // printf("DEBUG:Init has been called back\n");
    if (err < 0){
        ZF_LOGE("Open root directory fault :%s", (char *) data);
        return;
    }
    nfs_s.root = (struct nfsdir *) data;

    // printf(
    //     "DEBUG: Nfs open root with %s \n", 
    //     nfs_readdir(nfs_s.nfs_context, nfs_s.root)->name
    // );
}

void DriverNfs__init(struct nfs_context * context){
    // nfs_s.nfs_context = nfs_init_context();
    nfs_s.nfs_context = context;
    nfs_opendir_async(nfs_s.nfs_context, "/", DriverNfs__initCallback, NULL);
    nfs_s.tasks = DynamicArr__init(sizeof(struct driver_nfs_task));
}
void DriverNfs__free(){
    nfs_closedir(nfs_s.nfs_context, nfs_s.root);
    nfs_destroy_context(nfs_s.nfs_context);
    DynamicArr__free(nfs_s.tasks);
}

/**
 * Helper function might need
 */
void DriverNfs__setPath(char * path){
    path_buf[0] = '\0';
    uint32_t root_path_len = strlen(NFS_ROOT);
    strcat(path_buf, NFS_ROOT);
    // gracefully fault when the path is overflow
    // since we only support one layer, 1K is enought
    strncat(path_buf, path,NFS_PATH_MAX - root_path_len - 1);
}


/**
 * oft operation, iovec things
 */

void DriverNfs__callback(
    int err, struct nfs_context * nfs, void * data, void * private_data
){
    if (nfs != nfs_s.nfs_context) nfs_s.nfs_context = nfs;
    
    size_t id = (size_t) private_data;
    driver_nfs_task_t task = DynamicArr__get(nfs_s.tasks, id);

    switch (task->op)
    {
    case OPEN:
        *(void * * )task->buf = data;
        break;
    case CLOSE:
        ZF_LOGE_IF(err< 0, "NFS close fail");
        break;
    case READ:
        if (err > 0) {
            // copy out 
            memcpy(task->buf, data, err);
        }
        break;
    case WRITE:
        break;
    case STAT:
        ;
        struct nfs_stat_64 * nfs_stat = (struct nfs_stat_64 *) data;
        sos_stat_t client_stat        = (sos_stat_t) task->buf;
        
        client_stat->st_atime = nfs_stat->nfs_atime;
        client_stat->st_ctime = nfs_stat->nfs_ctime;
        client_stat->st_size  = nfs_stat->nfs_size;
        client_stat->st_fmode = (int) nfs_stat->nfs_mode;
        if (nfs_stat->nfs_dev){
            client_stat->st_type  = ST_SPECIAL;
        } else {
            client_stat->st_type  = ST_FILE;
        }
        break;
    }
    // return the user's data
    task->callback(err, task->private_data);
    DynamicArr__del(nfs_s.tasks, id);

}


/**
 * OPEN()
 */
void DriverNfs__open(
    char * path, int flags, void * buf,
    driver_nfs_callback_t cb, void * private_data
){
    struct driver_nfs_task dnt;
    dnt.buf          = buf;
    dnt.buf_len      = 0;
    dnt.callback     = cb;
    dnt.op           = OPEN;
    dnt.private_data = private_data;
    size_t id = DynamicArr__add(nfs_s.tasks, & dnt);
    
    DriverNfs__setPath(path);
    
    if (!nfs_open_async(
            nfs_s.nfs_context, path_buf, flags, DriverNfs__callback, (void *) id
    )){
        ZF_LOGE("NFS__async call failed");
        DynamicArr__del(nfs_s.tasks, id);
    }
    
}


/**
 * STAT()
 */
void DriverNfs__stat(
    char * path, void * buf, driver_nfs_callback_t cb, void * private_data
){
    struct driver_nfs_task dnt;
    dnt.buf          = buf;
    dnt.buf_len      = 0;
    dnt.callback     = cb;
    dnt.op           = STAT;
    dnt.private_data = private_data;
    size_t id = DynamicArr__add(nfs_s.tasks, & dnt);
    
    DriverNfs__setPath(path);
    
    if (!nfs_stat64_async(
            nfs_s.nfs_context, path_buf, DriverNfs__callback, (void *) id
    )){
        ZF_LOGE("NFS__async call failed");
        DynamicArr__del(nfs_s.tasks, id);
    }
    
}

/**
 * READ()
 * @buf is the sos vaddr that mapped of client buffer
 */
void DriverNfs__read(
    void * context, void * buf, uint64_t len, driver_nfs_callback_t cb, 
    void * private_data
){
    struct driver_nfs_task dnt;
    dnt.buf          = buf;
    dnt.buf_len      = len;
    dnt.callback     = cb;
    dnt.private_data = private_data;
    size_t id = DynamicArr__add(nfs_s.tasks, & dnt);
    if (!nfs_read_async(
            nfs_s.nfs_context, (struct nfsfh *) context, len, 
            DriverNfs__callback, (void *) id)
    ){
        ZF_LOGE("NFS__async call failed");
        DynamicArr__del(nfs_s.tasks, id);
    }
}

/**
 * WRITE()
 * @buf is the sos vaddr that mapped of client buffer
 */
void DriverNfs__write(
    void * context, void * buf, uint64_t len, driver_nfs_callback_t cb,
    void * private_data
){
    struct driver_nfs_task dnt;
    dnt.buf          = buf;
    dnt.buf_len      = len;
    dnt.callback     = cb;
    dnt.private_data = private_data;
    size_t id = DynamicArr__add(nfs_s.tasks, & dnt);
    if (!nfs_write_async(
            nfs_s.nfs_context, (struct nfsfh *)context, len, buf, 
            DriverNfs__callback, (void *) id
    )){
        ZF_LOGE("NFS__async call failed");
        DynamicArr__del(nfs_s.tasks, id);
    }
}


/**
 * CLOSE()
 */
void DriverNfs__close(
    void * context, driver_nfs_callback_t cb, void * private_data
){
    struct driver_nfs_task dnt;
    dnt.callback     = cb;
    dnt.private_data = private_data;
    size_t id = DynamicArr__add(nfs_s.tasks, & dnt);
    if (!nfs_close_async(
            nfs_s.nfs_context, (struct nfsfh *)context, 
            DriverNfs__callback, (void *) id
    )){
        ZF_LOGE("NFS__async call failed");
        DynamicArr__del(nfs_s.tasks, id);
    }
}

/**
 * GET_DIR_ENTRY()
 */
void DriverNfs__getDirEntry(
    UNUSED void * context, size_t loc, void * buf, size_t buf_len,
    driver_nfs_callback_t cb, void * private_data
){
    nfs_seekdir(nfs_s.nfs_context, nfs_s.root,loc);
    struct nfsdirent* entry = nfs_readdir(nfs_s.nfs_context, nfs_s.root);
    nfs_rewinddir(nfs_s.nfs_context, nfs_s.root);

    int ret;
    if (entry) {
        // TODO: BUG
        printf("buf before  to %s\n", (char *) buf);
        printf("I got buflen is %lu\n", buf_len);
        printf("I got entry name is %s\n", entry->name);
        // strncpy(buf, (const char *)entry->name, buf_len);
        char * char_buf = buf;
        size_t i;
        for (i = 0; i < buf_len && entry->name[i]!= '\0'; i++)
        {
            char_buf[i] = entry->name[i];
        }
        for (; i < buf_len; i++)
        {
            /* code */
            char_buf[i] = '\0';
        }
        
        
        printf("buf now is write to %s\n", (char *) buf);
        ret = 0;
    } else {
        ret = 1;
    }
    
    //  call the callback directly 
    cb(ret, private_data);
}