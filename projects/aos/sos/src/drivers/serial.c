#include "serial.h"
#include <adt/dynamicQ.h>
#include <stdio.h>
#include <utils/attribute.h>
#define NULL_BUFF_LEN 1024
char null_buff[NULL_BUFF_LEN];

static struct 
{
    struct serial* serial_ptr;
    DynamicQ_t read_q;
} serial_s;

typedef struct iovec
{
    void* buf;
    uint64_t len;
    void* data;
    vfs_callback_t callback;
} * iovec_t;

/* Two types of callbacks */
void null_call_back(struct serial * sptr, int len);
void reply_call_back(struct serial* sptr, int len);



static inline void try_reg_callback(){
    // printf("I try to register a callback\n");
    iovec_t iov = DynamicQ__first(serial_s.read_q) ;
    if (iov == NULL)
    {

        // printf("I registered a null callback\n");
        serial_register_handler(
            serial_s.serial_ptr , null_buff,
            NULL_BUFF_LEN,  null_call_back 
        );
        return;
    }
    // ELSE 
    // printf("I registered a buff callback\n");

    serial_register_handler(
        serial_s.serial_ptr, iov->buf, iov->len, reply_call_back
    );
}


void DriverSerial__init(){
    /* INIT: the basic structure */
    // printf("\nInit the serial port\n");
    serial_s.serial_ptr = serial_init();
    serial_s.read_q = DynamicQ__init(sizeof(struct iovec));
    try_reg_callback();
}

// uint64_t DriverSerial__write(void* buf, uint64_t len)
void DriverSerial__write(
    UNUSED void * context, void * buf, uint64_t len, vfs_callback_t cb,
    void * private_data
){
    // printf("I want to write %s\n", (char *) buf);
    cb(
        serial_send(serial_s.serial_ptr, (char *) buf, len), private_data
    );
}


/* I need deeply modify the libserial to support better performance */
void null_call_back(UNUSED struct serial * sptr, UNUSED int len){
    null_buff[NULL_BUFF_LEN-1] = '\0';
    // printf("dump to Null: %s", null_buff);
    return;
}

void reply_call_back(UNUSED struct serial* sptr, int len){
    // iovec_t read_iov = DynamicQ__first(serial_s.read_q);
    iovec_t iov =  DynamicQ__first(serial_s.read_q);
    iov->callback(len, iov->data);
    
    /* Destory the datastructure */
    DynamicQ__deQueue(serial_s.read_q);
    // printf("I have read %d\n",len);
    // printf("content %s\n", (char *) iov->buf);
    try_reg_callback();
}

// void DriverSerial__read(
//     void * buf, uint64_t len, void * data, vfs_callback_t callback
// )
void DriverSerial__read(
    UNUSED void * context, void * buf, uint64_t len, vfs_callback_t cb, 
    void * private_data
){
    // printf("\n\nI want to read\n");
    struct iovec io;
    io.buf       = buf;
    io.len       = len;
    io.data      = private_data;
    io.callback  = cb;
    DynamicQ__enQueue(serial_s.read_q, &io);
    try_reg_callback();
}