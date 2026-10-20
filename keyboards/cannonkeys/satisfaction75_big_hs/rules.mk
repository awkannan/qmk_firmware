# Wildcard to allow APM32 MCU
DFU_SUFFIX_ARGS = -v FFFF -p FFFF

VPATH += keyboards/cannonkeys/lib/satisfaction75
SRC += satisfaction_encoder.c \
      satisfaction_oled.c \
      satisfaction_bongo.c \
      satisfaction_pomodoro.c \
      satisfaction_core.c

# The OLED is driven through Quantum Painter
QUANTUM_PAINTER_ENABLE = yes
QUANTUM_PAINTER_DRIVERS += sh1107_i2c
