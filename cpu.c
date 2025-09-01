#include <stdint.h>
#include <stdbool.h>
#include "console.h"

const char* cpu_arch_name(void) {
    return "x86"; // current build is i386
}

static inline void cpuid_raw(uint32_t leaf, uint32_t* a, uint32_t* b, uint32_t* c, uint32_t* d) {
    uint32_t ra, rb, rc, rd;
    __asm__ volatile ("cpuid" : "=a"(ra), "=b"(rb), "=c"(rc), "=d"(rd) : "a"(leaf), "c"(0));
    if (a) *a = ra; if (b) *b = rb; if (c) *c = rc; if (d) *d = rd;
}

static bool cpuid_supported(void) {
    uint32_t e1, e2;
    __asm__ volatile ("pushfl; popl %0" : "=r"(e1));
    uint32_t toggled = e1 ^ (1u << 21);
    __asm__ volatile ("pushl %0; popfl" :: "r"(toggled));
    __asm__ volatile ("pushfl; popl %0" : "=r"(e2));
    // restore
    __asm__ volatile ("pushl %0; popfl" :: "r"(e1));
    return ((e1 ^ e2) & (1u << 21)) != 0;
}

static void print_hex32(uint32_t v) {
    static const char H[] = "0123456789ABCDEF";
    char buf[11];
    buf[0]='0'; buf[1]='x';
    buf[2]=H[(v>>28)&0xF]; buf[3]=H[(v>>24)&0xF];
    buf[4]=H[(v>>20)&0xF]; buf[5]=H[(v>>16)&0xF];
    buf[6]=H[(v>>12)&0xF]; buf[7]=H[(v>>8)&0xF];
    buf[8]=H[(v>>4)&0xF];  buf[9]=H[(v>>0)&0xF]; buf[10]=0;
    console_write(buf);
}

void cpuinfo_print(void) {
    console_write("arch=");
    console_write(cpu_arch_name());
    console_write("\n");

    if (!cpuid_supported()) {
        console_write("cpuid=not supported\n");
        return;
    }

    // Vendor string
    uint32_t a=0,b=0,c=0,d=0;
    cpuid_raw(0, &a, &b, &c, &d);
    char vendor[13];
    ((uint32_t*)vendor)[0] = b;
    ((uint32_t*)vendor)[1] = d;
    ((uint32_t*)vendor)[2] = c;
    vendor[12] = '\0';
    console_write("vendor="); console_write(vendor); console_write("\n");

    // Basic feature info
    cpuid_raw(1, &a, &b, &c, &d);
    uint32_t stepping = (a & 0xF);
    uint32_t model = (a >> 4) & 0xF;
    uint32_t family = (a >> 8) & 0xF;
    uint32_t ext_model = (a >> 16) & 0xF;
    uint32_t ext_family = (a >> 20) & 0xFF;
    if (family == 0xF) family += ext_family;
    if (family == 0x6 || family == 0xF) model |= (ext_model << 4);
    console_write("family="); console_write_dec(family);
    console_write(" model="); console_write_dec(model);
    console_write(" stepping="); console_write_dec(stepping);
    console_write(" eax="); print_hex32(a);
    console_write("\n");

    // Feature bits summary
    console_write("features:");
    struct { const char* name; int reg; int bit; } feats[] = {
        {"tsc", 1, 4}, {"pae", 1, 6}, {"apic",1,9}, {"cmov",1,15}, {"mmx",1,23},
        {"sse",1,25}, {"sse2",1,26}, {"htt",1,28}, {"sse3",2,0}, {"ssse3",2,9},
        {"sse4.1",2,19}, {"sse4.2",2,20}, {"avx",2,28}
    };
    for (unsigned i=0;i<sizeof(feats)/sizeof(feats[0]);i++) {
        bool present = false;
        if (feats[i].reg == 1) present = (d >> feats[i].bit) & 1u; // EDX
        else if (feats[i].reg == 2) present = (c >> feats[i].bit) & 1u; // ECX
        if (present) { console_write(" "); console_write(feats[i].name); }
    }
    // NX/LM from extended leaf
    uint32_t maxext=0; cpuid_raw(0x80000000u, &maxext, 0, 0, 0);
    if (maxext >= 0x80000001u) {
        uint32_t ea, eb, ec, ed; cpuid_raw(0x80000001u, &ea, &eb, &ec, &ed);
        if (ed & (1u<<20)) { console_write(" nx"); }
        if (ed & (1u<<29)) { console_write(" lm"); }
    }
    console_write("\n");

    // Brand string if available
    if (maxext >= 0x80000004u) {
        char brand[49];
        uint32_t* p = (uint32_t*)brand;
        for (uint32_t i=0;i<3;i++) {
            cpuid_raw(0x80000002u + i, &p[i*4+0], &p[i*4+1], &p[i*4+2], &p[i*4+3]);
        }
        brand[48] = '\0';
        // Trim leading spaces
        char* s = brand; while (*s==' ') s++;
        console_write("brand="); console_write(s); console_write("\n");
    }
}

void cpu_bootinfo_print(void) {
    // Compact one-line summary for boot log
    console_write("cpu: arch=");
    console_write(cpu_arch_name());

    if (!cpuid_supported()) {
        console_write(" cpuid=n/a\n");
        return;
    }

    uint32_t a=0,b=0,c=0,d=0; char vendor[13];
    cpuid_raw(0, &a, &b, &c, &d);
    ((uint32_t*)vendor)[0] = b; ((uint32_t*)vendor)[1] = d; ((uint32_t*)vendor)[2] = c; vendor[12]='\0';

    cpuid_raw(1, &a, &b, &c, &d);
    uint32_t stepping = (a & 0xF);
    uint32_t model = (a >> 4) & 0xF;
    uint32_t family = (a >> 8) & 0xF;
    uint32_t ext_model = (a >> 16) & 0xF;
    uint32_t ext_family = (a >> 20) & 0xFF;
    if (family == 0xF) family += ext_family;
    if (family == 0x6 || family == 0xF) model |= (ext_model << 4);

    console_write(" vendor="); console_write(vendor);
    console_write(" fam="); console_write_dec(family);
    console_write(" model="); console_write_dec(model);
    console_write(" step="); console_write_dec(stepping);

    uint32_t maxext=0; cpuid_raw(0x80000000u, &maxext, 0, 0, 0);
    if (maxext >= 0x80000004u) {
        char brand[49]; uint32_t* p = (uint32_t*)brand;
        for (uint32_t i=0;i<3;i++) cpuid_raw(0x80000002u + i, &p[i*4+0], &p[i*4+1], &p[i*4+2], &p[i*4+3]);
        brand[48]='\0'; char* s = brand; while (*s==' ') s++;
        console_write(" brand="); console_write(s);
    }
    console_write("\n");
}
