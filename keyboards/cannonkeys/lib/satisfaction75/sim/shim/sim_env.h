// Host shim: the QMK keyboard-state surface the OLED code reads from.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "progmem.h"
#include "hal.h"

#define MATRIX_ROWS 6
#define MATRIX_COLS 15

typedef uint32_t layer_state_t;
extern layer_state_t layer_state;

typedef union {
    uint8_t raw;
    struct {
        bool num_lock : 1;
        bool caps_lock : 1;
        bool scroll_lock : 1;
        bool compose : 1;
        bool kana : 1;
    };
} led_t;

#define MOD_MASK_SHIFT 0x22
#define MOD_MASK_CTRL  0x11
#define MOD_MASK_ALT   0x44
#define MOD_MASK_GUI   0x88

uint8_t  get_highest_layer(layer_state_t state);
uint8_t  get_mods(void);
led_t    host_keyboard_led_state(void);
typedef uint16_t matrix_row_t;
uint16_t matrix_get_row(uint8_t row);
uint8_t  get_current_wpm(void);

uint32_t timer_read32(void);
uint16_t timer_read(void);
uint32_t timer_elapsed32(uint32_t last);
uint16_t timer_elapsed(uint16_t last);
bool     timer_expired32(uint32_t current, uint32_t future);

// --- sim controls -----------------------------------------------------
void sim_set_layer(uint8_t layer);
void sim_set_mods(uint8_t mods);
void sim_set_leds(bool num, bool caps, bool scroll);
void sim_set_wpm(uint8_t wpm);
void sim_press(uint8_t row, uint8_t col);
void sim_release_all(void);
void sim_set_clock(uint8_t year_off, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second);
void sim_advance_ms(uint32_t ms);   // advances both the fake systick and the fake RTC
void sim_reset(void);
