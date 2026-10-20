# Quantum Painter assets.  Here rather than in rules.mk because this runs after
# the keymap's rules.mk, so a keymap can still turn the screen off.
ifeq ($(strip $(QUANTUM_PAINTER_ENABLE)), yes)
    VPATH += keyboards/cannonkeys/lib/satisfaction75/assets
    SRC += s75_glcdfont.qff.c
endif
