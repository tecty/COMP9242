#if !defined(DRIVER_NULL_H)
#define DRIVER_NULL_H

#include <stdint.h>
#include "../vfs.h"

/**
 * This is for null device or the null fuction pointer in the iov
 */
void DriverNull__init();

void DriverNull__open(
    char * path, int flags, void * buf,
    vfs_callback_t cb, void * private_data
);
void DriverNull__close(
    void * context, vfs_callback_t cb, void * private_data
);

void DriverNull__stat(
    char * path, void * buf, vfs_callback_t cb, void * private_data
);


void DriverNull__read(
    void * context, void * buf, uint64_t len, vfs_callback_t cb, 
    void * private_data
);
void DriverNull__write(
    void * context, void * buf, uint64_t len, vfs_callback_t cb,
    void * private_data
);


#endif // DRIVER_NULL_H


