#if !defined(PROCESS_H)
#define PROCESS_H

#include <cspace/cspace.h>
#include "ut.h"
#include "vmem_layout.h"
#include "vfs.h"
#include "addressSpace.h"

typedef struct sos_pcb * sos_pcb_t;
typedef struct sos_mapped_region * sos_mapped_region_t;



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
    addressSpace_t addressSpace;
    DynamicQ_t utList;
    DynamicQ_t capList;
    DynamicQ_t frameList;
    sos_mapped_region_t shareRegion;
};

void Process__init(
    cspace_t * cspace, char * cpio_archive, char* cpio_archive_end
);
uint32_t Process__startProc(char *app_name, seL4_CPtr ep);

void Process_dumpPcb(uint32_t pid);
sos_pcb_t Process__getPcbByPid(uint32_t pid);
sos_pcb_t Process__getPcbByBadage(uint64_t badage);
void Process_dumpPcbByBadge(uint64_t badage);
void Process__VMfaultHandler(seL4_MessageInfo_t message,uint64_t badge);
// global share buff addr incrementor
void * get_new_share_buff_vaddr();
#endif // PROCESS_H
