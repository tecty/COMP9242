#if !defined(CONTREGION_H)
#define CONTREGION_H

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

typedef struct ContinueRegion_Region_s * ContinueRegion_Region_t;
typedef struct ContinueRegion_s * ContinueRegion_t;

uint64_t ContinueRegionRegion__getStart(ContinueRegion_Region_t crrt);
uint64_t ContinueRegionRegion__getSize(ContinueRegion_Region_t crrt);
ContinueRegion_t ContinueRegion__init();
void ContinueRegion__free(ContinueRegion_t cr);
void ContinueRegion__release(
    ContinueRegion_t cr, ContinueRegion_Region_t crrt
);
ContinueRegion_Region_t ContinueRegion__requestRegion(
    ContinueRegion_t cr, uint64_t size
);


#endif // CONTREGION_H

