#ifndef MEMORY_H
#define MEMORY_H

#include <stddef.h>
#include <stdint.h>
#include "bootinfo.h"

void memory_init(const boot_info_t* bootinfo);
void memory_log_summary(void);
uint64_t memory_total_bytes(void);
uint64_t memory_usable_bytes(void);
uint64_t memory_highest_address(void);
size_t memory_region_count(void);
const bootinfo_memory_range_t* memory_region_at(size_t idx);
const boot_info_t* memory_boot_info(void);
void* memory_alloc(size_t size);
void* memory_alloc_aligned(size_t size, size_t alignment);
uint64_t memory_allocated_bytes(void);

#endif // MEMORY_H
