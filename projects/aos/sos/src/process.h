#if !defined(PROCESS_H)
#define PROCESS_H

#include <cspace/cspace.h>
#include "ut.h"
#include "vmem_layout.h"
#include "vfs.h"

struct sos_pcb{
    ut_t *tcb_ut;
    seL4_CPtr tcb;
    ut_t *vspace_ut;
    seL4_CPtr vspace;

    ut_t *ipc_buffer_ut;
    seL4_CPtr ipc_buffer;

    ut_t *share_buffer_ut;
    seL4_CPtr share_buffer;
    void* share_buffer_vaddr;

    cspace_t cspace;

    ut_t *stack_ut;
    seL4_CPtr stack;

    FDT_t fdt;
};

typedef struct sos_pcb * sos_pcb_t;

void Process__init(
    cspace_t * cspace, char * cpio_archive, char* cpio_archive_end
);
uint32_t Process__startProc(char *app_name, seL4_CPtr ep);

sos_pcb_t Process__getPcbByPid(uint32_t pid);
sos_pcb_t Process__getTheProc();
// global share buff addr incrementor
void * get_new_share_buff_vaddr();
#endif // PROCESS_H
