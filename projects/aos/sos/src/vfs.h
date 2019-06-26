#if !defined(VFS_H)
#define VFS_H
#include <adt/dynamic.h>

/**
 * Open file table part 
 */
typedef void (* vfs_read_callback_t)(uint64_t len,void * data);
typedef void (* vfs_read_t)( 
    void * buf, uint64_t len, void * data, vfs_read_callback_t callback);
typedef uint64_t (* vfs_write_t)(void* buf, uint64_t len);


void vfs__init();
int64_t vfs__open(char * path, uint64_t mode);
void vfs__close(uint64_t ofd);


/**
 * File descriptor table part 
 */
typedef DynamicArr_t FDT_t;
int64_t vfsFdt__open(FDT_t fdt, char * path, uint64_t mode);
FDT_t vfsFdt__init();
int64_t vfsFdt__getOftd(FDT_t fdt, uint64_t fd);
int64_t vfsFdt__close(FDT_t fdt, uint64_t fd);

vfs_read_t vfsFdt__getReadF(FDT_t fdt, uint64_t fd);
vfs_write_t vfsFdt__getWriteF(FDT_t fdt, uint64_t fd);


#endif // VFS_H
