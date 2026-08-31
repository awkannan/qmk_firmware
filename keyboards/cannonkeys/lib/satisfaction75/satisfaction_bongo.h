// Copyright 2023 Andrew Kannan
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>

// Drawing only, so it needs a screen.  Other files test SAT75_BONGO.
#if defined(BONGO_ENABLE) && defined(OLED_ENABLE)
#    define SAT75_BONGO
#endif

// Draws one frame of the bongo cat animation, advancing it as a side effect.
// Call once per OLED repaint.  With minimal set, the art is drawn without the
// WPM and clock overlay.
void draw_bongo(bool minimal);
