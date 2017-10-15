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

#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <ctype.h>

#include "targetmem.h"
#include "show_message.h"
#include "value.h"


matches_and_old_values_array *
allocate_array (matches_and_old_values_array *array, size_t max_bytes)
{
    /* make enough space for the array header and a null first swath */
    size_t bytes_to_allocate =
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
    size_t bytes_needed;

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
                               size_t index, int string_length)
{
    long swath_length = swath->number_of_bytes - index;
    /* TODO: what if length is too large ? */
    long max_length = (swath_length >= string_length) ? string_length : swath_length;
    int i;

    for (i = 0; i < max_length; ++i) {
        uint8_t byte = swath->data[index+i].old_value;
        buf[i] = isprint(byte) ? byte : '.';
    }
    buf[i] = 0; /* null-terminate */
}

void data_to_bytearray_text (char *buf, int buf_length,
                             matches_and_old_values_swath *swath,
                             size_t index, int bytearray_length)
{
    int i;
    int bytes_used = 0;
    long swath_length = swath->number_of_bytes - index;

    /* TODO: what if length is too large ? */
    long max_length = (swath_length >= bytearray_length) ?
                       bytearray_length : swath_length;

    for (i = 0; i < max_length; ++i) {
        uint8_t byte = swath->data[index+i].old_value;

        /* TODO: check error here */
        snprintf(buf+bytes_used, buf_length-bytes_used,
                 (i<max_length-1) ? "%02x " : "%02x", byte);
        bytes_used += 3;
    }
}

match_location
nth_match (matches_and_old_values_array *matches, size_t n)
{
    size_t i = 0;
    matches_and_old_values_swath *reading_swath_index;
    size_t reading_iterator = 0;

    assert(matches);
    reading_swath_index = matches->swaths;

    while (reading_swath_index->first_byte_in_child) {
        /* only actual matches are considered */
        if (reading_swath_index->data[reading_iterator].match_info != flags_empty) {

            if (i == n)
                return (match_location){reading_swath_index, reading_iterator};

            ++i;
        }

        /* go on to the next one... */
        ++reading_iterator;
        if (reading_iterator >= reading_swath_index->number_of_bytes) {
            reading_swath_index =
                local_address_beyond_last_element(reading_swath_index);

            reading_iterator = 0;
        }
    }

    /* I guess this is not a valid match-id */
    return (match_location){ NULL, 0 };
}

/* deletes matches in [start, end) and resizes the matches array */
matches_and_old_values_array *
delete_in_address_range (matches_and_old_values_array *array,
                         unsigned long *num_matches,
                         void *start_address, void *end_address)
{
    assert(array);

    size_t reading_iterator = 0;
    matches_and_old_values_swath *reading_swath_index = array->swaths;

    matches_and_old_values_swath reading_swath = *reading_swath_index;

    matches_and_old_values_swath *writing_swath_index = array->swaths;

    writing_swath_index->first_byte_in_child = NULL;
    writing_swath_index->number_of_bytes = 0;

    *num_matches = 0;

    while (reading_swath.first_byte_in_child) {
        void *address = reading_swath.first_byte_in_child + reading_iterator;

        if (address < start_address || address >= end_address) {
            old_value_and_match_info old_byte;

            old_byte = reading_swath_index->data[reading_iterator];

            /* Still a candidate. Write data.
                (We can get away with overwriting in the same array because
                 it is guaranteed to take up the same number of bytes or fewer,
                 and because we copied out the reading swath metadata already.)
                (We can get away with assuming that the pointers will stay
                 valid, because as we never add more data to the array than
                 there was before, it will not reallocate.) */
            writing_swath_index = add_element(&array,
                                      writing_swath_index, address,
                                      old_byte.old_value, old_byte.match_info);

            /* actual matches are recorded */
            if (old_byte.match_info != flags_empty)
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

    return null_terminate(array, writing_swath_index);
}
