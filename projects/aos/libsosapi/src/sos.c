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

#define SOS_OPEN  1
#define SOS_WRITE 2
#define SOS_READ  3

#define SHARE_BUF_VADDR (0xA0001000)
#define PAGE_SIZE_4K (0x1000)


int sos_sys_open(const char *path, fmode_t mode)
{
    seL4_MessageInfo_t msg = seL4_MessageInfo_new(0, 0, 0, 2);
    // call 
    seL4_SetMR(0,SOS_OPEN);
    seL4_SetMR(1,mode);
    strncpy((char *) SHARE_BUF_VADDR,path, 0x1000);
    seL4_Call(1, msg);
    return seL4_GetMR(0);
}

int sos_sys_close(int file)
{
    assert(!"You need to implement this sos_sys_close");
    return -1;
}

int sos_sys_read(int file, char *buf, size_t nbyte)
{
    // printf(" buf addr %p\n", buf);

    seL4_MessageInfo_t msg = seL4_MessageInfo_new(0, 0, 0, 2);
    // call 
    seL4_SetMR(0,SOS_READ);
    seL4_SetMR(1,nbyte);
    seL4_Call(1, msg);
    int64_t read =  seL4_GetMR(0);
    if (read != -1)
    {
        // printf("I'm here %p\n", buf);
        // printf("I'm here %p\n", (void *) SHARE_BUF_VADDR);
        
        memcpy(buf, (void *) SHARE_BUF_VADDR, read);
    }
    
    return read;
}

int sos_sys_write(int file, const char *buf, size_t nbyte)
{
    assert(!"You need to implement this sos_sys_write");
    return -1;
}

int sos_getdirent(int pos, char *name, size_t nbyte)
{
    assert(!"You need to implement this sos_getdirent");
    return -1;
}

int sos_stat(const char *path, sos_stat_t *buf)
{
    assert(!"You need to implement this sos_stat");
    return -1;
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
    assert(!"You need to implement this  sos_sys_usleep");
}

int64_t sos_sys_time_stamp(void)
{
    assert(!"You need to implement this 4_t");
    return -1;
}
