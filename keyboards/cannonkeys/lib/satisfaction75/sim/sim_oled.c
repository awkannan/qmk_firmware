// Host implementation of the QMK OLED buffer API.  The cursor/advance/clip
// rules below are transcribed from drivers/oled/oled_driver.c so that layout
// bugs reproduce here exactly as they would on hardware.
#include "oled_driver.h"
#include <stdio.h>
#include <string.h>

#include "glcdfont.c"

uint8_t  oled_buffer[SIM_MATRIX_SIZE];
static uint8_t *oled_cursor = oled_buffer;

// Dirty tracking follows the driver: oled_clear() marks everything, and each
// write marks the blocks it touched only if their bytes changed.
#define OLED_ALL_BLOCKS_MASK ((OLED_BLOCK_TYPE)~(OLED_BLOCK_TYPE)0)
OLED_BLOCK_TYPE oled_dirty = 0;
static uint8_t  sim_panel[SIM_MATRIX_SIZE];

static void mark_dirty(uint16_t index) {
    oled_dirty |= (OLED_BLOCK_TYPE)1 << (index / OLED_BLOCK_SIZE);
}
static bool     oled_active = true;

static const uint16_t oled_rotation_width = SIM_CANVAS_WIDTH;

bool oled_init(oled_rotation_t rotation) {
    (void)rotation;
    oled_clear();
    return true;
}

void oled_clear(void) {
    memset(oled_buffer, 0, sizeof(oled_buffer));
    oled_cursor = &oled_buffer[0];
    oled_dirty  = OLED_ALL_BLOCKS_MASK;
}

void oled_set_cursor(uint8_t col, uint8_t line) {
    uint16_t index = line * oled_rotation_width + col * OLED_FONT_WIDTH;
    if (index >= SIM_MATRIX_SIZE) {
        index = 0;
    }
    oled_cursor = &oled_buffer[index];
}

void oled_advance_char(void) {
    uint16_t nextIndex      = oled_cursor - &oled_buffer[0] + OLED_FONT_WIDTH;
    uint8_t  remainingSpace = oled_rotation_width - (nextIndex % oled_rotation_width);

    if (remainingSpace < OLED_FONT_WIDTH) {
        nextIndex += remainingSpace;
    }
    if (nextIndex >= SIM_MATRIX_SIZE) {
        nextIndex = 0;
    }
    oled_cursor = &oled_buffer[nextIndex];
}

void oled_advance_page(bool clearPageRemainder) {
    uint16_t index     = oled_cursor - &oled_buffer[0];
    uint8_t  remaining = oled_rotation_width - (index % oled_rotation_width);

    if (clearPageRemainder) {
        remaining = remaining / OLED_FONT_WIDTH;
        while (remaining--)
            oled_write_char(' ', false);
    } else {
        if (index + remaining >= SIM_MATRIX_SIZE) {
            index     = 0;
            remaining = 0;
        }
        oled_cursor = &oled_buffer[index + remaining];
    }
}

static void invert_character(uint8_t *cursor) {
    const uint8_t *end = cursor + OLED_FONT_WIDTH;
    while (cursor < end) {
        *cursor = ~(*cursor);
        cursor++;
    }
}

void oled_write_char(const char data, bool invert) {
    if (data == '\n') {
        oled_advance_page(true);
        return;
    }
    if (data == '\r') {
        oled_advance_page(false);
        return;
    }

    uint8_t before[OLED_FONT_WIDTH];
    memcpy(before, oled_cursor, OLED_FONT_WIDTH);

    uint8_t cast_data = (uint8_t)data;
    if (cast_data < OLED_FONT_START || cast_data > OLED_FONT_END) {
        memset(oled_cursor, 0x00, OLED_FONT_WIDTH);
    } else {
        memcpy(oled_cursor, &font[(cast_data - OLED_FONT_START) * OLED_FONT_WIDTH], OLED_FONT_WIDTH);
    }
    if (invert) {
        invert_character(oled_cursor);
    }
    if (memcmp(before, oled_cursor, OLED_FONT_WIDTH)) {
        uint16_t index = oled_cursor - &oled_buffer[0];
        mark_dirty(index);
        mark_dirty(index + OLED_FONT_WIDTH - 1);
    }
    oled_advance_char();
}

void oled_write(const char *data, bool invert) {
    while (*data)
        oled_write_char(*data++, invert);
}

void oled_write_ln(const char *data, bool invert) {
    oled_write(data, invert);
    oled_advance_page(true);
}

void oled_write_P(const char *data, bool invert) { oled_write(data, invert); }

void oled_write_raw(const char *data, uint16_t size) {
    uint16_t start = oled_cursor - &oled_buffer[0];
    if ((size + start) > SIM_MATRIX_SIZE) {
        size = SIM_MATRIX_SIZE - start;
    }
    for (uint16_t i = 0; i < size; i++) {
        if (oled_buffer[start + i] == (uint8_t)data[i]) continue;
        oled_buffer[start + i] = (uint8_t)data[i];
        mark_dirty(start + i);
    }
}

void oled_write_raw_P(const char *data, uint16_t size) { oled_write_raw(data, size); }

void oled_write_pixel(uint8_t x, uint8_t y, bool on) {
    if (x >= oled_rotation_width) {
        return;
    }
    uint16_t index = x + (y / 8) * oled_rotation_width;
    if (index >= SIM_MATRIX_SIZE) {
        return;
    }
    uint8_t data = oled_buffer[index];
    if (on) {
        data |= (1 << (y % 8));
    } else {
        data &= ~(1 << (y % 8));
    }
    if (oled_buffer[index] != data) {
        oled_buffer[index] = data;
        mark_dirty(index);
    }
}

uint8_t oled_max_chars(void) { return SIM_CANVAS_WIDTH / OLED_FONT_WIDTH; }
uint8_t oled_max_lines(void) { return SIM_CANVAS_HEIGHT / OLED_FONT_HEIGHT; }

bool oled_on(void) { oled_active = true; return true; }
bool oled_off(void) { oled_active = false; return true; }
bool is_oled_on(void) { return oled_active; }

oled_buffer_reader_t oled_read_raw(uint16_t start_index) {
    if (start_index > SIM_MATRIX_SIZE) start_index = SIM_MATRIX_SIZE;
    return (oled_buffer_reader_t){&oled_buffer[start_index], (uint16_t)(SIM_MATRIX_SIZE - start_index)};
}

uint8_t sim_oled_flush(uint8_t limit) {
    uint8_t sent = 0;
    for (uint8_t b = 0; b < OLED_BLOCK_COUNT && sent < limit; b++) {
        if (oled_dirty & ((OLED_BLOCK_TYPE)1 << b)) {
            memcpy(&sim_panel[b * OLED_BLOCK_SIZE], &oled_buffer[b * OLED_BLOCK_SIZE], OLED_BLOCK_SIZE);
            oled_dirty &= ~((OLED_BLOCK_TYPE)1 << b);
            sent++;
        }
    }
    return sent;
}

uint16_t sim_oled_stale_blocks(void) {
    uint16_t stale = 0;
    for (uint8_t b = 0; b < OLED_BLOCK_COUNT; b++) {
        if (memcmp(&sim_panel[b * OLED_BLOCK_SIZE], &oled_buffer[b * OLED_BLOCK_SIZE], OLED_BLOCK_SIZE)) {
            stale |= (uint16_t)1 << b;
        }
    }
    return stale;
}

void sim_oled_dump(const char *title) {
    printf("+- %s ", title);
    for (int i = 0; i < (int)(SIM_CANVAS_WIDTH - strlen(title) - 4); i++) putchar('-');
    printf("+\n");
    for (int y = 0; y < SIM_CANVAS_HEIGHT; y++) {
        putchar('|');
        for (int x = 0; x < SIM_CANVAS_WIDTH; x++) {
            putchar((oled_buffer[x + (y / 8) * SIM_CANVAS_WIDTH] >> (y % 8)) & 1 ? '#' : '.');
        }
        printf("|\n");
    }
    putchar('+');
    for (int i = 0; i < SIM_CANVAS_WIDTH; i++) putchar('-');
    printf("+\n\n");
}

void sim_oled_dump_pbm(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P1\n%d %d\n", SIM_CANVAS_WIDTH, SIM_CANVAS_HEIGHT);
    for (int y = 0; y < SIM_CANVAS_HEIGHT; y++) {
        for (int x = 0; x < SIM_CANVAS_WIDTH; x++) {
            fputc((oled_buffer[x + (y / 8) * SIM_CANVAS_WIDTH] >> (y % 8)) & 1 ? '1' : '0', f);
            fputc(x == SIM_CANVAS_WIDTH - 1 ? '\n' : ' ', f);
        }
    }
    fclose(f);
}
