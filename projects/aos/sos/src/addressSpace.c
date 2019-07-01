#include "addressSpace.h"
#include <assert.h>
#include "frame_table.h"

// #define BIT(n) (1ul << (n))
// #define MASK(n) (BIT(n)-1ul)

struct addressSpace_s
{
    uint64_t * rootPageTable;
    DynamicQ_t pageList;
    AddressRegion_t regions;
};


/* Private */
uint64_t * AddressSpace__allocPageTable(addressSpace_t ast){
    // TODO: this map be redifine by frametable 
    frame_ref_t  ref = alloc_frame();
    uint64_t * ret = (void *) frame_data(ref);
    memset(ret, 0, 0x1000);
    // we need the pointer to be recorded 
    DynamicQ__enQueue(ast->pageList , &ref);
    return ret;
}

addressSpace_t AddressSpace__init(){
    addressSpace_t ret = malloc(sizeof(struct addressSpace_s));
    // this need to change the size
    ret->pageList = DynamicQ__init(sizeof(void *));
    ret->rootPageTable = AddressSpace__allocPageTable(ret);
    // init region 
    ret->regions = AddressRegion__init();
    return ret;
}

// private
static inline uint16_t AddressSpace__shiftBits(uint16_t depth){
    return (12 + 9 * depth);
}

static inline uint16_t AddressSpace__getIndexByVaddr(void * vaddr, uint16_t depth){
    depth = 3 - depth;
    // calculate the mask
    uint64_t mask = ((1<<9) -1) << AddressSpace__shiftBits(depth);
    // calculate this index 
    uint64_t this_index = (uint64_t) vaddr;
    this_index &= mask;
    this_index >>= AddressSpace__shiftBits(depth);
    return this_index;
}

void AddressSpace__mapVaddr(
    addressSpace_t ast, void * paddr, void * vaddr 
){
    uint64_t this_index;
    uint64_t * this_pageTable =  ast->rootPageTable;
    // printf("Insert: got vaddr %lu\n", (uint64_t) vaddr);

    for (size_t i = 0; i <= 3; i++)
    {
        // calculate the mask
        this_index = AddressSpace__getIndexByVaddr(vaddr, i);
        // printf("Insert: got Index %lu\n", this_index);

        assert(this_index < PAGE_SLOT);
        if (i ==3)
        {
            // instead of mapping a page table, map a paddr 
            this_pageTable[this_index] = (uint64_t) paddr;
            return;
        }
        if (this_pageTable[this_index] == 0){
            this_pageTable[this_index] = (uint64_t) AddressSpace__allocPageTable(ast);
        }
        // go deep one level 
        this_pageTable = (uint64_t *) this_pageTable[this_index];
    }
}

void *AddressSpace__getPaddrByVaddr(addressSpace_t ast, void * vaddr){
    uint64_t this_index;
    uint64_t * this_pageTable =  ast->rootPageTable;
    // printf("Select: got vaddr %lu\n", (uint64_t) vaddr);


    for (size_t i = 0; i <= 3; i++)
    {
        this_index = AddressSpace__getIndexByVaddr(vaddr, i);
        // printf("Select: got Index %lu\n", this_index);

        assert(this_index < PAGE_SLOT);       
        if (i ==3)
        {
            // instead of mapping a page table, map a paddr 
            // TODO: type change
            return (void *) this_pageTable[this_index];
        }
        if (this_pageTable[this_index] == 0){
            this_pageTable[this_index] = 
                (uint64_t) AddressSpace__allocPageTable(ast);
        }
        // go deep one level 
        this_pageTable = (uint64_t *) this_pageTable[this_index];
    }
    return 0;
}

bool AddressSpace__isInAdddrSpace(addressSpace_t ast, void* vaddr){
    return AddressRegion__isInRegion(ast->regions, vaddr) > 0;
}

bool AddressSpace__tryResize(
    addressSpace_t ast, enum addressRegionTypes_e type, void * vaddr
){
    if (AddressSpace__isInAdddrSpace(ast, vaddr) == true) return true;
    return AddressRegion__resizeByAddr(ast->regions, type, vaddr);
}


void AddressSpace__declear(
    addressSpace_t ast, enum addressRegionTypes_e type,
    void * start, uint64_t size
){
    AddressRegion__declare(ast->regions, type, start, size);
}

seL4_Error AddressSpace__addSize(
    addressSpace_t ast, enum addressRegionTypes_e type, 
    void * new_end
){
    enum addressRegionTypes_e old_type = 
        AddressRegion__isInRegion(ast->regions, new_end);
    if (old_type == 0 || old_type == type){
        // request is avalid 
        return 0;
    }
    // over flow 
    return 1;
}

void AddressSpace__free(addressSpace_t ast){
    // uint64_t * page_addr = 0;
    frame_ref_t * queue_first;

    /* free all the data in the queue */
    while( 
        (queue_first = DynamicQ__first(ast->pageList)) &&
        queue_first != NULL
    ){
        free_frame(* queue_first);
        DynamicQ__deQueue(ast->pageList);
    }

    DynamicQ__free(ast->pageList);
    AddressRegion__free(ast->regions);
    free(ast);
}