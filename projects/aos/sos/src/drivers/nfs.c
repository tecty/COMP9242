#include "nfs.h"
#include <nfsc/libnfs.h>
#include <adt/dynamic.h>


typedef struct nfs_task {
    void * buf;
    uint64_t buf_len;
    driver_nfs_callback_t callback;
    void * private_data;
}* nfs_task_t;



static struct {
    DynamicArr_t tasks;
    struct nfs_context * nfs_context; 
} nfs_s;


// enable this driver;
void DriverNfs__init(){
    nfs_s.nfs_context = nfs_init_context();
    nfs_s.tasks = DynamicArr__init(sizeof(struct nfs_task));
}
void DriverNfs__free(){
    nfs_destroy_context(nfs_s.nfs_context);
    DynamicArr__free(nfs_s.tasks);
}


/**
 * oft operation, iovec things
 */

void DriverNfs__open(
    char * path, int flags, driver_nfs_callback_t cb, void * private_data
){
    
    
}