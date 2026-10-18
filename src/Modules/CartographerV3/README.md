# Cartographer V3 diagnostic module

The `cartographer-v3` extension reads an explicitly selected Cartographer V3 over
native Classic CAN on Octopus V1.1 F446. It is disabled at build time by default,
and enabling the build module leaves CAN inactive until an explicit start command.
This first package provides raw counts, coil frequency/temperature, clock
synchronization, bounded sample history and diagnostics. It does not create a
ZProbe, enable RRF scanning, command motion, or implement homing/Survey Touch.

## Build and verification

Use the workspace rrfsetup build system with this printer branch as `--source`:

```sh
rrfsetup build --source /path/to/this/checkout --target standard --enable-module cartographer-v3
rrfsetup build --source /path/to/this/checkout --target standard --disable-module cartographer-v3
bash Scripts/test-cartographer.sh
```

The catalog lists all six translation units. The hardware backend is intentionally
restricted to `OCTOPUS_F446`; selecting it for another MCU produces a compile error.
The module uses the existing lifecycle registry, including immediate local cleanup
on EmergencyStop. It does not depend on `accelerometers` or Duet CAN expansion.
The full F446 application remains subject to the 491520-byte image and 131072-byte
SRAM limits. Source SD configurations are independent of the module selection.

The host test command compiles production code with ASan and UBSan. Its three
harnesses cover framing/VLQ/CRC, real Full/Lite identify dictionaries, fake MCU
sessions, retries, timeouts, clock wrap/reboot, sensor validity, history bounds,
the bxCAN register model and the command facade. The facade tests exercise English
and Ukrainian errors, locale changes during a connection, UUID/pin validation,
EmergencyStop and repeated initialization. Hardware is not simulated electrically.

## Supported firmware

The built-in profiles match the **exact compressed identify bytes** in the
official **V3 6.1.0 Full and Lite CAN 1 Mbit/s** binaries. Other versions and
bitrates are rejected as `unknown_profile`; USB is not supported by this backend.
`profiles.json` records upstream commit, binary SHA256 and identify SHA256. The
profile identifies a protocol dictionary, not the entire running firmware image.

Regenerate or verify the profiles against the pinned official binary collection:

```sh
python3 Scripts/cartographer-profiles.py /path/to/cartographer_firmware/firmware/v2-v3/survey/6.1.0
python3 Scripts/cartographer-profiles.py /path/to/cartographer_firmware/firmware/v2-v3/survey/6.1.0 --check
```

The Python tool only reads files and generates source/metadata. It performs no
installation, connection or flash operation. The MCU client has no zlib or JSON
dependency. It first validates the dictionary, then checks the reported UUID and
node ID and reads shutdown/configuration status before enabling the stream. No
OID allocation, finalize_config, reset, bootloader, stepper, home or threshold
commands are issued.

## Diagnostic commands

`M558.9` is a local RRF extension for this module, not an upstream standard command.
When the module is absent, that fraction follows the existing macro fallback.

| Command | Effect |
|---|---|
| `M558.9` | Return state, fault, freshness, UUID and profile |
| `M558.9 S1 U"<12 hexadecimal UUID digits>"` | Explicitly start the selected diagnostic session |
| `M558.9 S0` | Invalidate freshness immediately, send stream-disable when possible, and release CAN after ACK or a bounded stop deadline |
| `M558.9 L"en"` / `M558.9 L"uk"` | Select command-error language while preserving the connection |
| `M122` | Include detailed module diagnostics through the normal registry |

The UUID placeholder must be replaced with the actual device UUID. No sample UUID
is installed in printer profiles. `S1` rejects an already active session; a fault
requires an explicit new start. Language changes and status reads never reconnect
or replay commands. Protocol field names, firmware version and raw values remain
verbatim in either language.

This implementation uses **node ID 64 on a dedicated, single-sensor bus** with one
host. Explicit node assignment is sent to the requested UUID; stock Klipper nodes
treat assigning their current node ID to another UUID as a conflict. Do not share
this diagnostic bus with another Klipper controller. Physical CAN termination,
probe power, exact PCB revision and connector wiring must be established before
hardware commissioning. The preserved BTT Octopus pinout maps CAN RX to PD0 and TX
to PD1; both pins are reserved through IoPort before changing their alternate
function. A pin conflict rejects activation. USB CDC and the printer configuration
are unaffected by module selection.

Normal stop waits at most 500 ms for the local diagnostic sequence. Exit and
EmergencyStop release local CAN immediately and never wait for a remote reply.
The stock probe may still produce background data after `stream en=0`; stopping
the local interface is not a promise of remote silence or remote power removal.

## Diagnostics and readiness

M122 reports the state/fault, selected profile/UUID, raw LDC count, ADC sum,
frequency in Hz, coil temperature in Celsius, sample timestamps, age in
microseconds, counters, RTT, clock uncertainty estimate, hardware RX overflow and
latched CAN ESR. The 64-entry history overwrites the oldest entry without growing;
`overwritten` describes this normal retention behavior. Hardware RX overflow is a
session fault. CRC failures are counted and the block parser resynchronizes.

`fresh=1` requires the validated profile/UUID, eight accepted clock observations
spanning at least one second, three consecutive valid samples, a synchronized
clock, estimated uncertainty at most 5000 us and sample age under 20000 us.
Age is measured from the projected sample time, not just CAN reception. The clock
model uses explicit 64-bit uptime queries and maps 32-bit sample clocks around
that anchor. RTT above 20 ms is excluded; uncertain retransmitted clock queries
are excluded as well. The uncertainty estimate includes half RTT, timer
quantization and a 5000 ppm drift envelope; hardware accuracy still needs
measurement. Readiness here describes diagnostic data quality and grants no
permission to move.

Missing samples or missing valid samples for 100 ms, bus-off, receive overflow,
clock discontinuity, shutdown and protocol/identity errors invalidate freshness.
Initial readiness has a five-second deadline after stream activation. A failed
session never silently reconnects. The published base count/ADC in this firmware
are fixed reference values, not a per-device factory calibration. No distance or
height is inferred from them.

## Source attribution

Wire semantics follow Klipper's `docs/Protocol.md`, `docs/CANBUS_protocol.md`,
`klippy/msgproto.py`, `src/basecmd.c` and `src/generic/canserial.c`. Upstream code is
Copyright Kevin O'Connor and contributors, GNU GPLv3. Sensor frequency and
thermistor conversion follow Cartographer3D's plugin and Klipper's thermistor
implementation. The compatibility dictionaries come from the GPLv3 Cartographer
firmware binaries; provenance is recorded in `profiles.json`. Keep these credits
and the repository's GPLv3 license when moving the implementation.

The integration plan and verification records belong in the workspace's
`rrfsetup/Docs`, including `Research/RepRapFirmware/CARTOGRAPHER_V3_INTEGRATION.md`.
