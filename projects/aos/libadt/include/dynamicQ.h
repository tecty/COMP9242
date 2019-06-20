#if !defined(DYNAMIC_Q)
#define DYNAMIC_Q


#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

typedef struct DynamicQ_s * DynamicQ_t ;

DynamicQ_t DynamicQ__init(uint64_t item_size);
void DynamicQ__enQueue(DynamicQ_t dq,void * data);
bool DynamicQ__isEmpty(DynamicQ_t dq);
void * DynamicQ__first(DynamicQ_t dq);
void DynamicQ__deQueue(DynamicQ_t dq);
void DynamicQ__free(DynamicQ_t dq);

#endif // DYNAMIC_Q

