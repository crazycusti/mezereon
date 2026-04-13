#include "paging.h"
#include "config.h"
#include "memory.h"
#include <stddef.h>
#include <stdint.h>

#if CONFIG_ARCH_X86
#define PAGE_PRESENT 0x001u
#define PAGE_RW      0x002u
#define PAGE_PWT     0x008u
#define PAGE_PCD     0x010u

// Total number of page tables we are willing to allocate.
// Identity mapping and ioremap share this pool.
#define PAGING_MAX_TABLES 96u
#define PAGING_MAX_IDENTITY_TABLES 64u

	#define PAGING_PAGE_SIZE 4096u
	#define PAGING_PT_ENTRIES 1024u
	#define PAGING_TABLE_COVER_BYTES (PAGING_PAGE_SIZE * PAGING_PT_ENTRIES) /* 4MiB */
	#define PAGING_MIN_IDENTITY_BYTES 0x000C0000u /* cover VGA text window (0xB8000) */

	static uint32_t* g_page_directory = NULL;
	static uint32_t* g_page_tables[PAGING_MAX_TABLES];
	static uint32_t g_page_table_count = 0;
	static int g_paging_enabled = 0;
	static int g_paging_attempted = 0;
	static uint32_t g_identity_limit = 0;
	static uint32_t g_ioremap_next = 0;

	// Virtual window for device mappings. Keep it far away from the identity-mapped low RAM.
	#define PAGING_IOREMAP_BASE  0xE0000000u
	#define PAGING_IOREMAP_LIMIT 0xFF000000u /* exclusive */

	static inline uint32_t align_up(uint32_t value, uint32_t alignment) {
	    return (value + alignment - 1u) & ~(alignment - 1u);
	}

	static inline uint32_t align_down(uint32_t value, uint32_t alignment) {
	    return value & ~(alignment - 1u);
	}

	static inline void paging_invlpg(uint32_t vaddr) {
	    __asm__ volatile ("invlpg (%0)" :: "r"(vaddr) : "memory");
	}

	static uint32_t detect_identity_limit(const boot_info_t* info) {
	    uint64_t highest = (uint64_t)PAGING_MIN_IDENTITY_BYTES;

	    extern uint8_t _end;
	    uint32_t kernel_end = align_up((uint32_t)(uintptr_t)&_end, PAGING_PAGE_SIZE);
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
	            if (r->type != BOOTINFO_MEMORY_TYPE_USABLE || r->length == 0) {
	                continue;
	            }
	            uint64_t end = r->base + r->length;
	            if (end > highest) {
	                highest = end;
	            }
	        }

	        // Include framebuffer windows only if they are actually in low memory.
	        // High LFBs (e.g. 0xFD000000) must be mapped via ioremap, not identity mapping.
	        const uint64_t max_identity_cover = (uint64_t)PAGING_MAX_IDENTITY_TABLES * (uint64_t)PAGING_TABLE_COVER_BYTES;
	        if (info->framebuffer_phys && info->vbe_pitch && info->vbe_height && info->vbe_bpp) {
	            uint64_t fb_bytes = (uint64_t)info->vbe_pitch * (uint64_t)info->vbe_height;
	            uint64_t fb_end = (uint64_t)info->framebuffer_phys + fb_bytes;
	            if (fb_end <= max_identity_cover && fb_end > highest) {
	                highest = fb_end;
	            }
	        }
	        if (info->framebuffer_phys_4bpp && info->vbe_pitch_4bpp && info->vbe_height_4bpp && info->vbe_bpp_4bpp) {
	            uint64_t fb_bytes4 = (uint64_t)info->vbe_pitch_4bpp * (uint64_t)info->vbe_height_4bpp;
	            uint64_t fb_end4 = (uint64_t)info->framebuffer_phys_4bpp + fb_bytes4;
	            if (fb_end4 <= max_identity_cover && fb_end4 > highest) {
	                highest = fb_end4;
	            }
	        }
	    }

	    uint64_t max_cover = (uint64_t)PAGING_MAX_IDENTITY_TABLES * (uint64_t)PAGING_TABLE_COVER_BYTES;
	    if (highest > max_cover) {
	        highest = max_cover;
	    }
	    if (highest < (uint64_t)PAGING_MIN_IDENTITY_BYTES) {
	        highest = (uint64_t)PAGING_MIN_IDENTITY_BYTES;
	    }

	    return (uint32_t)align_up((uint32_t)highest, PAGING_PAGE_SIZE);
	}

	static int paging_should_enable(void) {
	#if CONFIG_PAGING_POLICY == CONFIG_PAGING_POLICY_NEVER
	    return 0;
	#elif CONFIG_PAGING_POLICY == CONFIG_PAGING_POLICY_ALWAYS
	    return 1;
	#else
	    uint64_t usable = memory_usable_bytes();
	    uint64_t threshold = (uint64_t)CONFIG_PAGING_AUTO_MIN_USABLE_KB * 1024ull;
	    return (usable >= threshold) ? 1 : 0;
	#endif
	}

	static int paging_alloc_structures(uint32_t tables_needed) {
	    if (tables_needed == 0) {
	        tables_needed = 1;
	    }
	    if (tables_needed > PAGING_MAX_TABLES) {
	        tables_needed = PAGING_MAX_TABLES;
	    }

	    g_page_directory = (uint32_t*)memory_alloc_aligned(PAGING_PAGE_SIZE, PAGING_PAGE_SIZE);
	    if (!g_page_directory) {
	        return 0;
	    }
	    for (uint32_t i = 0; i < PAGING_PT_ENTRIES; ++i) {
	        g_page_directory[i] = 0u;
	    }

	    g_page_table_count = 0;
	    for (uint32_t i = 0; i < PAGING_MAX_TABLES; ++i) {
	        g_page_tables[i] = NULL;
	    }

	    for (uint32_t t = 0; t < tables_needed; ++t) {
	        uint32_t* pt = (uint32_t*)memory_alloc_aligned(PAGING_PAGE_SIZE, PAGING_PAGE_SIZE);
	        if (!pt) {
	            return 0;
	        }
	        g_page_tables[t] = pt;
	        ++g_page_table_count;
	    }
	    return 1;
	}

	static uint32_t* paging_alloc_page_table(void) {
	    if (g_page_table_count >= PAGING_MAX_TABLES) {
	        return NULL;
	    }
	    uint32_t* pt = (uint32_t*)memory_alloc_aligned(PAGING_PAGE_SIZE, PAGING_PAGE_SIZE);
	    if (!pt) {
	        return NULL;
	    }
	    for (uint32_t i = 0; i < PAGING_PT_ENTRIES; ++i) {
	        pt[i] = 0u;
	    }
	    g_page_tables[g_page_table_count++] = pt;
	    return pt;
	}

	static uint32_t* paging_get_or_create_pt(uint32_t vaddr, uint32_t pde_flags) {
	    if (!g_page_directory) {
	        return NULL;
	    }
	    uint32_t pd_index = (vaddr >> 22) & 0x3FFu;
	    uint32_t pde = g_page_directory[pd_index];
	    if (pde & PAGE_PRESENT) {
	        uint32_t pt_phys = pde & 0xFFFFF000u;
	        return (uint32_t*)(uintptr_t)pt_phys; // identity mapped
	    }
	    uint32_t* pt = paging_alloc_page_table();
	    if (!pt) {
	        return NULL;
	    }
	    g_page_directory[pd_index] = ((uint32_t)(uintptr_t)pt & 0xFFFFF000u) | pde_flags;
	    return pt;
	}

	static void paging_build_identity_map(uint32_t limit_bytes) {
	    const uint32_t common_flags = PAGE_PRESENT | PAGE_RW;
	    const uint32_t uncached_flags = common_flags | PAGE_PWT | PAGE_PCD;

	    uint32_t tables_needed = (limit_bytes + (PAGING_TABLE_COVER_BYTES - 1u)) / PAGING_TABLE_COVER_BYTES;
	    if (tables_needed == 0) {
	        tables_needed = 1;
	    }
	    if (tables_needed > PAGING_MAX_IDENTITY_TABLES) {
	        tables_needed = PAGING_MAX_IDENTITY_TABLES;
	    }
	    if (tables_needed > g_page_table_count) {
	        tables_needed = g_page_table_count;
	    }

	    for (uint32_t table = 0; table < tables_needed; ++table) {
	        uint32_t* pt = g_page_tables[table];
	        for (uint32_t entry = 0; entry < PAGING_PT_ENTRIES; ++entry) {
	            uint32_t phys = ((table * PAGING_PT_ENTRIES) + entry) * PAGING_PAGE_SIZE;
	            uint32_t flags = common_flags;
	            if (phys >= 0x000A0000u && phys <= 0x000BFFFFu) {
	                flags = uncached_flags;
	            }
	            pt[entry] = phys | flags;
	        }
	        g_page_directory[table] = (uint32_t)(uintptr_t)pt | common_flags;
	    }

	    for (uint32_t i = tables_needed; i < PAGING_PT_ENTRIES; ++i) {
	        g_page_directory[i] = 0u;
	    }
	}

	void paging_init(const boot_info_t* info) {
	    if (g_paging_enabled) {
	        return;
	    }
	    if (g_paging_attempted) {
	        return;
	    }
	    g_paging_attempted = 1;

	    uint32_t highest = detect_identity_limit(info);
	    g_identity_limit = highest;

	    if (!paging_should_enable()) {
	        return;
	    }

	    uint32_t tables_needed = (highest + (PAGING_TABLE_COVER_BYTES - 1u)) / PAGING_TABLE_COVER_BYTES;
	    if (tables_needed > PAGING_MAX_IDENTITY_TABLES) {
	        tables_needed = PAGING_MAX_IDENTITY_TABLES;
	    }
	    if (!paging_alloc_structures(tables_needed)) {
	        return;
	    }
	    paging_build_identity_map(highest);

	    uint32_t cr3 = (uint32_t)(uintptr_t)g_page_directory;
	    __asm__ volatile ("mov %0, %%cr3" :: "r"(cr3) : "memory");

	    uint32_t cr0;
	    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
	    cr0 |= 0x80000000u; // CR0.PG
	    __asm__ volatile ("mov %0, %%cr0" :: "r"(cr0) : "memory");
	    __asm__ volatile ("mov %0, %%cr3" :: "r"(cr3) : "memory");

	    g_paging_enabled = 1;
	    g_ioremap_next = PAGING_IOREMAP_BASE;
	}

int paging_is_enabled(void) {
    if (g_paging_enabled) {
        return 1;
    }
    uint32_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    return (cr0 & 0x80000000u) ? 1 : 0;
}

uint32_t paging_identity_limit(void) {
    return g_identity_limit;
}

void* paging_ioremap(uint32_t phys_addr, uint32_t size_bytes, uint32_t flags) {
    if (size_bytes == 0) {
        return NULL;
    }
    if (!paging_is_enabled()) {
        return (void*)(uintptr_t)phys_addr;
    }
    if (!g_page_directory) {
        return NULL;
    }

    uint32_t phys_aligned = align_down(phys_addr, PAGING_PAGE_SIZE);
    uint32_t offset = phys_addr - phys_aligned;
    uint32_t total = size_bytes + offset;
    uint32_t map_len = align_up(total, PAGING_PAGE_SIZE);
    if (map_len < total) {
        return NULL;
    }

    uint32_t vbase = align_up(g_ioremap_next, PAGING_PAGE_SIZE);
    if (vbase < g_ioremap_next) {
        return NULL;
    }
    if (vbase >= PAGING_IOREMAP_LIMIT) {
        return NULL;
    }
    if (map_len > (PAGING_IOREMAP_LIMIT - vbase)) {
        return NULL;
    }

    const uint32_t common_flags = PAGE_PRESENT | PAGE_RW;
    uint32_t pte_flags = common_flags;
    if (flags & PAGING_IOREMAP_UNCACHED) {
        pte_flags |= (PAGE_PWT | PAGE_PCD);
    }

    // Create mappings page by page.
    for (uint32_t mapped = 0; mapped < map_len; mapped += PAGING_PAGE_SIZE) {
        uint32_t vaddr = vbase + mapped;
        uint32_t paddr = phys_aligned + mapped;

        uint32_t pde_flags = common_flags;
        if (pte_flags & (PAGE_PWT | PAGE_PCD)) {
            pde_flags |= (PAGE_PWT | PAGE_PCD);
        }
        uint32_t* pt = paging_get_or_create_pt(vaddr, pde_flags);
        if (!pt) {
            return NULL;
        }
        uint32_t pt_index = (vaddr >> 12) & 0x3FFu;
        pt[pt_index] = (paddr & 0xFFFFF000u) | pte_flags;
        paging_invlpg(vaddr);
    }

    g_ioremap_next = vbase + map_len;
    return (void*)(uintptr_t)(vbase + offset);
}

int paging_is_ioremapped_ptr(const void* ptr) {
    if (!ptr) {
        return 0;
    }
    if (!paging_is_enabled()) {
        return 0;
    }
    uint32_t v = (uint32_t)(uintptr_t)ptr;
    if (v < PAGING_IOREMAP_BASE) {
        return 0;
    }
    // Only accept addresses we have actually handed out so far.
    return (g_ioremap_next != 0 && v < g_ioremap_next) ? 1 : 0;
}

#else

void paging_init(const boot_info_t* info) {
    (void)info;
}

int paging_is_enabled(void) {
    return 0;
}

uint32_t paging_identity_limit(void) {
    return 0;
}

void* paging_ioremap(uint32_t phys_addr, uint32_t size_bytes, uint32_t flags) {
    (void)size_bytes;
    (void)flags;
    return (void*)(uintptr_t)phys_addr;
}

int paging_is_ioremapped_ptr(const void* ptr) {
    (void)ptr;
    return 0;
}

#endif
