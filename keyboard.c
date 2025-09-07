#include "keyboard.h"
#include <stdint.h>
#include "arch/x86/io.h"

#define KBD_STATUS 0x64
#define KBD_DATA   0x60

static bool shift = false;
static bool caps = false;
static bool ctrl = false; // Left/Right Control
static bool ext  = false; // E0 prefix

// Simple ring buffer for scancodes filled by IRQ1
#define KBD_QSIZE 32
static volatile uint8_t qbuf[KBD_QSIZE];
static volatile uint8_t qhead = 0, qtail = 0;
static volatile int kbd_irq_mode = 0;

void keyboard_isr_byte(uint8_t sc) {
    uint8_t n = (uint8_t)((qhead + 1) % KBD_QSIZE);
    if (n != qtail) { qbuf[qhead] = sc; qhead = n; }
}

void keyboard_set_irq_mode(int enabled) { kbd_irq_mode = enabled ? 1 : 0; }

// Set 1 scancode to ASCII (no shift)
static const char keymap[128] = {
    0,  27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n', 0,
    'a','s','d','f','g','h','j','k','l',';','\'','`',  0, '\\','z','x','c','v','b','n','m',',','.','/', 0, '*', 0, ' ',
    0,  0,   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,   0,  0,   0,  0,
    0,  0,   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,   0,  0,   0,  0,
    0,  0,   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,   0,  0,   0,  0,
    0,  0,   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,   0,  0,   0,  0
};

static const char keymap_shift[128] = {
    0,  27, '!','@','#','$','%','^','&','*','(',')','_','+', '\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n', 0,
    'A','S','D','F','G','H','J','K','L',':','"','~',  0,  '|','Z','X','C','V','B','N','M','<','>','?', 0, '*', 0, ' ',
    0
};

void keyboard_init(void) {
    // Enable scanning if disabled: write 0xF4 to the device via 0x60 after sending 0xD4 to 0x64
    // Keep it minimal and rely on default state.
    (void)outb; (void)inb; // silence unused if optimized out
}

int keyboard_poll_char(void) {
    uint8_t sc = 0;
    if (qhead != qtail) {
        sc = qbuf[qtail]; qtail = (uint8_t)((qtail + 1) % KBD_QSIZE);
    } else {
        if (kbd_irq_mode) return -1; // rely on IRQ only
        if ((inb(KBD_STATUS) & 0x01) == 0) return -1; // no data
        sc = inb(KBD_DATA);
    }

    if (sc == 0xE0) { ext = true; return -1; }
    bool release = (sc & 0x80) != 0; sc &= 0x7F;

    if (ext) {
        // Handle extended keys (E0 prefix)
        switch (sc) {
            case 0x48: ext = false; return KEY_UP;
            case 0x50: ext = false; return KEY_DOWN;
            case 0x4B: ext = false; return KEY_LEFT;
            case 0x4D: ext = false; return KEY_RIGHT;
            case 0x49: ext = false; return KEY_PGUP;
            case 0x51: ext = false; return KEY_PGDN;
            case 0x1D: // Right Ctrl make
                ctrl = !release; ext = false; return -1;
            default:
                ext = false; return -1;
        }
    }

    // Shift press/release
    if (sc == 0x2A || sc == 0x36) { shift = !release; return -1; }
    // Ctrl press/release (left ctrl; right ctrl handled in E0 branch)
    if (sc == 0x1D) { ctrl = !release; return -1; }
    // Caps Lock toggle (make only)
    if (sc == 0x3A && !release) { caps = !caps; return -1; }
    // Ignore other releases
    if (release) return -1;

    if (sc < 128) {
        char ch = 0;
        if (shift) ch = keymap_shift[sc]; else ch = keymap[sc];
        if (!ch) return -1;
        // Apply Caps Lock to letters only if no shift; if both set, invert back
        if (((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z'))) {
            if (caps && !shift) {
                if (ch >= 'a' && ch <= 'z') ch = ch - 'a' + 'A';
                else if (ch >= 'A' && ch <= 'Z') ch = ch - 'A' + 'a';
            }
            // Ctrl-letter → control code (ASCII 1..26)
            if (ctrl) {
                // Normalize to uppercase range for mask
                char up = ch;
                if (up >= 'a' && up <= 'z') up = (char)(up - 'a' + 'A');
                ch = (char)(up & 0x1F);
            }
        }
        return (int)(unsigned char)ch;
    }
    return -1;
}
