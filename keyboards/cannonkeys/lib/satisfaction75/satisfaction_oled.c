// Copyright 2023 Andrew Kannan
// SPDX-License-Identifier: GPL-2.0-or-later

#include "satisfaction_core.h"
#include "satisfaction_oled.h"
#include "action_layer.h"
#include "action_util.h"
#include "timer.h"
#include "matrix.h"
#include "led.h"
#include "host.h"
#include "progmem.h"
#include <stdio.h>
#include <string.h>

#ifdef WPM_ENABLE
#    include "wpm.h"
#endif

#include "satisfaction_bongo.h"

#include "satisfaction_pomodoro.h"

void draw_default(void);
void draw_clock(void);

/*
 * Days since 1970-01-01, from Howard Hinnant's days_from_civil.  The RTC only
 * ever holds years >= 1980, so the negative-era handling is not needed.  Used
 * for the day of week here, and by the pomodoro timer to build a clock value
 * that does not wrap at midnight.
 */
uint32_t s75_days_from_civil(int16_t y, uint8_t m, uint8_t d) {
    y -= (m <= 2);
    const uint16_t era = y / 400;
    const uint16_t yoe = y - era * 400;
    const uint16_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const uint32_t doe = (uint32_t)yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (uint32_t)era * 146097 + doe - 719468;
}

// 0 = Sunday.  1970-01-01 was a Thursday.
uint8_t s75_day_of_week(int16_t year, uint8_t month, uint8_t day) {
    return (uint8_t)((s75_days_from_civil(year, month, day) + 4) % 7);
}


#ifdef QUANTUM_PAINTER_ENABLE
#    include "qp.h"
#    include "qp_surface.h"
#    include "qp_comms.h"
#    ifdef SAT75_OLED_LARGE
#        include "qp_sh1107.h"
#    else
#        include "qp_sh1106.h"
#    endif
#    include "s75_glcdfont.qff.h"

/*
 * Rendering happens in two stages.
 *
 * Every repaint draws the whole screen from scratch into `canvas`, an
 * offscreen 1bpp surface.  Starting from a blank canvas keeps each draw_*()
 * function free of any knowledge of what was on screen before.
 *
 * The canvas then reaches the panel one controller page at a time, one page per
 * main loop pass, by copying it into the panel driver's own framebuffer and
 * flushing.  That framebuffer only marks pixels dirty when their value actually
 * changes, so a flush sends just the part of the page that differs from what
 * the panel already shows; an unchanged page sends nothing.  Pushing a page at
 * a time bounds how long the main loop blocks on I2C (qp_flush() is
 * synchronous) to one page, roughly what the old OLED driver sent per pass.
 *
 * A new frame is not drawn until every page of the previous one has been
 * pushed.  That is what stops the bottom of the screen going stale: the old
 * driver restarted from its first block on every repaint and, on the 64x128
 * panel, never reached the last ones.
 */
static painter_device_t      display;
static painter_device_t      canvas;
static painter_font_handle_t font;

static uint8_t canvas_buffer[SURFACE_REQUIRED_BUFFER_BYTE_SIZE(SAT75_CANVAS_WIDTH, SAT75_CANVAS_HEIGHT, 1)];

/*
 * A "page" is the strip of canvas that lands in one controller page.  On the
 * small panel at rotation 0 that is a row of 8px; on the large panel, rotated,
 * it is a column 8px wide.
 */
#    define PAGE_COUNT SAT75_PANEL_PAGES
#    ifdef SAT75_OLED_LARGE
#        define PAGE_W 8
#        define PAGE_H SAT75_CANVAS_HEIGHT
#        define PAGE_X(p) ((p) * 8)
#        define PAGE_Y(p) 0
#    else
#        define PAGE_W SAT75_CANVAS_WIDTH
#        define PAGE_H 8
#        define PAGE_X(p) 0
#        define PAGE_Y(p) ((p) * 8)
#    endif

static uint8_t  next_page = PAGE_COUNT; // PAGE_COUNT when the last frame is fully pushed
static uint32_t last_repaint;
static bool     display_on;

// The text cursor, in character cells.
static uint8_t cursor_col;
static uint8_t cursor_row;

static void set_power(bool on) {
    if (on != display_on) {
        qp_power(display, on);
        display_on = on;
    }
}

/*
 * Sent after Quantum Painter's own init sequence, to match what the old OLED
 * driver configured.  QP leaves contrast at 0x7F and the precharge and VCOM
 * levels at their reset values, which renders noticeably dimmer than these
 * panels have always looked.
 */
static const uint8_t panel_setup[] = {
    // Command, Delay, N, Data[N]
    0x81, 0, 1, 0xFF, // contrast
    0xD9, 0, 1, 0xF1, // precharge period
    0xDB, 0, 1, 0x20, // VCOM deselect level
#    ifndef SAT75_OLED_LARGE
    // QP drives the SSD1306 with page addressing commands.  Page mode is the
    // controller's power-on default, but the old OLED driver set horizontal
    // mode, and that survives a DFU reflash because the panel never loses
    // power.
    0x20, 0, 1, 0x02, // memory addressing mode: page
#    endif
};

static void init_panel(void) {
    qp_init(display, SAT75_PANEL_ROTATION);
    // A bulk sequence rather than qp_comms_command_databyte(), which would
    // send the parameter bytes as display data.
    if (qp_comms_start(display)) {
        qp_comms_bulk_command_sequence(display, panel_setup, sizeof(panel_setup));
        qp_comms_stop(display);
    }
    // The panel's RAM holds whatever it powered up with.  A fresh init marks
    // the whole (blank) framebuffer dirty, so this flush clears it.
    qp_flush(display);
    display_on = true;
    next_page  = PAGE_COUNT;
}

void s75_display_init(void) {
#    ifdef SAT75_OLED_LARGE
    display = qp_sh1107_make_i2c_device(SAT75_PANEL_WIDTH, SAT75_PANEL_HEIGHT, SAT75_OLED_I2C_ADDRESS);
#    else
    display = qp_sh1106_make_i2c_device(SAT75_PANEL_WIDTH, SAT75_PANEL_HEIGHT, SAT75_OLED_I2C_ADDRESS);
#    endif
    canvas = qp_make_mono1bpp_surface(SAT75_CANVAS_WIDTH, SAT75_CANVAS_HEIGHT, canvas_buffer);
    qp_init(canvas, QP_ROTATION_0);
    font = qp_load_font_mem(font_s75_glcdfont);

    init_panel();
    oled_request_wakeup();
}

static void draw_screen(void) {
    if (clock_set_mode) {
        draw_clock();
        return;
    }
    switch (oled_mode) {
        default:
        case OLED_DEFAULT:
            draw_default();
            break;
        case OLED_TIME:
            draw_clock();
            break;
#    ifdef SAT75_BONGO
        case OLED_BONGO:
            draw_bongo(false);
            break;
        case OLED_BONGO_MIN:
            draw_bongo(true);
            break;
#    endif
#    ifdef SAT75_POMODORO
        case OLED_POMODORO:
            draw_pomodoro();
            break;
#    endif
    }
}

static void repaint(void) {
    qp_rect(canvas, 0, 0, SAT75_CANVAS_WIDTH - 1, SAT75_CANVAS_HEIGHT - 1, 0, 0, 0, true);
    cursor_col = 0;
    cursor_row = 0;
    draw_screen();
    next_page = 0;
}

// Copies one page of the canvas into the panel's framebuffer, then sends
// whatever part of it changed.
static void push_page(uint8_t page) {
    uint8_t        packed[(PAGE_W * PAGE_H + 7) / 8] = {0};
    const uint16_t x0                                = PAGE_X(page);
    const uint16_t y0                                = PAGE_Y(page);
    uint16_t       n                                 = 0;

    // Both buffers are 1bpp, row major, least significant bit first.
    for (uint16_t y = y0; y < y0 + PAGE_H; y++) {
        for (uint16_t x = x0; x < x0 + PAGE_W; x++, n++) {
            uint16_t src = y * SAT75_CANVAS_WIDTH + x;
            if (canvas_buffer[src / 8] & (1 << (src % 8))) {
                packed[n / 8] |= 1 << (n % 8);
            }
        }
    }

    qp_viewport(display, x0, y0, x0 + PAGE_W - 1, y0 + PAGE_H - 1);
    qp_pixdata(display, packed, PAGE_W * PAGE_H);
    qp_flush(display);
}

void s75_display_task(void) {
    if (!display) {
        return;
    }

    /*
     * The panel can miss its init when the board is first plugged in, and
     * stay dark until the next reset.  keyboard_post_init_kb() used to work
     * around this with a blocking 3s delay and re-init; re-initialising once
     * after 3s of uptime does the same without stalling the keyboard at boot.
     */
    static bool reinit_done = false;
    if (!reinit_done && timer_read32() > 3000) {
        reinit_done = true;
        init_panel();
        oled_request_repaint();
    }

    if (next_page < PAGE_COUNT) {
        push_page(next_page++);
        return;
    }

    if (timer_elapsed32(last_repaint) < SAT75_OLED_REPAINT_INTERVAL) {
        return;
    }
    last_repaint = timer_read32();

    if (oled_task_needs_to_repaint()) {
        repaint();
    }
}

// Request a repaint of the OLED image without resetting the OLED sleep timer.
// Used for things like clock updates that should not keep the OLED turned on
// if there is no other activity.
void oled_request_repaint(void) {
    if (display_on) {
        oled_repaint_requested = true;
    }
}

// Request a repaint of the OLED image and reset the OLED sleep timer.
// Needs to be called after any activity that should keep the OLED turned on.
void oled_request_wakeup(void) {
    oled_wakeup_requested = true;
}

// Check whether the OLED image needs to be repainted.  Also handles the OLED
// sleep timer and the OLED_OFF mode.
bool oled_task_needs_to_repaint(void) {
    // In the OLED_OFF mode the OLED is kept turned off; any wakeup requests
    // are ignored.
    if ((oled_mode == OLED_OFF) && !clock_set_mode) {
        oled_wakeup_requested  = false;
        oled_repaint_requested = false;
        set_power(false);
        return false;
    }

    // If OLED wakeup was requested, reset the sleep timer and do a repaint.
    if (oled_wakeup_requested) {
        oled_wakeup_requested  = false;
        oled_repaint_requested = false;
        oled_sleep_timer       = timer_read32() + CUSTOM_OLED_TIMEOUT;
        set_power(true);
        return true;
    }

    // If OLED repaint was requested, just do a repaint without touching the
    // sleep timer.
    if (oled_repaint_requested) {
        oled_repaint_requested = false;
        return true;
    }

    // If the OLED is currently off, skip the repaint.
    if (!display_on) {
        return false;
    }

    // If the sleep timer has expired while the OLED was on, turn the OLED off.
    if (timer_expired32(timer_read32(), oled_sleep_timer)) {
        set_power(false);
        return false;
    }

    // Always perform a repaint if the OLED is currently on.  (This can
    // potentially be optimized to avoid unneeded repaints if all possible
    // state changes are covered by oled_request_repaint() or
    // oled_request_wakeup(), but then any missed calls to these functions
    // would result in displaying a stale image.)
    return true;
}

// ---------------------------------------------------------------------
// Drawing primitives
// ---------------------------------------------------------------------

void s75_draw_line_h(uint8_t x, uint8_t y, uint8_t len) {
    s75_fill_rect(x, y, len, 1);
}

void s75_draw_line_v(uint8_t x, uint8_t y, uint8_t len) {
    s75_fill_rect(x, y, 1, len);
}

void s75_fill_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h) {
    if (w == 0 || h == 0) {
        return;
    }
    qp_rect(canvas, x, y, x + w - 1, y + h - 1, 0, 0, 255, true);
}

void s75_write_pixel(uint8_t x, uint8_t y, bool on) {
    qp_setpixel(canvas, x, y, 0, 0, on ? 255 : 0);
}

/*
 * Draws bitmap data in the OLED controller's page layout -- each byte a column
 * of 8 vertical pixels, least significant bit at the top, pages running left
 * to right and then down -- starting at the canvas origin.  This is what
 * oled_write_raw() took, and it lets the bongo art stay in that form.  A one
 * pixel wide viewport streams pixels top to bottom, which is exactly one
 * column byte.
 */
void s75_draw_raw(const uint8_t *data, uint16_t size) {
    for (uint16_t i = 0; i < size; i++) {
        uint16_t x = i % SAT75_CANVAS_WIDTH;
        uint16_t y = (i / SAT75_CANVAS_WIDTH) * 8;
        if (y >= SAT75_CANVAS_HEIGHT) {
            break;
        }
        qp_viewport(canvas, x, y, x, y + 7);
        qp_pixdata(canvas, &data[i], 8);
    }
}

// ---------------------------------------------------------------------
// Text
// ---------------------------------------------------------------------

void s75_set_cursor(uint8_t col, uint8_t row) {
    // Out of range lands at the origin, as s75_set_cursor() does.
    if (row >= SAT75_TEXT_ROWS || col >= SAT75_TEXT_COLS) {
        col = 0;
        row = 0;
    }
    cursor_col = col;
    cursor_row = row;
}

// Wraps to the next row at the right edge, and back to the top after the last.
void s75_advance_char(void) {
    if (++cursor_col >= SAT75_TEXT_COLS) {
        cursor_col = 0;
        if (++cursor_row >= SAT75_TEXT_ROWS) {
            cursor_row = 0;
        }
    }
}

void s75_write_char(char c, bool invert) {
    uint16_t x = cursor_col * SAT75_FONT_WIDTH;
    uint16_t y = cursor_row * SAT75_FONT_HEIGHT;
    uint8_t  fg = invert ? 0 : 255;

    if (c >= 0x20 && c <= 0x7E) {
        char str[2] = {c, '\0'};
        qp_drawtext_recolor(canvas, x, y, font, str, 0, 0, fg, 0, 0, 255 - fg);
    } else {
        // Outside the font: a blank cell.
        qp_rect(canvas, x, y, x + SAT75_FONT_WIDTH - 1, y + SAT75_FONT_HEIGHT - 1, 0, 0, 255 - fg, true);
    }
    s75_advance_char();
}

void s75_write(const char *str, bool invert) {
    while (*str) {
        s75_write_char(*str++, invert);
    }
}

// ---------------------------------------------------------------------
// Seven-segment digits
// ---------------------------------------------------------------------

// Bit per segment: a=top, b=upper right, c=lower right, d=bottom,
// e=lower left, f=upper left, g=middle.
#    define SEG_A 0x01
#    define SEG_B 0x02
#    define SEG_C 0x04
#    define SEG_D 0x08
#    define SEG_E 0x10
#    define SEG_F 0x20
#    define SEG_G 0x40

static const uint8_t PROGMEM seven_seg_digits[10] = {
    SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F,         // 0
    SEG_B | SEG_C,                                         // 1
    SEG_A | SEG_B | SEG_G | SEG_E | SEG_D,                 // 2
    SEG_A | SEG_B | SEG_G | SEG_C | SEG_D,                 // 3
    SEG_F | SEG_G | SEG_B | SEG_C,                         // 4
    SEG_A | SEG_F | SEG_G | SEG_C | SEG_D,                 // 5
    SEG_A | SEG_F | SEG_G | SEG_E | SEG_C | SEG_D,         // 6
    SEG_A | SEG_B | SEG_C,                                 // 7
    SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G, // 8
    SEG_A | SEG_B | SEG_C | SEG_D | SEG_F | SEG_G,         // 9
};

// A colon is drawn narrower than a digit; every other glyph advances by the
// full digit width so values never shift horizontally as they change.
#    define SEG_GAP(t) ((t) > 1 ? (uint8_t)((t) - 1) : (uint8_t)1)
#    define SEG_GLYPH_W(ch, w, t) ((ch) == ':' ? (uint8_t)((t) * 2) : (w))

static void draw_digit(uint8_t segs, uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t t) {
    // Top of the middle bar.  The lower verticals take whatever is left over
    // so they always meet the bottom bar exactly, at any size.
    uint8_t mid   = (h - t) / 2;
    uint8_t upper = mid - t;
    uint8_t lower = h - mid - 2 * t;
    uint8_t inner = w - 2 * t;

    if (segs & SEG_A) s75_fill_rect(x + t, y, inner, t);
    if (segs & SEG_F) s75_fill_rect(x, y + t, t, upper);
    if (segs & SEG_B) s75_fill_rect(x + w - t, y + t, t, upper);
    if (segs & SEG_G) s75_fill_rect(x + t, y + mid, inner, t);
    if (segs & SEG_E) s75_fill_rect(x, y + mid + t, t, lower);
    if (segs & SEG_C) s75_fill_rect(x + w - t, y + mid + t, t, lower);
    if (segs & SEG_D) s75_fill_rect(x + t, y + h - t, inner, t);
}

uint8_t s75_7seg_width(const char *s, uint8_t digit_w, uint8_t thickness) {
    uint8_t total = 0;
    for (const char *p = s; *p; p++) {
        if (p != s) total += SEG_GAP(thickness);
        total += SEG_GLYPH_W(*p, digit_w, thickness);
    }
    return total;
}

void s75_draw_7seg(const char *s, uint8_t x, uint8_t y, uint8_t digit_w, uint8_t digit_h, uint8_t thickness) {
    for (const char *p = s; *p; p++) {
        if (p != s) x += SEG_GAP(thickness);
        if (*p >= '0' && *p <= '9') {
            draw_digit(pgm_read_byte(&seven_seg_digits[*p - '0']), x, y, digit_w, digit_h, thickness);
        } else if (*p == ':') {
            uint8_t dot  = thickness;
            uint8_t dotx = x + (SEG_GLYPH_W(':', digit_w, thickness) - dot) / 2;
            s75_fill_rect(dotx, y + digit_h / 3 - dot / 2, dot, dot);
            s75_fill_rect(dotx, y + (2 * digit_h) / 3 - dot / 2, dot, dot);
        }
        // ' ' and anything else render blank, but still advance.
        x += SEG_GLYPH_W(*p, digit_w, thickness);
    }
}

// ---------------------------------------------------------------------
// Shared text helpers
// ---------------------------------------------------------------------

char *s75_get_enc_mode(void) {
    switch (encoder_mode) {
        default:
        case ENC_MODE_VOLUME:
            return "VOL";
        case ENC_MODE_MEDIA:
            return "MED";
        case ENC_MODE_SCROLL:
            return "SCR";
        case ENC_MODE_BRIGHTNESS:
            return "BRT";
        case ENC_MODE_BACKLIGHT:
            return "BKL";
        case ENC_MODE_CLOCK_SET:
            return "CLK";
        case ENC_MODE_CUSTOM0:
            return "CS0";
        case ENC_MODE_CUSTOM1:
            return "CS1";
        case ENC_MODE_CUSTOM2:
            return "CS2";
    }
}

char *s75_get_time(void) {
    uint8_t  hour   = last_minute / 60;
    uint16_t minute = last_minute % 60;

    if (encoder_mode == ENC_MODE_CLOCK_SET) {
        hour   = hour_config;
        minute = minute_config;
    }

    bool is_pm = (hour / 12) > 0;
    hour       = hour % 12;
    if (hour == 0) {
        hour = 12;
    }

    static char time_str[8] = "";
    snprintf(time_str, sizeof(time_str), "%02hhu:%02hu%s", hour, minute, is_pm ? "pm" : "am");

    return time_str;
}

char *s75_get_date(void) {
    int16_t year  = last_timespec.year + 1980;
    int8_t  month = last_timespec.month;
    int8_t  day   = last_timespec.day;

    if (encoder_mode == ENC_MODE_CLOCK_SET) {
        year  = year_config + 1980;
        month = month_config;
        day   = day_config;
    }

    static char date_str[11] = "";
    snprintf(date_str, sizeof(date_str), "%04hd-%02hhd-%02hhd", year, month, day);

    return date_str;
}

// Writes the four modifier flags as "S C A G", inverting the active ones.
static void draw_mod_flags(void) {
    uint8_t mod_state = get_mods();
    s75_write("S", mod_state & MOD_MASK_SHIFT);
    s75_advance_char();
    s75_write("C", mod_state & MOD_MASK_CTRL);
    s75_advance_char();
    s75_write("A", mod_state & MOD_MASK_ALT);
    s75_advance_char();
    s75_write("G", mod_state & MOD_MASK_GUI);
    s75_advance_char();
}

// ---------------------------------------------------------------------
// Screens
// ---------------------------------------------------------------------

#    ifdef SAT75_OLED_SMALL

/* Matrix display is 12 x 12 pixels */
#        define MATRIX_DISPLAY_X 0
#        define MATRIX_DISPLAY_Y 18

void draw_default(void) {
    s75_write("LAYER ", false);
    s75_write_char(get_highest_layer(layer_state) + 0x30, true);

    s75_write(" ENC ", false);
    s75_write(s75_get_enc_mode(), true);

    led_t led_state = host_keyboard_led_state();
    s75_set_cursor(18, 0);
    s75_write("CAP", led_state.caps_lock);
    s75_set_cursor(18, 1);
    s75_write("SCR", led_state.scroll_lock);

    s75_set_cursor(6, 3);
    draw_mod_flags();

    s75_write(s75_get_time(), false);

    // matrix
    for (uint8_t x = 0; x < MATRIX_ROWS; x++) {
        for (uint8_t y = 0; y < MATRIX_COLS; y++) {
            bool on = (matrix_get_row(x) & (1 << y)) > 0;
            s75_write_pixel(MATRIX_DISPLAY_X + y + 2, MATRIX_DISPLAY_Y + x + 2, on);
        }
    }

    // outline
    s75_draw_line_h(MATRIX_DISPLAY_X, MATRIX_DISPLAY_Y, 19);
    s75_draw_line_h(MATRIX_DISPLAY_X, MATRIX_DISPLAY_Y + 9, 19);
    s75_draw_line_v(MATRIX_DISPLAY_X, MATRIX_DISPLAY_Y, 9);
    s75_draw_line_v(MATRIX_DISPLAY_X + 19, MATRIX_DISPLAY_Y, 9);

    // oled location
    s75_draw_line_h(MATRIX_DISPLAY_X + 14, MATRIX_DISPLAY_Y + 2, 3);

    // bodge extra lines for invert layer and enc mode
    s75_draw_line_v(35, 0, 8);
    s75_draw_line_v(71, 0, 8);
}

void draw_clock(void) {
    s75_set_cursor(0, 0);
    s75_write(s75_get_date(), false);
    s75_set_cursor(0, 2);
    s75_write(s75_get_time(), false);

    s75_set_cursor(12, 0);
    s75_write(" ENC ", false);
    s75_write(s75_get_enc_mode(), true);

    s75_set_cursor(13, 1);
    s75_write("LAYER ", false);
    s75_write_char(get_highest_layer(layer_state) + 0x30, true);

    led_t led_state = host_keyboard_led_state();
    s75_set_cursor(15, 3);
    s75_write("CAPS", led_state.caps_lock);

    if (clock_set_mode) {
        switch (time_config_idx) {
            case 0: // hour
            default:
                s75_draw_line_h(0, 25, 10);
                break;
            case 1: // minute
                s75_draw_line_h(18, 25, 10);
                break;
            case 2: // year
                s75_draw_line_h(0, 9, 24);
                break;
            case 3: // month
                s75_draw_line_h(30, 9, 10);
                break;
            case 4: // day
                s75_draw_line_h(48, 9, 10);
                break;
        }
    }

    // bodge extra lines for invert layer and enc mode
    s75_draw_line_v(101, 0, 8);
    s75_draw_line_v(113, 8, 8);
}

#    else // SAT75_OLED_LARGE

// The dedicated clock screen (OLED_TIME / clock_set_mode) gets the full size
// digits; they run nearly the whole width of the panel.
#        define CLK_DIGIT_W 22
#        define CLK_DIGIT_H 32
#        define CLK_THICK 4

// The default screen's clock is deliberately smaller.  It shares the panel
// with six other fields, and shrinking it from 32px to 24px is what buys the
// gutter above and below -- the 21x8 grid has no spare row otherwise.
#        define CLK_SM_DIGIT_W 18
#        define CLK_SM_DIGIT_H 24
#        define CLK_SM_THICK 3

// Matrix activity display, one SAT75_MTX_SCALE-sized block per key.
#        define MTX_BOX_W (MATRIX_COLS * SAT75_MTX_SCALE + 2)
#        define MTX_BOX_H (MATRIX_ROWS * SAT75_MTX_SCALE + 2)

static void draw_matrix_display(uint8_t x, uint8_t y) {
    for (uint8_t row = 0; row < MATRIX_ROWS; row++) {
        for (uint8_t col = 0; col < MATRIX_COLS; col++) {
            if ((matrix_get_row(row) & (1 << col)) > 0) {
                s75_fill_rect(x + 1 + col * SAT75_MTX_SCALE, y + 1 + row * SAT75_MTX_SCALE, SAT75_MTX_SCALE, SAT75_MTX_SCALE);
            }
        }
    }

    s75_draw_line_h(x, y, MTX_BOX_W);
    s75_draw_line_h(x, y + MTX_BOX_H - 1, MTX_BOX_W);
    s75_draw_line_v(x, y, MTX_BOX_H);
    s75_draw_line_v(x + MTX_BOX_W - 1, y, MTX_BOX_H);

    // marks roughly where the OLED sits on the board
    s75_draw_line_h(x + 1 + 12 * SAT75_MTX_SCALE, y + 2, 3 * SAT75_MTX_SCALE);
}

// "LAYER n  ENC MMM  CAP", with the layer and encoder mode inverted.  ENC
// starts at column 9 so the gaps either side of it are both two characters.
// The two vertical rules close the left hand edge of the inverted blocks,
// which the 6x8 font leaves open.
static void draw_status_row(void) {
    s75_write("LAYER ", false);
    s75_write_char(get_highest_layer(layer_state) + 0x30, true);

    s75_set_cursor(9, 0);
    s75_write("ENC ", false);
    s75_write(s75_get_enc_mode(), true);

    s75_set_cursor(18, 0);
    s75_write("CAP", host_keyboard_led_state().caps_lock);

    s75_draw_line_v(35, 0, 8);
    s75_draw_line_v(77, 0, 8);
}

// "HH:MM" for the seven-segment clock.  In 12 hour mode a leading zero is
// blanked rather than dropped, so the digits keep their positions.
static void build_clock_digits(char *buf, size_t len) {
    uint8_t hour   = (uint8_t)(last_minute / 60);
    uint8_t minute = (uint8_t)(last_minute % 60);

    if (encoder_mode == ENC_MODE_CLOCK_SET) {
        hour   = (uint8_t)hour_config;
        minute = (uint8_t)minute_config;
    }

    // The clock-set values are user editable and unbounded as far as the
    // compiler is concerned, so clamp them: it keeps the formatted output
    // inside buf, and lets -Wformat-truncation see that it does.
    hour %= 24;
    minute %= 60;

#        ifndef SAT75_CLOCK_24H
    hour = hour % 12;
    if (hour == 0) {
        hour = 12;
    }
#        endif

    snprintf(buf, len, "%02u:%02u", (unsigned)hour, (unsigned)minute);
#        ifndef SAT75_CLOCK_24H
    if (buf[0] == '0') {
        buf[0] = ' ';
    }
#        endif
}

#        ifndef SAT75_CLOCK_24H
static bool clock_is_pm(void) {
    uint8_t hour = (encoder_mode == ENC_MODE_CLOCK_SET) ? (uint8_t)hour_config : (uint8_t)(last_minute / 60);
    return (hour / 12) > 0;
}
#        endif

// x of the clock block, centred in whatever is left after the am/pm column.
static uint8_t clock_origin_x(const char *digits, uint8_t digit_w, uint8_t thickness) {
    uint8_t w = s75_7seg_width(digits, digit_w, thickness);
#        ifdef SAT75_CLOCK_24H
    uint8_t avail = SAT75_CANVAS_WIDTH;
#        else
    uint8_t avail = SAT75_CANVAS_WIDTH - 14;
#        endif
    return w >= avail ? 0 : (uint8_t)((avail - w) / 2);
}

static void draw_big_clock(uint8_t y, uint8_t digit_w, uint8_t digit_h, uint8_t thickness) {
    char digits[8];
    build_clock_digits(digits, sizeof(digits));

    s75_draw_7seg(digits, clock_origin_x(digits, digit_w, thickness), y, digit_w, digit_h, thickness);

#        ifndef SAT75_CLOCK_24H
    // Sits on the same baseline as the bottom of the digits.
    s75_set_cursor(19, (uint8_t)((y + digit_h - 8) / 8));
    s75_write(clock_is_pm() ? "pm" : "am", false);
#        endif
}

// "MON 2026-08-31", or "MON 08-31" without the year.  The default screen drops
// the year to keep that row from filling all 21 columns; the clock screen keeps
// it, both because it has the room and because clock_set_mode has to underline
// it.  The RTC's own dayofweek field is not maintained by
// pre_encoder_mode_change(), so the day is derived from the date instead.
static void draw_date_text(bool with_year) {
    static const char *const days[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};

    int16_t year  = last_timespec.year + 1980;
    uint8_t month = last_timespec.month;
    uint8_t day   = last_timespec.day;
    if (encoder_mode == ENC_MODE_CLOCK_SET) {
        year  = year_config + 1980;
        month = month_config;
        day   = day_config;
    }

    s75_write(days[s75_day_of_week(year, month, day)], false);
    s75_write(" ", false);
    if (with_year) {
        s75_write(s75_get_date(), false);
    } else {
        // The % 100 is what lets -Wformat-truncation see that the result fits;
        // month and day are plain uint8_t as far as the compiler knows.
        char md[8];
        snprintf(md, sizeof(md), "%02u-%02u", (unsigned)(month % 100), (unsigned)(day % 100));
        s75_write(md, false);
    }
}

/*
 * The default screen, over the 21x8 character grid:
 *
 *   row 0     LAYER n / ENC mmm / CAP
 *   y 8-11    gutter
 *   y 12-35   compact clock, am/pm at the right
 *   y 36-39   gutter
 *   row 5     day + MM-DD, centred over the mod flags / WPM row
 *   row 6     pomodoro, when one is running
 *   row 7     mod flags, WPM
 *   rows 6-7  matrix activity box, at the left
 *
 * The clock is the one element not tied to the 8px text grid, which is what
 * makes the two gutters possible at all.
 *
 * The full size clock and the full date live on OLED_TIME, one encoder click
 * away, which is what lets this screen carry the abbreviated versions and
 * leave the middle of the panel with some air in it.
 */
void draw_default(void) {
    draw_status_row();
    // y=12 rather than 8: 32px of space, a 24px clock, so 4px above and below.
    draw_big_clock(12, CLK_SM_DIGIT_W, CLK_SM_DIGIT_H, CLK_SM_THICK);

    // "MON 08-31" is 9 characters; column 9 centres it over columns 6-20,
    // which is what the mod flags and WPM span on row 7.
    s75_set_cursor(9, 5);
    draw_date_text(false);

    draw_matrix_display(0, 48);

    s75_set_cursor(6, 7);
    draw_mod_flags();

#        ifdef WPM_ENABLE
    {
        static char wpm_str[9];
        snprintf(wpm_str, sizeof(wpm_str), "WPM %03u", (unsigned)get_current_wpm());
        s75_set_cursor(14, 7);
        s75_write(wpm_str, false);
    }
#        endif

#        ifdef SAT75_POMODORO
    s75_set_cursor(6, 6);
    draw_pomodoro_line();
#        endif
}

/*
 * The clock-only screen: full size digits, full date, nothing else.  Reached
 * as OLED_TIME, and again whenever clock_set_mode is on -- browsing and
 * editing share one layout, so the two cannot drift apart.  Setting swaps the
 * status row for a title and underlines the field the encoder is pointing at.
 */
void draw_clock(void) {
    char digits[8];
    build_clock_digits(digits, sizeof(digits));
    uint8_t x = clock_origin_x(digits, CLK_DIGIT_W, CLK_THICK);

    if (clock_set_mode) {
        s75_write("SET CLOCK", false);
        s75_set_cursor(14, 0);
        s75_write("ENC ", false);
        s75_write(s75_get_enc_mode(), true);
        s75_draw_line_v(125, 0, 8);
    } else {
        draw_status_row();
    }

    draw_big_clock(10, CLK_DIGIT_W, CLK_DIGIT_H, CLK_THICK);

    s75_set_cursor(0, 6);
    draw_date_text(true);

    if (!clock_set_mode) {
        return;
    }

    // Underline whichever field the encoder is currently editing.  The date
    // text starts at column 4, after the three character day name.
    uint8_t pair_w  = s75_7seg_width("00", CLK_DIGIT_W, CLK_THICK);
    uint8_t min_off = s75_7seg_width("00:", CLK_DIGIT_W, CLK_THICK) + SEG_GAP(CLK_THICK);

    switch (time_config_idx) {
        case 0: // hour
        default:
            s75_draw_line_h(x, 44, pair_w);
            break;
        case 1: // minute
            s75_draw_line_h(x + min_off, 44, pair_w);
            break;
        case 2: // year
            s75_draw_line_h(24, 57, 24);
            break;
        case 3: // month
            s75_draw_line_h(54, 57, 12);
            break;
        case 4: // day
            s75_draw_line_h(72, 57, 12);
            break;
    }
}

#    endif // SAT75_OLED_SMALL

#else // QUANTUM_PAINTER_ENABLE

void oled_request_repaint(void) {}

void oled_request_wakeup(void) {}

#endif
