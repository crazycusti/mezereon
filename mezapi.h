#pragma once
#include <stdint.h>

// MezAPI v1 for 32-bit x86 (cdecl, little-endian)
// Stable binary layout: new functions are appended; size guards compatibility.

#define MEZ_ARCH_X86_32   0x00000001u
#define MEZ_ABI32_V1      0x00010000u

#define MEZ_CAP_VIDEO_FB        (1u << 0)
#define MEZ_CAP_VIDEO_FB_ACCEL  (1u << 1)
#define MEZ_CAP_SOUND_SB16      (1u << 2)
#define MEZ_CAP_VIDEO_GPU_INFO  (1u << 3)

#define MEZ_SOUND_BACKEND_NONE    0u
#define MEZ_SOUND_BACKEND_PCSPK   (1u << 0)
#define MEZ_SOUND_BACKEND_SB16    (1u << 1)

typedef struct {
    uint16_t width;
    uint16_t height;
    uint32_t pitch;
    uint8_t  bpp;
    uint8_t  reserved0;
    uint8_t  reserved1;
    uint8_t  reserved2;
    const void* framebuffer;
} mez_fb_info32_t;

typedef struct {
    uint32_t backends;      // Bitmask of MEZ_SOUND_BACKEND_* flags
    uint16_t sb16_base_port;
    uint8_t  sb16_irq;
    uint8_t  sb16_dma8;
    uint8_t  sb16_dma16;
    uint8_t  sb16_version_major;
    uint8_t  sb16_version_minor;
    uint8_t  reserved0;
    uint8_t  reserved1;
} mez_sound_info32_t;

// GPU feature levels roughly map to the rendering pipelines we can expose.
typedef enum {
    MEZ_GPU_FEATURELEVEL_TEXTMODE            = 0u,   // reine Textausgabe, keine Bank-/Linear-FB-Unterstützung
    MEZ_GPU_FEATURELEVEL_BANKED_FB           = 100u, // 64-KiB-Bankfenster (ET4000, AVGA2)
    MEZ_GPU_FEATURELEVEL_BANKED_FB_ACCEL     = 200u, // Banked Framebuffer mit rudimentärer Beschleunigung (ET4000AX)
    MEZ_GPU_FEATURELEVEL_LINEAR_FB           = 300u, // Lineares Framebuffer ohne dedizierte 2D-Engine
    MEZ_GPU_FEATURELEVEL_LINEAR_FB_ACCEL     = 400u, // Lineares Framebuffer mit 2D-Beschleunigung (z. B. Cirrus BitBLT)
} mez_gpu_feature_level_t;

typedef enum {
    MEZ_GPU_ADAPTER_NONE            = 0u,
    MEZ_GPU_ADAPTER_TEXTMODE        = 1u,
    MEZ_GPU_ADAPTER_CIRRUS          = 2u,
    MEZ_GPU_ADAPTER_TSENG_ET4000    = 3u,
    MEZ_GPU_ADAPTER_TSENG_ET4000AX  = 4u,
    MEZ_GPU_ADAPTER_ACUMOS_AVGA2    = 5u,
} mez_gpu_adapter_t;

#define MEZ_GPU_CAP_LINEAR_FB   (1u << 0)
#define MEZ_GPU_CAP_ACCEL_2D    (1u << 1)
#define MEZ_GPU_CAP_HW_CURSOR   (1u << 2)
#define MEZ_GPU_CAP_VBE_BIOS    (1u << 3)
#define MEZ_GPU_CAP_BANKED_FB   (1u << 4)

typedef struct {
    uint32_t feature_level;    // mez_gpu_feature_level_t
    uint32_t adapter_type;     // mez_gpu_adapter_t
    uint32_t capabilities;     // MEZ_GPU_CAP_* Bits
    uint32_t vram_bytes;       // erkannter Videospeicher (falls bekannt)
    char     name[32];         // Adaptername (0-terminiert)
} mez_gpu_info32_t;

typedef enum {
    MEZ_STATUS_POS_LEFT = 0,
    MEZ_STATUS_POS_CENTER = 1,
    MEZ_STATUS_POS_RIGHT = 2
} mez_status_pos_t;

typedef uint8_t mez_status_slot_t;
#define MEZ_STATUS_SLOT_INVALID ((mez_status_slot_t)0xFF)
#define MEZ_STATUS_FLAG_ICON_ONLY_ON_TRUNCATE 0x01u

typedef struct mez_api32 {
    // Header
    uint32_t abi_version;   // e.g., MEZ_ABI32_V1
    uint32_t size;          // sizeof(struct mez_api32) provided by kernel
    uint32_t arch;          // MEZ_ARCH_* constant

    // Console/Text output
    void     (*console_write)(const char* s);
    void     (*console_writeln)(const char* s);
    void     (*console_clear)(void);

    // Input (non-blocking; returns ASCII or special codes >255, -1 if none)
    int      (*input_poll_key)(void);

    // Time
    uint32_t (*time_ticks_get)(void);
    uint32_t (*time_timer_hz)(void);
    void     (*time_sleep_ms)(uint32_t ms);

    // Sound (PC speaker or backend)
    void     (*sound_beep)(uint32_t hz, uint32_t ms);
    void     (*sound_tone_on)(uint32_t hz);
    void     (*sound_tone_off)(void);

    // Text-mode drawing (hardware-agnostic API; backend may be VGA text)
    void     (*text_put)(int x, int y, char ch, uint8_t attr);
    void     (*text_fill_line)(int y, char ch, uint8_t attr);
    void     (*status_left)(const char* s);
    void     (*status_right)(const char* s, int len);

    // Advanced status bar management (optional)
    mez_status_slot_t (*status_register)(mez_status_pos_t pos, uint8_t priority, uint8_t flags, char icon, const char* initial_text);
    void     (*status_update)(mez_status_slot_t slot, const char* text);
    void     (*status_release)(mez_status_slot_t slot);

    uint32_t capabilities;
    const mez_fb_info32_t* (*video_fb_get_info)(void);
    void     (*video_fb_fill_rect)(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t color);

    // Sound stack metadata (legacy + PCM pipelines)
    const mez_sound_info32_t* (*sound_get_info)(void);

    // GPU metadata (detected adapters + featurelevel)
    const mez_gpu_info32_t* (*video_gpu_get_info)(void);
} mez_api32_t;

// Provider from kernel
const mez_api32_t* mez_api_get(void);
