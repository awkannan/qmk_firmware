// Copyright 2023 Andrew Kannan
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef OLED_ENABLE
#    include "oled_driver.h"
#endif

/*
 * The Satisfaction75 family ships two OLED panels.  Both present a 128px wide
 * logical canvas once the driver's rotation is applied; they differ only in
 * height.
 *
 *   small   128x32 panel, OLED_ROTATION_0    ->  128 x 32 canvas, 21 x 4 chars
 *   large    64x128 panel, OLED_ROTATION_90  ->  128 x 64 canvas, 21 x 8 chars
 *
 * A board opts into the large panel by defining OLED_DISPLAY_64X128 in its
 * config.h, which is also what the OLED driver keys off.  Deriving the layout
 * from that macro keeps the two from drifting apart; define SAT75_OLED_LARGE
 * or SAT75_OLED_SMALL by hand only if you need to override it.
 */
#if !defined(SAT75_OLED_LARGE) && !defined(SAT75_OLED_SMALL)
#    if defined(OLED_DISPLAY_64X128)
#        define SAT75_OLED_LARGE
#    else
#        define SAT75_OLED_SMALL
#    endif
#endif

#ifdef SAT75_OLED_LARGE
#    define SAT75_OLED_ROTATION OLED_ROTATION_90
#    define SAT75_CANVAS_HEIGHT 64
#else
#    define SAT75_OLED_ROTATION OLED_ROTATION_0
#    define SAT75_CANVAS_HEIGHT 32
#endif

#define SAT75_CANVAS_WIDTH 128
#define SAT75_TEXT_ROWS (SAT75_CANVAS_HEIGHT / 8)
#define SAT75_TEXT_COLS (SAT75_CANVAS_WIDTH / 6) // OLED_FONT_WIDTH

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

#ifdef OLED_ENABLE

// --- primitives -------------------------------------------------------
void s75_draw_line_h(uint8_t x, uint8_t y, uint8_t len);
void s75_draw_line_v(uint8_t x, uint8_t y, uint8_t len);
void s75_fill_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h);

/*
 * Seven-segment digit rendering, for screens where the stock 6x8 font is too
 * small to read at a glance.  Segments are drawn as filled rectangles, so the
 * digits scale to whatever size the caller asks for and are not tied to the
 * text grid -- unlike oled_set_cursor(), y may be any pixel row.
 *
 * The string may contain '0'-'9', ':' and ' '.  Every glyph advances by the
 * same amount so digits never shift as the value changes; ' ' renders as a
 * blank of digit width, which is what blanks a 12 hour leading zero.
 */
void    s75_draw_7seg(const char *s, uint8_t x, uint8_t y, uint8_t digit_w, uint8_t digit_h, uint8_t thickness);
uint8_t s75_7seg_width(const char *s, uint8_t digit_w, uint8_t thickness);

// --- shared text helpers ----------------------------------------------
char *s75_get_enc_mode(void);
char *s75_get_time(void);
char *s75_get_date(void);

#endif // OLED_ENABLE
