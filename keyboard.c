#include "keyboard.h"
#include "config.h"
#include "arch/x86/io.h"
#include "console.h"
#include "debug_serial.h"
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
static uint8_t kbd_cmd_byte = 0;
static int kbd_aux_enabled = 0;

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

static int kbd_aux_disabled(void) {
    return (!kbd_aux_enabled) || ((kbd_cmd_byte & 0x20u) != 0);
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

static int kbd_wait_input_clear(uint32_t loops) {
    while (loops--) {
        if ((inb(KBD_STATUS) & 0x02u) == 0) {
            return 1;
        }
        io_delay();
    }
    return 0;
}

static int kbd_wait_output_full(uint32_t loops) {
    while (loops--) {
        if (inb(KBD_STATUS) & 0x01u) {
            return 1;
        }
        io_delay();
    }
    return 0;
}

static void kbd_drain_output(void) {
    for (int guard = 0; guard < 64; guard++) {
        if ((inb(KBD_STATUS) & 0x01u) == 0) {
            break;
        }
        (void)inb(KBD_DATA);
    }
}

static int kbd_write_cmd(uint8_t cmd) {
    if (!kbd_wait_input_clear(65535)) {
        return 0;
    }
    outb(KBD_STATUS, cmd);
    return 1;
}

static int kbd_write_data(uint8_t val) {
    if (!kbd_wait_input_clear(65535)) {
        return 0;
    }
    outb(KBD_DATA, val);
    return 1;
}

static int kbd_read_data(uint8_t* out) {
    if (!kbd_wait_output_full(65535)) {
        return 0;
    }
    *out = inb(KBD_DATA);
    return 1;
}

static int kbd_send_and_ack(uint8_t cmd) {
    for (int attempt = 0; attempt < 2; attempt++) {
        if (!kbd_write_data(cmd)) {
            return 0;
        }
        for (int i = 0; i < 32; i++) {
            uint8_t resp = 0;
            if (!kbd_read_data(&resp)) {
                continue;
            }
            if (resp == 0xFA) {
                return 1; // ACK
            }
            if (resp == 0xFE) {
                break; // resend
            }
            if (resp == 0xAA) {
                continue; // BAT
            }
        }
    }
    return 0;
}

static void kbd_log_cmd_byte(const char* label, uint8_t val) {
    console_write(label);
    console_write_hex16((uint16_t)val);
    console_write("\n");
}

#if CONFIG_KBD_PROBE || CONFIG_KBD_DEBUG_DUMP
static int g_kbd_probe_active = 0;
#endif
static void kbd_probe_run(void) {
#if CONFIG_KBD_PROBE
    console_writeln("kbd: probe start (press keys now)");
    g_kbd_probe_active = 1;
    int lines = 0;
    for (int i = 0; i < 400; i++) {
        uint8_t st = inb(KBD_STATUS);
        if (st & 0x01u) {
            uint8_t sc = inb(KBD_DATA);
            kbd_log(sc);
            console_write("kbd data=0x");
            console_write_hex16((uint16_t)sc);
            console_write("\n");
            if (++lines >= 20) break;
        }
        for (int d = 0; d < 4000; d++) io_delay();
    }
    g_kbd_probe_active = 0;
    console_writeln("kbd: probe end");
#endif
}

static void kbd_debug_dump(uint8_t sc) {
#if CONFIG_KBD_DEBUG_DUMP
    if (!g_kbd_probe_active) {
        return;
    }
    console_write("kbd sc=");
    console_write_hex16((uint16_t)sc);
    console_write("\n");
#else
    (void)sc;
#endif
}

#if CONFIG_KBD_FORCE_SET1
static int kbd_set_scancode_set(uint8_t set) {
    if (!kbd_send_and_ack(0xF0)) {
        return 0;
    }
    if (!kbd_send_and_ack(set)) {
        return 0;
    }
    return 1;
}
#endif

static int kbd_read_cmd_byte(uint8_t* out) {
    if (!kbd_write_cmd(0x20)) {
        return 0;
    }
    return kbd_read_data(out);
}

static int kbd_write_cmd_byte(uint8_t val) {
    if (!kbd_write_cmd(0x60)) {
        return 0;
    }
    return kbd_write_data(val);
}

void keyboard_init(void) {
    shift = false;
    caps = false;
    ctrl = false;
    ext = false;
    qhead = qtail = 0;
    dbg_head = dbg_count = 0;
    kbd_cmd_byte = 0;
    kbd_aux_enabled = 0;

    kbd_drain_output();
    uint8_t cmd = 0;
    if (kbd_read_cmd_byte(&cmd)) {
        kbd_cmd_byte = cmd;
        kbd_log_cmd_byte("kbd: BIOS cmd=", cmd);
        
        // Force sync: write the command byte back (even if identical)
        // This 'kicks' the controller into the active state for the new OS.
        if (kbd_write_cmd_byte(cmd)) {
            console_writeln("kbd: cmd sync ok");
        } else {
            console_writeln("kbd: cmd sync failed");
        }
    }

    // Ensure scanning is enabled (F4)
    (void)kbd_send_and_ack(0xF4); 

    keyboard_status_refresh();
    kbd_probe_run();
}

int keyboard_poll_char(void) {
    uint8_t sc = 0;
    if (qhead != qtail) {
        sc = qbuf[qtail];
        qtail = (uint8_t)((qtail + 1) % KBD_QSIZE);
        kbd_debug_dump(sc);
    } else {
        if (kbd_irq_mode) {
            // Even in IRQ mode, we can poll serial as a secondary input
            int ser = debug_serial_plugin_getc();
            if (ser != -1) {
                if (ser == '\r') return '\n';
                if (ser == 127) return '\b';
                return ser;
            }
            return -1;
        }
        uint8_t status = inb(KBD_STATUS);
        if ((status & 0x01u) == 0) {
            // No PS/2 data, check serial
            int ser = debug_serial_plugin_getc();
            if (ser != -1) {
                if (ser == '\r') return '\n';
                if (ser == 127) return '\b';
                return ser;
            }
            return -1;
        }
        sc = inb(KBD_DATA);
        kbd_debug_dump(sc);
        if (status & 0x20u) {
#if CONFIG_KBD_ACCEPT_AUX
            if (!kbd_aux_disabled()) {
                kbd_log(sc);
                return -1;
            }
#else
            kbd_log(sc);
            return -1;
#endif
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
    static char buf[48];
    const char HEX[] = "0123456789ABCDEF";
    uint32_t irqs = kbd_irq_count_get();
    uint8_t st = inb(KBD_STATUS);
    
    buf[0] = 'k'; buf[1] = 'b'; buf[2] = 'd'; buf[3] = ':'; buf[4] = ' ';
    buf[5] = 's'; buf[6] = 't'; buf[7] = '='; buf[8] = HEX[(st >> 4) & 0x0F]; buf[9] = HEX[st & 0x0F];
    buf[10] = ' '; buf[11] = 's'; buf[12] = 'c'; buf[13] = '='; 
    buf[14] = HEX[(dbg_last_sc >> 4) & 0x0F];
    buf[15] = HEX[dbg_last_sc & 0x0F];
    
    buf[16] = ' '; buf[17] = 'i'; buf[18] = 'r'; buf[19] = 'q'; buf[20] = ':';
    
    int pos = 21;
    if (irqs == 0) {
        buf[pos++] = '0';
    } else {
        char tmp[10];
        int i = 0;
        uint32_t v = irqs;
        while (v > 0 && i < 10) {
            tmp[i++] = (char)('0' + (v % 10));
            v /= 10;
        }
        while (i > 0) {
            buf[pos++] = tmp[--i];
        }
    }
    buf[pos] = 0;
    console_status_set_mid(buf);
}

int keyboard_wait_key(void) {
    int ch = -1;
    while (ch < 0) {
        ch = keyboard_poll_char();
    }
    return ch;
}
