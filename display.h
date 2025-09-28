#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>

// Welche Art von Bild wir darstellen (Text oder Pixel)
typedef enum {
    DISPLAY_MODE_KIND_NONE = 0,
    DISPLAY_MODE_KIND_TEXT = 1,
    DISPLAY_MODE_KIND_FRAMEBUFFER = 2,
} display_mode_kind_t;

// Farb-/Pixel-Formate in einfacher Form (nur das, was wir jetzt brauchen)
typedef enum {
    DISPLAY_PIXEL_FORMAT_NONE = 0,
    DISPLAY_PIXEL_FORMAT_TEXT_16COLOR,
    DISPLAY_PIXEL_FORMAT_TEXT_MONO,
    DISPLAY_PIXEL_FORMAT_PAL_256,
    DISPLAY_PIXEL_FORMAT_RGB_565,
    DISPLAY_PIXEL_FORMAT_RGB_888,
} display_pixel_format_t;

// Beschreibung eines verfügbaren Modus (z.B. 80x25 Text oder 640x480 @ 8bpp)
typedef struct {
    display_mode_kind_t kind;
    display_pixel_format_t pixel_format;
    uint16_t width;   // Bei Text: Spalten, bei Framebuffer: Pixel
    uint16_t height;  // Bei Text: Zeilen, bei Framebuffer: Pixel
    uint8_t  bpp;     // bits per pixel; Text = 0
    uint32_t pitch;   // Bytes pro Zeile; Text = Spaltenzahl
    uint32_t phys_base;              // Physische Basisadresse (falls Framebuffer)
    volatile uint8_t* framebuffer;  // Direkt zugreifbarer Speicher (NULL bei Text)
} display_mode_info_t;

// Feature-Bits für den aktiven Bildschirm
#define DISPLAY_FEATURE_TEXT        (1u << 0)
#define DISPLAY_FEATURE_FRAMEBUFFER (1u << 1)

// Voreinstellung aus der Konfiguration
#define DISPLAY_TARGET_TEXT        0u
#define DISPLAY_TARGET_AUTO        1u
#define DISPLAY_TARGET_FRAMEBUFFER 2u

// Laufzeitstatus: welcher Treiber/Methode ist aktiv, was steht zur Verfügung?
typedef struct {
    const char* active_driver_name;
    display_mode_info_t active_mode;
    uint32_t active_features;

    const char* text_driver_name;
    display_mode_info_t text_mode;

    const char* framebuffer_driver_name;
    display_mode_info_t framebuffer_mode;

    uint32_t requested_target;
} display_state_t;

void display_manager_init(uint32_t requested_target);
void display_manager_set_text_mode(const char* driver_name, uint16_t columns, uint16_t rows, uint8_t color_count);
void display_manager_set_framebuffer_candidate(const char* driver_name, const display_mode_info_t* mode);
void display_manager_activate_text(void);
void display_manager_activate_framebuffer(void);
const display_state_t* display_manager_state(void);
void display_manager_log_state(void);

#endif // DISPLAY_H
