#include "functions.h"
#include <stdio.h>
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>

#pragma region helper_functions
void printHello()
{
    printf("Hello, World!\n");
}

void printMessage(const char* message)
{
    printf("%s\n", message);
}

void printCustomGreeting(const char* name){
    printf("Hello, %s! Nice to see you.\n", name);
}

void readFile(const char* fileName){
    printf("Address of fileName: %p\n", (void*)fileName);

    FILE* fp = fopen(fileName, "r"); 
    if(fp == NULL){
        perror("Error opening file"); return;
    }

    char buffer[256];
    while(fgets(buffer, sizeof(buffer), fp)){
        printf("%s", buffer);
    }
    printf("\n");
    fclose(fp);
}

void overwriteFile(const char* fileName, const char* content){
    FILE* fp = fopen(fileName, "w");
    if(fp == NULL){
        perror("Error opening file for writing"); return;
    }

    fprintf(fp, "%s", content);
    fclose(fp);
}

void overwriteFileRaw(const char *fileName, const char *content, size_t length){
    int fd = _open(fileName, _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY, _S_IREAD | _S_IWRITE);

    if(fd == -1){
        perror("Error opening file with raw descriptors"); return;
    }

    size_t total_written = 0;
    while(total_written < length){
        int bytes_to_write = (int)(length - total_written);
        int n = _write(fd, content + total_written, bytes_to_write);

        if(n < 0){
            if(errno == EINTR){
                continue; // Retry the write operation
            }
            perror("Error while writing to file");
            _close(fd);
            return;
        }

        if(n == 0){
            fprintf(stderr, "Warning: write returned 0 bytes, stopping\n");
            break;
        }
        total_written += n;
    }

    if(total_written < length){
        fprintf(stderr, "warning: only write %zu of %zu bytes\n", total_written, length);
    }

    if(_close(fd) == -1){
        perror("Error closing file descriptor"); return;
    }
}

void appendToFile(const char* fileName, const char* contentToAppend){
    FILE* fp = fopen(fileName, "a");
    if(fp == NULL){
        perror("Error opening file for appending"); return;
    }

    fprintf(fp, "%s", contentToAppend);
    fclose(fp);
}

#pragma endregion