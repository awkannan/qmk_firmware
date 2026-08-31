// Copyright 2023 Andrew Kannan
// SPDX-License-Identifier: GPL-2.0-or-later

#include "satisfaction_pomodoro.h"

#ifdef SAT75_POMODORO

#    include "satisfaction_core.h"
#    include "satisfaction_oled.h"
#    include "timer.h"
#    include "progmem.h"
#    include "oled_driver.h"
#    include <stdio.h>

#    define POMODORO_MINUTES(n) ((uint16_t)(n) * 60u)

static uint8_t  phase     = POMODORO_WORK;
static uint8_t  round_no  = 1; // 1..POMODORO_ROUNDS
static bool     running   = false;
static bool     pending   = false; // phase over, waiting to be acknowledged
static uint16_t remaining = POMODORO_MINUTES(POMODORO_WORK_MINUTES);
static uint32_t deadline  = 0;     // seconds since the epoch
static uint32_t last_tick = 0;     // pomodoro_clock() at the previous task run
static bool     clock_seeded = false;

/*
 * Seconds since 1970.  Built from the date as well as the time of day so the
 * count does not wrap at midnight -- a phase started at 23:50 has to survive
 * the rollover.  Good until 2106, well past the point where a uint32 of
 * seconds is this firmware's problem.
 */
static uint32_t pomodoro_clock(void) {
    uint32_t days = s75_days_from_civil(last_timespec.year + 1980, last_timespec.month, last_timespec.day);
    return days * 86400u + last_timespec.millisecond / 1000u;
}

static uint16_t phase_seconds(uint8_t p) {
    switch (p) {
        case POMODORO_SHORT_BREAK:
            return POMODORO_MINUTES(POMODORO_SHORT_BREAK_MINUTES);
        case POMODORO_LONG_BREAK:
            return POMODORO_MINUTES(POMODORO_LONG_BREAK_MINUTES);
        case POMODORO_WORK:
        default:
            return POMODORO_MINUTES(POMODORO_WORK_MINUTES);
    }
}

static void arm(void) {
    deadline = pomodoro_clock() + remaining;
}

// Move to the next phase in the cycle and reload its duration.
static void advance_phase(void) {
    if (phase == POMODORO_WORK) {
        phase = (round_no >= POMODORO_ROUNDS) ? POMODORO_LONG_BREAK : POMODORO_SHORT_BREAK;
    } else {
        if (phase == POMODORO_LONG_BREAK) {
            round_no = 1;
        } else if (round_no < POMODORO_ROUNDS) {
            round_no++;
        }
        phase = POMODORO_WORK;
    }

    remaining = phase_seconds(phase);

#    if POMODORO_AUTO_ADVANCE
    running = true;
    pending = false;
    arm();
#    else
    running = false;
    pending = true;
#    endif

    // The only notification a silent timer gets to give.
    oled_request_wakeup();
}

void pomodoro_toggle(void) {
    if (pending) {
        // Acknowledge the finished phase and start the one already queued up.
        pending = false;
        running = true;
        arm();
    } else if (running) {
        running = false; // remaining already tracks the countdown
    } else {
        running = true;
        arm();
    }
    oled_request_wakeup();
}

void pomodoro_reset(void) {
    running   = false;
    pending   = false;
    remaining = phase_seconds(phase);
    oled_request_wakeup();
}

void pomodoro_task(void) {
    uint32_t now = pomodoro_clock();

    // The RTC reads 1980 until it is set, and rtcSetTime() can move it
    // arbitrarily; seed on the first run so that never looks like a jump.
    if (!clock_seeded) {
        clock_seeded = true;
        last_tick    = now;
        return;
    }

    if (running) {
        uint32_t step = now - last_tick; // wraps cleanly if the clock went back

        if (step > POMODORO_CLOCK_JUMP_SECONDS) {
            // Not elapsed time -- the wall clock moved.  Slide the deadline by
            // the same amount so the phase keeps its remaining time.
            deadline += step;
        }

        uint16_t left;
        if (now >= deadline) {
            left = 0;
        } else {
            uint32_t delta = deadline - now;
            left           = (delta > remaining) ? remaining : (uint16_t)delta;
        }

        if (left == 0) {
            last_tick = now;
            advance_phase();
            return;
        }

        if (left != remaining) {
            remaining = left;
            oled_request_repaint();
#    if POMODORO_KEEP_OLED_AWAKE
            if (oled_mode == OLED_POMODORO) {
                oled_request_wakeup();
            }
#    endif
        }
    }

    last_tick = now;
}

bool     pomodoro_is_running(void) { return running; }
bool     pomodoro_is_pending(void) { return pending; }
uint8_t  pomodoro_get_phase(void) { return phase; }
uint8_t  pomodoro_get_round(void) { return round_no; }
uint16_t pomodoro_remaining(void) { return remaining; }
uint16_t pomodoro_phase_length(void) { return phase_seconds(phase); }

// ---------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------

static const char *phase_name(bool shortform) {
    switch (phase) {
        case POMODORO_SHORT_BREAK:
            return shortform ? "BRK" : "SHORT BREAK";
        case POMODORO_LONG_BREAK:
            return shortform ? "LBRK" : "LONG BREAK";
        case POMODORO_WORK:
        default:
            return shortform ? "WORK" : "WORK";
    }
}

static void countdown_digits(char *buf, size_t len) {
    snprintf(buf, len, "%02u:%02u", (unsigned)(remaining / 60), (unsigned)(remaining % 60));
}

// A finished phase blinks until it is acknowledged.
static bool blink_visible(void) {
    return !pending || ((timer_read32() / 500u) % 2u) == 0u;
}

// Elapsed fraction of the phase, as a filled bar.
static void draw_progress_bar(uint8_t x, uint8_t y, uint8_t w, uint8_t h) {
    uint16_t len     = pomodoro_phase_length();
    uint16_t elapsed = (len > remaining) ? (uint16_t)(len - remaining) : 0;
    uint8_t  filled  = len ? (uint8_t)(((uint32_t)elapsed * (w - 2)) / len) : 0;

    s75_draw_line_h(x, y, w);
    s75_draw_line_h(x, y + h - 1, w);
    s75_draw_line_v(x, y, h);
    s75_draw_line_v(x + w - 1, y, h);

    if (filled) {
        s75_fill_rect(x + 1, y + 1, filled, h - 2);
    }
}

// "WORK 24:59 3/4" -- the glance line on the large default screen, so the
// timer never needs the screen switched to be read.
void draw_pomodoro_line(void) {
    char buf[22];

    if (!running && !pending && remaining == pomodoro_phase_length()) {
        return; // never started; leave the row clean
    }

    snprintf(buf, sizeof(buf), "%s %02u:%02u %u/%u", running || pending ? phase_name(true) : "PAUS",
             (unsigned)(remaining / 60), (unsigned)(remaining % 60), (unsigned)round_no, (unsigned)POMODORO_ROUNDS);

    if (blink_visible()) {
        oled_write(buf, pending);
    }
}

#        ifdef SAT75_OLED_LARGE
#            define POMO_DIGIT_W 22
#            define POMO_DIGIT_H 32
#            define POMO_THICK 4
#        else
#            define POMO_DIGIT_W 18
#            define POMO_DIGIT_H 21
#            define POMO_THICK 3
#        endif

void draw_pomodoro(void) {
    char digits[8];
    char header[22];

    countdown_digits(digits, sizeof(digits));

#        ifdef SAT75_OLED_LARGE
    oled_set_cursor(0, 0);
    oled_write_P(PSTR("POMODORO"), false);
    snprintf(header, sizeof(header), "%u/%u", (unsigned)round_no, (unsigned)POMODORO_ROUNDS);
    oled_set_cursor(18, 0);
    oled_write(header, false);

    if (blink_visible()) {
        uint8_t w = s75_7seg_width(digits, POMO_DIGIT_W, POMO_THICK);
        s75_draw_7seg(digits, (uint8_t)((SAT75_CANVAS_WIDTH - w) / 2), 10, POMO_DIGIT_W, POMO_DIGIT_H, POMO_THICK);
    }

    // Phase name, centred, inverted while it waits to be acknowledged.
    {
        const char *name = phase_name(false);
        uint8_t     len  = 0;
        while (name[len]) {
            len++;
        }
        oled_set_cursor((uint8_t)((SAT75_TEXT_COLS - len) / 2), 6);
        oled_write(name, pending);
    }

    if (!running && !pending) {
        oled_set_cursor(0, 6);
        oled_write_P(PSTR("PAUSED"), false);
    }

    draw_progress_bar(0, 58, SAT75_CANVAS_WIDTH, 6);
#        else
    snprintf(header, sizeof(header), "%s %u/%u", phase_name(true), (unsigned)round_no, (unsigned)POMODORO_ROUNDS);
    oled_set_cursor(0, 0);
    oled_write(header, pending);

    if (!running && !pending) {
        oled_set_cursor(15, 0);
        oled_write_P(PSTR("PAUSED"), false);
    }

    if (blink_visible()) {
        uint8_t w = s75_7seg_width(digits, POMO_DIGIT_W, POMO_THICK);
        s75_draw_7seg(digits, (uint8_t)((SAT75_CANVAS_WIDTH - w) / 2), 8, POMO_DIGIT_W, POMO_DIGIT_H, POMO_THICK);
    }

    draw_progress_bar(0, 29, SAT75_CANVAS_WIDTH, 3);
#        endif
}

#endif // SAT75_POMODORO
