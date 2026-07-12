#ifndef SCSI_SPTD_H
#define SCSI_SPTD_H

#include <windows.h>

#include "scsi_sptd/api.h"

#ifdef __cplusplus
extern "C" {
#endif

SCSI_SPTD_API BOOL scsi_sptd_load_chd_image(const wchar_t *chd_path);
SCSI_SPTD_API BOOL scsi_sptd_initialize_from_command_line(void);
SCSI_SPTD_API BOOL scsi_sptd_install_from_command_line(void);

SCSI_SPTD_API void init_sptd(void);

SCSI_SPTD_API BOOL __stdcall sptd_ioctl(
    HANDLE device,
    DWORD io_control_code,
    LPVOID input_buffer,
    DWORD input_buffer_size,
    LPVOID output_buffer,
    DWORD output_buffer_size,
    LPDWORD bytes_returned,
    LPOVERLAPPED overlapped);

#ifdef __cplusplus
}
#endif

#endif
