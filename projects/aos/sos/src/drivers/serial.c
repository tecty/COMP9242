#include "serial.h"
#include <adt/dynamicQ.h>
#include <stdio.h>

static struct 
{
    struct serial* serial_ptr;
    DynamicQ_t read_q;
} serial_s;

typedef struct iovec
{
    void* buf;
    uint64_t len;
    uint64_t event_id;
} * iovec_t;

void DriverSerial__init(){
    /* INIT: the basic structure */
    printf("\nInit the serial port\n");
    serial_s.serial_ptr = serial_init();
    serial_s.read_q = DynamicQ__init(sizeof(struct iovec));
}

uint64_t DriverSerial__write(void* buf, uint64_t len){
    return serial_send(serial_s.serial_ptr, (char *) buf, len);
}


/* I need deeply modify the libserial to support better performance */
// void null_call_back(){

// }



// static inline try_reg_callback(){
//     serial_register_handler();
// }

// void DriverSerial__read(void * buf, uint64_t len, uint64_t evnet_id){
//     struct iovec io;
//     io.buf = buf;
//     io.len = len;
//     io.event_id = evnet_id;    
//     DynamicQ__enQueue(serial_s.read_q, io);
// }