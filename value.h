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


#ifndef VALUE_H
#define VALUE_H

#include <stdint.h>
#include <stddef.h>
#include <assert.h>

/* some routines for working with value_t structures */

/* this is memory-efficient but DANGEROUS */
/* always keep in mind that don't mess up with bytearray_length or string_length when scanning for BYTEARRAY of STRING */
typedef union {
    struct __attribute__ ((packed)) {
        unsigned  u8b:1;        /* could be an unsigned  8-bit variable (e.g. unsigned char)      */
        unsigned u16b:1;        /* could be an unsigned 16-bit variable (e.g. unsigned short)     */
        unsigned u32b:1;        /* could be an unsigned 32-bit variable (e.g. unsigned int)       */
        unsigned u64b:1;        /* could be an unsigned 64-bit variable (e.g. unsigned long long) */
        unsigned  s8b:1;        /* could be a    signed  8-bit variable (e.g. signed char)        */
        unsigned s16b:1;        /* could be a    signed 16-bit variable (e.g. short)              */
        unsigned s32b:1;        /* could be a    signed 32-bit variable (e.g. int)                */
        unsigned s64b:1;        /* could be a    signed 64-bit variable (e.g. long long)          */
        unsigned f32b:1;        /* could be a 32-bit floating point variable (i.e. float)         */
        unsigned f64b:1;        /* could be a 64-bit floating point variable (i.e. double)        */

        unsigned ineq_forwards:1; /* Whether this value has matched inequalities used the normal way */
        unsigned ineq_reverse:1; /* Whether this value has matched inequalities in reverse */
    };

    uint16_t bytearray_length;       /* used when search for an array of bytes or text, I guess uint16_t is enough */
    uint16_t string_length;          /* used when search for a string */
} match_flags;

/* this struct describing values retrieved from target memory */
typedef struct {
    union {
        int8_t int8_value;
        uint8_t uint8_value;
        int16_t int16_value;
        uint16_t uint16_value;
        int32_t int32_value;
        uint32_t uint32_value;
        int64_t int64_value;  
        uint64_t uint64_value;  
        float float32_value;
        double float64_value;
        int8_t bytes[sizeof(int64_t)];
    };
    
    match_flags flags;
} value_t;

typedef struct{
    int8_t byte;
    int8_t is_wildcard;
} bytearray_element_t;

/* this struct describing values provided by users */
typedef struct {
    int8_t int8_value;
    uint8_t uint8_value;
    int16_t int16_value;
    uint16_t uint16_value;
    int32_t int32_value;
    uint32_t uint32_value;
    int64_t int64_value;
    uint64_t uint64_value;
    float float32_value;
    double float64_value;

    bytearray_element_t *bytearray_value;
    const char *string_value;

    match_flags flags;
} uservalue_t;

/* used when output values to user */
/* only work for numbers */
void valtostr(const value_t *val, char *str, size_t n);
bool parse_uservalue_bytearray(char **argv, unsigned argc, bytearray_element_t *array, uservalue_t * val); /* parse bytearray, the parameter array should be allocated beforehand */
bool parse_uservalue_number(const char *nptr, uservalue_t * val); /* parse int or float */
bool parse_uservalue_int(const char *nptr, uservalue_t * val);
bool parse_uservalue_float(const char *nptr, uservalue_t * val);
void valcpy(value_t * dst, const value_t * src);
void uservalue2value(value_t * dst, const uservalue_t * src); /* dst.flags must be set beforehand */
int flags_to_max_width_in_bytes(match_flags flags);
int val_max_width_in_bytes(value_t *val);

#define get_s8b(val) ((val)->int8_value)
#define get_u8b(val) ((val)->uint8_value)
#define get_s16b(val) ((val)->int16_value)
#define get_u16b(val) ((val)->uint16_value)
#define get_s32b(val) ((val)->int32_value)
#define get_u32b(val) ((val)->uint32_value)
#define get_s64b(val) ((val)->int64_value)
#define get_u64b(val) ((val)->uint64_value)
#define get_f32b(val) ((val)->float32_value)
#define get_f64b(val) ((val)->float64_value)

#define set_s8b(val, v) (((val)->int8_value) = v)
#define set_u8b(val, v) (((val)->uint8_value) = v)
#define set_s16b(val, v) (((val)->int16_value) = v)
#define set_u16b(val, v) (((val)->uint16_value) = v)
#define set_s32b(val, v) (((val)->int32_value) = v)
#define set_u32b(val, v) (((val)->uint32_value) = v)
#define set_s64b(val, v) (((val)->int64_value) = v)
#define set_u64b(val, v) (((val)->uint64_value) = v)
#define set_f32b(val, v) (((val)->float32_value) = v)
#define set_f64b(val, v) (((val)->float64_value) = v)

#define DECLARE_GET_BY_SYSTEM_DEPENDENT_TYPE_FUNCTIONS(type, typename) \
    unsigned type get_u##typename (const value_t const *val); \
    signed type get_s##typename (const value_t const *val); 

DECLARE_GET_BY_SYSTEM_DEPENDENT_TYPE_FUNCTIONS(char, char);
DECLARE_GET_BY_SYSTEM_DEPENDENT_TYPE_FUNCTIONS(short, short);
DECLARE_GET_BY_SYSTEM_DEPENDENT_TYPE_FUNCTIONS(int, int);
DECLARE_GET_BY_SYSTEM_DEPENDENT_TYPE_FUNCTIONS(long, long);
DECLARE_GET_BY_SYSTEM_DEPENDENT_TYPE_FUNCTIONS(long long, longlong);

static inline void truncval_to_flags(value_t *dst, match_flags flags)
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
    dst->flags.u8b  &= flags.u8b;
    dst->flags.s8b  &= flags.s8b;

    /* Hack - simply overwrite the inequality flags (this should
       have no effect except to make them display properly) */
    dst->flags.ineq_forwards = flags.ineq_forwards;
    dst->flags.ineq_reverse  = flags.ineq_reverse;
}

static inline void truncval(value_t *dst, const value_t *src)
{
    assert(src != NULL);

    truncval_to_flags(dst, src->flags);
}

/* set all possible width flags, if nothing is known about val */
static inline void valnowidth(value_t *val)
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

#endif /* VALUE_H */
