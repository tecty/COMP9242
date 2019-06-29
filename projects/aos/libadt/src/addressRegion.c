#include <adt/addressRegion.h>
// not required 
#include <assert.h>

struct AddressRegion_Region_s
{
    enum addressRegionTypes_e type;
    uint64_t start; 
    uint64_t size; 
};


struct AddressRegion_s
{
    struct AddressRegion_Region_s * regions;
    struct AddressRegion_Region_s * stack_region;
    uint16_t size; 
    uint16_t occupied;
};


AddressRegion_t AddressRegion__init(){
    AddressRegion_t ret = malloc(sizeof(struct AddressRegion_s));
    ret->size           = 8;
    ret->occupied       = 0;
    ret->regions = malloc(
        ret->size * sizeof(struct AddressRegion_Region_s)
    );
    // zero out the space 
    memset(
        ret->regions, 0, 
        ret->size * sizeof(struct AddressRegion_Region_s)
    );

    return ret;
}

static inline struct AddressRegion_Region_s * AddressRegion__currRegion(
    AddressRegion_t ar
){
   return & ar->regions[ar->occupied];
}

/**
 * @ret: the id to the inside managed strcuture 
 */
void AddressRegion__declare(
    AddressRegion_t ar,enum addressRegionTypes_e type,
    void * start, uint64_t size
){
    if (ar->size == ar->occupied)
    {
        // larger the array
        ar->regions = realloc(
            ar->regions, 
            ar->size * 2 * sizeof(struct AddressRegion_Region_s)
        );
        memset(
            & ar->regions[ar->size], 0,
            ar->size* sizeof(struct AddressRegion_Region_s) 
        );
        ar->size *= 2;
    }
    AddressRegion__currRegion(ar)->size  = size;
    AddressRegion__currRegion(ar)->type  = type;
    if (type == STACK)
    {
        // stack is grow downwards
        AddressRegion__currRegion(ar)->start = ((uint64_t)start) - size;
    } else {
        AddressRegion__currRegion(ar)->start = ((uint64_t)start);
    }
    
    if (type == STACK)
    {
        ar->stack_region = AddressRegion__currRegion(ar);
    }
    
    ar->occupied ++;
}

enum addressRegionTypes_e AddressRegion__isInRegion(
    AddressRegion_t ar, void * index
){
    struct AddressRegion_Region_s * the_region; 
    // return null to indecate this item is delted 
    for (size_t i = 0; i < ar->occupied; i++)
    {
        the_region = & ar->regions[i];
        // this is how it's in the region
        if(
            the_region->start <= (uint64_t) index &&
            the_region->start + the_region->size >= (uint64_t) index
        ) return the_region->type;
    }
    // not in the region
    return 0;
}


enum addressRegionTypes_e AddressRegion__isInStack(
    AddressRegion_t ar, void * index
){
    // compare with stack
    if(
        ar->stack_region->start <= (uint64_t) index &&
        (
            ar->stack_region->start +
            ar->stack_region->size +
            ALLOW_STACK_MALLOC
        ) >= (uint64_t) index
    ) return ar->stack_region->type;
    return 0;
}


void AddressRegion__regionAddSize(
    AddressRegion_t ar, enum addressRegionTypes_e type, size_t add_size
){
    // return null to indecate this item is delted 
    struct AddressRegion_Region_s * the_region; 
    // return null to indecate this item is delted 
    for (size_t i = 0; i < ar->occupied; i++)
    {
        the_region = & ar->regions[i];
        // found the region
        if(the_region->type == type) break;
    }
    // the region doesn't found 
    assert(the_region->type == type);
    if (type == STACK)
    {
        the_region->start -= add_size;
    }
    the_region->size += add_size;
}


void AddressRegion__free(AddressRegion_t ar){
    free(ar->regions);
    free(ar);
}