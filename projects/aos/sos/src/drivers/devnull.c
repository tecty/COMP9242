#include "devnull.h"
#include <utils/attribute.h>

void DriverNull__init(){
    return;
}

void DriverNull__open(
    UNUSED char * path, UNUSED int flags, UNUSED void * buf,
    vfs_callback_t cb, void * private_data
){
    cb(0, private_data);
}
void DriverNull__close(
    UNUSED void * context, vfs_callback_t cb, void * private_data
){
    cb(0, private_data);
}

void DriverNull__stat(
    UNUSED char * path, UNUSED void * buf, vfs_callback_t cb, 
    void * private_data
){
    cb(0, private_data);
}


void DriverNull__read(
    UNUSED void * context, UNUSED void * buf, UNUSED uint64_t len,
    vfs_callback_t cb, 
    void * private_data
){
    cb(0, private_data);
}

void DriverNull__write(
    UNUSED void * context, UNUSED void * buf, uint64_t len, vfs_callback_t cb,
    void * private_data
){
    cb(len, private_data);
}