#include "memory.h"
#include "console.h"
#include <stdint.h>

typedef struct {
    bootinfo_memory_range_t desc;
    uint64_t end;
} memory_region_t;

typedef struct {
    memory_region_t regions[BOOTINFO_MEMORY_MAX_RANGES];
    size_t region_count;
    size_t usable_indices[BOOTINFO_MEMORY_MAX_RANGES];
    size_t usable_count;
    uint64_t total_bytes;
    uint64_t usable_bytes;
    uint64_t highest_addr;
    uint64_t kernel_end;
    const boot_info_t* bootinfo;
    int initialized;

    /* High memory allocation cursor */
    size_t high_region_index;
    uint64_t high_cursor;

    /* High Memory Area (HMA) tracking */
    int hma_available;
    size_t hma_region_index;
    uint64_t hma_base;
    uint64_t hma_end;
    uint64_t hma_cursor;
    uint64_t hma_allocated;

    uint64_t allocated_bytes;
} memory_state_t;

static memory_state_t g_mem;
extern uint8_t _end;

#define HMA_BASE 0x00100000ULL
#define HMA_SIZE 0x0000FFF0ULL /* 64 KiB minus 16 bytes */
#define HMA_LIMIT (HMA_BASE + HMA_SIZE)

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

static const char* memory_type_name(uint32_t type) {
    switch (type) {
        case BOOTINFO_MEMORY_TYPE_USABLE:   return "usable";
        case BOOTINFO_MEMORY_TYPE_RESERVED: return "reserved";
        case BOOTINFO_MEMORY_TYPE_ACPI:     return "acpi";
        case BOOTINFO_MEMORY_TYPE_NVS:      return "nvs";
        case BOOTINFO_MEMORY_TYPE_BAD:      return "bad";
        default:                            return "unknown";
    }
}

static uint64_t memory_max_u64(uint64_t a, uint64_t b) {
    return (a > b) ? a : b;
}

static uint64_t memory_min_u64(uint64_t a, uint64_t b) {
    return (a < b) ? a : b;
}

static void memory_log_pretty_size(uint64_t bytes) {
    const uint64_t one_kib = 1024ULL;
    const uint64_t one_mib = one_kib * 1024ULL;
    const uint64_t one_gib = one_mib * 1024ULL;

    if (bytes == 0) {
        console_write("0");
        return;
    }

    if (bytes % one_gib == 0 && (bytes / one_gib) <= 0xFFFFFFFFULL) {
        console_write_dec((uint32_t)(bytes / one_gib));
        console_write(" GiB");
        return;
    }

    if (bytes % one_mib == 0 && (bytes / one_mib) <= 0xFFFFFFFFULL) {
        console_write_dec((uint32_t)(bytes / one_mib));
        console_write(" MiB");
        return;
    }

    if (bytes % one_kib == 0 && (bytes / one_kib) <= 0xFFFFFFFFULL) {
        console_write_dec((uint32_t)(bytes / one_kib));
        console_write(" KiB");
        return;
    }

    console_write_dec(clamp_u64_to_u32(bytes));
    console_write(" bytes");
}

static void memory_sort_regions(memory_region_t* regions, size_t count) {
    for (size_t i = 1; i < count; ++i) {
        memory_region_t key = regions[i];
        size_t j = i;
        while (j > 0 && regions[j - 1].desc.base > key.desc.base) {
            regions[j] = regions[j - 1];
            --j;
        }
        regions[j] = key;
    }
}

static void memory_allocator_reset(void) {
    g_mem.high_region_index = g_mem.region_count;
    g_mem.high_cursor = 0;
    g_mem.hma_cursor = g_mem.hma_end;
    g_mem.hma_allocated = 0;
    g_mem.allocated_bytes = 0;

    if (g_mem.hma_available) {
        uint64_t start = g_mem.kernel_end;
        if (start < g_mem.hma_base) {
            start = g_mem.hma_base;
        }
        if (start < g_mem.hma_end) {
            g_mem.hma_cursor = start;
        }
    }

    for (size_t i = 0; i < g_mem.usable_count; ++i) {
        size_t idx = g_mem.usable_indices[i];
        const memory_region_t* region = &g_mem.regions[idx];
        uint64_t start = g_mem.kernel_end;
        if (start < region->desc.base) {
            start = region->desc.base;
        }
        if (g_mem.hma_available && idx == g_mem.hma_region_index && start < g_mem.hma_end) {
            start = g_mem.hma_end;
        }
        if (start < region->end) {
            g_mem.high_region_index = idx;
            g_mem.high_cursor = start;
            break;
        }
    }
}

void memory_init(const boot_info_t* bootinfo) {
    g_mem.region_count = 0;
    g_mem.usable_count = 0;
    g_mem.total_bytes = 0;
    g_mem.usable_bytes = 0;
    g_mem.highest_addr = 0;
    g_mem.kernel_end = align_up_u64((uint64_t)(uintptr_t)&_end, 16);
    g_mem.bootinfo = bootinfo;
    g_mem.initialized = 1;
    g_mem.high_region_index = 0;
    g_mem.high_cursor = 0;
    g_mem.hma_available = 0;
    g_mem.hma_region_index = BOOTINFO_MEMORY_MAX_RANGES;
    g_mem.hma_base = HMA_LIMIT;
    g_mem.hma_end = HMA_LIMIT;
    g_mem.hma_cursor = HMA_LIMIT;
    g_mem.hma_allocated = 0;
    g_mem.allocated_bytes = 0;

    memory_region_t temp[BOOTINFO_MEMORY_MAX_RANGES];
    size_t temp_count = 0;

    if (bootinfo) {
        const bootinfo_memory_map_t* map = &bootinfo->memory;
        size_t count = map->entry_count;
        if (count > BOOTINFO_MEMORY_MAX_RANGES) {
            count = BOOTINFO_MEMORY_MAX_RANGES;
        }

        for (size_t i = 0; i < count; ++i) {
            const bootinfo_memory_range_t* src = &map->entries[i];
            if (!src || src->length == 0) {
                continue;
            }
            temp[temp_count].desc = *src;
            if (!u64_add_checked(src->base, src->length, &temp[temp_count].end)) {
                temp[temp_count].end = UINT64_MAX;
            }
            ++temp_count;
        }
    }

    if (temp_count == 0) {
        uint16_t kb = *((volatile uint16_t*)0x413);
        if (kb != 0) {
            uint64_t bytes = ((uint64_t)kb) << 10;
            temp[0].desc.base = 0;
            temp[0].desc.length = bytes;
            temp[0].desc.type = BOOTINFO_MEMORY_TYPE_USABLE;
            temp[0].desc.attr = 0;
            temp[0].end = bytes;
            temp_count = 1;
        }
    }

    if (temp_count) {
        memory_sort_regions(temp, temp_count);
        uint64_t consumed_end = 0;
        for (size_t i = 0; i < temp_count && g_mem.region_count < BOOTINFO_MEMORY_MAX_RANGES; ++i) {
            memory_region_t region = temp[i];
            uint64_t base = region.desc.base;
            uint64_t end = region.end;
            if (end <= base) {
                continue;
            }
            if (base < consumed_end) {
                base = consumed_end;
                if (base >= end) {
                    continue;
                }
            }

            region.desc.base = base;
            region.desc.length = end - base;
            region.end = end;

            g_mem.regions[g_mem.region_count] = region;
            g_mem.total_bytes = u64_add_saturating(g_mem.total_bytes, region.desc.length);
            if (region.desc.type == BOOTINFO_MEMORY_TYPE_USABLE) {
                g_mem.usable_bytes = u64_add_saturating(g_mem.usable_bytes, region.desc.length);
                g_mem.usable_indices[g_mem.usable_count++] = g_mem.region_count;
            }
            if (region.end > g_mem.highest_addr) {
                g_mem.highest_addr = region.end;
            }

            if (!g_mem.hma_available && region.desc.type == BOOTINFO_MEMORY_TYPE_USABLE) {
                if (region.desc.base < HMA_LIMIT && region.end > HMA_BASE) {
                    g_mem.hma_available = 1;
                    g_mem.hma_region_index = g_mem.region_count;
                    g_mem.hma_base = memory_max_u64(region.desc.base, HMA_BASE);
                    g_mem.hma_end = memory_min_u64(region.end, HMA_LIMIT);
                    if (g_mem.hma_base >= g_mem.hma_end) {
                        g_mem.hma_available = 0;
                        g_mem.hma_region_index = BOOTINFO_MEMORY_MAX_RANGES;
                        g_mem.hma_base = HMA_LIMIT;
                        g_mem.hma_end = HMA_LIMIT;
                    }
                }
            }

            ++g_mem.region_count;
            consumed_end = memory_max_u64(consumed_end, end);
        }
    }

    memory_allocator_reset();
}

void memory_log_summary(void) {
    if (!g_mem.initialized) {
        console_writeln("Memory: init missing");
        return;
    }

    console_write("Memory: E820 regions=");
    console_write_dec((uint32_t)g_mem.region_count);
    console_write(", usable=");
    console_write_dec((uint32_t)g_mem.usable_count);
    console_writeln("");

    for (size_t i = 0; i < g_mem.region_count; ++i) {
        const memory_region_t* region = &g_mem.regions[i];
        console_write("  [");
        console_write_dec((uint32_t)i);
        console_write("] ");
        console_write(memory_type_name(region->desc.type));
        console_write(" base=0x");
        console_write_u64_hex(region->desc.base);
        console_write(", end=0x");
        console_write_u64_hex(region->end);
        console_write(", length=");
        memory_log_pretty_size(region->desc.length);
        console_write(" (0x");
        console_write_u64_hex(region->desc.length);
        console_write(" bytes)");
        if (region->desc.attr) {
            console_write(", attr=0x");
            console_write_hex32(region->desc.attr);
        }
        if (g_mem.hma_available && i == g_mem.hma_region_index) {
            console_write(" [contains HMA]");
        }
        console_writeln("");
    }

    console_write("Memory: total physical=");
    memory_log_pretty_size(g_mem.total_bytes);
    console_write(" (0x");
    console_write_u64_hex(g_mem.total_bytes);
    console_writeln(" bytes)");

    console_write("Memory: usable=");
    memory_log_pretty_size(g_mem.usable_bytes);
    console_write(" (0x");
    console_write_u64_hex(g_mem.usable_bytes);
    console_writeln(" bytes)");

    console_write("Memory: highest-address=0x");
    console_write_u64_hex(g_mem.highest_addr);
    console_writeln("");

    console_write("Memory: kernel-end=0x");
    console_write_u64_hex(g_mem.kernel_end);
    console_writeln("");

    if (g_mem.hma_available) {
        console_write("Memory: HMA window 0x");
        console_write_u64_hex(g_mem.hma_base);
        console_write("-0x");
        console_write_u64_hex(g_mem.hma_end);
        console_write(" (usable ");
        memory_log_pretty_size(g_mem.hma_end - g_mem.hma_base);
        console_writeln(")");
    } else {
        console_writeln("Memory: HMA unavailable");
    }
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
    return g_mem.region_count;
}

const bootinfo_memory_range_t* memory_region_at(size_t idx) {
    if (idx >= g_mem.region_count) {
        return NULL;
    }
    return &g_mem.regions[idx].desc;
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

    uint64_t cursor;
    void* ptr;

    if (g_mem.hma_available && g_mem.hma_cursor < g_mem.hma_end) {
        cursor = g_mem.hma_cursor;
        uint64_t aligned = align_up_u64(cursor, alignment);
        if (aligned != UINT64_MAX) {
            uint64_t next_cursor;
            if (u64_add_checked(aligned, size, &next_cursor) && next_cursor <= g_mem.hma_end) {
                g_mem.hma_cursor = next_cursor;
                g_mem.hma_allocated = u64_add_saturating(g_mem.hma_allocated, size);
                g_mem.allocated_bytes = u64_add_saturating(g_mem.allocated_bytes, size);
                return (void*)(uintptr_t)aligned;
            }
        }
    }

    size_t region_idx = (g_mem.high_region_index < g_mem.region_count) ? g_mem.high_region_index : 0;
    uint64_t start_cursor = g_mem.high_cursor;

    for (; region_idx < g_mem.region_count; ++region_idx) {
        const memory_region_t* region = &g_mem.regions[region_idx];
        if (region->desc.type != BOOTINFO_MEMORY_TYPE_USABLE) {
            continue;
        }

        cursor = (region_idx == g_mem.high_region_index) ? start_cursor : g_mem.kernel_end;
        if (cursor < region->desc.base) {
            cursor = region->desc.base;
        }
        if (g_mem.hma_available && region_idx == g_mem.hma_region_index && cursor < g_mem.hma_end) {
            cursor = g_mem.hma_end;
        }

        uint64_t aligned = align_up_u64(cursor, alignment);
        if (aligned == UINT64_MAX) {
            continue;
        }

        uint64_t next_cursor;
        if (!u64_add_checked(aligned, size, &next_cursor) || next_cursor > region->end) {
            continue;
        }

        g_mem.high_region_index = region_idx;
        g_mem.high_cursor = next_cursor;
        g_mem.allocated_bytes = u64_add_saturating(g_mem.allocated_bytes, size);
        ptr = (void*)(uintptr_t)aligned;
        return ptr;
    }

    g_mem.high_region_index = g_mem.region_count;
    g_mem.high_cursor = 0;
    return NULL;
}

uint64_t memory_allocated_bytes(void) {
    return g_mem.allocated_bytes;
}
