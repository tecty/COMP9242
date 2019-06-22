#include "serial.h"
#include <adt/dynamicQ.h>
#include <stdio.h>
#include <utils/attribute.h>
#define NULL_BUFF_LEN 1024

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

/* Two types of callbacks */
void null_call_back(struct serial * sptr, int len);
void reply_call_back(struct serial* sptr, int len);


void DriverSerial__init(){
    /* INIT: the basic structure */
    printf("\nInit the serial port\n");
    serial_s.serial_ptr = serial_init();
    serial_s.read_q = DynamicQ__init(sizeof(struct iovec));
}

uint64_t DriverSerial__write(void* buf, uint64_t len){
    return serial_send(serial_s.serial_ptr, (char *) buf, len);
}

char null_buff[NULL_BUFF_LEN];

static inline void try_reg_callback(){
    iovec_t iov = DynamicQ__first(serial_s.read_q) ;
    if (iov == NULL)
    {
        serial_register_handler(
            serial_s.serial_ptr , &null_buff,
            NULL_BUFF_LEN,  null_call_back 
        );
        return;
    }
    // ELSE 
    serial_register_handler(
        serial_s.serial_ptr, iov->buf, iov->len, reply_call_back
    );

}

/* I need deeply modify the libserial to support better performance */
void null_call_back(UNUSED struct serial * sptr, int len){
    // do nothing 
    sptr = NULL;
    len ++;
    return;
}

void reply_call_back(UNUSED struct serial* sptr, int len){
    // iovec_t read_iov = DynamicQ__first(serial_s.read_q);
    DynamicQ__deQueue(serial_s.read_q);
    printf("I have read %d\n",len);
    try_reg_callback();
}

void DriverSerial__read(void * buf, uint64_t len, uint64_t evnet_id){
    struct iovec io;
    io.buf = buf;
    io.len = len;
    io.event_id = evnet_id;    
    DynamicQ__enQueue(serial_s.read_q, &io);
    try_reg_callback();
}