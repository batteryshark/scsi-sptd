# SCSI SPTD CHD Emulation Layer

A 32-bit Windows hook DLL that emulates a CD/DVD drive for software that talks
to optical media through `IOCTL_SCSI_PASS_THROUGH_DIRECT`. It patches the current
process so device opens and SCSI passthrough calls are answered from a CHD image
instead of real hardware.

Status: work in progress. It compiles and the command handling is complete
enough to smoke-test, but there are no behavioral tests and no bundled host
harness. Treat it as a reference substrate, not a finished emulator.

## What it does

- Hooks selected `KERNEL32.dll` imports in the current process.
- Returns a virtual handle when the process opens a Win32 device path such as `\\.\D:`.
- Answers a small SCSI command subset routed through `DeviceIoControl`:
  - `TEST UNIT READY`
  - `REQUEST SENSE`
  - `INQUIRY`
  - `READ TOC/PMA/ATIP`
  - `PREVENT ALLOW MEDIUM REMOVAL`
  - `READ CD`
- Loads a CHD image through an external 32-bit `libchd.dll` at runtime.
- Builds the READ TOC response from CHD track metadata and returns raw
  2352-byte sectors from the image.

## Layout

```text
.
|-- CMakeLists.txt
|-- include/scsi_sptd/     public API and shared types
|-- src/                   dllmain, fs hooks, IAT hook, SCSI dispatch
|-- docs/ARCHITECTURE.md   mechanism and per-file responsibilities
`-- third_party/libchd/    provenance notes for the external CHD binary
```

## Build

The runtime depends on a 32-bit (`COFF-i386`) `libchd.dll`, so this must be
built 32-bit.

Visual Studio:

```powershell
cmake -S . -B build -A Win32
cmake --build build --config Release
```

MinGW:

```powershell
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_C_FLAGS=-m32 -DCMAKE_SHARED_LINKER_FLAGS=-m32
cmake --build build
```

`libchd.dll` is not included in this repository. Place a compatible 32-bit
`libchd.dll` next to the built DLL (or anywhere on the process search path)
before running; see [`third_party/libchd/README.md`](third_party/libchd/README.md)
for the expected exports and binary identity. If a copy is present at
`third_party/libchd/libchd.dll` at build time, CMake copies it next to the
output for convenience.

By default the DLL exports its initialization functions and expects a host or
injector to call them. To rebuild the historical "initialize from the process
command line during DLL load" behavior:

```powershell
cmake -S . -B build -A Win32 -DSCSI_SPTD_ENABLE_DLLMAIN=ON
```

## Runtime contract

With the command-line initializer, argument 1 is the CHD path:

```text
target.exe path\to\disc.chd
```

A host can also call the exports directly:

- `scsi_sptd_load_chd_image(const wchar_t *path)`
- `scsi_sptd_install_file_system_hooks()`
- `scsi_sptd_install_from_command_line()`

`init_sptd()` and `patch_fs()` remain as thin wrappers over the newer names so
older host code has a migration path.

## Known limits

- 32-bit only. The bundled CHD DLL is 32-bit and the fake-handle and IAT logic
  assume a 32-bit process.
- CHD only. BIN/CUE and `IOCTL_CDROM_RAW_READ` are not implemented.
- The device-path hook catches any `\\.\` path opened through `CreateFileA`;
  it should be narrowed to configured optical drives before reuse.
- `READ CD` returns raw 2352-byte sectors and keeps the original audio
  byte-swap fallback (see `docs/ARCHITECTURE.md`).
- No characterization tests. Successful compilation is a smoke check, not proof
  of behavior.
