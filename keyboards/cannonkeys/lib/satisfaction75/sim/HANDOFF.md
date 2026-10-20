# satisfaction75 OLED work — status

Branch: `202610_sat75_quantum_painter` (off `202608_sat75_big_screen`, which is
off `202508_sat75_big_bongo`).

Goal of the parent branch: make `keyboards/cannonkeys/lib/satisfaction75`
drive both the original 128x32 OLED and the 64x128 panel on
`satisfaction75_big_hs`, rework the bongo cat for both, and add a silent
pomodoro timer.

Goal of this branch: the same screens, driven through **Quantum Painter**
instead of QMK's OLED driver. Every board in the family switches over; the
OLED-driver version stays on the parent branch.

**Everything that can be checked without the physical keyboard has been.** All
eleven board/keymap combinations compile and link under `-Werror`, and every
screen in the simulator is pixel-identical to the OLED-driver version at both
canvas sizes. What is left needs hardware; see the bottom of this file.

## The Quantum Painter port

**Drivers.** The 128x32 SSD1306 uses QP's `sh1106_i2c` driver (QP documents the
two as indistinguishable); the 64x128 SH1107 uses `sh1107_i2c`. A board selects
the large panel with `SAT75_OLED_LARGE` in config.h plus the `sh1107_i2c`
driver in rules.mk; `OLED_DISPLAY_64X128` is gone along with the OLED driver.

**Rotation is `QP_ROTATION_270` on the large panel, not 90.** Worked through
from both drivers' code: the old `OLED_ROTATION_90` path (`rotate_90()`, the
source/target maps, `calc_bounds_90()`) puts logical (x,y) at panel column y,
row 127-x. QP's `flush_rot270` does exactly that; `flush_rot90` would be
upside down. The SH1107 init sequences match (mux 63, display offset 32, COM
pins alt, segment remap inverted, COM scan decreasing).

**Init additions.** QP's init leaves contrast at 0x7F and does not set
precharge or VCOM, so `panel_setup[]` in `satisfaction_oled.c` sends the old
driver's values (0xFF, 0xF1, 0x20). On the SSD1306 it also forces page
addressing mode: QP only ever uses page addressing, and the old driver set
horizontal mode, which survives a DFU reflash because the panel stays
powered. The 3s blocking re-init in `keyboard_post_init_kb()` became a one-off
non-blocking re-init at 3s of uptime.

**Rendering.** Each repaint draws the whole screen into an offscreen 1bpp
surface, then copies it to the panel driver's framebuffer one controller page
per main loop pass, flushing after each. That framebuffer only marks pixels
that actually change, so unchanged screens send nothing and a key press on the
default screen sends one page. `qp_flush()` is synchronous; one page per pass
bounds the I2C stall to roughly what the old driver sent per pass. A new frame
is only drawn once the previous one is fully pushed, which removes the
stale-bottom-rows bug by construction (see "render bandwidth" below for the
history).

**Text** goes through a small character-grid layer (`s75_set_cursor()`,
`s75_write()`, ...) that keeps the OLED driver's cursor, advance and wrap
rules, drawing with a QFF conversion of QMK's own 6x8 glcdfont, so every
layout carried over unchanged. `assets/make_font_image.py` regenerates it.

**Bongo art** stays in the OLED page format and is drawn with `qp_viewport()`
plus `qp_pixdata()` one column byte at a time (`s75_draw_raw()`). Converting it
to QGF would have meant a second copy of 8K+ of sprite data for no visual
change.

**QP config.** `QUANTUM_PAINTER_DISPLAY_TIMEOUT 0` (the lib has its own sleep
timer and the OLED_OFF mode), minimal image/font/animation slots, and a 32
byte pixdata buffer. Animations stay compiled in because this tree's QP fails
to link with `QUANTUM_PAINTER_ANIMATIONS_ENABLE = no`
(`qp_internal_animation_tick()` calls deferred_exec unconditionally).

**Font sources in `post_rules.mk`.** It runs after the keymap's rules.mk, so
`rev1:tester` can still set `QUANTUM_PAINTER_ENABLE = no`; a conditional in
the board rules.mk would run too early to see that.

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

| Board | Keymap | Flash /128K | RAM /16K | vs. OLED driver (flash, RAM) |
|---|---|---|---|---|
| satisfaction75/prototype | default | 44736  34.1% | 11588  70.7% | +5364, +948 |
| satisfaction75/prototype | bongo   | 58804  44.9% | 12352  75.4% | +5372, +948 |
| satisfaction75/rev1      | default | 44736  34.1% | 11588  70.7% | +5364, +948 |
| satisfaction75/rev1      | bongo   | 58804  44.9% | 12352  75.4% | +5372, +948 |
| satisfaction75/rev1      | tester  | 32360  24.7% |  9520  58.1% | unchanged (no screen) |
| satisfaction75/rev2      | default | 44752  34.1% | 11588  70.7% | +5364, +948 |
| satisfaction75/rev2      | bongo   | 58440  44.6% | 12352  75.4% | +5372, +948 |
| satisfaction75_hs        | default | 44004  33.6% | 11860  72.4% | +5020, +948 |
| satisfaction75_hs        | bongo   | 57484  43.9% | 12632  77.1% | +5028, +956 |
| satisfaction75_big_hs    | default | 46492  35.5% | 13492  82.3% | +4780, +1440 |
| satisfaction75_big_hs    | bongo   | 56572  43.2% | 14188  86.6% | +4748, +1440 |

RAM is the one to watch. On the large panel QP's SH1107 driver reserves a
fixed 128x128 framebuffer (2K) whatever the panel size, so half of it is
unused here, on top of the 1K offscreen canvas.

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

The first flash then found a third: **render bandwidth** (OLED-driver version;
the QP pipeline described above replaces this fix). Every repaint
starts from `oled_clear()`, which marks all 16 blocks dirty, and the driver
sends one block per main loop pass, lowest first. On the SH1107 a 64-byte block
is eight page-mode command+data pairs at ~400kHz, so a full screen takes about
as long as the 66ms repaint interval. Each repaint re-dirtied block 0 before the
tail was reached, so the bottom two text rows never updated and kept the
previous screen. The parent branch fixed it by snapshotting the buffer and
narrowing `oled_dirty` to blocks that actually changed. This branch never
draws a new frame until the last one is fully pushed, so the failure cannot
recur; the sim's `bandwidth_scenario()` reports what each frame sends instead
of the old fixed-budget starvation test.

So: a green `make test` means the layouts and logic are right. It says nothing
about includes, warnings, the panel init and rotation, I2C timing, or flash
usage. Run a real compile before believing a change is done. (On this branch
the sim compiles real Quantum Painter, so the drawing code is the same code
that runs on the board; the panel is the stand-in.)

## Design decisions worth not re-litigating

**Rotation.** The parent branch set `oled_init_kb()` to return
`OLED_ROTATION_90` unconditionally. That is right for the 64x128 panel and
breaks every 128x32 board, because rotation 90 makes the logical canvas 32 wide
and 128 tall, so every cursor column and pixel coordinate lands wrong. It is now
`SAT75_PANEL_ROTATION`, derived per panel (and on this branch it is a QP
rotation; see the port section for why the large panel is 270).

**Both panels are 128 logical px wide.** Small is 128x32 (21x4 chars), large is
128x64 (21x8). That is why the split is cheap: same width, double the height.

**`SAT75_OLED_LARGE` selects the large panel.** On the parent branch it was
derived from the OLED driver's `OLED_DISPLAY_64X128`; with that driver gone,
the board defines it directly, next to choosing `sh1107_i2c` in rules.mk.

**Both panels have an `OLED_TIME` mode**, and on both `draw_clock()` serves
double duty: it is the browsable clock screen, and it is the `clock_set_mode`
field editor, which only adds a title row and an underline under the field
being edited. Keeping the two roles in one function is what stops the editor
and the view from drifting apart.

The large screen was briefly built without `OLED_TIME`, on the grounds that its
default screen already showed the time. That was the wrong trade: it forced the
default screen to carry a full size clock *and* everything else, and the panel
read as cramped. The clock now appears at two sizes -- 22x32 digits on the
dedicated screen, 18x24 on the default screen -- which is what frees the gutter
above and below it. `CLK_SM_DIGIT_*` in `satisfaction_oled.c` is the dial if
the small one wants to be smaller still.

`enum oled_modes` still differs by build, because of `BONGO_ENABLE` and
`POMODORO_ENABLE`; hence the `_NUM_OLED_MODES` clamp in `custom_config_load()`.
Note that re-adding `OLED_TIME` shifted every later mode index by one on the
large panel, so a board flashed with the earlier firmware comes back on a
different screen than it was left. Harmless -- one encoder press fixes it, and
the clamp catches anything out of range.

**The large default screen drops the year from the date** (`MON 08-31` rather
than `MON 2026-08-31`), and centres it over the mod flags / WPM row. The full
date is on `OLED_TIME`, which also needs it,
because `clock_set_mode` has to underline the year. `draw_date_text()` takes a
`with_year` flag for exactly this.

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

1. **Both panels light up at all, the right way up.** Highest risk: nothing
   below the panel framebuffer has run. The sim proves the screens are
   pixel-identical to the OLED-driver version up to that point; the QP
   controller init, `QP_ROTATION_270` on the SH1107 and the page/column
   commands are reasoned from the code, not observed. If the big screen is
   upside down, mirrored or offset, try the other rotations in
   `SAT75_PANEL_ROTATION` before touching any layout code. If the small screen
   shows garbage after reflashing from the OLED-driver firmware, power-cycle
   it once to rule out leftover controller state.
2. **Brightness matches.** `panel_setup[]` restores the old contrast,
   precharge and VCOM; compare against a board still on the old firmware.
3. **Typing feels the same with the bongo up.** A bongo frame changes most of
   the screen, so most pages are flushed, each a blocking I2C transfer of up to
   ~130 bytes. One page per main loop pass should keep each stall around a
   few ms, but check for missed or late keys while typing fast on the large
   panel, which has 16 pages to the small one's 4. The per-column
   `s75_draw_raw()` blit is the other CPU cost to watch there.
4. **The pomodoro against a real RTC.** The sim's RTC is a hand-written fake, so
   it proves the arithmetic, not that ChibiOS populates `RTCDateTime` the way
   this code assumes. Leave a phase running for a real 25 minutes and compare
   against a phone.
5. **VIA.** The new `pid 0x003B` needs a definition in VIA's own keyboards repo
   before the board is recognised and the new OLED mode is labelled.

## Not done

Nothing outstanding on the rebase: this branch was rebased onto upstream
`master` (`180f394b48`, 2026-10) with the `upstream` remote now configured.
Two upstream changes needed carrying through:

- #26320 moved satisfaction75 to the keyboard datablock only (no VIA custom
  config); `satisfaction75_big_hs/config.h` now matches the other boards with
  a plain `EECONFIG_KB_DATA_SIZE 20`, and `custom_config_load()` runs in
  `keyboard_post_init_kb()` before the display starts.
- #25591 made QP's comms callbacks return `bool`; only the sim's stub panel
  needed changing.

`DEFAULT_FOLDER` is gone upstream (keyboard aliases replace it), so
`cannonkeys/satisfaction75` still resolves to rev1. The pre-rebase branch is
kept as `backup/202610_sat75_quantum_painter_prerebase`.

`sim/` and this file are development scaffolding, not firmware. They would need
removing or relocating before anything goes near an upstream QMK PR.

## File map

```
satisfaction_oled.h         canvas/panel defines, display, drawing, text + calendar API
satisfaction_oled.c         QP setup, render/push pipeline, primitives, text layer,
                            7-segment, mode dispatch, both layouts
satisfaction_bongo.{c,h}    sprite data + animation state machine
satisfaction_pomodoro.{c,h} timer state machine + its two layouts
satisfaction_core.{c,h}     unchanged except display init/task hooks, EEPROM clamp, pomo hooks
assets/                     glcdfont as a QP font, and the script that makes it
sim/                        host-side simulator, on real Quantum Painter
```

`bongo.h` is gone; it defined non-static globals and 8K of sprite data in a
header, so it could only ever be included once.
