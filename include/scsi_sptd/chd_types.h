#ifndef SCSI_SPTD_CHD_TYPES_H
#define SCSI_SPTD_CHD_TYPES_H

#include <stdint.h>

#define CD_MAX_TRACKS 99
#define CD_MAX_SECTOR_DATA 2352
#define CD_MAX_SUBCODE_DATA 96

#define CD_FRAME_SIZE (CD_MAX_SECTOR_DATA + CD_MAX_SUBCODE_DATA)
#define CD_FRAMES_PER_HUNK 4
#define CD_METADATA_WORDS (1 + (CD_MAX_TRACKS * 6))

typedef enum cd_track_type {
    CD_TRACK_MODE1 = 0,
    CD_TRACK_MODE1_RAW,
    CD_TRACK_MODE2,
    CD_TRACK_MODE2_FORM1,
    CD_TRACK_MODE2_FORM2,
    CD_TRACK_MODE2_FORM_MIX,
    CD_TRACK_MODE2_RAW,
    CD_TRACK_AUDIO
} cd_track_type;

typedef enum cd_subcode_type {
    CD_SUB_NORMAL = 0,
    CD_SUB_RAW,
    CD_SUB_NONE
} cd_subcode_type;

typedef struct cdrom_track_info {
    uint32_t trktype;
    uint32_t subtype;
    uint32_t datasize;
    uint32_t subsize;
    uint32_t frames;
    uint32_t extraframes;
    uint32_t pregap;
    uint32_t postgap;
    uint32_t pgtype;
    uint32_t pgsub;
    uint32_t pgdatasize;
    uint32_t pgsubsize;
    uint32_t padframes;
    uint32_t logframeofs;
    uint32_t physframeofs;
    uint32_t chdframeofs;
} cdrom_track_info;

typedef struct cdrom_toc {
    uint32_t numtrks;
    cdrom_track_info tracks[CD_MAX_TRACKS];
} cdrom_toc;

typedef struct cdrom_file {
    void *chd;
    cdrom_toc cdtoc;
    uint32_t hunksectors;
    uint32_t cachehunk;
    unsigned char *cache;
} cdrom_file;

#endif
