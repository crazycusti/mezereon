#include "keyboard.h"
#include "arch/x86/io.h"
#include "console.h"
#include <stdint.h>

#define KBD_STATUS 0x64
#define KBD_DATA   0x60

static bool shift = false;
static bool caps = false;
static bool ctrl = false;
static bool ext  = false;

#define KBD_QSIZE 32
static volatile uint8_t qbuf[KBD_QSIZE];
static volatile uint8_t qhead = 0, qtail = 0;
static volatile int kbd_irq_mode = 0;

#define KBD_DBG_SIZE 64
static volatile uint8_t dbg_buf[KBD_DBG_SIZE];
static volatile uint8_t dbg_head = 0;
static volatile uint8_t dbg_count = 0;
static volatile uint8_t dbg_last_sc = 0;

static void kbd_update_status(uint8_t sc) {
    dbg_last_sc = sc;
}

static void kbd_log(uint8_t sc) {
    kbd_update_status(sc);
    dbg_buf[dbg_head] = sc;
    dbg_head = (uint8_t)((dbg_head + 1u) % KBD_DBG_SIZE);
    if (dbg_count < KBD_DBG_SIZE) dbg_count++;
}

void keyboard_isr_byte(uint8_t sc) {
    kbd_log(sc);
    uint8_t n = (uint8_t)((qhead + 1) % KBD_QSIZE);
    if (n != qtail) {
        qbuf[qhead] = sc;
        qhead = n;
    }
}

void keyboard_set_irq_mode(int enabled) {
    kbd_irq_mode = enabled ? 1 : 0;
}

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

static void keyboard_status_refresh(void);

void keyboard_init(void) {
    shift = false;
    caps = false;
    ctrl = false;
    ext = false;
    qhead = qtail = 0;
    dbg_head = dbg_count = 0;

    // Drain any pending bytes from the controller buffer.
    for (int guard = 0; guard < 64; guard++) {
        uint8_t status = inb(KBD_STATUS);
        if ((status & 0x01u) == 0) {
            break;
        }
        (void)inb(KBD_DATA);
    }
    keyboard_status_refresh();
}

int keyboard_poll_char(void) {
    uint8_t sc = 0;
    if (qhead != qtail) {
        sc = qbuf[qtail];
        qtail = (uint8_t)((qtail + 1) % KBD_QSIZE);
    } else {
        if (kbd_irq_mode) return -1;
        uint8_t status = inb(KBD_STATUS);
        if ((status & 0x01u) == 0) return -1;
        sc = inb(KBD_DATA);
        if (status & 0x20u) {
            kbd_log(sc);
            return -1;
        }
        kbd_log(sc);
    }

    keyboard_status_refresh();

    if (sc == 0xE0) {
        ext = true;
        return -1;
    }

    bool release = (sc & 0x80u) != 0;
    sc &= 0x7Fu;

    if (ext) {
        switch (sc) {
            case 0x48: ext = false; return KEY_UP;
            case 0x50: ext = false; return KEY_DOWN;
            case 0x4B: ext = false; return KEY_LEFT;
            case 0x4D: ext = false; return KEY_RIGHT;
            case 0x49: ext = false; return KEY_PGUP;
            case 0x51: ext = false; return KEY_PGDN;
            case 0x1D: ctrl = !release; ext = false; return -1;
            default: ext = false; return -1;
        }
    }

    if (sc == 0x2A || sc == 0x36) { shift = !release; return -1; }
    if (sc == 0x1D) { ctrl = !release; return -1; }
    if (sc == 0x3A && !release) { caps = !caps; return -1; }
    if (release) return -1;

    if (sc < 128) {
        char ch = shift ? keymap_shift[sc] : keymap[sc];
        if (!ch) return -1;
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')) {
            if (caps && !shift) {
                if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
                else ch = (char)(ch - 'A' + 'a');
            }
            if (ctrl) {
                char up = ch;
                if (up >= 'a' && up <= 'z') up = (char)(up - 'a' + 'A');
                ch = (char)(up & 0x1F);
            }
        }
        return (int)(unsigned char)ch;
    }
    return -1;
}

void keyboard_debug_dump(void) {
    console_write("kbd sc:");
    uint8_t count = dbg_count;
    uint8_t head = dbg_head;
    static const char HEX[] = "0123456789ABCDEF";
    for (uint8_t i = 0; i < count; i++) {
        uint8_t idx = (uint8_t)((head + KBD_DBG_SIZE - count + i) % KBD_DBG_SIZE);
        uint8_t sc = dbg_buf[idx];
        char buf[5];
        buf[0] = ' ';
        buf[1] = '0';
        buf[2] = 'x';
        buf[3] = HEX[(sc >> 4) & 0x0F];
        buf[4] = HEX[sc & 0x0F];
        console_write(buf);
    }
    if (count == 0) {
        console_write(" (empty)");
    }
    console_write("\n");
}

static void keyboard_status_refresh(void) {
    static char buf[32];
    const char HEX[] = "0123456789ABCDEF";
    buf[0] = 'k'; buf[1] = 'b'; buf[2] = 'd'; buf[3] = ':'; buf[4] = ' ';
    buf[5] = '0'; buf[6] = 'x';
    buf[7] = HEX[(dbg_last_sc >> 4) & 0x0F];
    buf[8] = HEX[dbg_last_sc & 0x0F];
    buf[9] = 0;
    console_status_set_mid(buf);
}
