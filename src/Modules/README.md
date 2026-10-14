# Compile-time RRF modules

`Registry.h` defines the Init/Spin/Exit/EmergencyStop/Diagnostics lifecycle.
`Generated.h` preserves upstream-style make defaults. rrfsetup generates the selected registry and header only in its source snapshot; optional translation units are added through `RRF_MODULES_FILE`.

The source catalog is `makefiles/modules/catalog.json`; paths in entries are relative to `src/`. BuildInfo is a read-only example that reports the selected target/modules through M122.

Canonical usage and extension instructions are in the rrfsetup repository: `Docs/Firmware/MODULES.md` (workspace path `rrfsetup/Docs/Firmware/MODULES.md`). Module selection belongs in `Profiles/Printers/<printer>/build-targets.json`, not SD printer configuration. Spin must not block; EmergencyStop must be handled independently of normal Exit. Disabled modules must have no translation units or symbols in the resulting image.
