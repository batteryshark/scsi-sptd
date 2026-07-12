#include "scsi_sptd/sptd.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <shellapi.h>

#include "scsi_sptd/chd_types.h"
#include "scsi_sptd/debug.h"
#include "scsi_sptd/fs_hooks.h"

#define RAW_SECTOR_BYTES 2352u
#define READ_TOC_HEADER_BYTES 4u
#define READ_TOC_DESCRIPTOR_BYTES 8u
#define READ_TOC_LEADOUT_TRACK 0xAAu
#define SCSI_STATUS_CHECK_CONDITION 0x02u

typedef cdrom_file *(__cdecl *libchd_cdrom_open_fn)(const wchar_t *path);
typedef int(__cdecl *libchd_cdrom_read_data_fn)(
    cdrom_file *disc,
    uint32_t lba,
    unsigned char *buffer,
    uint32_t track_type,
    unsigned char include_subcode);

typedef struct legacy_scsi_pass_through_direct {
    USHORT Length;
    UCHAR ScsiStatus;
    UCHAR PathId;
    UCHAR TargetId;
    UCHAR Lun;
    UCHAR CdbLength;
    UCHAR SenseInfoLength;
    UCHAR DataIn;
    ULONG DataTransferLength;
    ULONG TimeOutValue;
    PVOID DataBuffer;
    ULONG SenseInfoOffset;
    UCHAR Cdb[16];
} legacy_scsi_pass_through_direct;

static HMODULE chd_library = NULL;
static libchd_cdrom_open_fn libchd_cdrom_open = NULL;
static libchd_cdrom_read_data_fn libchd_cdrom_read_data = NULL;
static cdrom_file *active_disc = NULL;
static unsigned char *read_toc_response = NULL;
static uint32_t read_toc_response_size = 0;

static const unsigned char inquiry_response[32] = {
    0x05, 0x80, 0x00, 0x32, 0x5B, 0x00, 0x00, 0x00,
    0x43, 0x48, 0x44, 0x44, 0x72, 0x69, 0x76, 0x65,
    0x56, 0x69, 0x72, 0x74, 0x75, 0x61, 0x6C, 0x20,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20
};

static const unsigned char default_sense_info[18] = {
    0x70, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x0A, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static void write_be16(unsigned char *destination, uint16_t value)
{
    destination[0] = (unsigned char)((value >> 8) & 0xFFu);
    destination[1] = (unsigned char)(value & 0xFFu);
}

static void write_be32(unsigned char *destination, uint32_t value)
{
    destination[0] = (unsigned char)((value >> 24) & 0xFFu);
    destination[1] = (unsigned char)((value >> 16) & 0xFFu);
    destination[2] = (unsigned char)((value >> 8) & 0xFFu);
    destination[3] = (unsigned char)(value & 0xFFu);
}

static uint32_t read_be32(const unsigned char *source)
{
    return ((uint32_t)source[0] << 24) |
        ((uint32_t)source[1] << 16) |
        ((uint32_t)source[2] << 8) |
        (uint32_t)source[3];
}

static uint32_t read_be24(const unsigned char *source)
{
    return ((uint32_t)source[0] << 16) |
        ((uint32_t)source[1] << 8) |
        (uint32_t)source[2];
}

static ULONG min_ulong(ULONG left, ULONG right)
{
    return left < right ? left : right;
}

static BOOL copy_data_to_request(legacy_scsi_pass_through_direct *request, const void *data, ULONG data_size)
{
    if(request->DataBuffer == NULL || data == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    ULONG bytes_to_copy = min_ulong(request->DataTransferLength, data_size);
    memcpy(request->DataBuffer, data, bytes_to_copy);
    request->DataTransferLength = bytes_to_copy;
    request->SenseInfoLength = 0;

    return TRUE;
}

static BOOL complete_request(
    LPVOID output_buffer,
    DWORD output_buffer_size,
    LPDWORD bytes_returned,
    const legacy_scsi_pass_through_direct *request)
{
    if(output_buffer == NULL || request == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    DWORD bytes_to_copy = min_ulong((ULONG)output_buffer_size, request->Length);
    memcpy(output_buffer, request, bytes_to_copy);

    if(bytes_returned != NULL) {
        *bytes_returned = bytes_to_copy;
    }

    return TRUE;
}

static void mark_check_condition(legacy_scsi_pass_through_direct *request)
{
    request->ScsiStatus = SCSI_STATUS_CHECK_CONDITION;
    request->SenseInfoLength = sizeof(default_sense_info);
}

static unsigned char track_control_for_toc(const cdrom_track_info *track)
{
    return track->subtype == CD_SUB_RAW ? 0x14u : 0x10u;
}

static BOOL build_read_toc_response(void)
{
    if(active_disc == NULL) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    uint32_t track_count = active_disc->cdtoc.numtrks;
    if(track_count == 0 || track_count > CD_MAX_TRACKS) {
        scsi_sptd_debug("CHD TOC has an invalid track count.");
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }

    size_t response_size = READ_TOC_HEADER_BYTES + (((size_t)track_count + 1u) * READ_TOC_DESCRIPTOR_BYTES);
    if(response_size > UINT16_MAX + 2u) {
        SetLastError(ERROR_ARITHMETIC_OVERFLOW);
        return FALSE;
    }

    unsigned char *toc = (unsigned char *)calloc(response_size, sizeof(unsigned char));
    if(toc == NULL) {
        SetLastError(ERROR_OUTOFMEMORY);
        return FALSE;
    }

    write_be16(toc, (uint16_t)(response_size - 2u));
    toc[2] = 0x01u;
    toc[3] = (unsigned char)track_count;

    size_t descriptor_offset = READ_TOC_HEADER_BYTES;
    uint32_t leadout_lba = 0;

    for(uint32_t index = 0; index < track_count; ++index) {
        const cdrom_track_info *track = &active_disc->cdtoc.tracks[index];

        toc[descriptor_offset + 1u] = track_control_for_toc(track);
        toc[descriptor_offset + 2u] = (unsigned char)(index + 1u);
        write_be32(&toc[descriptor_offset + 4u], track->physframeofs);

        leadout_lba = track->physframeofs + track->frames + track->extraframes;
        descriptor_offset += READ_TOC_DESCRIPTOR_BYTES;
    }

    toc[descriptor_offset + 1u] = 0x10u;
    toc[descriptor_offset + 2u] = READ_TOC_LEADOUT_TRACK;
    write_be32(&toc[descriptor_offset + 4u], leadout_lba);

    free(read_toc_response);
    read_toc_response = toc;
    read_toc_response_size = (uint32_t)response_size;

    return TRUE;
}

static BOOL load_chd_library(void)
{
    if(chd_library != NULL) {
        return TRUE;
    }

    chd_library = LoadLibraryA("libchd.dll");
    if(chd_library == NULL) {
        scsi_sptd_debug("Failed to load libchd.dll.");
        return FALSE;
    }

    libchd_cdrom_open = (libchd_cdrom_open_fn)GetProcAddress(chd_library, "libchd_cdrom_open");
    libchd_cdrom_read_data = (libchd_cdrom_read_data_fn)GetProcAddress(chd_library, "libchd_cdrom_read_data");

    if(libchd_cdrom_open == NULL || libchd_cdrom_read_data == NULL) {
        scsi_sptd_debug("libchd.dll is missing required CD-ROM exports.");
        FreeLibrary(chd_library);
        chd_library = NULL;
        libchd_cdrom_open = NULL;
        libchd_cdrom_read_data = NULL;
        return FALSE;
    }

    return TRUE;
}

BOOL scsi_sptd_load_chd_image(const wchar_t *chd_path)
{
    if(chd_path == NULL || chd_path[0] == L'\0') {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if(!load_chd_library()) {
        return FALSE;
    }

    scsi_sptd_debug("Loading CHD:");
    scsi_sptd_debug_w(chd_path);

    cdrom_file *opened_disc = libchd_cdrom_open(chd_path);
    if((uintptr_t)opened_disc <= 1u) {
        scsi_sptd_debug("Opening CHD failed.");
        SetLastError(ERROR_OPEN_FAILED);
        return FALSE;
    }

    active_disc = opened_disc;
    return build_read_toc_response();
}

static void swap_audio_byte_pairs(unsigned char *buffer, uint32_t byte_count)
{
    for(uint32_t index = 0; index + 1u < byte_count; index += 2u) {
        unsigned char first = buffer[index];
        buffer[index] = buffer[index + 1u];
        buffer[index + 1u] = first;
    }
}

static BOOL read_disc_sectors(
    unsigned char *destination,
    uint32_t start_lba,
    uint32_t block_count,
    uint32_t track_type)
{
    if(destination == NULL || active_disc == NULL || libchd_cdrom_read_data == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    unsigned char sector[RAW_SECTOR_BYTES];

    for(uint32_t block = 0; block < block_count; ++block) {
        memset(sector, 0, sizeof(sector));

        uint32_t lba = start_lba + block;
        int read_result = libchd_cdrom_read_data(active_disc, lba, sector, track_type, 1);

        if(read_result == 0) {
            read_result = libchd_cdrom_read_data(active_disc, lba, sector, CD_TRACK_AUDIO, 1);
            if(read_result != 0) {
                swap_audio_byte_pairs(sector, RAW_SECTOR_BYTES);
            }
        }

        if(read_result == 0) {
            scsi_sptd_debug("CHD sector read failed.");
            SetLastError(ERROR_READ_FAULT);
            return FALSE;
        }

        memcpy(destination + ((size_t)block * RAW_SECTOR_BYTES), sector, RAW_SECTOR_BYTES);
    }

    return TRUE;
}

static BOOL handle_read_cd(legacy_scsi_pass_through_direct *request)
{
    uint32_t lba = read_be32(&request->Cdb[2]);
    uint32_t block_count = read_be24(&request->Cdb[6]);

    if(block_count == 0) {
        request->DataTransferLength = 0;
        request->SenseInfoLength = 0;
        return TRUE;
    }

    if(block_count > (UINT32_MAX / RAW_SECTOR_BYTES)) {
        SetLastError(ERROR_ARITHMETIC_OVERFLOW);
        return FALSE;
    }

    uint32_t byte_count = block_count * RAW_SECTOR_BYTES;
    if(request->DataBuffer == NULL || request->DataTransferLength < byte_count) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }

    if(!read_disc_sectors((unsigned char *)request->DataBuffer, lba, block_count, CD_TRACK_MODE1_RAW)) {
        return FALSE;
    }

    request->DataTransferLength = byte_count;
    request->SenseInfoLength = 0;
    return TRUE;
}

static BOOL validate_sptd_input(LPVOID input_buffer, DWORD input_buffer_size)
{
    return input_buffer != NULL && input_buffer_size >= sizeof(legacy_scsi_pass_through_direct);
}

BOOL __stdcall sptd_ioctl(
    HANDLE device,
    DWORD io_control_code,
    LPVOID input_buffer,
    DWORD input_buffer_size,
    LPVOID output_buffer,
    DWORD output_buffer_size,
    LPDWORD bytes_returned,
    LPOVERLAPPED overlapped)
{
    (void)device;
    (void)io_control_code;
    (void)overlapped;

    if(!validate_sptd_input(input_buffer, input_buffer_size)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    legacy_scsi_pass_through_direct request;
    memcpy(&request, input_buffer, sizeof(request));

    char debug_message[128] = {0};

    switch(request.Cdb[0]) {
        case 0x00:
            scsi_sptd_debug("TEST UNIT READY");
            request.SenseInfoLength = 0;
            return complete_request(output_buffer, output_buffer_size, bytes_returned, &request);

        case 0x03:
            scsi_sptd_debug("REQUEST SENSE");
            request.Lun = 1;
            if(!copy_data_to_request(&request, default_sense_info, sizeof(default_sense_info))) {
                mark_check_condition(&request);
            }
            return complete_request(output_buffer, output_buffer_size, bytes_returned, &request);

        case 0x12:
            scsi_sptd_debug("INQUIRY");
            request.Lun = 1;
            if(!copy_data_to_request(&request, inquiry_response, sizeof(inquiry_response))) {
                mark_check_condition(&request);
            }
            return complete_request(output_buffer, output_buffer_size, bytes_returned, &request);

        case 0x43:
            scsi_sptd_debug("READ TOC/PMA/ATIP");
            if(read_toc_response == NULL || !copy_data_to_request(&request, read_toc_response, read_toc_response_size)) {
                mark_check_condition(&request);
            }
            return complete_request(output_buffer, output_buffer_size, bytes_returned, &request);

        case 0x1E:
            snprintf(
                debug_message,
                sizeof(debug_message),
                "PREVENT ALLOW MEDIUM REMOVAL set to %u",
                (unsigned int)(request.Cdb[4] & 0x01u));
            scsi_sptd_debug(debug_message);
            request.SenseInfoLength = 0;
            return complete_request(output_buffer, output_buffer_size, bytes_returned, &request);

        case 0xBE:
            if(!handle_read_cd(&request)) {
                mark_check_condition(&request);
            }
            return complete_request(output_buffer, output_buffer_size, bytes_returned, &request);

        default:
            snprintf(debug_message, sizeof(debug_message), "Unknown SPTD operation code 0x%02X", request.Cdb[0]);
            scsi_sptd_debug(debug_message);
            request.SenseInfoLength = 0;
            return complete_request(output_buffer, output_buffer_size, bytes_returned, &request);
    }
}

BOOL scsi_sptd_initialize_from_command_line(void)
{
    int argument_count = 0;
    LPWSTR *arguments = CommandLineToArgvW(GetCommandLineW(), &argument_count);
    if(arguments == NULL) {
        return FALSE;
    }

    if(argument_count < 2) {
        scsi_sptd_debug("No CHD path supplied on the command line.");
        LocalFree(arguments);
        SetLastError(ERROR_BAD_ARGUMENTS);
        return FALSE;
    }

    BOOL initialized = scsi_sptd_load_chd_image(arguments[1]);
    LocalFree(arguments);

    return initialized;
}

BOOL scsi_sptd_install_from_command_line(void)
{
    if(!scsi_sptd_initialize_from_command_line()) {
        return FALSE;
    }

    return scsi_sptd_install_file_system_hooks();
}

void init_sptd(void)
{
    (void)scsi_sptd_initialize_from_command_line();
}
