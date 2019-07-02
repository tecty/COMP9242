#include "syscallHandler.h"
#include "drivers/serial.h"
#include "syscallEvents.h"
#include <clock/clock.h>

// basic elements for handleing syscalls 
static cspace_t * rootCspace;
static syscall_handles_t handles[SYSCALL_MAX];

void unimplemented_syscall(UNUSED syscallMessage_t msg){
    printf("Unkown Syscall\n");
}


static void __syscall_open(syscallMessage_t msg){
    seL4_MessageInfo_t reply_msg = seL4_MessageInfo_new(0, 0, 0, 1);
    // printf("try to invoke the open \n");
    // make sure it won't overflow 
    ((char *)msg->tcb->share_buffer_vaddr)[1023] ='\0';

    seL4_SetMR(
        0,
        vfsFdt__open(msg->tcb->fdt, (char *)msg->tcb->share_buffer_vaddr, msg->words[0])
    );

    // return 
    seL4_Send(msg->replyCap, reply_msg);    
}


static void __syscall_close(syscallMessage_t msg){
    seL4_MessageInfo_t reply_msg = seL4_MessageInfo_new(0, 0, 0, 1);
    // printf("try to invoke the open \n");
    // make sure it won't overflow 
    seL4_SetMR(0, vfsFdt__close(msg->tcb->fdt, msg->words[0]));
    // return 
    seL4_Send(msg->replyCap, reply_msg);    
}



void __syscall_read_callback(uint64_t len, void * data){
    syscallMessage_t msg = (syscallMessage_t) data;
    // ((char * )msg->tcb->share_buffer_vaddr)[len] = '\0';
    // printf("I sent to client %s\n", ((char * )msg->tcb->share_buffer_vaddr));
    seL4_MessageInfo_t reply_msg = seL4_MessageInfo_new(0,0,0,1);
    seL4_SetMR(0, len);
    seL4_Send(msg->replyCap, reply_msg);
    // finish it by calling a callback 
    syscallEvents__finish(msg->event_id);
}

static void __syscall_read(syscallMessage_t msg){
    uint64_t file = msg->words[0];
    uint64_t buf  = msg -> words[1];
    uint64_t len  = msg -> words[2];

    void * sos_buf =Process__mapOutShareRegionForce(msg->tcb, buf, len);
    if (sos_buf == NULL)
    {
        // clean up, return error 
        seL4_MessageInfo_t reply_msg = seL4_MessageInfo_new(0,0,0,1);
        seL4_SetMR(0, -1);
        seL4_Send(msg->replyCap, reply_msg);
        // finish it by calling a callback 
        syscallEvents__finish(msg->event_id);
        return;
    }
    
    // printf("I have got read len %lu\n",len);
    if (len > PAGE_SIZE_4K) len = PAGE_SIZE_4K;
    vfsFdt__getReadF(msg->tcb->fdt, file)(
        msg->tcb->share_buffer_vaddr, len, msg, __syscall_read_callback
    );
}

static void __syscall_write(syscallMessage_t msg){
    // get the sent word 
    size_t file = msg->words[0];
    seL4_Word buf =  msg->words[1];
    size_t len = msg->words[2];

    // upperbound of the data I can get
    if (len > PAGE_SIZE_4K) len = PAGE_SIZE_4K;

    // ret from pico sent 
    // ret might be -1;
    // return the error if any let userlevel deal with error 
    // printf("I want to write at %ld\n",file);
    // printf("I want to print with data in %p\n", (void *) buf);
    // printf("I want to write with %p\n",DriverSerial__write);
    // printf("which i got is %p\n", vfsFdt__getWriteF(msg->tcb->fdt, file));

    void * sos_buf = Process__mapOutShareRegion(msg->tcb, buf, len);
    // printf("sos buf at vaddr %p\n", sos_buf);

    int ret;
    if (sos_buf == NULL)
    {
        // error, client vaddr hasn't found 
        ret = -1;
    } else {
        // function to function pointer ==> use function pointer to call the function
        ret = vfsFdt__getWriteF(msg->tcb->fdt, file)(sos_buf, len);
        // unmap the region for useage in other area
        Process__unmapShareRegion(msg->tcb);
    }
    
    /* construct a reply message of length 1 */
    seL4_MessageInfo_t reply_msg = seL4_MessageInfo_new(0, 0, 0, 1);
    /* Set the first (and only) word in the message to 0 */
    seL4_SetMR(0, ret);
    /* Send the reply to the saved reply capability. */
    seL4_Send(msg->replyCap, reply_msg);
    // finish it by calling a callback 
    syscallEvents__finish(msg->event_id);
}


static void __syscall_timestamp(syscallMessage_t msg){
    /* construct a reply message of length 1 */
    seL4_MessageInfo_t reply_msg = seL4_MessageInfo_new(0, 0, 0, 1);
    /* Set the first (and only) word in the message to 0 */
    seL4_SetMR(0, get_time());
    /* Send the reply to the saved reply capability. */
    seL4_Send(msg->replyCap, reply_msg);
    // finish it by calling a callback 
    syscallEvents__finish(msg->event_id);
}

void us_sleep_callback(UNUSED uint32_t id, void * data ){
    syscallMessage_t msg = data;
    
    // just reply
    seL4_MessageInfo_t reply_msg = seL4_MessageInfo_new(0, 0, 0, 0);
    seL4_Send(msg->replyCap, reply_msg);

    syscallEvents__finish(msg->event_id);
}

static void __syscall_us_sleep(syscallMessage_t msg){
    register_timer(msg->words[0], us_sleep_callback, (void *) msg);
}


static void __syscall_sys_brk(syscallMessage_t msg){
    seL4_Word ret;
    printf("brk has received %ld\n", msg->words[0]);
    // error by return 0  eg. over 122kB
    if (!Proccess__increaseHeap(msg->tcb,(void *) msg->words[0])){
        ret = 0;
    } else {
        ret = msg->words[0];
    }

    seL4_MessageInfo_t reply_msg = seL4_MessageInfo_new(0, 0, 0, 1);
    /* Set the first (and only) word in the message to 0 */
    seL4_SetMR(0, ret);
    /* Send the reply to the saved reply capability. */
    seL4_Send(msg->replyCap, reply_msg);
    // finish it by calling a callback 
    syscallEvents__finish(msg->event_id);
}

void syscallHandler__init(cspace_t *cspace){
    rootCspace = cspace;
    for (size_t i = 0; i < SYSCALL_MAX; i++){
        // clearup the handles space 
        handles[i] = unimplemented_syscall;
    }
    
    /* Register implemented syscalls here */
    handles[SOS_WRITE]     = __syscall_write;
    handles[SOS_OPEN]      = __syscall_open;
    handles[SOS_CLOSE]     = __syscall_close;
    handles[SOS_READ]      = __syscall_read;
    handles[SOS_TIMESTAMP] = __syscall_timestamp;
    handles[SOS_US_SLEEP]  = __syscall_us_sleep;
    handles[SOS_SYS_BRK]   = __syscall_sys_brk;

}

void syscallHandler__handle(uint64_t syscall_num, syscallMessage_t msg){
    handles[syscall_num](msg);

    if(handles[syscall_num] == unimplemented_syscall){
        printf("This process will halt forever for %lu\n", syscall_num);
    }
    /* Free the slot we allocated for the reply - it is now empty, as the reply
    * capability was consumed by the send. */
    cspace_free_slot(rootCspace, msg->replyCap);
}