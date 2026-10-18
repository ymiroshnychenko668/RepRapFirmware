#pragma once
#include <RepRapFirmware.h>
#include <Platform/MessageType.h>
class GCodeBuffer;
namespace RrfModules::CartographerV3 {
void Init() noexcept;
void Spin() noexcept;
void Exit() noexcept;
void EmergencyStop() noexcept;
void Diagnostics(MessageType destination) noexcept;
GCodeResult Configure(GCodeBuffer& gb, const StringRef& reply) THROWS(GCodeException);
// Task-context acquisition. Interpolation retains the requested raw StepTimer time.
bool ReadHeight(float& height) noexcept;
bool ReadHeightAt(uint32_t rawTimerTicks, float& height) noexcept;
GCodeResult ReadHeightAtState(uint32_t rawTimerTicks, float& height) noexcept;
bool BeginScan() noexcept;
void EndScan() noexcept;
bool ModelReady() noexcept;
// Preparation is asynchronous; ArmProbe never waits for a remote command.
GCodeResult PrepareProbe(bool touchMode, float triggerHeight, float maxSpeed, const StringRef& reply) THROWS(GCodeException);
bool ArmProbe(bool touchMode, float triggerHeight, float maxSpeed) noexcept;
void DisarmProbe() noexcept;
// These two calls are safe in the step ISR: atomic flags and a 32-bit deadline only.
bool ProbeTriggered() noexcept;
bool ProbeFailed() noexcept;
bool TouchMode() noexcept;
float TouchTriggerHeight() noexcept;
float TouchProbeSpeed() noexcept;
float TouchThreshold() noexcept;
bool IsUkrainian() noexcept;
}
