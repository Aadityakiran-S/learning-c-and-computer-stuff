#include <stdio.h>
#include <windows.h>
#include "functions.h"

int main()
{
    overwriteFile("example.txt", "Hey guys! I'm a file.\n");
    readFile("example.txt");
    appendToFile("example.txt", "Oh, it seems here that I have some content added to me. Yay!.\n");
    readFile("example.txt");
    return 0;
}