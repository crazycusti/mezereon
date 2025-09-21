#include <stdint.h>
#include <stdbool.h>
#include "config.h"
#include "keyboard.h"
#include "platform.h"
#include "console.h"
#include "drivers/pcspeaker.h"

#define VGA_MEM ((volatile uint16_t*)0xB8000)
#define W CONFIG_VGA_WIDTH
#define H CONFIG_VGA_HEIGHT

// Colors (attribute byte: high 4 bits = BG, low 4 bits = FG)
#define COL(fg,bg) (uint8_t)(((bg)<<4) | ((fg)&0x0F))

static inline void vga_put(int x, int y, char ch, uint8_t attr){
    if (x<0||x>=W||y<0||y>=H) return;
    VGA_MEM[y*W + x] = (uint16_t)((attr<<8) | (uint8_t)ch);
}

static inline void vga_fill_line(int y, char ch, uint8_t attr){
    if (y<0||y>=H) return;
    for (int x=0;x<W;x++) vga_put(x,y,ch,attr);
}

static inline void vga_box_border(int x0,int y0,int x1,int y1,uint8_t attr){
    for (int x=x0;x<=x1;x++){ vga_put(x,y0,'\xC4',attr); vga_put(x,y1,'\xC4',attr); }
    for (int y=y0;y<=y1;y++){ vga_put(x0,y,'\xB3',attr); vga_put(x1,y,'\xB3',attr); }
    vga_put(x0,y0,'\xDA',attr); vga_put(x1,y0,'\xBF',attr);
    vga_put(x0,y1,'\xC0',attr); vga_put(x1,y1,'\xD9',attr);
}

// Simple LCG RNG
static uint32_t rng_state;
static inline uint32_t rng_next(void){ rng_state = rng_state*1664525u + 1013904223u; return rng_state; }

void snake_run(void){
    // Playfield inside a border; keep status row (y=0) for status bar
    const int x0 = 0, x1 = W-1;      // outer border left/right
    const int y0 = 1, y1 = H-1;      // top border below status, bottom at last row
    const int px0 = x0+1, px1 = x1-1; // playable area
    const int py0 = y0+1, py1 = y1-1;
    const int PW = px1 - px0 + 1;    // width of play
    const int PH = py1 - py0 + 1;    // height of play

    // Clear the area and draw frame
    for (int y=y0; y<=y1; y++) vga_fill_line(y,' ',COL(7,0));
    vga_box_border(x0,y0,x1,y1,COL(7,0));

    // Initialize RNG
    rng_state = (platform_ticks_get() | 1u);

    // Snake state
    enum { MAX_SEG = 1024 };
    static uint8_t occ[H][W];
    for (int y=0;y<H;y++) for(int x=0;x<W;x++) occ[y][x]=0;
    int sx[MAX_SEG], sy[MAX_SEG];
    int head = -1; int len = 4; // initial length
    int dirx = 1, diry = 0;     // moving right

    // Seed initial snake in center
    int cx = px0 + PW/2;
    int cy = py0 + PH/2;
    for (int i=0;i<len;i++){
        head = (head + 1) % MAX_SEG;
        sx[head] = cx - (len-1-i); sy[head] = cy;
        occ[sy[head]][sx[head]] = 1;
        vga_put(sx[head], sy[head], (i==len-1)?'@':'o', COL(10,0));
    }

    // Food
    int fx=px0, fy=py0;
    auto_place_food:;
    for (int tries=0;tries<10000;tries++){
        int rx = (int)(px0 + (rng_next() % (uint32_t)PW));
        int ry = (int)(py0 + (rng_next() % (uint32_t)PH));
        if (!occ[ry][rx]){ fx=rx; fy=ry; break; }
    }
    vga_put(fx, fy, '*', COL(12,0));

    // Status
    int score = 0; char sbuf[48]; int sp=0;
    const uint32_t t_hz = platform_timer_get_hz()?platform_timer_get_hz():100;
    uint32_t step_ms = 140; // start speed

    // Game loop
    for (;;) {
        // Frame deadline
        uint32_t start = platform_ticks_get();
        uint32_t wait_ticks = (step_ms * t_hz + 999) / 1000u;

        // Input: process until tick
        for(;;){
            int k = keyboard_poll_char();
            if (k >= 0) {
                if (k == 0x1B || k == 0x11) { // ESC or Ctrl+Q
                    // Exit game
                    console_writeln("\n[snake: quit]\n");
                    return;
                }
                if (k == KEY_UP || k=='w' || k=='W') { if (diry!=1){ dirx=0; diry=-1; } }
                else if (k == KEY_DOWN || k=='s' || k=='S') { if (diry!=-1){ dirx=0; diry=1; } }
                else if (k == KEY_LEFT || k=='a' || k=='A') { if (dirx!=1){ dirx=-1; diry=0; } }
                else if (k == KEY_RIGHT || k=='d' || k=='D') { if (dirx!=-1){ dirx=1; diry=0; } }
            }
            // timing
            uint32_t now = platform_ticks_get();
            if ((now - start) >= wait_ticks) break;
        }

        // Compute next head
        int hx = sx[head];
        int hy = sy[head];
        int nx = hx + dirx;
        int ny = hy + diry;

        // Border collision
        if (nx < px0 || nx > px1 || ny < py0 || ny > py1) {
            // Game over
            pcspeaker_beep(200, 180);
            pcspeaker_beep(150, 180);
            console_writeln("\n[snake: game over]\n");
            return;
        }

        // Eating?
        bool grow = (nx == fx && ny == fy);

        // Tail index (to clear if not growing)
        int tail_idx = (head - (len - 1) + MAX_SEG) % MAX_SEG;
        if (!grow) {
            // Allow moving into the old tail cell
            occ[ sy[tail_idx] ][ sx[tail_idx] ] = 0;
            vga_put(sx[tail_idx], sy[tail_idx], ' ', COL(7,0));
        }

        // Self collision
        if (occ[ny][nx]) {
            pcspeaker_beep(200, 180);
            pcspeaker_beep(150, 180);
            console_writeln("\n[snake: game over]\n");
            return;
        }

        // Advance head
        head = (head + 1) % MAX_SEG;
        sx[head] = nx; sy[head] = ny; occ[ny][nx] = 1;
        vga_put(nx, ny, '@', COL(14,0));
        // Draw previous head as body
        vga_put(hx, hy, 'o', COL(10,0));

        if (grow) {
            len++;
            score += 10;
            pcspeaker_beep(880, 40);
            // place new food
            goto auto_place_food;
        }

        // Speed up gradually (min cap)
        if (step_ms > 70 && (score % 50)==0) step_ms -= 2;

        // Update status bar left text
        const char* prefix = "snake score: "; sp=0;
        for (int i=0; prefix[i] && i<40; i++) sbuf[sp++]=prefix[i];
        // score decimal
        uint32_t v=(uint32_t)score; char tmp[10]; int ti=0; if (v==0) tmp[ti++]='0'; else { while(v&&ti<10){ tmp[ti++]=(char)('0'+(v%10)); v/=10; } }
        while (ti--) { if (sp < (int)sizeof(sbuf)-1) sbuf[sp++]=tmp[ti]; }
        sbuf[sp]=0;
        console_status_set_left(sbuf);
    }
}
