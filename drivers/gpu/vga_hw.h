#ifndef DRIVERS_GPU_VGA_HW_H
#define DRIVERS_GPU_VGA_HW_H

#include <stdint.h>

uint8_t vga_seq_read(uint8_t index);
void    vga_seq_write(uint8_t index, uint8_t value);
uint8_t vga_crtc_read(uint8_t index);
void    vga_crtc_write(uint8_t index, uint8_t value);
uint8_t vga_gc_read(uint8_t index);
void    vga_gc_write(uint8_t index, uint8_t value);
uint8_t vga_attr_read(uint8_t index);
void    vga_attr_write(uint8_t index, uint8_t value);
void    vga_attr_reenable_video(void);
uint8_t vga_misc_read(void);
void    vga_misc_write(uint8_t value);
void    vga_dac_set_entry(uint8_t index, uint8_t r6, uint8_t g6, uint8_t b6);
void    vga_dac_set_entry_rgb(uint8_t index, uint8_t r8, uint8_t g8, uint8_t b8);
void    vga_dac_reset_text_palette(void);
void    vga_dac_load_default_palette(void);
uint8_t vga_pel_mask_read(void);
void    vga_pel_mask_write(uint8_t value);
void    vga_attr_mask(uint8_t index, uint8_t mask, uint8_t value);
void    vga_attr_index_write(uint8_t value);
void    vga_load_font_8x16(void);

#endif // DRIVERS_GPU_VGA_HW_H
