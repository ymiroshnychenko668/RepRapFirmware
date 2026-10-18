#pragma once
#include <Endstops/ZProbe.h>

// RRF adapter: the Cartographer calibration is evaluated by the module, never by
// the analog-probe cubic fit. ISR methods only inspect the module's cached latch.
class CartographerZProbe final : public ZProbe
{
public:
    DECLARE_FREELIST_NEW_DELETE(CartographerZProbe)
    explicit CartographerZProbe(unsigned int number) noexcept;
    ~CartographerZProbe() noexcept override;
    void SetIREmitter(bool) const noexcept override { }
    uint32_t GetRawReading() const noexcept override;
    GCodeResult PrepareForProbing(const StringRef& reply, float requestedSpeed = 0.0) noexcept override;
    bool SetProbing(bool probing) noexcept override;
    bool StartHoming(float speed) noexcept override;
    void EndProbeMove() noexcept override;
    bool HasProbeError() const noexcept override;
    bool Stopped() const noexcept override;
    EndstopHitDetails CheckTriggered() noexcept override;
    GCodeResult Configure(GCodeBuffer& gb, const StringRef& reply, bool& seen) THROWS(GCodeException) override;
    GCodeResult AppendPinNames(const StringRef& reply) noexcept override;
    bool UsesExternalModel() const noexcept override { return true; }
    bool IsCalibrated() const noexcept override;
    float GetActiveModeTriggerHeight() const noexcept override;
    float GetProbingSpeed(int tapsDone) const noexcept override;
#if SUPPORT_SCANNING_PROBES
    GCodeResult PrepareForScanning(const StringRef& reply) noexcept override;
    void EndScanning() noexcept override;
    bool GetTouchModeActive() const noexcept override;
    float GetTouchModeHeight() const noexcept override;
    float GetTouchModeSpeed() const noexcept override;
    float GetTouchModeThreshold() const noexcept override;
    GCodeResult GetCalibratedReading(float& value) const noexcept override;
    GCodeResult GetCalibratedReadingAt(uint32_t rawTime, float& value) const noexcept override;
    float GetLatestHeight() const noexcept override;
#endif
private:
    bool collecting = false;
    float preparedSpeed = 0.0;
};
