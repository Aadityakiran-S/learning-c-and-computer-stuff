#include "tar.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

#pragma region internal_helpers

/*
 * Compute the checksum for a tar header.
 *
 * The rule is:
 *   1. Treat the entire 512-byte header as an array of unsigned bytes.
 *   2. Sum all 512 bytes, BUT treat the 8-byte checksum field (bytes 148–155)
 *      as if they were all ASCII spaces (0x20 = 32).
 *   3. Store the result as a 6-digit octal string, followed by '\0' and ' '.
 *
 * Why spaces? Because when the header is first being built, the checksum
 * field doesn't have a value yet. By convention, we fill it with spaces
 * before computing the sum. This way both the creator and the reader
 * compute the same value.
 */
static void calculate_checksum(struct tar_header *header) {
    unsigned char *bytes = (unsigned char *)header;
    unsigned int sum = 0;

    /* First, fill the checksum field with spaces (the "blank" state). */
    memset(header->checksum, ' ', sizeof(header->checksum));

    /* Sum every byte in the 512-byte header. */
    for (int i = 0; i < TAR_BLOCK_SIZE; i++) {
        sum += bytes[i];
    }

    /* Write the checksum as a 6-digit octal number, null-terminated.
     * The format "%06o" gives us zero-padded octal. The 7th byte is '\0'
     * from snprintf, and the 8th byte is already a space from memset. */
    snprintf(header->checksum, sizeof(header->checksum), "%06o", sum);
}

/*
 * Verify the checksum of a header we just read from disk.
 * Returns 1 if valid, 0 if not.
 */
static int verify_checksum(struct tar_header *header) {
    /* Save the checksum that was stored in the file. */
    char saved_checksum[8];
    memcpy(saved_checksum, header->checksum, 8);

    /* Recompute the checksum (this overwrites header->checksum). */
    calculate_checksum(header);

    /* Compare: we only check the first 6 octal digits + null. */
    int valid = (strncmp(saved_checksum, header->checksum, 6) == 0);

    /* Restore the original checksum field. */
    memcpy(header->checksum, saved_checksum, 8);

    return valid;
}

/*
 * Check if a header block is all zeros (= end-of-archive marker).
 * A tar archive ends with two consecutive 512-byte blocks of zeros.
 */
static int is_end_of_archive(const struct tar_header *header) {
    const unsigned char *bytes = (const unsigned char *)header;
    for (int i = 0; i < TAR_BLOCK_SIZE; i++) {
        if (bytes[i] != 0) {
            return 0;  /* Found a non-zero byte, not end of archive. */
        }
    }
    return 1;  /* All zeros — this is an end-of-archive block. */
}

/*
 * Fill in a tar header for a given file.
 *
 * We use stat() to get the file's size and modification time,
 * then pack everything into the 512-byte header struct.
 */
static int fill_header(struct tar_header *header, const char *file_name) {
    /* Zero out the entire header first. This ensures all padding
     * and unused fields are clean zeros. */
    memset(header, 0, sizeof(struct tar_header));

    /* --- File name --- */
    /* If the name is longer than 99 chars, it won't fit. Real tar
     * implementations use the prefix field or extended headers for
     * long names. We keep it simple here. */
    if (strlen(file_name) >= sizeof(header->name)) {
        fprintf(stderr, "Error: file name '%s' is too long (max 99 chars)\n",
                file_name);
        return -1;
    }
    strncpy(header->name, file_name, sizeof(header->name) - 1);

    /* --- Get file metadata with stat() --- */
    struct stat file_stat;
    if (stat(file_name, &file_stat) != 0) {
        perror("stat failed");
        return -1;
    }

    /* --- Numeric fields are stored as octal ASCII strings --- */
    snprintf(header->mode,  sizeof(header->mode),  "%07o", 0644);
    snprintf(header->uid,   sizeof(header->uid),    "%07o", 0);
    snprintf(header->gid,   sizeof(header->gid),    "%07o", 0);
    snprintf(header->size,  sizeof(header->size),   "%011o", (unsigned int)file_stat.st_size);
    snprintf(header->mtime, sizeof(header->mtime),  "%011o", (unsigned int)file_stat.st_mtime);

    /* --- Type flag --- */
    header->typeflag = TAR_TYPEFLAG_FILE;  /* '0' = regular file */

    /* --- UStar magic and version --- */
    memcpy(header->magic, TAR_MAGIC, 5);   /* "ustar" (5 chars + null) */
    header->magic[5] = '\0';
    header->version[0] = '0';
    header->version[1] = '0';

    /* --- Owner names (just cosmetic) --- */
    strncpy(header->uname, "user", sizeof(header->uname) - 1);
    strncpy(header->gname, "group", sizeof(header->gname) - 1);

    /* --- Compute and store checksum (must be done LAST) --- */
    calculate_checksum(header);

    return 0;
}

#pragma endregion

#pragma region public_api

/*
 * tar_create — Create a .tar archive from a list of files.
 *
 * Algorithm:
 *   For each input file:
 *     1. Build a 512-byte header (fill_header).
 *     2. Write the header to the archive.
 *     3. Read the file's contents and write them to the archive.
 *     4. Pad the file data with zeros to the next 512-byte boundary.
 *        (Tar requires every block to be exactly 512 bytes.)
 *   After all files:
 *     5. Write 1024 bytes of zeros (two empty blocks) as the
 *        end-of-archive marker.
 */
int tar_create(const char *archive_name,
               const char **file_names,
               int file_count) {

    FILE *archive = fopen(archive_name, "wb");  /* "wb" = write binary */
    if (!archive) {
        perror("Error creating archive");
        return -1;
    }

    for (int i = 0; i < file_count; i++) {
        printf("  Adding: %s\n", file_names[i]);

        /* --- Step 1: Build the header --- */
        struct tar_header header;
        if (fill_header(&header, file_names[i]) != 0) {
            fclose(archive);
            return -1;
        }

        /* --- Step 2: Write the 512-byte header --- */
        fwrite(&header, 1, TAR_BLOCK_SIZE, archive);

        /* --- Step 3: Read the source file and write its contents --- */
        FILE *input = fopen(file_names[i], "rb");
        if (!input) {
            perror("Error opening input file");
            fclose(archive);
            return -1;
        }

        char buffer[TAR_BLOCK_SIZE];
        size_t bytes_read;
        size_t total_written = 0;

        while ((bytes_read = fread(buffer, 1, TAR_BLOCK_SIZE, input)) > 0) {
            /*
             * Step 4: If this is the last chunk and it's not a full 512 bytes,
             * we zero out the rest of the buffer before writing. This ensures
             * the file data is padded to a 512-byte boundary.
             */
            if (bytes_read < TAR_BLOCK_SIZE) {
                memset(buffer + bytes_read, 0, TAR_BLOCK_SIZE - bytes_read);
            }
            fwrite(buffer, 1, TAR_BLOCK_SIZE, archive);
            total_written += bytes_read;
        }

        fclose(input);
    }

    /* --- Step 5: Write end-of-archive marker (two 512-byte zero blocks) --- */
    char end_marker[TAR_BLOCK_SIZE];
    memset(end_marker, 0, TAR_BLOCK_SIZE);
    fwrite(end_marker, 1, TAR_BLOCK_SIZE, archive);
    fwrite(end_marker, 1, TAR_BLOCK_SIZE, archive);

    fclose(archive);
    printf("Archive '%s' created successfully.\n", archive_name);
    return 0;
}

/*
 * tar_list — List the contents of a .tar archive.
 *
 * Algorithm:
 *   Loop:
 *     1. Read a 512-byte header block.
 *     2. If it's all zeros, we've hit the end-of-archive marker. Stop.
 *     3. Parse the file name and size from the header.
 *     4. Print them out.
 *     5. Skip over the file data blocks (we don't need to read them,
 *        just advance the file pointer past them).
 *     6. Go back to step 1 for the next file.
 */
int tar_list(const char *archive_name) {
    FILE *archive = fopen(archive_name, "rb");
    if (!archive) {
        perror("Error opening archive");
        return -1;
    }

    printf("Contents of '%s':\n", archive_name);
    printf("%-40s %10s\n", "FILE NAME", "SIZE");
    printf("%-40s %10s\n", "----------------------------------------", "----------");

    struct tar_header header;

    while (fread(&header, 1, TAR_BLOCK_SIZE, archive) == TAR_BLOCK_SIZE) {
        /* Step 2: Check for end-of-archive. */
        if (is_end_of_archive(&header)) {
            break;
        }

        /* Verify checksum to catch corrupted headers. */
        if (!verify_checksum(&header)) {
            fprintf(stderr, "Warning: bad checksum for '%s'\n", header.name);
        }

        /* Step 3: Parse the file size from the octal ASCII string.
         * strtoul(str, NULL, 8) converts an octal string to unsigned long. */
        unsigned long file_size = strtoul(header.size, NULL, 8);

        /* Step 4: Print file info. */
        printf("%-40s %10lu bytes\n", header.name, file_size);

        /* Step 5: Skip over the file's data blocks.
         * The data occupies ceil(file_size / 512) blocks of 512 bytes each.
         * Integer trick: (n + 511) / 512 computes ceil(n / 512). */
        unsigned long blocks_to_skip = (file_size + TAR_BLOCK_SIZE - 1) / TAR_BLOCK_SIZE;
        fseek(archive, (long)(blocks_to_skip * TAR_BLOCK_SIZE), SEEK_CUR);
    }

    fclose(archive);
    return 0;
}

/*
 * tar_extract — Extract all files from a .tar archive.
 *
 * Algorithm:
 *   Loop:
 *     1. Read a 512-byte header block.
 *     2. If it's all zeros, we've hit end-of-archive. Stop.
 *     3. Parse file name and size from the header.
 *     4. Open/create the output file.
 *     5. Read the file data blocks from the archive and write them
 *        to the output file. Be careful on the last block: only write
 *        the actual remaining bytes, not the full 512-byte block
 *        (to avoid writing padding zeros into the file).
 *     6. Close the output file and go back to step 1.
 */
int tar_extract(const char *archive_name) {
    FILE *archive = fopen(archive_name, "rb");
    if (!archive) {
        perror("Error opening archive");
        return -1;
    }

    struct tar_header header;

    while (fread(&header, 1, TAR_BLOCK_SIZE, archive) == TAR_BLOCK_SIZE) {
        /* Step 2: Check for end-of-archive. */
        if (is_end_of_archive(&header)) {
            break;
        }

        /* Verify checksum. */
        if (!verify_checksum(&header)) {
            fprintf(stderr, "Warning: bad checksum for '%s', skipping\n",
                    header.name);
            /* Skip the data blocks and continue to next entry. */
            unsigned long file_size = strtoul(header.size, NULL, 8);
            unsigned long blocks = (file_size + TAR_BLOCK_SIZE - 1) / TAR_BLOCK_SIZE;
            fseek(archive, (long)(blocks * TAR_BLOCK_SIZE), SEEK_CUR);
            continue;
        }

        /* Step 3: Parse file name and size. */
        unsigned long file_size = strtoul(header.size, NULL, 8);
        printf("  Extracting: %-30s (%lu bytes)\n", header.name, file_size);

        /* Step 4: Create the output file. */
        FILE *output = fopen(header.name, "wb");
        if (!output) {
            perror("Error creating output file");
            fclose(archive);
            return -1;
        }

        /* Step 5: Read data blocks and write to output file. */
        unsigned long remaining = file_size;
        char buffer[TAR_BLOCK_SIZE];

        while (remaining > 0) {
            /* Always read a full 512-byte block from the archive
             * (that's how tar stores data). */
            size_t bytes_read = fread(buffer, 1, TAR_BLOCK_SIZE, archive);
            if (bytes_read != TAR_BLOCK_SIZE) {
                fprintf(stderr, "Error: unexpected end of archive\n");
                fclose(output);
                fclose(archive);
                return -1;
            }

            /* But only write the actual file bytes, not the padding.
             * On the last block, 'remaining' may be less than 512. */
            size_t bytes_to_write = (remaining < TAR_BLOCK_SIZE)
                                     ? remaining
                                     : TAR_BLOCK_SIZE;
            fwrite(buffer, 1, bytes_to_write, output);
            remaining -= bytes_to_write;
        }

        /* Step 6: Close the extracted file. */
        fclose(output);
    }

    fclose(archive);
    printf("Extraction complete.\n");
    return 0;
}

#pragma endregion
