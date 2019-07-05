#if !defined(DRIVER_NFS_H)
#define DRIVER_NFS_H

#include <stdint.h>
#include "pico_bsd_sockets.h"

void DriverNfs__init();
void DriverNfs__free();

// @ret[err]: errno
// @ret[data]: give in task data  
// pair of call backs 
typedef void (* driver_nfs_callback_t)(int64_t err, void * private_data);

// @buf: the buf to store the nfsfh pointer
void DriverNfs__open(
    char * path, int flags, void * buf,
    driver_nfs_callback_t cb, void * private_data
);
void DriverNfs__close(
    void * context, driver_nfs_callback_t cb, void * private_data
);

void DriverNfs__stat(
    char * path, void * buf, driver_nfs_callback_t cb, void * private_data
);


void DriverNfs__read(
    void * context, void * buf, uint64_t len, driver_nfs_callback_t cb, 
    void * private_data
);
void DriverNfs__write(
    void * context, void * buf, uint64_t len, driver_nfs_callback_t cb,
    void * private_data
);



// not need callbacks, I keep the callback strategy,
// but call the callback directly
void DriverNfs__getDirEntry(
    void * context,size_t loc, void * buf, driver_nfs_callback_t cb,
    void * private_data
);


#endif // DRIVER_NFS_H
