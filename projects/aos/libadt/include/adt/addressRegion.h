#if !defined(ADDRESS_REGION_H)
#define ADDRESS_REGION_H

#include <stdint.h>
#include <stdbool.h>

#define ALLOW_STACK_MALLOC (0x2000)
enum addressRegionTypes_e {
    HEAP = 1, STACK, CODE, SHARE
} ;

typedef struct AddressRegion_s * AddressRegion_t;
AddressRegion_t AddressRegion__init();

void AddressRegion__declare(
    AddressRegion_t ar,enum addressRegionTypes_e type,
    void * start, uint64_t size
);


enum addressRegionTypes_e AddressRegion__isInRegion(
    AddressRegion_t ar, void * index
);
bool AddressRegion__resizeByAddr(
    AddressRegion_t ar, enum addressRegionTypes_e type, void * index
);

void AddressRegion__free(AddressRegion_t ar);

#endif // ADDRESS_REGION_H

