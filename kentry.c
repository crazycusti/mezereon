#include "bootinfo.h"
#include "config.h"
#include <stddef.h>

#if CONFIG_ARCH_X86
// Legacy kernel main (no bootinfo)
extern void kmain(const boot_info_t* info);
#endif

#if CONFIG_ARCH_SPARC
// Minimal OpenFirmware client helpers (replicated to avoid cross-unit deps)
typedef int (*ofw_entry_t)(void*);
// Local minimal libc stubs to avoid external deps (static, TU-local)
static void* memcpy(void* dst, const void* src, size_t n) {
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    while (n--) *d++ = *s++;
    return dst;
}
static void* memset(void* dst, int v, size_t n) {
    unsigned char* d = (unsigned char*)dst; unsigned char b = (unsigned char)v;
    while (n--) *d++ = b; return dst;
}
static void* memmove(void* dst, const void* src, size_t n) {
    unsigned char* d = (unsigned char*)dst; const unsigned char* s = (const unsigned char*)src;
    if (d == s || n == 0) return dst; if (d < s) { while (n--) *d++ = *s++; }
    else { d += n; s += n; while (n--) *--d = *--s; } return dst;
}
static int k_str_len(const char* s) { int n=0; while (s && s[n]) n++; return n; }
static uint32_t ofw_finddevice(ofw_entry_t ofw, const char* path) {
    volatile struct { const char* service; int nargs; int nret; const char* path; uint32_t phandle; } args = { "finddevice", 1, 1, path, 0 };
    (void)ofw((void*)&args); return args.phandle;
}
static int ofw_getprop(ofw_entry_t ofw, uint32_t ph, const char* name, void* buf, int buflen) {
    volatile struct { const char* service; int nargs; int nret; uint32_t ph; const char* name; void* buf; int buflen; int actual; } args = { "getprop", 4, 1, ph, name, buf, buflen, -1 };
    (void)ofw((void*)&args); return args.actual;
}
static int ofw_write(ofw_entry_t ofw, uint32_t ih, const void* buf, int len) {
    volatile struct { const char* service; int nargs; int nret; uint32_t ih; const void* buf; int len; int actual; } args = { "write", 3, 1, ih, buf, len, -1 };
    (void)ofw((void*)&args); return args.actual;
}
#endif

// New arch-neutral entry point.
// Early debug output directly to VGA
static void early_debug(char c) {
    volatile char* vga = (volatile char*)0xB8000;
    *vga = c;
    *(vga+1) = 0x07;  // Light gray on black
}

void kentry(void* bi) {
#if CONFIG_ARCH_X86
    early_debug('K');  // Kernel entered
    const boot_info_t* info = (const boot_info_t*)bi;
    if (!info) {
        static boot_info_t fallback;
        info = &fallback;
    }
    kmain(info);
#elif CONFIG_ARCH_SPARC
    boot_info_t* info = (boot_info_t*)bi;
    ofw_entry_t ofw = (info && info->prom) ? (ofw_entry_t)info->prom : (ofw_entry_t)0;
    if (ofw) {
        uint32_t chosen = ofw_finddevice(ofw, "/chosen");
        uint32_t ih = 0;
        if (chosen) {
            int n = ofw_getprop(ofw, chosen, "stdout", &ih, (int)sizeof(ih));
            if (n != (int)sizeof(ih) || (int)ih == -1) ih = 0;
        }
        if (ih) {
            const char pre[] = "Mezereon SPARC kentry: ";
            const char ver[] = CONFIG_KERNEL_VERSION;
            const char end[] = "\r\n";
            ofw_write(ofw, ih, pre, k_str_len(pre));
            ofw_write(ofw, ih, ver, k_str_len(ver));
            ofw_write(ofw, ih, end, 2);
        }
    }
    // Halt loop to keep control under OBP
    for(;;) { __asm__ volatile ("nop"); }
#else
    (void)bi; return;
#endif
}
