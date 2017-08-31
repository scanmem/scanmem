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

#include "value.h"
#include "maps.h"
#include "show_message.h"

/* Public structs */

/* Single match struct */
typedef struct {
    uint8_t old_value;
    match_flags match_info;
} old_value_and_match_info;

/* Array that contains a consecutive (in memory) sequence of matches (= swath).
   - the first_byte_in_child pointer refers to locations in the child,
     it cannot be followed except using ptrace()
   - the number_of_bytes refers to the number of bytes in the child
     process's memory that are covered, not the number of bytes the struct
     takes up. It's the length of data. */
typedef struct {
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
        matches_and_old_values_array *original_location = array;

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

        /* Put the swath pointer back where it should be, if needed.
           We cast everything to void pointers in this line to make
           sure the math works out. */
        if (swath_pointer_to_correct) {
            (*swath_pointer_to_correct) = (matches_and_old_values_swath *)
                (((void *)(*swath_pointer_to_correct)) +
                 ((void *)array - (void *)original_location));
        }

        return array;
    }
}

/* returns a pointer to the swath to which the element was added -
   i.e. the last swath in the array after the operation */
static inline matches_and_old_values_swath *
add_element (matches_and_old_values_array **array,
             matches_and_old_values_swath *swath,
             void *remote_address,
             const old_value_and_match_info *new_element)
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

        size_t needed_size =
            sizeof(matches_and_old_values_swath) +
            sizeof(old_value_and_match_info);

        if (local_address_excess >= needed_size) {
            /* it is most memory-efficient to start a new swath */
            *array = allocate_enough_to_reach(*array,
                local_address_beyond_last_element(swath) +
                sizeof(matches_and_old_values_swath) +
                sizeof(old_value_and_match_info), &swath);

            swath = local_address_beyond_last_element(swath);
            swath->first_byte_in_child = remote_address;
            swath->number_of_bytes = 0;

        } else {
            /* it is most memory-efficient to write over the intervening
               space with null values */
            *array = allocate_enough_to_reach(*array,
                local_address_beyond_last_element(swath) +
                local_address_excess, &swath);

            switch (local_address_excess) {
            case 4 : /* = sizeof(old_value_and_match_info) */
                memset(local_address_beyond_last_element(swath), 0, 4);
                break;
            case 8 : /* = 2*sizeof(old_value_and_match_info) */
                memset(local_address_beyond_last_element(swath), 0, 8);
                break;
            default:
                /* slow due to unknown size to be zeroed */
                memset(local_address_beyond_last_element(swath), 0,
                       local_address_excess);
                break;
            }
            swath->number_of_bytes += local_index_excess - 1;
        }
    }

    /* add me */
    *(old_value_and_match_info *)local_address_beyond_last_element(swath) =
        *new_element;
    ++swath->number_of_bytes;

    return swath;
}

/* only at most sizeof(int64_t) bytes will be read,
   if more bytes are needed (e.g. bytearray),
   read them separately (for performance) */
static inline value_t
data_to_val_aux (const matches_and_old_values_swath *swath,
                 size_t index, size_t swath_length)
{
    uint i;
    value_t val;
    size_t max_bytes = swath_length - index;

    /* Init all possible flags in a single go.
     * Also init length to the maximum possible value */
    val.flags.all_flags = 0xffffu;

    /* NOTE: This does the right thing for VLT because the high flags are
     * not in the least significant bits in the `length` union member,
     * otherwise their zeroing would interfere with useful bits of `length`.
     * This works on both endians, as the high flags are in the middle. */
    if (max_bytes > 8) max_bytes = 8;
    if (max_bytes < 8) val.flags.u64b = val.flags.s64b = val.flags.f64b = 0;
    if (max_bytes < 4) val.flags.u32b = val.flags.s32b = val.flags.f32b = 0;
    if (max_bytes < 2) val.flags.u16b = val.flags.s16b                  = 0;
    if (max_bytes < 1) val.flags.u8b  = val.flags.s8b                   = 0;

    for (i = 0; i < max_bytes; ++i) {
        /* Both uint8_t, no explicit casting needed */
        val.bytes[i] = swath->data[index + i].old_value;
    }

    /* Truncate to the old flags, which are stored with the first matched byte */
    val.flags.all_flags &= swath->data[index].match_info.all_flags;

    return val;
}

static inline value_t
data_to_val (const matches_and_old_values_swath *swath, size_t index)
{
    return data_to_val_aux(swath, index, swath->number_of_bytes);
}

#endif /* TARGETMEM_H */
