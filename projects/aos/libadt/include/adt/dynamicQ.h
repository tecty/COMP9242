#if !defined(DYNAMIC_Q)
#define DYNAMIC_Q


#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

typedef struct DynamicQ_s * DynamicQ_t ;
typedef void (* dynamicQ_callback_t)(void * data);


DynamicQ_t DynamicQ__init(uint64_t item_size);
void DynamicQ__enQueue(DynamicQ_t dq,void * data);
bool DynamicQ__isEmpty(DynamicQ_t dq);
void * DynamicQ__first(DynamicQ_t dq);
void DynamicQ__deQueue(DynamicQ_t dq);
size_t DynamicQ__getAlloced(DynamicQ_t dq);
void DynamicQ__free(DynamicQ_t dq);
void DynamicQ__forAll(DynamicQ_t dq, dynamicQ_callback_t callback);
#endif // DYNAMIC_Q

