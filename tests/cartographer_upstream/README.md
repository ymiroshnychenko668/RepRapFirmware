# Independent Cartographer MCU protocol peer

Run `Scripts/test-cartographer-upstream.sh <pinned-MCU-source> <official-6.1-directory>`.
The script verifies MCU revision `dae17c2509356dc285d5435a5cc05f94c388a5b4` and
unchanged source inputs, then builds these upstream C translation units directly:
`src/command.c`, `src/generic/canserial.c`, `src/generic/crc16_ccitt.c`,
`src/trsync.c` and `lib/fast-hash/fasthash.c`. They are not copied or edited.
The compatibility dictionaries and exact command/response tables come from the
preserved official Full/Lite 6.1 CAN 1M binaries, verified against generated
production profile provenance.

`peer.c` supplies test board functions, a bounded timer scheduler, OID storage,
modeled config/base readout and coil counts. The sensor/home command bodies are
hardware models; actual sensor analog behavior and contact detection are not
simulated. ACK/NAK generation, stream resynchronization, signed MCU VLQ/CRC,
CAN node assignment/conflict handling, trsync arm/report/timeout/trigger state,
and timer callback behavior come from the unchanged MCU C code. Thus this peer
does not reuse RRF's encoder or parser.

`peer.h` is reusable by the full facade/register integration fixture. Initialize
one coldboot per test process with `peer_uuid`, define `peer_output` to receive
CAN frames, feed host packets using `peer_feed`, and advance the MCU clock with
`peer_advance` or `peer_tick` (one MHz host). OID/sequence/CAN state intentionally
persists through host client reconnects. Each independent scenario is a fresh
process, matching a remote coldboot.

Both the peer and production client are instrumented with ASan/UBSan. Only the
GNU C zero-length VLA check is disabled for upstream C, because its unchanged
`command_dispatch` declares a zero-length argument array for commands without
parameters. Production C++ retains all enabled sanitizer checks. Compile-time
request section attributes are omitted by a preprocessor adapter for Mach-O;
protocol logic is unchanged.

The standalone client scenarios cover Full and Lite, read-only diagnostic mode,
foreign configuration rejection, owned reconnect, Scan and Touch setup, genuine
EndstopHit success, invalid sensor data, unexpected trigger reasons, MCU expiry,
local deadline and report loss while raw samples continue, CAN node conflict,
CRC/NAK recovery, simultaneous ACK/response loss and bounded backpressure.
The fixed hardware UUID and all samples belong to this emulator only. This does
not commission a real printer or substitute for measuring CAN/stop latency.

Klipper/Cartographer upstream code is GPLv3; attribution remains in its source
files and the module README.

The full facade/register integration harness runs with
`Scripts/test-cartographer-full-stack.sh <MCU-source> <6.1-directory> [capture-output-directory]`.
It connects the production `Configure` facade, `CanTransport` translation unit,
CAN RX interrupt handler, protocol client, clock synchronization and model to
this same MCU peer. A conditional test-only TX register hook records each real
mailbox write, preserves its chronological order and marks occupied mailboxes
until the scripted bus drains them. Init acknowledgements and one-entry FIFO
releases are explicit host adapters. This is not a complete bxCAN register or
physical bus simulation.

The harness starts its StepTimer near a 32-bit wrap, queries actual unassigned
CAN discovery, starts an explicit UUID with trigger opt-in, loads a verified
model bound to the emulator's UUID and current binary dictionary, exports the
production sample JSON with binary64 bit fields, and exercises scanning and
probe preparation/arming. Optional output captures are actual facade JSON from
this emulator, useful as cross-language import fixtures. Their height is a
synthetic mechanically labelled test value, not a real calibration measurement.


The emergency regressions hold the production runtime mutex from another host
thread, both through a deliberately blocked calibration-file read and during an
armed probe. Emergency shutdown must return while that owner remains blocked,
publish probe failure immediately, and disable local CAN/IRQ independently.
The subsequent model load and pending restart are rejected, and deferred cleanup
releases pin ownership. The host test's 100 ms future deadline detects waiting;
it is not a physical MCU timing measurement. A separate late-hit case omits any
prior ISR status read and proves task supervision latches an expired local lease
before accepting a queued otherwise-valid remote EndstopHit.
