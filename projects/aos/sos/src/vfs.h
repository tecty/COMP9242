#if !defined(VFS_H)
#define VFS_H
#include <adt/dynamic.h>

typedef void (* vfs_callback_t)(int64_t len,void * data);

/**
 * Open file table part 
 */
typedef void (* vfs_read_callback_t)(uint64_t len,void * data);
typedef void (* vfs_read_t)( 
    void * buf, uint64_t len, void * data, vfs_read_callback_t callback);
typedef uint64_t (* vfs_write_t)(void* buf, uint64_t len);


void Vfs__init();
int64_t Vfs__open(char * path, uint64_t mode);
void Vfs__close(uint64_t ofd);


/**
 * File descriptor table part 
 */
typedef DynamicArr_t FDT_t;
int64_t VfsFdt__open(FDT_t fdt, char * path, uint64_t mode);
FDT_t VfsFdt__init();
int64_t VfsFdt__getOftd(FDT_t fdt, uint64_t fd);
int64_t VfsFdt__close(FDT_t fdt, uint64_t fd);

vfs_read_t VfsFdt__getReadF(FDT_t fdt, uint64_t fd);
vfs_write_t VfsFdt__getWriteF(FDT_t fdt, uint64_t fd);


/**
 * New async interface (callback hell, maybe I need a Promise LOL)
 */


void Vfs__openAsync(
    char * path, uint64_t flags, vfs_callback_t cb, void * private_data
);
void Vfs__closeAsync(
    uint64_t oftd, vfs_callback_t cb, void * private_data 
);

void VfsFdt__openAsync(
    FDT_t fdt, char * path, uint64_t mode, vfs_callback_t cb, 
    void * private_data
);
void VfsFdt__closeAsync(
    FDT_t fdt, uint64_t fd, vfs_callback_t cb, 
    void * private_data
);
void VfsFdt__readAsync(
    FDT_t fdt, uint64_t fd, void * buf, uint64_t len, vfs_callback_t cb,
    void * private_data
);
void VfsFdt__writeAsync(
    FDT_t fdt, uint64_t fd, void * buf, uint64_t len, vfs_callback_t cb, 
    void * private_data
);

/**
 * This might do it later
 */
void VfsFdt__statAsync(
    FDT_t fdt, void * buf, vfs_callback_t cb, void * private_data
);
void VfsFdt__getDirEntryAsync(
    FDT_t fdt, void * buf, uint64_t loc, vfs_callback_t cb, void * private_data
);

/* Private */
typedef struct sos_iovec
{
    vfs_read_t read_f;
    vfs_write_t write_f;
}* sos_iovec_t;

typedef struct open_file
{
    sos_iovec_t iov;
    uint64_t mode;
    void * data;
} * open_file_t;


enum vfs_task_type {
    OPEN, CLOSE, READ, WRITE, GETDIRENT, STAT
};

typedef struct fdt_task
{
    vfs_callback_t cb;
    enum vfs_task_type type;
    FDT_t fdt;
    size_t fd;
    void * private_data;
} * fdt_task_t;


sos_iovec_t Vfs__getIov(int64_t ofd);
DynamicArr_t Vfs__getFdtTaskArr();


#endif // VFS_H
