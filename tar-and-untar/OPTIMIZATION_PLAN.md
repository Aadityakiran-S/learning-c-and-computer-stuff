# Tar Performance Optimization Plan

We're going to take our working tar implementation and make it as fast as possible,
while learning what actually makes programs fast (and how to prove it with numbers).

The plan has **5 steps**. Each step builds on the previous one. After each step,
you should be able to measure the improvement against the baseline.

**Current state:** tar works correctly, reads/writes in 512-byte chunks using
`fread`/`fwrite`. That's our baseline.

---

## Table of Contents

1. [Step 1: Build a Timer + Test Harness](#step-1-build-a-timer--test-harness)
2. [Step 2: Larger I/O Buffers](#step-2-larger-io-buffers)
3. [Step 3: Raw Unbuffered I/O](#step-3-raw-unbuffered-io)
4. [Step 4: Memory-Mapped I/O](#step-4-memory-mapped-io)
5. [Step 5: Multi-Threaded Pipeline](#step-5-multi-threaded-pipeline)

---

## Step 1: Build a Timer + Test Harness

### What and why

Before changing anything, we need two things:

1. **A timer** — so we can measure how long each operation takes in milliseconds.
2. **A test harness** — a program that creates LARGE files (tens of megabytes),
   then tars/untars them and prints how long each operation took.

We need large files because our current test files are ~50 bytes — everything
finishes instantly. You can't optimize what you can't measure.

### The timer: `QueryPerformanceCounter`

On Windows, the best high-resolution timer is `QueryPerformanceCounter` from
`<windows.h>`. It gives you microsecond (or better) precision.

Here's how it works conceptually:

- Your CPU has a **hardware counter** that ticks at a very high frequency
  (e.g. 10 MHz = 10 million ticks per second).
- `QueryPerformanceFrequency()` tells you how many ticks happen per second.
- `QueryPerformanceCounter()` tells you the current tick count.
- To measure elapsed time: record the tick count before and after, subtract,
  then divide by the frequency to get seconds.

It's like a very precise stopwatch built into the CPU.

**Why not `clock()` from `time.h`?**  
`clock()` measures **CPU time** — time your program spent actually running on the
CPU. But our tar program spends most of its time *waiting for disk I/O*, not
computing. During that wait, the CPU is idle. `clock()` would report that as
nearly zero time, which is misleading. We want **wall-clock time** — the total
real-world time from start to finish, including I/O waits.

### Files to create

Create two new files: `src/timer.h` and `src/timer.c`.

#### `timer.h`

```c
#ifndef TIMER_H
#define TIMER_H

/*
 * A simple high-resolution wall-clock timer for Windows.
 *
 * Usage:
 *   timer t;
 *   timer_start(&t);
 *   // ... do work ...
 *   timer_stop(&t);
 *   printf("Took %.3f ms\n", timer_elapsed_ms(&t));
 */

/*
 * We need LARGE_INTEGER from <windows.h>, which is a union that can
 * hold a 64-bit integer. The performance counter uses 64-bit values
 * because a 32-bit counter at 10 MHz would overflow in ~7 minutes.
 *
 * We store:
 *   - start_count: the tick count when timer_start() was called
 *   - end_count:   the tick count when timer_stop() was called
 *   - frequency:   ticks per second (queried once at timer_start)
 */
#include <windows.h>

typedef struct {
    LARGE_INTEGER start_count;
    LARGE_INTEGER end_count;
    LARGE_INTEGER frequency;
} timer;

/* Record the current tick count as the start time. */
void timer_start(timer *t);

/* Record the current tick count as the end time. */
void timer_stop(timer *t);

/* Calculate elapsed time in milliseconds between start and stop. */
double timer_elapsed_ms(const timer *t);

#endif /* TIMER_H */
```

#### `timer.c`

```c
#include "timer.h"

/*
 * timer_start — Snapshot the current performance counter value.
 *
 * QueryPerformanceFrequency fills in `frequency` with the number of
 * counter ticks per second. This value is constant for the lifetime
 * of your system — it doesn't change with CPU speed (it's tied to a
 * fixed hardware oscillator, not the CPU clock).
 *
 * QueryPerformanceCounter fills in `start_count` with the current
 * tick value. Think of it as reading a stopwatch.
 */
void timer_start(timer *t) {
    QueryPerformanceFrequency(&t->frequency);
    QueryPerformanceCounter(&t->start_count);
}

/*
 * timer_stop — Snapshot the counter value again.
 *
 * The difference (end_count - start_count) gives us the number of
 * ticks that elapsed during the measured operation.
 */
void timer_stop(timer *t) {
    QueryPerformanceCounter(&t->end_count);
}

/*
 * timer_elapsed_ms — Convert elapsed ticks to milliseconds.
 *
 * The math:
 *   elapsed_ticks = end_count - start_count
 *   elapsed_seconds = elapsed_ticks / frequency
 *   elapsed_ms = elapsed_seconds * 1000.0
 *
 * We cast to double because integer division would lose precision.
 * The .QuadPart field accesses the 64-bit integer inside LARGE_INTEGER.
 *
 * Example: if frequency = 10,000,000 (10 MHz) and elapsed = 50,000 ticks:
 *   50000 / 10000000 = 0.005 seconds = 5.0 ms
 */
double timer_elapsed_ms(const timer *t) {
    double elapsed_ticks = (double)(t->end_count.QuadPart - t->start_count.QuadPart);
    double ticks_per_ms = (double)t->frequency.QuadPart / 1000.0;
    return elapsed_ticks / ticks_per_ms;
}
```

### The test harness: `benchmark.c`

This is a separate program (not `program.c`) that generates large test files
and measures how long tar operations take.

Create `src/benchmark.c`:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <direct.h>
#include "tar.h"
#include "timer.h"

#define OUTPUT_DIR "output"
#define BENCH_ARCHIVE OUTPUT_DIR "/benchmark.tar"

/*
 * generate_test_file — Create a file filled with a repeating pattern.
 *
 * We write in 4 KB chunks to make file generation itself fast.
 * The pattern is just repeating printable ASCII so the file isn't
 * all zeros (which could be special-cased by some systems).
 *
 * Parameters:
 *   path      — where to create the file
 *   size_mb   — how large to make it, in megabytes
 */
static void generate_test_file(const char *path, int size_mb) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        perror("Error creating test file");
        return;
    }

    /* Build a 4 KB chunk of repeating characters. */
    char chunk[4096];
    for (int i = 0; i < 4096; i++) {
        chunk[i] = 'A' + (i % 26);  /* A, B, C, ..., Z, A, B, ... */
    }

    /* Write enough chunks to reach the target size.
     * 1 MB = 1,048,576 bytes = 256 chunks of 4096 bytes. */
    long total_chunks = (long)size_mb * 256;
    for (long i = 0; i < total_chunks; i++) {
        fwrite(chunk, 1, sizeof(chunk), f);
    }

    fclose(f);
    printf("  Generated: %s (%d MB)\n", path, size_mb);
}

int main() {
    _mkdir(OUTPUT_DIR);

    /* ── Generate test files ── */
    printf("=== Generating test files ===\n");
    generate_test_file(OUTPUT_DIR "/big1.txt", 10);   /* 10 MB */
    generate_test_file(OUTPUT_DIR "/big2.txt", 10);   /* 10 MB */
    generate_test_file(OUTPUT_DIR "/big3.txt", 10);   /* 10 MB */
    printf("\n");

    const char *files[] = {
        OUTPUT_DIR "/big1.txt",
        OUTPUT_DIR "/big2.txt",
        OUTPUT_DIR "/big3.txt"
    };
    int file_count = 3;

    timer t;

    /* ── Benchmark: tar_create ── */
    printf("=== Benchmarking tar_create (3 x 10 MB files) ===\n");
    timer_start(&t);
    tar_create(BENCH_ARCHIVE, files, file_count);
    timer_stop(&t);
    printf("tar_create: %.3f ms\n\n", timer_elapsed_ms(&t));

    /* ── Benchmark: tar_list ── */
    printf("=== Benchmarking tar_list ===\n");
    timer_start(&t);
    tar_list(BENCH_ARCHIVE);
    timer_stop(&t);
    printf("tar_list: %.3f ms\n\n", timer_elapsed_ms(&t));

    /* ── Delete originals so extract has to recreate them ── */
    remove(OUTPUT_DIR "/big1.txt");
    remove(OUTPUT_DIR "/big2.txt");
    remove(OUTPUT_DIR "/big3.txt");

    /* ── Benchmark: tar_extract ── */
    printf("=== Benchmarking tar_extract ===\n");
    timer_start(&t);
    tar_extract(BENCH_ARCHIVE);
    timer_stop(&t);
    printf("tar_extract: %.3f ms\n\n", timer_elapsed_ms(&t));

    /* ── Cleanup ── */
    remove(OUTPUT_DIR "/big1.txt");
    remove(OUTPUT_DIR "/big2.txt");
    remove(OUTPUT_DIR "/big3.txt");
    remove(BENCH_ARCHIVE);
    printf("Cleaned up test files.\n");

    return 0;
}
```

### Build command for the benchmark

```
gcc -O2 -o tar-and-untar/output/benchmark.exe tar-and-untar/src/benchmark.c tar-and-untar/src/tar.c tar-and-untar/src/timer.c
```

Notes:
- We use `-O2` (optimization level 2) so the compiler generates reasonably
  optimized code. Without this, the compiler does zero optimization — it
  translates your C literally, which is slower. `-O2` enables things like
  inlining small functions, removing dead code, and reordering instructions.
  We want to measure I/O performance, not "how slow is unoptimized code."
- We do NOT link `functions.c` — the benchmark doesn't need it.

### What to do

1. Create `timer.h` and `timer.c` — type them out, don't copy-paste.
2. Create `benchmark.c` — type it out.
3. Build and run it.
4. Write down the numbers! These are your **baseline**.
5. Come back and tell me the numbers.

### Expected results

For 3 x 10 MB files with the current 512-byte buffer:
- `tar_create`: probably **100–500 ms** (lots of tiny reads + writes)
- `tar_list`: probably **< 1 ms** (just reads headers, skips data)
- `tar_extract`: probably **100–500 ms** (same issue as create)

The actual numbers depend on your disk speed (SSD vs HDD).

---

## Step 2: Larger I/O Buffers

### What and why

The current code reads and writes **512 bytes at a time** (= `TAR_BLOCK_SIZE`).
Every `fread()` / `fwrite()` call has overhead:

1. **Function call overhead** — pushing arguments, jumping to the function, returning.
2. **C library buffer management** — `fread` manages an internal buffer. When you
   ask for 512 bytes, it checks its buffer, may need to call the OS for more data.
3. **System call overhead** — When the C library's internal buffer runs out, it
   calls `ReadFile()` (Windows) or `read()` (Linux) to get more data from the OS.
   This means switching from user mode to kernel mode, which takes ~1 microsecond.
   That sounds tiny, but for a 10 MB file: 10,000,000 / 512 = ~19,531 calls.
   At 1 µs each, that's ~20 ms in pure syscall overhead alone.
4. **Disk I/O scheduling** — The OS batches and reorders I/O requests for efficiency.
   Larger requests give the disk controller more to work with, enabling techniques
   like read-ahead (speculatively loading the next chunk before you ask for it).

By reading/writing in larger chunks (e.g., 64 KB = 65,536 bytes = 128 tar blocks),
we reduce the number of function calls by 128x. The total data transferred is the
same, but the overhead per byte drops dramatically.

### The changes

The key insight: **the tar FORMAT requires 512-byte block alignment, but nothing
forces us to do I/O in 512-byte chunks**. We can read 64 KB at a time and still
write 512-byte-aligned blocks to the archive.

#### In `tar.h`, add a new constant:

```c
/*
 * I/O_BUFFER_SIZE controls how many bytes we read/write at once.
 * Must be a multiple of TAR_BLOCK_SIZE (512).
 *
 * Good values: 65536 (64 KB), 131072 (128 KB), 262144 (256 KB).
 * These are chosen because:
 *   - They're large enough to amortize syscall overhead.
 *   - They fit comfortably in the CPU's L2 cache (usually 256 KB–1 MB).
 *   - They match common OS page sizes and disk sector sizes well.
 *   - They're powers of 2 (makes alignment and division efficient).
 */
#define IO_BUFFER_SIZE (64 * 1024)   /* 64 KB */
```

#### Changes to `tar_create`:

```c
int tar_create(const char *archive_name,
               const char **file_names,
               int file_count) {

    FILE *archive = fopen(archive_name, "wb");
    if (!archive) {
        perror("Error creating archive");
        return -1;
    }

    /*
     * Use a large I/O buffer instead of a 512-byte one.
     * We still need to pad the LAST chunk to a 512-byte boundary,
     * but all the intermediate reads can be large.
     */
    char buffer[IO_BUFFER_SIZE];

    for (int i = 0; i < file_count; i++) {
        printf("  Adding: %s\n", file_names[i]);

        /* Build and write the 512-byte header (unchanged). */
        struct tar_header header;
        if (fill_header(&header, file_names[i]) != 0) {
            fclose(archive);
            return -1;
        }
        fwrite(&header, 1, TAR_BLOCK_SIZE, archive);

        /* Open the source file. */
        FILE *input = fopen(file_names[i], "rb");
        if (!input) {
            perror("Error opening input file");
            fclose(archive);
            return -1;
        }

        /*
         * Read in large chunks. The only tricky part is the last chunk:
         * we need to round it up to the next 512-byte boundary and
         * zero-fill the remainder.
         */
        size_t bytes_read;
        while ((bytes_read = fread(buffer, 1, IO_BUFFER_SIZE, input)) > 0) {

            /* Calculate how many bytes to actually write.
             * Round UP to the nearest 512-byte boundary.
             *
             * The formula (n + 511) & ~511 works because:
             *   ~511 in binary is ...11111111 00000000 0  (the bottom 9 bits cleared)
             *   Adding 511 first ensures we round UP, not down.
             *   The & then clears the bottom 9 bits, snapping to a 512 boundary.
             *
             * Example: bytes_read = 1000
             *   1000 + 511 = 1511
             *   1511 & ~511 = 1511 & 0xFFFFFE00 = 1024
             *   So we write 1024 bytes (1000 data + 24 zeros).
             *
             * Example: bytes_read = 1024  (already aligned)
             *   1024 + 511 = 1535
             *   1535 & ~511 = 1024
             *   No extra padding needed.
             */
            size_t padded_size = (bytes_read + TAR_BLOCK_SIZE - 1)
                                  & ~(size_t)(TAR_BLOCK_SIZE - 1);

            /* Zero out the padding area (between bytes_read and padded_size). */
            if (padded_size > bytes_read) {
                memset(buffer + bytes_read, 0, padded_size - bytes_read);
            }

            fwrite(buffer, 1, padded_size, archive);
        }

        fclose(input);
    }

    /* End-of-archive marker: two zero blocks. */
    memset(buffer, 0, TAR_BLOCK_SIZE * 2);
    fwrite(buffer, 1, TAR_BLOCK_SIZE * 2, archive);

    fclose(archive);
    printf("Archive '%s' created successfully.\n", archive_name);
    return 0;
}
```

**Wait — there's a subtle bug risk here.** In the original code, we padded every
512-byte chunk. In the new code, we only pad the *final* chunk of each file.
This is actually correct because:
- Intermediate chunks are full `IO_BUFFER_SIZE` bytes, and `IO_BUFFER_SIZE` is a
  multiple of 512, so they're already aligned.
- Only the last chunk can be non-aligned, and we pad that one.

#### Changes to `tar_extract`:

```c
int tar_extract(const char *archive_name) {
    FILE *archive = fopen(archive_name, "rb");
    if (!archive) {
        perror("Error opening archive");
        return -1;
    }

    struct tar_header header;
    char buffer[IO_BUFFER_SIZE];

    while (fread(&header, 1, TAR_BLOCK_SIZE, archive) == TAR_BLOCK_SIZE) {
        if (is_end_of_archive(&header)) {
            break;
        }

        if (!verify_checksum(&header)) {
            fprintf(stderr, "Warning: bad checksum for '%s', skipping\n",
                    header.name);
            unsigned long file_size = strtoul(header.size, NULL, 8);
            unsigned long blocks = (file_size + TAR_BLOCK_SIZE - 1) / TAR_BLOCK_SIZE;
            fseek(archive, (long)(blocks * TAR_BLOCK_SIZE), SEEK_CUR);
            continue;
        }

        unsigned long file_size = strtoul(header.size, NULL, 8);
        printf("  Extracting: %-30s (%lu bytes)\n", header.name, file_size);

        FILE *output = fopen(header.name, "wb");
        if (!output) {
            perror("Error creating output file");
            fclose(archive);
            return -1;
        }

        /*
         * The file data in the archive is stored as ceil(file_size / 512)
         * blocks of 512 bytes. We want to read it in large chunks for speed,
         * but we need to:
         *   1. Read exactly the right number of archive bytes (padded to 512).
         *   2. Write exactly file_size bytes to the output (no padding).
         */
        unsigned long remaining = file_size;

        /* Total bytes in the archive for this file (padded to 512). */
        unsigned long archive_bytes = ((file_size + TAR_BLOCK_SIZE - 1)
                                        / TAR_BLOCK_SIZE) * TAR_BLOCK_SIZE;
        unsigned long archive_remaining = archive_bytes;

        while (archive_remaining > 0) {
            /* Decide how much to read: either a full buffer or what's left. */
            size_t to_read = (archive_remaining < IO_BUFFER_SIZE)
                              ? archive_remaining
                              : IO_BUFFER_SIZE;

            size_t bytes_read = fread(buffer, 1, to_read, archive);
            if (bytes_read != to_read) {
                fprintf(stderr, "Error: unexpected end of archive\n");
                fclose(output);
                fclose(archive);
                return -1;
            }

            /* Write only actual file data, not padding.
             * On the last chunk, 'remaining' may be less than bytes_read. */
            size_t to_write = (remaining < bytes_read) ? remaining : bytes_read;
            if (to_write > 0) {
                fwrite(buffer, 1, to_write, output);
                remaining -= to_write;
            }

            archive_remaining -= bytes_read;
        }

        fclose(output);
    }

    fclose(archive);
    printf("Extraction complete.\n");
    return 0;
}
```

### What to do

1. Add `IO_BUFFER_SIZE` to `tar.h`.
2. Modify `tar_create` — replace the read/write loop with the large-buffer version.
3. Modify `tar_extract` — replace the read/write loop with the large-buffer version.
4. Build and run the benchmark.
5. Compare the numbers against your Step 1 baseline.

### Expected improvement

The improvement depends on file size and disk speed. Rough expectations:
- **10 MB files on SSD**: maybe 2–5x faster
- **10 MB files on HDD**: maybe 3–10x faster
- **100 MB files**: the difference becomes even more dramatic

You can also experiment with different `IO_BUFFER_SIZE` values (16 KB, 64 KB, 256 KB,
1 MB) and chart the results. There's usually a sweet spot — going beyond ~256 KB
often doesn't help much more.

---

## Step 3: Raw Unbuffered I/O

### What and why

So far we've used `fopen` / `fread` / `fwrite` — the **C standard library's
buffered I/O**. Under the hood, these functions maintain their own internal
buffer (usually 4–8 KB). When you call `fread(buf, 1, 64KB, file)`, the
library might issue multiple `ReadFile()` system calls internally to fill
that request.

The idea in this step: **bypass the C library's buffer entirely** and call the
OS directly. On Windows, that means `_open()` / `_read()` / `_write()` / `_close()`
from `<io.h>` (you already used `_open` in `functions.c`).

```
fread path:    your code → C library buffer → ReadFile() → kernel → disk
raw path:      your code → ReadFile() → kernel → disk
```

By managing the buffer ourselves (which we already do — that's our `buffer[IO_BUFFER_SIZE]`),
we eliminate one layer of copying.

### New version of `tar_create` with raw I/O

Here's the key I/O loop rewritten. The header logic stays the same — we only change
how we read the input file.

```c
#include <io.h>       /* _open, _read, _write, _close */
#include <fcntl.h>    /* _O_RDONLY, _O_WRONLY, _O_CREAT, _O_TRUNC, _O_BINARY */
#include <sys/stat.h> /* _S_IREAD, _S_IWRITE */

/*
 * Inside tar_create, replace the input file reading loop with:
 */

        /* Open the source file with raw I/O (no C library buffering). */
        int input_fd = _open(file_names[i], _O_RDONLY | _O_BINARY);
        if (input_fd == -1) {
            perror("Error opening input file");
            fclose(archive);  /* archive can still use fwrite for simplicity */
            return -1;
        }

        int bytes_read;
        while ((bytes_read = _read(input_fd, buffer, IO_BUFFER_SIZE)) > 0) {
            size_t padded_size = (bytes_read + TAR_BLOCK_SIZE - 1)
                                  & ~(size_t)(TAR_BLOCK_SIZE - 1);

            if (padded_size > (size_t)bytes_read) {
                memset(buffer + bytes_read, 0, padded_size - bytes_read);
            }

            fwrite(buffer, 1, padded_size, archive);
        }

        if (bytes_read < 0) {
            perror("Error reading input file");
            _close(input_fd);
            fclose(archive);
            return -1;
        }

        _close(input_fd);
```

### Why this might be faster (or might not)

**Might be faster because:**
- One less buffer copy. `fread` copies data from its internal buffer to your
  buffer. `_read` copies directly from the OS to your buffer.
- For large sequential reads, the OS's own read-ahead is usually sufficient.

**Might NOT be faster because:**
- `fread`'s internal buffer is actually quite efficient — it's been optimized
  for decades.
- The real bottleneck is the disk, not the CPU copying a few bytes in memory.
- On modern systems, the OS kernel also does its own buffering (the page cache),
  so data is often already in RAM by the time you ask for it.

**This step is primarily educational.** The goal is to understand the difference
between buffered and unbuffered I/O, not necessarily to get a big speedup.

### What to do

1. Modify `tar_create` to use `_open` / `_read` / `_close` for reading input files.
2. Optionally modify `tar_extract` to use `_open` / `_write` / `_close` for
   writing output files.
3. Benchmark. Compare against Step 2 results.

---

## Step 4: Memory-Mapped I/O

### What and why

This is the big conceptual leap. Instead of calling `read()` in a loop to copy
data from the OS into our buffer, we ask the OS to **map the file directly into
our process's memory address space**. After that, the file's contents are
accessible through a regular pointer — like a giant array.

```
Traditional I/O:
  disk → kernel page cache → user buffer (your char[64KB]) → you use it

Memory-mapped I/O:
  disk → kernel page cache → your pointer sees it directly (no copy!)
```

The OS handles everything lazily: it only loads pages from disk when you actually
access them (this is called a **page fault**, and it's how virtual memory works).
The OS's prefetcher will detect that you're reading sequentially and start loading
pages ahead of where you are.

On Windows, the API is:
1. `CreateFileA()` — open the file
2. `CreateFileMappingA()` — create a mapping object (tells the OS "I want this file
   in my address space")
3. `MapViewOfFile()` — actually map it into memory (gives you a pointer)
4. Use the pointer like a normal array
5. `UnmapViewOfFile()` — release the mapping
6. `CloseHandle()` — close the mapping and file objects

### Reading input files with memory mapping

Here's how `tar_create` would read each input file using memory mapping:

```c
#include <windows.h>

/*
 * Inside tar_create, replace the input file reading section with:
 */

        /* ── Open the file using Windows API ── */
        HANDLE hFile = CreateFileA(
            file_names[i],        /* File path                             */
            GENERIC_READ,         /* We only need to read                  */
            FILE_SHARE_READ,      /* Allow other processes to read too     */
            NULL,                 /* Default security                      */
            OPEN_EXISTING,        /* File must already exist               */
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
            /*
             * FILE_FLAG_SEQUENTIAL_SCAN is a hint to the OS:
             * "I'm going to read this file from start to end."
             * The OS uses this to aggressively prefetch data ahead
             * of where we're currently reading. This can make
             * sequential reads significantly faster.
             */
            NULL                  /* No template file                      */
        );

        if (hFile == INVALID_HANDLE_VALUE) {
            fprintf(stderr, "Error opening '%s'\n", file_names[i]);
            fclose(archive);
            return -1;
        }

        /* ── Get file size ── */
        LARGE_INTEGER file_size_li;
        GetFileSizeEx(hFile, &file_size_li);
        size_t file_size = (size_t)file_size_li.QuadPart;

        if (file_size > 0) {
            /* ── Create a file mapping object ── */
            HANDLE hMapping = CreateFileMappingA(
                hFile,            /* The file to map                       */
                NULL,             /* Default security                      */
                PAGE_READONLY,    /* We only need read access              */
                0, 0,             /* Map the entire file (0,0 = full size) */
                NULL              /* No name for the mapping               */
            );

            if (!hMapping) {
                fprintf(stderr, "Error creating file mapping\n");
                CloseHandle(hFile);
                fclose(archive);
                return -1;
            }

            /* ── Map the file into memory ── */
            const char *mapped_data = (const char *)MapViewOfFile(
                hMapping,         /* The mapping object                    */
                FILE_MAP_READ,    /* Read-only access                      */
                0, 0,             /* Start from offset 0                   */
                0                 /* Map the entire file                   */
            );

            if (!mapped_data) {
                fprintf(stderr, "Error mapping file into memory\n");
                CloseHandle(hMapping);
                CloseHandle(hFile);
                fclose(archive);
                return -1;
            }

            /*
             * Now `mapped_data` points to the file's contents in memory.
             * We can read from it like a normal array: mapped_data[0],
             * mapped_data[1], etc. — no fread() calls needed!
             *
             * Write it to the archive in large chunks:
             */
            size_t offset = 0;
            while (offset < file_size) {
                /* How much is left to write? */
                size_t remaining = file_size - offset;
                size_t chunk = (remaining < IO_BUFFER_SIZE) ? remaining : IO_BUFFER_SIZE;

                /* Write the chunk directly from the mapped memory. */
                fwrite(mapped_data + offset, 1, chunk, archive);
                offset += chunk;
            }

            /* Pad the last block to 512-byte boundary. */
            size_t tail = file_size % TAR_BLOCK_SIZE;
            if (tail != 0) {
                char padding[TAR_BLOCK_SIZE];
                memset(padding, 0, TAR_BLOCK_SIZE);
                fwrite(padding, 1, TAR_BLOCK_SIZE - tail, archive);
            }

            /* ── Cleanup ── */
            UnmapViewOfFile(mapped_data);
            CloseHandle(hMapping);
        }

        CloseHandle(hFile);
```

### Why this can be the fastest option

1. **Zero copy**: The OS maps the same physical memory pages that the kernel page
   cache already uses. There's no copying from kernel buffer → user buffer.
2. **Automatic prefetching**: Combined with `FILE_FLAG_SEQUENTIAL_SCAN`, the OS
   will read ahead aggressively.
3. **No syscall per read**: Once mapped, accessing the data is just a memory
   read — no function call, no mode switch.
4. **The TLB and page table do the work**: The CPU's memory management unit handles
   translating your virtual address to the physical page. On the first access to
   each page (4 KB), there's a page fault and the OS loads it from disk. After
   that, it's in RAM and access is instant.

### When it's NOT faster

- For very small files (< 4 KB), the setup cost of creating the mapping exceeds
  the savings.
- If the file is already fully in the OS page cache (because you just wrote it),
  both approaches read from RAM — the difference is minimal.
- If you're writing (not reading), memory mapping is more complex and not
  always faster.

### What to do

1. Modify `tar_create` to use memory-mapped I/O for reading input files.
2. Benchmark. Compare against Step 2 and Step 3 results.
3. For `tar_extract`, memory mapping is less useful (we're *writing* output files,
   not reading them), so you can leave extraction using the Step 2 approach.

---

## Step 5: Multi-Threaded Pipeline

### What and why

Up to this point, everything is **single-threaded**: we read a file, then write
to the archive, then read the next file, then write. The disk is idle while we're
writing, and vice versa.

A pipeline uses **two threads**:
- **Reader thread**: reads data from source files into shared buffers.
- **Writer thread**: writes from those shared buffers into the archive.

While the writer is writing chunk N, the reader can be reading chunk N+1. This
overlaps I/O operations and can reduce total time.

```
Single-threaded:
  [Read 1][Write 1][Read 2][Write 2][Read 3][Write 3]

Pipelined:
  Reader:  [Read 1][Read 2][Read 3]
  Writer:         [Write 1][Write 2][Write 3]
  Total time is shorter because operations overlap.
```

### The building blocks

This requires several new concepts:

1. **Threads** (`CreateThread` on Windows, or `_beginthreadex`)
2. **Mutexes** (mutual exclusion locks) — to protect shared data
3. **Condition variables** (or Events on Windows) — to signal between threads
4. **A shared buffer queue** — the reader puts full buffers in, the writer takes
   them out

Here's the architecture:

```
┌──────────┐    ┌─────────────────┐    ┌──────────┐
│  Reader  │───►│  Buffer Queue   │───►│  Writer  │
│  Thread  │    │  (mutex-guarded)│    │  Thread  │
└──────────┘    └─────────────────┘    └──────────┘
```

### Pseudocode

```
// Shared state
buffer_queue = empty queue of {data, length} pairs
mutex = new mutex
not_empty = new condition variable (signals "queue has items")
not_full  = new condition variable (signals "queue has space")
done_reading = false

// Reader thread
for each file:
    while data remains in file:
        chunk = read up to IO_BUFFER_SIZE bytes
        lock(mutex)
        while queue is full:
            wait(not_full, mutex)    // sleep until writer drains the queue
        enqueue(chunk)
        signal(not_empty)            // wake up writer
        unlock(mutex)
lock(mutex)
done_reading = true
signal(not_empty)
unlock(mutex)

// Writer thread
loop:
    lock(mutex)
    while queue is empty AND NOT done_reading:
        wait(not_empty, mutex)       // sleep until reader adds data
    if queue is empty AND done_reading:
        unlock(mutex)
        break                        // all done
    chunk = dequeue()
    signal(not_full)                 // wake up reader
    unlock(mutex)
    write chunk to archive
```

### Full implementation

This is the most complex step. I'll provide the full code here, but you should
implement it ONLY after steps 1–4 are done and understood.

I'm providing the conceptual structure rather than fully polished code because
this is the step where you should be thinking hardest about what each piece does.
When you get here, we'll work through it interactively.

**Key types and functions you'll need:**

```c
#include <windows.h>
#include <process.h>   /* _beginthreadex */

/* A single buffer in the queue. */
typedef struct {
    char data[IO_BUFFER_SIZE];
    size_t length;           /* actual bytes in this buffer                  */
    size_t padded_length;    /* length rounded up to 512 boundary            */
    int is_header;           /* 1 if this buffer contains a tar header       */
} buffer_entry;

/* The shared queue between reader and writer threads. */
#define QUEUE_CAPACITY 4     /* small queue — don't hog memory */

typedef struct {
    buffer_entry entries[QUEUE_CAPACITY];
    int head;                /* index of next item to dequeue                */
    int tail;                /* index of next slot to enqueue into           */
    int count;               /* current number of items in queue             */
    int done;                /* reader sets this when all files are read     */

    CRITICAL_SECTION cs;     /* mutex (Windows name for mutex)               */
    CONDITION_VARIABLE cv_not_empty;  /* "queue has items"                   */
    CONDITION_VARIABLE cv_not_full;   /* "queue has space"                   */
} thread_queue;
```

### When threading helps

- **Multiple files on different physical disks**: huge win.
- **SSD/NVMe drives**: the drive can handle overlapped I/O well.
- **Later, when you add compression**: the reader can read while the compressor
  thread compresses while the writer writes — true pipeline parallelism.

### When threading hurts

- **Single HDD, sequential access**: the disk arm has to seek between the
  source file and the archive file. Two threads cause more seeking = slower.
- **Small files**: thread creation and synchronization overhead exceeds the
  I/O time savings.
- **Complexity**: more code = more bugs. Race conditions are the hardest bugs
  to find in all of programming.

### What to do

1. Read the pseudocode until you understand the producer-consumer pattern.
2. Implement the queue, reader thread, and writer thread.
3. Benchmark. Compare against all previous steps.

---

## Summary: Expected Results Table

After all steps, you should have a table like this:

| Step | tar_create (ms) | tar_extract (ms) | Notes |
|------|-----------------|-------------------|-------|
| 1 (baseline, 512B buffer) | ??? | ??? | Your starting point |
| 2 (64 KB buffer) | ??? | ??? | Biggest single improvement |
| 3 (raw I/O) | ??? | ??? | Might be similar to step 2 |
| 4 (memory-mapped) | ??? | ??? | Possibly fastest for reads |
| 5 (multi-threaded) | ??? | ??? | Helps most with compression later |

Fill this in as you go. The actual numbers are the lesson — they teach you
which optimizations matter in practice and which are just "theoretically better."

---

## How to Proceed

1. Start with **Step 1**. Create `timer.h`, `timer.c`, and `benchmark.c`.
2. Build, run, record the baseline numbers.
3. Come back and share the numbers + any questions.
4. We'll discuss, then move to Step 2.
