#include "process.h"

#include "bootstrap.h"
#include <cspace/cspace.h>
#include <aos/sel4_zf_logif.h>
#include <aos/debug.h>

#include <cpio/cpio.h>
#include <elf/elf.h>

#include "elfload.h"
#include "sos_mapping.h"
#include "ut.h"
#include "frame_table.h"
#include "vfs.h"
#include "syscallEvents.h"
#include <adt/contRegion.h>

#include <stdio.h>

static seL4_Word share_buff_curr =  SOS_SHARE_BUF_START;

#define TTY_PRIORITY         (0)
#define TTY_EP_BADGE         (101)

static struct {
    cspace_t * cspace;
    DynamicArr_t procArr;
    char * cpio_archive;
    char * cpio_archive_end;
    DynamicQ_t sosUtList;
    ContinueRegion_t contRegion;
} process_s;

struct sos_mapped_region
{
    DynamicQ_t capList;
    ContinueRegion_Region_t crrt;
};

void * get_new_share_buff_vaddr(){
    seL4_Word ret = share_buff_curr;
    share_buff_curr += PAGE_SIZE_4K;
    return (void *) ret;
}

sos_mapped_region_t MappedRegion__init(){
    sos_mapped_region_t ret = malloc(sizeof(struct sos_mapped_region));
    ret->capList = DynamicQ__init(sizeof(seL4_CPtr));
    return ret;
}

void MappedRegion__free(sos_mapped_region_t mgt){
    // TODO; free all the caps;
    DynamicQ__free(mgt->capList);
    ContinueRegion__release(process_s.contRegion, mgt->crrt);
    free(mgt);
}


void * Process__getShareRegionStart(sos_pcb_t proc){
    return (void *) (
        SOS_SHARE_BUF_START +
        BIT(12) * ContinueRegionRegion__getStart(proc->shareRegion->crrt)
    );
}
uint64_t Process__getShareRegionSize(sos_pcb_t proc){
    return BIT(12) * ContinueRegionRegion__getSize(proc->shareRegion->crrt);
}


void Process__init(
    cspace_t * cspace, char * cpio_archive, char* cpio_archive_end
){
    process_s.cspace           = cspace;
    process_s.procArr          = DynamicArr__init(sizeof(struct sos_pcb));
    process_s.sosUtList        = DynamicQ__init(sizeof(ut_t *));
    process_s.cpio_archive     = cpio_archive;
    process_s.cpio_archive_end = cpio_archive_end;
    process_s.contRegion       = ContinueRegion__init();
}

seL4_CPtr Process__allocFrameCap(sos_pcb_t proc, cspace_t * cspace){
    frame_ref_t frame_d = alloc_frame();
    if (frame_d == NULL_FRAME){
        return seL4_CapNull;
    }
    seL4_CPtr ret = cspace_alloc_slot(cspace);
    
    seL4_Word err = cspace_copy(
        cspace, ret, frame_table_cspace(),
        frame_page(frame_d), seL4_AllRights
    );
    if (err != seL4_NoError){
        // freee the structurs 
        free_frame(frame_d);
        cspace_delete(cspace, ret);
        return seL4_CapNull;    
    }
    // record it 
    DynamicQ__enQueue(proc->capList, &ret);
    DynamicQ__enQueue(proc->frameList, & frame_d);
    return ret;
}

seL4_Error Process__allocMapIn(sos_pcb_t proc, seL4_Word vaddr){
    /* Map the frame to tty's addr and sos's addr  */
    frame_ref_t frame_d = alloc_frame();
    if (frame_d == NULL_FRAME){
        return 1;
    }
    seL4_CPtr cptr = cspace_alloc_slot(process_s.cspace);
    
    seL4_Error err = cspace_copy(
        process_s.cspace, cptr, frame_table_cspace(),
        frame_page(frame_d), seL4_AllRights
    );
    if (err != seL4_NoError){
        // freee the structurs 
        free_frame(frame_d);
        cspace_delete(process_s.cspace, cptr);
        return 1;    
    }
    // ret = ret;
    if (cptr == seL4_CapNull) return 1;
    err = sos_map_frame(
        proc->utList,
        process_s.cspace, cptr, proc->vspace, 
        vaddr, seL4_AllRights,
        seL4_ARM_Default_VMAttributes
    );
    if (err != 0) {
        ZF_LOGE("Unable to share buff for user app");
        return 1;
    }
    // record it in address space 
    AddressSpace__mapVaddr(
        proc->addressSpace, (void *) frame_d, (void *) vaddr
    );

    // not leaking the cap and frame I touched 
    DynamicQ__enQueue(proc->capList  , &cptr);
    DynamicQ__enQueue(proc->frameList, &frame_d);

    return 0;
}

seL4_Error Process__mapOut(
    sos_pcb_t proc, seL4_Word vaddr, seL4_Word sos_vaddr, DynamicQ_t capList
){
    /* Map the fram into sos addr space */
    frame_ref_t frame_d =  (frame_ref_t) AddressSpace__getPaddrByVaddr(
        proc->addressSpace, (void *) vaddr
    );
    // printf("I have %lu\n", frame_d);
    // protential cspace leak, let the vm to handle this 
    seL4_CPtr cptr = cspace_alloc_slot(process_s.cspace);

    seL4_Error err = cspace_copy(
        process_s.cspace, cptr, process_s.cspace,
        frame_page(frame_d), seL4_AllRights);
    if (err != seL4_NoError) {
        cspace_free_slot(process_s.cspace, cptr);
        ZF_LOGE("Failed to copy cap");
        return 1;
    }

    // proc->share_buffer_vaddr = get_new_share_buff_vaddr();
    err = sos_map_frame(
        process_s.sosUtList,
        process_s.cspace, cptr, seL4_CapInitThreadVSpace,
        (seL4_Word) sos_vaddr, seL4_AllRights,
        seL4_ARM_Default_VMAttributes);
    // printf("share Buff vaddr %p\n", the_proc->share_buffer_vaddr);
    if (err != seL4_NoError) {
        ZF_LOGE("\n\n\nUnable to map share buff to sos vaddr");
        cspace_delete(process_s.cspace, cptr);
        cspace_free_slot(process_s.cspace, cptr);
        return 1;
    }

    DynamicQ__enQueue(capList, &cptr);
    return 0;
}

void * Process__mapOutRegion(sos_pcb_t proc){
    proc->shareRegion = MappedRegion__init();
    proc->shareRegion->crrt = ContinueRegion__requestRegion(process_s.contRegion, 1);

    if (
        Process__mapOut(
            proc, PROCESS_IPC_BUFFER + PAGE_SIZE_4K,
            (seL4_Word)Process__getShareRegionStart(proc), 
            proc->shareRegion->capList
        )!= seL4_NoError
    ) {
        MappedRegion__free(proc->shareRegion);
        return NULL;
    };
    proc->share_buffer_vaddr = Process__getShareRegionStart(proc);
    return proc->share_buffer_vaddr;
}


/* helper to allocate a ut + cslot, and retype the ut into the cslot */
static ut_t *Process__allocRetype(sos_pcb_t proc, seL4_CPtr *cptr, seL4_Word type, size_t size_bits)
{
    /* Allocate the object */
    ut_t *ut = ut_alloc(size_bits, process_s.cspace);
    if (ut == NULL) {
        ZF_LOGE("No memory for object of size %zu", size_bits);
        return NULL;
    }

    /* allocate a slot to retype the memory for object into */
    *cptr = cspace_alloc_slot(process_s.cspace);
    if (*cptr == seL4_CapNull) {
        ut_free(ut);
        ZF_LOGE("Failed to allocate slot");
        return NULL;
    }

    /* now do the retype */
    seL4_Error err = cspace_untyped_retype(process_s.cspace, ut->cap, *cptr, type, size_bits);
    ZF_LOGE_IFERR(err, "Failed retype untyped");
    if (err != seL4_NoError) {
        ut_free(ut);
        cspace_free_slot(process_s.cspace, *cptr);
        return NULL;
    }
    // fail save: record all the alloced ut 
    DynamicQ__enQueue(proc->utList, & ut);
    return ut;
}

static int Process__writeStack(seL4_Word *mapped_stack, int index, uintptr_t val)
{
    mapped_stack[index] = val;
    return index - 1;
}

/* set up System V ABI compliant stack, so that the process can
 * start up and initialise the C library */
static uintptr_t init_process_stack(
    sos_pcb_t proc, seL4_CPtr local_vspace, elf_t *elf_file
){
    /* Create a stack frame */
    /* virtual addresses in the target process' address space */
    uintptr_t stack_top = PROCESS_STACK_TOP;
    // uintptr_t stack_bottom = PROCESS_STACK_TOP - PAGE_SIZE_4K;
    uintptr_t stack_bottom = PROCESS_STACK_TOP ;
    /* virtual addresses in the SOS's address space */
    void *local_stack_top  = (seL4_Word *) SOS_SCRATCH;
    uintptr_t local_stack_bottom = SOS_SCRATCH - PAGE_SIZE_4K;

    /* find the vsyscall table */
    uintptr_t sysinfo = *((uintptr_t *) elf_getSectionNamed(elf_file, "__vsyscall", NULL));
    if (sysinfo == 0) {
        ZF_LOGE("could not find syscall table for c library");
        return 0;
    }
    // printf("\nAllocated fdt at %p\n", process_s.the_proc->fdt);
    seL4_Error err;

    



    for (size_t i = 0; i < 20; i++)
    {
        seL4_CPtr memcap = Process__allocFrameCap(proc,process_s.cspace);
        if (i == 0)
        {
            proc->stack = memcap;
        }
        stack_bottom -= PAGE_SIZE_4K;
        
        /* Map in the stack frame for the user app */
        err = sos_map_frame(
            proc->utList,
            process_s.cspace, memcap, proc->vspace, stack_bottom,
            seL4_AllRights, seL4_ARM_Default_VMAttributes
        );
        if (err != 0) {
            ZF_LOGE("Unable to map stack for user app");
            return 0;
        }
    }
    
    /* allocate a slot to duplicate the stack frame cap so we can map it into our address space */
    seL4_CPtr local_stack_cptr = cspace_alloc_slot(process_s.cspace);
    if (local_stack_cptr == seL4_CapNull) {
        ZF_LOGE("Failed to alloc slot for stack");
        return 0;
    }

    /* copy the stack frame cap into the slot */
    err = cspace_copy(process_s.cspace, local_stack_cptr, process_s.cspace, proc->stack, seL4_AllRights);
    if (err != seL4_NoError) {
        cspace_free_slot(process_s.cspace, local_stack_cptr);
        ZF_LOGE("Failed to copy cap");
        return 0;
    }

    /* map it into the sos address space */
    err = sos_map_frame(
        process_s.sosUtList,process_s.cspace, local_stack_cptr, local_vspace, local_stack_bottom, seL4_AllRights,
                    seL4_ARM_Default_VMAttributes);
    if (err != seL4_NoError) {
        cspace_delete(process_s.cspace, local_stack_cptr);
        cspace_free_slot(process_s.cspace, local_stack_cptr);
        return 0;
    }

    int index = -2;

    /* null terminate the aux vectors */
    index = Process__writeStack(local_stack_top, index, 0);
    index = Process__writeStack(local_stack_top, index, 0);

    /* write the aux vectors */
    index = Process__writeStack(local_stack_top, index, PAGE_SIZE_4K);
    index = Process__writeStack(local_stack_top, index, AT_PAGESZ);

    index = Process__writeStack(local_stack_top, index, sysinfo);
    index = Process__writeStack(local_stack_top, index, AT_SYSINFO);

    /* null terminate the environment pointers */
    index = Process__writeStack(local_stack_top, index, 0);

    /* we don't have any env pointers - skip */

    /* null terminate the argument pointers */
    index = Process__writeStack(local_stack_top, index, 0);

    /* no argpointers - skip */

    /* set argc to 0 */
    Process__writeStack(local_stack_top, index, 0);

    /* adjust the initial stack top */
    stack_top += (index * sizeof(seL4_Word));

    /* the stack *must* remain aligned to a double word boundary,
     * as GCC assumes this, and horrible bugs occur if this is wrong */
    assert(index % 2 == 0);
    assert(stack_top % (sizeof(seL4_Word) * 2) == 0);

    /* unmap our copy of the stack */
    err = seL4_ARM_Page_Unmap(local_stack_cptr);
    assert(err == seL4_NoError);

    /* delete the copy of the stack frame cap */
    err = cspace_delete(process_s.cspace, local_stack_cptr);
    assert(err == seL4_NoError);

    /* mark the slot as free */
    cspace_free_slot(process_s.cspace, local_stack_cptr);

    return stack_top;
}

static addressSpace_t Process__addrSpaceInit(){
    addressSpace_t ret = AddressSpace__init();
    // we give stak 16 MB
    AddressSpace__declear(ret, STACK, (void *) PROCESS_STACK_TOP, BIT(24));
    // we give heap 4K (It can grow as required)
    AddressSpace__declear(ret, HEAP, (void *) PROCESS_HEAP_BOTTOM, BIT(12));
    return ret;
}

// seL4_Error Process__increaseHeap(sos_pcb_t proc, void * top){
//     addrspace
// }

/* Start the first process, and return true if successful
 *
 * This function will leak memory if the process does not start successfully.
 * TODO: avoid leaking memory once you implement real processes, otherwise a user
 *       can force your OS to run out of memory by creating lots of failed processes.
 */
uint32_t Process__startProc(char *app_name, seL4_CPtr ep)
{
    /* Create a VSpace */
    struct sos_pcb the_proc;
    the_proc.utList       = DynamicQ__init(sizeof(ut_t *));
    the_proc.capList      = DynamicQ__init(sizeof(seL4_CPtr));
    the_proc.frameList    = DynamicQ__init(sizeof(frame_ref_t));
    the_proc.addressSpace = Process__addrSpaceInit();


    the_proc.vspace_ut = Process__allocRetype(
        &the_proc, &the_proc.vspace, seL4_ARM_PageGlobalDirectoryObject,
        seL4_PGDBits
    );
    if (the_proc.vspace_ut == NULL) {
        return 0;
    }

    /* assign the vspace to an asid pool */
    seL4_Word err = seL4_ARM_ASIDPool_Assign(seL4_CapInitThreadASIDPool, the_proc.vspace);
    if (err != seL4_NoError) {
        ZF_LOGE("Failed to assign asid pool");
        return 0;
    }

    /* Create a simple 1 level CSpace */
    err = cspace_create_one_level(process_s.cspace, &the_proc.cspace);
    if (err != CSPACE_NOERROR) {
        ZF_LOGE("Failed to create cspace");
        return 0;
    }

    /* Create an IPC buffer */
    the_proc.ipc_buffer = Process__allocFrameCap(&the_proc, process_s.cspace);
    if(the_proc.ipc_buffer == seL4_CapNull) goto cleanUp;  

    /* allocate a new slot in the target cspace which we will mint a badged endpoint cap into --
     * the badge is used to identify the process, which will come in handy when you have multiple
     * processes. */
    seL4_CPtr user_ep = cspace_alloc_slot(&the_proc.cspace);
    if (user_ep == seL4_CapNull) {
        ZF_LOGE("Failed to alloc user ep slot");
        return 0;
    }

    /* now mutate the cap, thereby setting the badge */
    err = cspace_mint(&the_proc.cspace, user_ep, process_s.cspace, ep, seL4_AllRights, TTY_EP_BADGE);
    if (err) {
        ZF_LOGE("Failed to mint user ep");
        return 0;
    }

    /* Create a new TCB object */
    the_proc.tcb_ut = Process__allocRetype(
        &the_proc, &the_proc.tcb, seL4_TCBObject, seL4_TCBBits);
    if (the_proc.tcb_ut == NULL) {
        ZF_LOGE("Failed to alloc tcb ut");
        return 0;
    }

    /* Configure the TCB */
    err = seL4_TCB_Configure(the_proc.tcb, user_ep,
                             the_proc.cspace.root_cnode, seL4_NilData,
                             the_proc.vspace, seL4_NilData, PROCESS_IPC_BUFFER,
                             the_proc.ipc_buffer);
    if (err != seL4_NoError) {
        ZF_LOGE("Unable to configure new TCB");
        return 0;
    }

    /* Set the priority */
    err = seL4_TCB_SetPriority(the_proc.tcb, seL4_CapInitThreadTCB, TTY_PRIORITY);
    if (err != seL4_NoError) {
        ZF_LOGE("Unable to set priority of new TCB");
        return 0;
    }

    /* Provide a name for the thread -- Helpful for debugging */
    NAME_THREAD(the_proc.tcb, app_name);

    /* parse the cpio image */
    ZF_LOGI("\nStarting \"%s\"...\n", app_name);
    elf_t elf_file = {};
    unsigned long elf_size;
    size_t cpio_len = process_s.cpio_archive_end - process_s.cpio_archive;
    char *elf_base = cpio_get_file(process_s.cpio_archive, cpio_len, app_name, &elf_size);
    if (elf_base == NULL) {
        ZF_LOGE("Unable to locate cpio header for %s", app_name);
        return 0;
    }
    /* Ensure that the file is an elf file. */
    if (elf_newFile(elf_base, elf_size, &elf_file)) {
        ZF_LOGE("Invalid elf file");
        return -1;
    }

    /* set up the stack */
    seL4_Word sp = init_process_stack(&the_proc,seL4_CapInitThreadVSpace, &elf_file);

    /* load the elf image from the cpio file */
    err = elf_load(the_proc.utList, process_s.cspace, the_proc.vspace, &elf_file);
    if (err) {
        ZF_LOGE("Failed to load elf image");
        return 0;
    }

    /* Map in the IPC buffer for the thread */
    err = sos_map_frame(
        the_proc.utList,
        process_s.cspace, the_proc.ipc_buffer, the_proc.vspace, PROCESS_IPC_BUFFER,
        seL4_AllRights, seL4_ARM_Default_VMAttributes
    );
    if (err != 0) {
        ZF_LOGE("Unable to map IPC buffer for user app");
        return 0;
    }

    /* Start the new process */
    seL4_UserContext context = {
        .pc = elf_getEntryPoint(&elf_file),
        .sp = sp,
    };
    // printf("Starting ttytest at %p\n", (void *) context.pc);
    err = seL4_TCB_WriteRegisters(the_proc.tcb, 1, 0, 2, &context);
    ZF_LOGE_IF(err, "Failed to write registers");
    if (err != seL4_NoError){
        return 0;
    }

    /**
     * Map In
     */
    err = Process__allocMapIn(&the_proc, PROCESS_IPC_BUFFER + PAGE_SIZE_4K);
    ZF_LOGE_IFERR( err, "Fail to mapin share buff\n");
    /**
     * Map out 
     */
    void * ret = Process__mapOutRegion(&the_proc);
    ZF_LOGF_IF(ret == NULL, "Fail to map the share region to sos\n");

    /**
     * Process routeine
     */
    the_proc.fdt = vfsFdt__init();
    return DynamicArr__add(process_s.procArr, &the_proc) + 1;

    cleanUp: 
    // TODO: add clean up code. modify the dynamicQ
    return 0;
}


void Process_dumpPcb(uint32_t pid){
    sos_pcb_t proc = DynamicArr__get(process_s.procArr, pid - 1);
    printf("tcb_ut: %p\n", proc->tcb_ut);
    printf("tcb: %ld\n", proc->tcb);
    printf("vspace_ut: %p\n", proc->vspace_ut);
    printf("vspace: %ld\n", proc->vspace);
    printf("ipc_buffer_ut: %p\n", proc->ipc_buffer_ut);
    printf("ipc_buffer: %ld\n", proc->ipc_buffer);
    printf("share_buffer_ut: %p\n", proc->share_buffer_ut);
    printf("share_buffer: %ld\n", proc->share_buffer);
    printf("share_buffer_vaddr: %p\n", proc->share_buffer_vaddr);
    printf("cspace: %p\n",  (void *) & proc->cspace);
    printf("stack_ut: %p\n", proc->stack_ut);
    printf("stack: %ld\n", proc->stack);
    printf("fdt: %p\n", proc->fdt);
    debug_dump_registers(proc->tcb);
}

sos_pcb_t Process__getPcbByPid(uint32_t pid){
    sos_pcb_t ret = DynamicArr__get(process_s.procArr, pid - 1);
    // Process_dumpPcb(pid);
    return ret;
}

sos_pcb_t Process__getPcbByBadage(uint64_t badage){
    return Process__getPcbByPid(badage - 100);
}
void Process_dumpPcbByBadge(uint64_t badage){
    Process_dumpPcb(badage - 100);
}