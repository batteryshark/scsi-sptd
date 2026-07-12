# Architecture

`scsi_sptd` is a process-local compatibility shim, not a system device driver.
It patches the import table of the current process so selected Win32 file and
device calls are routed to local C functions that answer from a CHD image.

```mermaid
flowchart LR
    Host[Host process] -->|CreateFileA "\\\\.\\..."| FsHooks[fs_hooks.c]
    FsHooks -->|fake HANDLE 0xB00BD001| Host
    Host -->|DeviceIoControl IOCTL_SCSI_PASS_THROUGH_DIRECT| FsHooks
    FsHooks --> Sptd[sptd.c SCSI command dispatcher]
    Sptd -->|READ TOC / READ CD| Chd[libchd.dll]
    Chd --> Image[CHD image file]
    Sptd -->|INQUIRY / SENSE / TOC / sectors| Host
    FsHooks -->|IAT mutation| Iat[iat_hook.c]
```

Solid links are observed directly in source. The host process behavior is
inferred from the hooked APIs and the original command-line CHD path.

## Components

### `src/fs_hooks.c`

Owns the Win32 API hook surface: `CreateFileA`, `DeviceIoControl`,
`GetDriveTypeA`, and `CloseHandle`. It returns a sentinel handle for device-path
opens and forwards SPTD IOCTLs into `sptd_ioctl`.

### `src/sptd.c`

Owns the emulated SCSI behavior and CHD-backed reads. It loads `libchd.dll`,
opens the CHD image, builds a READ TOC response from CHD track metadata, and
handles the command subset listed in the README.

### `src/iat_hook.c`

Owns import-table mutation for the current process. The original scope
referenced `Hook_IAT_Name` without its implementation; this restores it as a
focused module rather than pulling in unrelated patch helpers from sibling
projects.

### `third_party/libchd`

The 32-bit (`COFF-i386`) CHD binary dependency and its provenance notes. It
exports `libchd_cdrom_open`, `libchd_cdrom_read_data`, and related CD-ROM and
hard-disk helpers.

## Gotchas

- The fake handle value `0xB00BD001` is part of the contract between the
  filesystem hooks and the SPTD dispatcher.
- `GetDriveTypeA` returns `DRIVE_CDROM` once and `DRIVE_FIXED` afterward,
  preserving the original "discover an optical drive once" behavior.
- `READ CD` retries a failed mode-1 raw read as audio and byte-swaps the sector.
  The original note tied this to Saturn Archive.org CHD audio.
- The `libchd_cdrom_open` signature is inferred from how the original code passed
  `CommandLineToArgvW(args[1])` into it. The wrapper treats the path as wide text.

## Scope decision

Preserved as a focused 32-bit hook DLL that emulates a small SPTD optical-drive
command subset from a CHD image, not as a general disc emulator. The 32-bit
constraint follows the bundled CHD dependency and the original fake-handle and
IAT assumptions. Reconsider the scope if an original host or injector is found,
the `libchd` wrapper source is recovered, or there is a concrete reason to add
BIN/CUE, `IOCTL_CDROM_RAW_READ`, or a 64-bit target.

## Known weaknesses

- No host harness is preserved with this folder.
- No characterization tests exist for the SCSI command responses.
- Device-path interception is broad and should be narrowed before reuse.
- The CHD DLL is preserved only as a binary with recorded metadata, not as a
  rebuildable source dependency.
