# FlyingBear Reborn 2 / MKS Nano6 STM32F407VG, stock 48 KiB bootloader.
# Sector 3: vectors; sector 4: reset data; sectors 5-11: firmware and embedded FS.
LINKER_SCRIPT_BASE = $(REPRAPFIRMWARE_DIR)/src/Hardware/STM32/variants/STM32F407/flyingbear-reborn-2
VECT_TAB_OFFSET = 0xC000
PRINTER_FLAGS += -DFLYINGBEAR_REBORN2
