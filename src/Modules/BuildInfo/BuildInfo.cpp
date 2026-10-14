/*
 * This module only counts main-loop calls and reports its build selection.
 * It never changes motion, heaters, outputs, storage or interrupt configuration.
 */
#include "BuildInfo.h"

#include <Modules/Generated.h>
#include <Platform/Platform.h>
#include <Platform/RepRap.h>
#include <cinttypes>
#include <cstdint>

namespace RrfModules::BuildInfo
{
namespace
{
	uint32_t spins = 0;
	bool running = false;
}

void Init() noexcept
{
	spins = 0;
	running = true;
}

void Spin() noexcept
{
	if (running && spins != UINT32_MAX)
	{
		++spins;
	}
}

void Exit() noexcept
{
	running = false;
}

void EmergencyStop() noexcept
{
	running = false;
}

void Diagnostics(MessageType destination) noexcept
{
	reprap.GetPlatform().MessageF(destination,
		"RRF build target: %s; modules: %s; build-info loops: %" PRIu32 "; state: %s\n",
		RRF_BUILD_TARGET, RRF_ENABLED_MODULES, spins, (running) ? "running" : "stopped");
}
}
