#!/usr/bin/env bash
set -euo pipefail
cartographer_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cartographer_tmp="$(mktemp -d "${TMPDIR:-/tmp}/rrf-cartographer.XXXXXX")"
trap 'rm -rf "$cartographer_tmp"' EXIT
cartographer_sources="$cartographer_root/src/Modules/CartographerV3"
"${CXX:-clang++}" -std=c++17 -O1 -g -Wall -Wextra -Werror -pedantic \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I"$cartographer_sources" "$cartographer_root/tests/cartographer_client.cpp" \
    "$cartographer_sources/Client.cpp" "$cartographer_sources/ClockSync.cpp" \
    "$cartographer_sources/Protocol.cpp" "$cartographer_sources/Profiles.cpp" \
    "$cartographer_sources/CalibrationModel.cpp" \
    -o "$cartographer_tmp/cartographer"
UBSAN_OPTIONS=halt_on_error=1 "$cartographer_tmp/cartographer"
"${CXX:-clang++}" -std=c++17 -O1 -g -Wall -Wextra -Werror -pedantic \
    -fsanitize=address,undefined -fno-omit-frame-pointer -DOCTOPUS_F446 \
    -I"$cartographer_root/tests/cartographer_stubs" -I"$cartographer_sources" \
    "$cartographer_root/tests/cartographer_can.cpp" "$cartographer_sources/CanTransport.cpp" \
    -o "$cartographer_tmp/cartographer-can"
UBSAN_OPTIONS=halt_on_error=1 "$cartographer_tmp/cartographer-can"
"${CXX:-clang++}" -std=c++17 -O1 -g -Wall -Wextra -Werror -pedantic \
    -fsanitize=address,undefined -fno-omit-frame-pointer -DOCTOPUS_F446 \
    -I"$cartographer_root/tests/cartographer_stubs" -I"$cartographer_sources" \
    "$cartographer_root/tests/cartographer_facade.cpp" \
    "$cartographer_sources/CartographerV3.cpp" "$cartographer_sources/CanTransport.cpp" \
    "$cartographer_sources/Client.cpp" "$cartographer_sources/ClockSync.cpp" \
    "$cartographer_sources/Protocol.cpp" "$cartographer_sources/Profiles.cpp" \
    "$cartographer_sources/CalibrationModel.cpp" \
    -o "$cartographer_tmp/cartographer-facade"
UBSAN_OPTIONS=halt_on_error=1 "$cartographer_tmp/cartographer-facade"
"${CXX:-clang++}" -std=c++17 -O1 -g -Wall -Wextra -Werror -pedantic \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I"$cartographer_sources" "$cartographer_root/tests/cartographer_model.cpp" \
    "$cartographer_sources/CalibrationModel.cpp" "$cartographer_sources/Profiles.cpp" \
    -o "$cartographer_tmp/cartographer-model"
UBSAN_OPTIONS=halt_on_error=1 "$cartographer_tmp/cartographer-model"
if [[ $# -gt 0 ]]; then
    python3 "$cartographer_root/Scripts/test-cartographer-reference.py" "$cartographer_tmp/cartographer-model" "$1"
fi
if "${CXX:-clang++}" -std=c++17 -fsyntax-only \
    -I"$cartographer_root/tests/cartographer_stubs" -I"$cartographer_sources" \
    "$cartographer_sources/CanTransport.cpp" >"$cartographer_tmp/unsupported.log" 2>&1; then
    echo "Unsupported target unexpectedly compiled" >&2; exit 1
fi
rg -q 'requires Octopus F446' "$cartographer_tmp/unsupported.log"
