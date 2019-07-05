#include "vfs.h"


#include "drivers/serial.h"
#include "drivers/nfs.h"
#include <stdio.h>

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

static struct sos_iovec serial_iov = 
{
    .read_f = DriverSerial__read,
    .write_f = DriverSerial__write
};

static struct {
    DynamicArr_t open_file_table;
} vfs_s;


void Vfs__init(){
    vfs_s.open_file_table = DynamicArr__init(sizeof(struct open_file));
    DriverNfs__init();
}

int64_t Vfs__open(char * path, uint64_t mode){
    if (strcmp(path, "console") == 0){
        struct open_file of;
        of.iov = &serial_iov;
        of.mode =mode;
        return DynamicArr__add(vfs_s.open_file_table, &of) + 1 ;
    }
    return 0;
}

sos_iovec_t Vfs__getIov(int64_t ofd){
    if (ofd == 0) return NULL;
    open_file_t oft= DynamicArr__get(vfs_s.open_file_table, ofd -1 );
    if (oft == NULL) return NULL;
    // ELSE
    return oft->iov;
}


void Vfs__close(uint64_t ofd){
    return DynamicArr__del(vfs_s.open_file_table, ofd-1);
}

int64_t VfsFdt__open(FDT_t fdt, char * path, uint64_t mode){
    int64_t ofd = Vfs__open(path,mode);
    // printf("Open with a result %ld \n",ofd);
    if (ofd > 0) return DynamicArr__add(fdt, &ofd);
    // ELSE 
    return ofd;
}

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

int64_t VfsFdt__close(FDT_t fdt, uint64_t fd){
    int64_t ofd = VfsFdt__getOftd(fdt, fd);
    if( ofd > 0){
        Vfs__close(ofd);
        DynamicArr__del(fdt , fd);
        return 0;
    }
    return -1;
}

vfs_read_t VfsFdt__getReadF(FDT_t fdt, uint64_t fd){
    // TODO: Mode check, Error checking
    return Vfs__getIov(VfsFdt__getOftd(fdt, fd))->read_f;
}

vfs_write_t VfsFdt__getWriteF(FDT_t fdt, uint64_t fd){
    // TODO: Mode check, Error checking
    // printf ("I have got the fdt_addr is  %p\n", fdt);
    int64_t oftd = VfsFdt__getOftd(fdt, fd);
    // printf ("I have got the oftd is %ld\n", oftd);
    return Vfs__getIov(oftd)->write_f;
}
