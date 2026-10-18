#include "CartographerZProbe.h"
#include "CartographerV3.h"
#include "ProbeMessages.h"
#include <GCodes/GCodeBuffer/GCodeBuffer.h>
#include <Platform/RepRap.h>
#include <cmath>
#include <cstring>
#include <limits>

namespace Carto = RrfModules::CartographerV3;
CartographerZProbe::CartographerZProbe(unsigned int number) noexcept : ZProbe(number, ZProbeType::cartographerV3) { }
CartographerZProbe::~CartographerZProbe() noexcept { EndScanning(); Carto::DisarmProbe(); }
uint32_t CartographerZProbe::GetRawReading() const noexcept {
    return Carto::ProbeTriggered() ? 1000u : 0u;
}
GCodeResult CartographerZProbe::PrepareForProbing(const StringRef& reply, float requestedSpeed) noexcept {
    collecting = false;
    preparedSpeed = requestedSpeed == 0.0f ? InverseConvertSpeedToMmPerMin(GetProbingSpeed(0)) / 60.0f
                                         : std::fabs(InverseConvertSpeedToMmPerMin(requestedSpeed)) / 60.0f;
    return Carto::PrepareProbe(Carto::TouchMode(), GetActiveModeTriggerHeight(), preparedSpeed, reply);
}
bool CartographerZProbe::SetProbing(bool probing) noexcept {
    if (!probing) { if (!collecting) Carto::DisarmProbe(); return true; }
    return collecting ? Carto::ModelReady() : Carto::ArmProbe(Carto::TouchMode(), GetActiveModeTriggerHeight(), preparedSpeed);
}
bool CartographerZProbe::StartHoming(float speed) noexcept {
    const float requested = std::fabs(InverseConvertSpeedToMmPerMin(speed)) / 60.0f;
    return speed < 0.0f && !collecting && requested <= preparedSpeed * 1.001f && SetProbing(true);
}
void CartographerZProbe::EndProbeMove() noexcept { SetProbing(false); }
bool CartographerZProbe::HasProbeError() const noexcept { return Carto::ProbeFailed(); }
bool CartographerZProbe::Stopped() const noexcept { return Carto::ProbeTriggered() || Carto::ProbeFailed(); }
EndstopHitDetails CartographerZProbe::CheckTriggered() noexcept {
    EndstopHitDetails result;
    if (Stopped()) {
        result.SetAction(EndstopHitAction::stopAll);
        result.isZProbe = true;
        result.probeError = HasProbeError();
    }
    return result;
}
GCodeResult CartographerZProbe::Configure(GCodeBuffer& gb, const StringRef& reply, bool& seen) THROWS(GCodeException) {
    if (gb.Seen('P') && gb.GetUIValue() != static_cast<unsigned>(ZProbeType::cartographerV3)) {
        reply.copy(Carto::ProbeText(Carto::ProbeMessage::Configuration)); return GCodeResult::error;
    }
    if (gb.Seen('C')) {
        String<32> port; gb.GetQuotedString(port.GetRef());
        if (std::strcmp(port.c_str(), "cartographer-v3") != 0) {
            reply.copy(Carto::ProbeText(Carto::ProbeMessage::Configuration)); return GCodeResult::error;
        }
        seen = true;
    }
    return ZProbe::Configure(gb, reply, seen);
}
GCodeResult CartographerZProbe::AppendPinNames(const StringRef& reply) noexcept { reply.cat(", CAN cartographer-v3"); return GCodeResult::ok; }
bool CartographerZProbe::IsCalibrated() const noexcept { return Carto::ModelReady(); }
float CartographerZProbe::GetActiveModeTriggerHeight() const noexcept {
    return Carto::TouchMode() ? Carto::TouchTriggerHeight() : GetActualTriggerHeight();
}
float CartographerZProbe::GetProbingSpeed(int tapsDone) const noexcept {
    return Carto::TouchMode() ? ConvertSpeedFromMmPerSec(Carto::TouchProbeSpeed()) : ZProbe::GetProbingSpeed(tapsDone);
}
#if SUPPORT_SCANNING_PROBES
GCodeResult CartographerZProbe::PrepareForScanning(const StringRef& reply) noexcept {
    if (collecting) return GCodeResult::ok;
    float height;
    if (Carto::TouchMode() || !Carto::ReadHeight(height)) {
        reply.copy(Carto::ProbeText(Carto::ProbeMessage::Unavailable)); return GCodeResult::error;
    }
    if (!Carto::BeginScan()) { reply.copy(Carto::ProbeText(Carto::ProbeMessage::Unavailable)); return GCodeResult::error; }
    collecting = true; return GCodeResult::ok;
}
void CartographerZProbe::EndScanning() noexcept { if (collecting) Carto::EndScan(); collecting = false; }
GCodeResult CartographerZProbe::GetCalibratedReading(float& value) const noexcept {
    float height;
    if (!Carto::ReadHeight(height)) return GCodeResult::error;
    value = height - GetScanningHeight(); return GCodeResult::ok;
}
GCodeResult CartographerZProbe::GetCalibratedReadingAt(uint32_t rawTime, float& value) const noexcept {
    float height;
    const GCodeResult result = Carto::ReadHeightAtState(rawTime, height);
    if (result != GCodeResult::ok) return result;
    value = height - GetScanningHeight(); return GCodeResult::ok;
}
bool CartographerZProbe::GetTouchModeActive() const noexcept { return Carto::TouchMode(); }
float CartographerZProbe::GetTouchModeHeight() const noexcept { return Carto::TouchTriggerHeight(); }
float CartographerZProbe::GetTouchModeSpeed() const noexcept { return Carto::TouchProbeSpeed() * 60.0f; }
float CartographerZProbe::GetTouchModeThreshold() const noexcept { return Carto::TouchThreshold(); }
float CartographerZProbe::GetLatestHeight() const noexcept {
    float height; return Carto::ReadHeight(height) ? height : std::numeric_limits<float>::quiet_NaN();
}
#endif
