#ifndef SCSI_SPTD_DEBUG_H
#define SCSI_SPTD_DEBUG_H

#include <windows.h>

static inline void scsi_sptd_debug(const char *message)
{
    if(message != NULL) {
        OutputDebugStringA(message);
    }
}

static inline void scsi_sptd_debug_w(const wchar_t *message)
{
    if(message != NULL) {
        OutputDebugStringW(message);
    }
}

#endif
