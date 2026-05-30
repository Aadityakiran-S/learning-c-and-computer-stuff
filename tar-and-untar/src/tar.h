#ifndef TAR_H
#define TAR_H

#include <stddef.h>

/*
 * ============================================================================
 *  POSIX UStar Tar Header (512 bytes)
 * ============================================================================
 *
 *  A .tar file is simply a sequence of these blocks:
 *
 *    [512-byte header][file data padded to 512-byte boundary]
 *    [512-byte header][file data padded to 512-byte boundary]
 *    ...
 *    [1024 bytes of zeros = end-of-archive marker]
 *
 *  Every numeric field (mode, uid, gid, size, mtime, checksum) is stored
 *  as a null-terminated ASCII string in OCTAL — not binary.
 *  For example, a file that is 1024 bytes is stored as "0000002000\0" in
 *  the size field (2000 octal = 1024 decimal).
 *
 *  The checksum is computed by treating all 512 header bytes as unsigned
 *  chars and summing them, but treating the 8-byte checksum field itself
 *  as all spaces (0x20) during the calculation.
 */

/* This struct maps 1:1 onto the 512-byte tar header block on disk. */
struct tar_header {
    char name[100];      /* File name (null-terminated string)             */
    char mode[8];        /* File permissions in octal, e.g. "0000644\0"   */
    char uid[8];         /* Owner user ID in octal                        */
    char gid[8];         /* Owner group ID in octal                       */
    char size[12];       /* File size in bytes, in octal                  */
    char mtime[12];      /* Last modification time (Unix timestamp, octal)*/
    char checksum[8];    /* Header checksum (see calculation below)       */
    char typeflag;       /* '0' or '\0' = regular file, '5' = directory   */
    char linkname[100];  /* Name of linked file (for hard/sym links)      */
    char magic[6];       /* Must be "ustar" (with null terminator)        */
    char version[2];     /* Must be "00" (no null terminator)             */
    char uname[32];      /* Owner user name                               */
    char gname[32];      /* Owner group name                              */
    char devmajor[8];    /* Device major number (for special files)       */
    char devminor[8];    /* Device minor number (for special files)       */
    char prefix[155];    /* Prefix for file names longer than 100 chars   */
    char padding[12];    /* Unused padding to reach exactly 512 bytes     */
};

/* Sanity check: the header must be exactly 512 bytes.
 * If this fails, your compiler is adding padding — you'd need #pragma pack. */
_Static_assert(sizeof(struct tar_header) == 512, "tar header must be 512 bytes");

#define TAR_BLOCK_SIZE 512
#define TAR_TYPEFLAG_FILE '0'
#define TAR_TYPEFLAG_DIR  '5'
#define TAR_MAGIC "ustar"

/* ---- Public API ---- */

/*
 * tar_create: Pack one or more files into a .tar archive.
 *
 *   archive_name  — output .tar file path
 *   file_names    — array of input file paths to archive
 *   file_count    — number of entries in file_names
 *
 * Returns 0 on success, -1 on error.
 */
int tar_create(const char *archive_name,
               const char **file_names,
               int file_count);

/*
 * tar_list: Print the table of contents of a .tar archive (like `tar -tf`).
 *
 *   archive_name  — path to the .tar file
 *
 * Returns 0 on success, -1 on error.
 */
int tar_list(const char *archive_name);

/*
 * tar_extract: Extract all files from a .tar archive (like `tar -xf`).
 *
 *   archive_name  — path to the .tar file
 *
 * Returns 0 on success, -1 on error.
 */
int tar_extract(const char *archive_name);

#endif /* TAR_H */
