#include "statusbar.h"
#include "console_backend.h"
#include <stddef.h>

typedef struct {
    uint8_t used;
    statusbar_pos_t pos;
    uint8_t priority;
    uint8_t flags;
    char icon;
    char text[STATUSBAR_TEXT_MAX];
    uint8_t text_len;
} statusbar_slot_entry_t;

static statusbar_slot_entry_t g_slots[STATUSBAR_MAX_SLOTS];
static statusbar_slot_t g_legacy_left = STATUSBAR_SLOT_INVALID;
static statusbar_slot_t g_legacy_mid  = STATUSBAR_SLOT_INVALID;
static statusbar_slot_t g_legacy_right = STATUSBAR_SLOT_INVALID;
static int g_backend_ready = 0;
static char g_cached_line[STATUSBAR_COLS];
static int g_cached_valid = 0;

static void statusbar_render(void);
static void statusbar_slot_set_text(statusbar_slot_entry_t* slot, const char* text);

void statusbar_init(void) {
    for (size_t i = 0; i < STATUSBAR_MAX_SLOTS; i++) {
        g_slots[i].used = 0;
        g_slots[i].icon = 0;
        g_slots[i].text[0] = '\0';
        g_slots[i].text_len = 0;
    }
    g_backend_ready = 0;
    g_cached_valid = 0;
    g_legacy_left = statusbar_register(&(statusbar_slot_desc_t){
        .position = STATUSBAR_POS_LEFT,
        .priority = 10,
        .flags = 0,
        .icon = 0,
        .initial_text = ""
    });
    g_legacy_mid = statusbar_register(&(statusbar_slot_desc_t){
        .position = STATUSBAR_POS_CENTER,
        .priority = 5,
        .flags = 0,
        .icon = 0,
        .initial_text = ""
    });
    g_legacy_right = statusbar_register(&(statusbar_slot_desc_t){
        .position = STATUSBAR_POS_RIGHT,
        .priority = 10,
        .flags = 0,
        .icon = 0,
        .initial_text = ""
    });
}

void statusbar_backend_ready(void) {
    g_backend_ready = 1;
    if (g_cached_valid) {
        cback_status_draw_full(g_cached_line, STATUSBAR_COLS);
    }
}

statusbar_slot_t statusbar_register(const statusbar_slot_desc_t* desc) {
    if (!desc) {
        return STATUSBAR_SLOT_INVALID;
    }
    for (statusbar_slot_t i = 0; i < STATUSBAR_MAX_SLOTS; i++) {
        if (!g_slots[i].used) {
            g_slots[i].used = 1;
            g_slots[i].pos = desc->position;
            g_slots[i].priority = desc->priority;
            g_slots[i].flags = desc->flags;
            g_slots[i].icon = desc->icon;
            statusbar_slot_set_text(&g_slots[i], desc->initial_text);
            statusbar_render();
            return i;
        }
    }
    return STATUSBAR_SLOT_INVALID;
}

void statusbar_release(statusbar_slot_t slot) {
    if (slot >= STATUSBAR_MAX_SLOTS) return;
    g_slots[slot].used = 0;
    g_slots[slot].text[0] = '\0';
    g_slots[slot].text_len = 0;
    statusbar_render();
}

static void statusbar_slot_set_text(statusbar_slot_entry_t* slot, const char* text) {
    if (!slot) return;
    if (!text) {
        slot->text[0] = '\0';
        slot->text_len = 0;
        return;
    }
    uint8_t len = 0;
    while (text[len] && len < (STATUSBAR_TEXT_MAX - 1)) {
        slot->text[len] = text[len];
        len++;
    }
    slot->text[len] = '\0';
    slot->text_len = len;
}

void statusbar_set_text(statusbar_slot_t slot, const char* text) {
    if (slot >= STATUSBAR_MAX_SLOTS || !g_slots[slot].used) return;
    statusbar_slot_set_text(&g_slots[slot], text);
    statusbar_render();
}

void statusbar_set_icon(statusbar_slot_t slot, char icon) {
    if (slot >= STATUSBAR_MAX_SLOTS || !g_slots[slot].used) return;
    g_slots[slot].icon = icon;
    statusbar_render();
}

void statusbar_legacy_set_left(const char* text) {
    statusbar_set_text(g_legacy_left, text);
}

void statusbar_legacy_set_mid(const char* text) {
    statusbar_set_text(g_legacy_mid, text);
}

void statusbar_legacy_set_right(const char* text) {
    statusbar_set_text(g_legacy_right, text);
}

static size_t compose_slot_string(const statusbar_slot_entry_t* slot, char* buffer, size_t buf_cap) {
    size_t pos = 0;
    if (slot->icon) {
        if (pos < buf_cap) buffer[pos] = slot->icon;
        pos++;
        if (slot->text_len > 0) {
            if (pos < buf_cap) buffer[pos] = ' ';
            pos++;
        }
    }
    for (uint8_t i = 0; i < slot->text_len; i++) {
        if (pos < buf_cap) buffer[pos] = slot->text[i];
        pos++;
    }
    return pos;
}

static int collect_slots(statusbar_pos_t pos, statusbar_slot_t* out, int max_out) {
    int count = 0;
    for (statusbar_slot_t i = 0; i < STATUSBAR_MAX_SLOTS; i++) {
        if (g_slots[i].used && g_slots[i].pos == pos) {
            if (count < max_out) {
                out[count++] = i;
            }
        }
    }
    // simple insertion sort by descending priority, then slot index
    for (int i = 1; i < count; i++) {
        statusbar_slot_t key = out[i];
        int j = i - 1;
        while (j >= 0) {
            statusbar_slot_entry_t* a = &g_slots[out[j]];
            statusbar_slot_entry_t* b = &g_slots[key];
            if (a->priority >= b->priority) break;
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = key;
    }
    return count;
}

static void statusbar_render(void) {
    char line[STATUSBAR_COLS];
    for (int i = 0; i < STATUSBAR_COLS; i++) {
        line[i] = ' ';
    }

    statusbar_slot_t idx_left[STATUSBAR_MAX_SLOTS];
    statusbar_slot_t idx_center[STATUSBAR_MAX_SLOTS];
    statusbar_slot_t idx_right[STATUSBAR_MAX_SLOTS];

    int left_count = collect_slots(STATUSBAR_POS_LEFT, idx_left, STATUSBAR_MAX_SLOTS);
    int center_count = collect_slots(STATUSBAR_POS_CENTER, idx_center, STATUSBAR_MAX_SLOTS);
    int right_count = collect_slots(STATUSBAR_POS_RIGHT, idx_right, STATUSBAR_MAX_SLOTS);

    int left_end = 0;
    for (int i = 0; i < left_count && left_end < STATUSBAR_COLS; i++) {
        statusbar_slot_entry_t* slot = &g_slots[idx_left[i]];
        char tmp[STATUSBAR_TEXT_MAX + 4];
        size_t slot_len = compose_slot_string(slot, tmp, sizeof(tmp));
        if (slot_len == 0) continue;
        if (left_end > 0 && left_end < STATUSBAR_COLS) {
            line[left_end++] = ' ';
        }
        size_t avail = (size_t)(STATUSBAR_COLS - left_end);
        if (slot_len <= avail) {
            for (size_t k = 0; k < slot_len; k++) {
                line[left_end + k] = tmp[k];
            }
            left_end += (int)slot_len;
        } else {
            if ((slot->flags & STATUSBAR_FLAG_ICON_ONLY_ON_TRUNCATE) && slot->icon && avail >= 1) {
                line[left_end++] = slot->icon;
            } else {
                for (size_t k = 0; k < avail; k++) {
                    line[left_end + k] = tmp[k];
                }
                left_end += (int)avail;
            }
            break;
        }
    }

    int right_start = STATUSBAR_COLS;
    int right_written = 0;
    for (int i = 0; i < right_count && right_start > left_end; i++) {
        statusbar_slot_entry_t* slot = &g_slots[idx_right[i]];
        char tmp[STATUSBAR_TEXT_MAX + 4];
        size_t slot_len = compose_slot_string(slot, tmp, sizeof(tmp));
        if (slot_len == 0) continue;
        size_t avail = (size_t)(right_start - left_end);
        size_t needed = slot_len;
        if (right_written > 0) {
            needed += 1; // leading space
        }
        if (needed > avail) {
            if ((slot->flags & STATUSBAR_FLAG_ICON_ONLY_ON_TRUNCATE) && slot->icon && avail >= 1) {
                right_start--;
                line[right_start] = slot->icon;
                break;
            } else {
                size_t copy = (needed > avail) ? avail : needed;
                if (copy > slot_len) {
                    // include space
                    line[right_start - 1] = ' ';
                    right_start--;
                    copy--;
                }
                size_t src_start = slot_len > copy ? slot_len - copy : 0;
                for (size_t k = 0; k < copy; k++) {
                    right_start--;
                    line[right_start] = tmp[src_start + (copy - 1 - k)];
                }
                break;
            }
        } else {
            if (right_written > 0) {
                right_start--;
                line[right_start] = ' ';
            }
            for (size_t k = 0; k < slot_len; k++) {
                right_start--;
                line[right_start] = tmp[slot_len - 1 - k];
            }
            right_written++;
        }
    }

    if (center_count > 0 && right_start > left_end) {
        int write_pos = left_end;
        for (int i = 0; i < center_count && write_pos < right_start; i++) {
            statusbar_slot_entry_t* slot = &g_slots[idx_center[i]];
            char tmp[STATUSBAR_TEXT_MAX + 4];
            size_t slot_len = compose_slot_string(slot, tmp, sizeof(tmp));
            if (slot_len == 0) continue;
            if (write_pos > left_end && write_pos < right_start) {
                line[write_pos++] = ' ';
            }
            size_t avail = (size_t)(right_start - write_pos);
            if (avail == 0) break;
            if (slot_len > avail) {
                if ((slot->flags & STATUSBAR_FLAG_ICON_ONLY_ON_TRUNCATE) && slot->icon && avail >= 1) {
                    line[write_pos++] = slot->icon;
                } else {
                    for (size_t k = 0; k < avail; k++) {
                        line[write_pos + (int)k] = tmp[k];
                    }
                    write_pos += (int)avail;
                }
                break;
            }
            for (size_t k = 0; k < slot_len; k++) {
                line[write_pos++] = tmp[k];
            }
        }
    }

    for (int i = 0; i < STATUSBAR_COLS; i++) {
        g_cached_line[i] = line[i];
    }
    g_cached_valid = 1;
    if (g_backend_ready) {
        cback_status_draw_full(line, STATUSBAR_COLS);
    }
}
