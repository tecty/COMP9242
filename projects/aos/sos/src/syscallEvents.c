#include "syscallEvents.h"
#include <stdio.h>
#include "process.h"
/**
 * when event id is 0, that is a virtual event
 * No message is stored in the queue
 */

static struct 
{
    cspace_t *     rootCspace;
    DynamicArr_t messageArr;
    DynamicQ_t   messageQ;
} sysEvent;

struct syscallEvent_s
{
    uint64_t syscallType;
    struct syscallMessage_s msg;
};

typedef struct syscallEvent_s * syscallEvent_t;

void syscallEvents__init(cspace_t * cspace){
    /* Init the DT */
    sysEvent.messageArr  = DynamicArr__init(sizeof(struct syscallEvent_s));
    sysEvent.messageQ    = DynamicQ__init(sizeof(uint64_t));
    sysEvent.rootCspace  = cspace;

    /* Call the handler to init */
    syscallHandler__init(cspace);
}

bool syscallEvents__isEmpty(){
    return DynamicQ__isEmpty(sysEvent.messageQ);
}

static inline syscallEvent_t syscallEvents__first(){
    /* Use the int as pointer again.. */
    // it's nasty, becuase the queue adt doesn't think of the case 
    // the delete is async
    return DynamicArr__get(
        sysEvent.messageArr,
        (*(uint64_t *) DynamicQ__first(sysEvent.messageQ)) -1
    );
}

void syscallEvents__deQueue(){
    // for safety, delete later
    if (syscallEvents__isEmpty()) return;
    // pop and call the handlers
    syscallEvent_t first = syscallEvents__first();
    DynamicQ__deQueue(sysEvent.messageQ);
    syscallHandler__handle(first->syscallType, &first->msg);
}

/* Handle the async deletion of message */
void syscallEvents__finish(syscallMessage_t msg){
    if (unlikely(msg->event_id !=0))
    {
        DynamicArr__del(sysEvent.messageArr, msg->event_id-1);
    }
}

/* Now it just a simple wrapper */
void syscallEvents__enQueue(UNUSED seL4_Word badge, UNUSED int num_args){
    /* allocate a slot for the reply cap */
    seL4_CPtr reply = cspace_alloc_slot(sysEvent.rootCspace);
    seL4_Error err = cspace_save_reply_cap(sysEvent.rootCspace, reply);
    ZF_LOGF_IF(err !=0, "Failed to save reply");

    // construct the unify structure to call to handler 
    struct syscallEvent_s event; 
    event.msg.replyCap = reply;
    event.msg.tcb = Process__getPcbByBadage(badge);
    event.syscallType  = seL4_GetMR(0);
    event.msg.words[0] = seL4_GetMR(1);
    event.msg.words[1] = seL4_GetMR(2);
    event.msg.words[2] = seL4_GetMR(3);

    /* DT enqueue save it for later*/
    size_t event_id = DynamicArr__add(sysEvent.messageArr, &event);
    syscallEvent_t event_ptr = DynamicArr__get(sysEvent.messageArr, event_id );
    event_ptr ->msg.event_id = event_id + 1;
    // printf("I have eid %lu\n", event_ptr ->msg.event_id );
    // now I can use dequeue to run the syscall 
    // never dequeue, dequeue is decided by upper layer
    DynamicQ__enQueue(sysEvent.messageQ, &event_ptr->msg.event_id);
}