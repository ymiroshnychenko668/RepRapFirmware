#!/usr/bin/env bash
set -euo pipefail
cartographer_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cartographer_tmp="$(mktemp -d "${TMPDIR:-/tmp}/rrf-cartographer-integration.XXXXXX")"
trap 'rm -rf "$cartographer_tmp"' EXIT
cartographer_sources="$cartographer_root/src/Modules/CartographerV3"
"${CXX:-clang++}" -std=c++17 -O1 -g -Wall -Wextra -Werror -pedantic \
    -fsanitize=address,undefined -fno-omit-frame-pointer -DSUPPORT_SCANNING_PROBES=1 \
    -I"$cartographer_root/tests/cartographer_integration_stubs" \
    -I"$cartographer_root/tests/cartographer_stubs" -I"$cartographer_sources" \
    "$cartographer_root/tests/cartographer_zprobe.cpp" "$cartographer_sources/CartographerZProbe.cpp" \
    -o "$cartographer_tmp/zprobe"
UBSAN_OPTIONS=halt_on_error=1 "$cartographer_tmp/zprobe"
for cartographer_mode in 0 1; do
    "${CXX:-clang++}" -std=c++17 -O1 -g -Wall -Wextra -Werror -pedantic \
        -fsanitize=address,undefined -fno-omit-frame-pointer \
        -DSUPPORT_SCANNING_PROBES=0 -DSUPPORT_PHASE_STEPPING=0 -DRRF_MODULE_CARTOGRAPHER_V3="$cartographer_mode" \
        -I"$cartographer_root/src" "$cartographer_root/tests/cartographer_fractional.cpp" \
        -o "$cartographer_tmp/fractional-$cartographer_mode"
    UBSAN_OPTIONS=halt_on_error=1 "$cartographer_tmp/fractional-$cartographer_mode"
done
# Compile the exact production callback definition, with only its RRF services
# replaced by host fixtures. No implementation is copied into a test double.
python3 - "$cartographer_root" "$cartographer_tmp/scanning.cpp" <<'PYCODE'
from pathlib import Path
import sys
root=Path(sys.argv[1])
source=(root/'src/GCodes/GCodes6.cpp').read_text()
start=source.index('bool GCodes::TakeScanningProbeReading(uint32_t rawTime, uint32_t generation) noexcept')
end=source.index('// Return the number of scanning probe readings',start)
Path(sys.argv[2]).write_text((root/'tests/cartographer_scanning_prefix.h').read_text()+source[start:end]+(root/'tests/cartographer_scanning_suffix.h').read_text())
PYCODE
"${CXX:-clang++}" -std=c++17 -O1 -g -Wall -Wextra -Werror -pedantic \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I"$cartographer_root/tests/cartographer_stubs" "$cartographer_tmp/scanning.cpp" \
    -o "$cartographer_tmp/scanning"
UBSAN_OPTIONS=halt_on_error=1 "$cartographer_tmp/scanning"
python3 - "$cartographer_root" "$cartographer_tmp/motion.cpp" <<'PYCODE'
from pathlib import Path
import sys
root=Path(sys.argv[1])
manager=(root/'src/Endstops/EndstopsManager.cpp').read_text()
start=manager.index('EndstopHitDetails EndstopsManager::CheckEndstops() noexcept')
end=manager.index('// Configure the endstops in response to M574',start)
body=manager[start:end]
move=(root/'src/Movement/Move.cpp').read_text()
start=move.index('#if SUPPORT_CAN_EXPANSION\n// Returns true if the caller needs to wake')
end=move.index('// Generate the step pulses of internal drivers',start)
body+=move[start:end]
gcode=(root/'src/GCodes/GCodes4.cpp').read_text()
start=gcode.index('bool GCodes::WaitForEndstopOrProbingMoveToFinish(GCodeBuffer& gb) noexcept')
end=gcode.index('bool GCodes::PrepareZProbeMove',start)
body+=gcode[start:end]
Path(sys.argv[2]).write_text((root/'tests/cartographer_motion_prefix.h').read_text()+body+(root/'tests/cartographer_motion_suffix.h').read_text())
PYCODE
"${CXX:-clang++}" -std=c++17 -O1 -g -Wall -Wextra -Werror -pedantic \
    -fsanitize=address,undefined -fno-omit-frame-pointer -DSUPPORT_CAN_EXPANSION=0 -DRRF_MODULE_CARTOGRAPHER_V3=1 \
    -I"$cartographer_root/tests/cartographer_stubs" -I"$cartographer_sources" "$cartographer_tmp/motion.cpp" \
    -o "$cartographer_tmp/motion"
UBSAN_OPTIONS=halt_on_error=1 "$cartographer_tmp/motion"
python3 - "$cartographer_root" "$cartographer_tmp/grid.cpp" <<'PYCODE'
from pathlib import Path
import sys
root=Path(sys.argv[1])
gcode=(root/'src/GCodes/GCodes6.cpp').read_text()
start=gcode.index('GCodeResult GCodes::SelectProbeGridFilename(')
end=gcode.index('// Start probing the grid',start)
body=gcode[start:end]
state=(root/'src/GCodes/GCodes4.cpp').read_text()
start=state.index('\tcase GCodeState::gridProbing7:')
end=state.index('#if SUPPORT_SCANNING_PROBES\n\n\t// States used for grid scanning',start)
body+='void GCodes::FinishGrid(GCodeBuffer& gb,const StringRef& reply) { auto& move=reprap.GetMove(); GCodeResult stateMachineResult=GCodeResult::ok; switch(GCodeState::gridProbing7) {\n'+state[start:end]+'default: break; } result=stateMachineResult; }\n'
Path(sys.argv[2]).write_text((root/'tests/cartographer_grid_prefix.h').read_text()+body+(root/'tests/cartographer_grid_suffix.h').read_text())
PYCODE
"${CXX:-clang++}" -std=c++17 -O1 -g -Wall -Wextra -Werror -pedantic \
    -fsanitize=address,undefined -fno-omit-frame-pointer -DRRF_MODULE_CARTOGRAPHER_V3=1 -DHAS_MASS_STORAGE=1 -DHAS_SBC_INTERFACE=0 \
    -I"$cartographer_root/tests/cartographer_stubs" -I"$cartographer_sources" "$cartographer_tmp/grid.cpp" \
    -o "$cartographer_tmp/grid"
UBSAN_OPTIONS=halt_on_error=1 "$cartographer_tmp/grid"
# Exercise the exact feature guards from Config/Pins.h for both stock and plugin
# selections, including a pre-existing explicitly enabled legacy scanner build.
python3 - "$cartographer_root" "$cartographer_tmp/scanning-flags.h" <<'PYCODE'
from pathlib import Path
import sys
source=(Path(sys.argv[1])/'src/Config/Pins.h').read_text()
start=source.index('#ifndef SUPPORT_SCANNING_PROBES')
end=source.index('#ifndef SUPPORT_MODBUS_RTU',start)
Path(sys.argv[2]).write_text(source[start:end])
PYCODE
for cartographer_case in '0 0 0 0' '0 1 1 0' '1 0 1 1' '1 1 1 1'; do
    read -r cartographer_can cartographer_module cartographer_scan cartographer_legacy <<< "$cartographer_case"
    "${CXX:-clang++}" -std=c++17 -x c++ -fsyntax-only \
        -DSUPPORT_CAN_EXPANSION="$cartographer_can" -DRRF_MODULE_CARTOGRAPHER_V3="$cartographer_module" \
        -DEXPECT_SCAN="$cartographer_scan" -DEXPECT_LEGACY="$cartographer_legacy" \
        -include "$cartographer_tmp/scanning-flags.h" - <<'CPPCODE'
static_assert(SUPPORT_SCANNING_PROBES==EXPECT_SCAN);
static_assert(SUPPORT_LEGACY_SCANNING_PROBES==EXPECT_LEGACY);
CPPCODE
done
"${CXX:-clang++}" -std=c++17 -x c++ -fsyntax-only -DSUPPORT_CAN_EXPANSION=0 -DRRF_MODULE_CARTOGRAPHER_V3=0 -DSUPPORT_SCANNING_PROBES=1 \
    -include "$cartographer_tmp/scanning-flags.h" - <<'CPPCODE'
static_assert(SUPPORT_SCANNING_PROBES==1 && SUPPORT_LEGACY_SCANNING_PROBES==1);
CPPCODE
printf '%s\n' 'RRF scanning feature guards: stock/plugin ON/OFF, Duet CAN and explicit legacy scanner configurations passed'
python3 - "$cartographer_root" "$cartographer_tmp/scanning-states.cpp" <<'PYCODE'
from pathlib import Path
import sys
source=(Path(sys.argv[1])/'src/GCodes/GCodeMachineState.h').read_text()
start=source.index('enum class GCodeState : uint8_t')
end=source.index('};',start)+2
checks='\nstatic_assert(static_cast<unsigned>(GCodeState::gridScanning2)==static_cast<unsigned>(GCodeState::gridScanning1)+1);\n#if SUPPORT_LEGACY_SCANNING_PROBES\nstatic_assert(static_cast<unsigned>(GCodeState::probeCalibration3)==static_cast<unsigned>(GCodeState::probeCalibration1)+2);\n#endif\n'
Path(sys.argv[2]).write_text('#include <cstdint>\n'+source[start:end]+checks)
PYCODE
for cartographer_legacy in 0 1; do
    "${CXX:-clang++}" -std=c++17 -fsyntax-only -DSUPPORT_SCANNING_PROBES=1 -DSUPPORT_LEGACY_SCANNING_PROBES="$cartographer_legacy" "$cartographer_tmp/scanning-states.cpp"
done
