// Host shim: satisfaction_core.h pulls this in only for EEPROM addressing.
#pragma once
#define QK_KB_0 0x7E00
#define VIA_EEPROM_CUSTOM_CONFIG_SIZE 20
enum via_ids { id_custom_channel = 0, id_custom_set_value, id_custom_get_value, id_custom_save, id_unhandled };
