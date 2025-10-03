#include "bootinfo.h"
#include "config.h"
#include <stddef.h>

#if CONFIG_ARCH_X86
extern void kmain(const boot_info_t* info);
#endif

// Debug helper - direkte VGA-Ausgabe
static void debug_vga_char(char c, int pos) {
    volatile unsigned short *vga = (volatile unsigned short *)0xB8000;
    vga[pos] = (0x0F << 8) | c;  // Weiß auf schwarz
}

static void debug_vga_string(const char* str, int start_pos) {
    for (int i = 0; str[i] && i < 20; i++) {
        debug_vga_char(str[i], start_pos + i);
    }
}

void kentry(void* bi) {
    // Debug: Zeige dass wir kentry erreichen
    debug_vga_string("KENTRY OK", 0);
    
#if CONFIG_ARCH_X86
    const boot_info_t* info = (const boot_info_t*)bi;
    if (!info) {
        static boot_info_t fallback;
        info = &fallback;
    }
    
    // Debug: Zeige dass wir kmain aufrufen
    debug_vga_string("CALLING KMAIN", 20);
    
    kmain(info);
    
    // Falls kmain zurückkehrt (sollte nicht passieren)
    debug_vga_string("KMAIN RETURNED", 40);
    
#elif CONFIG_ARCH_SPARC
    // SPARC code bleibt unverändert
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
    for(;;) { __asm__ volatile ("nop"); }
#else
    (void)bi; 
    debug_vga_string("UNSUPPORTED ARCH", 0);
    for(;;);
#endif
}
