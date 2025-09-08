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

void kmain()
{
    console_init();
    console_writeln("Initializing Mezereon... Video initialized.");
    // Compact CPU info line
    cpu_bootinfo_print();
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
    keyboard_init();
    shell_run();
}
