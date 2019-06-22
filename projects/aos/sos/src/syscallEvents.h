#if !defined(SYSCALL_EVNETS_H)
#define SYSCALL_EVNETS_H

#include <adt/dynamicQ.h>
#include <adt/dynamic.h>
#include "syscallHandler.h"
void syscallEvents__init(cspace_t * cspace, tcb_t the_process);
bool syscallEvents__isEmpty();
void syscallEvents__deQueue();
void syscallEvents__finish(uint64_t event_id);
void syscallEvents__enQueue(UNUSED seL4_Word badge, UNUSED int num_args);



#endif // SYSCALL_EVNETS_H