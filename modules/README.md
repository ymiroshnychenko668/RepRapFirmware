# External module firmware sources

This directory preserves firmware for external hardware as Git submodules. Each
checkout retains its upstream history and license. The printer worktree's RRF
build modules use `src/Modules` and `makefiles/modules/catalog.json`; this
lowercase `modules/` directory stores independent firmware projects instead.

## SIBOOR Cartographer V3

[`cartographer-v3/`](cartographer-v3/) is a full clone of
[Cartographer3D/MCU-Firmware---Based-on-Klipper](https://github.com/Cartographer3D/MCU-Firmware---Based-on-Klipper),
branch `cartographer`. It contains the probe MCU firmware sources, based on
Klipper and licensed under GPL-3.0; see `cartographer-v3/COPYING`.

The upstream V3 build configurations are:

- `config.CartoV3USB`
- `config.CartoV3USBLite`
- `config.CartoV3CAN1M`
- `config.CartoV3CAN1MLite`

The source checkout also contains V4 configurations. Preserve the V3 target when
working with this hardware. The upstream build instructions are in
[`cartographer-v3/README.md`](cartographer-v3/README.md) and its `docs/` directory.

This addition preserves sources for research and future integration; Cartographer
support has not been implemented or enabled in RepRapFirmware by adding this
submodule. No firmware has been built or flashed.

Documentation, hardware materials, host plugins, bootloader and prebuilt firmware
are preserved in the workspace's
[`references/siboor-cartographer-v3/`](../../references/siboor-cartographer-v3/README.md).
The workspace `repositories.lock.json` records the pinned source revision and the
independent reference checkout.

To restore this checkout after cloning RepRapFirmware:

```sh
git submodule update --init modules/cartographer-v3
```
