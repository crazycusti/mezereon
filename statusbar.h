#ifndef STATUSBAR_H
#define STATUSBAR_H

#include <stdint.h>

#define STATUSBAR_MAX_SLOTS 16
#define STATUSBAR_TEXT_MAX  64
#define STATUSBAR_COLS      80

typedef enum {
    STATUSBAR_POS_LEFT = 0,
    STATUSBAR_POS_CENTER = 1,
    STATUSBAR_POS_RIGHT = 2
} statusbar_pos_t;

typedef uint8_t statusbar_slot_t;
#define STATUSBAR_SLOT_INVALID ((statusbar_slot_t)0xFF)

#define STATUSBAR_FLAG_ICON_ONLY_ON_TRUNCATE 0x01

typedef struct {
    statusbar_pos_t position;
    uint8_t priority;
    uint8_t flags;
    char icon;
    const char* initial_text;
} statusbar_slot_desc_t;

void statusbar_init(void);
void statusbar_backend_ready(void);
statusbar_slot_t statusbar_register(const statusbar_slot_desc_t* desc);
void statusbar_release(statusbar_slot_t slot);
void statusbar_set_text(statusbar_slot_t slot, const char* text);
void statusbar_set_icon(statusbar_slot_t slot, char icon);

/* Legacy helpers for existing kernel modules */
void statusbar_legacy_set_left(const char* text);
void statusbar_legacy_set_mid(const char* text);
void statusbar_legacy_set_right(const char* text);

#endif /* STATUSBAR_H */
