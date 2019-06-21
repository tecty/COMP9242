#if !defined(PROCESS_H)
#define PROCESS_H

#include <cspace/cspace.h>
#include "ut.h"


struct tcb{
    ut_t *tcb_ut;
    seL4_CPtr tcb;
    ut_t *vspace_ut;
    seL4_CPtr vspace;

    ut_t *ipc_buffer_ut;
    seL4_CPtr ipc_buffer;

    cspace_t cspace;

    ut_t *stack_ut;
    seL4_CPtr stack;
};

typedef struct tcb * tcb_t;

#endif // PROCESS_H

