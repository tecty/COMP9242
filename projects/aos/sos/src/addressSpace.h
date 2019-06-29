#if !defined(ADDRESS_SPACE_H)
#define ADDRESS_SPACE_H

#include <stdlib.h>
#include <adt/dynamicQ.h>
#include <adt/addressRegion.h>


#define PAGE_SLOT (512)

typedef struct addressSpace_s * addressSpace_t;


addressSpace_t AddressSpace__init();
void AddressSpace__mapVaddr(
    addressSpace_t ast, void * paddr, void * vaddr 
);
void *AddressSpace__getPaddrByVaddr(addressSpace_t ast, void * vaddr);
bool AddressSpace__isInAdddrSpace(addressSpace_t ast, void* vaddr);
void AddressSpace__free(addressSpace_t ast);


#endif // ADDRESS_SPACE_H

