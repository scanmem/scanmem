/*
 target_memory_info_array.c

 Copyright (C) 2009 Eli Dupree  <elidupree(a)charter.net>
 Copyright (C) 2010 WANG Lu  <coolwanglu(a)gmail.com>

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

#include "config.h"

#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <ctype.h>

#include "target_memory_info_array.h"
#include "show_message.h"

matches_and_old_values_array * allocate_array (matches_and_old_values_array *array, unsigned long max_bytes)
{
    /* Make enough space for the array header and a null first swath. */
    unsigned long bytes_to_allocate = sizeof(matches_and_old_values_array) + sizeof(matches_and_old_values_swath);
    
    if (!(array = realloc(array, bytes_to_allocate))) return NULL;
    
    array->bytes_allocated = bytes_to_allocate;
    array->max_needed_bytes = max_bytes;
    
    return array;
}

matches_and_old_values_array * allocate_enough_to_reach(matches_and_old_values_array *array, void *last_byte_to_reach_plus_one, matches_and_old_values_swath **swath_pointer_to_correct)
{
    unsigned long bytes_needed = (last_byte_to_reach_plus_one - (void *)array);
    
    if (bytes_needed <= array->bytes_allocated) return array;
    else
    {
        matches_and_old_values_array *original_location = array;
        
        /* Allocate twice as much each time, so we don't have to do it too often */
        unsigned long bytes_to_allocate = array->bytes_allocated;
        while(bytes_to_allocate < bytes_needed)
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
        
        /* Put the swath pointer back where it should be, if needed. We cast everything to void pointers in this line to make sure the math works out. */
        if (swath_pointer_to_correct) (*swath_pointer_to_correct) = (matches_and_old_values_swath *)(((void *)(*swath_pointer_to_correct)) + ((void *)array - (void *)original_location));
        
        return array;
    }
}

/* Returns a pointer to the swath to which the element was added - i.e. the last swath in the array after the operation */
matches_and_old_values_swath * add_element (matches_and_old_values_array **array, matches_and_old_values_swath *swath, void *remote_address, void *new_element )
{
    if (swath->number_of_bytes == 0)
    {
        assert(swath->first_byte_in_child == NULL);
        
        /* We have to overwrite this as a new swath */
        *array = allocate_enough_to_reach(*array, (void *)swath + sizeof(matches_and_old_values_swath) + sizeof(old_value_and_match_info), &swath);
        
        swath->first_byte_in_child = remote_address;
    }
    else
    {
        unsigned long local_index_excess = remote_address - remote_address_of_last_element(swath );
        unsigned long local_address_excess = local_index_excess * sizeof(old_value_and_match_info);
         
        if (local_address_excess >= sizeof(matches_and_old_values_swath) + sizeof(old_value_and_match_info))
        {
            /* It is most memory-efficient to start a new swath */
            *array = allocate_enough_to_reach(*array, local_address_beyond_last_element(swath ) + sizeof(matches_and_old_values_swath) + sizeof(old_value_and_match_info), &swath);

            swath = local_address_beyond_last_element(swath );
            swath->first_byte_in_child = remote_address;
            swath->number_of_bytes = 0;
        }
        else
        {
            /* It is most memory-efficient to write over the intervening space with null values */
            *array = allocate_enough_to_reach(*array, local_address_beyond_last_element(swath ) + local_address_excess, &swath);
            memset(local_address_beyond_last_element(swath), 0, local_address_excess);
            swath->number_of_bytes += local_index_excess - 1;
        }
    }
    
    /* Add me */
    *(old_value_and_match_info *)(local_address_beyond_last_element(swath )) = *(old_value_and_match_info *)new_element;
    ++swath->number_of_bytes;
    
    return swath;
}

matches_and_old_values_array * null_terminate (matches_and_old_values_array *array, matches_and_old_values_swath *swath )
{
    unsigned long bytes_needed;
    
    if (swath->number_of_bytes == 0)
    {
        assert(swath->first_byte_in_child == NULL);
    }
    else
    {
        swath = local_address_beyond_last_element(swath );
        array = allocate_enough_to_reach(array, ((void *)swath) + sizeof(matches_and_old_values_swath), &swath);
        swath->first_byte_in_child = NULL;
        swath->number_of_bytes = 0;
    }
    
    bytes_needed = ((void *)swath + sizeof(matches_and_old_values_swath) - (void *)array);
    
    if (bytes_needed < array->bytes_allocated)
    {
        /* Reduce array to its final size */
        if (!(array = realloc(array, bytes_needed))) return NULL;
        array->bytes_allocated = bytes_needed;
    }
    
    return array;
}

long index_of_last_element(matches_and_old_values_swath *swath )
{
    return swath->number_of_bytes - 1;
}

value_t data_to_val_aux(matches_and_old_values_swath *swath, long index, long swath_length )
{
    int i;
    value_t val;
    int max_bytes = swath_length - index;
    
    memset(&val, 0x00, sizeof(val));
    
    if (max_bytes > 8) max_bytes = 8;
    
    if (max_bytes >= 8) val.flags.u64b = val.flags.s64b = val.flags.f64b = 1;
    if (max_bytes >= 4) val.flags.u32b = val.flags.s32b = val.flags.f32b = 1;
    if (max_bytes >= 2) val.flags.u16b = val.flags.s16b                  = 1;
    if (max_bytes >= 1) val.flags.u8b  = val.flags.s8b                   = 1;
    
    for (i = 0; i < max_bytes; ++i)
    {
        uint8_t byte;
        byte = ((matches_and_old_values_swath *)swath)->data[index + i].old_value;
        
        *((uint8_t *)(&val.int64_value) + i) = byte;
    }
    
    return val;
}

value_t data_to_val(matches_and_old_values_swath *swath, long index )
{
	return data_to_val_aux(swath, index, swath->number_of_bytes );
}

void data_to_printable_string(char *buf, int buf_length, matches_and_old_values_swath *swath, long index, int string_length)
{
    long swath_length = swath->number_of_bytes - index;
    /* TODO: what if length is too large ? */
    long max_length = (swath_length >= string_length) ? string_length : swath_length;
    int i;
    for(i = 0; i < max_length; ++i)
    {
        uint8_t byte = ((matches_and_old_values_swath *)swath)->data[index+i].old_value;
        buf[i] = isprint(byte) ? byte : '.';
    }
    buf[i] = 0; /* null-terminate */
}

void data_to_bytearray_text(char *buf, int buf_length,  matches_and_old_values_swath *swath, long index, int bytearray_length)
{
    long swath_length = swath->number_of_bytes - index;
    /* TODO: what if length is too large ? */
    long max_length = (swath_length >= bytearray_length) ? bytearray_length : swath_length;
    int i;
    int bytes_used = 0;
    for(i = 0; i < max_length; ++i)
    {
        uint8_t byte = ((matches_and_old_values_swath *)swath)->data[index+i].old_value;
        /* TODO: check error here */
        snprintf(buf+bytes_used, buf_length-bytes_used, (i<max_length-1) ? "%02x " : "%02x", byte);
        bytes_used += 3;
    }
}

void * remote_address_of_nth_element(matches_and_old_values_swath *swath, long n )
{
    return swath->first_byte_in_child + n;
}

void * remote_address_of_last_element(matches_and_old_values_swath *swath )
{
    return (remote_address_of_nth_element(swath, index_of_last_element(swath ) ));
}

void * local_address_beyond_nth_element(matches_and_old_values_swath *swath, long n )
{
    return &((matches_and_old_values_swath *)swath)->data[n + 1];
}

void * local_address_beyond_last_element(matches_and_old_values_swath *swath )
{
    return (local_address_beyond_nth_element(swath, index_of_last_element(swath ) ));
}

match_location nth_match(matches_and_old_values_array *matches, unsigned n)
{
    unsigned i = 0;

    matches_and_old_values_swath *reading_swath_index = (matches_and_old_values_swath *)matches->swaths;
    int reading_iterator = 0;

    if (!matches) return (match_location){ NULL, 0 };

    while (reading_swath_index->first_byte_in_child) {
        /* Only actual matches are considered */
        if (flags_to_max_width_in_bytes(reading_swath_index->data[reading_iterator].match_info) > 0)
        {
            if (i == n)
            {
                return (match_location){ reading_swath_index, reading_iterator };
            }
        
            ++i;
        }

        /* Go on to the next one... */
        ++reading_iterator;
        if (reading_iterator >= reading_swath_index->number_of_bytes)
        {
            reading_swath_index = local_address_beyond_last_element((matches_and_old_values_swath *)reading_swath_index );
            reading_iterator = 0;
        }
    }

    /* I guess this is not a valid match-id */
    return (match_location){ NULL, 0 };
}

matches_and_old_values_array * delete_by_region(matches_and_old_values_array *matches, long *num_matches, region_t *which, bool invert)
{
    matches_and_old_values_swath *reading_swath_index = (matches_and_old_values_swath *)matches->swaths;
    matches_and_old_values_swath reading_swath = *reading_swath_index;
    int reading_iterator = 0;
    matches_and_old_values_swath *writing_swath_index = (matches_and_old_values_swath *)matches->swaths;
    writing_swath_index->first_byte_in_child = NULL;
    writing_swath_index->number_of_bytes = 0;

    *num_matches = 0;
    
    while (reading_swath.first_byte_in_child) {
        void *address = reading_swath.first_byte_in_child + reading_iterator;
        bool in_region = (address >= which->start && address < which->start + which->size);

        if ((in_region && invert) || (!in_region && !invert))
        {
            match_flags flags = reading_swath_index->data[reading_iterator].match_info;
            
            /* Still a candidate. Write data. 
                (We can get away with overwriting in the same array because it is guaranteed to take up the same number of bytes or fewer, and because we copied out the reading swath metadata already.)
                (We can get away with assuming that the pointers will stay valid, because as we never add more data to the array than there was before, it will not reallocate.) */
            writing_swath_index = add_element((matches_and_old_values_array **)(&matches), (matches_and_old_values_swath *)writing_swath_index, address, &reading_swath_index->data[reading_iterator] );

            /* Actual matches are recorded */
            if (flags_to_max_width_in_bytes(flags) > 0) ++(*num_matches);
        }
        
        /* Go on to the next one... */
        ++reading_iterator;
        if (reading_iterator >= reading_swath.number_of_bytes)
        {
            assert(((matches_and_old_values_swath *)(&reading_swath_index->data[reading_swath.number_of_bytes]))->number_of_bytes >= 0);
            reading_swath_index = (matches_and_old_values_swath *)(&reading_swath_index->data[reading_swath.number_of_bytes]);
            reading_swath = *reading_swath_index;
            reading_iterator = 0;
        }
    }
    
    if (!(matches = null_terminate((matches_and_old_values_array *)matches, (matches_and_old_values_swath *)writing_swath_index )))
    {
        return NULL;
    }
    
    return matches;
}
