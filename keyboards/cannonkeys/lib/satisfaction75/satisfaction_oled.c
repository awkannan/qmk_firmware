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


#ifdef OLED_ENABLE
#    include "oled_driver.h"

oled_rotation_t oled_init_kb(oled_rotation_t rotation) {
    return SAT75_OLED_ROTATION;
}

bool oled_task_kb(void) {
    if (!oled_task_user()) {
        return false;
    }
    if (!oled_task_needs_to_repaint()) {
        return false;
    }
    oled_clear();
    if (clock_set_mode) {
        draw_clock();
        return false;
    }
    switch (oled_mode) {
        default:
        case OLED_DEFAULT:
            draw_default();
            break;
#    ifdef SAT75_OLED_SMALL
        case OLED_TIME:
            draw_clock();
            break;
#    endif
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
    return false;
}

// Request a repaint of the OLED image without resetting the OLED sleep timer.
// Used for things like clock updates that should not keep the OLED turned on
// if there is no other activity.
void oled_request_repaint(void) {
    if (is_oled_on()) {
        oled_repaint_requested = true;
    }
}

// Request a repaint of the OLED image and reset the OLED sleep timer.
// Needs to be called after any activity that should keep the OLED turned on.
void oled_request_wakeup(void) {
    oled_wakeup_requested = true;
}

// Check whether oled_task_user() needs to repaint the OLED image.  This
// function should be called at the start of oled_task_user(); it also handles
// the OLED sleep timer and the OLED_OFF mode.
bool oled_task_needs_to_repaint(void) {
    // In the OLED_OFF mode the OLED is kept turned off; any wakeup requests
    // are ignored.
    if ((oled_mode == OLED_OFF) && !clock_set_mode) {
        oled_wakeup_requested  = false;
        oled_repaint_requested = false;
        oled_off();
        return false;
    }

    // If OLED wakeup was requested, reset the sleep timer and do a repaint.
    if (oled_wakeup_requested) {
        oled_wakeup_requested  = false;
        oled_repaint_requested = false;
        oled_sleep_timer       = timer_read32() + CUSTOM_OLED_TIMEOUT;
        oled_on();
        return true;
    }

    // If OLED repaint was requested, just do a repaint without touching the
    // sleep timer.
    if (oled_repaint_requested) {
        oled_repaint_requested = false;
        return true;
    }

    // If the OLED is currently off, skip the repaint (which would turn the
    // OLED on if the image is changed in any way).
    if (!is_oled_on()) {
        return false;
    }

    // If the sleep timer has expired while the OLED was on, turn the OLED off.
    if (timer_expired32(timer_read32(), oled_sleep_timer)) {
        oled_off();
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
    for (uint8_t i = 0; i < len; i++) {
        oled_write_pixel(i + x, y, true);
    }
}

void s75_draw_line_v(uint8_t x, uint8_t y, uint8_t len) {
    for (uint8_t i = 0; i < len; i++) {
        oled_write_pixel(x, i + y, true);
    }
}

void s75_fill_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h) {
    for (uint8_t iy = 0; iy < h; iy++) {
        for (uint8_t ix = 0; ix < w; ix++) {
            oled_write_pixel(x + ix, y + iy, true);
        }
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
    oled_write_P(PSTR("S"), mod_state & MOD_MASK_SHIFT);
    oled_advance_char();
    oled_write_P(PSTR("C"), mod_state & MOD_MASK_CTRL);
    oled_advance_char();
    oled_write_P(PSTR("A"), mod_state & MOD_MASK_ALT);
    oled_advance_char();
    oled_write_P(PSTR("G"), mod_state & MOD_MASK_GUI);
    oled_advance_char();
}

// ---------------------------------------------------------------------
// Screens
// ---------------------------------------------------------------------

#    ifdef SAT75_OLED_SMALL

/* Matrix display is 12 x 12 pixels */
#        define MATRIX_DISPLAY_X 0
#        define MATRIX_DISPLAY_Y 18

void draw_default(void) {
    oled_write_P(PSTR("LAYER "), false);
    oled_write_char(get_highest_layer(layer_state) + 0x30, true);

    oled_write_P(PSTR(" ENC "), false);
    oled_write(s75_get_enc_mode(), true);

    led_t led_state = host_keyboard_led_state();
    oled_set_cursor(18, 0);
    oled_write_P(PSTR("CAP"), led_state.caps_lock);
    oled_set_cursor(18, 1);
    oled_write_P(PSTR("SCR"), led_state.scroll_lock);

    oled_set_cursor(6, 3);
    draw_mod_flags();

    oled_write(s75_get_time(), false);

    // matrix
    for (uint8_t x = 0; x < MATRIX_ROWS; x++) {
        for (uint8_t y = 0; y < MATRIX_COLS; y++) {
            bool on = (matrix_get_row(x) & (1 << y)) > 0;
            oled_write_pixel(MATRIX_DISPLAY_X + y + 2, MATRIX_DISPLAY_Y + x + 2, on);
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
    oled_set_cursor(0, 0);
    oled_write(s75_get_date(), false);
    oled_set_cursor(0, 2);
    oled_write(s75_get_time(), false);

    oled_set_cursor(12, 0);
    oled_write_P(PSTR(" ENC "), false);
    oled_write(s75_get_enc_mode(), true);

    oled_set_cursor(13, 1);
    oled_write_P(PSTR("LAYER "), false);
    oled_write_char(get_highest_layer(layer_state) + 0x30, true);

    led_t led_state = host_keyboard_led_state();
    oled_set_cursor(15, 3);
    oled_write_P(PSTR("CAPS"), led_state.caps_lock);

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

#        define CLK_DIGIT_W 22
#        define CLK_DIGIT_H 32
#        define CLK_THICK 4

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

// "LAYER n  ENC MMM     CAP", with the layer and encoder mode inverted.  The
// two vertical rules close the right hand edge of those inverted blocks, which
// the 6x8 font leaves open.
static void draw_status_row(void) {
    oled_write_P(PSTR("LAYER "), false);
    oled_write_char(get_highest_layer(layer_state) + 0x30, true);

    oled_write_P(PSTR(" ENC "), false);
    oled_write(s75_get_enc_mode(), true);

    oled_set_cursor(18, 0);
    oled_write_P(PSTR("CAP"), host_keyboard_led_state().caps_lock);

    s75_draw_line_v(35, 0, 8);
    s75_draw_line_v(71, 0, 8);
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
static uint8_t clock_origin_x(const char *digits) {
    uint8_t w = s75_7seg_width(digits, CLK_DIGIT_W, CLK_THICK);
#        ifdef SAT75_CLOCK_24H
    uint8_t avail = SAT75_CANVAS_WIDTH;
#        else
    uint8_t avail = SAT75_CANVAS_WIDTH - 14;
#        endif
    return w >= avail ? 0 : (uint8_t)((avail - w) / 2);
}

static void draw_big_clock(uint8_t y) {
    char digits[8];
    build_clock_digits(digits, sizeof(digits));

    s75_draw_7seg(digits, clock_origin_x(digits), y, CLK_DIGIT_W, CLK_DIGIT_H, CLK_THICK);

#        ifndef SAT75_CLOCK_24H
    // Sits on the same baseline as the bottom of the digits.
    oled_set_cursor(19, (uint8_t)((y + CLK_DIGIT_H - 8) / 8));
    oled_write(clock_is_pm() ? "pm" : "am", false);
#        endif
}

// "MON 2026-08-31".  The RTC's own dayofweek field is not maintained by
// pre_encoder_mode_change(), so the day is derived from the date instead.
static void draw_date_text(void) {
    static const char *const days[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};

    int16_t year  = last_timespec.year + 1980;
    uint8_t month = last_timespec.month;
    uint8_t day   = last_timespec.day;
    if (encoder_mode == ENC_MODE_CLOCK_SET) {
        year  = year_config + 1980;
        month = month_config;
        day   = day_config;
    }

    oled_write(days[s75_day_of_week(year, month, day)], false);
    oled_write_P(PSTR(" "), false);
    oled_write(s75_get_date(), false);
}

void draw_default(void) {
    led_t led_state = host_keyboard_led_state();

    draw_status_row();
    draw_big_clock(8);

    oled_set_cursor(0, 5);
    draw_date_text();
    oled_set_cursor(14, 5);
    oled_write_P(PSTR("SCR"), led_state.scroll_lock);
    oled_set_cursor(18, 5);
    oled_write_P(PSTR("NUM"), led_state.num_lock);

    draw_matrix_display(0, 48);

    oled_set_cursor(6, 6);
    draw_mod_flags();

#        ifdef WPM_ENABLE
    {
        static char wpm_str[9];
        snprintf(wpm_str, sizeof(wpm_str), "WPM %03u", (unsigned)get_current_wpm());
        oled_set_cursor(14, 6);
        oled_write(wpm_str, false);
    }
#        endif

#        ifdef SAT75_POMODORO
    oled_set_cursor(6, 7);
    draw_pomodoro_line();
#        endif
}

// Only reached via clock_set_mode on the large screen -- the default screen
// already shows the time, so this is purely the field editor.
void draw_clock(void) {
    char digits[8];
    build_clock_digits(digits, sizeof(digits));
    uint8_t x = clock_origin_x(digits);

    oled_write_P(PSTR("SET CLOCK"), false);
    oled_set_cursor(14, 0);
    oled_write_P(PSTR("ENC "), false);
    oled_write(s75_get_enc_mode(), true);
    s75_draw_line_v(125, 0, 8);

    draw_big_clock(10);

    oled_set_cursor(0, 6);
    draw_date_text();

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

#else // OLED_ENABLE

void oled_request_repaint(void) {}

void oled_request_wakeup(void) {}

#endif
