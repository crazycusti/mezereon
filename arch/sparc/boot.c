#include "../../bootinfo.h"
#include <stddef.h>

// Minimal libc stubs local to this TU to avoid external deps when
// building freestanding (-nostdlib). These are 'static' so they only
// satisfy references generated within this file.
static void* memcpy(void* dst, const void* src, size_t n) {
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    while (n--) *d++ = *s++;
    return dst;
}
static void* memset(void* dst, int v, size_t n) {
    unsigned char* d = (unsigned char*)dst;
    unsigned char b = (unsigned char)v;
    while (n--) *d++ = b;
    return dst;
}
static void* memmove(void* dst, const void* src, size_t n) {
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    if (d == s || n == 0) return dst;
    if (d < s) { while (n--) *d++ = *s++; }
    else { d += n; s += n; while (n--) *--d = *--s; }
    return dst;
}

typedef int (*ofw_entry_t)(void*);

extern void kentry(void* bootinfo);

static int str_len(const char* s) { int n=0; while (s && s[n]) n++; return n; }

// OF client services
static uint32_t ofw_open(ofw_entry_t ofw, const char* dev) {
    volatile struct {
        const char* service; int nargs; int nret;
        const char* dev;     uint32_t ihandle;
    } args = { "open", 1, 1, dev, 0 };
    (void)ofw((void*)&args);
    return args.ihandle;
}

static int ofw_write(ofw_entry_t ofw, uint32_t ih, const void* buf, int len) {
    volatile struct {
        const char* service; int nargs; int nret;
        uint32_t ih;         const void* buf; int len; int actual;
    } args = { "write", 3, 1, ih, buf, len, -1 };
    (void)ofw((void*)&args);
    return args.actual;
}

static uint32_t ofw_finddevice(ofw_entry_t ofw, const char* path) {
    volatile struct {
        const char* service; int nargs; int nret;
        const char* path;    uint32_t phandle;
    } args = { "finddevice", 1, 1, path, 0 };
    (void)ofw((void*)&args);
    return args.phandle;
}

static int ofw_getprop(ofw_entry_t ofw, uint32_t ph, const char* name, void* buf, int buflen) {
    volatile struct {
        const char* service; int nargs; int nret;
        uint32_t ph;         const char* name; void* buf; int buflen; int actual;
    } args = { "getprop", 4, 1, ph, name, buf, buflen, -1 };
    (void)ofw((void*)&args);
    return args.actual;
}

void sparc_boot_main(void* ofw) {
    ofw_entry_t entry = (ofw_entry_t)ofw;

    // If no OF entry was provided (e.g., -kernel), skip all OF calls and go straight to kentry
    if (!entry) {
        boot_info_t bi; memset(&bi, 0, sizeof(bi));
        bi.arch = BI_ARCH_SPARC; bi.machine = 0; bi.console = 0; bi.flags = 0; bi.prom = 0;
        kentry(&bi);
        for(;;) { /* spin */ }
    }

    // Prefer stdout ihandle from /chosen; fallback to open("ttya") then "screen"
    uint32_t ih = 0;
    {
        uint32_t chosen = ofw_finddevice(entry, "/chosen");
        if (chosen) {
            uint32_t stdout_ih = 0; int n = ofw_getprop(entry, chosen, "stdout", &stdout_ih, sizeof(stdout_ih));
            if (n == sizeof(stdout_ih) && stdout_ih && (int)stdout_ih != -1) ih = stdout_ih;
        }
    }
    if (!ih || (int)ih == -1) ih = ofw_open(entry, "ttya");
    if (!ih || (int)ih == -1) ih = ofw_open(entry, "screen");

    const char hello[] = "Mezereon SPARC boot stub...\r\n";
    if (ih && (int)ih != -1) {
        ofw_write(entry, ih, hello, str_len(hello));

        // Print bootargs from /chosen if available
        uint32_t chosen = ofw_finddevice(entry, "/chosen");
        if (chosen) {
            char bootargs[256];
            int n = ofw_getprop(entry, chosen, "bootargs", bootargs, (int)sizeof(bootargs)-1);
            if (n > 0) {
                bootargs[n] = '\0';
                const char pfx[] = "bootargs=";
                ofw_write(entry, ih, pfx, str_len(pfx));
                ofw_write(entry, ih, bootargs, str_len(bootargs));
                ofw_write(entry, ih, "\r\n", 2);
            }
        }
    }

    static const char c_backend[] = "ofw";
    boot_info_t bi; memset(&bi, 0, sizeof(bi));
    bi.arch = BI_ARCH_SPARC;
    bi.machine = 0;
    bi.console = c_backend;
    bi.flags = 0;
    bi.prom  = ofw;

    kentry(&bi);

    for(;;) { /* spin */ }
}
