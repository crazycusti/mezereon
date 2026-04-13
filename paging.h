#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>
#include "bootinfo.h"

#ifdef __cplusplus
extern "C" {
#endif

void paging_init(const boot_info_t* info);
int paging_is_enabled(void);
uint32_t paging_identity_limit(void);

// Map a physical device range (e.g. framebuffer/MMIO) into a dedicated virtual window.
// On non-x86 or when paging is disabled, this returns the identity-mapped pointer.
//
// flags:
//  - PAGING_IOREMAP_UNCACHED: map pages with PWT|PCD (safe default for MMIO/LFB)
#define PAGING_IOREMAP_UNCACHED (1u << 0)
void* paging_ioremap(uint32_t phys_addr, uint32_t size_bytes, uint32_t flags);

// Returns 1 if ptr lies within the active ioremap window (i.e. has been mapped by paging_ioremap()).
int paging_is_ioremapped_ptr(const void* ptr);

#ifdef __cplusplus
}
#endif

#endif // PAGING_H
