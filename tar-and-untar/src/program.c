#include <stdio.h>
#include <direct.h>
#include "functions.h"
#include "tar.h"

#define OUTPUT_DIR "output"

int main()
{
    /* Create the output directory (no-op if it already exists) */
    _mkdir(OUTPUT_DIR);

    /* --- Step 1: Create some sample files to archive --- */
    printf("=== Creating sample files ===\n");
    overwriteFile(OUTPUT_DIR "/file1.txt", "Hello from file1!\nThis is the first file.\n");
    overwriteFile(OUTPUT_DIR "/file2.txt", "Greetings from file2!\nThis is the second file.\nIt has three lines.\n");
    overwriteFile(OUTPUT_DIR "/file3.txt", "File number three here.\n");

    printf("Created 3 sample files.\n\n");

    /* --- Step 2: Pack them into a .tar archive --- */
    printf("=== Creating tar archive ===\n");
    const char *files_to_tar[] = {
        OUTPUT_DIR "/file1.txt",
        OUTPUT_DIR "/file2.txt",
        OUTPUT_DIR "/file3.txt"
    };
    tar_create(OUTPUT_DIR "/my_archive.tar", files_to_tar, 3);
    printf("\n");

    /* --- Step 3: List the archive contents (like `tar -tf`) --- */
    printf("=== Listing archive contents ===\n");
    tar_list(OUTPUT_DIR "/my_archive.tar");
    printf("\n");

    /* --- Step 4: Delete the original files so we can prove extraction works --- */
    printf("=== Deleting original files ===\n");
    remove(OUTPUT_DIR "/file1.txt");
    remove(OUTPUT_DIR "/file2.txt");
    remove(OUTPUT_DIR "/file3.txt");
    printf("Original files deleted.\n\n");

    /* --- Step 5: Extract files back from the archive --- */
    printf("=== Extracting from archive ===\n");
    tar_extract(OUTPUT_DIR "/my_archive.tar");
    printf("\n");

    /* --- Step 6: Verify by reading the extracted files --- */
    printf("=== Verifying extracted files ===\n");
    printf("--- file1.txt ---\n");
    readFile(OUTPUT_DIR "/file1.txt");
    printf("--- file2.txt ---\n");
    readFile(OUTPUT_DIR "/file2.txt");
    printf("--- file3.txt ---\n");
    readFile(OUTPUT_DIR "/file3.txt");

    return 0;
}