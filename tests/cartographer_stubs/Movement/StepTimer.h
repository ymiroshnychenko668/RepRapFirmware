#pragma once
#include "RepRapFirmware.h"
struct StepTimer {
    static uint32_t GetTimerTicks() noexcept { return fakeTicks; }
    static constexpr uint32_t GetTickRate() noexcept { return 1000000; }
};
