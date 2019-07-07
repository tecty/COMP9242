#include "nfs.h"
#include "../network.h"
#include <adt/dynamic.h>
#include "sos_stat.h"
#include <fcntl.h>
#include <string.h>

#include <aos/sel4_zf_logif.h>
#define NFS_IO_MAX (1<<16)
// #define NFS_IO_MAX (1<<11)

enum DriverNfs_op {
    OPEN, CLOSE, READ, WRITE, STAT
};

typedef struct driver_nfs_task {
    void * buf;
    uint64_t buf_len;
    driver_nfs_callback_t callback;
    enum DriverNfs_op op;
    void * private_data;
    // for continue 
    struct nfsfh * fh;
    uint64_t index;
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
 * oft operation, iovec things
 */

void DriverNfs__callback(
    int err, struct nfs_context * nfs, void * data, void * private_data
){
    if (nfs != nfs_s.nfs_context) nfs_s.nfs_context = nfs;
    
    size_t id = (size_t) private_data;
    driver_nfs_task_t task = DynamicArr__get(nfs_s.tasks, id);

    // ZF_LOGE("The NFS task size is %lu", DynamicArr__getAlloced(nfs_s.tasks));
    size_t this_io;
    switch (task->op)
    {
    case OPEN:
        *(void * * )task->buf = data;
        break;
    case CLOSE:
        ZF_LOGE_IF(err< 0, "NFS close fail");
        break;
    case READ:
        if (err < 0) break;

        // ELSE: no error
        // copy out 
        // ZF_LOGE("Read %d", err);
        memcpy(task->buf +task->index , data, err);
        task->index += err;
        // return to client if nothing to read, or read till buffer size 
        if (task->index == task->buf_len || err == 0) break;
        this_io = 
            (task->buf_len - task->index) > NFS_IO_MAX ? 
            NFS_IO_MAX : (task->buf_len - task->index);
        if (
            nfs_read_async(
                nfs_s.nfs_context, task->fh, this_io, DriverNfs__callback, (void *) id
            ) < 0
        ){
            ZF_LOGE("NFS__async call failed");
            DynamicArr__del(nfs_s.tasks, id);
        } else {
            return;
        }
        break;
    case WRITE:
        if (err < 0) break;
        task->index += err;
        if (task->index == task->buf_len) break;
        this_io = 
            (task->buf_len - task->index) > NFS_IO_MAX ? 
            NFS_IO_MAX : (task->buf_len - task->index);
        // ZF_LOGE("try to continue the write index %lu\tthis_io %lubuf_len %lu\t",task->index, this_io, task->buf_len);
        if (
            nfs_write_async(
                nfs_s.nfs_context, task->fh, this_io,
                // &(((char *)task->buf)[task->index]), 
                task->buf + task->index, 
                DriverNfs__callback, (void *) id
            ) < 0
        ){
            ZF_LOGE("NFS__async call failed");
            DynamicArr__del(nfs_s.tasks, id);
        } else {
            return;
        }
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
    if (task->op == WRITE || task->op == READ) {
        task->callback(task->index, task->private_data);
    } else
    {
        task->callback(err, task->private_data);
    }
    
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
    
    if (
        nfs_open_async(
            nfs_s.nfs_context, path, flags, DriverNfs__callback, (void *) id
        ) < 0
    ){
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
    
    if (strcmp(path, "..")==0){
        path = ".";
    }

    if (
        nfs_stat64_async(
            nfs_s.nfs_context, path, DriverNfs__callback, (void *) id
        ) < 0
    ){
        ZF_LOGE("NFS__async call failed path: %s", path);
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
    dnt.op           = READ;
    dnt.fh           = context;
    dnt.index        = 0;
    dnt.private_data = private_data;
    size_t id = DynamicArr__add(nfs_s.tasks, & dnt);

    size_t this_io = len > NFS_IO_MAX ? NFS_IO_MAX : len;
    if (
        nfs_read_async(
            nfs_s.nfs_context, (struct nfsfh *) context, this_io, 
            DriverNfs__callback, (void *) id
        ) < 0
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
    dnt.op           = WRITE;
    dnt.private_data = private_data;
    dnt.fh           = context;
    dnt.index        = 0;
    size_t id = DynamicArr__add(nfs_s.tasks, & dnt);
    // ZF_LOGE("I want to print something");
    size_t this_io = len > NFS_IO_MAX ? NFS_IO_MAX: len;
    if (
        nfs_write_async(
            nfs_s.nfs_context, (struct nfsfh *)context, this_io, buf, 
            DriverNfs__callback, (void *) id
        ) < 0
    ){
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
    dnt.op           = CLOSE;
    dnt.private_data = private_data;
    size_t id = DynamicArr__add(nfs_s.tasks, & dnt);
    if (
        nfs_close_async(
            nfs_s.nfs_context, (struct nfsfh *)context, 
            DriverNfs__callback, (void *) id
        ) < 0
    ){
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
    // nfs_seekdir(nfs_s.nfs_context, nfs_s.root, 0);
    nfs_rewinddir(nfs_s.nfs_context, nfs_s.root);

    struct nfsdirent* entry;
    for (size_t i = 0; i <= loc; i++)
    {
        // I don't know why the seekdir wont push the curr forward
        entry = nfs_readdir(nfs_s.nfs_context, nfs_s.root);
    }
    

    int ret;
    if (entry) {
        strncpy(buf, (const char *)entry->name, buf_len);
        ret = 1;
    } else {
        ret = 0;
    }
    
    //  call the callback directly 
    cb(ret, private_data);
}