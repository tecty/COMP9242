#if !defined(SERIAL_DRIVER_H)
#define SERIAL_DRIVER_H

#include <serial/serial.h>
#include <stdint.h>
#include "../vfs.h"

void DriverSerial__init();

// void DriverSerial__open(
//     char * path, int flags, void * buf,
//     driver_nfs_callback_t cb, void * private_data
// );
// void DriverSerial__close(
//     void * context, driver_nfs_callback_t cb, void * private_data
// );

// void DriverSerial__stat(
//     char * path, void * buf, driver_nfs_callback_t cb, void * private_data
// );


void DriverSerial__read(
    void * context, void * buf, uint64_t len, vfs_callback_t cb, 
    void * private_data
);
void DriverSerial__write(
    void * context, void * buf, uint64_t len, vfs_callback_t cb,
    void * private_data
);

#endif // SERIAL_DRIVER_H
