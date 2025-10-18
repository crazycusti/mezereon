#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>
#include "bootinfo.h"

#ifdef __cplusplus
extern "C" {
#endif

void paging_init(const boot_info_t* info);
int paging_is_enabled(void);

#ifdef __cplusplus
}
#endif

#endif // PAGING_H
