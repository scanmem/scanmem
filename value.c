/*
    Simple routines for working with the value_t data structure.

    Copyright (C) 2006,2007,2009 Tavis Ormandy <taviso@sdf.lonestar.org>
    Copyright (C) 2009           Eli Dupree <elidupree@charter.net>
    Copyright (C) 2009,2010      WANG Lu <coolwanglu@gmail.com>

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

#include "config.h"

#include <config.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <limits.h>
#include <assert.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>

#include "value.h"
#include "scanroutines.h"
#include "scanmem.h"

void valtostr(const value_t *val, char *str, size_t n)
{
    char buf[128];
    int np = 0;
    int max_bytes = 0;
    bool print_as_unsigned = false;

#define FLAG_MACRO(bytes, string) (val->flags.u##bytes##b && val->flags.s##bytes##b) ? (string " ") : (val->flags.u##bytes##b) ? (string "u ") : (val->flags.s##bytes##b) ? (string "s ") : ""
    
    /* set the flags */
    np = snprintf(buf, sizeof(buf), "[%s%s%s%s%s%s%s]",
         FLAG_MACRO(64, "I64"),
         FLAG_MACRO(32, "I32"),
         FLAG_MACRO(16, "I16"),
         FLAG_MACRO(8,  "I8"),
         val->flags.f64b ? "F64 " : "",
         val->flags.f32b ? "F32 " : "",
         (val->flags.ineq_reverse && !val->flags.ineq_forwards) ? "(reversed inequality) " : "");
    /* handle having no type at all */
    if (np <= 2)
        goto err;

         if (val->flags.u64b) { max_bytes = 8; print_as_unsigned =  true; }
    else if (val->flags.s64b) { max_bytes = 8; print_as_unsigned = false; }
    else if (val->flags.u32b) { max_bytes = 4; print_as_unsigned =  true; }
    else if (val->flags.s32b) { max_bytes = 4; print_as_unsigned = false; }
    else if (val->flags.u16b) { max_bytes = 2; print_as_unsigned =  true; }
    else if (val->flags.s16b) { max_bytes = 2; print_as_unsigned = false; }
    else if (val->flags.u8b ) { max_bytes = 1; print_as_unsigned =  true; }
    else if (val->flags.s8b ) { max_bytes = 1; print_as_unsigned = false; }

    /* find the right format, considering different integer size implementations */
         if (max_bytes == sizeof(long long)) np = snprintf(str, n, print_as_unsigned ? "%llu, %s" : "%lld, %s", print_as_unsigned ? get_ulonglong(val) : get_slonglong(val), buf);
    else if (max_bytes == sizeof(long))      np = snprintf(str, n, print_as_unsigned ? "%lu, %s"  : "%ld, %s" , print_as_unsigned ? get_ulong(val) : get_slong(val), buf);
    else if (max_bytes == sizeof(int))       np = snprintf(str, n, print_as_unsigned ? "%u, %s"   : "%d, %s"  , print_as_unsigned ? get_uint(val) : get_sint(val), buf);
    else if (max_bytes == sizeof(short))     np = snprintf(str, n, print_as_unsigned ? "%hu, %s"  : "%hd, %s" , print_as_unsigned ? get_ushort(val) : get_sshort(val), buf);
    else if (max_bytes == sizeof(char))      np = snprintf(str, n, print_as_unsigned ? "%hhu, %s" : "%hhd, %s", print_as_unsigned ? get_uchar(val) : get_schar(val), buf);
    else if (val->flags.f64b) np = snprintf(str, n, "%lf, %s", get_f64b(val), buf);
    else if (val->flags.f32b) np = snprintf(str, n, "%f, %s", get_f32b(val), buf);
    else
        goto err;
    if (np <= 0 || np >= (n - 1))
        goto err;

    return;
err:
    /* always print a value and a type for not crashing the GUI */
    strncpy(str, "unknown, [unknown]", n);
}

void valcpy(value_t * dst, const value_t * src)
{
    memcpy(dst, src, sizeof(value_t));
    return;
}

/* dst.flags must be set beforehand */
void uservalue2value(value_t *dst, const uservalue_t *src)
{
    if(dst->flags.u8b) set_u8b(dst, get_u8b(src));
    if(dst->flags.s8b) set_s8b(dst, get_s8b(src));
    if(dst->flags.u16b) set_u16b(dst, get_u16b(src));
    if(dst->flags.s16b) set_s16b(dst, get_s16b(src));
    if(dst->flags.u32b) set_u32b(dst, get_u32b(src));
    if(dst->flags.s32b) set_s32b(dst, get_s32b(src));
    if(dst->flags.u64b) set_u64b(dst, get_u64b(src));
    if(dst->flags.s64b) set_s64b(dst, get_s64b(src));
    /* I guess integer and float cannot be matched together */
    if(dst->flags.f32b) set_f32b(dst, get_f32b(src));
    if(dst->flags.f64b) set_f64b(dst, get_f64b(src));
}

void truncval_to_flags(value_t * dst, match_flags flags)
{
    assert(dst != NULL);
    
    dst->flags.u64b &= flags.u64b;
    dst->flags.s64b &= flags.s64b;
    dst->flags.f64b &= flags.f64b;
    dst->flags.u32b &= flags.u32b;
    dst->flags.s32b &= flags.s32b;
    dst->flags.f32b &= flags.f32b;
    dst->flags.u16b &= flags.u16b;
    dst->flags.s16b &= flags.s16b;
    dst->flags.u8b  &= flags.u8b ;
    dst->flags.s8b  &= flags.s8b ;
    
    /* Hack - simply overwrite the inequality flags (this should have no effect except to make them display properly) */
    dst->flags.ineq_forwards = flags.ineq_forwards;
    dst->flags.ineq_reverse  = flags.ineq_reverse ;
}

void truncval(value_t * dst, const value_t * src)
{
    assert(src != NULL);

    truncval_to_flags(dst, src->flags);
}

/* set all possible width flags, if nothing is known about val */
void valnowidth(value_t * val)
{
    assert(val);

    val->flags.u64b = 1;
    val->flags.s64b = 1;
    val->flags.u32b = 1;
    val->flags.s32b = 1;
    val->flags.u16b = 1;
    val->flags.s16b = 1;
    val->flags.u8b  = 1;
    val->flags.s8b  = 1;
    val->flags.f64b = 1;
    val->flags.f32b = 1;

    val->flags.ineq_forwards = 1;
    val->flags.ineq_reverse = 1;
    
    /* don't bother with bytearray_length and string_length */
    return;
}

/* array must have been allocated, of size at least argc */
bool parse_uservalue_bytearray(char **argv, unsigned argc, bytearray_element_t *array, uservalue_t *val)
{
    int i,j;

    const char *cur_str;
    char cur_char;
    char *endptr;
    bytearray_element_t *cur_element;

    for(i = 0; i < argc; ++i)
    {
        /* get current string */
        cur_str = argv[i];
        /* test its length */
        for(j = 0; (j < 3) && (cur_str[j]); ++j) {}
        if (j != 2) /* length is not 2 */
            return false;

        cur_element = array + i;
        if (strcmp(cur_str, "??") == 0)
        {
            cur_element->is_wildcard = 1;
            continue;
        }
        else
        {
            /* parse as hex integer */
            cur_char = (char)strtol(cur_str, &endptr, 16);
            if (*endptr != '\0')
                return false;

            cur_element->is_wildcard = 0;
            cur_element->byte = cur_char;
        }
    }

    /* everything is ok */
    val->bytearray_value = array;
    val->flags.bytearray_length = argc;
    return true;
}

bool parse_uservalue_number(const char *nptr, uservalue_t * val)
{
    /* TODO multiple rounding method */
    if (parse_uservalue_int(nptr, val))
    {
        val->flags.f32b = val->flags.f64b = 1;
        val->float32_value = (float) val->int64_value;
        val->float64_value = (double) val->int64_value;   
        return true;
    }
    else if(parse_uservalue_float(nptr, val))
    {
        double num = val->float64_value;
        if (num >=        (double)(0) && num < (double)(1LL<< 8)) { val->flags.u8b  = 1; set_u8b(val, (uint8_t)num); }
        if (num >= (double)-(1LL<< 7) && num < (double)(1LL<< 7)) { val->flags.s8b  = 1; set_s8b(val, (int8_t)num); }
        if (num >=        (double)(0) && num < (double)(1LL<<16)) { val->flags.u16b = 1; set_u16b(val, (uint16_t)num); }
        if (num >= (double)-(1LL<<15) && num < (double)(1LL<<15)) { val->flags.s16b = 1; set_s16b(val, (int16_t)num); }
        if (num >=        (double)(0) && num < (double)(1LL<<32)) { val->flags.u32b = 1; set_u32b(val, (uint32_t)num); }
        if (num >= (double)-(1LL<<31) && num < (double)(1LL<<31)) { val->flags.s32b = 1; set_s32b(val, (int32_t)num); }
        if (           (double)(true) &&          (double)(true)) { val->flags.u64b = 1; set_u64b(val, (uint64_t)num); }
        if (           (double)(true) &&          (double)(true)) { val->flags.s64b = 1; set_s64b(val, (int64_t)num); }
        return true;
    }

    return false;
}

bool parse_uservalue_int(const char *nptr, uservalue_t * val)
{
    int64_t num;
    char *endptr;

    assert(nptr != NULL);
    assert(val != NULL);

    memset(val, 0x00, sizeof(uservalue_t));

    /* skip past any whitespace */
    while (isspace(*nptr))
        ++nptr;

    /* now parse it using strtoul */
    errno = 0;
    num = strtoll(nptr, &endptr, 0);
    if ((errno != 0) || (*endptr != '\0'))
        return false;

    /* determine correct flags */
    if (num >=        (0) && num < (1LL<< 8)) { val->flags.u8b  = 1; set_u8b(val, (uint8_t)num); }
    if (num >= -(1LL<< 7) && num < (1LL<< 7)) { val->flags.s8b  = 1; set_s8b(val, (int8_t)num); }
    if (num >=        (0) && num < (1LL<<16)) { val->flags.u16b = 1; set_u16b(val, (uint16_t)num); }
    if (num >= -(1LL<<15) && num < (1LL<<15)) { val->flags.s16b = 1; set_s16b(val, (int16_t)num); }
    if (num >=        (0) && num < (1LL<<32)) { val->flags.u32b = 1; set_u32b(val, (uint32_t)num); }
    if (num >= -(1LL<<31) && num < (1LL<<31)) { val->flags.s32b = 1; set_s32b(val, (int32_t)num); }
    if (           (true) &&          (true)) { val->flags.u64b = 1; set_u64b(val, (uint64_t)num); }
    if (           (true) &&          (true)) { val->flags.s64b = 1; set_s64b(val, (int64_t)num); }

    return true;
}

bool parse_uservalue_float(const char *nptr, uservalue_t * val)
{
    double num;
    char *endptr;
    assert(nptr);
    assert(val);

    memset(val, 0x00, sizeof(uservalue_t));
    while (isspace(*nptr))
        ++nptr;

    errno = 0;
    num = strtod(nptr, &endptr);
    if ((errno != 0) || (*endptr != '\0'))
        return false;
    
    /* I'm not sure how to distinguish float & double, but I guess it's not necessary here */
    val->flags.f32b = val->flags.f64b = 1;
    val->float32_value = (float) num;
    val->float64_value =  num;   
    return true;
}

int flags_to_max_width_in_bytes(match_flags flags)
{
    switch(globals.options.scan_data_type)
    {
        case BYTEARRAY:
            return flags.bytearray_length;
            break;
        case STRING:
            return flags.string_length;
            break;
        default: /* numbers */
                 if (flags.u64b || flags.s64b || flags.f64b) return 8;
            else if (flags.u32b || flags.s32b || flags.f32b) return 4;
            else if (flags.u16b || flags.s16b              ) return 2;
            else if (flags.u8b  || flags.s8b               ) return 1;
            else    /* It can't be a variable of any size */ return 0;
            break;
    }
}

int val_max_width_in_bytes(value_t *val)
{
	return flags_to_max_width_in_bytes(val->flags);
}

#define DEFINE_GET_BY_SYSTEM_DEPENDENT_TYPE_FUNCTION(type, typename, signedness_letter) \
type get_##signedness_letter##typename (const value_t const* val) \
{ \
	     if (sizeof(type) <= 1) return (type)get_##signedness_letter##8b(val); \
	else if (sizeof(type) <= 2) return (type)get_##signedness_letter##16b(val); \
	else if (sizeof(type) <= 4) return (type)get_##signedness_letter##32b(val); \
	else if (sizeof(type) <= 8) return (type)get_##signedness_letter##64b(val); \
	else assert(false); \
}
#define DEFINE_GET_BY_SYSTEM_DEPENDENT_TYPE_FUNCTIONS(type, typename) \
	DEFINE_GET_BY_SYSTEM_DEPENDENT_TYPE_FUNCTION(unsigned type, typename, u) \
	DEFINE_GET_BY_SYSTEM_DEPENDENT_TYPE_FUNCTION(signed type, typename, s)

DEFINE_GET_BY_SYSTEM_DEPENDENT_TYPE_FUNCTIONS(char, char)
DEFINE_GET_BY_SYSTEM_DEPENDENT_TYPE_FUNCTIONS(short, short)
DEFINE_GET_BY_SYSTEM_DEPENDENT_TYPE_FUNCTIONS(int, int)
DEFINE_GET_BY_SYSTEM_DEPENDENT_TYPE_FUNCTIONS(long, long)
DEFINE_GET_BY_SYSTEM_DEPENDENT_TYPE_FUNCTIONS(long long, longlong)
