#include "functions.h"
#include <stdio.h>
#include <windows.h>

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

void appendToFile(const char* fileName, const char* contentToAppend){
    FILE* fp = fopen(fileName, "a");
    if(fp == NULL){
        perror("Error opening file for appending"); return;
    }

    fprintf(fp, "%s", contentToAppend);
    fclose(fp);
}

void readFileRawDog(const char* fileName){
    // Open the file
    HANDLE hFile = CreateFileA(
        fileName,                    // File name
        GENERIC_READ,                // Read access
        FILE_SHARE_READ,             // Allow others to read
        NULL,                        // Default security
        OPEN_EXISTING,               // File must exist
        FILE_ATTRIBUTE_NORMAL,       // Normal file
        NULL                         // No template
    );
    
    if(hFile == INVALID_HANDLE_VALUE){
        // Error handling without perror
        return;
    }
    
    char buffer[256];
    DWORD bytesRead;
    
    // Read file in chunks
    while(ReadFile(hFile, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0){
        buffer[bytesRead] = '\0';  // Null-terminate
        
        // Write to console (stdout)
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD bytesWritten;
        WriteFile(hConsole, buffer, bytesRead, &bytesWritten, NULL);
    }
    
    CloseHandle(hFile);
}
#pragma endregion