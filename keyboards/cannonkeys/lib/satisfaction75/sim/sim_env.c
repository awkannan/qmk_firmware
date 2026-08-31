// Host implementation of the QMK keyboard-state surface, plus a fake systick
// and a fake RTC that advance together so pomodoro phases can be fast-forwarded.
#include "sim_env.h"
#include <string.h>

RTCDriver     RTCD1 = 0;
layer_state_t layer_state = 1;

static uint8_t  s_mods;
static led_t    s_leds;
static uint8_t  s_wpm;
static uint16_t s_matrix[MATRIX_ROWS];
static uint32_t s_now_ms;       // fake systick
static RTCDateTime s_rtc;

uint8_t get_highest_layer(layer_state_t state) {
    uint8_t l = 0;
    while (state > 1) { state >>= 1; l++; }
    return l;
}

uint8_t  get_mods(void) { return s_mods; }
led_t    host_keyboard_led_state(void) { return s_leds; }
uint16_t matrix_get_row(uint8_t row) { return row < MATRIX_ROWS ? s_matrix[row] : 0; }
uint8_t  get_current_wpm(void) { return s_wpm; }

uint32_t timer_read32(void) { return s_now_ms; }
uint16_t timer_read(void) { return (uint16_t)s_now_ms; }
uint32_t timer_elapsed32(uint32_t last) { return s_now_ms - last; }
uint16_t timer_elapsed(uint16_t last) { return (uint16_t)s_now_ms - last; }
bool     timer_expired32(uint32_t current, uint32_t future) { return (int32_t)(current - future) >= 0; }

void rtcGetTime(RTCDriver *rtcp, RTCDateTime *t) { (void)rtcp; *t = s_rtc; }
void rtcSetTime(RTCDriver *rtcp, const RTCDateTime *t) { (void)rtcp; s_rtc = *t; }

void sim_set_layer(uint8_t layer) { layer_state = (layer_state_t)1 << layer; }
void sim_set_mods(uint8_t mods) { s_mods = mods; }
void sim_set_leds(bool num, bool caps, bool scroll) {
    s_leds.raw = 0;
    s_leds.num_lock = num;
    s_leds.caps_lock = caps;
    s_leds.scroll_lock = scroll;
}
void sim_set_wpm(uint8_t wpm) { s_wpm = wpm; }

void sim_press(uint8_t row, uint8_t col) {
    if (row < MATRIX_ROWS && col < MATRIX_COLS) s_matrix[row] |= (uint16_t)1 << col;
}
void sim_release_all(void) { memset(s_matrix, 0, sizeof(s_matrix)); }

void sim_set_clock(uint8_t year_off, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second) {
    s_rtc.year = year_off;
    s_rtc.month = month;
    s_rtc.day = day;
    s_rtc.dayofweek = 1;
    s_rtc.dstflag = 0;
    s_rtc.millisecond = ((uint32_t)hour * 3600u + (uint32_t)minute * 60u + second) * 1000u;
}

// Days in month, ignoring the year for February unless it is a leap year.
static uint8_t days_in_month(uint16_t year, uint8_t month) {
    static const uint8_t d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) return 29;
    return d[month - 1];
}

void sim_advance_ms(uint32_t ms) {
    s_now_ms += ms;

    uint32_t t = s_rtc.millisecond + ms;
    while (t >= 86400000u) {
        t -= 86400000u;
        uint8_t day = s_rtc.day + 1;
        if (day > days_in_month(1980 + s_rtc.year, s_rtc.month)) {
            day = 1;
            if (++s_rtc.month > 12) { s_rtc.month = 1; s_rtc.year++; }
        }
        s_rtc.day = day;
    }
    s_rtc.millisecond = t;
}

void sim_reset(void) {
    layer_state = 1;
    s_mods = 0;
    s_leds.raw = 0;
    s_wpm = 0;
    memset(s_matrix, 0, sizeof(s_matrix));
    s_now_ms = 100000;
    sim_set_clock(46, 8, 31, 13, 45, 0);   // 2026-08-31 13:45:00
}
