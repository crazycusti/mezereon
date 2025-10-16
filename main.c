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

#include "video_fb.h"
#include "statusbar.h"

void kmain(const boot_info_t* bootinfo)
{
    size_t pci_device_count = 0;
    display_manager_init(CONFIG_VIDEO_TARGET);
    statusbar_init();
    console_init();
    console_writeln("kmain: entering");
    memory_init(bootinfo);
    /* Automatische Aktivierung des Bootloader-Framebuffers, falls vorhanden */
    if (bootinfo && bootinfo->framebuffer_phys != 0 && bootinfo->vbe_width && bootinfo->vbe_height && bootinfo->vbe_bpp == 8) {
    volatile uint8_t* fb_ptr = (volatile uint8_t*)(uintptr_t)bootinfo->framebuffer_phys;
        display_mode_info_t mode = {
            .kind = DISPLAY_MODE_KIND_FRAMEBUFFER,
            .pixel_format = DISPLAY_PIXEL_FORMAT_PAL_256,
            .width = bootinfo->vbe_width,
            .height = bootinfo->vbe_height,
            .bpp = bootinfo->vbe_bpp,
            .pitch = bootinfo->vbe_pitch,
            .phys_base = bootinfo->framebuffer_phys,
            .framebuffer = fb_ptr
        };
        display_manager_set_framebuffer_candidate("bootinfo-lfb", &mode);
        display_manager_activate_framebuffer();
        video_switch_to_framebuffer(&mode);
        console_writeln("Display: Bootloader-Framebuffer aktiviert.");
    }
    console_writeln("Initializing Mezereon... Video initialized.");
    display_manager_log_state();
    // Compact CPU info line
    cpu_bootinfo_print();
    memory_log_summary();

    pci_init();
    pci_get_devices(&pci_device_count);
    console_write("PCI: detected ");
    console_write_dec((uint32_t)pci_device_count);
    console_writeln(" device(s).");

    gpu_init();
    gpu_log_summary();

    if (CONFIG_VIDEO_TARGET != CONFIG_VIDEO_TARGET_TEXT) {
        uint16_t preferred_height = 480;
#if CONFIG_VIDEO_ENABLE_ET4000
        if (CONFIG_VIDEO_ET4000_MODE == CONFIG_VIDEO_ET4000_MODE_640x400x8) {
            preferred_height = 400;
        }
#endif
        int fb_enabled = gpu_request_framebuffer_mode(640, preferred_height, 8);
        if (!fb_enabled && preferred_height != 480) {
            fb_enabled = gpu_request_framebuffer_mode(640, 480, 8);
        }
        if (fb_enabled) {
            const display_state_t* st = display_manager_state();
            if (st && st->active_mode.kind == DISPLAY_MODE_KIND_FRAMEBUFFER) {
                console_write("Display: Framebuffer ");
                console_write_dec(st->active_mode.width);
                console_write("x");
                console_write_dec(st->active_mode.height);
                console_write("@");
                console_write_dec(st->active_mode.bpp);
                console_write(" via ");
                console_write(st->active_driver_name ? st->active_driver_name : "(unbekannt)");
                console_writeln(".");
            } else {
                console_writeln("Display: Framebuffer aktiviert.");
            }
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
