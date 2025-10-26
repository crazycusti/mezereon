#include "avga2.h"
#include "vga_hw.h"
#include "../../console.h"
#include "../../config.h"

#include <stddef.h>
#include <stdint.h>

#if CONFIG_ARCH_X86

#define AVGA2_BIOS_BASE        0x000C0000u
#define AVGA2_BIOS_LENGTH      0x00020000u
#define AVGA2_VRAM_WINDOW_PHYS 0x000A0000u
#define AVGA2_VRAM_WINDOW_SIZE 0x00040000u

#endif

typedef struct {
    int scanned;
    int present;
    uint32_t offset;
    char signature[48];
} avga2_signature_state_t;

static avga2_signature_state_t g_avga2_state = { 0, 0, 0, { '\0' } };

static void avga2_copy_string(char* dst, size_t dst_len, const char* src) {
    if (!dst || dst_len == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    size_t i = 0;
    while (i + 1 < dst_len && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

#if CONFIG_ARCH_X86

static char avga2_to_upper(uint8_t ch) {
    if (ch >= 'a' && ch <= 'z') {
        return (char)(ch - ('a' - 'A'));
    }
    return (char)ch;
}

static int avga2_match_at(const volatile uint8_t* base, uint32_t offset, const char* needle) {
    if (!base || !needle) {
        return 0;
    }
    for (uint32_t i = 0; needle[i] != '\0'; ++i) {
        uint8_t bios_ch = base[offset + i];
        if (bios_ch == 0x00) {
            return 0;
        }
        if (avga2_to_upper(bios_ch) != avga2_to_upper((uint8_t)needle[i])) {
            return 0;
        }
    }
    return 1;
}

static void avga2_extract_signature(const volatile uint8_t* base, uint32_t offset) {
    size_t out_index = 0;
    size_t max_len = sizeof(g_avga2_state.signature) - 1u;
    while (out_index < max_len) {
        uint8_t ch = base[offset + out_index];
        if (ch == 0x00 || ch == '\r' || ch == '\n') {
            break;
        }
        if (ch < 0x20 || ch > 0x7E) {
            break;
        }
        g_avga2_state.signature[out_index++] = (char)ch;
    }
    g_avga2_state.signature[out_index] = '\0';
}

static void avga2_scan_signature(void) {
    if (g_avga2_state.scanned) {
        return;
    }
    g_avga2_state.scanned = 1;
    g_avga2_state.present = 0;
    g_avga2_state.offset = 0;
    g_avga2_state.signature[0] = '\0';

    const char* needles[] = {
        "ACUMOS AVGA2",
        "ACUMOS AVGA 2",
        "AVGA2 BIOS",
    };

    const volatile uint8_t* bios = (const volatile uint8_t*)(uintptr_t)AVGA2_BIOS_BASE;
    for (uint32_t offset = 0; offset + 16u < AVGA2_BIOS_LENGTH; ++offset) {
        for (size_t n = 0; n < sizeof(needles) / sizeof(needles[0]); ++n) {
            const char* needle = needles[n];
            if (!needle || !needle[0]) {
                continue;
            }
            if (avga2_match_at(bios, offset, needle)) {
                g_avga2_state.present = 1;
                g_avga2_state.offset = offset;
                avga2_extract_signature(bios, offset);
                return;
            }
        }
    }
}

#else

static void avga2_scan_signature(void) {
    g_avga2_state.scanned = 1;
    g_avga2_state.present = 0;
    g_avga2_state.offset = 0;
    g_avga2_state.signature[0] = '\0';
}

#endif

void avga2_classify_info(gpu_info_t* info) {
    if (!info) {
        return;
    }
#if CONFIG_ARCH_X86
    avga2_scan_signature();
    if (!g_avga2_state.present) {
        return;
    }
    info->type = GPU_TYPE_AVGA2;
    avga2_copy_string(info->name, sizeof(info->name),
                      g_avga2_state.signature[0] ? g_avga2_state.signature : "Acumos AVGA2");
    if (info->framebuffer_base == 0) {
        info->framebuffer_base = AVGA2_VRAM_WINDOW_PHYS;
    }
    if (info->framebuffer_size == 0) {
        info->framebuffer_size = AVGA2_VRAM_WINDOW_SIZE;
    }
    gpu_debug_log("INFO", "avga2: BIOS signature detected");
#else
    (void)info;
#endif
}

int avga2_signature_present(void) {
#if CONFIG_ARCH_X86
    avga2_scan_signature();
    return g_avga2_state.present;
#else
    return 0;
#endif
}

void avga2_dump_state(void) {
#if CONFIG_ARCH_X86
    avga2_scan_signature();
    console_writeln("      Acumos AVGA2 diagnostics:");
    if (!g_avga2_state.present) {
        console_writeln("        BIOS signature: not found");
        return;
    }
    console_write("        BIOS signature offset: 0x");
    console_write_hex32(AVGA2_BIOS_BASE + g_avga2_state.offset);
    console_writeln("");
    console_write("        Signature text: ");
    console_writeln(g_avga2_state.signature);

    uint8_t seq6 = vga_seq_read(0x06);
    uint8_t crtc33 = vga_crtc_read(0x33);
    uint8_t gc06 = vga_gc_read(0x06);
    console_write("        VGA regs: SEQ[06]=0x");
    console_write_hex16(seq6);
    console_write(" CRTC[33]=0x");
    console_write_hex16(crtc33);
    console_write(" GC[06]=0x");
    console_write_hex16(gc06);
    console_writeln("");

    console_write("        Linear window: base=0x");
    console_write_hex32(AVGA2_VRAM_WINDOW_PHYS);
    console_write(" size=0x");
    console_write_hex32(AVGA2_VRAM_WINDOW_SIZE);
    console_writeln("");
#else
    console_writeln("      Acumos AVGA2 diagnostics not supported on this architecture.");
#endif
}
