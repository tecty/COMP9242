#include "process.h"

static seL4_Word share_buff_curr =  SOS_SHARE_BUF_START;

void * get_new_share_buff_vaddr(){
    seL4_Word ret = share_buff_curr;
    share_buff_curr += PAGE_SIZE_4K;
    return (void *) ret;
}
