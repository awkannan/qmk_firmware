# satisfaction75 OLED simulator

Renders the real `draw_*()` functions on the host so layouts can be checked
without flashing. No ARM toolchain and no QMK CLI required — just `gcc` and
`make`.

```
make run      # print every screen at both canvas sizes
make test     # diff current output against the committed snapshots
make golden   # refresh the snapshots after an intentional layout change
```

## How it works

`shim/` provides just enough of QMK to compile `satisfaction_oled.c` off-target:
`PROGMEM`/`PSTR` no-ops, a fake systick, a fake ChibiOS RTC, and stubs for the
keyboard state the draw code reads (`get_mods()`, `matrix_get_row()`,
`host_keyboard_led_state()`, `get_current_wpm()`, …). `sim_oled.c` reimplements
the OLED buffer API — the cursor, advance and clipping rules are transcribed
from `drivers/oled/oled_driver.c`, so off-by-one and wrap bugs reproduce here
exactly as they would on hardware. The real `drivers/oled/glcdfont.c` is
compiled in, so text lands on true pixels.

`sim_env.c`'s `sim_advance_ms()` steps the fake systick and the fake RTC
together, which is what makes it possible to fast-forward a 25-minute pomodoro
phase instantly, or jump the wall clock backwards to exercise the clamp.

## Canvas sizes

Both panels present a **128px wide** logical canvas once rotated; they differ
only in height.

| Build | Panel | Rotation | Logical canvas | Text grid |
|---|---|---|---|---|
| `s75sim-small` | 128x32 | `OLED_ROTATION_0` | 128 x 32 | 21 x 4 |
| `s75sim-large` | 64x128 | `OLED_ROTATION_90` | 128 x 64 | 21 x 8 |

## What this does and does not prove

Covered: glyph and pixel placement at both sizes, cursor wrap and clipping,
mode dispatch through `oled_task_kb()`, the OLED sleep/wake logic, and any
state machine driven by the fake clock.

**Not** covered: the driver's `rotate_90()`, the SH1107 init sequence, I2C
timing, and firmware size. The sim models the *post-rotation logical canvas* —
the layer every `draw_*()` function actually sees. Everything below that line
still needs a real flash to confirm.

## Snapshots

`golden/small.txt` and `golden/large.txt` are the current expected output.
`golden/baseline-small.txt` is a frozen capture of the small screen taken
*before* the large-screen rework, kept so small-screen regressions stay
obvious.
