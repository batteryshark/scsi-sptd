#ifndef SCSI_SPTD_API_H
#define SCSI_SPTD_API_H

#if defined(_WIN32)
#    if defined(SCSI_SPTD_BUILD)
#        define SCSI_SPTD_API __declspec(dllexport)
#    else
#        define SCSI_SPTD_API __declspec(dllimport)
#    endif
#else
#    define SCSI_SPTD_API
#endif

#endif
