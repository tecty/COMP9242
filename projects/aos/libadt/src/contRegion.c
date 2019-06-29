#include <adt/contRegion.h>

#define DEFAULT_SIZE 4
// #define DEFAULT_SIZE 16

struct ContinueRegion_s
{
    bool* occupied;
    uint64_t size;
    uint64_t alloced;
    uint64_t last_index; 
};

/**
 * A small read-only tuple
 */
struct ContinueRegion_Region_s {
    uint64_t start;
    uint64_t size;
} ;

uint64_t ContinueRegionRegion__getStart(ContinueRegion_Region_t crrt){
    return crrt->start;
}

uint64_t ContinueRegionRegion__getSize(ContinueRegion_Region_t crrt){
    return crrt->size;
}


static inline void ContinueRegion__modIndex(
    ContinueRegion_t cr, uint64_t * index
){
    *index  = (*index) % cr->size;
}

static inline void ContinueRegion__incIndex(
    ContinueRegion_t cr, uint64_t * index
){
    (*index) ++;
    ContinueRegion__modIndex(cr, index); 
}


ContinueRegion_t ContinueRegion__init(){
    ContinueRegion_t cr = malloc(sizeof(struct ContinueRegion_s));
    cr->size            = DEFAULT_SIZE;
    cr->alloced         = 0;
    cr->last_index      = 0;
    cr->occupied        = malloc(sizeof(bool) * cr->size);
    for (size_t i = 0; i < cr->size; i++)
    {
        // zero out the new space 
        cr->occupied[i] = false;
    }
    
    return cr; 
}

void ContinueRegion__free(ContinueRegion_t cr){
    free(cr->occupied);
    free(cr);
}

// only can be larger
void ContinueRegion__resize(ContinueRegion_t cr, uint64_t new_size){
    if(new_size > cr->size){
        cr->occupied = realloc(cr->occupied,sizeof(bool) *new_size);
        for (size_t i = cr->size; i < new_size; i++)
        {
            // zero out the new space 
            cr->occupied[i] = false;
        }
        cr->size =  new_size;
    }
}

/* Private fun */
void ContinueRegion__alloc(
    ContinueRegion_t cr, uint64_t start, uint64_t size
){
    for (size_t i = 0; i < size; i++) {
        assert(cr->occupied[i+ start] == false);
        cr->occupied[i + start] = true;
    }
    cr->alloced += size;
}

void ContinueRegion__release(
    ContinueRegion_t cr, ContinueRegion_Region_t crrt
){
    
    for (size_t i = 0; i < crrt->size; i++){
        assert(cr->occupied[i+ crrt->start] == true);
        cr->occupied[i + crrt->start] = false;
    }
    cr->alloced -= crrt->size;
    // free the struct
    free(crrt);
}

ContinueRegion_Region_t ContinueRegion__requestRegion(
    ContinueRegion_t cr, uint64_t size
){
    if(size + cr->alloced > cr->size){
        // double the size to make sure it have space to alloc
        ContinueRegion__resize(cr, 2 * (size + cr->alloced));
    }
    uint64_t search_index = cr->last_index ;
    if (search_index + size >= cr->size ) search_index = 0;
    // the region start 
    uint64_t start_index = search_index; 
    do {
        if (cr->occupied[search_index]){
            ContinueRegion__incIndex(cr, &search_index);
            start_index  = search_index;
        }else {
            ContinueRegion__incIndex(cr, &search_index);
        }

        /* found */
        if (search_index - start_index == size){
            // I found a continous region 
            ContinueRegion_Region_t crrt = malloc(
                sizeof(struct ContinueRegion_Region_s)
            );
            crrt->start = start_index;
            crrt->size  = size;

            cr->last_index = search_index;
            // alloct the crrt 
            ContinueRegion__alloc(cr, crrt->start, crrt->size);
            return crrt;
        }
    } while (search_index != cr->last_index);
    
    /**
     *  Not found-> theres not cont-Region in current managemennt 
     *  Make sure it would found and search
     */
    ContinueRegion__resize(cr, 2*(size + cr->alloced));
    return ContinueRegion__requestRegion(cr, size);
}