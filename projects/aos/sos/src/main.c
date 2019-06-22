/*
 * Copyright 2019, Data61
 * Commonwealth Scientific and Industrial Research Organisation (CSIRO)
 * ABN 41 687 119 230.
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(DATA61_GPL)
 */
#include <autoconf.h>
#include <utils/util.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include <cspace/cspace.h>
#include <aos/sel4_zf_logif.h>
#include <aos/debug.h>

#include <clock/clock.h>
#include <clock/device.h>
#include <cpio/cpio.h>
#include <elf/elf.h>
#include <serial/serial.h>

#include "bootstrap.h"
#include "irq.h"
#include "network.h"
#include "frame_table.h"
#include "drivers/uart.h"
#include "ut.h"
#include "vmem_layout.h"
#include "mapping.h"
#include "elfload.h"
#include "syscalls.h"
#include "tests.h"

#include "drivers/serial.h"
#include "process.h"
#include "syscallHandler.h"
#include "syscallEvents.h"

#include <aos/vsyscall.h>

#include <adt/dynamicQ.h>

// register test 
// #include <tests/clock.h>

/*
 * To differentiate between signals from notification objects and and IPC messages,
 * we assign a badge to the notification object. The badge that we receive will
 * be the bitwise 'OR' of the notification object badge and the badges
 * of all pending IPC messages.
 *
 * All badged IRQs set high bet, then we use uniqe bits to
 * distinguish interrupt sources.
 */
#define IRQ_EP_BADGE         BIT(seL4_BadgeBits - 1ul)
#define IRQ_IDENT_BADGE_BITS MASK(seL4_BadgeBits - 1ul)

#define TTY_NAME             "sosh"
#define TTY_PRIORITY         (0)
#define TTY_EP_BADGE         (101)

/*
 * A dummy starting syscall
 */
#define SOS_SYSCALL0 0



/* The linker will link this symbol to the start address  *
 * of an archive of attached applications.                */
extern char _cpio_archive[];
extern char _cpio_archive_end[];
extern char __eh_frame_start[];
/* provided by gcc */
extern void (__register_frame)(void *);

/* root tasks cspace */
static cspace_t cspace;

/* the one process we start */
static struct tcb tty_test_process;

NORETURN void syscall_loop(seL4_CPtr ep)
{

    while (1) {
        seL4_Word badge = 0;
        /* Block on ep, waiting for an IPC sent over ep, or
         * a notification from our bound notification object */
        seL4_MessageInfo_t message;
        seL4_Word label ;
        
        message = seL4_NBRecv(ep, &badge);
        label = seL4_MessageInfo_get_label(message);
        if (badge == 0)
        {
            /* Block the sos for performance  */
            message = seL4_Recv(ep, &badge);
            label = seL4_MessageInfo_get_label(message);
        }
        
        printf("2I have a badage here %lu\n", badge);
        

        /* Awake! We got a message - check the label and badge to
         * see what the message is about */


        if (badge & IRQ_EP_BADGE) {
            /* It's a notification from our bound notification
             * object! */
            sos_handle_irq_notification(&badge);
        } else if (label == seL4_Fault_NullFault) {
            /* It's not a fault or an interrupt, it must be an IPC
             * message from tty_test! */
            // handle_syscall(badge, seL4_MessageInfo_get_length(message) - 1);
            syscallEvents__enQueue(badge, seL4_MessageInfo_get_length(message) - 1);
        } else {
            /* some kind of fault */
            debug_print_fault(message, TTY_NAME);
            /* dump registers too */
            debug_dump_registers(tty_test_process.tcb);

            ZF_LOGF("The SOS skeleton does not know how to handle faults!");
        }
    }
}

/* helper to allocate a ut + cslot, and retype the ut into the cslot */
static ut_t *alloc_retype(seL4_CPtr *cptr, seL4_Word type, size_t size_bits)
{
    /* Allocate the object */
    ut_t *ut = ut_alloc(size_bits, &cspace);
    if (ut == NULL) {
        ZF_LOGE("No memory for object of size %zu", size_bits);
        return NULL;
    }

    /* allocate a slot to retype the memory for object into */
    *cptr = cspace_alloc_slot(&cspace);
    if (*cptr == seL4_CapNull) {
        ut_free(ut);
        ZF_LOGE("Failed to allocate slot");
        return NULL;
    }

    /* now do the retype */
    seL4_Error err = cspace_untyped_retype(&cspace, ut->cap, *cptr, type, size_bits);
    ZF_LOGE_IFERR(err, "Failed retype untyped");
    if (err != seL4_NoError) {
        ut_free(ut);
        cspace_free_slot(&cspace, *cptr);
        return NULL;
    }

    return ut;
}

static int stack_write(seL4_Word *mapped_stack, int index, uintptr_t val)
{
    mapped_stack[index] = val;
    return index - 1;
}

/* set up System V ABI compliant stack, so that the process can
 * start up and initialise the C library */
static uintptr_t init_process_stack(cspace_t *cspace, seL4_CPtr local_vspace, elf_t *elf_file)
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

    seL4_Error err;

    /* Map the frame to tty's addr and sos's addr  */
    tty_test_process.share_buffer_ut = alloc_retype(
        &tty_test_process.share_buffer, seL4_ARM_SmallPageObject,seL4_PageBits
    );
    if (tty_test_process.share_buffer_ut == NULL) {
        ZF_LOGE("Failed to alloc share buffer ut");
        return false;
    }
    err = map_frame(
        cspace, tty_test_process.share_buffer, tty_test_process.vspace, 
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
        tty_test_process.share_buffer, seL4_AllRights);
    if (err != seL4_NoError) {
        cspace_free_slot(cspace, local_share_buff_cptr);
        ZF_LOGE("Failed to copy cap");
        return 0;
    }

    tty_test_process.share_buffer_vaddr = get_new_share_buff_vaddr();
    err = map_frame(
        cspace, local_share_buff_cptr, local_vspace,
        (seL4_Word) tty_test_process.share_buffer_vaddr, seL4_AllRights,
        seL4_ARM_Default_VMAttributes);
    printf("share Buff vaddr %p\n", tty_test_process.share_buffer_vaddr);
    if (err != seL4_NoError) {
        ZF_LOGE("\n\n\nUnable to map share buff to sos vaddr");
        cspace_delete(cspace, local_share_buff_cptr);
        cspace_free_slot(cspace, local_share_buff_cptr);
        return 0;
    }



    for (size_t i = 0; i < 20; i++)
    {
        seL4_CPtr memcap;

        tty_test_process.stack_ut = alloc_retype(
            &memcap, seL4_ARM_SmallPageObject, seL4_PageBits
        );
        if (tty_test_process.stack_ut == NULL) {
            ZF_LOGE("Failed to allocate stack");
            return 0;
        }

        if (i == 0)
        {
            tty_test_process.stack = memcap;
        }
        stack_bottom -= PAGE_SIZE_4K;
        
        /* Map in the stack frame for the user app */
        err = map_frame(
            cspace, memcap, tty_test_process.vspace, stack_bottom,
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
    err = cspace_copy(cspace, local_stack_cptr, cspace, tty_test_process.stack, seL4_AllRights);
    if (err != seL4_NoError) {
        cspace_free_slot(cspace, local_stack_cptr);
        ZF_LOGE("Failed to copy cap");
        return 0;
    }

    /* map it into the sos address space */
    err = map_frame(cspace, local_stack_cptr, local_vspace, local_stack_bottom, seL4_AllRights,
                    seL4_ARM_Default_VMAttributes);
    if (err != seL4_NoError) {
        cspace_delete(cspace, local_stack_cptr);
        cspace_free_slot(cspace, local_stack_cptr);
        return 0;
    }

    int index = -2;

    /* null terminate the aux vectors */
    index = stack_write(local_stack_top, index, 0);
    index = stack_write(local_stack_top, index, 0);

    /* write the aux vectors */
    index = stack_write(local_stack_top, index, PAGE_SIZE_4K);
    index = stack_write(local_stack_top, index, AT_PAGESZ);

    index = stack_write(local_stack_top, index, sysinfo);
    index = stack_write(local_stack_top, index, AT_SYSINFO);

    /* null terminate the environment pointers */
    index = stack_write(local_stack_top, index, 0);

    /* we don't have any env pointers - skip */

    /* null terminate the argument pointers */
    index = stack_write(local_stack_top, index, 0);

    /* no argpointers - skip */

    /* set argc to 0 */
    stack_write(local_stack_top, index, 0);

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
bool start_first_process(char *app_name, seL4_CPtr ep)
{
    /* Create a VSpace */
    tty_test_process.vspace_ut = alloc_retype(
        &tty_test_process.vspace, seL4_ARM_PageGlobalDirectoryObject,
        seL4_PGDBits
    );
    if (tty_test_process.vspace_ut == NULL) {
        return false;
    }

    /* assign the vspace to an asid pool */
    seL4_Word err = seL4_ARM_ASIDPool_Assign(seL4_CapInitThreadASIDPool, tty_test_process.vspace);
    if (err != seL4_NoError) {
        ZF_LOGE("Failed to assign asid pool");
        return false;
    }

    /* Create a simple 1 level CSpace */
    err = cspace_create_one_level(&cspace, &tty_test_process.cspace);
    if (err != CSPACE_NOERROR) {
        ZF_LOGE("Failed to create cspace");
        return false;
    }

    /* Create an IPC buffer */
    tty_test_process.ipc_buffer_ut = alloc_retype(&tty_test_process.ipc_buffer, seL4_ARM_SmallPageObject,
                                                  seL4_PageBits);
    if (tty_test_process.ipc_buffer_ut == NULL) {
        ZF_LOGE("Failed to alloc ipc buffer ut");
        return false;
    }

    /* allocate a new slot in the target cspace which we will mint a badged endpoint cap into --
     * the badge is used to identify the process, which will come in handy when you have multiple
     * processes. */
    seL4_CPtr user_ep = cspace_alloc_slot(&tty_test_process.cspace);
    if (user_ep == seL4_CapNull) {
        ZF_LOGE("Failed to alloc user ep slot");
        return false;
    }

    /* now mutate the cap, thereby setting the badge */
    err = cspace_mint(&tty_test_process.cspace, user_ep, &cspace, ep, seL4_AllRights, TTY_EP_BADGE);
    if (err) {
        ZF_LOGE("Failed to mint user ep");
        return false;
    }

    /* Create a new TCB object */
    tty_test_process.tcb_ut = alloc_retype(&tty_test_process.tcb, seL4_TCBObject, seL4_TCBBits);
    if (tty_test_process.tcb_ut == NULL) {
        ZF_LOGE("Failed to alloc tcb ut");
        return false;
    }

    /* Configure the TCB */
    err = seL4_TCB_Configure(tty_test_process.tcb, user_ep,
                             tty_test_process.cspace.root_cnode, seL4_NilData,
                             tty_test_process.vspace, seL4_NilData, PROCESS_IPC_BUFFER,
                             tty_test_process.ipc_buffer);
    if (err != seL4_NoError) {
        ZF_LOGE("Unable to configure new TCB");
        return false;
    }

    /* Set the priority */
    err = seL4_TCB_SetPriority(tty_test_process.tcb, seL4_CapInitThreadTCB, TTY_PRIORITY);
    if (err != seL4_NoError) {
        ZF_LOGE("Unable to set priority of new TCB");
        return false;
    }

    /* Provide a name for the thread -- Helpful for debugging */
    NAME_THREAD(tty_test_process.tcb, app_name);

    /* parse the cpio image */
    ZF_LOGI("\nStarting \"%s\"...\n", app_name);
    elf_t elf_file = {};
    unsigned long elf_size;
    size_t cpio_len = _cpio_archive_end - _cpio_archive;
    char *elf_base = cpio_get_file(_cpio_archive, cpio_len, app_name, &elf_size);
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
    seL4_Word sp = init_process_stack(&cspace, seL4_CapInitThreadVSpace, &elf_file);

    /* load the elf image from the cpio file */
    err = elf_load(&cspace, tty_test_process.vspace, &elf_file);
    if (err) {
        ZF_LOGE("Failed to load elf image");
        return false;
    }

    /* Map in the IPC buffer for the thread */
    err = map_frame(&cspace, tty_test_process.ipc_buffer, tty_test_process.vspace, PROCESS_IPC_BUFFER,
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
    printf("Starting ttytest at %p\n", (void *) context.pc);
    err = seL4_TCB_WriteRegisters(tty_test_process.tcb, 1, 0, 2, &context);
    ZF_LOGE_IF(err, "Failed to write registers");
    return err == seL4_NoError;
}

/* Allocate an endpoint and a notification object for sos.
 * Note that these objects will never be freed, so we do not
 * track the allocated ut objects anywhere
 */
static void sos_ipc_init(seL4_CPtr *ipc_ep, seL4_CPtr *ntfn)
{
    /* Create an notification object for interrupts */
    ut_t *ut = alloc_retype(ntfn, seL4_NotificationObject, seL4_NotificationBits);
    ZF_LOGF_IF(!ut, "No memory for notification object");

    /* Bind the notification object to our TCB */
    seL4_Error err = seL4_TCB_BindNotification(seL4_CapInitThreadTCB, *ntfn);
    ZF_LOGF_IFERR(err, "Failed to bind notification object to TCB");

    /* Create an endpoint for user application IPC */
    ut = alloc_retype(ipc_ep, seL4_EndpointObject, seL4_EndpointBits);
    ZF_LOGF_IF(!ut, "No memory for endpoint");
}

/* called by crt */
seL4_CPtr get_seL4_CapInitThreadTCB(void)
{
    return seL4_CapInitThreadTCB;
}

/* tell muslc about our "syscalls", which will bve called by muslc on invocations to the c library */
void init_muslc(void)
{
    muslcsys_install_syscall(__NR_set_tid_address, sys_set_tid_address);
    muslcsys_install_syscall(__NR_writev, sys_writev);
    muslcsys_install_syscall(__NR_exit, sys_exit);
    muslcsys_install_syscall(__NR_rt_sigprocmask, sys_rt_sigprocmask);
    muslcsys_install_syscall(__NR_gettid, sys_gettid);
    muslcsys_install_syscall(__NR_getpid, sys_getpid);
    muslcsys_install_syscall(__NR_tgkill, sys_tgkill);
    muslcsys_install_syscall(__NR_tkill, sys_tkill);
    muslcsys_install_syscall(__NR_exit_group, sys_exit_group);
    muslcsys_install_syscall(__NR_ioctl, sys_ioctl);
    muslcsys_install_syscall(__NR_mmap, sys_mmap);
    muslcsys_install_syscall(__NR_brk,  sys_brk);
    muslcsys_install_syscall(__NR_clock_gettime, sys_clock_gettime);
    muslcsys_install_syscall(__NR_nanosleep, sys_nanosleep);
    muslcsys_install_syscall(__NR_getuid, sys_getuid);
    muslcsys_install_syscall(__NR_getgid, sys_getgid);
    muslcsys_install_syscall(__NR_openat, sys_openat);
    muslcsys_install_syscall(__NR_close, sys_close);
    muslcsys_install_syscall(__NR_socket, sys_socket);
    muslcsys_install_syscall(__NR_bind, sys_bind);
    muslcsys_install_syscall(__NR_listen, sys_listen);
    muslcsys_install_syscall(__NR_connect, sys_connect);
    muslcsys_install_syscall(__NR_accept, sys_accept);
    muslcsys_install_syscall(__NR_sendto, sys_sendto);
    muslcsys_install_syscall(__NR_recvfrom, sys_recvfrom);
    muslcsys_install_syscall(__NR_readv, sys_readv);
    muslcsys_install_syscall(__NR_getsockname, sys_getsockname);
    muslcsys_install_syscall(__NR_getpeername, sys_getpeername);
    muslcsys_install_syscall(__NR_fcntl, sys_fcntl);
    muslcsys_install_syscall(__NR_setsockopt, sys_setsockopt);
    muslcsys_install_syscall(__NR_getsockopt, sys_getsockopt);
    muslcsys_install_syscall(__NR_ppoll, sys_ppoll);
    muslcsys_install_syscall(__NR_madvise, sys_madvise);
}

NORETURN void *main_continued(UNUSED void *arg)
{
    /* Initialise other system compenents here */
    seL4_CPtr ipc_ep, ntfn;
    sos_ipc_init(&ipc_ep, &ntfn);
    sos_init_irq_dispatch(
        &cspace,
        seL4_CapIRQControl,
        ntfn,
        IRQ_EP_BADGE,
        IRQ_IDENT_BADGE_BITS
    );
    frame_table_init(&cspace, seL4_CapInitThreadVSpace);

    /* run sos initialisation tests */
    run_tests(&cspace);

    /* Map the timer device (NOTE: this is the same mapping you will use for your timer driver -
     * sos uses the watchdog timers on this page to implement reset infrastructure & network ticks,
     * so touching the watchdog timers here is not recommended!) */
    void *timer_vaddr = sos_map_device(&cspace, PAGE_ALIGN_4K(TIMER_MAP_BASE), PAGE_SIZE_4K);

    /* Initialise the network hardware. */
    printf("Network init\n");
    network_init(&cspace, timer_vaddr);

    DriverSerial__init();

    /* Initialises the timer */
    printf("Timer init\n");
    start_timer(timer_vaddr);
    /* You will need to register an IRQ handler for the timer here.
     * See "irq.h". */
    seL4_IRQHandler irqhandler;
    seL4_Error error  = sos_register_irq_handler(
        meson_timeout_irq(MESON_TIMER_A), true, timer_irq, MESON_TIMER_A, &irqhandler 
    );
    ZF_LOGF_IF(error, "Fail to register timer A");

    

    /* Start the user application */
    printf("Start first process\n");
    bool success = start_first_process(TTY_NAME, ipc_ep);
    ZF_LOGF_IF(!success, "Failed to start first process");

    
    printf("init the Syscall Message Queue\n");

    // init the syscall table and routine 
    // syscallHandler__init(&cspace);
    syscallEvents__init(&cspace, &tty_test_process);

    printf("\nSOS entering syscall loop\n");
    syscall_loop(ipc_ep);
}
/*
 * Main entry point - called by crt.
 */
int main(void)
{
    init_muslc();

    /* bootinfo was set as an environment variable in _sel4_start */
    char *bi_string = getenv("bootinfo");
    ZF_LOGF_IF(!bi_string, "Could not parse bootinfo from env.");

    /* register the location of the unwind_tables -- this is required for
     * backtrace() to work */
    __register_frame(&__eh_frame_start);

    seL4_BootInfo *boot_info;
    if (sscanf(bi_string, "%p", &boot_info) != 1) {
        ZF_LOGF("bootinfo environment value '%s' was not valid.", bi_string);
    }

    debug_print_bootinfo(boot_info);

    printf("\nSOS Starting...\n");

    NAME_THREAD(seL4_CapInitThreadTCB, "SOS:root");

    /* Initialise the cspace manager, ut manager and dma */
    sos_bootstrap(&cspace, boot_info);

    /* switch to the real uart to output (rather than seL4_DebugPutChar, which only works if the
     * kernel is built with support for printing, and is much slower, as each character print
     * goes via the kernel)
     *
     * NOTE we share this uart with the kernel when the kernel is in debug mode. */
    uart_init(&cspace);
    update_vputchar(uart_putchar);

    /* test print */
    printf("SOS Started!\n");

    /* allocate a bigger stack and switch to it -- we'll also have a guard page, which makes it much
     * easier to detect stack overruns */
    seL4_Word vaddr = SOS_STACK;
    for (int i = 0; i < SOS_STACK_PAGES; i++) {
        seL4_CPtr frame_cap;
        ut_t *frame = alloc_retype(&frame_cap, seL4_ARM_SmallPageObject, seL4_PageBits);
        ZF_LOGF_IF(frame == NULL, "Failed to allocate stack page");
        seL4_Error err = map_frame(&cspace, frame_cap, seL4_CapInitThreadVSpace,
                                   vaddr, seL4_AllRights, seL4_ARM_Default_VMAttributes);
        ZF_LOGF_IFERR(err, "Failed to map stack");
        vaddr += PAGE_SIZE_4K;
    }

    utils_run_on_stack((void *) vaddr, main_continued, NULL);

    UNREACHABLE();
}


