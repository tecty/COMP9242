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

static seL4_Word share_buff_curr =  SOS_SHARE_BUF_START;

#define TTY_PRIORITY         (0)
#define TTY_EP_BADGE         (101)

static struct {
    cspace_t * cspace;
    char * cpio_archive;
    char * cpio_archive_end;
} process_s;

void * get_new_share_buff_vaddr(){
    seL4_Word ret = share_buff_curr;
    share_buff_curr += PAGE_SIZE_4K;
    return (void *) ret;
}

void Process__init(
    cspace_t * cspace, char * cpio_archive, char* cpio_archive_end
){
    process_s.cspace           = cspace;
    process_s.cpio_archive     = cpio_archive;
    process_s.cpio_archive_end = cpio_archive_end;
}


/* helper to allocate a ut + cslot, and retype the ut into the cslot */
static ut_t *Process__allocRetype(seL4_CPtr *cptr, seL4_Word type, size_t size_bits)
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

    return ut;
}



static int Process__writeStack(seL4_Word *mapped_stack, int index, uintptr_t val)
{
    mapped_stack[index] = val;
    return index - 1;
}

/* set up System V ABI compliant stack, so that the process can
 * start up and initialise the C library */
static uintptr_t Process__initStack(sos_pcb_t proc, cspace_t *cspace, seL4_CPtr local_vspace, elf_t *elf_file)
{
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
    proc->fdt = vfsFdt__init();
    // printf("\nAllocated fdt at %p\n", proc->fdt);
    seL4_Error err;

    /* Map the frame to tty's addr and sos's addr  */
    proc->share_buffer_ut = Process__allocRetype(
        &proc->share_buffer, seL4_ARM_SmallPageObject,seL4_PageBits
    );
    if (proc->share_buffer_ut == NULL) {
        ZF_LOGE("Failed to alloc share buffer ut");
        return false;
    }
    err = sos_map_frame(
        cspace, proc->share_buffer, proc->vspace, 
        PROCESS_IPC_BUFFER + PAGE_SIZE_4K, seL4_AllRights,
        seL4_ARM_Default_VMAttributes
    );
    if (err != 0) {
        ZF_LOGE("Unable to share buff for user app");
        return 0;
    }
    /* Map the fram into sos addr space */
    // protential cspace leak, let the vm to handle this 
    seL4_CPtr local_share_buff_cptr = cspace_alloc_slot(cspace);

    err = cspace_copy(
        cspace, local_share_buff_cptr, cspace,
        proc->share_buffer, seL4_AllRights);
    if (err != seL4_NoError) {
        cspace_free_slot(cspace, local_share_buff_cptr);
        ZF_LOGE("Failed to copy cap");
        return 0;
    }

    proc->share_buffer_vaddr = get_new_share_buff_vaddr();
    err = sos_map_frame(
        cspace, local_share_buff_cptr, local_vspace,
        (seL4_Word) proc->share_buffer_vaddr, seL4_AllRights,
        seL4_ARM_Default_VMAttributes);
    // printf("share Buff vaddr %p\n", proc->share_buffer_vaddr);
    if (err != seL4_NoError) {
        ZF_LOGE("\n\n\nUnable to map share buff to sos vaddr");
        cspace_delete(cspace, local_share_buff_cptr);
        cspace_free_slot(cspace, local_share_buff_cptr);
        return 0;
    }



    for (size_t i = 0; i < 20; i++)
    {
        seL4_CPtr memcap;

        proc->stack_ut = Process__allocRetype(
            &memcap, seL4_ARM_SmallPageObject, seL4_PageBits
        );
        if (proc->stack_ut == NULL) {
            ZF_LOGE("Failed to allocate stack");
            return 0;
        }

        if (i == 0)
        {
            proc->stack = memcap;
        }
        stack_bottom -= PAGE_SIZE_4K;
        
        /* Map in the stack frame for the user app */
        err = sos_map_frame(
            cspace, memcap, proc->vspace, stack_bottom,
            seL4_AllRights, seL4_ARM_Default_VMAttributes
        );
        if (err != 0) {
            ZF_LOGE("Unable to map stack for user app");
            return 0;
        }
    }
    
    /* allocate a slot to duplicate the stack frame cap so we can map it into our address space */
    seL4_CPtr local_stack_cptr = cspace_alloc_slot(cspace);
    if (local_stack_cptr == seL4_CapNull) {
        ZF_LOGE("Failed to alloc slot for stack");
        return 0;
    }

    /* copy the stack frame cap into the slot */
    err = cspace_copy(cspace, local_stack_cptr, cspace, proc->stack, seL4_AllRights);
    if (err != seL4_NoError) {
        cspace_free_slot(cspace, local_stack_cptr);
        ZF_LOGE("Failed to copy cap");
        return 0;
    }

    /* map it into the sos address space */
    err = sos_map_frame(cspace, local_stack_cptr, local_vspace, local_stack_bottom, seL4_AllRights,
                    seL4_ARM_Default_VMAttributes);
    if (err != seL4_NoError) {
        cspace_delete(cspace, local_stack_cptr);
        cspace_free_slot(cspace, local_stack_cptr);
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
    err = cspace_delete(cspace, local_stack_cptr);
    assert(err == seL4_NoError);

    /* mark the slot as free */
    cspace_free_slot(cspace, local_stack_cptr);

    return stack_top;
}



/* Start the first process, and return true if successful
 *
 * This function will leak memory if the process does not start successfully.
 * TODO: avoid leaking memory once you implement real processes, otherwise a user
 *       can force your OS to run out of memory by creating lots of failed processes.
 */
bool Proccess__startProcess(char *app_name, seL4_CPtr ep)
{
    /* Create a VSpace */
    struct sos_pcb the_process;
    the_process.vspace_ut = Process__allocRetype(
        &the_process.vspace, seL4_ARM_PageGlobalDirectoryObject,
        seL4_PGDBits
    );
    if (the_process.vspace_ut == NULL) {
        return false;
    }

    /* assign the vspace to an asid pool */
    seL4_Word err = seL4_ARM_ASIDPool_Assign(seL4_CapInitThreadASIDPool, the_process.vspace);
    if (err != seL4_NoError) {
        ZF_LOGE("Failed to assign asid pool");
        return false;
    }

    /* Create a simple 1 level CSpace */
    err = cspace_create_one_level(process_s.cspace, &the_process.cspace);
    if (err != CSPACE_NOERROR) {
        ZF_LOGE("Failed to create cspace");
        return false;
    }

    /* Create an IPC buffer */
    frame_ref_t frame = alloc_frame();
    if (frame == NULL_FRAME){
        ZF_LOGE("Fail to map frame to sos\n");
    }
    the_process.ipc_buffer = cspace_alloc_slot(process_s.cspace);
    err = cspace_copy(
        process_s.cspace, the_process.ipc_buffer, frame_table_cspace(),
        frame_page(frame), seL4_AllRights
    );
    if (frame == NULL_FRAME){
        ZF_LOGE("Fail to copy frame cap sos\n");
    }
    

    /* allocate a new slot in the target cspace which we will mint a badged endpoint cap into --
     * the badge is used to identify the process, which will come in handy when you have multiple
     * processes. */
    seL4_CPtr user_ep = cspace_alloc_slot(&the_process.cspace);
    if (user_ep == seL4_CapNull) {
        ZF_LOGE("Failed to alloc user ep slot");
        return false;
    }

    /* now mutate the cap, thereby setting the badge */
    err = cspace_mint(&the_process.cspace, user_ep, process_s.cspace, ep, seL4_AllRights, TTY_EP_BADGE);
    if (err) {
        ZF_LOGE("Failed to mint user ep");
        return false;
    }

    /* Create a new TCB object */
    the_process.tcb_ut = Process__allocRetype(&the_process.tcb, seL4_TCBObject, seL4_TCBBits);
    if (the_process.tcb_ut == NULL) {
        ZF_LOGE("Failed to alloc tcb ut");
        return false;
    }

    /* Configure the TCB */
    err = seL4_TCB_Configure(the_process.tcb, user_ep,
                             the_process.cspace.root_cnode, seL4_NilData,
                             the_process.vspace, seL4_NilData, PROCESS_IPC_BUFFER,
                             the_process.ipc_buffer);
    if (err != seL4_NoError) {
        ZF_LOGE("Unable to configure new TCB");
        return false;
    }

    /* Set the priority */
    err = seL4_TCB_SetPriority(the_process.tcb, seL4_CapInitThreadTCB, TTY_PRIORITY);
    if (err != seL4_NoError) {
        ZF_LOGE("Unable to set priority of new TCB");
        return false;
    }

    /* Provide a name for the thread -- Helpful for debugging */
    NAME_THREAD(the_process.tcb, app_name);

    /* parse the cpio image */
    ZF_LOGI("\nStarting \"%s\"...\n", app_name);
    elf_t elf_file = {};
    unsigned long elf_size;
    size_t cpio_len =process_s.cpio_archive_end -process_s.cpio_archive;
    char *elf_base = cpio_get_file(
        process_s.cpio_archive, cpio_len, app_name, &elf_size
    );
    if (elf_base == NULL) {
        ZF_LOGE("Unable to locate cpio header for %s", app_name);
        return false;
    }
    /* Ensure that the file is an elf file. */
    if (elf_newFile(elf_base, elf_size, &elf_file)) {
        ZF_LOGE("Invalid elf file");
        return -1;
    }

    /* set up the stack */
    seL4_Word sp = Process__initStack(
        &the_process, process_s.cspace, seL4_CapInitThreadVSpace, &elf_file
    );

    /* load the elf image from the cpio file */
    err = elf_load(process_s.cspace, the_process.vspace, &elf_file);
    if (err) {
        ZF_LOGE("Failed to load elf image");
        return false;
    }

    /* Map in the IPC buffer for the thread */
    err = sos_map_frame(process_s.cspace, the_process.ipc_buffer, the_process.vspace, PROCESS_IPC_BUFFER,
                    seL4_AllRights, seL4_ARM_Default_VMAttributes);
    if (err != 0) {
        ZF_LOGE("Unable to map IPC buffer for user app");
        return false;
    }

    /* Start the new process */
    seL4_UserContext context = {
        .pc = elf_getEntryPoint(&elf_file),
        .sp = sp,
    };
    // printf("Starting ttytest at %p\n", (void *) context.pc);
    err = seL4_TCB_WriteRegisters(the_process.tcb, 1, 0, 2, &context);
    ZF_LOGE_IF(err, "Failed to write registers");
    return err == seL4_NoError;
}
