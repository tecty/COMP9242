#include "vfs.h"

#include "drivers/serial.h"


typedef struct iovec
{
    vfs_read_t read_f;
    vfs_write_t write_f;
}* iovec_t;


typedef struct open_file
{
    iovec_t iov;
    uint64_t mode;
} * open_file_t;

static struct iovec serial_iov = 
{
    .read_f = DriverSerial__read,
    .write_f = DriverSerial__write
};

static struct {
    DynamicArr_t open_file_table;
} vfs_s;


void vfs__init(){
    vfs_s.open_file_table = DynamicArr__init(sizeof(struct open_file));
}

int64_t vfs__open(char * path, uint64_t mode){
    if (strcmp(path, "console") == 0){
        struct open_file of;
        of.iov = &serial_iov;
        of.mode =mode;
        return DynamicArr__add(vfs_s.open_file_table, &of) + 1 ;
    }
    return 0;
}

iovec_t vfs__getIov(uint64_t ofd){
    if (ofd == 0) return NULL;
    open_file_t oft= DynamicArr__get(vfs_s.open_file_table, ofd);
    if (oft == NULL) return NULL;
    // ELSE
    return oft->iov;
}


void vfs__close(uint64_t ofd){
    return DynamicArr__del(vfs_s.open_file_table, ofd-1);
}

int64_t vfsFdt__open(FDT_t fdt, char * path, uint64_t mode){
    int64_t ofd = vfs__open(path,mode);
    if (ofd > 0) return DynamicArr__add(fdt, &ofd);
    // ELSE 
    return ofd;
}

FDT_t vfsFdt__init(){
    FDT_t fdt = DynamicArr__init(sizeof(uint64_t));
    vfsFdt__open(fdt, "console", 0);
    vfsFdt__open(fdt, "console", 0);
    vfsFdt__open(fdt, "console", 0);
    return fdt;
}

int64_t vfsFdt__getOftd(FDT_t fdt, uint64_t fd){
    return * (int64_t *) DynamicArr__get(fdt, fd);
}

void vfsFdt__close(FDT_t fdt, uint64_t fd){
    int64_t ofd = vfsFdt__getOftd(fdt, fd);
    if( ofd > 0){
        vfs__close(ofd);
        DynamicArr__del(fdt , fd);
    }
}

vfs_read_t vfsFdt__getReadF(FDT_t fdt, uint64_t fd){
    // TODO: Mode check, Error checking
    return vfs__getIov(vfsFdt__getOftd(fdt, fd))->read_f;
}

vfs_write_t vfsFdt__getWriteF(FDT_t fdt, uint64_t fd){
    // TODO: Mode check, Error checking
    return vfs__getIov(vfsFdt__getOftd(fdt, fd))->write_f;
}
