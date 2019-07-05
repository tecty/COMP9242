#include "nfs.h"
#include <nfsc/libnfs.h>
#include <adt/dynamic.h>
#include "sos_stat.h"
#include <fcntl.h>
#include <aos/sel4_zf_logif.h>

typedef struct driver_nfs_task {
    void * buf;
    uint64_t buf_len;
    driver_nfs_callback_t callback;
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
    if (err < 0){
        return;
    }
    nfs_s.root = (struct nfsdir *) data;
}

void DriverNfs__init(){
    nfs_s.nfs_context = nfs_init_context();
    nfs_opendir_async(
        nfs_s.nfs_context, "/var/lib/tftpboot/tecty", 
        DriverNfs__initCallback, NULL
    );
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


/**
 * OPEN()
 */
void DriverNfs__openCallback(
    int err, struct nfs_context * nfs, void * data, void * private_data
){
    if (nfs != nfs_s.nfs_context) nfs_s.nfs_context = nfs;
    
    size_t id = (size_t) private_data;
    driver_nfs_task_t task = DynamicArr__get(nfs_s.tasks, id);
    *(void * * )task->buf = data;
    // return the user's data
    task->callback(err, task->private_data);
    DynamicArr__del(nfs_s.tasks, id);
}
void DriverNfs__open(
    char * path, int flags, void * buf,
    driver_nfs_callback_t cb, void * private_data
){
    struct driver_nfs_task dnt;
    dnt.buf          = buf;
    dnt.buf_len      = 0;
    dnt.callback     = cb;
    dnt.private_data = private_data;
    size_t id = DynamicArr__add(nfs_s.tasks, & dnt);
    if (!nfs_open_async(
            nfs_s.nfs_context, path, flags, DriverNfs__openCallback, (void *) id
    )){
        ZF_LOGE("NFS__async call failed");
        DynamicArr__del(nfs_s.tasks, id);
    }
    
}


/**
 * STAT()
 */
void DriverNfs__statCallback(
    int err, struct nfs_context * nfs, void * data, void * private_data
){
    if (nfs != nfs_s.nfs_context) nfs_s.nfs_context = nfs;
    
    size_t id = (size_t) private_data;
    driver_nfs_task_t task = DynamicArr__get(nfs_s.tasks, id);

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
    
    // return the user's data
    task->callback(err, task->private_data);
    DynamicArr__del(nfs_s.tasks, id);
}
void DriverNfs__stat(
    char * path, void * buf, driver_nfs_callback_t cb, void * private_data
){
    struct driver_nfs_task dnt;
    dnt.buf          = buf;
    dnt.buf_len      = 0;
    dnt.callback     = cb;
    dnt.private_data = private_data;
    size_t id = DynamicArr__add(nfs_s.tasks, & dnt);
    if (!nfs_stat64_async(
            nfs_s.nfs_context, path, DriverNfs__statCallback, (void *) id
    )){
        ZF_LOGE("NFS__async call failed");
        DynamicArr__del(nfs_s.tasks, id);
    }
    
}

/**
 * READ()
 * @buf is the sos vaddr that mapped of client buffer
 */
void DriverNfs__readCallback(
    int err, struct nfs_context * nfs, void * data, void * private_data
){
    if (nfs != nfs_s.nfs_context) nfs_s.nfs_context = nfs;
    
    size_t id = (size_t) private_data;
    driver_nfs_task_t task = DynamicArr__get(nfs_s.tasks, id);
    if (err > 0)
    {
        // copy out 
        memcpy(task->buf, data, err);
    }
    
    // return the user's data
    task->callback(err, task->private_data);
    DynamicArr__del(nfs_s.tasks, id);
}

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
            DriverNfs__readCallback, (void *) id)
    ){
        ZF_LOGE("NFS__async call failed");
        DynamicArr__del(nfs_s.tasks, id);
    }
}

/**
 * WRITE()
 * @buf is the sos vaddr that mapped of client buffer
 */
void DriverNfs__writeCallback(
    int err, struct nfs_context * nfs,UNUSED void * data, void * private_data
){
    if (nfs != nfs_s.nfs_context) nfs_s.nfs_context = nfs;
    
    size_t id = (size_t) private_data;
    driver_nfs_task_t task = DynamicArr__get(nfs_s.tasks, id);

    // return the user's data
    task->callback(err, task->private_data);
    DynamicArr__del(nfs_s.tasks, id);
}

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
            nfs_s.nfs_context, (struct nfsfh *)context, len, buf, DriverNfs__writeCallback, (void *) id
    )){
        ZF_LOGE("NFS__async call failed");
        DynamicArr__del(nfs_s.tasks, id);
    }
}


/**
 * CLOSE()
 */
void DriverNfs__closeCallback(
    int err, struct nfs_context * nfs,UNUSED void * data, void * private_data
){
    if (nfs != nfs_s.nfs_context) nfs_s.nfs_context = nfs;
    
    size_t id = (size_t) private_data;
    driver_nfs_task_t task = DynamicArr__get(nfs_s.tasks, id);

    ZF_LOGE_IF(err< 0, "NFS close fail");

    // return the user's data
    task->callback(err, task->private_data);
    DynamicArr__del(nfs_s.tasks, id);
}

void DriverNfs__close(
    void * context, driver_nfs_callback_t cb, void * private_data
){
    struct driver_nfs_task dnt;
    dnt.callback     = cb;
    dnt.private_data = private_data;
    size_t id = DynamicArr__add(nfs_s.tasks, & dnt);
    if (!nfs_close_async(
            nfs_s.nfs_context, (struct nfsfh *)context, 
            DriverNfs__closeCallback, (void *) id
    )){
        ZF_LOGE("NFS__async call failed");
        DynamicArr__del(nfs_s.tasks, id);
    }
}

/**
 * GET_DIR_ENTRY()
 */

void DriverNfs__getDirEntry(
    UNUSED void * context, size_t loc, void * buf, driver_nfs_callback_t cb,
    void * private_data
){
    nfs_seekdir(nfs_s.nfs_context, nfs_s.root,loc);
    struct nfsdirent* entry = nfs_readdir(nfs_s.nfs_context, nfs_s.root);
    nfs_rewinddir(nfs_s.nfs_context, nfs_s.root);

    int ret;
    if (entry) {
        strcpy(buf, entry->name);
        ret = 0;
    } else {
        ret = 1;
    }
    
    //  call the callback directly 
    cb(ret, private_data);
}