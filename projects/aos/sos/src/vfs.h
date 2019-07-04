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


#endif // VFS_H
