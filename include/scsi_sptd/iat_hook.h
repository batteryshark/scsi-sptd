#ifndef SCSI_SPTD_IAT_HOOK_H
#define SCSI_SPTD_IAT_HOOK_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

BOOL hook_iat_by_name(const char *dll_name, const char *function_name, FARPROC replacement_function);

BOOL Hook_IAT_Name(const char *dll_name, const char *function_name, FARPROC replacement_function);

#ifdef __cplusplus
}
#endif

#endif
