# Enter lower-power sleep mode when on the ChibiOS idle thread
OPT_DEFS += -DCORTEX_ENABLE_WFI_IDLE=TRUE -DBLUEFRUIT_TRACE_SERIAL=TRUE

# these lines are all for bluetooth
BLUETOOTH_ENABLE = yes
BLUETOOTH_DRIVER = custom
SRC += ble.c uart.c
