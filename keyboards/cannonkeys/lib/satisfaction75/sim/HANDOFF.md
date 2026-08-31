# satisfaction75 large-OLED work — status

Branch: `202608_sat75_big_screen` (off `202508_sat75_big_bongo`).

Goal: make `keyboards/cannonkeys/lib/satisfaction75` drive both the original
128x32 OLED and the 64x128 panel on `satisfaction75_big_hs`, rework the bongo
cat for both, and add a silent pomodoro timer.

**Everything that can be checked without the physical keyboard has been.** All
eleven board/keymap combinations compile and link, the simulator's golden
snapshots pass at both canvas sizes, and the calendar arithmetic is
cross-checked against a reference. What is left needs hardware; see the bottom
of this file.

## Build environment (already set up on this machine)

```
pyenv virtualenv 3.11.11 qmk
pyenv local qmk                 # .python-version, gitignored
pip install -r requirements.txt qmk
qmk config user.qmk_home=<this repo>
qmk git-submodule
```

3.11 rather than the system 3.14: QMK's `hid`/`pyusb`/`milc` are much better
exercised there.

Toolchain, via apt:

```
gcc-arm-none-eabi binutils-arm-none-eabi libnewlib-arm-none-eabi
libstdc++-arm-none-eabi-newlib dfu-util
```

`libnewlib` and `libstdc++-...-newlib` are not optional -- without them ChibiOS
fails at link on missing standard-library pieces. `dfu-util` is needed even on a
machine that cannot flash: these boards set `DFU_SUFFIX_ARGS`, so the `.bin`
rule shells out to `dfu-suffix`, and the build exits non-zero without it. The
avr packages and `dfu-programmer` are not needed; `qmk doctor` will flag them
anyway.

## Build status

Eleven combinations, all clean. Five boards -- keymaps live partly at the
`satisfaction75/` parent level and are shared across its revisions.

| Board | Keymap | Flash /128K | RAM /16K |
|---|---|---|---|
| satisfaction75/prototype | default | 39240  29.9% | 10128  61.8% |
| satisfaction75/prototype | bongo   | 53300  40.7% | 10892  66.5% |
| satisfaction75/rev1      | default | 39240  29.9% | 10128  61.8% |
| satisfaction75/rev1      | bongo   | 53300  40.7% | 10892  66.5% |
| satisfaction75/rev1      | tester  | 32360  24.7% |  9520  58.1% |
| satisfaction75/rev2      | default | 39256  29.9% | 10128  61.8% |
| satisfaction75/rev2      | bongo   | 52936  40.4% | 10892  66.5% |
| satisfaction75_hs        | default | 38852  29.6% | 10400  63.5% |
| satisfaction75_hs        | bongo   | 52324  39.9% | 11164  68.1% |
| satisfaction75_big_hs    | default | 41452  31.6% | 11028  67.3% |
| satisfaction75_big_hs    | bongo   | 51560  39.3% | 11724  71.6% |

Flash is `.vectors + .text + .rodata + .ARM.exidx + .data`; RAM is
`.mstack + .pstack + .data + .bss`. Plain `arm-none-eabi-size` without `-A`
mis-groups ChibiOS's stack sections and reports RAM at 100%.

`_big_hs:bongo` is the worst case -- the only build with `BONGO_ENABLE`,
`POMODORO_ENABLE`, `WPM_ENABLE` and `VIA_ENABLE` together -- and it has ample
headroom.

## The simulator, and what it cannot see

`make run` prints every screen at both sizes; `make test` diffs against
`golden/`. It compiles the real `draw_*()` functions with QMK's real
`glcdfont.c`, over an OLED buffer whose cursor, advance and clipping rules are
transcribed from `drivers/oled/oled_driver.c`.

It caught real layout and state-machine problems, but the first ARM build still
found two bugs it structurally could not:

**Missing includes are invisible to it.** `shim/sim_env.h` declares the whole
keyboard-state surface in one catch-all header, so it is *more permissive* than
the real QMK headers. `satisfaction_oled.c` called `get_current_wpm()` with no
`#include "wpm.h"` and the sim was perfectly happy.

**Warning-level differences.** QMK builds ARM with `-Werror`; the sim does not,
and runs at `-O1`. `-Wformat-truncation` caught a `char[8]` that
`"%02u:%02u"` could overrun, because `minute_config` is a user-editable
`int16_t` that GCC cannot bound. Host gcc never flagged it.

So: a green `make test` means the layouts and logic are right. It says nothing
about includes, warnings, `rotate_90()`, the SH1107 command stream, I2C timing,
or flash usage. Run a real compile before believing a change is done.

## Design decisions worth not re-litigating

**Rotation.** The parent branch set `oled_init_kb()` to return
`OLED_ROTATION_90` unconditionally. That is right for the 64x128 panel and
breaks every 128x32 board, because rotation 90 makes the logical canvas 32 wide
and 128 tall, so every cursor column and pixel coordinate lands wrong. It is now
`SAT75_OLED_ROTATION`, derived per panel, and the sim asserts it.

**Both panels are 128 logical px wide.** Small is 128x32 (21x4 chars), large is
128x64 (21x8). That is why the split is cheap: same width, double the height.

**`SAT75_OLED_LARGE` is derived from `OLED_DISPLAY_64X128`**, the same macro the
OLED driver keys off, so the layout and the panel config cannot drift apart.

**The large screen has no `OLED_TIME` mode.** Its default screen carries a
seven-segment clock, so `draw_clock()` there is purely the `clock_set_mode`
field editor. This makes `enum oled_modes` differ by build -- already true of
`BONGO_ENABLE` -- hence the `_NUM_OLED_MODES` clamp in `custom_config_load()`.

**Bongo art.** The two panels get different drawings; a 128x32 crop of the large
cat is not recognisable. Large `prep` aliases `idle[0]` because the upstream
source has no prep frame (its animation is WPM-driven with two states).
Attribution is in the header of `satisfaction_bongo.c` -- j-inc, pixelbenny,
obosob, via j-dags' Kyria keymap.

**Pomodoro uses the RTC, not `timer_read32()`.** `STM32_SW` is a PLL off the
internal HSI RC -- there is no HSE crystal on this board -- spec'd around +/-1%,
so up to a quarter minute of error across a 25 minute phase. The RTC runs off
the 32.768kHz LSE at roughly +/-20ppm. Phases store an absolute deadline built
from date plus time of day, so nothing wraps at midnight, and a clock step over
`POMODORO_CLOCK_JUMP_SECONDS` or any backwards step slides the deadline instead
of burning down the phase (`pre_encoder_mode_change()` calls `rtcSetTime()`
every time clock-set mode is left).

**Pomodoro and bongo both require `OLED_ENABLE`.** Their only output is the
screen. `SAT75_POMODORO` and `SAT75_BONGO` are the derived macros every other
file tests.

## Settled defaults

Reviewed and agreed with the board owner. Changeable, but they are decisions
rather than leftovers -- do not re-open them without a reason.

**`POMODORO_KEEP_OLED_AWAKE` is 0.** Holding a near-static image for a whole
25 minute phase is a real burn-in risk on these panels, and typing keeps the
screen awake by itself during a work phase. The gap is breaks, when you are not
typing; phase boundaries wake it regardless. Flip to 1 if that proves annoying.

**`WPM_ENABLE` is on for `satisfaction75_big_hs:default`.** The default screen
has a WPM slot that would otherwise render empty.

**Only `satisfaction75_big_hs` sets `POMODORO_ENABLE`.** The small boards can
opt in with one define in a keymap config.h.

## What actually still needs hardware

Nothing else is blocking. In priority order:

1. **A small-screen board is visually unchanged.** Highest risk. The sim says
   the output is byte-identical to the pre-rework baseline, but it models the
   post-rotation canvas and so never exercises the driver's rotation path.
2. **The large panel renders upright and unmirrored.** `rotate_90()`,
   `OLED_SOURCE_MAP`/`OLED_TARGET_MAP` and the SH1107 init are all untested. If
   the big screen is scrambled, look there, not at the layout code.
3. **The bongo animates smoothly on the large panel.** 1024 bytes per frame
   instead of 512, plus a rotation pass per block. `oled_render()` runs from the
   main loop rather than the 66ms task so it should keep up, but tearing would
   show here first.
4. **The pomodoro against a real RTC.** The sim's RTC is a hand-written fake, so
   it proves the arithmetic, not that ChibiOS populates `RTCDateTime` the way
   this code assumes. Leave a phase running for a real 25 minutes and compare
   against a phone.
5. **VIA.** The new `pid 0x003B` needs a definition in VIA's own keyboards repo
   before the board is recognised and the new OLED mode is labelled.

## Not done

Rebasing onto upstream master. This branch is a long way behind, and `upstream`
is not configured as a remote -- only `origin` (the fork). Deliberately deferred.

`sim/` and this file are development scaffolding, not firmware. They would need
removing or relocating before anything goes near an upstream QMK PR.

## File map

```
satisfaction_oled.h         canvas size defines, drawing + calendar API
satisfaction_oled.c         mode dispatch, primitives, 7-segment, both layouts
satisfaction_bongo.{c,h}    sprite data + animation state machine
satisfaction_pomodoro.{c,h} timer state machine + its two layouts
satisfaction_core.{c,h}     unchanged except rotation, EEPROM clamp, pomo hooks
sim/                        host-side simulator
```

`bongo.h` is gone; it defined non-static globals and 8K of sprite data in a
header, so it could only ever be included once.
