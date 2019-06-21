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
/****************************************************************************
 *
 *      $Id:  $
 *
 *      Description: Simple milestone 0 code.
 *                   Libc will need sos_write & sos_read implemented.
 *
 *      Author:      Ben Leslie
 *
 ****************************************************************************/

#include <stdarg.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "ttyout.h"

#include <sel4/sel4.h>

void ttyout_init(void)
{
    /* Perform any initialisation you require here */
}

static size_t sos_debug_print(const void *vData, size_t count)
{
#ifdef CONFIG_DEBUG_BUILD
    size_t i;
    const char *realdata = vData;
    for (i = 0; i < count; i++) {
        seL4_DebugPutChar(realdata[i]);
    }
#endif
    return count;
}

static size_t sos_write_words(seL4_Word * word, size_t len){
    //implement this to use your syscall
    // return sos_debug_print(vData, count);
    int ret = -1; 
    // limit trial 
    size_t trial = 0;
    // sizeof(seL4_Word) * (seL4_MsgMaxLength -2)
    if (len > sizeof(seL4_Word) * (seL4_MsgMaxLength -2)){
        len=  sizeof(seL4_Word) * (seL4_MsgMaxLength -2);
    }

    // how much slot I will occupied besides of headers 
    size_t wrote_slots = 
        (len + sizeof(seL4_Word) -1 ) / sizeof(seL4_Word)  ;

    while ( ret == -1 && trial < 3){
        /* deal with the hardware error in the user-mode */

        seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0,
            // slots will be occupied 
            wrote_slots + 2
        );
        /* Set the first word in the message to 0 */
        seL4_SetMR(0, 1);
        seL4_SetMR(1, len);
        // copy the message into the ipc buffer
        memcpy(
            &(seL4_GetIPCBuffer()->msg[2]), 
            word,
            len
        );

        /* Now send the ipc -- call will send the ipc, then block until a reply
        * message is received */
        seL4_Call(SYSCALL_ENDPOINT_SLOT, tag);
        
        // update value after syscall 
        ret= seL4_GetMR(0);

        trial ++;
    }
    
    // pretend nothing happend if trail > 3 
    // we couldn't do anything 
    return ret;
}



size_t sos_write(void *vData, size_t count)
{
    seL4_Word* words = vData;
    size_t index = 0;
    while( index  < count  ){
        // send it word by word 
        index  += sos_write_words(
            &words[index/sizeof(seL4_Word)],
            // how much bytes remains 
            count - index 
        );
    }
    
    return count;
}



size_t sos_read(void *vData, size_t count)
{
    //implement this to use your syscall
    return 0;
}

