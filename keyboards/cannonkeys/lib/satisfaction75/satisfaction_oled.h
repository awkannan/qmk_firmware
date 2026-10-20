// Copyright 2023 Andrew Kannan
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdint.h>
#include <stdbool.h>

/*
 * The Satisfaction75 family ships two OLED panels, both driven through Quantum
 * Painter.  Both present a 128px wide logical canvas once rotation is applied;
 * they differ only in height.
 *
 *   small   128x32 SSD1306, sh1106 driver, QP_ROTATION_0    ->  128 x 32 canvas, 21 x 4 chars
 *   large    64x128 SH1107, sh1107 driver, QP_ROTATION_270  ->  128 x 64 canvas, 21 x 8 chars
 *
 * A board opts into the large panel by defining SAT75_OLED_LARGE in its
 * config.h, and selecting the sh1107_i2c driver in its rules.mk.
 *
 * QP_ROTATION_270 rather than 90 on the large panel: it is the rotation that
 * lands each logical pixel where the old OLED driver's OLED_ROTATION_90 put it
 * (logical x,y -> panel column y, row 127 - x), so the image is the same way
 * up as it always was.
 */
#if !defined(SAT75_OLED_LARGE) && !defined(SAT75_OLED_SMALL)
#    define SAT75_OLED_SMALL
#endif

#ifdef SAT75_OLED_LARGE
#    define SAT75_PANEL_WIDTH 64
#    define SAT75_PANEL_HEIGHT 128
#    define SAT75_PANEL_ROTATION QP_ROTATION_270
#    define SAT75_CANVAS_HEIGHT 64
#else
#    define SAT75_PANEL_WIDTH 128
#    define SAT75_PANEL_HEIGHT 32
#    define SAT75_PANEL_ROTATION QP_ROTATION_0
#    define SAT75_CANVAS_HEIGHT 32
#endif

// Controller pages: 8px strips along the panel's native height.
#define SAT75_PANEL_PAGES (SAT75_PANEL_HEIGHT / 8)

#define SAT75_CANVAS_WIDTH 128
#define SAT75_FONT_WIDTH 6
#define SAT75_FONT_HEIGHT 8
#define SAT75_TEXT_ROWS (SAT75_CANVAS_HEIGHT / SAT75_FONT_HEIGHT)
#define SAT75_TEXT_COLS (SAT75_CANVAS_WIDTH / SAT75_FONT_WIDTH)

#ifndef SAT75_OLED_I2C_ADDRESS
#    define SAT75_OLED_I2C_ADDRESS 0x3C
#endif

// How often the screen is redrawn, in ms.  ~15fps.
#ifndef SAT75_OLED_REPAINT_INTERVAL
#    define SAT75_OLED_REPAINT_INTERVAL 66
#endif

// Pixels per key in the matrix activity display.
#ifdef SAT75_OLED_LARGE
#    define SAT75_MTX_SCALE 2
#else
#    define SAT75_MTX_SCALE 1
#endif

// Show the clock as 24 hour rather than 12 hour + am/pm.
// #define SAT75_CLOCK_24H

/*
 * Calendar helpers.  Declared outside the OLED guard: the pomodoro timer needs
 * them to build a clock that does not wrap at midnight, whether or not the
 * board has a screen.
 */
uint32_t s75_days_from_civil(int16_t y, uint8_t m, uint8_t d);
uint8_t  s75_day_of_week(int16_t year, uint8_t month, uint8_t day);

#ifdef QUANTUM_PAINTER_ENABLE

// --- display ----------------------------------------------------------
void s75_display_init(void);
// Redraws and pushes the screen as needed.  Call from housekeeping_task_kb().
void s75_display_task(void);

// --- primitives -------------------------------------------------------
void s75_draw_line_h(uint8_t x, uint8_t y, uint8_t len);
void s75_draw_line_v(uint8_t x, uint8_t y, uint8_t len);
void s75_fill_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h);
// Bitmap data in the OLED controller's page layout, drawn at the origin.
void s75_draw_raw(const uint8_t *data, uint16_t size);

/*
 * Seven-segment digit rendering, for screens where the stock 6x8 font is too
 * small to read at a glance.  Segments are drawn as filled rectangles, so the
 * digits scale to whatever size the caller asks for and are not tied to the
 * text grid -- unlike s75_set_cursor(), y may be any pixel row.
 *
 * The string may contain '0'-'9', ':' and ' '.  Every glyph advances by the
 * same amount so digits never shift as the value changes; ' ' renders as a
 * blank of digit width, which is what blanks a 12 hour leading zero.
 */
void    s75_draw_7seg(const char *s, uint8_t x, uint8_t y, uint8_t digit_w, uint8_t digit_h, uint8_t thickness);
uint8_t s75_7seg_width(const char *s, uint8_t digit_w, uint8_t thickness);

// --- text --------------------------------------------------------------

/*
 * A character-grid text layer over the 6x8 glcdfont, with the cursor, advance
 * and wrapping rules of the old OLED driver's oled_set_cursor()/oled_write(),
 * so the screen layouts carry over unchanged.  Inverted text is drawn white
 * on black by swapping the colours, which covers the full 6px cell.
 */
void s75_set_cursor(uint8_t col, uint8_t row);
void s75_write(const char *str, bool invert);
void s75_write_char(char c, bool invert);
void s75_advance_char(void);
void s75_write_pixel(uint8_t x, uint8_t y, bool on);

// --- shared text helpers ----------------------------------------------
char *s75_get_enc_mode(void);
char *s75_get_time(void);
char *s75_get_date(void);

#endif // QUANTUM_PAINTER_ENABLE
