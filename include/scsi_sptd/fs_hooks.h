#ifndef SCSI_SPTD_FS_HOOKS_H
#define SCSI_SPTD_FS_HOOKS_H

#include <windows.h>

#include "scsi_sptd/api.h"

#ifdef __cplusplus
extern "C" {
#endif

SCSI_SPTD_API BOOL scsi_sptd_install_file_system_hooks(void);

SCSI_SPTD_API void patch_fs(void);

#ifdef __cplusplus
}
#endif

#endif
