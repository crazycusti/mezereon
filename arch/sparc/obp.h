#ifndef ARCH_SPARC_OBP_H
#define ARCH_SPARC_OBP_H

#include <stdint.h>

// Minimal OpenBoot PROM vector for sun4m/sun4c (subset)
typedef struct promvec {
    int32_t  pv_romvers;        // ROM vector version
    void   (*pv_printrev)(void);
    void*    pv_nodeops;        // device tree ops (unused here)
    char   **pv_bootargs;       // boot arguments string pointer
    void*    pv_bootops;        // bootops (unused here)
    void   (*pv_putchar)(char); // output one char
    int    (*pv_getchar)(void); // blocking getchar
    int    (*pv_mayget)(void);  // non-blocking getchar
    int    (*pv_mayput)(int);   // non-blocking putchar
    void   (*pv_forth)(void);   // enter forth prompt
    // ... many more fields in real OBP revisions
} promvec_t;

#endif // ARCH_SPARC_OBP_H

