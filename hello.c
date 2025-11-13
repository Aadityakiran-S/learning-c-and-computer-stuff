#include <stdio.h>
#include <windows.h>

#pragma region function_prototypes
void printHello(); 
void printMessage(const char* message);
void printCustomGreeting(const char* name);
void readFile(const char* fileName);
void readFileRawDog(const char* fileName);
#pragma endregion

int main()
{
    readFileRawDog("example.txt");
    return 0;
}

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