// Host stand-in for the OLED panel.  Everything above the panel is the real
// Quantum Painter: the canvas surface, the font decoder, qp_rect(), qp_pixdata()
// and so on are compiled from quantum/painter and drivers/painter/generic.
//
// The panel itself is replaced by a 1bpp surface at the *post-rotation*
// geometry, which is what the sh1106/sh1107 drivers keep internally as their
// framebuffer.  Its flush is wrapped to record what a real flush would send.
// The controller init, rotation-to-page mapping and I2C are below this line
// and out of scope here, as rotate_90() was for the old sim.
#include "sim_qp.h"
#include "satisfaction_oled.h"
#include "qp_internal.h"
#include "qp_comms.h"
#include "qp_surface.h"
#include "qp_surface_internal.h"
#include <stdio.h>
#include <string.h>

#define CANVAS_BYTES SURFACE_REQUIRED_BUFFER_BYTE_SIZE(SAT75_CANVAS_WIDTH, SAT75_CANVAS_HEIGHT, 1)

static surface_painter_device_t panel;
static uint8_t                  panel_buffer[CANVAS_BYTES];
static painter_driver_vtable_t  panel_vtable;
static bool                     panel_powered;

static uint32_t flushes_sent;  // flushes that had anything to send
static uint32_t pixels_sent;   // area of those flushes' dirty boxes

static bool panel_init(painter_device_t device, painter_rotation_t rotation) {
    // The real driver swaps its surface geometry for 90/270; this surface is
    // already built at the rotated size.
    ((painter_driver_t *)device)->rotation = QP_ROTATION_0;
    return qp_surface_init(device, QP_ROTATION_0);
}

static bool panel_power(painter_device_t device, bool on) {
    panel_powered = on;
    return true;
}

static bool panel_flush(painter_device_t device) {
    surface_dirty_data_t *d = &panel.dirty;
    if (d->is_dirty) {
        flushes_sent++;
        pixels_sent += (uint32_t)(d->r - d->l + 1) * (d->b - d->t + 1);
    }
    return qp_surface_flush(device);
}

static bool comms_init(painter_device_t device) { return true; }
static bool comms_start(painter_device_t device) { return true; }
static bool comms_stop(painter_device_t device) { return true; }
static uint32_t comms_send(painter_device_t device, const void *data, uint32_t n) { return n; }
static bool comms_command(painter_device_t device, uint8_t cmd) { return true; }
static bool comms_bulk(painter_device_t device, const uint8_t *seq, size_t len) { return true; }

static const painter_comms_with_command_vtable_t panel_comms = {
    .base =
        {
            .comms_init  = comms_init,
            .comms_start = comms_start,
            .comms_send  = comms_send,
            .comms_stop  = comms_stop,
        },
    .send_command          = comms_command,
    .bulk_command_sequence = comms_bulk,
};

static painter_device_t make_panel(void) {
    painter_device_t dev = qp_make_mono1bpp_surface_advanced(&panel, 1, SAT75_CANVAS_WIDTH, SAT75_CANVAS_HEIGHT, panel_buffer);
    panel_vtable        = *panel.base.driver_vtable;
    panel_vtable.init   = panel_init;
    panel_vtable.power  = panel_power;
    panel_vtable.flush  = panel_flush;
    panel.base.driver_vtable = &panel_vtable;
    panel.base.comms_vtable  = (const painter_comms_vtable_t *)&panel_comms;
    return dev;
}

painter_device_t qp_sh1106_make_i2c_device(uint16_t panel_width, uint16_t panel_height, uint8_t i2c_address) {
    return make_panel();
}

painter_device_t qp_sh1107_make_i2c_device(uint16_t panel_width, uint16_t panel_height, uint8_t i2c_address) {
    return make_panel();
}

// QP's animations run on deferred_exec, which the sim does not have.  Nothing
// here animates through QP.
void deferred_exec_advanced_task(deferred_executor_t *table, size_t table_count, uint32_t *last_execution_time) {}
deferred_token defer_exec_advanced(deferred_executor_t *table, size_t table_count, uint32_t delay_ms, deferred_exec_callback callback, void *cb_arg) {
    return INVALID_DEFERRED_TOKEN;
}
bool cancel_deferred_exec_advanced(deferred_executor_t *table, size_t table_count, deferred_token token) {
    return false;
}

// --- inspection -------------------------------------------------------

static bool pixel(const uint8_t *buf, int x, int y) {
    int n = y * SAT75_CANVAS_WIDTH + x;
    return (buf[n / 8] >> (n % 8)) & 1;
}

bool sim_panel_powered(void) {
    return panel_powered;
}

void sim_panel_take_stats(uint32_t *flushes, uint32_t *pixels) {
    *flushes     = flushes_sent;
    *pixels      = pixels_sent;
    flushes_sent = 0;
    pixels_sent  = 0;
}

void sim_panel_dump(const char *title) {
    const uint8_t *buf = panel_buffer;
    printf("+- %s ", title);
    for (int i = 0; i < (int)(SAT75_CANVAS_WIDTH - strlen(title) - 4); i++) putchar('-');
    printf("+\n");
    for (int y = 0; y < SAT75_CANVAS_HEIGHT; y++) {
        putchar('|');
        for (int x = 0; x < SAT75_CANVAS_WIDTH; x++) {
            putchar(pixel(buf, x, y) ? '#' : '.');
        }
        printf("|\n");
    }
    putchar('+');
    for (int i = 0; i < SAT75_CANVAS_WIDTH; i++) putchar('-');
    printf("+\n\n");
}
