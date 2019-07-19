#include <adt/dynamicQ.h>
#include <stdio.h>

typedef struct DynamicQ_s
{
    void * item_arr;
    uint64_t item_size;
    uint64_t alloced; 
    uint64_t length; 
    // the last position it's empty
    uint64_t head;
    uint64_t tail;
}  * DynamicQ_t;

static inline void DynamicQ__incTail(DynamicQ_t dq){
    dq->tail++; dq->tail %= dq->length;
}

static inline void DynamicQ__incHead(DynamicQ_t dq){
    dq->head++; dq->head %= dq->length;
}

DynamicQ_t DynamicQ__init(uint64_t item_size){
    DynamicQ_t ret = malloc(sizeof(struct DynamicQ_s));
    // basic setup 
    ret->length    = 4;
    ret->head      = 0;
    ret->tail      = 0; //point to an empty slot 
    ret->alloced   = 0;
    ret->item_size = item_size;
    // create the dynamic arr 
    ret-> item_arr      = calloc(ret->length, item_size);

    return ret;
}

void DynamicQ__enQueue(DynamicQ_t dq,void * data){
    if (dq->alloced == dq-> length - 2){
        // printf("DynamicQ is resizing\n");
        // double the size, pre_alloc to improve searching
        dq->length *= 2;
        dq->item_arr      = realloc(dq->item_arr, dq->length* dq->item_size);
        if (dq->tail < dq->head)
        {
            /* Protential bug */
            // copy the first half to the end of second half 
            memcpy(
                dq->item_arr + (dq->length / 2) * dq->item_size,
                dq->item_arr,
                dq->tail  * dq->item_size 
            );
            dq->tail += dq->length /2;
        }
        
    }
    dq->alloced ++;

    // store the new item here 
    memcpy (dq->item_arr + (dq->tail * dq->item_size), data, dq->item_size);
    // some routine to maintain the data intergity
    DynamicQ__incTail(dq);
}

bool DynamicQ__isEmpty(DynamicQ_t dq){
    // if (dq->alloced ==0) {
    //     printf("Head:%lu\tTail:%lu\talloced%lu\tsize:%lu\n",dq->head, dq->tail, dq->alloced, dq->length);
    //     assert(dq->head == dq->tail);
    // }
    return dq->alloced == 0; 
}

void * DynamicQ__first(DynamicQ_t dq){
    // nothing in the queue 
    if (DynamicQ__isEmpty(dq)) return  NULL;
    return dq->item_arr + dq->head * dq->item_size;
}

void DynamicQ__deQueue(DynamicQ_t dq){
    if (DynamicQ__isEmpty(dq)) return;

    // decrement 
    dq->alloced --;
    DynamicQ__incHead(dq);
}

size_t DynamicQ__getAlloced(DynamicQ_t dq){
    return dq->alloced;
}

void DynamicQ__free(DynamicQ_t dq){
    free(dq->item_arr);
    free(dq);
}

void DynamicQ__foreach(DynamicQ_t dq, dynamicQ_callback_t callback){
    void * data;
    while ((data = DynamicQ__first(dq)) && data != NULL){
        callback(data);
        DynamicQ__deQueue(dq) ;
    }
}
