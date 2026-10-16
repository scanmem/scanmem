/*
    Definition of routines of scanning for different data types.

    Copyright (C) 2009,2010 WANG Lu  <coolwanglu(a)gmail.com>
    Copyright (C) 2015      Vyacheslav Shegai <v.shegai(a)netris.ru>

    This file is part of libscanmem.

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

#ifndef SCANROUTINES_H
#define SCANROUTINES_H

#include <stdbool.h>

#include "value.h"
#include "targetmem.h"

typedef enum {
    ANYNUMBER,              /* ANYINTEGER or ANYFLOAT */
    ANYINTEGER,             /* INTEGER of whatever width */
    ANYFLOAT,               /* FLOAT of whatever width */
    INTEGER8,
    INTEGER16,
    INTEGER32,
    INTEGER64,
    FLOAT32,
    FLOAT64,
    BYTEARRAY,
    STRING
} scan_data_type_t;

typedef enum {
    MATCHANY,                /* for snapshot */
    /* following: compare with a given value */
    MATCHEQUALTO,
    MATCHNOTEQUALTO,
    MATCHGREATERTHAN,
    MATCHLESSTHAN,
    MATCHRANGE,
    /* following: compare with the old value */
    MATCHUPDATE,
    MATCHNOTCHANGED,
    MATCHCHANGED,
    MATCHINCREASED,
    MATCHDECREASED,
    /* following: compare with both given value and old value */
    MATCHINCREASEDBY,
    MATCHDECREASEDBY
} scan_match_type_t;


/* Matches a memory area given by `memory_ptr` and `memlength` against `user_value` or `old_value`
 * (or both, depending on the matching type), stores the result into saveflags.
 * NOTE: saveflags must be set to 0, since only useful bits are set, but extra bits are not cleared!
 * Returns the number of bytes needed to store said match, 0 for not matched
 */
typedef unsigned int (*scan_routine_t)(const mem64_t *memory_ptr, size_t memlength,
                                       const value_t *old_value, const uservalue_t *user_value, match_flags *saveflags);
extern scan_routine_t sm_scan_routine;

/* 
 * Choose the global scanroutine according to the given parameters, sm_scan_routine will be set.
 * Returns whether a proper routine has been found.
 */
bool sm_choose_scanroutine(scan_data_type_t dt, scan_match_type_t mt, const uservalue_t* uval, bool reverse_endianness);

scan_routine_t sm_get_scanroutine(scan_data_type_t dt, scan_match_type_t mt, match_flags uflags, bool reverse_endianness);

/* The flags the routines for `dt` look at. */
match_flags scan_data_type_flags(scan_data_type_t dt);

/* A number scan routine run over `count` consecutive positions, inlined
 * instead of called for each. `flags[k]` gets what the routine sets for the
 * value at `memory + k`; the match length is the width of the widest flag.
 * `memory` must hold count + 7 bytes.
 * Without `old`, this is an initial search: the routine gets memlength 8, as
 * for any position with at least 8 bytes left in its region.
 * With `old`, the count + 7 elements of a swath starting at the address of
 * `memory`, this is a rescan: elements with flags are compared with the old
 * value data_to_val_aux() makes of them, with the memlength sm_checkmatches()
 * passes (the length of the widest old flag), and the others get no flags.
 * Returns how many positions got flags. */
typedef size_t (*block_routine_t)(const uint8_t *memory, size_t count,
                                  const old_value_and_match_info *old,
                                  const uservalue_t *user_value, match_flags *flags);

/* The block form of `routine`, or NULL if it has none. Only the number
 * routines that compare with a value or with the old value have one. */
block_routine_t get_block_routine(scan_routine_t routine);

/* The number routines that compare with a value (==, !=, >, <, range) on
 * vectors of VECTOR_POSITIONS positions, where the CPU can (AVX2).
 * vector_compare_init() sets `vc` up for the routine sm_choose_scanroutine()
 * picks for `dt`, `mt` and `reverse_endianness` with `uval`, or returns false.
 * vector_compare() then does what that routine's block form does without
 * `old`, for `count` positions, a multiple of VECTOR_POSITIONS: the same flags
 * for every position, and the number of positions that got any. */
#define VECTOR_POSITIONS 32

typedef struct {
    match_flags flags; /* the flags the routine can set with this value */
    scan_match_type_t mt;
    bool reverse;
    /* for flag bit b: the value (and a range's upper bound) as compared in
     * lanes: unsigned integers with the sign bit flipped, floats as bits */
    uint64_t lo[10], hi[10];
} vector_compare_t;

bool vector_compare_init(vector_compare_t *vc, scan_data_type_t dt, scan_match_type_t mt,
                         const uservalue_t *uval, bool reverse_endianness);
size_t vector_compare(const vector_compare_t *vc, const uint8_t *memory, size_t count,
                      match_flags *flags);

/* What the CHANGED, NOTCHANGED and UPDATE routines of the number types
 * compute, without a call per type and width: integers of a width are
 * unchanged exactly when their bytes are, and floats are compared as floats.
 * Takes what sm_checkmatches() passes those routines: the first 8 bytes of
 * memory in `now`, of which `memlength` are valid, and the old value's bytes
 * and flags (as data_to_val_aux() makes them). `allowed` is
 * scan_data_type_flags() of the scan data type. Bytes past `memlength` or past
 * the old value's flags do not change the result. */
/* the first `n` bytes of a uint64_t in memory, as a mask (n < 8) */
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#    define FIRST_BYTES_MASK(n) ((UINT64_C(1) << (8 * (n))) - 1)
#else
#    define FIRST_BYTES_MASK(n) (~(UINT64_MAX >> (8 * (n))))
#endif

static inline __attribute__((always_inline)) unsigned int
quick_old_value_routine(scan_match_type_t mt, match_flags allowed, uint64_t now, uint64_t before,
                        size_t memlength, match_flags old_flags, match_flags *saveflags)
{
    match_flags flags = old_flags & allowed;
    match_flags keep;
    uint64_t differ = now ^ before;

    /* widths that fit */
    keep = number_flags_within(memlength);
    if (mt != MATCHUPDATE) {
        bool want_same = mt == MATCHNOTCHANGED;
        match_flags same = flags_empty;
        if (((differ & FIRST_BYTES_MASK(1)) == 0) == want_same)
            same |= flags_i8b;
        if (((differ & FIRST_BYTES_MASK(2)) == 0) == want_same)
            same |= flags_i16b;
        if (((differ & FIRST_BYTES_MASK(4)) == 0) == want_same)
            same |= flags_i32b;
        if ((differ == 0) == want_same)
            same |= flags_i64b;
        if (flags & flag_f32b && memlength >= 4) {
            float a, b;
            memcpy(&a, &now, sizeof(a));
            memcpy(&b, &before, sizeof(b));
            if ((a == b) == want_same)
                same |= flag_f32b;
        }
        if (flags & flag_f64b && memlength >= 8) {
            double a, b;
            memcpy(&a, &now, sizeof(a));
            memcpy(&b, &before, sizeof(b));
            if ((a == b) == want_same)
                same |= flag_f64b;
        }
        keep &= same;
    }

    flags &= keep;
    *saveflags |= flags;
    return flags_number_width(flags);
}

/* Prefilter for equality scans: positions where the scan routine can match
 * hold one of a few known byte patterns, so a vectorized search for a rare one
 * or two byte window of each finds every candidate, and the routine only runs
 * on those. Positions that are not candidates never match. */
#define PREFILTER_MAX_WINDOWS 16

typedef struct {
    unsigned num_windows;
    struct {
        unsigned offset; /* from the candidate position */
        uint8_t first;
        uint8_t second; /* at offset + 1, if `pair` */
        bool pair;
    } windows[PREFILTER_MAX_WINDOWS];
} prefilter_t;

/* Set up `pf` for the scan sm_choose_scanroutine() chose. Returns false when
 * there is no useful prefilter: not an equality scan, or one whose candidates
 * would be too common (e.g. searching for 0) to be worth filtering. */
bool prefilter_init(prefilter_t *pf, scan_data_type_t dt, scan_match_type_t mt,
                    const uservalue_t *uval, bool reverse_endianness);

/* The first candidate position in [pos, end) of `buf`, or `end`. Bytes are
 * only read below `avail` (avail >= end). */
size_t prefilter_next(const prefilter_t *pf, const uint8_t *buf, size_t pos, size_t end,
                      size_t avail);

#endif /* SCANROUTINES_H */
