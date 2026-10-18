# Cartographer V3 height and diagnostic module

The `cartographer-v3` extension reads an explicitly selected Cartographer V3 over
native Classic CAN on Octopus V1.1 F446. It is disabled at build time by default,
and enabling the build module leaves CAN inactive until an explicit start command.
The module provides raw counts, coil frequency/temperature, clock
synchronization, bounded sample history, calibrated height and diagnostics. It does not create a
ZProbe, enable RRF scanning, command motion, or implement homing/Survey Touch.

## Build and verification

Use the workspace rrfsetup build system with this printer branch as `--source`:

```sh
rrfsetup build --source /path/to/this/checkout --target standard --enable-module cartographer-v3
rrfsetup build --source /path/to/this/checkout --target standard --disable-module cartographer-v3
bash Scripts/test-cartographer.sh
```

The catalog lists all seven translation units. The hardware backend is intentionally
restricted to `OCTOPUS_F446`; selecting it for another MCU produces a compile error.
The module uses the existing lifecycle registry, including immediate local cleanup
on EmergencyStop. It does not depend on `accelerometers` or Duet CAN expansion.
The full F446 application remains subject to the 491520-byte image and 131072-byte
SRAM limits. Source SD configurations are independent of the module selection.

The host test command compiles production code with ASan and UBSan. Its four
harnesses cover framing/VLQ/CRC, real Full/Lite identify dictionaries, fake MCU
sessions, retries, timeouts, clock wrap/reboot, sensor validity, history bounds,
the bxCAN register model and the command facade. The facade tests exercise English
and Ukrainian errors, locale changes during a connection, UUID/pin validation,
EmergencyStop and repeated initialization. A fourth harness validates the height
model, boundaries, checksum, temperature and monotonicity proof. The client uses
double frequency and thermistor conversion throughout, avoiding float rounding
before height evaluation. Hardware is not simulated electrically.

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
| `M558.9 S2 P"0:/sys/cartographer.cart" U"<12 UUID digits>" B"<surface ID>"` | Load and verify a complete calibration file without reconnecting |
| `M558.9 S3` | Unload the model while preserving the diagnostic connection |
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
measurement. This describes raw diagnostic readiness and grants no permission to move.
The summary reports `state=uncalibrated` when this raw link is ready but a valid
height is unavailable. `state=ready` additionally requires `height_valid=1` from
the matching model, surface, profile, domain and temperature; M122 preserves the
underlying `raw_state` explicitly. A missing or invalid model never authorizes a
height reading or movement.

Missing samples or missing valid samples for 100 ms, bus-off, receive overflow,
clock discontinuity, shutdown and protocol/identity errors invalidate freshness.
Initial readiness has a five-second deadline after stream activation. A failed
session never silently reconnects. The published base count/ADC in this firmware
are fixed reference values, not a per-device factory calibration. Height is
inferred only from an explicitly supplied, verified model.

## Height model and offline calibration

The offline `rrfsetup cartographer calibrate` command fits ten ascending power
coefficients in the normalized reciprocal-frequency domain, using column-pivoted
Householder QR. A separate validation CSV is mandatory. Supplied measurements
must come from independently known mechanical heights; this package does not
collect a motion trajectory. Synthetic tests do not calibrate a real probe.

The firmware evaluates `u=(2/f-d0-d1)/(d1-d0)` and `height=Horner(c,u)+z_offset`
in double precision, matching the pinned current Cartographer plugin. The legacy
`model_offset` has the opposite sign and cannot be copied as `z_offset`.
Extrapolation is rejected. Explicit imported coil compensation converts sample
frequency from measured temperature to calibration reference temperature before
evaluating the polynomial. Without compensation, the admitted temperature interval
is limited to one Celsius degree. No universal coil parameters are used.

The 320-byte little-endian binary64 file uses magic `CARTOV3\0`, version 1,
UUID, exact compressed identify SHA256, an ASCII surface ID, domain, coefficients,
temperature bounds/reference, additive offset, height bounds, validation errors
and sample counts, optional five compensation parameters, and CRC32 IEEE over
bytes 0..315 stored at offset 316. Reserved bytes and surface padding must be zero.
The exact layout and independent host codec are documented in the workspace's
`rrfsetup/internal/cartographer/README.md`.

Loading verifies all 320 bytes and EOF, checksum, metadata and numeric bounds,
then proves positive derivative over the whole domain using Bernstein bounds and
bounded subdivision with a conservative binary64 roundoff margin. The proof uses a fixed stack, at most 4096 nodes / depth 20;
there is no recursive or dynamic allocation. Stored heldout maximum error must
not exceed 0.001 mm. CRC detects accidental corruption; it does not authenticate
measurement results.

`S2` checks explicit UUID and surface ID and a supported profile; an established
session must match the UUID/profile. A complete valid candidate replaces the old
model. Rejected or incomplete files leave the old model intact. `Init` clears the
RAM model; no startup G-code or printer SD configuration is modified and there is
no automatic reload. The operator must select the physical surface ID correctly;
hardware cannot detect a changed plate.

M122 adds `height_mm`, `height_valid`, `height_status`, `compensated_hz`, `surface`,
`model_loaded` and `model_load_error`. Unavailable heights are invalid, never a
clamped or extrapolated success. Runtime evaluation occurs on diagnostic queries,
outside the IRQ. ZProbe, mesh, motion, homing and Touch are future packages.

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

Height and coil compensation follow Cartographer3D `probe/scan_model.py` and
`coil/temperature_compensation.py`, commit
`e94dcfc98644dee08452e36338cafa2eb41753db`, GNU GPLv3.
