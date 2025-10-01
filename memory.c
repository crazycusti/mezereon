#include "memory.h"
#include "console.h"
#include <stdint.h>

typedef struct {
    bootinfo_memory_range_t ranges[BOOTINFO_MEMORY_MAX_RANGES];
    size_t range_count;
    uint64_t total_bytes;
    uint64_t usable_bytes;
    uint64_t highest_addr;
    const boot_info_t* bootinfo;
    int initialized;
    size_t alloc_region;
    uint64_t alloc_next;
    uint64_t allocated_bytes;
} memory_state_t;

static memory_state_t g_mem;
extern uint8_t _end;

static int u64_add_checked(uint64_t a, uint64_t b, uint64_t* out) {
    if (UINT64_MAX - a < b) {
        return 0;
    }
    *out = a + b;
    return 1;
}

static uint64_t u64_add_saturating(uint64_t a, uint64_t b) {
    if (UINT64_MAX - a < b) {
        return UINT64_MAX;
    }
    return a + b;
}

static uint64_t align_up_u64(uint64_t value, uint64_t alignment) {
    if (alignment <= 1) {
        return value;
    }
    uint64_t a = alignment;
    if (a & (a - 1)) {
        uint64_t next = 1;
        while (next < a && next < (1ULL << 63)) {
            next <<= 1;
        }
        a = next;
    }
    uint64_t mask = a - 1;
    uint64_t aligned;
    if (!u64_add_checked(value, mask, &aligned)) {
        return UINT64_MAX;
    }
    return aligned & ~mask;
}

static void console_write_u64_hex(uint64_t v) {
    uint32_t hi = (uint32_t)(v >> 32);
    uint32_t lo = (uint32_t)v;
    if (hi) {
        console_write_hex32(hi);
        console_write_hex32(lo);
    } else {
        console_write_hex32(lo);
    }
}

static uint32_t clamp_u64_to_u32(uint64_t v) {
    return (v > 0xFFFFFFFFu) ? 0xFFFFFFFFu : (uint32_t)v;
}

static void memory_allocator_reset(void) {
    g_mem.alloc_region = g_mem.range_count;
    g_mem.alloc_next = UINT64_MAX;
    g_mem.allocated_bytes = 0;

    uint64_t kernel_end = align_up_u64((uint64_t)(uintptr_t)&_end, 16);
    for (size_t i = 0; i < g_mem.range_count; i++) {
        const bootinfo_memory_range_t* r = &g_mem.ranges[i];
        if (r->type != BOOTINFO_MEMORY_TYPE_USABLE) {
            continue;
        }
        uint64_t region_end;
        if (!u64_add_checked(r->base, r->length, &region_end)) {
            region_end = UINT64_MAX;
        }
        if (kernel_end >= region_end) {
            continue;
        }
        g_mem.alloc_region = i;
        if (kernel_end <= r->base) {
            g_mem.alloc_next = r->base;
        } else {
            g_mem.alloc_next = kernel_end;
        }
        return;
    }
}

void memory_init(const boot_info_t* bootinfo) {
    g_mem.range_count = 0;
    g_mem.total_bytes = 0;
    g_mem.usable_bytes = 0;
    g_mem.highest_addr = 0;
    g_mem.bootinfo = bootinfo;
    g_mem.initialized = 1;
    g_mem.alloc_region = 0;
    g_mem.alloc_next = UINT64_MAX;
    g_mem.allocated_bytes = 0;

    if (bootinfo) {
        const bootinfo_memory_map_t* map = &bootinfo->memory;
        size_t count = map->entry_count;
        if (count > BOOTINFO_MEMORY_MAX_RANGES) {
            count = BOOTINFO_MEMORY_MAX_RANGES;
        }

        for (size_t i = 0; i < count; i++) {
            const bootinfo_memory_range_t* src = &map->entries[i];
            if (src->length == 0) {
                continue;
            }
            g_mem.ranges[g_mem.range_count] = *src;
            g_mem.range_count++;

            g_mem.total_bytes = u64_add_saturating(g_mem.total_bytes, src->length);
            if (src->type == BOOTINFO_MEMORY_TYPE_USABLE) {
                g_mem.usable_bytes = u64_add_saturating(g_mem.usable_bytes, src->length);
            }

            uint64_t end;
            if (!u64_add_checked(src->base, src->length, &end)) {
                end = UINT64_MAX;
            }
            if (end > g_mem.highest_addr) {
                g_mem.highest_addr = end;
            }
        }
    }

    if (g_mem.range_count == 0) {
        uint16_t kb = *((volatile uint16_t*)0x413);
        if (kb != 0) {
            bootinfo_memory_range_t* r = &g_mem.ranges[0];
            uint64_t bytes = ((uint64_t)kb) << 10;
            r->base = 0;
            r->length = bytes;
            r->type = BOOTINFO_MEMORY_TYPE_USABLE;
            r->attr = 0;
            g_mem.range_count = 1;
            g_mem.total_bytes = bytes;
            g_mem.usable_bytes = bytes;
            g_mem.highest_addr = bytes;
        }
    }

    memory_allocator_reset();
}

void memory_log_summary(void) {
    if (!g_mem.initialized) {
        console_writeln("Memory: init missing");
        return;
    }

    uint64_t total_mib64 = g_mem.total_bytes >> 20;
    uint64_t usable_mib64 = g_mem.usable_bytes >> 20;

    console_write("Memory: regions=");
    console_write_dec((uint32_t)g_mem.range_count);
    console_write(", total=");
    console_write_dec(clamp_u64_to_u32(total_mib64));
    console_write(" MiB, usable=");
    console_write_dec(clamp_u64_to_u32(usable_mib64));
    console_write(" MiB (total=0x");
    console_write_u64_hex(g_mem.total_bytes);
    console_write(", usable=0x");
    console_write_u64_hex(g_mem.usable_bytes);
    console_write(" bytes)\n");
}

uint64_t memory_total_bytes(void) {
    return g_mem.total_bytes;
}

uint64_t memory_usable_bytes(void) {
    return g_mem.usable_bytes;
}

uint64_t memory_highest_address(void) {
    return g_mem.highest_addr;
}

size_t memory_region_count(void) {
    return g_mem.range_count;
}

const bootinfo_memory_range_t* memory_region_at(size_t idx) {
    if (idx >= g_mem.range_count) {
        return NULL;
    }
    return &g_mem.ranges[idx];
}

const boot_info_t* memory_boot_info(void) {
    return g_mem.bootinfo;
}

void* memory_alloc(size_t size) {
    return memory_alloc_aligned(size, 16);
}

void* memory_alloc_aligned(size_t size, size_t alignment) {
    if (!g_mem.initialized || size == 0) {
        return NULL;
    }
    if (alignment == 0) {
        alignment = 1;
    }

    size_t idx = g_mem.alloc_region;
    uint64_t cursor = g_mem.alloc_next;

    for (; idx < g_mem.range_count; idx++) {
        const bootinfo_memory_range_t* r = &g_mem.ranges[idx];
        if (r->type != BOOTINFO_MEMORY_TYPE_USABLE) {
            continue;
        }

        uint64_t region_start = r->base;
        uint64_t region_end;
        if (!u64_add_checked(r->base, r->length, &region_end)) {
            region_end = UINT64_MAX;
        }

        if (cursor == UINT64_MAX || cursor < region_start) {
            cursor = region_start;
        }

        uint64_t aligned = align_up_u64(cursor, alignment);
        if (aligned == UINT64_MAX) {
            cursor = UINT64_MAX;
            continue;
        }

        uint64_t next_cursor;
        if (!u64_add_checked(aligned, size, &next_cursor) || next_cursor > region_end) {
            cursor = UINT64_MAX;
            continue;
        }

        g_mem.alloc_region = idx;
        g_mem.alloc_next = next_cursor;
        g_mem.allocated_bytes = u64_add_saturating(g_mem.allocated_bytes, size);
        return (void*)(uintptr_t)aligned;
    }

    g_mem.alloc_region = g_mem.range_count;
    g_mem.alloc_next = UINT64_MAX;
    return NULL;
}

uint64_t memory_allocated_bytes(void) {
    return g_mem.allocated_bytes;
}
