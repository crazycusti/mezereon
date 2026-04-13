#include "main.h"
#include "shell.h"
#include "keyboard.h"
#include "platform.h"
#include "cpu.h"
#include "drivers/storage.h"
#include "console.h"
#include "cpuidle.h"
#include "net/ipv4.h"
#include "drivers/pcspeaker.h"
#include "drivers/sb16.h"
#include "display.h"
#include "drivers/pci.h"
#include "drivers/gpu/gpu.h"
#include "version.h"
#include <stddef.h>
#include "memory.h"
#include "paging.h"
#include "debug_serial.h"

#include "video_fb.h"
#include "statusbar.h"

void kmain(const boot_info_t* bootinfo)
{
    debug_serial_plugin_init(bootinfo);
    /* Full init with fallback bootinfo (E820 may be synthetic) */
    display_manager_init(CONFIG_VIDEO_TARGET);
    statusbar_init();
    console_init();
    console_writeln("kmain: entering");
    // --- E820 sanity dump and fallback map ---
    console_writeln("E820 dump:");
    if (bootinfo) {
        uint32_t count = bootinfo->memory.entry_count;
        console_write(" count=");
        console_write_dec(count);
        console_write("\n");
        if (count > BOOTINFO_MEMORY_MAX_RANGES) count = BOOTINFO_MEMORY_MAX_RANGES;
        for (uint32_t i = 0; i < count; i++) {
            const bootinfo_memory_range_t* r = &bootinfo->memory.entries[i];
            console_write("  [");
            console_write_dec(i);
            console_write("] base=0x");
            console_write_hex32((uint32_t)(r->base & 0xFFFFFFFFu));
            console_write(" len=0x");
            console_write_hex32((uint32_t)(r->length & 0xFFFFFFFFu));
            console_write(" type=");
            console_write_dec(r->type);
            console_write("\n");
        }
    } else {
        console_writeln(" bootinfo missing");
    }

    // Fallback: if memory map is missing/bogus, replace with a simple synthesized map.
    // Prefer BIOS sizing (INT 15h E801/88h) if stage2 captured it.
    int use_fallback_mem = 0;
    if (!bootinfo) {
        use_fallback_mem = 1;
    } else if (bootinfo->memory.entry_count == 0) {
        use_fallback_mem = 1;
    } else {
        // If E820 reports no usable memory above 1MiB but BIOS sizing says there is,
        // treat the E820 map as broken and synthesize a contiguous extended range.
        uint64_t usable_above_1m = 0;
        uint32_t count = bootinfo->memory.entry_count;
        if (count > BOOTINFO_MEMORY_MAX_RANGES) count = BOOTINFO_MEMORY_MAX_RANGES;
        for (uint32_t i = 0; i < count; i++) {
            const bootinfo_memory_range_t* r = &bootinfo->memory.entries[i];
            if (r->type != BOOTINFO_MEMORY_TYPE_USABLE || r->length == 0) {
                continue;
            }
            uint64_t base = r->base;
            uint64_t end = r->base + r->length;
            if (end <= (1ull << 20)) {
                continue;
            }
            if (base < (1ull << 20)) {
                base = (1ull << 20);
            }
            if (end > base) {
                usable_above_1m += (end - base);
            }
        }
        if (usable_above_1m == 0 && bootinfo->bios_extended_kb != 0) {
            use_fallback_mem = 1;
        }
    }

    boot_info_t fallback_bi;
    if (use_fallback_mem) {
        console_writeln("E820 fallback: synthesized memory map");
        // Build minimal bootinfo with a contiguous usable range (avoid low-memory BIOS areas).
        if (bootinfo) {
            fallback_bi = *bootinfo; // copy metadata if present
        } else {
            fallback_bi.arch = BI_ARCH_X86;
            fallback_bi.machine = 0;
            fallback_bi.console = "vga_text";
            fallback_bi.flags = 0;
            fallback_bi.prom = NULL;
            fallback_bi.boot_device = 0;
        }
        fallback_bi.memory.entry_count = 0;
        uint32_t ext_kb = bootinfo ? bootinfo->bios_extended_kb : 0;
        uint32_t conv_kb = bootinfo ? bootinfo->bios_conventional_kb : 0;
        if (ext_kb) {
            fallback_bi.memory.entries[0].base = 0x00100000ull;
            fallback_bi.memory.entries[0].length = (uint64_t)ext_kb * 1024ull;
            fallback_bi.memory.entries[0].type = BOOTINFO_MEMORY_TYPE_USABLE;
            fallback_bi.memory.entries[0].attr = 0;
            fallback_bi.memory.entry_count = 1;
        } else if (conv_kb) {
            fallback_bi.memory.entries[0].base = 0x00000000ull;
            fallback_bi.memory.entries[0].length = (uint64_t)conv_kb * 1024ull;
            fallback_bi.memory.entries[0].type = BOOTINFO_MEMORY_TYPE_USABLE;
            fallback_bi.memory.entries[0].attr = 0;
            fallback_bi.memory.entry_count = 1;
        } else {
            // Last resort: assume at least 16MiB is available.
            fallback_bi.memory.entries[0].base = 0x00100000ull;
            fallback_bi.memory.entries[0].length = 15ull * 1024ull * 1024ull;
            fallback_bi.memory.entries[0].type = BOOTINFO_MEMORY_TYPE_USABLE;
            fallback_bi.memory.entries[0].attr = 0;
            fallback_bi.memory.entry_count = 1;
        }
        bootinfo = &fallback_bi;
    }

    memory_init(bootinfo);
    paging_init(bootinfo);
    console_writeln(paging_is_enabled() ? "Paging: enabled" : "Paging: disabled");
    display_manager_apply_active_mode();
    console_writeln("Initializing Mezereon... Video path selected.");
    // Compact CPU info line
    cpu_bootinfo_print();
    memory_log_summary();

    pci_init();
    size_t pci_count = 0;
    pci_get_devices(&pci_count);
    console_write("PCI: devices=");
    console_write_dec((uint32_t)pci_count);
    console_write("\n");
    gpu_init(bootinfo);
    gpu_log_summary();
    if (CONFIG_VIDEO_TARGET != CONFIG_VIDEO_TARGET_TEXT) {
        const gpu_info_t* primary = gpu_get_primary();
        int activated = 0;
        if (primary) {
            if (gpu_streq(primary->name, "SMOS SPC8106F0B (Aero)") || 
                gpu_streq(primary->name, "SMOS SPC8106F0A") ||
                gpu_streq(primary->name, "Compaq SPC8106 (Aero)")) {
                console_writeln("GPU: SMOS detected, auto-activating 640x400x8...");
                if (gpu_request_framebuffer_mode(640, 400, 8)) activated = 1;
                else if (gpu_request_framebuffer_mode(320, 200, 8)) activated = 1;
            } else if (gpu_streq(primary->name, "Acumos AVGA2 (Cirrus ISA)")) {
                console_writeln("GPU: AVGA2 detected, auto-activating 640x480x8...");
                if (gpu_request_framebuffer_mode(640, 480, 8)) activated = 1;
                else if (gpu_request_framebuffer_mode(320, 200, 8)) activated = 1;
            }
        }
        
        if (!activated) {
             gpu_request_framebuffer_mode(0, 0, 0);
        }
    }
    display_manager_apply_active_mode();
    display_manager_log_state();

    console_status_set_right(GIT_REV);

    // Interrupts/timer enabled, keyboard IRQ polling disabled
    platform_interrupts_init();
    platform_quiesce_floppy();
    console_writeln("INT: timer init...");
    platform_timer_init(CONFIG_TIMER_HZ);
#if CONFIG_BOOT_ENABLE_INTERRUPTS
    console_writeln("INT: unmask IRQ0/1/3...");
    platform_irq_unmask(0); // timer
    platform_irq_unmask(3); // NE2000 (driver masks internally if needed)
    console_writeln("INT: sti");
    platform_interrupts_enable();
    console_writeln("INT: enabled");
    // Keep IRQ1 masked until keyboard_init() completes (otherwise the IRQ handler can
    // consume controller responses like ACK/BAT during init).
    keyboard_set_irq_mode(0);
#else
    console_writeln("INT: stay disabled (boot bring-up).");
    keyboard_set_irq_mode(0);
#endif
    cpuidle_init();

    // Auto-detect storage and attempt mounting NeeleFS
    console_writeln("Storage: scanning ATA (PM/PS/SM/SS)...");
    storage_scan();
    int mounted_idx = storage_automount();
    if (mounted_idx >= 0) {
        storage_info_t inf; storage_get(mounted_idx, &inf);
        console_write("Storage: mounted NeeleFS");
        console_write(inf.neelefs_ver==2?"2 ":"1 ");
        console_write("at LBA "); console_write_dec(inf.neelefs_lba);
        console_write(" on ");
        const char* slot = (inf.dev.io==CONFIG_ATA_PRIMARY_IO && !inf.dev.slave)?"PM":
                           (inf.dev.io==CONFIG_ATA_PRIMARY_IO &&  inf.dev.slave)?"PS":
                           (inf.dev.io==0x170 && !inf.dev.slave)?"SM":"SS";
        console_write(slot); console_write("\n");
        char sbuf[64]; int p=0;
        const char* vx = (inf.neelefs_ver==2)?"v2":"v1";
        const char* sep = (inf.neelefs_lba==2048)?"@2048":"@0";
        sbuf[p++]='s'; sbuf[p++]='t'; sbuf[p++]='o'; sbuf[p++]='r'; sbuf[p++]='a'; sbuf[p++]='g'; sbuf[p++]='e'; sbuf[p++]=':'; sbuf[p++]=' ';
        while (*vx && p<63) sbuf[p++]=*vx++;
        while (*sep && p<63) sbuf[p++]=*sep++;
        sbuf[p]=0; console_status_set_left(sbuf);
    } else {
        console_writeln("Storage: no NeeleFS volume found");
    }
    bool spk = pcspeaker_init();
    console_write("pcspk: ");
    console_write(spk?"present":"not present");
    console_write("\n");
    if (spk) { pcspeaker_beep(880, 60); }

    console_writeln("audio: probing SB16...");
    sb16_init();
    console_writeln("audio: SB16 probe done.");
    if (sb16_present()) {
        const sb16_info_t* info = sb16_get_info();
        console_write("audio: sb16 @0x");
        console_write_hex16(info->base_port);
        console_write(", irq=");
        console_write_dec((uint32_t)info->irq);
        console_write(", dma=");
        console_write_dec((uint32_t)info->dma8);
        console_write("/");
        console_write_dec((uint32_t)info->dma16);
        console_write(", dsp=");
        console_write_dec((uint32_t)info->version_major);
        console_putc('.');
        console_write_dec((uint32_t)info->version_minor);
        console_write("\n");
    } else {
        console_writeln("audio: sb16 not present");
    }

    console_writeln("net: probing NIC...");
    if (netface_init()) {
        net_ipv4_init();
        console_write("Network interface initialized. Selected: ");
        console_write(netface_active_name());
        console_write("\n");
        netface_bootinfo_print();
        netface_send_test();
    } else {
        console_write("No network interface present. Selected: ");
        console_write(netface_active_name());
        console_write("\n");
        netface_bootinfo_print();
    }
    console_writeln("net: probe done.");
    console_writeln("Welcome to Mezereon.");
    console_status_set_right(GIT_REV);
    keyboard_init();
#if CONFIG_BOOT_ENABLE_INTERRUPTS
    platform_irq_unmask(1); // keyboard
    keyboard_set_irq_mode(1);
    console_writeln("INT: IRQ1 (kbd) unmasked.");
#else
    keyboard_set_irq_mode(0);
#endif
    shell_run();
}
