// Drives the real draw_*() functions through real Quantum Painter, into a
// stand-in panel, and dumps each screen.  Build for both canvas sizes; see the
// Makefile.
#include <stdio.h>
#include <string.h>
#include "satisfaction_core.h"
#include "sim_env.h"
#include "sim_qp.h"
#ifdef POMODORO_ENABLE
#    include "satisfaction_pomodoro.h"
#endif

// --- globals normally owned by satisfaction_core.c --------------------
volatile uint8_t led_numlock, led_capslock, led_scrolllock;
uint8_t  layer;
bool     clock_set_mode = false;
uint8_t  oled_mode = OLED_DEFAULT;
bool     oled_repaint_requested = false;
bool     oled_wakeup_requested = false;
uint32_t oled_sleep_timer;
uint8_t  encoder_value = 32;
uint8_t  encoder_mode = ENC_MODE_VOLUME;
uint8_t  enabled_encoder_modes = 0x1F;
RTCDateTime last_timespec;
uint16_t last_minute = 0;
uint8_t  time_config_idx = 0;
int8_t   hour_config = 0;
int16_t  minute_config = 0;
int8_t   year_config = 0;
int8_t   month_config = 0;
int8_t   day_config = 0;
uint8_t  previous_encoder_mode = 0;

// Mirror housekeeping_task_kb()'s RTC snapshot so draw code sees a live clock.
static void sim_sync_rtc(void) {
    rtcGetTime(&RTCD1, &last_timespec);
    last_minute = last_timespec.millisecond / 1000 / 60;
}

/*
 * One repaint, then the main loop passes that push it to the panel, one
 * controller page each.  The sim builds with SAT75_OLED_REPAINT_INTERVAL=0, so
 * the first s75_display_task() call always draws; what remains is exactly
 * SAT75_PANEL_PAGES calls of pushing.  Time does not move, as it did not when
 * the old sim called oled_task_kb() directly.
 */
static void repaint(void) {
    sim_sync_rtc();
    oled_request_wakeup(); // as if the user just touched the board
    s75_display_task();
    for (int i = 0; i < SAT75_PANEL_PAGES; i++) {
        s75_display_task();
    }
}

static void report_sent(void) {
    uint32_t flushes, pixels;
    sim_panel_take_stats(&flushes, &pixels);
    printf("pages sent: %u/%u, %u px\n\n", (unsigned)flushes, (unsigned)SAT75_PANEL_PAGES, (unsigned)pixels);
}

static void frame(const char *title) {
    repaint();
    sim_panel_dump(title);
    report_sent();
}

/*
 * Only what changed reaches the panel.  Redrawing an unchanged screen must send
 * nothing; a single key press on the default screen should send only the pages
 * under the matrix activity box.
 */
static void bandwidth_scenario(void) {
    sim_reset();
    oled_mode = OLED_DEFAULT;
    repaint();
    report_sent();

    repaint();
    printf("unchanged screen redrawn: ");
    report_sent();

    sim_press(0, 0);
    repaint();
    printf("one key pressed: ");
    report_sent();
    sim_release_all();

    clock_set_mode = true;
    repaint();
    printf("switched to the clock screen: ");
    report_sent();
    clock_set_mode = false;
}

// Cross-checks s75_days_from_civil()/s75_day_of_week() against a reference.
static int dump_dates(void) {
    for (int16_t y = 1980; y <= 2100; y++) {
        for (uint8_t m = 1; m <= 12; m++) {
            for (uint8_t d = 1; d <= 28; d += 9) {
                printf("%04d-%02u-%02u %lu %u\n", y, m, d,
                       (unsigned long)s75_days_from_civil(y, m, d), s75_day_of_week(y, m, d));
            }
        }
    }
    return 0;
}

#ifdef BONGO_ENABLE
// Walks the idle -> tap -> prep -> idle state machine.  draw_bongo() advances
// the animation as a side effect of drawing, so each frame() below is one
// OLED repaint's worth of progress.
static void bongo_scenarios(void) {
    char t[48];

    sim_reset();
    sim_set_wpm(87);
    oled_mode = OLED_BONGO;

    for (int i = 0; i < 3; i++) {
        snprintf(t, sizeof(t), "BONGO idle frame %d", i);
        frame(t);
        sim_advance_ms(100);   // > BONGO_FRAME_MS, so the frame advances
    }

    // Each *new* keypress flips the paw; holding a key does not.
    sim_press(2, 5);
    frame("BONGO tap (key down)");
    sim_release_all();
    sim_advance_ms(80);
    sim_press(3, 7);
    frame("BONGO tap (next key)");

    // No new press: the cat hovers in prep until the idle timeout expires.
    sim_release_all();
    sim_advance_ms(80);
    frame("BONGO prep (between keystrokes)");

    sim_advance_ms(800);       // > BONGO_IDLE_TIMEOUT
    frame("BONGO idle (after timeout)");

    oled_mode = OLED_BONGO_MIN;
    frame("BONGO_MIN (art only, no overlay)");
}
#endif

#ifdef POMODORO_ENABLE
// housekeeping_task_kb() refreshes the RTC snapshot then ticks the timer, many
// times a second.  Stepping in chunks well under POMODORO_CLOCK_JUMP_SECONDS
// keeps that shape, so the jump detector is not tripped by the fast-forward.
static void pomo_advance(uint32_t ms) {
    while (ms) {
        uint32_t chunk = ms > 30000 ? 30000 : ms;
        sim_advance_ms(chunk);
        sim_sync_rtc();
        pomodoro_task();
        ms -= chunk;
    }
}

// Move the wall clock without any time actually passing, as CLOCK_SET does.
static void pomo_set_clock(uint8_t yr, uint8_t mo, uint8_t dy, uint8_t h, uint8_t mi) {
    sim_set_clock(yr, mo, dy, h, mi, 0);
    sim_sync_rtc();
    pomodoro_task();
}

static void pomodoro_scenarios(void) {
    sim_reset();
    sim_sync_rtc();
    pomodoro_task(); // seed the clock
    oled_mode = OLED_POMODORO;

    frame("POMO idle (never started)");

    pomodoro_toggle();
    pomo_advance(90u * 1000u);
    frame("POMO running, 90s in");

    pomodoro_toggle();
    frame("POMO paused");
    pomodoro_toggle();

    pomo_advance((uint32_t)(POMODORO_WORK_MINUTES * 60 - 90) * 1000u);
    frame("POMO phase over, awaiting ack (blink A)");
    sim_advance_ms(500);
    frame("POMO phase over, awaiting ack (blink B)");

    pomodoro_toggle(); // acknowledge -> the queued break starts
    pomo_advance(45u * 1000u);
    frame("POMO short break running");

    // --- clock robustness -------------------------------------------
    pomodoro_reset();
    pomodoro_toggle();
    pomo_advance(60u * 1000u);
    uint16_t before = pomodoro_remaining();

    pomo_set_clock(46, 8, 31, 3, 0); // yanked back ~11 hours
    printf("clock set backwards: remaining %u -> %u (expect unchanged)\n", before, pomodoro_remaining());

    pomo_set_clock(46, 8, 31, 21, 30); // and forwards
    printf("clock set forwards:  remaining %u -> %u (expect unchanged)\n", before, pomodoro_remaining());

    // --- midnight rollover ------------------------------------------
    pomodoro_reset();
    pomo_set_clock(46, 8, 31, 23, 58);
    uint16_t len = pomodoro_phase_length();
    pomodoro_toggle();
    pomo_advance(4u * 60u * 1000u); // midnight falls two minutes in
    printf("ran 4m across midnight: remaining %u (expect %u), RTC now %04d-%02u-%02u %02u:%02u\n",
           pomodoro_remaining(), len - 240, last_timespec.year + 1980, last_timespec.month,
           last_timespec.day, (unsigned)(last_minute / 60), (unsigned)(last_minute % 60));
    frame("POMO 4m in, having crossed midnight");

    // The timer is readable without leaving the default screen.
    oled_mode = OLED_DEFAULT;
    frame("DEFAULT with the pomodoro running");
}
#endif

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "--dates") == 0) return dump_dates();
#ifdef SAT75_OLED_LARGE
    const char *size = "LARGE 128x64";
#else
    const char *size = "SMALL 128x32";
#endif
    printf("################ satisfaction75 OLED sim -- %s ################\n\n", size);

    sim_reset();
    sim_sync_rtc();
    s75_display_init();
    printf("init, clearing the panel: ");
    report_sent();
    // The first task pass also runs the one-off re-init (sim time starts well
    // past 3s), which clears and redraws the whole panel again.
    repaint();
    printf("first repaint, after the re-init: ");
    report_sent();

    oled_mode = OLED_DEFAULT;
    frame("DEFAULT idle");

    sim_set_layer(1);
    sim_set_mods(MOD_MASK_SHIFT | MOD_MASK_GUI);
    sim_set_leds(false, true, true);
    encoder_mode = ENC_MODE_MEDIA;
    sim_press(0, 0); sim_press(2, 5); sim_press(4, 11); sim_press(5, 14);
    frame("DEFAULT layer1 + caps/scroll + mods + keys");

    sim_reset();
    encoder_mode = ENC_MODE_VOLUME;
    oled_mode = OLED_TIME;
    frame("TIME");
    oled_mode = OLED_DEFAULT;

    sim_set_leds(false, true, false);
    clock_set_mode = true;
    encoder_mode = ENC_MODE_CLOCK_SET;
    hour_config = 13; minute_config = 45;
    year_config = 46; month_config = 8; day_config = 31;
    const char *fields[] = {"hour", "minute", "year", "month", "day"};
    for (uint8_t i = 0; i < 5; i++) {
        char t[64];
        time_config_idx = i;
        snprintf(t, sizeof(t), "CLOCK_SET field=%s", fields[i]);
        frame(t);
    }
    clock_set_mode = false;
    encoder_mode   = ENC_MODE_VOLUME; // else get_time/get_date keep reading *_config

#ifdef BONGO_ENABLE
    bongo_scenarios();
#endif

#ifdef POMODORO_ENABLE
    pomodoro_scenarios();
#endif

    bandwidth_scenario();

    return 0;
}
