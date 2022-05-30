/*
Copyright 2015 Jun Wako <wakojun@gmail.com>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

/* USB Device descriptor parameter */
#define VENDOR_ID       0xCA04
#define PRODUCT_ID      0x000D
#define DEVICE_VER      0x0001
#define MANUFACTURER CannonKeys
#define PRODUCT BrutalV2_1800

/* key matrix size */
#define MATRIX_ROWS 6
#define MATRIX_COLS 18

#define MATRIX_COL_PINS { GP28, GP27, GP26, GP25, GP24, GP23, GP22, GP21, GP20, GP19, GP18, GP17, GP16, GP9, GP6, GP5, GP4, GP3 }
#define MATRIX_ROW_PINS { GP13, GP12, GP11, GP10, GP8, GP7 }
#define DIODE_DIRECTION COL2ROW

/* define if matrix has ghost */
//#define MATRIX_HAS_GHOST

/* Set 0 if debouncing isn't needed */
#define DEBOUNCE    5

/* Mechanical locking support. Use KC_LCAP, KC_LNUM or KC_LSCR instead in keymap */
#define LOCKING_SUPPORT_ENABLE
/* Locking resynchronize hack */
#define LOCKING_RESYNC_ENABLE

#define RP2040_BOOTLOADER_DOUBLE_TAP_RESET
#define RP2040_BOOTLOADER_DOUBLE_TAP_RESET_TIMEOUT 500U

#define LED_CAPS_LOCK_PIN GP29
#define LED_NUM_LOCK_PIN GP2
#define LED_PIN_ON_STATE 0

#define I2C1_SDA_PIN GP0
#define I2C1_SCL_PIN GP1
#define I2C_DRIVER I2CD1

#define EXTERNAL_EEPROM_BYTE_COUNT 2048
#define EXTERNAL_EEPROM_PAGE_SIZE 16

/* 3 bits are represented in the base address */
/* the other 8 bits are transferred over wire */
/* 11 bits total to address the 2048 bytes */

#define EXTERNAL_EEPROM_I2C_ADDRESS(loc) \
        (EXTERNAL_EEPROM_I2C_BASE_ADDRESS | ((((loc) >> 8) & 0x07) << 1))
#define EXTERNAL_EEPROM_ADDRESS_SIZE 1

#define DYNAMIC_KEYMAP_LAYER_COUNT 3

/*
 * Feature disable options
 *  These options are also useful to firmware size reduction.
 */

/* disable debug print */
//#define NO_DEBUG

/* disable print */
//#define NO_PRINT

/* disable action features */
//#define NO_ACTION_LAYER
//#define NO_ACTION_TAPPING
//#define NO_ACTION_ONESHOT
