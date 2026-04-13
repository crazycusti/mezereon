#include "../mezapi.h"
#include <stdint.h>

#define LUT_SIZE 256
#define LUT_MASK (LUT_SIZE - 1)
#define LUT_SCALE 1024

static const int16_t kSinLut[LUT_SIZE] = {
    0,25,50,75,100,125,150,175,200,224,249,273,297,321,345,369,
    392,415,438,460,483,505,526,548,569,590,610,630,650,669,688,706,
    724,742,759,775,792,807,822,837,851,865,878,891,903,915,926,936,
    946,955,964,972,980,987,993,999,1004,1009,1013,1016,1019,1021,1023,1024,
    1024,1024,1023,1021,1019,1016,1013,1009,1004,999,993,987,980,972,964,955,
    946,936,926,915,903,891,878,865,851,837,822,807,792,775,759,742,
    724,706,688,669,650,630,610,590,569,548,526,505,483,460,438,415,
    392,369,345,321,297,273,249,224,200,175,150,125,100,75,50,25,
    0,-25,-50,-75,-100,-125,-150,-175,-200,-224,-249,-273,-297,-321,-345,-369,
    -392,-415,-438,-460,-483,-505,-526,-548,-569,-590,-610,-630,-650,-669,-688,-706,
    -724,-742,-759,-775,-792,-807,-822,-837,-851,-865,-878,-891,-903,-915,-926,-936,
    -946,-955,-964,-972,-980,-987,-993,-999,-1004,-1009,-1013,-1016,-1019,-1021,-1023,-1024,
    -1024,-1024,-1023,-1021,-1019,-1016,-1013,-1009,-1004,-999,-993,-987,-980,-972,-964,-955,
    -946,-936,-926,-915,-903,-891,-878,-865,-851,-837,-822,-807,-792,-775,-759,-742,
    -724,-706,-688,-669,-650,-630,-610,-590,-569,-548,-526,-505,-483,-460,-438,-415,
    -392,-369,-345,-321,-297,-273,-249,-224,-200,-175,-150,-125,-100,-75,-50,-25
};

static inline int16_t sin_fixed(uint32_t phase) { return kSinLut[phase & LUT_MASK]; }
static inline int16_t cos_fixed(uint32_t phase) { return kSinLut[(phase + 64) & LUT_MASK]; }

typedef struct { int16_t x, y, z; } vec3_t;
typedef struct { int16_t x, y; } vec2_t;

typedef struct {
    const mez_api32_t* api;
    volatile uint8_t* base;
    int pitch, width, height;
} fb_ctx_t;

static void put_pixel(const fb_ctx_t* ctx, int x, int y, uint8_t color) {
    if (x < 0 || y < 0 || x >= ctx->width || y >= ctx->height) return;
    ctx->base[(uint32_t)y * ctx->pitch + (uint32_t)x] = color;
}

static void draw_line(const fb_ctx_t* ctx, int x0, int y0, int x1, int y1, uint8_t color) {
    int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int dy = (y1 > y0) ? (y1 - y0) : (y0 - y1);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = (dx > dy ? dx : -dy) / 2;
    for (;;) {
        put_pixel(ctx, x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = err;
        if (e2 > -dx) { err -= dy; x0 += sx; }
        if (e2 < dy) { err += dx; y0 += sy; }
    }
}

// Simple scanline filler for convex quads
static void fill_quad(const fb_ctx_t* ctx, vec2_t* p, uint8_t color) {
    int min_y = p[0].y, max_y = p[0].y;
    for (int i = 1; i < 4; i++) {
        if (p[i].y < min_y) min_y = p[i].y;
        if (p[i].y > max_y) max_y = p[i].y;
    }
    if (min_y < 0) min_y = 0;
    if (max_y >= ctx->height) max_y = ctx->height - 1;

    for (int y = min_y; y <= max_y; y++) {
        int x_start = 10000, x_end = -10000;
        for (int i = 0; i < 4; i++) {
            vec2_t v0 = p[i], v1 = p[(i + 1) % 4];
            if ((v0.y <= y && v1.y > y) || (v1.y <= y && v0.y > y)) {
                int x = v0.x + (y - v0.y) * (v1.x - v0.x) / (v1.y - v0.y);
                if (x < x_start) x_start = x;
                if (x > x_end) x_end = x;
            }
        }
        if (x_start < 0) x_start = 0;
        if (x_end >= ctx->width) x_end = ctx->width - 1;
        for (int x = x_start; x <= x_end; x++) {
            ctx->base[(uint32_t)y * ctx->pitch + (uint32_t)x] = color;
        }
    }
}

static void rotate(vec3_t* v, uint32_t ax, uint32_t ay, uint32_t az) {
    int x = v->x, y = v->y, z = v->z;
    int tx, ty, tz;
    // Rotate X
    ty = (y * cos_fixed(ax) - z * sin_fixed(ax)) / LUT_SCALE;
    tz = (y * sin_fixed(ax) + z * cos_fixed(ax)) / LUT_SCALE;
    y = ty; z = tz;
    // Rotate Y
    tx = (x * cos_fixed(ay) + z * sin_fixed(ay)) / LUT_SCALE;
    tz = (-x * sin_fixed(ay) + z * cos_fixed(ay)) / LUT_SCALE;
    x = tx; z = tz;
    // Rotate Z
    tx = (x * cos_fixed(az) - y * sin_fixed(az)) / LUT_SCALE;
    ty = (x * sin_fixed(az) + y * cos_fixed(az)) / LUT_SCALE;
    x = tx; y = ty;
    v->x = (int16_t)x; v->y = (int16_t)y; v->z = (int16_t)z;
}

static void draw_cube(const fb_ctx_t* ctx, uint32_t phase) {
    vec3_t vertices[8] = {
        {-64,-64,-64}, {64,-64,-64}, {64,64,-64}, {-64,64,-64},
        {-64,-64,64}, {64,-64,64}, {64,64,64}, {-64,64,64}
    };
    int faces[6][4] = {
        {0,1,2,3}, {5,4,7,6}, {4,0,3,7}, {1,5,6,2}, {4,5,1,0}, {3,2,6,7}
    };
    uint8_t face_colors[6] = {32, 48, 64, 80, 96, 112};
    vec2_t proj[8];

    // Clear screen
    if (ctx->api->video_fb_fill_rect) ctx->api->video_fb_fill_rect(0, 0, ctx->width, ctx->height, 16);
    else {
        for(int y=0; y<ctx->height; y++) 
            for(int x=0; x<ctx->width; x++) ctx->base[y*ctx->pitch+x] = 16;
    }

    for (int i = 0; i < 8; i++) {
        rotate(&vertices[i], phase, phase * 2, phase / 2);
        int z = vertices[i].z + 256;
        proj[i].x = (int16_t)(ctx->width / 2 + (vertices[i].x * 256) / z);
        proj[i].y = (int16_t)(ctx->height / 2 + (vertices[i].y * 256) / z);
    }

    for (int i = 0; i < 6; i++) {
        vec2_t p[4] = {proj[faces[i][0]], proj[faces[i][1]], proj[faces[i][2]], proj[faces[i][3]]};
        // Back-face culling
        long long cross = (long long)(p[1].x - p[0].x) * (p[2].y - p[0].y) - (long long)(p[1].y - p[0].y) * (p[2].x - p[0].x);
        if (cross < 0) {
            // Simple shading based on face index + some phase
            uint8_t color = face_colors[i];
            fill_quad(ctx, p, color);
            for(int j=0; j<4; j++) draw_line(ctx, p[j].x, p[j].y, p[(j+1)%4].x, p[(j+1)%4].y, color + 20);
        }
    }
}

int rotcube_app_main(const mez_api32_t* api) {
    if (!api || api->abi_version < MEZ_ABI32_V1) return -1;
    const mez_fb_info32_t* fb = api->video_fb_get_info();
    if (!fb || fb->bpp != 8 || !fb->framebuffer) return -1;

    fb_ctx_t ctx = {api, (volatile uint8_t*)(uintptr_t)fb->framebuffer, (int)fb->pitch, fb->width, fb->height};
    uint32_t phase = 0;
    for (;;) {
        int key = api->input_poll_key ? api->input_poll_key() : -1;
        if (key == 0x11 || key == 0x1b || key == 'q' || key == 'Q') break;
        draw_cube(&ctx, phase);
        phase = (phase + 2) & LUT_MASK;
        if (api->video_fb_sync) api->video_fb_sync();
        if (api->time_sleep_ms) api->time_sleep_ms(10);
    }
    return 0;
}
