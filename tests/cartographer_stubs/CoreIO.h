#pragma once
#include "RepRapFirmware.h"
inline uint32_t millis() noexcept { return fakeMillis; }
inline uint32_t HAL_RCC_GetPCLK1Freq() noexcept { return fakePclk; }
