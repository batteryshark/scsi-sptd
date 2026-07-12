# libchd.dll

External CHD helper library used by `scsi_sptd` at runtime. The binary is **not
included** in this repository; this note records the identity and exports of the
copy the code was written against, so a compatible 32-bit `libchd.dll` can be
supplied or rebuilt.

Observed metadata:

- Format: `COFF-i386`
- Address size: 32-bit
- SHA-256: `BB45A4A1789D027DF1A4E798178443CF9578DF16E6097C68DA6A0A8EB91C1397`

Observed exports:

- `libchd_cdrom_close`
- `libchd_cdrom_get_toc`
- `libchd_cdrom_get_version`
- `libchd_cdrom_open`
- `libchd_cdrom_read_data`
- `libchd_cdrom_read_subcode`
- `libchd_hard_disk_close`
- `libchd_hard_disk_get_info`
- `libchd_hard_disk_open`
- `libchd_hard_disk_read`
- `libchd_hard_disk_write`

The source for this wrapper was not recovered. Treat the DLL as an external
binary dependency until its source is found or replaced.
