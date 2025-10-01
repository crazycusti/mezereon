#include "sb16.h"
#include "../config.h"
#include "../arch/x86/io.h"
#include <stddef.h>

// DSP ports relative to legacy base address
#define SB16_DSP_RESET          0x06
#define SB16_DSP_READ_DATA      0x0A
#define SB16_DSP_WRITE_DATA     0x0C
#define SB16_DSP_WRITE_STATUS   0x0C
#define SB16_DSP_READ_STATUS    0x0E

static sb16_info_t g_sb16_info = {0};
static bool g_sb16_initialized = false;

static void sb16_io_delay(void)
{
    // A couple of OUT 0x80 delays to satisfy DSP timing requirements
    io_delay();
    io_delay();
}

static void sb16_dsp_flush(uint16_t base)
{
    for (int i = 0; i < 32; i++) {
        if (!(inb((uint16_t)(base + SB16_DSP_READ_STATUS)) & 0x80)) {
            break;
        }
        (void)inb((uint16_t)(base + SB16_DSP_READ_DATA));
    }
}

static bool sb16_dsp_wait_read(uint16_t base)
{
    for (int i = 0; i < 4096; i++) {
        if (inb((uint16_t)(base + SB16_DSP_READ_STATUS)) & 0x80) {
            return true;
        }
        sb16_io_delay();
    }
    return false;
}

static bool sb16_dsp_wait_write(uint16_t base)
{
    for (int i = 0; i < 4096; i++) {
        if ((inb((uint16_t)(base + SB16_DSP_WRITE_STATUS)) & 0x80) == 0) {
            return true;
        }
        sb16_io_delay();
    }
    return false;
}

static bool sb16_dsp_write(uint16_t base, uint8_t value)
{
    if (!sb16_dsp_wait_write(base)) {
        return false;
    }
    outb((uint16_t)(base + SB16_DSP_WRITE_DATA), value);
    return true;
}

static bool sb16_dsp_read(uint16_t base, uint8_t* value)
{
    if (!sb16_dsp_wait_read(base)) {
        return false;
    }
    *value = inb((uint16_t)(base + SB16_DSP_READ_DATA));
    return true;
}

static bool sb16_dsp_reset(uint16_t base)
{
    sb16_dsp_flush(base);
    outb((uint16_t)(base + SB16_DSP_RESET), 1);
    sb16_io_delay();
    sb16_io_delay();
    outb((uint16_t)(base + SB16_DSP_RESET), 0);
    if (!sb16_dsp_wait_read(base)) {
        return false;
    }
    uint8_t ident = inb((uint16_t)(base + SB16_DSP_READ_DATA));
    return ident == 0xAA;
}

static bool sb16_dsp_get_version(uint16_t base, uint8_t* major, uint8_t* minor)
{
    if (!sb16_dsp_write(base, 0xE1)) { // DSP version command
        return false;
    }
    uint8_t maj = 0;
    uint8_t min = 0;
    if (!sb16_dsp_read(base, &maj)) {
        return false;
    }
    if (!sb16_dsp_read(base, &min)) {
        return false;
    }
    if (major) *major = maj;
    if (minor) *minor = min;
    return true;
}

static bool sb16_try_port(uint16_t base)
{
    if (!sb16_dsp_reset(base)) {
        return false;
    }
    uint8_t vmaj = 0;
    uint8_t vmin = 0;
    if (!sb16_dsp_get_version(base, &vmaj, &vmin)) {
        return false;
    }
    g_sb16_info.base_port = base;
    g_sb16_info.irq = (uint8_t)CONFIG_SB16_IRQ;
    g_sb16_info.dma8 = (uint8_t)CONFIG_SB16_DMA8;
    g_sb16_info.dma16 = (uint8_t)CONFIG_SB16_DMA16;
    g_sb16_info.version_major = vmaj;
    g_sb16_info.version_minor = vmin;
    g_sb16_info.present = 1;
    return true;
}

void sb16_init(void)
{
    if (g_sb16_initialized) {
        return;
    }
    g_sb16_initialized = true;
    g_sb16_info.present = 0;

#if CONFIG_SB16_ENABLE
    uint16_t candidates[6];
    size_t count = 0;
    uint16_t configured = (uint16_t)CONFIG_SB16_IO;
    if (configured) {
        candidates[count++] = configured;
    }
#if CONFIG_SB16_SCAN
    static const uint16_t k_defaults[] = { 0x220, 0x240, 0x260, 0x280 };
    for (size_t i = 0; i < sizeof(k_defaults)/sizeof(k_defaults[0]); i++) {
        uint16_t port = k_defaults[i];
        int seen = 0;
        for (size_t j = 0; j < count; j++) {
            if (candidates[j] == port) { seen = 1; break; }
        }
        if (!seen) {
            candidates[count++] = port;
        }
    }
#endif // CONFIG_SB16_SCAN

    for (size_t i = 0; i < count; i++) {
        if (sb16_try_port(candidates[i])) {
            return;
        }
    }
#endif // CONFIG_SB16_ENABLE
}

bool sb16_present(void)
{
    return g_sb16_info.present != 0;
}

const sb16_info_t* sb16_get_info(void)
{
    if (!sb16_present()) {
        return NULL;
    }
    return &g_sb16_info;
}

int sb16_pcm_submit(const void* data, uint32_t length)
{
    (void)data;
    (void)length;
    return -1;
}
