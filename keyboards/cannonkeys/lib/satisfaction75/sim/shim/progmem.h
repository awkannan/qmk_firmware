// Host shim: PROGMEM is a no-op off-target.
#pragma once
#include <string.h>
#define PROGMEM
#define PSTR(x) (x)
#define pgm_read_byte(p) (*(const unsigned char *)(p))
#define memcpy_P memcpy
#define strcpy_P strcpy
