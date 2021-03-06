#include "vfs.h"
#include <stdio.h>
#include <aos/sel4_zf_logif.h>
#include <fcntl.h>

FDT_t VfsFdt__init(){
    FDT_t fdt = DynamicArr__init(sizeof(uint64_t));
    VfsFdt__open(fdt, "console", 0);
    VfsFdt__open(fdt, "console", 0);
    VfsFdt__open(fdt, "console", 0);
    return fdt;
}

int64_t VfsFdt__getOftd(FDT_t fdt, uint64_t fd){
    // printf("search %lu, I got %p \n ", fd, DynamicArr__get(fdt, fd));
    return * (int64_t *) DynamicArr__get(fdt, fd);
}

uint16_t VfsFdt__getModeByFd(FDT_t fdt, uint64_t fd){
    uint64_t oftd = VfsFdt__getOftd(fdt, fd);
    // printf("I have got the oftd %lu\n",oftd);
    return Vfs__getModeByOftd(oftd);
}


int64_t VfsFdt__open(FDT_t fdt, char * path, uint64_t mode){
    int64_t ofd = Vfs__open(path,mode);
    // printf("Open with a result %ld \n",ofd);
    if (ofd > 0) return DynamicArr__add(fdt, &ofd);
    // ELSE 
    return ofd;
}

int64_t VfsFdt__close(FDT_t fdt, uint64_t fd){
    int64_t ofd = VfsFdt__getOftd(fdt, fd);
    if( ofd > 0){
        Vfs__close(ofd);
        DynamicArr__del(fdt , fd);
        return 0;
    }
    return -1;
}

sos_iovec_t VfsFdt__getIovByFd(FDT_t fdt, uint64_t fd){
    uint64_t oftd = VfsFdt__getOftd(fdt, fd);
    if( oftd > 0) return Vfs__getIov(oftd);
    return NULL;
}

void * VfsFdt__getContextByFd(FDT_t fdt, uint64_t fd){
    uint64_t oftd = VfsFdt__getOftd(fdt, fd);
    if( oftd > 0) return Vfs__getContextByOftd(oftd);
    return NULL;
}
/**
 * Async Interfaces
 */


void VfsFdt__callback(int64_t err, void * private_data){
    // ZF_LOGI("I have been callbacked");
    fdt_task_t task = DynamicArr__get(
        Vfs__getFdtTaskArr(), (size_t) private_data
    );
    // ZF_LOGE(
    //     "The fdt task size %lu",
    //     DynamicArr__getAlloced(Vfs__getFdtTaskArr())
    // );

    switch (task->type)
    {
    case OPEN:
        if (err > 0) {
            // alloc a fd success 
            err = DynamicArr__add(task->fdt, & err);
        }
        break;
    case CLOSE:
        if (err == 0){
            // delete the slot in the fdt 
            DynamicArr__del(task->fdt, task->fd);
        }
    default:
        break;
    }

    task->cb(err, task->private_data);    
    // delete the task in the array 
    DynamicArr__del(
        Vfs__getFdtTaskArr(), (size_t) private_data
    );

}

void VfsFdt__openAsync(
    FDT_t fdt, char * path, uint64_t mode, vfs_callback_t cb, 
    void * private_data
){
    struct fdt_task task;
    task.cb = cb; 
    task.private_data = private_data;
    task.fdt = fdt;
    task.type = OPEN;
    size_t id= DynamicArr__add(Vfs__getFdtTaskArr(), &task);

    Vfs__openAsync(path, mode, VfsFdt__callback, (void *) id);
}

void VfsFdt__closeAsync(
    FDT_t fdt, uint64_t fd, vfs_callback_t cb, 
    void * private_data
){
    size_t oftd = VfsFdt__getOftd(fdt, fd);
    if (oftd == 0){
        // the fd hasn't open a file yet 
        cb(-1, private_data);
    }

    struct fdt_task task;
    task.cb = cb; 
    task.private_data = private_data;
    task.fdt = fdt;
    task.fd = fd;
    task.type = CLOSE;
    size_t id= DynamicArr__add(Vfs__getFdtTaskArr(), &task);

    Vfs__closeAsync(oftd, VfsFdt__callback, (void *) id);
}

/**
 * We don't need a call back in VFS Layer in these function,
 * Since read/write/stat/dirent won't change the state in VFS layer
 * TODO: change to Async interface
 */
void VfsFdt__readAsync(
    FDT_t fdt, uint64_t fd, void * buf, uint64_t len, vfs_callback_t cb,
    void * private_data
){
    
    sos_iovec_t iov= VfsFdt__getIovByFd(fdt, fd);
    if (iov == NULL|| (VfsFdt__getModeByFd(fdt, fd) & O_ACCMODE) == O_WRONLY ){
        // I couln't call the iov, since there's none
        cb(-1, private_data);
    }
    // construct the task and consumed by unified callback
    struct fdt_task task;
    task.cb = cb; 
    task.private_data = private_data;
    task.fd = fd;
    task.fdt = fdt;
    task.type = READ;
    size_t id= DynamicArr__add(Vfs__getFdtTaskArr(), &task);

    iov->read_f(
        VfsFdt__getContextByFd(fdt, fd),buf, len, VfsFdt__callback, (void *)id
    );
}

void VfsFdt__writeAsync(
    FDT_t fdt, uint64_t fd, void * buf, uint64_t len, vfs_callback_t cb, 
    void * private_data
){
    sos_iovec_t iov= VfsFdt__getIovByFd(fdt, fd);
    if (iov == NULL || (VfsFdt__getModeByFd(fdt, fd) & O_ACCMODE) == O_RDONLY ){
        // I couln't call the iov, since there's none
        // or there's no permission to do so 
        cb(-1, private_data);
    }
    // construct the task and consumed by unified callback
    struct fdt_task task;
    task.cb = cb; 
    task.private_data = private_data;
    task.fd = fd;
    task.fdt = fdt;
    task.type = WRITE;
    size_t id= DynamicArr__add(Vfs__getFdtTaskArr(), &task);

    iov->write_f(
        VfsFdt__getContextByFd(fdt, fd), buf, len, VfsFdt__callback, (void *) id
    );
}
