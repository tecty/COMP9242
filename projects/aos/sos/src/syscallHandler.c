#include "syscallHandler.h"
#include "drivers/serial.h"
#include "syscallEvents.h"
#include <clock/clock.h>
#include "drivers/sos_stat.h"

#define PATH_SIZE_MAX (1024)

// basic elements for handleing syscalls 
static cspace_t * rootCspace;
static syscall_handles_t handles[SYSCALL_MAX];

void unimplemented_syscall(UNUSED syscallMessage_t msg){
    printf("Unkown Syscall\n");
}


void __syscall_vfs_callback(int64_t len, void * data){
    printf("Debug: vfs try to callback with %ld\n", len);
    printf("callback has been sent %ld\n", len);
    syscallMessage_t msg = (syscallMessage_t) data;
    seL4_MessageInfo_t reply_msg = seL4_MessageInfo_new(0,0,0,1);
    seL4_SetMR(0, len);
    seL4_Send(msg->replyCap, reply_msg);

    // finish it by calling a callback 
    syscallEvents__finish(msg);
    Process__unmapShareRegion(msg->tcb);
}

static void __syscall_open(syscallMessage_t msg){
    char * path = Process__mapOutShareRegion(
        msg->tcb, msg->words[0], PATH_SIZE_MAX
    );

    VfsFdt__openAsync(
        msg->tcb->fdt,path, msg->words[1], __syscall_vfs_callback,msg
    );
}

static void __syscall_close(syscallMessage_t msg){
    // TODO: Debug here, tests never runs here
    VfsFdt__closeAsync(
        msg->tcb->fdt, msg->words[0],__syscall_vfs_callback, msg
    );
}

static void __syscall_read(syscallMessage_t msg){
    uint64_t file = msg->words[0];
    uint64_t buf  = msg -> words[1];
    uint64_t len  = msg -> words[2];

    void * sos_buf =Process__mapOutShareRegionForce(msg->tcb, buf, len);
    if (sos_buf == NULL)
    {
        // clean up, return error 
        __syscall_vfs_callback(-1, msg);
    } else{
        // valid in some sence 
        VfsFdt__readAsync(
            msg->tcb->fdt, file, sos_buf, len, __syscall_vfs_callback, msg
        );
    }
}

static void __syscall_write(syscallMessage_t msg){
    // get the sent word 
    size_t file   = msg->words[0];
    seL4_Word buf = msg->words[1];
    size_t len    = msg->words[2];

    // return the error if any let userlevel deal with error 
    // printf("I want to write at %ld\n",file);
    // printf("I want to print with data in %p\n", (void *) buf);
    // printf("I want to write with %p\n",DriverSerial__write);
    // printf("which i got is %p\n", VfsFdt__getWriteF(msg->tcb->fdt, file));

    void * sos_buf = Process__mapOutShareRegion(msg->tcb, buf, len);
    // printf("sos buf at vaddr %p\n", sos_buf);

    if (sos_buf == NULL){
        __syscall_vfs_callback(-1, msg);
    } else {
        VfsFdt__writeAsync(
            msg->tcb->fdt, file, sos_buf, len, __syscall_vfs_callback, msg
        );
    }
}


static void __syscall_timestamp(syscallMessage_t msg){
    /* construct a reply message of length 1 */
    seL4_MessageInfo_t reply_msg = seL4_MessageInfo_new(0, 0, 0, 1);
    /* Set the first (and only) word in the message to 0 */
    seL4_SetMR(0, get_time());
    /* Send the reply to the saved reply capability. */
    seL4_Send(msg->replyCap, reply_msg);
    // finish it by calling a callback 
    syscallEvents__finish(msg);
}

void us_sleep_callback(UNUSED uint32_t id, void * data ){
    syscallMessage_t msg = data;
    
    // just reply
    seL4_MessageInfo_t reply_msg = seL4_MessageInfo_new(0, 0, 0, 0);
    seL4_Send(msg->replyCap, reply_msg);

    syscallEvents__finish(msg);
}

static void __syscall_us_sleep(syscallMessage_t msg){
    register_timer(msg->words[0], us_sleep_callback, (void *) msg);
}


static void __syscall_sys_brk(syscallMessage_t msg){
    seL4_Word ret;
    // printf("brk has received %ld\n", msg->words[0]);
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
    syscallEvents__finish(msg);
}

static void __syscall_stat(syscallMessage_t msg){
    printf("I have recevie the stat reques\n");

    // get the sent word 
    seL4_Word path = msg->words[0];
    seL4_Word buf  = msg->words[1];

    void * sos_buf  = Process__mapOutShareRegion(
        msg->tcb, buf, sizeof(struct sos_stat)
    );
    void * sos_path = Process__mapOutShareRegion2(msg->tcb, path, 0x1000);
    // printf("sos buf at vaddr %p\n", sos_buf);

    if (sos_buf == NULL || sos_path == NULL){
        __syscall_vfs_callback(-1, msg);
    } else {
        VfsFdt__statAsync(sos_path, sos_buf, __syscall_vfs_callback, msg);
    }
}

static void __syscall_get_dir_ent(syscallMessage_t msg){
    // get the sent word 
    size_t loc     = msg->words[0];
    seL4_Word buf  = msg->words[1];
    size_t buf_len = msg->words[2];

    
    void * sos_buf = Process__mapOutShareRegionForce(msg->tcb, buf, buf_len);
    // printf("DEBUG Entsos buf at vaddr %p\n", sos_buf);
    printf("I got buf, it have%s\n", (char *) sos_buf);


    if (sos_buf == NULL){
        __syscall_vfs_callback(-1, msg);
    } else {
        VfsFdt__getDirEntryAsync(
            loc, sos_buf, buf_len, __syscall_vfs_callback, msg
        );
    }
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
    handles[SOS_STAT]      = __syscall_stat;
    handles[SOS_DIRENT]    = __syscall_get_dir_ent;

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