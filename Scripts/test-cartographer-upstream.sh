#!/usr/bin/env bash
set -euo pipefail
cartographer_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cartographer_mcu="${1:?Pass the pinned Cartographer MCU source repository}"
cartographer_binary="${2:?Pass official firmware v2-v3/survey/6.1.0 directory}"
[[ "$(git -C "$cartographer_mcu" rev-parse HEAD)" == dae17c2509356dc285d5435a5cc05f94c388a5b4 ]] || { echo 'Unexpected Cartographer MCU revision' >&2; exit 1; }
git -C "$cartographer_mcu" diff --quiet HEAD -- src/command.c src/command.h src/trsync.c src/trsync.h src/generic src/compiler.h src/ctr.h src/sched.h src/basecmd.h lib/fast-hash
python3 "$cartographer_root/Scripts/cartographer-profiles.py" "$cartographer_binary" --check
cartographer_tmp="$(mktemp -d "${TMPDIR:-/tmp}/rrf-carto-upstream.XXXXXX")"
trap 'rm -rf "$cartographer_tmp"' EXIT
# Build unchanged upstream translation units; only board/scheduler/storage are shims.
mkdir -p "$cartographer_tmp/board"
for header in io misc pgm; do ln -s "$cartographer_mcu/src/generic/$header.h" "$cartographer_tmp/board/$header.h"; done
# Upstream GNU C intentionally uses zero-length VLA for zero-argument commands.
# Keep all other ASan/UBSan checks, and all checks for production C++ below.
cartographer_flags=('-D__section(S)=' -std=gnu11 -O1 -g -fsanitize=address,undefined -fno-sanitize=vla-bound -fno-omit-frame-pointer -I"$cartographer_root/tests/cartographer_upstream" -I"$cartographer_tmp" -I"$cartographer_mcu/src" -I"$cartographer_mcu/src/generic" -I"$cartographer_mcu/lib/fast-hash")
cartographer_objects=()
for unit in command generic/canserial generic/crc16_ccitt trsync; do
    cartographer_object="$cartographer_tmp/${unit//\//_}.o"
    "${CC:-clang}" "${cartographer_flags[@]}" -c "$cartographer_mcu/src/$unit.c" -o "$cartographer_object"
    cartographer_objects+=("$cartographer_object")
done
"${CC:-clang}" "${cartographer_flags[@]}" -c "$cartographer_mcu/lib/fast-hash/fasthash.c" -o "$cartographer_tmp/fasthash.o"
cartographer_objects+=("$cartographer_tmp/fasthash.o")
"${CC:-clang}" "${cartographer_flags[@]}" -c "$cartographer_root/tests/cartographer_upstream/peer.c" -o "$cartographer_tmp/peer.o"
cartographer_sources="$cartographer_root/src/Modules/CartographerV3"
for variant in full lite; do
    python3 "$cartographer_root/Scripts/cartographer-upstream-metadata.py" "$cartographer_binary/CartographerV3_6.1.0_CAN_1M_${variant}_8kib_offset.bin" >"$cartographer_tmp/metadata.c"
    "${CC:-clang}" "${cartographer_flags[@]}" -c "$cartographer_tmp/metadata.c" -o "$cartographer_tmp/metadata.o"
    "${CXX:-clang++}" -std=c++17 -O1 -g -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined -fno-omit-frame-pointer \
        -I"$cartographer_sources" "$cartographer_root/tests/cartographer_upstream.cpp" \
        "$cartographer_sources/Client.cpp" "$cartographer_sources/ClockSync.cpp" "$cartographer_sources/Protocol.cpp" "$cartographer_sources/Profiles.cpp" \
        "${cartographer_objects[@]}" "$cartographer_tmp/peer.o" "$cartographer_tmp/metadata.o" -o "$cartographer_tmp/upstream-$variant"
    for scenario in diagnostic foreign scan touch local-timeout lost-reports remote-timeout node-conflict crc-recovery ack-response-loss backpressure owned-reconnect invalid-sample unexpected-reason normal-stop bad-oid stale-report; do
        UBSAN_OPTIONS=halt_on_error=1 "$cartographer_tmp/upstream-$variant" "$scenario"
    done
done
