#include "../mezapi.h"
#include <stdint.h>

#define LUT_SIZE 256
#define LUT_MASK (LUT_SIZE - 1)
#define LUT_SCALE 1024

static inline uint8_t clamp_color(int value) {
    if (value < 0) return 0;
    if (value > 255) return 255;
    return (uint8_t)value;
}

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

static inline int16_t sin_fixed(uint32_t phase) {
    return kSinLut[phase & LUT_MASK];
}

static inline int16_t cos_fixed(uint32_t phase) {
    return kSinLut[(phase + (LUT_SIZE / 4)) & LUT_MASK];
}

typedef struct {
    const mez_api32_t* api;
    const mez_fb_info32_t* fb;
    volatile uint8_t* base;
    int pitch;
    int width;
    int height;
    int has_accel;
} fb_ctx_t;

static void fill_rect_accel(const fb_ctx_t* ctx, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t color) {
    if (!w || !h) return;
    if (x >= ctx->width || y >= ctx->height) return;
    if ((uint32_t)x + w > (uint32_t)ctx->width) w = (uint16_t)(ctx->width - x);
    if ((uint32_t)y + h > (uint32_t)ctx->height) h = (uint16_t)(ctx->height - y);

    if (ctx->has_accel && ctx->api->video_fb_fill_rect) {
        ctx->api->video_fb_fill_rect(x, y, w, h, color);
        return;
    }

    volatile uint8_t* base = ctx->base + (uint32_t)y * ctx->pitch + x;
    for (uint16_t yy = 0; yy < h; yy++) {
        volatile uint8_t* line = base + (uint32_t)yy * ctx->pitch;
        for (uint16_t xx = 0; xx < w; xx++) {
            line[xx] = color;
        }
    }
}

static void put_pixel(const fb_ctx_t* ctx, int x, int y, uint8_t color) {
    if (x < 0 || y < 0 || x >= ctx->width || y >= ctx->height) return;
    ctx->base[(uint32_t)y * ctx->pitch + (uint32_t)x] = color;
}

static void draw_line(const fb_ctx_t* ctx, int x0, int y0, int x1, int y1, uint8_t color) {
    int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int sx = (x0 < x1) ? 1 : -1;
    int dy = (y1 > y0) ? (y1 - y0) : (y0 - y1);
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

static void draw_cube(const fb_ctx_t* ctx, uint32_t phase) {
    int base_extent = ctx->height;
    if (ctx->width < base_extent) base_extent = ctx->width;
    const int base_size = base_extent / 3;
    const int depth = base_size / 2;
    const int center_x = ctx->width / 2;
    const int center_y = ctx->height / 2 + base_size / 8;

    int16_t cosv = cos_fixed(phase);
    int16_t sinv = sin_fixed(phase);

    int abs_sin = sinv;
    if (abs_sin < 0) abs_sin = -abs_sin;
    int abs_cos = cosv;
    if (abs_cos < 0) abs_cos = -abs_cos;

    int front_w = base_size + (cosv * depth) / LUT_SCALE;
    if (front_w < base_size / 4) front_w = base_size / 4;
    if (front_w > base_size + depth) front_w = base_size + depth;

    int cube_h = base_size;
    int side_w = (abs_sin * depth) / LUT_SCALE;
    if (side_w > 0) side_w += 4;
    if (side_w > depth) side_w = depth;

    int side_dir = 0;
    if (side_w > 0) {
        side_dir = 1;
        if (sinv <= 0) side_dir = -1;
    }

    int front_x = center_x - front_w / 2;
    if (side_dir > 0) {
        front_x -= side_w / 2;
    } else if (side_dir < 0) {
        front_x += side_w / 2;
    }
    int front_y = center_y - cube_h / 2;

    fill_rect_accel(ctx, 0, 0, ctx->width, ctx->height, 16);

    int light_front = (cosv * 48) / LUT_SCALE;
    int light_side = (sinv * 40) / LUT_SCALE;
    int light_top = ((abs_cos - abs_sin) * 32) / LUT_SCALE;

    uint8_t front_color = clamp_color(160 + light_front);
    uint8_t side_color = clamp_color(130 + light_side);
    uint8_t top_color = clamp_color(190 + light_top);
    uint8_t edge_color = clamp_color(25 + ((abs_cos + abs_sin) * 10) / LUT_SCALE);

    fill_rect_accel(ctx, (uint16_t)front_x, (uint16_t)front_y, (uint16_t)front_w, (uint16_t)cube_h, front_color);

    if (side_dir > 0) {
        int side_x = front_x + front_w;
        fill_rect_accel(ctx, (uint16_t)side_x, (uint16_t)front_y, (uint16_t)side_w, (uint16_t)cube_h, side_color);
    } else if (side_dir < 0) {
        int side_x = front_x - side_w;
        fill_rect_accel(ctx, (uint16_t)side_x, (uint16_t)front_y, (uint16_t)side_w, (uint16_t)cube_h, side_color);
    }

    int top_h = depth / 3 + (abs_cos * depth) / (2 * LUT_SCALE);
    if (top_h < 4) top_h = 4;
    if (top_h > depth) top_h = depth;
    int top_x = front_x;
    if (side_dir < 0) top_x -= side_w;
    int top_w = front_w;
    if (side_dir != 0) top_w += side_w;
    fill_rect_accel(ctx, (uint16_t)top_x, (uint16_t)(front_y - top_h), (uint16_t)top_w, (uint16_t)top_h, top_color);

    int fx0 = front_x;
    int fx1 = front_x + front_w;
    int fy0 = front_y;
    int fy1 = front_y + cube_h;
    int top_y = front_y - top_h;
    int top_left_x = front_x;
    if (side_dir < 0) top_left_x -= side_w;
    int top_right_x = front_x + front_w;
    if (side_dir > 0) top_right_x += side_w;

    draw_line(ctx, fx0, fy0, fx1, fy0, edge_color);
    draw_line(ctx, fx1, fy0, fx1, fy1, edge_color);
    draw_line(ctx, fx1, fy1, fx0, fy1, edge_color);
    draw_line(ctx, fx0, fy1, fx0, fy0, edge_color);

    draw_line(ctx, fx0, fy0, top_left_x, top_y, edge_color);
    draw_line(ctx, fx1, fy0, top_right_x, top_y, edge_color);
    draw_line(ctx, top_left_x, top_y, top_right_x, top_y, edge_color);

    if (side_dir > 0 && side_w > 0) {
        int outer_x = fx1 + side_w;
        int bottom_outer_y = fy1 - top_h;
        draw_line(ctx, fx1, fy1, outer_x, bottom_outer_y, edge_color);
        draw_line(ctx, outer_x, top_y, outer_x, bottom_outer_y, edge_color);
    } else if (side_dir < 0 && side_w > 0) {
        int outer_x = fx0 - side_w;
        int bottom_outer_y = fy1 - top_h;
        draw_line(ctx, fx0, fy1, outer_x, bottom_outer_y, edge_color);
        draw_line(ctx, outer_x, top_y, outer_x, bottom_outer_y, edge_color);
    } else {
        draw_line(ctx, fx0, fy0, fx0, top_y, edge_color);
        draw_line(ctx, fx1, fy0, fx1, top_y, edge_color);
    }
}

int rotcube_app_main(const mez_api32_t* api) {
    if (!api || api->abi_version < MEZ_ABI32_V1) return -1;
    if (!(api->capabilities & MEZ_CAP_VIDEO_FB) || !api->video_fb_get_info) {
        if (api->console_writeln) api->console_writeln("rotcube: framebuffer not available");
        return -1;
    }
    const mez_fb_info32_t* fb = api->video_fb_get_info();
    if (!fb || fb->bpp != 8 || !fb->framebuffer) {
        if (api->console_writeln) api->console_writeln("rotcube: need 8bpp framebuffer");
        return -1;
    }

    fb_ctx_t ctx;
    ctx.api = api;
    ctx.fb = fb;
    ctx.pitch = (int)fb->pitch;
    ctx.width = fb->width;
    ctx.height = fb->height;
    ctx.base = (volatile uint8_t*)(uintptr_t)fb->framebuffer;
    ctx.has_accel = ((api->capabilities & MEZ_CAP_VIDEO_FB_ACCEL) && api->video_fb_fill_rect);

    if (api->console_writeln) {
        api->console_writeln("rotcube: rotating cube demo (Ctrl+Q or Esc exits)");
        if (!ctx.has_accel) api->console_writeln("rotcube: warning — running without acceleration fallback");
    }

    uint32_t phase = 0;
    const uint32_t phase_step = 3;
    for (;;) {
        int key = api->input_poll_key ? api->input_poll_key() : -1;
        if (key == 0x11 || key == 0x1b || key == 'q' || key == 'Q') break;
        draw_cube(&ctx, phase);
        phase = (phase + phase_step) & LUT_MASK;
        if (api->time_sleep_ms) api->time_sleep_ms(16);
    }

    fill_rect_accel(&ctx, 0, 0, (uint16_t)ctx.width, (uint16_t)ctx.height, 0);
    return 0;
}
