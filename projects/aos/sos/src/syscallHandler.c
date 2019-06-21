#include "syscallHandler.h"

// basic elements for handleing syscalls 
static cspace_t * rootCspace;
static syscall_handles_t handles[SYSCALL_MAX];
static struct serial* serial_ptr;

void unimplemented_syscall(UNUSED syscallMessage_t msg){
    printf("Unkown Syscall\n");
}



static void __syscall_write(syscallMessage_t msg){
    // get the sent word 
    seL4_Word* words   = &(seL4_GetIPCBuffer() -> msg[2]);
    size_t len = seL4_GetMR(1);

    if (len > IPC_DATA_SIZE)
    {
        // upperbound of the data I can get
        len = IPC_DATA_SIZE;
    }
    
    // use device driver to print 
    // return the error if any let userlevel deal with error 
    int ret = serial_send(serial_ptr, (char *) words, len);

    /* construct a reply message of length 1 */
    seL4_MessageInfo_t reply_msg = seL4_MessageInfo_new(0, 0, 0, 1);
    /* Set the first (and only) word in the message to 0 */
    seL4_SetMR(0, ret);
    /* Send the reply to the saved reply capability. */
    seL4_Send(msg->replyCap, reply_msg);


    /* Free the slot we allocated for the reply - it is now empty, as the reply
        * capability was consumed by the send. */
    cspace_free_slot(rootCspace, msg->replyCap);
}



void syscallHandler__init(cspace_t *cspace, struct serial* serial_addr){
    rootCspace = cspace;
    serial_ptr = serial_addr;
    for (size_t i = 0; i < SYSCALL_MAX; i++){
        // clearup the handles space 
        handles[i] = unimplemented_syscall;
    }
    
    /* Register implemented syscalls here */
    handles[SOS_WRITE] = __syscall_write;
}

void syscallHandler__handle(uint64_t syscall_num, syscallMessage_t msg){
    handles[syscall_num](msg);
}