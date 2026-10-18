#pragma once
#include <cstdint>

namespace RrfModules::CartographerV3 {
// All host timestamps are raw StepTimer ticks, extended to 64 bits by the caller.
class ClockSync {
public:
    void Reset(uint32_t hostRate, uint32_t sensorRate) noexcept;
    bool Observe(uint64_t mcuClock, uint64_t sent, uint64_t received) noexcept;
    bool Convert(uint32_t mcuClock, uint64_t& expanded, uint64_t& hostTime) const noexcept;
    bool Ready(uint64_t now) const noexcept;
    uint32_t UncertaintyUs(uint64_t now) const noexcept;
    uint32_t RttUs() const noexcept;
    bool Rebooted() const noexcept { return rebooted; }
    uint32_t Count() const noexcept { return count; }
private:
    uint32_t hostHz = 1, mcuHz = 1, count = 0;
    uint64_t anchorMcu = 0, anchorHost = 0, lastReceived = 0, lastRtt = 0, firstHost = 0;
    uint64_t rateMcu = 0, rateHost = 0;
    double ticksPerHostTick = 1.0;
    bool rebooted = false;
};
}
