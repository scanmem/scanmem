/*
    The target memory information array (storage of matches).

    Copyright (C) 2009 Eli Dupree  <elidupree(a)charter.net>
    Copyright (C) 2010 WANG Lu  <coolwanglu(a)gmail.com>

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

#include "config.h"

#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <ctype.h>

#include "targetmem.h"
#include "show_message.h"


matches_and_old_values_array *
allocate_array (matches_and_old_values_array *array, unsigned long max_bytes)
{
    /* make enough space for the array header and a null first swath */
    unsigned long bytes_to_allocate =
        sizeof(matches_and_old_values_array) +
        sizeof(matches_and_old_values_swath);

    if (!(array = realloc(array, bytes_to_allocate)))
        return NULL;

    array->bytes_allocated = bytes_to_allocate;
    array->max_needed_bytes = max_bytes;

    return array;
}

matches_and_old_values_array *
null_terminate (matches_and_old_values_array *array,
                matches_and_old_values_swath *swath)
{
    unsigned long bytes_needed;

    if (swath->number_of_bytes == 0) {
        assert(swath->first_byte_in_child == NULL);

    } else {
        swath = local_address_beyond_last_element(swath );
        array = allocate_enough_to_reach(array, ((void *)swath) +
                                         sizeof(matches_and_old_values_swath),
                                         &swath);
        swath->first_byte_in_child = NULL;
        swath->number_of_bytes = 0;
    }

    bytes_needed = ((void *)swath + sizeof(matches_and_old_values_swath) -
                    (void *)array);

    if (bytes_needed < array->bytes_allocated) {
        /* reduce array to its final size */
        if (!(array = realloc(array, bytes_needed)))
            return NULL;

        array->bytes_allocated = bytes_needed;
    }

    return array;
}

void data_to_printable_string (char *buf, int buf_length,
                               matches_and_old_values_swath *swath,
                               long index, int string_length)
{
    long swath_length = swath->number_of_bytes - index;
    /* TODO: what if length is too large ? */
    long max_length = (swath_length >= string_length) ? string_length : swath_length;
    int i;

    for (i = 0; i < max_length; ++i) {
        uint8_t byte;

        byte  = ((matches_and_old_values_swath *)swath)->data[index+i].old_value;
        buf[i] = isprint(byte) ? byte : '.';
    }
    buf[i] = 0; /* null-terminate */
}

void data_to_bytearray_text (char *buf, int buf_length,
                             matches_and_old_values_swath *swath,
                             long index, int bytearray_length)
{
    int i;
    int bytes_used = 0;
    long swath_length = swath->number_of_bytes - index;

    /* TODO: what if length is too large ? */
    long max_length = (swath_length >= bytearray_length) ?
                       bytearray_length : swath_length;

    for (i = 0; i < max_length; ++i) {
        uint8_t byte;

        byte = ((matches_and_old_values_swath *)swath)->data[index+i].old_value;
        /* TODO: check error here */
        snprintf(buf+bytes_used, buf_length-bytes_used,
                 (i<max_length-1) ? "%02x " : "%02x", byte);
        bytes_used += 3;
    }
}

match_location
nth_match (matches_and_old_values_array *matches, unsigned n)
{
    unsigned i = 0;

    matches_and_old_values_swath *reading_swath_index =
        (matches_and_old_values_swath *)matches->swaths;

    int reading_iterator = 0;

    if (!matches)
        return (match_location){NULL, 0};

    while (reading_swath_index->first_byte_in_child) {
        /* only actual matches are considered */
        if (flags_to_max_width_in_bytes(
                reading_swath_index->data[reading_iterator].match_info) > 0) {

            if (i == n)
                return (match_location){reading_swath_index, reading_iterator};

            ++i;
        }

        /* go on to the next one... */
        ++reading_iterator;
        if (reading_iterator >= reading_swath_index->number_of_bytes) {
            reading_swath_index =
                local_address_beyond_last_element(
                    (matches_and_old_values_swath *)reading_swath_index);

            reading_iterator = 0;
        }
    }

    /* I guess this is not a valid match-id */
    return (match_location){ NULL, 0 };
}

matches_and_old_values_array *
delete_by_region (matches_and_old_values_array *matches,
                  unsigned long *num_matches, region_t *which, bool invert)
{
    int reading_iterator = 0;
    matches_and_old_values_swath *reading_swath_index =
        (matches_and_old_values_swath *)matches->swaths;

    matches_and_old_values_swath reading_swath = *reading_swath_index;

    matches_and_old_values_swath *writing_swath_index =
        (matches_and_old_values_swath *)matches->swaths;

    writing_swath_index->first_byte_in_child = NULL;
    writing_swath_index->number_of_bytes = 0;

    *num_matches = 0;

    while (reading_swath.first_byte_in_child) {
        void *address = reading_swath.first_byte_in_child + reading_iterator;
        bool in_region = (address >= which->start &&
                          address < which->start + which->size);

        if ((in_region && invert) || (!in_region && !invert)) {
            match_flags flags;

            flags = reading_swath_index->data[reading_iterator].match_info;

            /* Still a candidate. Write data.
                (We can get away with overwriting in the same array because
                 it is guaranteed to take up the same number of bytes or fewer,
                 and because we copied out the reading swath metadata already.)
                (We can get away with assuming that the pointers will stay
                 valid, because as we never add more data to the array than
                 there was before, it will not reallocate.) */
            writing_swath_index = add_element((matches_and_old_values_array **)
                                      (&matches), (matches_and_old_values_swath *)
                                      writing_swath_index, address,
                                      &reading_swath_index->data[reading_iterator]);

            /* actual matches are recorded */
            if (flags_to_max_width_in_bytes(flags) > 0)
                ++(*num_matches);
        }

        /* go on to the next one... */
        ++reading_iterator;
        if (reading_iterator >= reading_swath.number_of_bytes) {

            reading_swath_index = (matches_and_old_values_swath *)
                (&reading_swath_index->data[reading_swath.number_of_bytes]);

            reading_swath = *reading_swath_index;

            reading_iterator = 0;
        }
    }

    matches = null_terminate((matches_and_old_values_array *)matches,
                             (matches_and_old_values_swath *)writing_swath_index);

    if (!matches)
        return NULL;

    return matches;
}
