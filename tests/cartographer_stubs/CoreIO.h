#pragma once
#include "RepRapFirmware.h"
inline uint32_t millis() noexcept { return fakeMillis; }
inline uint32_t HAL_RCC_GetPCLK1Freq() noexcept { return fakePclk; }

// Host analogue of bounded interrupt-masked register access (recursive for nested guards).
#include <mutex>
inline std::recursive_mutex fakeInterruptMutex;
inline unsigned IrqSave() noexcept { fakeInterruptMutex.lock(); return 0; }
inline void IrqRestore(unsigned) noexcept { fakeInterruptMutex.unlock(); }
