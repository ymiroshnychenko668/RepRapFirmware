# Host simulator for this fork

This target compiles the `../src` tree of
`ymiroshnychenko668/RepRapFirmware` (`v3.6-dev`, base
`e9940cfede7abe239da036f0c9739cb1d708ecba`, version 3.6.3).
It uses the fork's G-code interpreter, kinematics, lookahead and motion planner.
Hardware and FreeRTOS are replaced by host adapters; movement is captured as
virtual CAN messages. This is a host port, not STM32 instruction emulation.

The adapter originated in `tobbelobb/hp-sim5` commit
`c73db9a757c4ccb161c20d87b0bb94c832da39e1` (directory `RRF/host`).
The corresponding `RRF_HOST_BUILD` source adaptations were ported from
`tobbelobb/RepRapFirmware` legacy host commits `f5188561d` through
`dd6c44cf6d16d008d8a5fc1f1ec6cc6d4d3baacb`; newer motion algorithms were not
imported. The source retains its original license and attribution.

In the surrounding RepRap workspace, build with:

```sh
bash simulation/build-fork.sh
```

For a standalone build, provide the two library paths explicitly:

```sh
cmake -S host -B build-host -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DRRFLibraries_SOURCE_DIR=/path/to/RRFLibraries \
  -DCANlib_SOURCE_DIR=/path/to/CANlib
cmake --build build-host --target rrf_simulator -j 1
```

The workspace pins RRFLibraries at `7a81a6be863c5958596de05a3b646e4e3197e084`
with the `size_t` formatting fix in `simulation/patches/rrflibraries-size-format.patch`,
and `gloomyandy/CANlib` at `24c37bd23c12c8a613e4bb0fa39899e73fe91f6f`
with `simulation/patches/fork-canlib-noexcept.patch`.
Generated source copies live in the build directory; edit originals, not copies.

Heaters, sensors, endstops and GPIO are simplified adapters. They cannot validate
electrical behavior, thermal tuning or STM32 timing. Use a virtual SD directory
and bind the HTTP server to loopback via the workspace Docker launcher.

The verified runtime is native macOS/ARM64. Apple libc++ compatibility, real
pthread stack bounds and host heap alignment are handled locally. The fork's
real handle heap supports macro strings and arrays. Its own `StepDrivers` code
services virtual drives as time advances, including live position feedback.
File rewind after EOF is supported for nested macros. The Docker/Linux build
was interrupted by a full-disk/I/O failure and is not the validated final path.

Start DWC using `bash simulation/start-dwc.sh` in the parent workspace. It serves
`simulation/dwc-fork/vsd` on `127.0.0.1:18081`. Run
`python3 simulation/tests/run-fork-smoke.py` for the isolated regression test.
