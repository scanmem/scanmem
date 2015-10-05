/*
    The target memory information array (storage of matches).

    Copyright (C) 2009 Eli Dupree  <elidupree(a)charter.net>
    Copyright (C) 2010 WANG Lu  <coolwanglu(a)gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef TARGET_MEMORY_INFO_ARRAY_H
#define TARGET_MEMORY_INFO_ARRAY_H

#include <stdlib.h>
#include <inttypes.h>

#include "value.h"
#include "maps.h"
#include "show_message.h"

/* These two structs are not used; pointers to them are used to refer to arrays containing copied_data_swaths / matches_swaths, in the following ormat:
 - the array begins with the first_byte_in_child pointer; immediately after that is the number_of_bytes, and then the struct's data describing that many bytes. (Note that the number refers to the number of bytes in the child process's memory that are covered, not the number of bytes the struct takes up.)
 - in the first position after each such block is another such block (first byte pointer and number of bytes), or a null pointer and a 0 number of bytes to terminate the data.
 (The first_byte_in_child pointers refer to locations in the child - they cannot be followed except using ptrace())*/

typedef struct {
	uint8_t old_value;
	match_flags match_info;
} old_value_and_match_info;

/*
typedef struct {
	void *first_byte_in_child;
	unsigned long number_of_bytes;
} unknown_type_of_swath;
typedef struct {
	void *first_byte_in_child;
	unsigned long number_of_bytes;
	uint8_t copied_bytes[0];
} copied_data_swath;
typedef struct {
	void *first_byte_in_child;
	unsigned long number_of_bytes;
	match_flags match_info[0];
} matches_swath;
*/
typedef struct {
	void *first_byte_in_child;
	unsigned long number_of_bytes;
	old_value_and_match_info data[0];
} matches_and_old_values_swath;

/*
typedef struct {
	unsigned long bytes_allocated;
	unsigned long max_needed_bytes;
} unknown_type_of_array;
typedef struct {
	unsigned long bytes_allocated;
	unsigned long max_needed_bytes;
	copied_data_swath swaths[0];
} copied_data_array;
typedef struct {
	unsigned long bytes_allocated;
	unsigned long max_needed_bytes;
	matches_swath swaths[0];
} matches_array;
*/
typedef struct {
	unsigned long bytes_allocated;
	unsigned long max_needed_bytes;
	matches_and_old_values_swath swaths[0];
} matches_and_old_values_array;

/*
 * now we use MATCHES_AND_VALUES only
 *
typedef enum {
	MATCHES,
	MATCHES_AND_VALUES,
	VALUES
} data_array_type_t;
*/

typedef struct {
	matches_and_old_values_swath *swath;
	long index;
} match_location;

matches_and_old_values_array * allocate_array(matches_and_old_values_array *array, unsigned long max_bytes);
matches_and_old_values_array * null_terminate(matches_and_old_values_array *array, matches_and_old_values_swath *swath);

/* only at most sizeof(int64_t) bytes will be readed, if more bytes needed (e.g. bytearray), read it separatedly (for performance) */
value_t data_to_val_aux(matches_and_old_values_swath *swath, long index, long swath_length);
value_t data_to_val(matches_and_old_values_swath *swath, long index);
/* for printable text representation */
void data_to_printable_string(char *buf, int buf_length, matches_and_old_values_swath *swath, long index, int string_length);
/* for bytearray representation */
void data_to_bytearray_text(char *buf, int buf_length,  matches_and_old_values_swath *swath, long index, int bytearray_length);

match_location nth_match(matches_and_old_values_array *matches, unsigned n);
matches_and_old_values_array * delete_by_region(matches_and_old_values_array *array, long *num_matches, region_t *which, bool invert);

static inline long index_of_last_element(matches_and_old_values_swath *swath)
{
    return swath->number_of_bytes - 1;
}

static inline void *remote_address_of_nth_element(matches_and_old_values_swath *swath, long n)
{
    return swath->first_byte_in_child + n;
}

static inline void *remote_address_of_last_element(matches_and_old_values_swath *swath)
{
    return (remote_address_of_nth_element(swath, index_of_last_element(swath)));
}

static inline void *local_address_beyond_nth_element(matches_and_old_values_swath *swath, long n)
{
    return &((matches_and_old_values_swath *)swath)->data[n + 1];
}

static inline void *local_address_beyond_last_element(matches_and_old_values_swath *swath)
{
    return (local_address_beyond_nth_element(swath, index_of_last_element(swath)));
}

static inline matches_and_old_values_array *
allocate_enough_to_reach(matches_and_old_values_array *array,
                         void *last_byte_to_reach_plus_one,
                         matches_and_old_values_swath **swath_pointer_to_correct)
{
    unsigned long bytes_needed = (last_byte_to_reach_plus_one - (void *)array);

    if (bytes_needed <= array->bytes_allocated) return array;
    else
    {
        matches_and_old_values_array *original_location = array;

        /* Allocate twice as much each time, so we don't have to do it too often */
        unsigned long bytes_to_allocate = array->bytes_allocated;
        while (bytes_to_allocate < bytes_needed)
            bytes_to_allocate *= 2;

        show_debug("to_allocate %ld, max %ld\n", bytes_to_allocate, array->max_needed_bytes);

        /* Sometimes we know an absolute max that we will need */
        if (array->max_needed_bytes < bytes_to_allocate)
        {
            assert(array->max_needed_bytes >= bytes_needed);
            bytes_to_allocate = array->max_needed_bytes;
        }

        if (!(array = realloc(array, bytes_to_allocate))) return NULL;

        array->bytes_allocated = bytes_to_allocate;

        /* Put the swath pointer back where it should be, if needed. We cast
           everything to void pointers in this line to make sure the math works out. */
        if (swath_pointer_to_correct) (*swath_pointer_to_correct) = (matches_and_old_values_swath *)
            (((void *)(*swath_pointer_to_correct)) + ((void *)array - (void *)original_location));

        return array;
    }
}

/* Returns a pointer to the swath to which the element was added -
   i.e. the last swath in the array after the operation */
static inline matches_and_old_values_swath *
add_element(matches_and_old_values_array **array,
            matches_and_old_values_swath *swath,
            void *remote_address, void *new_element)
{
    if (swath->number_of_bytes == 0)
    {
        assert(swath->first_byte_in_child == NULL);

        /* We have to overwrite this as a new swath */
        *array = allocate_enough_to_reach(*array, (void *)swath +
            sizeof(matches_and_old_values_swath) +
            sizeof(old_value_and_match_info), &swath);

        swath->first_byte_in_child = remote_address;
    }
    else
    {
        unsigned long local_index_excess = remote_address - remote_address_of_last_element(swath);
        unsigned long local_address_excess = local_index_excess * sizeof(old_value_and_match_info);

        if (local_address_excess >= sizeof(matches_and_old_values_swath) +
            sizeof(old_value_and_match_info))
        {
            /* It is most memory-efficient to start a new swath */
            *array = allocate_enough_to_reach(*array, local_address_beyond_last_element(swath) +
                sizeof(matches_and_old_values_swath) + sizeof(old_value_and_match_info), &swath);

            swath = local_address_beyond_last_element(swath);
            swath->first_byte_in_child = remote_address;
            swath->number_of_bytes = 0;
        }
        else
        {
            /* It is most memory-efficient to write over the intervening space with null values */
            *array = allocate_enough_to_reach(*array, local_address_beyond_last_element(swath) +
                                              local_address_excess, &swath);
            memset(local_address_beyond_last_element(swath), 0, local_address_excess);
            swath->number_of_bytes += local_index_excess - 1;
        }
    }

    /* Add me */
    *(old_value_and_match_info *)(local_address_beyond_last_element(swath)) =
        *(old_value_and_match_info *)new_element;
    ++swath->number_of_bytes;

    return swath;
}

#endif /* TARGET_MEMORY_INFO_ARRAY_H */
