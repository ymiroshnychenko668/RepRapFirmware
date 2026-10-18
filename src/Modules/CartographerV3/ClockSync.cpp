#include "ClockSync.h"
#include <cmath>
#include <limits>

namespace RrfModules::CartographerV3 {
void ClockSync::Reset(uint32_t hostRate, uint32_t sensorRate) noexcept {
    *this = ClockSync();
    hostHz = hostRate; mcuHz = sensorRate;
    ticksPerHostTick = static_cast<double>(mcuHz) / hostHz;
}
bool ClockSync::Observe(uint64_t clock, uint64_t sent, uint64_t received) noexcept {
    if (received < sent || received - sent > hostHz / 50) return false; // reject RTT >20ms
    const uint64_t midpoint = sent + (received - sent) / 2;
    if (count != 0) {
        if (clock < anchorMcu || midpoint <= anchorHost) { rebooted = clock < anchorMcu; return false; }
        const double expected = static_cast<double>(midpoint - anchorHost) * ticksPerHostTick;
        const double error = std::fabs(static_cast<double>(clock - anchorMcu) - expected);
        // A reboot near a 32-bit wrap must not masquerade as a clock update.
        if (error > static_cast<double>(mcuHz) / 20 + expected / 200) { rebooted = true; return false; }
        if (midpoint - rateHost >= hostHz) {
            const double measured = static_cast<double>(clock - rateMcu) / (midpoint - rateHost);
            const double nominal = static_cast<double>(mcuHz) / hostHz;
            if (std::fabs(measured / nominal - 1) < static_cast<double>(5) / 1000) ticksPerHostTick += (measured - ticksPerHostTick) / 4;
            rateMcu = clock; rateHost = midpoint;
        }
    } else { firstHost = midpoint; rateMcu = clock; rateHost = midpoint; }
    anchorMcu = clock; anchorHost = midpoint; lastReceived = received; lastRtt = received - sent;
    ++count;
    return true;
}
bool ClockSync::Convert(uint32_t raw, uint64_t& expanded, uint64_t& hostTime) const noexcept {
    if (count == 0) return false;
    const int32_t delta = static_cast<int32_t>(raw - static_cast<uint32_t>(anchorMcu));
    if (delta < 0 && static_cast<uint64_t>(-static_cast<int64_t>(delta)) > anchorMcu) return false;
    expanded = static_cast<uint64_t>(static_cast<int64_t>(anchorMcu) + delta);
    const double time = static_cast<double>(anchorHost) + delta / ticksPerHostTick;
    if (!std::isfinite(time) || time < 0 || time >= static_cast<double>(std::numeric_limits<uint64_t>::max())) return false;
    hostTime = static_cast<uint64_t>(time);
    return true;
}
bool ClockSync::Project(uint64_t hostTime, uint64_t& mcuClock) const noexcept {
    if (count == 0) return false;
    const double delta = hostTime >= anchorHost ? static_cast<double>(hostTime-anchorHost) : -static_cast<double>(anchorHost-hostTime);
    const double projected = static_cast<double>(anchorMcu) + delta*ticksPerHostTick;
    if (!std::isfinite(projected) || projected < 0 || projected >= static_cast<double>(std::numeric_limits<uint64_t>::max())) return false;
    mcuClock=static_cast<uint64_t>(projected); return true;
}
bool ClockSync::Ready(uint64_t now) const noexcept {
    return count >= 8 && anchorHost - firstHost >= hostHz && now >= lastReceived && now - lastReceived < 2 * hostHz;
}
uint32_t ClockSync::UncertaintyUs(uint64_t now) const noexcept {
    if (count == 0 || now < anchorHost) return UINT32_MAX;
    // Includes half RTT, timer quantization and a conservative 5000ppm drift envelope.
    const uint64_t ticks = lastRtt / 2 + 1 + (now - anchorHost) / 200;
    const uint64_t us = ticks * 1000000 / hostHz;
    return us > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(us);
}
uint32_t ClockSync::RttUs() const noexcept { return static_cast<uint32_t>(lastRtt * 1000000 / hostHz); }
}
