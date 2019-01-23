#include "satisfaction75.h"
#include "print.h"
#include "debug.h"
#include "micro_oled.h"

#include "ch.h"
#include "hal.h"

// #ifdef QWIIC_MICRO_OLED_ENABLE
#include "qwiic.h"

#include "timer.h"
/* Artificial delay added to get media keys to work in the encoder*/
#define MEDIA_KEY_DELAY 10
/* screen off after this many milliseconds */
#define ScreenOffInterval 60000 /* milliseconds */

static uint16_t last_flush;

volatile uint8_t led_numlock = false;
volatile uint8_t  led_capslock = false;
volatile uint8_t  led_scrolllock = false;

static uint8_t layer;
static bool queue_for_send = false;
static uint8_t encoder_value = 32;
static uint8_t encoder_mode = ENC_MODE_VOLUME;
static RTCDateTime last_timespec;
static uint16_t last_minute = 0;

static uint8_t time_config_idx = 0;
static uint8_t hour_config = 0;
static uint16_t minute_config = 0;

__attribute__ ((weak))
void draw_ui(void) {
  uint8_t hour = last_minute / 60;
  uint16_t minute = last_minute % 60;

  if(encoder_mode == ENC_MODE_CLOCK_SET){
    hour = hour_config;
    minute = minute_config;
  }

  bool is_pm = (hour / 12) > 0;
  hour = hour % 12;
  if (hour == 0){
    hour = 12;
  }
  char hour_str[2] = "";
  char min_str[2] = "";

  sprintf(hour_str, "%02d", hour);
  sprintf(min_str, "%02d", minute);


  clear_buffer();
  last_flush = timer_read();
  send_command(DISPLAYON);

/* Layer indicator is 41 x 10 pixels */
#define LAYER_INDICATOR_X 0
#define LAYER_INDICATOR_Y 0

  draw_string(LAYER_INDICATOR_X + 1, LAYER_INDICATOR_Y + 2, "LAYER", PIXEL_ON, NORM, 0);
  draw_rect_filled_soft(LAYER_INDICATOR_X + 32, LAYER_INDICATOR_Y + 1, 9, 9, PIXEL_ON, NORM);
  draw_char(LAYER_INDICATOR_X + 34, LAYER_INDICATOR_Y + 2, layer + 0x30, PIXEL_ON, XOR, 0);

#define ENCODER_INDICATOR_X 45
#define ENCODER_INDICATOR_Y 0
  draw_string(ENCODER_INDICATOR_X + 1, ENCODER_INDICATOR_Y + 2, "ENC", PIXEL_ON, NORM, 0);
  draw_rect_filled_soft(ENCODER_INDICATOR_X + 22, ENCODER_INDICATOR_Y + 1, 3 + (3 * 6), 9, PIXEL_ON, NORM);
  switch(encoder_mode){
    default:
    case ENC_MODE_VOLUME:
      draw_string(ENCODER_INDICATOR_X + 24, ENCODER_INDICATOR_Y + 2, "VOL", PIXEL_ON, XOR, 0);
      break;
    case ENC_MODE_MEDIA:
      draw_string(ENCODER_INDICATOR_X + 24, ENCODER_INDICATOR_Y + 2, "MED", PIXEL_ON, XOR, 0);
      break;
    case ENC_MODE_SCROLL:
      draw_string(ENCODER_INDICATOR_X + 24, ENCODER_INDICATOR_Y + 2, "SCR", PIXEL_ON, XOR, 0);
      break;
    case ENC_MODE_BRIGHTNESS:
      draw_string(ENCODER_INDICATOR_X + 24, ENCODER_INDICATOR_Y + 2, "BRT", PIXEL_ON, XOR, 0);
      break;
    case ENC_MODE_BACKLIGHT:
      draw_string(ENCODER_INDICATOR_X + 24, ENCODER_INDICATOR_Y + 2, "BKL", PIXEL_ON, XOR, 0);
      break;
    case ENC_MODE_CLOCK_SET:
      draw_string(ENCODER_INDICATOR_X + 24, ENCODER_INDICATOR_Y + 2, "CLK", PIXEL_ON, XOR, 0);
      break;
  }

/* Matrix display is 19 x 9 pixels */
#define MATRIX_DISPLAY_X 0
#define MATRIX_DISPLAY_Y 18

  for (uint8_t x = 0; x < MATRIX_ROWS; x++) {
    for (uint8_t y = 0; y < MATRIX_COLS; y++) {
      draw_pixel(MATRIX_DISPLAY_X + y + 2, MATRIX_DISPLAY_Y + x + 2,(matrix_get_row(x) & (1 << y)) > 0, NORM);
    }
  }
  draw_rect_soft(MATRIX_DISPLAY_X, MATRIX_DISPLAY_Y, 19, 9, PIXEL_ON, NORM);
  /* hadron oled location on thumbnail */
  draw_rect_filled_soft(MATRIX_DISPLAY_X + 14, MATRIX_DISPLAY_Y + 2, 3, 1, PIXEL_ON, NORM);
/*
  draw_rect_soft(0, 13, 64, 6, PIXEL_ON, NORM);
  draw_line_vert(encoder_value, 13, 6, PIXEL_ON, NORM);

*/

/* Mod display is 41 x 16 pixels */
#define MOD_DISPLAY_X 30
#define MOD_DISPLAY_Y 18

  uint8_t mods = get_mods();
  if (mods & MOD_LSFT) {
    draw_rect_filled_soft(MOD_DISPLAY_X + 0, MOD_DISPLAY_Y, 5 + (1 * 6), 11, PIXEL_ON, NORM);
    draw_string(MOD_DISPLAY_X + 3, MOD_DISPLAY_Y + 2, "S", PIXEL_OFF, NORM, 0);
  } else {
    draw_string(MOD_DISPLAY_X + 3, MOD_DISPLAY_Y + 2, "S", PIXEL_ON, NORM, 0);
  }
  if (mods & MOD_LCTL) {
    draw_rect_filled_soft(MOD_DISPLAY_X + 10, MOD_DISPLAY_Y, 5 + (1 * 6), 11, PIXEL_ON, NORM);
    draw_string(MOD_DISPLAY_X + 13, MOD_DISPLAY_Y + 2, "C", PIXEL_OFF, NORM, 0);
  } else {
    draw_string(MOD_DISPLAY_X + 13, MOD_DISPLAY_Y + 2, "C", PIXEL_ON, NORM, 0);
  }
  if (mods & MOD_LALT) {
    draw_rect_filled_soft(MOD_DISPLAY_X + 20, MOD_DISPLAY_Y, 5 + (1 * 6), 11, PIXEL_ON, NORM);
    draw_string(MOD_DISPLAY_X + 23, MOD_DISPLAY_Y + 2, "A", PIXEL_OFF, NORM, 0);
  } else {
    draw_string(MOD_DISPLAY_X + 23, MOD_DISPLAY_Y + 2, "A", PIXEL_ON, NORM, 0);
  }
  if (mods & MOD_LGUI) {
    draw_rect_filled_soft(MOD_DISPLAY_X + 30, MOD_DISPLAY_Y, 5 + (1 * 6), 11, PIXEL_ON, NORM);
    draw_string(MOD_DISPLAY_X + 33, MOD_DISPLAY_Y + 2, "G", PIXEL_OFF, NORM, 0);
  } else {
    draw_string(MOD_DISPLAY_X + 33, MOD_DISPLAY_Y + 2, "G", PIXEL_ON, NORM, 0);
  }

/* Lock display is 23 x 21 */
#define LOCK_DISPLAY_X 100
#define LOCK_DISPLAY_Y 0

  if (led_capslock == true) {
    draw_rect_filled_soft(LOCK_DISPLAY_X + 0, LOCK_DISPLAY_Y, 5 + (3 * 6), 9, PIXEL_ON, NORM);
    draw_string(LOCK_DISPLAY_X + 3, LOCK_DISPLAY_Y +1, "CAP", PIXEL_OFF, NORM, 0);
  } else if (led_capslock == false) {
    draw_string(LOCK_DISPLAY_X + 3, LOCK_DISPLAY_Y +1, "CAP", PIXEL_ON, NORM, 0);
  }

  if (led_scrolllock == true) {
    draw_rect_filled_soft(LOCK_DISPLAY_X + 0, LOCK_DISPLAY_Y + 11, 5 + (3 * 6), 9, PIXEL_ON, NORM);
    draw_string(LOCK_DISPLAY_X + 3, LOCK_DISPLAY_Y + 11 +1, "SCR", PIXEL_OFF, NORM, 0);
  } else if (led_scrolllock == false) {
    draw_string(LOCK_DISPLAY_X + 3, LOCK_DISPLAY_Y + 11 +1, "SCR", PIXEL_ON, NORM, 0);
  }

#define TIME_DISPLAY_X 82
#define TIME_DISPLAY_Y 22
  draw_string(TIME_DISPLAY_X, TIME_DISPLAY_Y, hour_str, PIXEL_ON, NORM, 0);
  draw_string(TIME_DISPLAY_X + 11, TIME_DISPLAY_Y, ":", PIXEL_ON, NORM, 0);
  draw_string(TIME_DISPLAY_X + 15, TIME_DISPLAY_Y, min_str, PIXEL_ON, NORM, 0);
  if(is_pm){
    draw_string(TIME_DISPLAY_X + 27, TIME_DISPLAY_Y, "pm", PIXEL_ON, NORM, 0);
  } else{
    draw_string(TIME_DISPLAY_X + 27, TIME_DISPLAY_Y, "am", PIXEL_ON, NORM, 0);
  }

  send_buffer();
}


void read_host_led_state(void) {
  uint8_t leds = host_keyboard_leds();
  if (leds & (1 << USB_LED_NUM_LOCK))    {
    if (led_numlock == false){
    led_numlock = true;}
    } else {
    if (led_numlock == true){
    led_numlock = false;}
    }
  if (leds & (1 << USB_LED_CAPS_LOCK))   {
    if (led_capslock == false){
    led_capslock = true;}
    } else {
    if (led_capslock == true){
    led_capslock = false;}
    }
  if (leds & (1 << USB_LED_SCROLL_LOCK)) {
    if (led_scrolllock == false){
    led_scrolllock = true;}
    } else {
    if (led_scrolllock == true){
    led_scrolllock = false;}
    }
}

uint32_t layer_state_set_kb(uint32_t state) {
  state = layer_state_set_user(state);
  layer = biton32(state);
  queue_for_send = true;
  return state;
}

bool process_record_kb(uint16_t keycode, keyrecord_t *record) {
  queue_for_send = true;
  switch (keycode) {
    case ENC_MOD:
      if (record->event.pressed) {
        if(encoder_mode == ENC_MODE_CLOCK_SET){
          RTCDateTime timespec;
          timespec.year = last_timespec.year;
          timespec.month = last_timespec.month;
          timespec.day = last_timespec.day;
          timespec.dayofweek = last_timespec.dayofweek;
          timespec.dstflag = last_timespec.dstflag;
          timespec.millisecond = (hour_config * 60 + minute_config) * 60 * 1000;
          rtcSetTime(&RTCD1, &timespec);
        }
        encoder_mode = (encoder_mode + 1) % _NUM_ENCODER_MODES;
        if(encoder_mode == ENC_MODE_CLOCK_SET){
          hour_config = (last_minute / 60);
          minute_config = last_minute % 60;
          time_config_idx = 0;
        }
      }
      return false;
    case ENC_PRESS:
      if (record->event.pressed) {
        uint16_t mapped_code = 0;
        switch(encoder_mode){
          case ENC_MODE_VOLUME:
            mapped_code = KC_MUTE;
            break;
          case ENC_MODE_MEDIA:
            mapped_code = KC_MEDIA_PLAY_PAUSE;
            break;
          case ENC_MODE_SCROLL:
            mapped_code = KC_BTN3;
            break;
          case ENC_MODE_BACKLIGHT:
            mapped_code = BL_TOGG;
            break;
          case ENC_MODE_CLOCK_SET:
            time_config_idx = (time_config_idx + 1) % 2;
          default:
          case ENC_MODE_BRIGHTNESS:
            break;
        }
        uint16_t held_keycode_timer = timer_read();
        if(mapped_code != 0){
          register_code(mapped_code);
          while (timer_elapsed(held_keycode_timer) < MEDIA_KEY_DELAY){ /* no-op */ }
          unregister_code(mapped_code);
        }
      } else {
        // Do something else when release
      }
      return false;
    default:
      break;
  }

  return process_record_user(keycode, record);
}

void encoder_update_kb(uint8_t index, bool clockwise) {
  encoder_value = (encoder_value + (clockwise ? 1 : -1)) % 64;
  queue_for_send = true;
  if (index == 0) {
    uint16_t mapped_code = 0;
    if (clockwise) {
      switch(encoder_mode){
          default:
          case ENC_MODE_VOLUME:
            mapped_code = KC_VOLU;
            break;
          case ENC_MODE_MEDIA:
            mapped_code = KC_MEDIA_NEXT_TRACK;
            break;
          case ENC_MODE_SCROLL:
            mapped_code = KC_WH_D;
            break;
          case ENC_MODE_BACKLIGHT:
            mapped_code = BL_INC;
            break;
          case ENC_MODE_BRIGHTNESS:
            mapped_code = KC_BRIGHTNESS_UP;
            break;
          case ENC_MODE_CLOCK_SET:
            if(time_config_idx == 0){
              hour_config = (hour_config + 1) % 24;
            }else{
              minute_config = (minute_config + 1) % 60;
            }
            queue_for_send = true;
            break;
      }
    } else {
      switch(encoder_mode){
          default:
          case ENC_MODE_VOLUME:
            mapped_code = KC_VOLD;
            break;
          case ENC_MODE_MEDIA:
            mapped_code = KC_MEDIA_PREV_TRACK;
            break;
          case ENC_MODE_SCROLL:
            mapped_code = KC_WH_U;
            break;
          case ENC_MODE_BACKLIGHT:
            mapped_code = BL_DEC;
            break;
          case ENC_MODE_BRIGHTNESS:
            mapped_code = KC_BRIGHTNESS_DOWN;
            break;
          case ENC_MODE_CLOCK_SET:
            if(time_config_idx == 0){
              hour_config = (hour_config - 1) % 24;
            }else{
              minute_config = (minute_config - 1) % 60;
            }
            queue_for_send = true;
            break;

      }
    }
    uint16_t held_keycode_timer = timer_read();
    if(mapped_code != 0){
      register_code(mapped_code);
      while (timer_elapsed(held_keycode_timer) < MEDIA_KEY_DELAY){ /* no-op */ }
      unregister_code(mapped_code);
    }
  }
}

void matrix_init_user(void) {
  rtcGetTime(&RTCD1, &last_timespec);
  queue_for_send = true;
}

void matrix_scan_user(void) {
  rtcGetTime(&RTCD1, &last_timespec);
  uint16_t minutes_since_midnight = last_timespec.millisecond / 1000 / 60;

  if (minutes_since_midnight != last_minute){
    last_minute = minutes_since_midnight;
    if(timer_elapsed(last_flush) <= ScreenOffInterval){
      queue_for_send = true;
    }

    printf(
      "%d-%d-%d T %d:%d\n",
      last_timespec.year + 1980,
      last_timespec.month,
      last_timespec.day,
      minutes_since_midnight / 60,
      minutes_since_midnight % 60
    );
  }

  if (queue_for_send) {
    read_host_led_state();
    draw_ui();
    queue_for_send = false;
  }
  if (timer_elapsed(last_flush) > ScreenOffInterval) {
    send_command(DISPLAYOFF);      /* 0xAE */
  }
}

