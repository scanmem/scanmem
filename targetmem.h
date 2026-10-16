/*
    The target memory information array (storage of matches).

    Copyright (C) 2009 Eli Dupree  <elidupree(a)charter.net>
    Copyright (C) 2010 WANG Lu  <coolwanglu(a)gmail.com>
    Copyright (C) 2015 Sebastian Parschauer <s.parschauer@gmx.de>

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

#ifndef TARGETMEM_H
#define TARGETMEM_H

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/mman.h>

#include "value.h"
#include "show_message.h"

/* Public structs */

/* Single match struct, 3 bytes: the array holds one per byte of every swath,
   so padding would cost a quarter of its memory, and of the memory traffic of
   a scan. Its fields, and the swath headers, are unaligned: use them only
   through these packed structs, never through pointers to their fields. */
typedef struct __attribute__((packed)) {
    uint8_t old_value;
    match_flags match_info;
} old_value_and_match_info;

/* Array that contains a consecutive (in memory) sequence of matches (= swath).
   - the first_byte_in_child pointer refers to locations in the child,
     it cannot be followed except using ptrace()
   - the number_of_bytes refers to the number of bytes in the child
     process's memory that are covered, not the number of bytes the struct
     takes up. It's the length of data. */
typedef struct __attribute__((packed)) {
    void *first_byte_in_child;
    size_t number_of_bytes;
    old_value_and_match_info data[0];
} matches_and_old_values_swath;

/* Master matches array, smartly resized, contains swaths.
   Both `bytes` values refer to real struct bytes this time. */
typedef struct {
    size_t bytes_allocated;
    size_t max_needed_bytes;
    matches_and_old_values_swath swaths[0];
} matches_and_old_values_array;

/* Location of a match in a matches_and_old_values_array */
typedef struct {
    matches_and_old_values_swath *swath;
    size_t index;
} match_location;


/* Public functions */

matches_and_old_values_array *allocate_array (matches_and_old_values_array *array,
                                              size_t max_bytes);

matches_and_old_values_array *null_terminate (matches_and_old_values_array *array,
                                              matches_and_old_values_swath *swath);

/* for printable text representation */
void data_to_printable_string (char *buf, int buf_length,
                               matches_and_old_values_swath *swath,
                               size_t index, int string_length);

/* for bytearray representation */
void data_to_bytearray_text (char *buf, int buf_length,
                             matches_and_old_values_swath *swath,
                             size_t index, int bytearray_length);

match_location nth_match (matches_and_old_values_array *matches, size_t n);

/* deletes matches in [start, end) and resizes the matches array */
matches_and_old_values_array *
delete_in_address_range (matches_and_old_values_array *array,
                         unsigned long *num_matches,
                         void *start_address, void *end_address);

/* Ask for transparent huge pages for a big array: touching its memory for the
   first time then costs a page fault per 2 MiB instead of per 4 KiB. */
static inline void
advise_huge_pages (void *start, size_t length)
{
#ifdef MADV_HUGEPAGE
    if (length >= (4UL << 20)) {
        /* madvise() wants whole pages */
        uintptr_t first = ((uintptr_t)start + 4095) & ~(uintptr_t)4095;
        uintptr_t end = ((uintptr_t)start + length) & ~(uintptr_t)4095;
        madvise((void *)first, end - first, MADV_HUGEPAGE);
    }
#else
    (void)start;
    (void)length;
#endif
}

/* The following functions are called in the hot scanning path and were moved
   to this header from the .c file so that they could be inlined */

static inline size_t
index_of_last_element (matches_and_old_values_swath *swath)
{
    return swath->number_of_bytes - 1;
}

static inline void *
remote_address_of_nth_element (matches_and_old_values_swath *swath, size_t n)
{
    return swath->first_byte_in_child + n;
}

static inline void *
remote_address_of_last_element (matches_and_old_values_swath *swath)
{
    return (remote_address_of_nth_element(swath, index_of_last_element(swath)));
}

static inline void *
local_address_beyond_nth_element (matches_and_old_values_swath *swath, size_t n)
{
    return &(swath->data[n + 1]);
}

static inline void *
local_address_beyond_last_element (matches_and_old_values_swath *swath)
{
    return (local_address_beyond_nth_element(swath, index_of_last_element(swath)));
}

static inline matches_and_old_values_array *
allocate_enough_to_reach (matches_and_old_values_array *array,
                          void *last_byte_to_reach_plus_one,
                          matches_and_old_values_swath **swath_pointer_to_correct)
{
    size_t bytes_needed = last_byte_to_reach_plus_one - (void *)array;

    if (bytes_needed <= array->bytes_allocated) {
        return array;

    } else {
        /* Note where the swath sits inside the array before the realloc.
           Doing that arithmetic against the old pointer afterwards is
           undefined once realloc has taken it, and gcc 12+ warns on it. */
        size_t swath_offset = 0;
        if (swath_pointer_to_correct)
            swath_offset = (size_t)((char *)(*swath_pointer_to_correct)
                                    - (char *)array);

        /* allocate twice as much each time,
           so we don't have to do it too often */
        size_t bytes_to_allocate = array->bytes_allocated;
        while (bytes_to_allocate < bytes_needed)
            bytes_to_allocate *= 2;

        show_debug("to_allocate %ld, max %ld\n", bytes_to_allocate,
                   array->max_needed_bytes);

        /* sometimes we know an absolute max that we will need */
        if (array->max_needed_bytes < bytes_to_allocate) {
            assert(array->max_needed_bytes >= bytes_needed);
            bytes_to_allocate = array->max_needed_bytes;
        }

        if (!(array = realloc(array, bytes_to_allocate)))
            return NULL;

        array->bytes_allocated = bytes_to_allocate;

        /* Put the swath pointer back where it should be, if needed. */
        if (swath_pointer_to_correct) {
            (*swath_pointer_to_correct) = (matches_and_old_values_swath *)
                ((char *)array + swath_offset);
        }

        return array;
    }
}

/* Whether an element `index_excess` bytes past a swath's last element starts
   a new swath, rather than joining that one over a padded gap: when the gap
   would take at least the room of a new swath's header. The equal case starts
   a new swath, so that later scans don't needlessly iterate through a bunch
   of empty values.
   Rescans, which rewrite the array in place, rely on this: the elements a new
   swath leaves out take at least the room of its header, so the array never
   gets longer than it was up to the same point. Whatever adds elements to a
   matches array (add_element(), and the merges of the chunked scans in
   ptrace.c) must decide by this. */
static inline bool
gap_starts_new_swath (size_t index_excess)
{
    return index_excess * sizeof(old_value_and_match_info) >=
           sizeof(matches_and_old_values_swath) + sizeof(old_value_and_match_info);
}

/* returns a pointer to the swath to which the element was added -
   i.e. the last swath in the array after the operation */
static inline matches_and_old_values_swath *
add_element (matches_and_old_values_array **array,
             matches_and_old_values_swath *swath,
             void *remote_address,
             uint8_t new_byte,
             match_flags new_flags)
{
    if (swath->number_of_bytes == 0) {
        assert(swath->first_byte_in_child == NULL);

        /* we have to overwrite this as a new swath */
        *array = allocate_enough_to_reach(*array, (void *)swath +
            sizeof(matches_and_old_values_swath) +
            sizeof(old_value_and_match_info), &swath);

        swath->first_byte_in_child = remote_address;

    } else {
        size_t local_index_excess =
            remote_address - remote_address_of_last_element(swath);

        size_t local_address_excess =
            local_index_excess * sizeof(old_value_and_match_info);

        size_t needed_size_for_a_new_swath =
            sizeof(matches_and_old_values_swath) +
            sizeof(old_value_and_match_info);

        if (gap_starts_new_swath(local_index_excess)) {
            /* It is more memory-efficient to start a new swath. */
            *array = allocate_enough_to_reach(*array,
                local_address_beyond_last_element(swath) +
                needed_size_for_a_new_swath, &swath);

            swath = local_address_beyond_last_element(swath);
            swath->first_byte_in_child = remote_address;
            swath->number_of_bytes = 0;

        } else {
            /* It is more memory-efficient to write over the intervening
               space with null values */
            *array = allocate_enough_to_reach(*array,
                local_address_beyond_last_element(swath) +
                local_address_excess, &swath);

            switch (local_index_excess) {
            case 1:
                /* do nothing, the new value is right after the old */
                break;
            case 2:
                memset(local_address_beyond_last_element(swath), 0,
                       sizeof(old_value_and_match_info));
                break;
            default:
                /* slow due to unknown size to be zeroed */
                memset(local_address_beyond_last_element(swath), 0,
                       local_address_excess - sizeof(old_value_and_match_info));
                break;
            }
            swath->number_of_bytes += local_index_excess - 1;
        }
    }

    /* add me */
    old_value_and_match_info *dataptr = local_address_beyond_last_element(swath);
    dataptr->old_value = new_byte;
    dataptr->match_info = new_flags;
    ++swath->number_of_bytes;

    return swath;
}

/* only at most sizeof(int64_t) bytes will be read,
   if more bytes are needed (e.g. bytearray),
   read them separately (for performance) */
/* The flags an old value read from `max_bytes` (at most 8) bytes of a swath
   can keep: all, except the number types too wide for them.
   NOTE: This does the right thing for VLT because the flags are in
   the same order as the number representation (for both endians), so
   that the zeroing of a flag does not change useful bits of `length`. */
static inline match_flags
old_value_flags_within (size_t max_bytes)
{
    match_flags flags = flags_max;

    if (max_bytes < 8) flags &= ~flags_64b;
    if (max_bytes < 4) flags &= ~flags_32b;
    if (max_bytes < 2) flags &= ~flags_16b;
    if (max_bytes < 1) flags = flags_empty;
    return flags;
}

static inline value_t
data_to_val_aux (const matches_and_old_values_swath *swath,
                 size_t index, size_t swath_length)
{
    unsigned int i;
    volatile value_t val;
    size_t max_bytes = swath_length - index;

    if (max_bytes > 8) max_bytes = 8;
    val.flags = old_value_flags_within(max_bytes);

    for (i = 0; i < max_bytes; ++i) {
        /* Both uint8_t, no explicit casting needed */
        val.bytes[i] = swath->data[index + i].old_value;
    }

    /* Truncate to the old flags, which are stored with the first matched byte */
    val.flags &= swath->data[index].match_info;

    return val;
}

static inline value_t
data_to_val (const matches_and_old_values_swath *swath, size_t index)
{
    return data_to_val_aux(swath, index, swath->number_of_bytes);
}

#endif /* TARGETMEM_H */
