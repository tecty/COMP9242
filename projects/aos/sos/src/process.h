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
    cspace_t cspace;
    seL4_CPtr vspace;

    seL4_CPtr ipc_buffer;

    void* share_buffer_vaddr;

    ut_t *stack_ut;

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
void * Process__mapOutShareRegion(sos_pcb_t proc, seL4_Word vaddr, seL4_Word size);
void * Process__mapOutShareRegionForce(sos_pcb_t proc, seL4_Word vaddr, seL4_Word size);

void Process_dumpPcb(uint32_t pid);
sos_pcb_t Process__getPcbByPid(uint32_t pid);
sos_pcb_t Process__getPcbByBadage(uint64_t badage);
void Process_dumpPcbByBadge(uint64_t badage);
// global share buff addr incrementor
void Process__VMfaultHandler(seL4_MessageInfo_t message,uint64_t badge);
bool Proccess__increaseHeap(sos_pcb_t proc, void * vaddr);

#endif // PROCESS_H
