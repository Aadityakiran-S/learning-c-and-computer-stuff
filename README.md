
# learning-c-and-computer-stuff

This is the repository that will be the cornerstone for me to get into systems engineering.

## Current State

- Modular C project with:
	- `hello.c`: main program
	- `functions.c`/`functions.h`: file operation functions (read, write, append)
	- `example.txt`: sample file for testing

- Compiles and runs successfully using:

```sh
gcc hello.c functions.c -o hello.exe
./hello.exe
```

## Next Steps: Learn tar/untar in C

1. Research the tar file format (structure, headers, how files are stored).
2. Try writing a simple C program to:
	- List files in a tar archive
	- Extract a file from a tar archive
3. Consider using existing libraries (e.g., `libtar`) or POSIX functions for file handling.
4. Document your findings and code in this project.

---

*This project is for learning and experimenting with C and file operations. Next goal: understand and implement tar/untar functionality in C.*
