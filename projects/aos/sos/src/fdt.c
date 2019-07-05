#include "vfs.h"

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

vfs_read_t VfsFdt__getReadF(FDT_t fdt, uint64_t fd){
    // TODO: Mode check, Error checking
    return VfsFdt__getIovByFd(fdt, fd)->read_f;
}

vfs_write_t VfsFdt__getWriteF(FDT_t fdt, uint64_t fd){
    // TODO: Mode check, Error checking
    // printf ("I have got the fdt_addr is  %p\n", fdt);
    // int64_t oftd = VfsFdt__getOftd(fdt, fd);
    // printf ("I have got the oftd is %ld\n", oftd);
    return VfsFdt__getIovByFd(fdt, fd)->write_f;
}

/**
 * Async Interfaces
 */


void VfsFdt__callback(int64_t err, void * private_data){
    fdt_task_t task = DynamicArr__get(
        Vfs__getFdtTaskArr(), (size_t) private_data
    );

    switch (task->type)
    {
    case OPEN:
        if (err > 0)
        {
            // alloc a fd success 
            err = DynamicArr__add(task->fdt, (void *) err);
        }
        task->cb(err, task->private_data);        
        break;
    default:
        break;
    }
    
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
    task.type = CLOSE;
    size_t id= DynamicArr__add(Vfs__getFdtTaskArr(), &task);

    Vfs__closeAsync(oftd, VfsFdt__callback, (void *) id);
}