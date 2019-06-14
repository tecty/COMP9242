#if !defined(PRIORITY_H)
#define PRIORITY_H

#include <stdlib.h>
#include <string.h>

typedef int (*compare_fun_t)(void * a , void * b);

typedef struct PriorityQueue_s * PriorityQueue_t;
PriorityQueue_t PriorityQueue__init(compare_fun_t compare);
void PriorityQueue__add(PriorityQueue_t pq, void* data);
void* PriorityQueue__first(PriorityQueue_t pq);
void* PriorityQueue__pop(PriorityQueue_t pq);
void PriorityQueue__free(PriorityQueue_t pq);


#endif // PRIORITY_H
