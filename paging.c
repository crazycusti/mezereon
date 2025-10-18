#include "paging.h"
#include "config.h"
#include <stddef.h>
#include <stdint.h>

#if CONFIG_ARCH_X86
#define PAGE_PRESENT 0x001u
#define PAGE_RW      0x002u
#define PAGE_PWT     0x008u
#define PAGE_PCD     0x010u

#define PAGING_MAX_TABLES 64u

static uint32_t g_page_directory[1024] __attribute__((aligned(4096)));
static uint32_t g_page_tables[PAGING_MAX_TABLES][1024] __attribute__((aligned(4096)));
static int g_paging_enabled = 0;

static inline uint32_t align_up(uint32_t value, uint32_t alignment) {
    return (value + alignment - 1u) & ~(alignment - 1u);
}

static uint32_t detect_highest_physical(const boot_info_t* info) {
    uint64_t highest = 0x01000000ull; // default: map at least 16 MiB
    extern uint8_t _end;
    uint32_t kernel_end = align_up((uint32_t)(uintptr_t)&_end, 0x1000u);
    if ((uint64_t)kernel_end > highest) {
        highest = (uint64_t)kernel_end;
    }

    if (info) {
        const bootinfo_memory_map_t* map = &info->memory;
        uint32_t count = map->entry_count;
        if (count > BOOTINFO_MEMORY_MAX_RANGES) {
            count = BOOTINFO_MEMORY_MAX_RANGES;
        }
        for (uint32_t i = 0; i < count; ++i) {
            const bootinfo_memory_range_t* r = &map->entries[i];
            if (r->length == 0) {
                continue;
            }
            uint64_t end = r->base + r->length;
            if (end > highest) {
                highest = end;
            }
        }
    }

    uint64_t max_cover = (uint64_t)PAGING_MAX_TABLES * 1024u * 4096u;
    if (highest > max_cover) {
        highest = max_cover;
    }

    if (highest < 0x00100000ull) {
        highest = 0x00100000ull;
    }

    return (uint32_t)align_up((uint32_t)highest, 0x1000u);
}

static void build_identity_map(uint32_t limit_bytes) {
    const uint32_t common_flags = PAGE_PRESENT | PAGE_RW;
    const uint32_t uncached_flags = common_flags | PAGE_PWT | PAGE_PCD;

    for (size_t i = 0; i < 1024; ++i) {
        g_page_directory[i] = 0u;
    }

    uint32_t total_pages = limit_bytes >> 12; // already aligned up
    uint32_t tables_needed = (total_pages + 1023u) / 1024u;
    if (tables_needed == 0) {
        tables_needed = 1;
    }
    if (tables_needed > PAGING_MAX_TABLES) {
        tables_needed = PAGING_MAX_TABLES;
    }

    for (uint32_t table = 0; table < tables_needed; ++table) {
        uint32_t* pt = g_page_tables[table];
        for (uint32_t entry = 0; entry < 1024u; ++entry) {
            uint32_t phys = ((table * 1024u) + entry) * 4096u;
            uint32_t flags = common_flags;
            if (phys >= 0x000A0000u && phys <= 0x000BFFFFu) {
                flags = uncached_flags;
            }
            pt[entry] = phys | flags;
        }
        g_page_directory[table] = (uint32_t)(uintptr_t)pt | common_flags;
    }

    for (uint32_t table = tables_needed; table < PAGING_MAX_TABLES; ++table) {
        uint32_t* pt = g_page_tables[table];
        for (uint32_t entry = 0; entry < 1024u; ++entry) {
            pt[entry] = 0u;
        }
    }
}

void paging_init(const boot_info_t* info) {
    if (g_paging_enabled) {
        return;
    }

    uint32_t highest = detect_highest_physical(info);
    build_identity_map(highest);

    uint32_t cr3 = (uint32_t)(uintptr_t)g_page_directory;
    __asm__ volatile ("mov %0, %%cr3" :: "r"(cr3) : "memory");

    uint32_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000u; // CR0.PG
    __asm__ volatile ("mov %0, %%cr0" :: "r"(cr0) : "memory");
    __asm__ volatile ("mov %0, %%cr3" :: "r"(cr3) : "memory");

    g_paging_enabled = 1;
}

int paging_is_enabled(void) {
    if (g_paging_enabled) {
        return 1;
    }
    uint32_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    return (cr0 & 0x80000000u) ? 1 : 0;
}

#else

void paging_init(const boot_info_t* info) {
    (void)info;
}

int paging_is_enabled(void) {
    return 0;
}

#endif
