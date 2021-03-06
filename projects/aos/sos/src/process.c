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

#define TTY_PRIORITY         (0)
#define TTY_EP_BADGE         (101)

#define ERR_NOT_IN_ADDRESS_SPACE (2)
#define ERR_INVALID_ADDRESS (3)

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

static inline seL4_Word Process__vaddr4kAlign(seL4_Word vaddr){
    return vaddr & (~ ((1<<12)-1) );
}

static inline seL4_Word Process__size4kAlign(seL4_Word vaddr, seL4_Word size){
    // Src: adt/addressRegion
    seL4_Word vaddr_top = vaddr + size;
    // recalculat the actual size i need to map 
    size = vaddr_top - Process__vaddr4kAlign(vaddr);
    seL4_Word mask = ~((1<<12) - 1);
    size = (size + ((1<<12) - 1)) & mask;
    return size;
}


sos_mapped_region_t MappedRegion__init(){
    sos_mapped_region_t ret = malloc(sizeof(struct sos_mapped_region));
    ret->capList = DynamicQ__init(sizeof(seL4_CPtr));
    return ret;
}

void MappedRegion__unmapCapCallback( void * data){
    seL4_CPtr cap =  * (seL4_CPtr *) data;
    seL4_Error err;
    err = seL4_ARM_Page_Unmap(cap);
    ZF_LOGE_IFERR(err, "Fail to unmap cap %ld\n", cap);
    err = cspace_delete(process_s.cspace, cap);
    ZF_LOGE_IFERR(err, "Fail to delete cap %ld in cspace\n", cap);
    cspace_free_slot(process_s.cspace, cap);
}

void MappedRegion__free(sos_mapped_region_t mgt){
    DynamicQ__foreach(mgt->capList, MappedRegion__unmapCapCallback);
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
void * Process__getShareRegion2Start(sos_pcb_t proc){
    return (void *) (
        SOS_SHARE_BUF_START +
        BIT(12) * ContinueRegionRegion__getStart(proc->shareRegion2->crrt)
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
        ZF_LOGE("Unable map in vaddr %lu app", vaddr);
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
    if (frame_d == NULL_FRAME) return ERR_NOT_IN_ADDRESS_SPACE;

    // printf("I have %lu\n", frame_d);
    // protential cspace leak, let the vm to handle this 
    seL4_CPtr cptr = cspace_alloc_slot(process_s.cspace);
    // printf("Map out cptr dest  %ld \n", cptr);
    seL4_Error err = cspace_copy(
        process_s.cspace, cptr, process_s.cspace,
        frame_page(frame_d), seL4_AllRights);
    if (err != seL4_NoError) {
        cspace_free_slot(process_s.cspace, cptr);
        ZF_LOGE("Failed to copy cap");
        return 1;
    }

    err = sos_map_frame(
        process_s.sosUtList,
        process_s.cspace, cptr, seL4_CapInitThreadVSpace,
        (seL4_Word) sos_vaddr, seL4_AllRights,
        seL4_ARM_Default_VMAttributes);
    if (err != seL4_NoError) {
        ZF_LOGE("\n\n\nUnable to map share buff to sos vaddr");
        cspace_delete(process_s.cspace, cptr);
        cspace_free_slot(process_s.cspace, cptr);
        return 1;
    }

    DynamicQ__enQueue(capList, &cptr);
    return 0;
}

void Process__unmapShareRegion(sos_pcb_t proc){
    // NULL ==> the thread hasn't mapped anything into sos
    // Fast path and Null protect 
    if(proc->shareRegion != NULL) {
        MappedRegion__free(proc->shareRegion);
        proc->shareRegion = NULL;
    }
    if (proc->shareRegion2 != NULL){
        MappedRegion__free(proc->shareRegion2);
        proc->shareRegion2 = NULL;
    }
}

void * Process__mapOutShareRegion(sos_pcb_t proc, seL4_Word vaddr, seL4_Word size){
    // protect share region is always one
    if (proc->shareRegion != NULL) Process__unmapShareRegion(proc);
    proc->shareRegion = MappedRegion__init();
    // how much page need to map 
    size = Process__size4kAlign(vaddr,size);
    size >>= 12;
    proc->shareRegion->crrt =
        ContinueRegion__requestRegion(process_s.contRegion, size);

    // ZF_LOGE("I have mapped in %lu pages", size);
    seL4_Word vaddr_aligned = Process__vaddr4kAlign(vaddr);

    for (size_t i = 0; i < size; i++)
    {
        if (
            Process__mapOut(
                proc, vaddr_aligned+(PAGE_SIZE_4K * i),
                (seL4_Word)Process__getShareRegionStart(proc) +(PAGE_SIZE_4K * i), 
                proc->shareRegion->capList
            )!= seL4_NoError
        ) {
            MappedRegion__free(proc->shareRegion);
            return NULL;
        };
    }
    
    proc->share_buffer_vaddr = Process__getShareRegionStart(proc);

    return proc->share_buffer_vaddr + (vaddr & ((1<<12) -1));
}


void * Process__mapOutShareRegion2(
    sos_pcb_t proc, seL4_Word vaddr, seL4_Word size
){
    // protect share region is always one
    if (proc->shareRegion2 != NULL && proc->shareRegion != NULL) {
        Process__unmapShareRegion(proc);
    }
    proc->shareRegion2 = MappedRegion__init();
    size = Process__size4kAlign(vaddr,size);
    // how much page need to map 
    size >>= 12;
    proc->shareRegion2->crrt = 
        ContinueRegion__requestRegion(process_s.contRegion, size);

    seL4_Word vaddr_aligned = Process__vaddr4kAlign(vaddr);

    for (size_t i = 0; i < size; i++)
    {
        if (
            Process__mapOut(
                proc, vaddr_aligned+(PAGE_SIZE_4K * i),
                (seL4_Word)Process__getShareRegion2Start(proc) +(PAGE_SIZE_4K * i), 
                proc->shareRegion2->capList
            )!= seL4_NoError
        ) {
            MappedRegion__free(proc->shareRegion2);
            return NULL;
        };
    }
    
    return Process__getShareRegion2Start(proc) + (vaddr & ((1<<12) -1));
}



void * Process__mapOutShareRegionForce(sos_pcb_t proc, seL4_Word vaddr, seL4_Word size){
    seL4_Word size_aligned = Process__size4kAlign(vaddr,size);
    // how much page need to map 
    size_aligned >>= 12;
    seL4_Word vaddr_aligned = Process__vaddr4kAlign(vaddr);
    for (size_t i = 0; i < size_aligned; i++)
    {
        void * this_vaddr = (void *) (vaddr_aligned + PAGE_SIZE_4K * i );

        if (
            AddressSpace__isInAdddrSpace(proc->addressSpace, this_vaddr)==0 && AddressSpace__tryResize(proc->addressSpace, STACK, this_vaddr) == false
        ){
            ZF_LOGE("Client give a invalid address %p\n", (void *) vaddr);
            return NULL;
        }

        frame_ref_t framd = (frame_ref_t) AddressSpace__getPaddrByVaddr(
            proc->addressSpace, this_vaddr
        );
        if (framd == NULL_FRAME)
        {
            Process__allocMapIn(proc, (seL4_Word) this_vaddr);
        }
    }
    // passthrough
    // @post: all frame is alloced;
    return Process__mapOutShareRegion(proc,vaddr, size);    

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
    sos_pcb_t proc,elf_t *elf_file
){
    /* Create a stack frame */
    /* virtual addresses in the target process' address space */
    uintptr_t stack_top = PROCESS_STACK_TOP;
    uintptr_t stack_bottom = PROCESS_STACK_TOP - PAGE_SIZE_4K;
    /* virtual addresses in the SOS's address space */
    void *local_stack_top  = (seL4_Word *) SOS_SCRATCH;

    /* find the vsyscall table */
    uintptr_t sysinfo = *((uintptr_t *) elf_getSectionNamed(elf_file, "__vsyscall", NULL));
    if (sysinfo == 0) {
        ZF_LOGE("could not find syscall table for c library");
        return 0;
    }
    seL4_Error err = Process__allocMapIn(proc, stack_bottom);
    ZF_LOGE_IFERR(err, "Unable to Map in process stack\n");
    // Map out
    local_stack_top = Process__mapOutShareRegion(proc, stack_bottom, PAGE_SIZE_4K);
    ZF_LOGE_IF(
        local_stack_top == NULL, "Fail to map out the vaddr %p\n", 
        (void *) stack_bottom
    );
    local_stack_top += PAGE_SIZE_4K;

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
    // TODO: Unmap 
    Process__unmapShareRegion(proc);
    return stack_top;
}

static addressSpace_t Process__addrSpaceInit(){
    addressSpace_t ret = AddressSpace__init();
    // we give stak 16 MB
    AddressSpace__declear(ret, STACK, (void *) PROCESS_STACK_TOP, 0x10000000);
    // we give heap 4K (It can grow as required)
    AddressSpace__declear(ret, HEAP, (void *) PROCESS_HEAP_BOTTOM, 0x10000000);
    // I map two page there for current implementation 
    AddressSpace__declear(ret, IPC, (void *) PROCESS_IPC_BUFFER, BIT(13));
    return ret;
}

void Process__declearAddressRegion(
    sos_pcb_t proc, enum addressRegionTypes_e type, seL4_Word vaddr, seL4_Word size
){
    AddressSpace__declear(proc->addressSpace, type, (void *) vaddr, size);
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
    the_proc.shareRegion  = NULL;
    the_proc.shareRegion2 = NULL;


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
    seL4_Word sp = init_process_stack(&the_proc, &elf_file);

    /* load the elf image from the cpio file */
    err = elf_load(&the_proc, process_s.cspace, the_proc.vspace, &elf_file);
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
    void * ret = Process__mapOutShareRegion(
        &the_proc, PROCESS_IPC_BUFFER + PAGE_SIZE_4K, PAGE_SIZE_4K
    );
    ZF_LOGF_IF(ret == NULL, "Fail to map the share region to sos\n");


    /**
     * Process routeine
     */
    the_proc.fdt = VfsFdt__init();
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
    printf("ipc_buffer: %ld\n", proc->ipc_buffer);
    printf("share_buffer_vaddr: %p\n", proc->share_buffer_vaddr);
    printf("cspace: %p\n",  (void *) & proc->cspace);
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


/**
 * VM managemanagements
 */
void Process__VMfaultHandler(seL4_MessageInfo_t message,uint64_t badge){
    sos_pcb_t proc = Process__getPcbByBadage(badge);
    // printf("I need to process vmfault\n");
    if (proc != NULL)
    {
        /* Map in one frame into the addresspace */
        seL4_Fault_t fault = seL4_getFault(message);
        seL4_Word vaddr = seL4_Fault_VMFault_get_Addr(fault);
        
        // printf("It fault at %p\n", (void *) vaddr);
        // printf("I need map to %p\n", (void *) Process__vaddr4kAlign(vaddr));
        
        // not premitted region
        if (!AddressSpace__tryResize(proc->addressSpace, STACK, (void *) vaddr)){
            ZF_LOGE(
                "The vaddr is invalid in address space Vaddr:%p", (void *)vaddr
            );
            Process_dumpPcbByBadge(badge);
            return ;
        }

        seL4_Error err = Process__allocMapIn(proc, Process__vaddr4kAlign(vaddr));
        if (err != seL4_NoError) return;
        
        seL4_TCB_Resume(proc->tcb);
    }else
    {
        printf("I couldn't found thread %lu\n", badge);
    }
}

bool Proccess__increaseHeap(sos_pcb_t proc, void * vaddr){
    // I know it's the stack, for this syscall
    return AddressSpace__tryResize(proc->addressSpace, HEAP, vaddr);
}