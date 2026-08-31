// Host shim: mirrors QMK's oled_driver.h API over a plain byte buffer.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "progmem.h"

#define OLED_FONT_WIDTH  6
#define OLED_FONT_HEIGHT 8
#define OLED_FONT_START  0
#define OLED_FONT_END    223

typedef enum { OLED_ROTATION_0 = 0, OLED_ROTATION_90 = 1, OLED_ROTATION_180 = 2, OLED_ROTATION_270 = 3 } oled_rotation_t;

// The sim models the POST-ROTATION logical canvas, which is what every
// draw_*() function actually sees.  The driver's rotate_90() lives below this
// layer and is out of scope here.
#ifdef SAT75_SIM_LARGE
#    define OLED_DISPLAY_64X128
#endif

#ifdef OLED_DISPLAY_64X128
#    define SIM_CANVAS_WIDTH  128
#    define SIM_CANVAS_HEIGHT 64
#else
#    define SIM_CANVAS_WIDTH  128
#    define SIM_CANVAS_HEIGHT 32
#endif
#define SIM_MATRIX_SIZE (SIM_CANVAS_WIDTH * SIM_CANVAS_HEIGHT / 8)

bool oled_init(oled_rotation_t rotation);
void oled_clear(void);
void oled_set_cursor(uint8_t col, uint8_t line);
void oled_advance_char(void);
void oled_advance_page(bool clearPageRemainder);
void oled_write_char(const char data, bool invert);
void oled_write(const char *data, bool invert);
void oled_write_ln(const char *data, bool invert);
void oled_write_P(const char *data, bool invert);
void oled_write_raw(const char *data, uint16_t size);
void oled_write_raw_P(const char *data, uint16_t size);
void oled_write_pixel(uint8_t x, uint8_t y, bool on);
uint8_t oled_max_chars(void);
uint8_t oled_max_lines(void);
bool oled_on(void);
bool oled_off(void);
bool is_oled_on(void);

// --- sim only ---------------------------------------------------------
extern uint8_t oled_buffer[SIM_MATRIX_SIZE];
void sim_oled_dump(const char *title);
void sim_oled_dump_pbm(const char *path);

// Weak-in-QMK hooks the keyboard code implements or calls.
oled_rotation_t oled_init_kb(oled_rotation_t rotation);
oled_rotation_t oled_init_user(oled_rotation_t rotation);
bool oled_task_kb(void);
bool oled_task_user(void);
