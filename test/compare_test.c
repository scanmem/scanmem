/* Differential test of vector_compare() against the scalar block routines:
 * every number type, comparison and endianness, user values parsed the way
 * scanmem parses them, and memory full of the value, its neighbours and
 * special floats. Every position's flags must agree.
 * Built from the library sources (see Makefile.am), since the shared library
 * only exports sm_ symbols. usage: compare_test [rounds]
 * Exits 77 (skipped) where the CPU has no vector compare. */
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "common.h"
#include "scanmem.h"
#include "scanroutines.h"
#include "value.h"

#define BLOCKS 64
#define COUNT (BLOCKS * VECTOR_POSITIONS)

static uint64_t rng = 0x9e3779b97f4a7c15ULL;
static uint64_t next(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return rng;
}

/* random memory, seeded with what makes comparisons interesting */
static void fill(uint8_t *mem, size_t n, const uservalue_t *uv)
{
    for (size_t i = 0; i < n; i++)
        mem[i] = next();
    const float fs[] = {0.0f, -0.0f, NAN, -NAN, INFINITY, -INFINITY, 1e-45f, 1.5f, -2.5f};
    const double ds[] = {0.0, -0.0, NAN, -NAN, INFINITY, -INFINITY, 5e-324, 1.5, -2.5};
    for (size_t i = 0; i + 8 < n; i += 1 + next() % 6) {
        uint8_t tmp[8];
        uint64_t r = next();
        switch (r % 12) {
        case 0:
            memcpy(tmp, &uv->uint64_value, 8);
            break;
        case 1: {
            uint64_t v = uv->uint64_value + (r & 16 ? 1 : -1);
            memcpy(tmp, &v, 8);
            break;
        }
        case 2:
            memcpy(tmp, &uv->uint32_value, 4);
            memset(tmp + 4, r >> 8, 4);
            break;
        case 3: {
            uint32_t v = uv->uint32_value + (r & 16 ? 1 : -1);
            memcpy(tmp, &v, 4);
            memset(tmp + 4, 0, 4);
            break;
        }
        case 4:
            memcpy(tmp, &uv->uint16_value, 2);
            memset(tmp + 2, r >> 8, 6);
            break;
        case 5:
            tmp[0] = uv->uint8_value + (r >> 20) % 3 - 1;
            memset(tmp + 1, r >> 8, 7);
            break;
        case 6:
            memcpy(tmp, &uv->float32_value, 4);
            memcpy(tmp + 4, &fs[(r >> 8) % 9], 4);
            break;
        case 7:
            memcpy(tmp, &uv->float64_value, 8);
            break;
        case 8:
            memcpy(tmp, &ds[(r >> 8) % 9], 8);
            break;
        case 9:
            memset(tmp, (r >> 8) % 3 == 0 ? 0 : (r >> 8) % 3 == 1 ? 0xff
                                                                  : 0x80,
                   8);
            break;
        case 10:
            memcpy(tmp, &fs[(r >> 8) % 9], 4);
            memcpy(tmp + 4, &fs[(r >> 16) % 9], 4);
            break;
        default: { /* the value with its bytes reversed */
            uint64_t v = __builtin_bswap64(uv->uint64_value);
            if (r & 32) {
                uint32_t w = __builtin_bswap32(uv->uint32_value);
                memcpy(tmp, &w, 4);
                memset(tmp + 4, 0, 4);
            } else
                memcpy(tmp, &v, 8);
        }
        }
        memcpy(mem + i, tmp, MIN(8, n - i));
    }
}

int main(int argc, char **argv)
{
    int rounds = argc > 1 ? atoi(argv[1]) : 10;
    const char *values[] = {"0", "1", "-1", "2", "100", "127", "128", "200", "255", "256",
                            "-128", "-129", "300", "32767", "32768", "65535", "65536", "-32768",
                            "-32769", "2147483647", "2147483648", "4294967295", "4294967296",
                            "-2147483648", "-2147483649", "9223372036854775807",
                            "9223372036854775808", "18446744073709551615",
                            "-9223372036854775808", "1.5", "-0", "0.0", "-2.5", "200.0",
                            "1e10", "3.4e38", "1e300", "-1e300", "0.1", "1e-40"};
    const int nvalues = sizeof(values) / sizeof(values[0]);
    const scan_data_type_t dts[] = {ANYNUMBER, ANYINTEGER, ANYFLOAT, INTEGER8, INTEGER16,
                                    INTEGER32, INTEGER64, FLOAT32, FLOAT64};
    const scan_match_type_t mts[] = {MATCHEQUALTO, MATCHNOTEQUALTO, MATCHGREATERTHAN,
                                     MATCHLESSTHAN, MATCHRANGE};
    static uint8_t mem[COUNT + 8];
    static match_flags scalar[COUNT], vector[COUNT];
    long cases = 0, positions = 0, skipped = 0, failures = 0;

    vector_compare_t probe;
    uservalue_t one;
    memset(&one, 0, sizeof(one));
    if (!parse_uservalue_number("1", &one) ||
        !vector_compare_init(&probe, ANYINTEGER, MATCHEQUALTO, &one, false)) {
        printf("SKIP: no vector compare on this CPU\n");
        return 77;
    }

    for (int round = 0; round < rounds; round++) {
        for (int a = 0; a < nvalues; a++) {
            uservalue_t uv[2];
            memset(uv, 0, sizeof(uv));
            if (!parse_uservalue_number(values[a], &uv[0]))
                continue;
            int b = next() % nvalues;
            if (!parse_uservalue_number(values[b], &uv[1]))
                continue;
            /* as the default handler builds a range */
            if (uv[0].float64_value > uv[1].float64_value) {
                uservalue_t t = uv[0];
                uv[0] = uv[1];
                uv[1] = t;
            }
            for (size_t d = 0; d < sizeof(dts) / sizeof(dts[0]); d++)
                for (size_t m = 0; m < sizeof(mts) / sizeof(mts[0]); m++)
                    for (int rev = 0; rev < 2; rev++) {
                        uservalue_t use[2] = {uv[0], uv[1]};
                        if (mts[m] == MATCHRANGE)
                            use[0].flags &= use[1].flags;
                        if (!sm_choose_scanroutine(dts[d], mts[m], use, rev)) {
                            skipped++;
                            continue;
                        }
                        block_routine_t block = get_block_routine(sm_scan_routine);
                        vector_compare_t vc;
                        if (!block || !vector_compare_init(&vc, dts[d], mts[m], use, rev)) {
                            fprintf(stderr, "no vector form: dt %d mt %d\n", dts[d], mts[m]);
                            return 2;
                        }
                        fill(mem, sizeof(mem), &use[0]);
                        size_t fs = block(mem, COUNT, NULL, use, scalar);
                        size_t fv = vector_compare(&vc, mem, COUNT, vector);
                        cases++;
                        positions += COUNT;
                        if (fs != fv || memcmp(scalar, vector, sizeof(scalar))) {
                            failures++;
                            for (int k = 0; k < COUNT; k++)
                                if (scalar[k] != vector[k]) {
                                    fprintf(stderr, "DIFF value %s..%s dt %d mt %d rev %d pos %d: "
                                                    "scalar %04x vector %04x (found %zu vs %zu)\n",
                                            values[a], values[b], dts[d], mts[m], rev, k,
                                            scalar[k], vector[k], fs, fv);
                                    break;
                                }
                            if (failures > 20)
                                return 1;
                        }
                    }
        }
    }
    printf("%ld cases, %ld positions compared, %ld skipped (no routine), %ld failures\n",
           cases, positions, skipped, failures);
    return failures != 0;
}
