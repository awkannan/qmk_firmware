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

The drawing runs through **real Quantum Painter**: `quantum/painter` and the
1bpp surface from `drivers/painter/generic` are compiled for the host, along
with the converted glcdfont in `../assets`. So `qp_rect()`, the QFF font
decoder, `qp_pixdata()` and the surface's dirty tracking are all the code that
runs on the keyboard.

`shim/` provides just enough of QMK to compile off-target: `PROGMEM`/`PSTR`
no-ops, a fake systick, a fake ChibiOS RTC, and stubs for the keyboard state
the draw code reads (`get_mods()`, `matrix_get_row()`,
`host_keyboard_led_state()`, `get_current_wpm()`, …).

`sim_qp.c` replaces only the panel. `qp_sh1106_make_i2c_device()` and
`qp_sh1107_make_i2c_device()` return a 1bpp surface at the post-rotation
geometry, which is what those drivers keep internally as their framebuffer,
with its flush wrapped to count what a real flush would send. Each frame dumps
that panel framebuffer, so the output is what actually reached the panel, not
just what was drawn.

The sim builds with `SAT75_OLED_REPAINT_INTERVAL=0`, so one
`s75_display_task()` call always draws a frame, and the next
`SAT75_PANEL_PAGES` calls push it to the panel.

`sim_env.c`'s `sim_advance_ms()` steps the fake systick and the fake RTC
together, which is what makes it possible to fast-forward a 25-minute pomodoro
phase instantly, or jump the wall clock backwards to exercise the clamp.

## Canvas sizes

Both panels present a **128px wide** logical canvas once rotated; they differ
only in height.

| Build | Panel | QP driver, rotation | Logical canvas | Text grid | Pages |
|---|---|---|---|---|---|
| `s75sim-small` | 128x32 SSD1306 | `sh1106_i2c`, `QP_ROTATION_0` | 128 x 32 | 21 x 4 | 4 |
| `s75sim-large` | 64x128 SH1107 | `sh1107_i2c`, `QP_ROTATION_270` | 128 x 64 | 21 x 8 | 16 |

## What this does and does not prove

Covered: glyph and pixel placement at both sizes, cursor wrap and clipping,
mode dispatch, the OLED sleep/wake logic, the page-at-a-time push to the panel
framebuffer, how much of each frame would be sent, and any state machine
driven by the fake clock.

**Not** covered: the controller init sequences, the drivers' rotation-to-page
mapping, I2C timing, and firmware size. Everything below the panel framebuffer
still needs a real flash to confirm.

## Snapshots

`golden/small.txt` and `golden/large.txt` are the current expected output.
Every screen in them is pixel-identical to what the OLED-driver version of
this code rendered; only the per-frame send statistics differ.
`golden/baseline-small.txt` is a frozen capture of the small screen taken
*before* the large-screen rework, kept so small-screen regressions stay
obvious.
