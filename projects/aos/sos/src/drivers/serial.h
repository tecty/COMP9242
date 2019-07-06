#if !defined(SERIAL_DRIVER_H)
#define SERIAL_DRIVER_H

#include <serial/serial.h>
#include <stdint.h>
#include <stdint.h>
#include "../vfs.h"

void DriverSerial__init();
uint64_t DriverSerial__write(void* buf, uint64_t len);

void DriverSerial__read(
    void * buf, uint64_t len, void * data, vfs_callback_t callback
);

#endif // SERIAL_DRIVER_H
