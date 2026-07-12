#include "scsi_sptd/fs_hooks.h"

#include <string.h>

#include "scsi_sptd/debug.h"
#include "scsi_sptd/iat_hook.h"
#include "scsi_sptd/sptd.h"

#define DEVICE_PATH_PREFIX "\\\\.\\"
#define SCSI_PASS_THROUGH_DIRECT_IOCTL 0x0004D014u
#define VIRTUAL_SCSI_HANDLE_VALUE ((ULONG_PTR)0xB00BD001u)

static BOOL returned_initial_cdrom_drive = FALSE;

static BOOL is_virtual_scsi_handle(HANDLE handle)
{
    return (ULONG_PTR)handle == VIRTUAL_SCSI_HANDLE_VALUE;
}

static BOOL is_device_path(const char *file_name)
{
    return file_name != NULL && strncmp(file_name, DEVICE_PATH_PREFIX, strlen(DEVICE_PATH_PREFIX)) == 0;
}

static HANDLE virtual_scsi_handle(void)
{
    return (HANDLE)VIRTUAL_SCSI_HANDLE_VALUE;
}

HANDLE __stdcall rfx_CreateFileA(
    LPCSTR file_name,
    DWORD desired_access,
    DWORD share_mode,
    LPSECURITY_ATTRIBUTES security_attributes,
    DWORD creation_disposition,
    DWORD flags_and_attributes,
    HANDLE template_file)
{
    scsi_sptd_debug("CreateFileA");
    scsi_sptd_debug(file_name);

    if(is_device_path(file_name)) {
        scsi_sptd_debug("Optical device open redirected to virtual SPTD handle.");
        return virtual_scsi_handle();
    }

    return CreateFileA(
        file_name,
        desired_access,
        share_mode,
        security_attributes,
        creation_disposition,
        flags_and_attributes,
        template_file);
}

BOOL __stdcall rfx_CloseHandle(HANDLE object)
{
    if(is_virtual_scsi_handle(object)) {
        scsi_sptd_debug("Closing virtual SPTD handle.");
        return TRUE;
    }

    return CloseHandle(object);
}

BOOL __stdcall rfx_DeviceIoControl(
    HANDLE device,
    DWORD io_control_code,
    LPVOID input_buffer,
    DWORD input_buffer_size,
    LPVOID output_buffer,
    DWORD output_buffer_size,
    LPDWORD bytes_returned,
    LPOVERLAPPED overlapped)
{
    if(is_virtual_scsi_handle(device)) {
        if(io_control_code == SCSI_PASS_THROUGH_DIRECT_IOCTL) {
            return sptd_ioctl(
                device,
                io_control_code,
                input_buffer,
                input_buffer_size,
                output_buffer,
                output_buffer_size,
                bytes_returned,
                overlapped);
        }

        SetLastError(ERROR_INVALID_FUNCTION);
        return FALSE;
    }

    return DeviceIoControl(
        device,
        io_control_code,
        input_buffer,
        input_buffer_size,
        output_buffer,
        output_buffer_size,
        bytes_returned,
        overlapped);
}

UINT __stdcall rfx_GetDriveTypeA(LPCSTR root_path_name)
{
    (void)root_path_name;

    if(!returned_initial_cdrom_drive) {
        returned_initial_cdrom_drive = TRUE;
        return DRIVE_CDROM;
    }

    return DRIVE_FIXED;
}

BOOL scsi_sptd_install_file_system_hooks(void)
{
    BOOL installed = TRUE;

    installed = Hook_IAT_Name("KERNEL32.dll", "CreateFileA", (FARPROC)rfx_CreateFileA) && installed;
    installed = Hook_IAT_Name("KERNEL32.dll", "DeviceIoControl", (FARPROC)rfx_DeviceIoControl) && installed;
    installed = Hook_IAT_Name("KERNEL32.dll", "GetDriveTypeA", (FARPROC)rfx_GetDriveTypeA) && installed;
    installed = Hook_IAT_Name("KERNEL32.dll", "CloseHandle", (FARPROC)rfx_CloseHandle) && installed;

    if(!installed) {
        scsi_sptd_debug("One or more filesystem hooks failed to install.");
    }

    return installed;
}

void patch_fs(void)
{
    (void)scsi_sptd_install_file_system_hooks();
}
