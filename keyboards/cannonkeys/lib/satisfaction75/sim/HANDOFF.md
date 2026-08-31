# satisfaction75 large-OLED work — handoff

Branch: `202608_sat75_big_screen` (off `202508_sat75_big_bongo`).

Goal: make `keyboards/cannonkeys/lib/satisfaction75` drive both the original
128x32 OLED and the new 64x128 panel on `satisfaction75_big_hs`, rework the
bongo cat for both, and add a silent pomodoro timer.

**None of this has been compiled for ARM or flashed.** It was developed on a
box with no `qmk` CLI, no `arm-none-eabi-gcc`, and uninitialised submodules.
Everything below was verified with the host simulator in this directory, which
covers layout and logic but stops at the OLED driver boundary. Confirming it on
hardware is the next job.

## Set up

```
qmk git-submodule                     # lib/chibios et al are not initialised
qmk compile -kb cannonkeys/satisfaction75_big_hs -km default
qmk compile -kb cannonkeys/satisfaction75_big_hs -km bongo
qmk compile -kb cannonkeys/satisfaction75_hs     -km default   # must not regress
qmk compile -kb cannonkeys/satisfaction75        -km default
```

The small-screen boards matter as much as the big one — the branch this came
from had broken them, see "Rotation" below.

## Verify on hardware, in this order

1. **A small-screen board still looks exactly as it did.** This is the highest
   risk item; the sim says the output is byte-identical, but the sim does not
   exercise the driver's rotation path.
2. **The large panel renders upright and unmirrored.** The sim models the
   *post-rotation logical canvas*, so `rotate_90()`, `OLED_SOURCE_MAP` /
   `OLED_TARGET_MAP` and the SH1107 init sequence are all untested. If the
   large screen is scrambled, that is where to look, not in the layout code.
3. **Flash size.** Unmeasured. STM32F072 has 128K. Sprite data is 5120 bytes on
   large, 8192 on small.
4. **Pomodoro against a real RTC.** The sim's RTC is a hand-written fake, so it
   proves the arithmetic, not that ChibiOS populates `RTCDateTime` the way this
   code assumes. Check `pomodoro_clock()` reads sensibly, then leave a phase
   running for a real 25 minutes and compare against a phone.
5. **I2C throughput on the large panel.** 1024 bytes per frame instead of 512,
   plus a rotation pass per block. `oled_render()` runs from the main loop, not
   the 66ms task, so it should keep up, but the bongo is the thing that would
   show tearing first.

## What the simulator does and does not prove

Covered: glyph and pixel placement at both canvas sizes, cursor wrap and
clipping, mode dispatch through `oled_task_kb()`, the OLED sleep/wake logic,
the bongo and pomodoro state machines, calendar arithmetic, and that 26
combinations of `OLED_ENABLE` / `BONGO_ENABLE` / `POMODORO_ENABLE` / `WPM_ENABLE`
/ `SAT75_CLOCK_24H` / `POMODORO_*` compile clean.

Not covered: anything below `oled_write_*` — rotation, the SH1107 command
stream, I2C, timing under matrix-scan load, and flash usage.

`make test` diffs current output against `golden/`. `golden/baseline-small.txt`
is a frozen small-screen capture; the only line that should differ from it is
the `oled_init_kb()` assertion, which went from 1 to 0 when the rotation bug was
fixed. Any pixel difference there is a regression on shipped boards.

## Design decisions worth not re-litigating

**Rotation.** The parent branch set `oled_init_kb()` to return
`OLED_ROTATION_90` unconditionally. That is right for the 64x128 panel and
breaks every 128x32 board, because rotation 90 makes the logical canvas 32 wide
and 128 tall, so every cursor column and pixel coordinate lands wrong. It is
now `SAT75_OLED_ROTATION`, derived per panel.

**Both panels are 128 logical px wide.** Small is 128x32 (21x4 chars), large is
128x64 (21x8). That is why the split is cheap: same width, double the height.

**`SAT75_OLED_LARGE` is derived from `OLED_DISPLAY_64X128`**, the same macro the
OLED driver keys off, so the layout and the panel config cannot drift apart.

**The large screen has no `OLED_TIME` mode.** Its default screen carries a
seven-segment clock, so `draw_clock()` there is purely the `clock_set_mode`
field editor. This makes `enum oled_modes` differ by build — which was already
true of `BONGO_ENABLE` — hence the `_NUM_OLED_MODES` clamp in
`custom_config_load()`.

**Bongo art.** The two panels get different drawings; a 128x32 crop of the
large cat is not recognisable. Large `prep` aliases `idle[0]` because the
upstream source has no prep frame (its animation is WPM-driven with two
states). Attribution is in the header of `satisfaction_bongo.c` — j-inc,
pixelbenny, obosob, via j-dags' Kyria keymap.

**Pomodoro uses the RTC, not `timer_read32()`.** `STM32_SW` is a PLL off the
internal HSI RC — there is no HSE crystal on this board — spec'd around +/-1%,
so up to a quarter minute of error across a 25 minute phase. The RTC runs off
the 32.768kHz LSE at roughly +/-20ppm.

**Phases store an absolute deadline** built from date + time-of-day, so nothing
wraps at midnight and nothing accumulates drift. `pre_encoder_mode_change()`
calls `rtcSetTime()` whenever you leave clock-set mode, so a step over
`POMODORO_CLOCK_JUMP_SECONDS` or any backwards step is treated as the clock
moving rather than time passing, and slides the deadline instead.

**Pomodoro and bongo both require `OLED_ENABLE`.** Their only output is the
screen. `SAT75_POMODORO` and `SAT75_BONGO` are the derived macros every other
file tests.

## Open questions for the user

- `POMODORO_KEEP_OLED_AWAKE` defaults to **0**. Burn-in risk versus being able
  to see the countdown during a break, when you are not typing to wake it.
  Worth trying as-is first, then flipping if breaks feel blind.
- `WPM_ENABLE` is on for `satisfaction75_big_hs:default` so the default screen's
  WPM slot is populated. It was not previously enabled on that keymap.
- The small screen has not been given a pomodoro keymap binding; only
  `satisfaction75_big_hs` has `POMODORO_ENABLE`. The other boards can opt in
  with one define.

## File map

```
satisfaction_oled.h        canvas size defines, drawing + calendar API
satisfaction_oled.c        mode dispatch, primitives, 7-segment, both layouts
satisfaction_bongo.{c,h}   sprite data + animation state machine
satisfaction_pomodoro.{c,h} timer state machine + its two layouts
satisfaction_core.{c,h}    unchanged except rotation, EEPROM clamp, pomo hooks
sim/                       this harness
```

`bongo.h` is gone; it defined non-static globals and 8K of sprite data in a
header, so it could only ever be included once.
