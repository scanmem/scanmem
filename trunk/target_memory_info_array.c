
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "target_memory_info_array.h"

#define SIZE_OF_ELEMENT_TYPE(typ) (type == MATCHES ? sizeof(match_flags) : type == MATCHES_AND_VALUES ? sizeof(old_value_and_match_info) : sizeof(unsigned char))

unknown_type_of_array * allocate_array (unknown_type_of_array *array, long max_bytes)
{
    /* Make enough space for the array header and a null first swath. */
    long bytes_to_allocate = sizeof(unknown_type_of_array) + sizeof(unknown_type_of_swath);
    
    if (!(array = realloc(array, bytes_to_allocate))) return NULL;
    
    array->bytes_allocated = bytes_to_allocate;
    array->max_needed_bytes = max_bytes;
    
    return array;
}

unknown_type_of_array * allocate_enough_to_reach(unknown_type_of_array *array, void *last_byte_to_reach_plus_one, unknown_type_of_swath **swath_pointer_to_correct)
{
    long bytes_needed = (last_byte_to_reach_plus_one - (void *)array);
    
    if (bytes_needed <= array->bytes_allocated) return array;
    else
    {
        unknown_type_of_array *original_location = array;
        
        /* Allocate twice as much each time, so we don't have to do it too often */
        long bytes_to_allocate = array->bytes_allocated * 2;
        
        /* Sometimes we know an absolute max that we will need */
        if (array->max_needed_bytes < bytes_to_allocate)
        {
            assert(array->max_needed_bytes >= bytes_needed);
            bytes_to_allocate = array->max_needed_bytes;
        }
        
        if (!(array = realloc(array, bytes_to_allocate))) return NULL;
        
        array->bytes_allocated = bytes_to_allocate;
        
        /* Put the swath pointer back where it should be, if needed. We cast everything to void pointers in this line to make sure the math works out. */
        if (swath_pointer_to_correct) (*swath_pointer_to_correct) = (unknown_type_of_swath *)(((void *)(*swath_pointer_to_correct)) + ((void *)array - (void *)original_location));
        
        return array;
    }
}

/* Returns a pointer to the swath to which the element was added - i.e. the last swath in the array after the operation */
unknown_type_of_swath * add_element (unknown_type_of_array **array, unknown_type_of_swath *swath, void *remote_address, void *new_element, data_array_type_t type)
{
    if (swath->number_of_bytes == 0)
    {
        assert(swath->first_byte_in_child == NULL);
        
        /* We have to overwrite this as a new swath */
        *array = allocate_enough_to_reach(*array, (void *)swath + sizeof(unknown_type_of_swath) + SIZE_OF_ELEMENT_TYPE(type), &swath);
        
        swath->first_byte_in_child = remote_address;
    }
    else
    {
        long local_index_excess = remote_address - 1 - remote_address_of_last_element(swath, MATCHES_AND_VALUES);
        long local_address_excess = local_index_excess * SIZE_OF_ELEMENT_TYPE(type);
         
        if (local_address_excess >= sizeof(unknown_type_of_swath))
        {
            /* It is most memory-efficient to start a new swath */
            *array = allocate_enough_to_reach(*array, local_address_beyond_last_element(swath, type) + sizeof(unknown_type_of_swath) + SIZE_OF_ELEMENT_TYPE(type), &swath);
            
            swath = local_address_beyond_last_element(swath, type);
            swath->first_byte_in_child = remote_address;
            swath->number_of_bytes = 0;
        }
        else
        {
            /* It is most memory-efficient to write over the intervening space with null values */
            *array = allocate_enough_to_reach(*array, local_address_beyond_last_element(swath, type) + local_address_excess + SIZE_OF_ELEMENT_TYPE(type), &swath);
            
            memset(local_address_beyond_last_element(swath, type), 0, local_address_excess);
            swath->number_of_bytes += local_index_excess;
        }
    }
    
    /* Add me */
    switch(type)
    {
        case MATCHES: *(match_flags *)(local_address_beyond_last_element(swath, type)) = *(match_flags *)new_element; break;
        case MATCHES_AND_VALUES: *(old_value_and_match_info *)(local_address_beyond_last_element(swath, type)) = *(old_value_and_match_info *)new_element; break;
        case VALUES: *(unsigned char *)(local_address_beyond_last_element(swath, type)) = *(unsigned char *)new_element; break;
    }
    ++swath->number_of_bytes;
    
    return swath;
}

unknown_type_of_array * null_terminate (unknown_type_of_array *array, unknown_type_of_swath *swath, data_array_type_t type)
{
    long bytes_needed;
    
    if (swath->number_of_bytes == 0)
    {
        assert(swath->first_byte_in_child == NULL);
    }
    else
    {
        swath = local_address_beyond_last_element(swath, type);
        swath->first_byte_in_child = NULL;
        swath->number_of_bytes = 0;
    }
    
    bytes_needed = ((void *)swath + sizeof(unknown_type_of_swath) - (void *)array);
    
    if (bytes_needed < array->bytes_allocated)
    {
        /* Reduce array to its final size */
        if (!(array = realloc(array, bytes_needed))) return NULL;
        array->bytes_allocated = bytes_needed;
    }
    
    return array;
}

long index_of_last_element(unknown_type_of_swath *swath, data_array_type_t type)
{
    return swath->number_of_bytes - 1;
}

value_t data_to_val_aux(unknown_type_of_swath *swath, long index, long swath_length, data_array_type_t type)
{
    int i;
    value_t val;
    int max_bytes = swath_length - index;
    
    memset(&val, 0x00, sizeof(val));
    val.how_to_calculate_values = BY_POINTER_SHIFTING;
    
    if (max_bytes > 8) max_bytes = 8;
    
    if (max_bytes >= 8) val.flags.u64b = val.flags.s64b = val.flags.f64b = 1;
    if (max_bytes >= 4) val.flags.u32b = val.flags.s32b = val.flags.f32b = 1;
    if (max_bytes >= 2) val.flags.u16b = val.flags.s16b                  = 1;
    if (max_bytes >= 1) val.flags.u8b  = val.flags.s8b                   = 1;
    
    for (i = 0; i < max_bytes; ++i)
    {
        uint8_t byte;
        switch(type)
        {
            case MATCHES_AND_VALUES: byte = ((matches_and_old_values_swath *)swath)->data[index + i].old_value; break;
            case VALUES:             byte = ((copied_data_swath *)swath)->copied_bytes[index + i]; break;
            default: assert(false);
        }
        
            *((uint8_t *)(&val.   int_value) + i) = byte;
            *((uint8_t *)(&val.double_value) + i) = byte;
        if (i < sizeof(float))
            *((uint8_t *)(&val. float_value) + i) = byte;
    }
    
    return val;
}

value_t data_to_val(unknown_type_of_swath *swath, long index, data_array_type_t type)
{
	return data_to_val_aux(swath, index, swath->number_of_bytes, type);
}

void * remote_address_of_nth_element(unknown_type_of_swath *swath, long n, data_array_type_t type)
{
    return swath->first_byte_in_child + n;
}

void * remote_address_of_last_element(unknown_type_of_swath *swath, data_array_type_t type)
{
    return (remote_address_of_nth_element(swath, index_of_last_element(swath, type), type));
}

void * local_address_beyond_nth_element(unknown_type_of_swath *swath, long n, data_array_type_t type)
{
    switch(type)
    {
        case MATCHES: return &((matches_swath *)swath)->match_info[n + 1];
        case MATCHES_AND_VALUES: return &((matches_and_old_values_swath *)swath)->data[n + 1];
        case VALUES: return &((copied_data_swath *)swath)->copied_bytes[n + 1];
        default: assert(false);
    }
}

void * local_address_beyond_last_element(unknown_type_of_swath *swath, data_array_type_t type)
{
    return (local_address_beyond_nth_element(swath, index_of_last_element(swath, type), type));
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
            reading_swath_index = local_address_beyond_last_element((unknown_type_of_swath *)reading_swath_index, MATCHES_AND_VALUES);
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
            writing_swath_index = add_element((unknown_type_of_array **)(&matches), (unknown_type_of_swath *)writing_swath_index, address, &reading_swath_index->data[reading_iterator], MATCHES_AND_VALUES);

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
    
    if (!(matches = null_terminate((unknown_type_of_array *)matches, (unknown_type_of_swath *)writing_swath_index, MATCHES_AND_VALUES)))
    {
        return NULL;
    }
    
    return matches;
}
