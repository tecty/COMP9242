#include <adt/dynamic.h>
#include <stdio.h>

typedef struct DynamicArr_s
{
    void * item_arr;
    bool * item_occupied;
    size_t item_size;
    size_t alloced; 
    size_t length; 
    // the last position it's empty
    size_t tail;
}  * DynamicArr_t;

static inline void DynamicArr__incTail(DynamicArr_t da){
    da->tail++;
    da->tail %= da->length;
}

DynamicArr_t DynamicArr__init(size_t item_size){
    DynamicArr_t ret = malloc(sizeof(struct DynamicArr_s));
    // basic setup 
    ret->length    = 4;
    ret->tail      = 0;
    ret->alloced   = 0;
    ret->item_size = item_size;
    // create the dynamic arr 
    ret-> item_arr      = malloc(ret->length * item_size);
    ret-> item_occupied = malloc(ret->length* sizeof(bool));
    for (size_t i = 0; i < ret->length; i++)
    {
        ret->item_occupied[i] = false;
    }
    

    return ret;
}

/**
 * @ret: the id to the inside managed strcuture 
 */
size_t DynamicArr__add(DynamicArr_t da,void * data){
    if (da->alloced == da-> length - 2){
        // double the size, pre_alloc to improve searching
        da->length *= 2;
        da->item_occupied = realloc(da->item_occupied, da->length * sizeof(bool));
        da->item_arr      = realloc(da->item_arr     , da->length * da->item_size);
        for (size_t i = da->length/2; i < da->length; i++)
        {
            /* code */
            da->item_occupied[i] = false;
        }
    }


    while (da->item_occupied[da->tail] == true)
    {
        // printf("%lu is occupied\n", da->tail);
        DynamicArr__incTail(da);
    }

    // printf("Find a place to alloc at %ld\n", da->tail);
    da->alloced ++;

    // store the new item here 
    memcpy (da->item_arr + (da->tail * da->item_size), data, da->item_size);
    // some routine to maintain the data intergity
    size_t ret = da->tail;
    da->item_occupied[ret] = true;
    // DynamicArr__incTail(da);
    return ret;
}

void * DynamicArr__get(DynamicArr_t da, size_t index){
    // return null to indecate this item is delted 
    if (da->item_occupied[index] == false ){
        return NULL;
    }
    return da->item_arr + index * da->item_size;
}

void DynamicArr__del(DynamicArr_t da, size_t index){
    if (da->item_occupied[index] == true){
        da->item_occupied[index] = false;
        da->alloced --;
    } 
    // printf("%lu/%lu deleted %lu\n", da->alloced, da->length , index);
}

void DynamicArr__free(DynamicArr_t da){
    free(da->item_arr);
    free(da->item_occupied);
    free(da);
}
