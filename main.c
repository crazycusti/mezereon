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

void kmain(const boot_info_t* bootinfo)
{
    display_manager_init(CONFIG_VIDEO_TARGET);
    console_init();
    console_writeln("kmain: entering");
    if (bootinfo) {
        console_write("Boot: BIOS dev=0x");
        console_write_hex16((uint16_t)(bootinfo->boot_device & 0xFFu));
        if (bootinfo->flags & BOOTINFO_FLAG_BOOT_DEVICE_IS_HDD) {
            console_write(" (hdd)");
        } else {
            console_write(" (floppy/legacy)");
        }
        console_write("\n");
    } else {
        console_writeln("Boot: legacy entry (no bootinfo)");
    }
    memory_init(bootinfo);
    console_writeln("Initializing Mezereon... Video initialized.");
    display_manager_log_state();
    // Compact CPU info line
    cpu_bootinfo_print();
    memory_log_summary();

    pci_init();
    size_t pci_device_count = 0;
    pci_get_devices(&pci_device_count);
    console_write("PCI: detected ");
    console_write_dec((uint32_t)pci_device_count);
    console_writeln(" device(s).");

    gpu_init();
    gpu_log_summary();

    if (CONFIG_VIDEO_TARGET != CONFIG_VIDEO_TARGET_TEXT) {
        if (gpu_request_framebuffer_mode(640, 480, 8)) {
            console_writeln("Display: Cirrus framebuffer 640x480@8 aktiv.");
            display_manager_log_state();
        } else if (CONFIG_VIDEO_TARGET == CONFIG_VIDEO_TARGET_FRAMEBUFFER) {
            console_writeln("Display: Framebuffer angefordert, aber kein passender Adapter.");
        }
    }

    console_status_set_right(GIT_REV);

    // Platform init: IDT/PIC remap, PIT 100Hz, unmask required IRQs and enable
    platform_interrupts_init();
    platform_timer_init(CONFIG_TIMER_HZ);
    platform_irq_unmask(0); // timer
    platform_irq_unmask(1); // keyboard
    platform_irq_unmask(3); // NE2000 (driver masks internally if needed)
    platform_interrupts_enable();
    keyboard_set_irq_mode(1);
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
        // Status bar left text
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
    // Initialize PC speaker (best-effort) and log
    bool spk = pcspeaker_init();
    console_write("pcspk: ");
    console_write(spk?"present":"not present");
    console_write("\n");
    if (spk) { pcspeaker_beep(880, 60); }

    sb16_init();
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
        console_writeln("audio: sb16 not detected");
    }

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
    console_writeln("Welcome to Mezereon.");
    console_status_set_right(GIT_REV);
    keyboard_init();
    shell_run();
}
