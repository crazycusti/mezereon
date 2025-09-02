#include "../../bootinfo.h"

typedef int (*ofw_entry_t)(void*);

extern void kentry(void* bootinfo);

// Minimal IEEE-1275 (OpenFirmware) client interface: interpret
static int ofw_interpret(ofw_entry_t ofw, const char* forth) {
    struct {
        const char* service; int nargs; int nret;
        const char* forth;   int catch_result;
    } args = { "interpret", 1, 1, forth, 0 };
    (void)ofw(&args);
    return args.catch_result;
}

void sparc_boot_main(void* ofw) {
    ofw_entry_t entry = (ofw_entry_t)ofw;
    // Print a hello via OF 'interpret'
    ofw_interpret(entry, " .\" Mezereon SPARC boot stub...\" cr");

    static const char c_backend[] = "ofw";
    boot_info_t bi;
    bi.arch = BI_ARCH_SPARC;
    bi.machine = 0;
    bi.console = c_backend;
    bi.flags = 0;
    bi.prom  = ofw;

    kentry(&bi);

    // If kentry returns, drop into forth prompt
    ofw_interpret(entry, " enter");
    for(;;) { /* spin */ }
}
