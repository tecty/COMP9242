#if !defined(ADDRESS_REGION_H)
#define ADDRESS_REGION_H

#include <string.h>
#include <stdlib.h>
#include <stdint.h>
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

enum addressRegionTypes_e AddressRegion__isInStack(
    AddressRegion_t ar, void * index
);

void AddressRegion__regionAddSize(
    AddressRegion_t ar, enum addressRegionTypes_e type, size_t add_size
);

void AddressRegion__free(AddressRegion_t ar);

#endif // ADDRESS_REGION_H

