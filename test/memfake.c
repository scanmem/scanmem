/*
    Provide a simple program to run test scans on

    Copyright (C) 2017 Andrea Stacchiotti  <andreastacchiotti(a)gmail.com>

    This library is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published
    by the Free Software Foundation; either version 3 of the License, or
    (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this library.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <assert.h>
#include <stdint.h>
#include <signal.h>

static volatile sig_atomic_t got_mutate = 0;

static void on_mutate(int sig) { (void)sig; got_mutate = 1; }

/* Legacy positional usage is kept as-is because the old smoke test used it:
       memfake [MB] [randomness]
   The plant modes are the useful ones for asserting on scan results, they
   put a known value in memory a known number of times so a test can say
   "this scan must return exactly N" instead of just "it didn't crash". */

static void usage(const char *argv0)
{
    fprintf(stderr,
        "usage: %s [MB] [randomness]\n"
        "       %s --plant VALUE --count N [--width 1|2|4|8] [--mb N]\n"
        "       %s --plant-float VALUE --count N [--width 4|8] [--mb N]\n"
        "       %s --plant-bytes HEXSTRING --count N [--mb N]\n"
        "\n"
        "Prints its own pid on stdout, then waits. Kill it when done.\n",
        argv0, argv0, argv0, argv0);
}

/* filler that is guaranteed not to collide with whatever we plant. we scan
   the buffer afterwards and bump it until nothing matches, brute force but
   this is a test helper and it runs once */
static uint64_t pick_filler(uint64_t planted, unsigned width)
{
    uint64_t f = 0x5a5a5a5a5a5a5a5aULL;
    uint64_t mask = (width >= 8) ? ~0ULL : ((1ULL << (width * 8)) - 1);

    while ((f & mask) == (planted & mask))
        f += 0x0101010101010101ULL;

    return f;
}

static void fill(void *base, size_t bytes, uint64_t pattern, unsigned width)
{
    unsigned char *p = base;
    for (size_t off = 0; off + width <= bytes; off += width)
        memcpy(p + off, &pattern, width);
}

/* ready file lets the harness wait for us instead of sleeping and hoping */
static void signal_ready(const char *path)
{
    if (!path) return;
    FILE *f = fopen(path, "w");
    if (f) { fprintf(f, "%d\n", (int)getpid()); fclose(f); }
}

int main(int argc, char **argv)
{
    size_t MB_to_allocate = 1;
    bool add_randomness = false;

    uint64_t plant = 0;
    double plant_f = 0;
    unsigned char plant_bytes[64];
    size_t plant_bytes_len = 0;
    size_t plant_count = 0;
    unsigned width = 4;
    enum { MODE_LEGACY, MODE_INT, MODE_FLOAT, MODE_BYTES } mode = MODE_LEGACY;
    const char *ready_path = NULL;
    const char *done_path = NULL;

    if (argc >= 2 && argv[1][0] == '-') {
        for (int i = 1; i < argc; i++) {
            const char *a = argv[i];
            const char *next = (i + 1 < argc) ? argv[i + 1] : NULL;

            if (!strcmp(a, "--plant") && next) { plant = strtoull(next, NULL, 0); mode = MODE_INT; i++; }
            else if (!strcmp(a, "--plant-float") && next) { plant_f = strtod(next, NULL); mode = MODE_FLOAT; i++; }
            else if (!strcmp(a, "--plant-bytes") && next) {
                mode = MODE_BYTES;
                size_t n = strlen(next);
                if (n % 2 || n / 2 > sizeof(plant_bytes)) { usage(argv[0]); return 1; }
                for (size_t k = 0; k < n; k += 2) {
                    char b[3] = { next[k], next[k + 1], 0 };
                    plant_bytes[k / 2] = (unsigned char)strtoul(b, NULL, 16);
                }
                plant_bytes_len = n / 2;
                i++;
            }
            else if (!strcmp(a, "--count") && next) { plant_count = strtoul(next, NULL, 10); i++; }
            else if (!strcmp(a, "--width") && next) { width = strtoul(next, NULL, 10); i++; }
            else if (!strcmp(a, "--mb") && next) { MB_to_allocate = strtoul(next, NULL, 10); i++; }
            else if (!strcmp(a, "--ready-file") && next) { ready_path = next; i++; }
            else if (!strcmp(a, "--done-file") && next) { done_path = next; i++; }
            else { usage(argv[0]); return 1; }
        }
        if (mode == MODE_INT || mode == MODE_FLOAT) {
            if (width != 1 && width != 2 && width != 4 && width != 8) { usage(argv[0]); return 1; }
            if (mode == MODE_FLOAT && width != 4 && width != 8) { usage(argv[0]); return 1; }
        }
        if (!plant_count) { usage(argv[0]); return 1; }
    } else {
        if (argc >= 2) MB_to_allocate = strtoul(argv[1], NULL, 10);
        if (argc >= 3) add_randomness = strtoul(argv[2], NULL, 10);
        if (argc >= 4) return 1;
    }

    size_t total_bytes = MB_to_allocate * 1024 * 1024;

    if (mode == MODE_LEGACY) {
        size_t array_size = total_bytes / sizeof(int);
        int *array = calloc(array_size, sizeof(int));
        assert(array != NULL);

        // Fill half with random values and leave an half of zeroes, if asked to
        if (add_randomness) {
            srand(time(NULL));
            for (size_t i = 0; i < array_size/2; i++) {
                array[i] = rand();
            }
        }

        printf("%d\n", (int)getpid());
        fflush(stdout);
        signal_ready(ready_path);
        pause();

        free(array);
        return 0;
    }

    unsigned char *buf = malloc(total_bytes);
    assert(buf != NULL);

    if (mode == MODE_BYTES) {
        /* fill with something that can't contain the needle, then stamp it in */
        memset(buf, 0x5a, total_bytes);
        if (plant_bytes_len && plant_bytes[0] == 0x5a) memset(buf, 0x17, total_bytes);

        size_t stride = total_bytes / (plant_count ? plant_count : 1);
        if (stride < plant_bytes_len) { fprintf(stderr, "memfake: buffer too small for that count\n"); return 1; }
        for (size_t i = 0; i < plant_count; i++)
            memcpy(buf + i * stride, plant_bytes, plant_bytes_len);
    } else {
        uint64_t pattern;
        if (mode == MODE_FLOAT) {
            if (width == 4) { float f = (float)plant_f; pattern = 0; memcpy(&pattern, &f, 4); }
            else { double d = plant_f; memcpy(&pattern, &d, 8); }
        } else {
            pattern = plant;
        }

        uint64_t filler = pick_filler(pattern, width);
        fill(buf, total_bytes, filler, width);

        /* space the planted values out so they land in different pages, that
           way a batched read has to actually gather from several places */
        size_t slots = total_bytes / width;
        size_t stride = slots / (plant_count ? plant_count : 1);
        if (!stride) { fprintf(stderr, "memfake: buffer too small for that count\n"); return 1; }
        for (size_t i = 0; i < plant_count; i++)
            memcpy(buf + (i * stride) * width, &pattern, width);
    }

    signal(SIGUSR1, on_mutate);

    printf("%d\n", (int)getpid());
    fflush(stdout);
    signal_ready(ready_path);

    /* SIGUSR1 rewrites only the slots we planted, so a narrowing scan after
       the signal should come back with exactly plant_count. the process's own
       copies of the value (locals, spilled registers) keep the old value and
       drop out of the match set, which is what makes the count exact. */
    for (;;) {
        pause();
        if (!got_mutate) continue;
        got_mutate = 0;

        if (mode == MODE_BYTES) {
            size_t stride = total_bytes / (plant_count ? plant_count : 1);
            unsigned char flipped[sizeof(plant_bytes)];
            memcpy(flipped, plant_bytes, plant_bytes_len);
            flipped[0] = (unsigned char)(flipped[0] ^ 0xff);
            for (size_t i = 0; i < plant_count; i++)
                memcpy(buf + i * stride, flipped, plant_bytes_len);
        } else {
            uint64_t pattern2;
            if (mode == MODE_FLOAT) {
                if (width == 4) { float f = (float)plant_f + 1.0f; pattern2 = 0; memcpy(&pattern2, &f, 4); }
                else { double d = plant_f + 1.0; memcpy(&pattern2, &d, 8); }
            } else {
                pattern2 = plant + 1;
            }
            size_t slots = total_bytes / width;
            size_t stride = slots / (plant_count ? plant_count : 1);
            for (size_t i = 0; i < plant_count; i++)
                memcpy(buf + (i * stride) * width, &pattern2, width);
        }

        /* tell the harness the rewrite finished. without this the next scan
           can attach (which SIGSTOPs us) while we are still partway through,
           and the narrow then sees a mix of old and new values */
        signal_ready(done_path);
    }

    free(buf);
    return 0;
}
