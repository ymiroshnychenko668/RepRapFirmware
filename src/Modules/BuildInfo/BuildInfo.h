/* Read-only example of a firmware extension with explicit lifecycle hooks. */
#ifndef RRF_MODULES_BUILD_INFO_H
#define RRF_MODULES_BUILD_INFO_H

#include <Platform/MessageType.h>

namespace RrfModules::BuildInfo
{
	void Init() noexcept;
	void Spin() noexcept;
	void Exit() noexcept;
	void EmergencyStop() noexcept;
	void Diagnostics(MessageType destination) noexcept;
}

#endif
