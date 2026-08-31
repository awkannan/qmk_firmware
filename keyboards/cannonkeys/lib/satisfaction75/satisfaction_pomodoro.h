// Copyright 2023 Andrew Kannan
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdint.h>
#include <stdbool.h>

/*
 * The pomodoro's only output is the screen, so the feature needs one.  Every
 * other file tests SAT75_POMODORO rather than spelling out both conditions.
 */
#if defined(POMODORO_ENABLE) && defined(OLED_ENABLE)
#    define SAT75_POMODORO
#endif

#ifdef SAT75_POMODORO

/*
 * A silent pomodoro timer: no audio, no RGB, no host interaction.  The only
 * notification is the OLED waking at a phase boundary and the countdown
 * blinking until acknowledged.
 *
 * Timekeeping comes from the RTC rather than timer_read32().  The system clock
 * is a PLL off the internal HSI RC (this board has no HSE crystal), which is
 * specified around +/-1% and drifts with temperature -- up to a quarter minute
 * of error across a 25 minute phase.  The RTC runs off the 32.768kHz LSE
 * crystal at roughly +/-20ppm, which is under a second over the same span.
 */

#ifndef POMODORO_WORK_MINUTES
#    define POMODORO_WORK_MINUTES 25
#endif
#ifndef POMODORO_SHORT_BREAK_MINUTES
#    define POMODORO_SHORT_BREAK_MINUTES 5
#endif
#ifndef POMODORO_LONG_BREAK_MINUTES
#    define POMODORO_LONG_BREAK_MINUTES 15
#endif
#ifndef POMODORO_ROUNDS
#    define POMODORO_ROUNDS 4 // work phases before the long break
#endif

// Start the next phase automatically instead of waiting to be acknowledged.
#ifndef POMODORO_AUTO_ADVANCE
#    define POMODORO_AUTO_ADVANCE 0
#endif

/*
 * Hold the OLED awake for the whole of a running phase while its screen is
 * selected.  Off by default: 25 minutes of a near-static image is a real
 * burn-in risk on these panels, and any keypress wakes the screen anyway.
 * Phase boundaries wake the display regardless of this setting.
 */
#ifndef POMODORO_KEEP_OLED_AWAKE
#    define POMODORO_KEEP_OLED_AWAKE 0
#endif

/*
 * A jump larger than this (in seconds), or any backwards jump, is treated as
 * the wall clock being changed rather than as elapsed time, and slides the
 * deadline instead of burning down the phase.  Ordinary ticks are well under
 * a second, so this only ever catches rtcSetTime() and long stalls.
 */
#ifndef POMODORO_CLOCK_JUMP_SECONDS
#    define POMODORO_CLOCK_JUMP_SECONDS 60
#endif

enum pomodoro_phase {
    POMODORO_WORK,
    POMODORO_SHORT_BREAK,
    POMODORO_LONG_BREAK,
};

// Start, pause or resume.  When a finished phase is waiting to be
// acknowledged, this accepts it and starts the next one.
void pomodoro_toggle(void);

// Return the current phase to its full duration and pause it.
void pomodoro_reset(void);

// Advance the timer.  Call from housekeeping_task_kb().
void pomodoro_task(void);

bool     pomodoro_is_running(void);
bool     pomodoro_is_pending(void); // phase over, waiting to be acknowledged
uint8_t  pomodoro_get_phase(void);
uint8_t  pomodoro_get_round(void);   // 1..POMODORO_ROUNDS
uint16_t pomodoro_remaining(void);   // seconds
uint16_t pomodoro_phase_length(void); // seconds

void draw_pomodoro(void);      // full screen
void draw_pomodoro_line(void); // one text row, for the large default screen

#endif // SAT75_POMODORO
