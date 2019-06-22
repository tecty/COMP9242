#include "syscallEvents.h"
#include <stdio.h>

/**
 * when event id is 0, that is a virtual event
 * No message is stored in the queue
 */

static struct 
{
    cspace_t *     rootCspace;
    DynamicArr_t messageArr;
    DynamicQ_t   messageQ;
    // legacy when doing process
    tcb_t        the_process;
} sysEvent;

struct syscallEvent_s
{
    uint64_t syscallType;
    struct syscallMessage_s msg;
};

typedef struct syscallEvent_s * syscallEvent_t;

void syscallEvents__init(cspace_t * cspace, tcb_t the_process){
    /* Init the DT */
    sysEvent.messageArr  = DynamicArr__init(sizeof(struct syscallEvent_s));
    sysEvent.messageQ    = DynamicQ__init(sizeof(uint64_t));
    sysEvent.rootCspace  = cspace;
    sysEvent.the_process = the_process;

    /* Call the handler to init */
    syscallHandler__init(cspace);
}

bool syscallEvents__isEmpty(){
    return DynamicQ__isEmpty(sysEvent.messageQ);
}

static inline syscallEvent_t syscallEvents__first(){
    /* Use the int as pointer again.. */
    return DynamicArr__get(
        sysEvent.messageArr,
        (uint64_t) DynamicQ__first(sysEvent.messageQ)
    );
}

void syscallEvents__deQueue(){
    // for safety, delete later
    if (syscallEvents__isEmpty()) return;
    // call the handlers
    syscallEvent_t first = syscallEvents__first();
    syscallHandler__handle(first->syscallType, &first->msg);
}

/* Handle the async deletion of message */
void syscallEvents__finish(uint64_t event_id){
    if (unlikely(event_id !=0))
    {
        DynamicArr__del(sysEvent.messageArr, event_id-1);
    }
}

/* Now it just a simple wrapper */
void syscallEvents__enQueue(UNUSED seL4_Word badge, UNUSED int num_args){

    /* allocate a slot for the reply cap */
    seL4_CPtr reply = cspace_alloc_slot(sysEvent.rootCspace);
    seL4_Error err = cspace_save_reply_cap(sysEvent.rootCspace, reply);
    ZF_LOGF_IF(err !=0, "Failed to save reply");


    // construct the unify structure to call to handler 
    struct syscallMessage_s msg;
    msg.replyCap = reply;
    msg.tcb = sysEvent.the_process;
    msg.words[0] = seL4_GetMR(1);
    msg.words[1] = seL4_GetMR(2);
    msg.words[2] = seL4_GetMR(3);
    syscallHandler__handle(seL4_GetMR(0), &msg);
}