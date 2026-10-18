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
}
