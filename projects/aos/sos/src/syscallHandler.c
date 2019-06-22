#include "syscallHandler.h"
#include "drivers/serial.h"
#include "syscallEvents.h"

// basic elements for handleing syscalls 
static cspace_t * rootCspace;
static syscall_handles_t handles[SYSCALL_MAX];

void unimplemented_syscall(UNUSED syscallMessage_t msg){
    printf("Unkown Syscall\n");
}



static void __syscall_write(syscallMessage_t msg){
    // get the sent word 
    size_t len = msg->words[0];

    // upperbound of the data I can get
    if (len > PAGE_SIZE_4K) len = PAGE_SIZE_4K;

    // ret from pico sent 
    // ret might be -1;
    // return the error if any let userlevel deal with error 
    int ret = DriverSerial__write(
        msg -> tcb ->share_buffer_vaddr, len
    );

    /* construct a reply message of length 1 */
    seL4_MessageInfo_t reply_msg = seL4_MessageInfo_new(0, 0, 0, 1);
    /* Set the first (and only) word in the message to 0 */
    seL4_SetMR(0, ret);
    /* Send the reply to the saved reply capability. */
    seL4_Send(msg->replyCap, reply_msg);
}

void syscallHandler__init(cspace_t *cspace){
    rootCspace = cspace;
    for (size_t i = 0; i < SYSCALL_MAX; i++){
        // clearup the handles space 
        handles[i] = unimplemented_syscall;
    }
    
    /* Register implemented syscalls here */
    handles[SOS_WRITE] = __syscall_write;
}

void syscallHandler__handle(uint64_t syscall_num, syscallMessage_t msg){
    handles[syscall_num](msg);
    // finish it by calling a callback 
    syscallEvents__finish(msg->event_id);
    if(handles[syscall_num] == unimplemented_syscall){
        printf("This process will halt forever for %lu\n", syscall_num);
    }
    /* Free the slot we allocated for the reply - it is now empty, as the reply
    * capability was consumed by the send. */
    cspace_free_slot(rootCspace, msg->replyCap);
}