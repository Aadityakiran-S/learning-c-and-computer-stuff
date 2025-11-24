#ifndef FUNCTIONS_H
#define FUNCTIONS_H

void printHello(); 
void printMessage(const char* message);
void printCustomGreeting(const char* name);
void readFile(const char* fileName);
void overwriteFile(const char* fileName, const char* content);
void overwriteFileRaw(const char* fileName, const char* content, size_t length);
void appendToFile(const char* fileName, const char* contentToAppend);

#endif // FUNCTIONS_H