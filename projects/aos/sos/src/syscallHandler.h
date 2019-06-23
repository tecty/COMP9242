#if !defined(SYSCALL_HANDLES_H)
#define SYSCALL_HANDLES_H

#include <cspace/cspace.h>
#include <serial/serial.h>
#include "process.h"

/**
 * Circle dependencies, because we want to seperate the handlers and the loop
 * But they are tightly coupled, we couldn't do anything about that
 */ 

#define SYSCALL_MAX 128

struct syscallMessage_s
{
    seL4_CPtr replyCap;
    // first is syscall, so we only need to trap three MR 
    seL4_Word words[3];
    tcb_t tcb;
    uint64_t event_id;
};

typedef struct syscallMessage_s * syscallMessage_t;
typedef void (*syscall_handles_t)(syscallMessage_t msg);

void syscallHandler__init(cspace_t * cspace);
void syscallHandler__handle(uint64_t syscall_num, syscallMessage_t msg);


// /**
//  * How much juice I can get from ipc buff
//  */
// #define IPC_DATA_SIZE (seL4_MsgMaxLength -2) * sizeof(seL4_Word) 


/**
 * Some syscall numbers 
 */

#define SOS_OPEN       1
#define SOS_CLOSE      2
#define SOS_WRITE      3
#define SOS_READ       4
#define SOS_TIMESTAMP  5
#define SOS_US_SLEEP   6



#endif // SYSCALL_HANDLES_H
