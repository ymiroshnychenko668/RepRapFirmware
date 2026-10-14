/*
 * Compile-time extension lifecycle. rrfsetup generates Registry.cpp for the
 * selected modules in its source snapshot. No runtime registration is required.
 */
#ifndef RRF_MODULES_REGISTRY_H
#define RRF_MODULES_REGISTRY_H

#include <Modules/Generated.h>
#include <Platform/MessageType.h>

namespace RrfModules
{
#if RRF_HAS_EXTENSIONS
	void Init() noexcept;
	void Spin() noexcept;
	void Exit() noexcept;
	void EmergencyStop() noexcept;
	void Diagnostics(MessageType destination) noexcept;
#else
	inline void Init() noexcept { }
	inline void Spin() noexcept { }
	inline void Exit() noexcept { }
	inline void EmergencyStop() noexcept { }
	inline void Diagnostics(MessageType) noexcept { }
#endif
}

#endif
