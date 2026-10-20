// Host stand-in for the OLED panel; see sim_qp.c.
#pragma once
#include <stdbool.h>
#include <stdint.h>

bool sim_panel_powered(void);
// Flushes that sent anything, and the total area of their dirty boxes, since
// the last call.
void sim_panel_take_stats(uint32_t *flushes, uint32_t *pixels);
// Prints what the panel is showing.
void sim_panel_dump(const char *title);
