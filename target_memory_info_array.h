/*
 target_memory_info_array.h

 Copyright (C) 2009 Eli Dupree  <elidupree(a)charter.net>

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.
 
 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#ifndef _SEOIGROIYIOOBKOB_INC
#define _SEOIGROIYIOOBKOB_INC            /* include guard */

#include <inttypes.h>

#include "value.h"
#include "maps.h"

/* These two structs are not used; pointers to them are used to refer to arrays containing copied_data_swaths / matches_swaths, in the following ormat:
 - the array begins with the first_byte_in_child pointer; immediately after that is the number_of_bytes, and then the struct's data describing that many bytes. (Note that the number refers to the number of bytes in the child process's memory that are covered, not the number of bytes the struct takes up.)
 - in the first position after each such block is another such block (first byte pointer and number of bytes), or a null pointer and a 0 number of bytes to terminate the data.
 (The first_byte_in_child pointers refer to locations in the child - they cannot be followed except using ptrace())*/

typedef struct {
	uint8_t old_value;
	match_flags match_info;
} old_value_and_match_info;

typedef struct {
	void *first_byte_in_child;
	long number_of_bytes;
} unknown_type_of_swath;
typedef struct {
	void *first_byte_in_child;
	long number_of_bytes;
	uint8_t copied_bytes[0];
} copied_data_swath;
typedef struct {
	void *first_byte_in_child;
	long number_of_bytes;
	match_flags match_info[0];
} matches_swath;
typedef struct {
	void *first_byte_in_child;
	long number_of_bytes;
	old_value_and_match_info data[0];
} matches_and_old_values_swath;

typedef struct {
	long bytes_allocated;
	long max_needed_bytes;
} unknown_type_of_array;
typedef struct {
	long bytes_allocated;
	long max_needed_bytes;
	copied_data_swath swaths[0];
} copied_data_array;
typedef struct {
	long bytes_allocated;
	long max_needed_bytes;
	matches_swath swaths[0];
} matches_array;
typedef struct {
	long bytes_allocated;
	long max_needed_bytes;
	matches_and_old_values_swath swaths[0];
} matches_and_old_values_array;

typedef enum {
	MATCHES,
	MATCHES_AND_VALUES,
	VALUES
} data_array_type_t;

typedef struct {
	matches_and_old_values_swath *swath;
	long index;
} match_location;

unknown_type_of_array * allocate_array(unknown_type_of_array *array, long max_bytes);
unknown_type_of_array * allocate_enough_to_reach(unknown_type_of_array *array, void *last_byte_to_reach_plus_one, unknown_type_of_swath **swath_pointer_to_correct);
unknown_type_of_swath * add_element(unknown_type_of_array **array, unknown_type_of_swath *swath, void *remote_address, void *new_element, data_array_type_t type);
unknown_type_of_array * null_terminate(unknown_type_of_array *array, unknown_type_of_swath *swath, data_array_type_t type);
value_t data_to_val_aux(unknown_type_of_swath *swath, long index, long swath_length, data_array_type_t type);
value_t data_to_val(unknown_type_of_swath *swath, long index, data_array_type_t type);
void * remote_address_of_nth_element(unknown_type_of_swath *swath, long n, data_array_type_t type);
void * remote_address_of_last_element(unknown_type_of_swath *swath, data_array_type_t type);
void * local_address_beyond_nth_element(unknown_type_of_swath *swath, long n, data_array_type_t type);
void * local_address_beyond_last_element(unknown_type_of_swath *swath, data_array_type_t type);
match_location nth_match(matches_and_old_values_array *matches, unsigned n);
matches_and_old_values_array * delete_by_region(matches_and_old_values_array *array, long *num_matches, region_t *which, bool invert);

#endif
