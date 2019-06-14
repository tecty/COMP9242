#if !defined(DYNAMIC_H)
#define DYNAMIC_H

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

typedef struct DynamicArr_s * DynamicArr_t ;
DynamicArr_t DynamicArr__init(size_t item_size);
size_t DynamicArr__add(DynamicArr_t da,void * data);
void * DynamicArr__get(DynamicArr_t da, size_t index);
void DynamicArr__del(DynamicArr_t da, size_t index);
void DynamicArr__free(DynamicArr_t da);


#endif // DYNAMIC_H
