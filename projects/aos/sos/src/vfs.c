#include "vfs.h"

#include "drivers/serial.h"
#include "drivers/nfs.h"
#include "drivers/devnull.h"
#include <stdio.h>

#include <aos/sel4_zf_logif.h>


static struct sos_iovec serial_iov = 
{
    .open_f   = DriverNull__open,
    .close_f  = DriverNull__close,
    .stat_f   = DriverNull__stat,
    .read_f   = DriverSerial__read,
    .write_f  = DriverSerial__write
};

static struct sos_iovec nfs_iov = {
    .open_f   = DriverNfs__open,
    .close_f  = DriverNfs__close,
    .stat_f   = DriverNfs__stat,
    .read_f   = DriverNfs__read,
    .write_f  = DriverNfs__write
};


typedef struct vfs_task
{
    uint64_t oftd;
    vfs_callback_t cb;
    enum vfs_task_type type;
    void * private_data;
} * vfs_task_t;


static struct {
    DynamicArr_t open_file_table;
    DynamicArr_t tasks;
    DynamicArr_t fdt_task;
} vfs_s;


void Vfs__init(){
    vfs_s.open_file_table = DynamicArr__init(sizeof(struct open_file));
    vfs_s.tasks           = DynamicArr__init(sizeof(struct vfs_task));
    vfs_s.fdt_task        = DynamicArr__init(sizeof(struct fdt_task));

    
    // initial my fs
    // DriverNfs__init();
    DriverSerial__init();
}

DynamicArr_t Vfs__getFdtTaskArr(){
    return vfs_s.fdt_task;
}

/**
 * Vfs only need to manage open and close, else could be managed by fdt
 */
int64_t Vfs__open(char * path, uint64_t mode){
    // legacy code, support for syncorniouse proc init
    if (strcmp(path, "console") == 0){
        struct open_file of;
        of.iov = &serial_iov;
        of.mode =mode;
        return DynamicArr__add(vfs_s.open_file_table, &of) + 1 ;
    }
    return 0;
}

void Vfs__close(uint64_t ofd){
    // legacy code, support for syncorniouse proc init
    return DynamicArr__del(vfs_s.open_file_table, ofd-1);
}

/**
 * Async transaction of open and close
 */
void Vfs__callback(int64_t err, void * private_data){
    // ZF_LOGE(
    //     "The vfs task size %lu, oft %lu",
    //     DynamicArr__getAlloced(vfs_s.tasks),
    //     DynamicArr__getAlloced(vfs_s.open_file_table)
    // );

    vfs_task_t task = DynamicArr__get(vfs_s.tasks, (size_t) private_data);
    if (
        (err < 0 && task->type == OPEN) ||
        (task->type == CLOSE && err == 0)
    ){
        // fail the open 
        DynamicArr__del(vfs_s.open_file_table, task-> oftd - 1);
    } else if (err == 0 && task->type == OPEN) {
        // I should return the oftd to fdt
        err = task->oftd;
    } 
    task->cb(err, task->private_data);
    DynamicArr__del(vfs_s.tasks, (size_t) private_data);

}


void Vfs__openAsync(
    char * path, uint64_t flags, vfs_callback_t cb, void * private_data
){
    uint64_t task_id;
    struct vfs_task task;
    task.cb = cb;
    task.type = OPEN;
    task.private_data = private_data;
    open_file_t oft = Dynamic__alloc(vfs_s.open_file_table, &(task.oftd));
    // oftd should increment 1
    task.oftd ++; 

    task_id = DynamicArr__add(vfs_s.tasks, &task);
    oft->mode = flags;
    // call the async open 
    if (strcmp(path, "console") == 0) {
        oft->iov  = &serial_iov;
    } else {
        oft->iov = & nfs_iov;
    }
    oft->iov->open_f(path, flags, &oft->data, Vfs__callback, (void *) task_id);
}

void Vfs__closeAsync(
    uint64_t oftd, vfs_callback_t cb, void * private_data 
){
    uint64_t task_id;
    struct vfs_task task;
    task.cb = cb;
    task.type = CLOSE;
    task.private_data = private_data;
    task.oftd = oftd;
    task_id = DynamicArr__add(vfs_s.tasks, &task);
    // call the call back
    sos_iovec_t iov= Vfs__getIov(oftd);
    if (iov == NULL) {
        Vfs__callback(0, (void *) task_id);
    } else {
        // call the iov to clean up the struct store in oft 
        iov->close_f(
            Vfs__getContextByOftd(oftd), Vfs__callback, (void *) task_id
        );
    }
}

void VfsFdt__statAsync(
    char * path, void * buf, vfs_callback_t cb,
    void * private_data
){
    uint64_t task_id;
    struct vfs_task task;
    task.cb = cb;
    task.type = STAT;
    task.private_data = private_data;
    task_id = DynamicArr__add(vfs_s.tasks, &task);
    // call the call back
    // call the iov to clean up the struct store in oft 
    DriverNfs__stat(path, buf, Vfs__callback, (void *) task_id);
}

void VfsFdt__getDirEntryAsync(
    uint64_t loc, void * buf, uint64_t buf_len, vfs_callback_t cb, 
    void * private_data
){
    uint64_t task_id;
    struct vfs_task task;
    task.cb = cb;
    task.type = GETDIRENT;
    task.private_data = private_data;
    task_id = DynamicArr__add(vfs_s.tasks, &task);
    // printf("DEBUG: Entry try to call the nfs driver \n");
    // call the iov to clean up the struct store in oft 
    DriverNfs__getDirEntry(
        NULL,loc, buf, buf_len, Vfs__callback, (void *) task_id
    );
}

/**
 * Helper functions to decouple the implementations
 */
sos_iovec_t Vfs__getIov(int64_t ofd){
    if (ofd == 0) return NULL;
    open_file_t oft= DynamicArr__get(vfs_s.open_file_table, ofd -1 );
    if (oft == NULL) return NULL;
    // ELSE
    return oft->iov;
}

void * Vfs__getContextByOftd(uint64_t oftd){
    if (oftd == 0) {
        return NULL;
    }
    
    open_file_t oft = DynamicArr__get(vfs_s.open_file_table, oftd - 1);
    return oft->data;
}
