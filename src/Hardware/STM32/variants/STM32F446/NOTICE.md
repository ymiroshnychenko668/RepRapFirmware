# F446 variant provenance

variant.cpp, variant.h, PeripheralPins.c and PinNamesVar.h originate from BTT BIGTREETECH-OCTOPUS-V1.0 commit 9526ff16f68839626cb8875a299e73f99f2e57e8, Firmware/RepRapFirmware/RRFBuild-SourceCode/CoreN2G/src/STM32F4/variants/STM32F446. Their original ST and Arduino licence notices are retained.

The startup file and HAL configuration originate from CoreN2G aae27c368f63d2cdec3da45e19dd231c9eb83b13. Startup adds RRF CRC/version/vector metadata; the original ST notice is retained. The linker layout is derived from this repository's STM32F407 layout, under its GPL-3.0 licence.
