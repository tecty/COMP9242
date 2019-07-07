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
#include <stdarg.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sos.h>

#include <sel4/sel4.h>

/**
 * Some syscall numbers 
 */
#define SOS_OPEN       1
#define SOS_CLOSE      2
#define SOS_WRITE      3
#define SOS_READ       4
#define SOS_TIMESTAMP  5
#define SOS_US_SLEEP   6
#define SOS_SYS_BRK    7
#define SOS_STAT       8
#define SOS_DIRENT     9

#define SHARE_BUF_VADDR       (0xA0001000)
#define PAGE_SIZE_4K          (0x1000)
#define SYSCALL_ENDPOINT_SLOT (1)

int sos_sys_open(const char *path, fmode_t mode)
{
    seL4_MessageInfo_t msg = seL4_MessageInfo_new(0, 0, 0, 3);
    // call 
    seL4_SetMR(0,SOS_OPEN);
    seL4_SetMR(1,(seL4_Word) path);
    seL4_SetMR(2,mode);
    seL4_Call(SYSCALL_ENDPOINT_SLOT, msg);
    // printf("\n\nI have got open %ld\n", seL4_GetMR(0));
    return seL4_GetMR(0);
}

int sos_sys_close(int file)
{
    seL4_MessageInfo_t msg = seL4_MessageInfo_new(0, 0, 0, 2);
    // call 
    seL4_SetMR(0,SOS_CLOSE);
    seL4_SetMR(1,file);
    seL4_Call(SYSCALL_ENDPOINT_SLOT, msg);
    // printf("close %d got %lu\n",file, seL4_GetMR(0));
    return seL4_GetMR(0);
}

int sos_sys_read(int file, char *buf, size_t nbyte)
{
    // printf("I want to read %lu\n", nbyte);
    seL4_MessageInfo_t msg = seL4_MessageInfo_new(0, 0, 0, 4);
    // call 
    seL4_SetMR(0,SOS_READ);
    seL4_SetMR(1,file);
    seL4_SetMR(2,(seL4_Word) buf);
    seL4_SetMR(3,nbyte);
    seL4_Call(SYSCALL_ENDPOINT_SLOT, msg);
    int64_t read =  seL4_GetMR(0);
    
    return read;
}

int sos_sys_write(int file, const char *buf, size_t nbyte)
{
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 4);
    /* Set the first word in the message to 0 */
    seL4_SetMR(0, SOS_WRITE);
    seL4_SetMR(1, file);
    seL4_SetMR(2, (seL4_Word) buf);
    seL4_SetMR(3, nbyte);
    /* Now send the ipc -- call will send the ipc, then block until a reply
    * message is received */
    seL4_Call(SYSCALL_ENDPOINT_SLOT, tag);
    
    // update value after syscall 
    return seL4_GetMR(0);
}

int sos_getdirent(int pos, char *name, size_t nbyte)
{
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 4);
    /* Set the first word in the message to 0 */
    strcpy(name, "hello there");
    seL4_SetMR(0, SOS_DIRENT);
    seL4_SetMR(1, pos);
    seL4_SetMR(2, (seL4_Word) name);
    seL4_SetMR(3, nbyte);
    /* Now send the ipc -- call will send the ipc, then block until a reply
    * message is received */
    printf("I have sent msg %s\n", name);

    seL4_Call(SYSCALL_ENDPOINT_SLOT, tag);
    
    printf("\nI will not reach here\n");
    return seL4_GetMR(0);
}

int sos_stat(const char *path, sos_stat_t *buf)
{
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 2);
    /* Set the first word in the message to 0 */
    seL4_SetMR(0, SOS_STAT);
    seL4_SetMR(1, (seL4_Word) path);
    seL4_SetMR(2, (seL4_Word) buf);
    /* Now send the ipc -- call will send the ipc, then block until a reply
    * message is received */
    seL4_Call(SYSCALL_ENDPOINT_SLOT, tag);
   
    return seL4_GetMR(0);
}

pid_t sos_process_create(const char *path)
{
    assert(!"You need to implement this t");
    return -1;
}

int sos_process_delete(pid_t pid)
{
    assert(!"You need to implement this sos_process_delete");
    return -1;
}

pid_t sos_my_id(void)
{
    assert(!"You need to implement this t");
    return -1;

}

int sos_process_status(sos_process_t *processes, unsigned max)
{
    assert(!"You need to implement this sos_process_status");
    return -1;
}

pid_t sos_process_wait(pid_t pid)
{
    assert(!"You need to implement this t");
    return -1;

}

void sos_sys_usleep(int msec)
{
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 2);
    /* Set the first word in the message to 0 */
    seL4_SetMR(0, SOS_US_SLEEP);
    seL4_SetMR(1, msec*1000);
    seL4_Call(SYSCALL_ENDPOINT_SLOT, tag);
    // update value after syscall 
    return;
}

int64_t sos_sys_time_stamp(void)
{
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 1);
    /* Set the first word in the message to 0 */
    seL4_SetMR(0, SOS_TIMESTAMP);
    seL4_Call(SYSCALL_ENDPOINT_SLOT, tag);
    // printf("I got here %lu\n", seL4_GetMR(0));

    // update value after syscall 
    return  seL4_GetMR(0)*1000;
}
