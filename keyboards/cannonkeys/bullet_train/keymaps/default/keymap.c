// Copyright 2025 Andrew Kannan
// SPDX-License-Identifier: GPL-2.0-or-later
#include QMK_KEYBOARD_H

#define _______ KC_TRNS

const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {

[0] = LAYOUT_all(
    KC_1, KC_2, KC_3, KC_4, KC_5, KC_6,
    KC_ESC,  KC_Q,    KC_W,    KC_E,    KC_R,    KC_T,    KC_Y,    KC_U,    KC_I,    KC_O,    KC_P,    KC_BSPC,
    KC_TAB,  KC_A,    KC_S,    KC_D,    KC_F,    KC_G,    KC_H,    KC_J,    KC_K,    KC_L,    KC_SCLN, LT(2,KC_ENT),
    KC_LSFT, KC_Z,    KC_X,    KC_C,    KC_V,    KC_B,    KC_N,    KC_M,    KC_COMM, KC_DOT,  KC_UP, KC_SLSH ,
    KC_LCTL, KC_LALT, KC_LGUI, KC_SPC,  MO(1),   KC_SPC,  KC_LEFT, KC_DOWN, KC_RGHT
),
[1] = LAYOUT_all(
    KC_MPLY, KC_MPRV, KC_MNXT, KC_MUTE, UG_TOGG, UG_NEXT,
    _______, KC_1,    KC_2,    KC_3,    KC_4,    KC_5,    KC_6,    KC_7,    KC_8,    KC_9,    KC_0,    KC_DEL,
    _______, KC_GRV,  KC_MINS, KC_QUOT, KC_PLUS, KC_BSLS, _______, _______, _______, _______, _______, _______,
    _______, _______, _______, _______, KC_LBRC, KC_RBRC, _______, _______, _______, _______, _______, _______ ,
    _______, _______, _______, _______, _______, _______, _______, _______, _______
),
[2] = LAYOUT_all(
    _______, _______, _______, _______, _______, _______,
    _______, KC_F1  , KC_F2,   KC_F3,   KC_F4,   _______, _______, _______, _______, _______, _______, _______,
    _______, KC_F5  , KC_F8,   KC_F7,   KC_F8,   _______, _______, _______, _______, _______, _______, _______,
    _______, KC_F9  , KC_F10,  KC_F11,  KC_F12,  _______, _______, _______, _______, _______, _______, _______ ,
    QK_BOOT, _______, _______, _______, _______, _______, _______, _______, _______
)
};

