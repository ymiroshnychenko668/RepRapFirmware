#pragma once
#include <RepRapFirmware.h>
#include <GCodes/GCodeBuffer/GCodeBuffer.h>
#define DECLARE_FREELIST_NEW_DELETE(Type)
constexpr float CartographerTestClock = 1000000.0f;
inline float ConvertSpeedFromMmPerSec(float value) { return value / CartographerTestClock; }
inline float InverseConvertSpeedToMmPerMin(float value) { return value * CartographerTestClock * 60.0f; }
enum class ZProbeType { cartographerV3 = 12 };
enum class EndstopHitAction { none, stopAll };
struct EndstopHitDetails {
    EndstopHitAction action=EndstopHitAction::none; bool isZProbe=false, probeError=false;
    void SetAction(EndstopHitAction value) { action=value; }
    EndstopHitAction GetAction() const { return action; }
};
class ZProbe {
public:
    ZProbe(unsigned, ZProbeType) { }
    virtual ~ZProbe() = default;
    virtual void SetIREmitter(bool) const noexcept=0;
    virtual uint32_t GetRawReading() const noexcept=0;
    virtual GCodeResult PrepareForProbing(const StringRef&,float=0) noexcept { return GCodeResult::ok; }
    virtual bool SetProbing(bool) noexcept=0;
    virtual bool StartHoming(float) noexcept { return true; }
    virtual void EndProbeMove() noexcept { }
    virtual bool HasProbeError() const noexcept { return false; }
    virtual bool Stopped() const noexcept=0;
    virtual EndstopHitDetails CheckTriggered() noexcept=0;
    virtual GCodeResult Configure(GCodeBuffer&,const StringRef&,bool&) { return GCodeResult::ok; }
    virtual GCodeResult AppendPinNames(const StringRef&) noexcept=0;
    virtual bool UsesExternalModel() const noexcept { return false; }
    virtual bool IsCalibrated() const noexcept { return false; }
    virtual float GetActiveModeTriggerHeight() const noexcept { return 2.0f; }
    virtual float GetProbingSpeed(int) const noexcept { return ConvertSpeedFromMmPerSec(2.0f); }
    virtual GCodeResult PrepareForScanning(const StringRef&) noexcept { return GCodeResult::ok; }
    virtual void EndScanning() noexcept { }
    virtual bool GetTouchModeActive() const noexcept { return false; }
    virtual float GetTouchModeHeight() const noexcept { return 0; }
    virtual float GetTouchModeSpeed() const noexcept { return 0; }
    virtual float GetTouchModeThreshold() const noexcept { return 0; }
    virtual GCodeResult GetCalibratedReading(float&) const noexcept { return GCodeResult::error; }
    virtual GCodeResult GetCalibratedReadingAt(uint32_t,float&) const noexcept { return GCodeResult::error; }
    virtual float GetLatestHeight() const noexcept { return 0; }
    float GetActualTriggerHeight() const noexcept { return 2.0f; }
    float GetScanningHeight() const noexcept { return 2.0f; }
};
