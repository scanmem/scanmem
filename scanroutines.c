/*
    Scanning routines for different data types.

    Copyright (C) 2009,2010 WANG Lu  <coolwanglu(a)gmail.com>
    Copyright (C) 2017 Andrea Stacchiotti  <andreastacchiotti(a)gmail.com>

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

#include <assert.h>
#include <stdbool.h>

#include "scanroutines.h"
#include "common.h"
#include "endianness.h"
#include "value.h"


/* for convenience */
#define SCAN_ROUTINE_ARGUMENTS (const mem64_t *memory_ptr, size_t memlength, const value_t *old_value, const uservalue_t *user_value, match_flags *saveflags)
unsigned int (*sm_scan_routine) SCAN_ROUTINE_ARGUMENTS;

#define MEMORY_COMP(value,field,op)  (((value)->flags & flag_##field) && (get_##field(memory_ptr) op get_##field(value)))
#define GET_FLAG(valptr, field)      ((valptr)->flags & flag_##field)
#define SET_FLAG(flagsptr, field)    ((*(flagsptr)) |= flag_##field)


/********************/
/* Integer specific */
/********************/

/* for MATCHANY */
#define DEFINE_INTEGER_MATCHANY_ROUTINE(DATAWIDTH) \
    extern inline unsigned int scan_routine_INTEGER##DATAWIDTH##_ANY SCAN_ROUTINE_ARGUMENTS \
    { \
        if (memlength >= (DATAWIDTH)/8) { \
            SET_FLAG(saveflags, s##DATAWIDTH##b); \
            SET_FLAG(saveflags, u##DATAWIDTH##b); \
            return (DATAWIDTH)/8; \
        } \
        else { \
            return 0; \
        } \
    }

DEFINE_INTEGER_MATCHANY_ROUTINE( 8)
DEFINE_INTEGER_MATCHANY_ROUTINE(16)
DEFINE_INTEGER_MATCHANY_ROUTINE(32)
DEFINE_INTEGER_MATCHANY_ROUTINE(64)


#define DEFINE_INTEGER_MATCHUPDATE_ROUTINE(DATAWIDTH) \
    extern inline unsigned int scan_routine_INTEGER##DATAWIDTH##_UPDATE SCAN_ROUTINE_ARGUMENTS \
    { \
        if (memlength < (DATAWIDTH)/8) return 0; \
        int ret = 0; \
        if (GET_FLAG(old_value, s##DATAWIDTH##b)) { ret = (DATAWIDTH)/8; SET_FLAG(saveflags, s##DATAWIDTH##b); } \
        if (GET_FLAG(old_value, u##DATAWIDTH##b)) { ret = (DATAWIDTH)/8; SET_FLAG(saveflags, u##DATAWIDTH##b); } \
        return ret; \
    }

DEFINE_INTEGER_MATCHUPDATE_ROUTINE( 8)
DEFINE_INTEGER_MATCHUPDATE_ROUTINE(16)
DEFINE_INTEGER_MATCHUPDATE_ROUTINE(32)
DEFINE_INTEGER_MATCHUPDATE_ROUTINE(64)


#define DEFINE_INTEGER_ROUTINE(DATAWIDTH, MATCHTYPENAME, MATCHTYPE, VALUE_TO_COMPARE_WITH, REVENDIAN, REVEND_STR) \
    extern inline unsigned int scan_routine_INTEGER##DATAWIDTH##_##MATCHTYPENAME##REVEND_STR SCAN_ROUTINE_ARGUMENTS \
    { \
        if (memlength < (DATAWIDTH)/8) return 0; \
        int ret = 0; \
        mem64_t val; \
        if (REVENDIAN) { \
            val.uint##DATAWIDTH##_value = swap_bytes##DATAWIDTH (memory_ptr->uint##DATAWIDTH##_value); \
            memory_ptr = &val; \
        } \
        if (MEMORY_COMP(VALUE_TO_COMPARE_WITH, s##DATAWIDTH##b, MATCHTYPE)) { \
            SET_FLAG(saveflags, s##DATAWIDTH##b); \
            ret = (DATAWIDTH)/8; \
        } \
        if (MEMORY_COMP(VALUE_TO_COMPARE_WITH, u##DATAWIDTH##b, MATCHTYPE)) { \
            SET_FLAG(saveflags, u##DATAWIDTH##b); \
            ret = (DATAWIDTH)/8; \
        } \
        return ret; \
    }

#define DEFINE_INTEGER_ROUTINE_FOR_ALL_INTEGER_TYPES(MATCHTYPENAME, MATCHTYPE, VALUE_TO_COMPARE_WITH) \
    DEFINE_INTEGER_ROUTINE( 8, MATCHTYPENAME, MATCHTYPE, VALUE_TO_COMPARE_WITH, 0, ) \
    DEFINE_INTEGER_ROUTINE(16, MATCHTYPENAME, MATCHTYPE, VALUE_TO_COMPARE_WITH, 0, ) \
    DEFINE_INTEGER_ROUTINE(32, MATCHTYPENAME, MATCHTYPE, VALUE_TO_COMPARE_WITH, 0, ) \
    DEFINE_INTEGER_ROUTINE(64, MATCHTYPENAME, MATCHTYPE, VALUE_TO_COMPARE_WITH, 0, )

#define DEFINE_INTEGER_ROUTINE_FOR_ALL_INTEGER_TYPES_AND_ENDIANS(MATCHTYPENAME, MATCHTYPE, VALUE_TO_COMPARE_WITH) \
    DEFINE_INTEGER_ROUTINE( 8, MATCHTYPENAME, MATCHTYPE, VALUE_TO_COMPARE_WITH, 0, ) \
    DEFINE_INTEGER_ROUTINE(16, MATCHTYPENAME, MATCHTYPE, VALUE_TO_COMPARE_WITH, 0, ) \
    DEFINE_INTEGER_ROUTINE(32, MATCHTYPENAME, MATCHTYPE, VALUE_TO_COMPARE_WITH, 0, ) \
    DEFINE_INTEGER_ROUTINE(64, MATCHTYPENAME, MATCHTYPE, VALUE_TO_COMPARE_WITH, 0, ) \
    DEFINE_INTEGER_ROUTINE(16, MATCHTYPENAME, MATCHTYPE, VALUE_TO_COMPARE_WITH, 1, _REVENDIAN) \
    DEFINE_INTEGER_ROUTINE(32, MATCHTYPENAME, MATCHTYPE, VALUE_TO_COMPARE_WITH, 1, _REVENDIAN) \
    DEFINE_INTEGER_ROUTINE(64, MATCHTYPENAME, MATCHTYPE, VALUE_TO_COMPARE_WITH, 1, _REVENDIAN)

DEFINE_INTEGER_ROUTINE_FOR_ALL_INTEGER_TYPES_AND_ENDIANS(EQUALTO, ==, user_value)
DEFINE_INTEGER_ROUTINE_FOR_ALL_INTEGER_TYPES_AND_ENDIANS(NOTEQUALTO, !=, user_value)
DEFINE_INTEGER_ROUTINE_FOR_ALL_INTEGER_TYPES_AND_ENDIANS(GREATERTHAN, >, user_value)
DEFINE_INTEGER_ROUTINE_FOR_ALL_INTEGER_TYPES_AND_ENDIANS(LESSTHAN, <, user_value)
DEFINE_INTEGER_ROUTINE_FOR_ALL_INTEGER_TYPES(NOTCHANGED, ==, old_value)
DEFINE_INTEGER_ROUTINE_FOR_ALL_INTEGER_TYPES(CHANGED, !=, old_value)
DEFINE_INTEGER_ROUTINE_FOR_ALL_INTEGER_TYPES(INCREASED, >, old_value)
DEFINE_INTEGER_ROUTINE_FOR_ALL_INTEGER_TYPES(DECREASED, <, old_value)

/******************/
/* Float specific */
/******************/

/* for MATCHANY */
#define DEFINE_FLOAT_MATCHANY_ROUTINE(DATAWIDTH) \
    extern inline unsigned int scan_routine_FLOAT##DATAWIDTH##_ANY SCAN_ROUTINE_ARGUMENTS \
    { \
        if (memlength >= (DATAWIDTH)/8) { \
            SET_FLAG(saveflags, f##DATAWIDTH##b); \
            return (DATAWIDTH)/8; \
        } \
        else { \
            return 0; \
        } \
    }

DEFINE_FLOAT_MATCHANY_ROUTINE(32)
DEFINE_FLOAT_MATCHANY_ROUTINE(64)


#define DEFINE_FLOAT_MATCHUPDATE_ROUTINE(DATAWIDTH) \
    extern inline unsigned int scan_routine_FLOAT##DATAWIDTH##_UPDATE SCAN_ROUTINE_ARGUMENTS \
    { \
        if (memlength < (DATAWIDTH)/8) return 0; \
        int ret = 0; \
        if (GET_FLAG(old_value, f##DATAWIDTH##b)) { ret = (DATAWIDTH)/8; SET_FLAG(saveflags, f##DATAWIDTH##b); } \
        return ret; \
    }

DEFINE_FLOAT_MATCHUPDATE_ROUTINE(32)
DEFINE_FLOAT_MATCHUPDATE_ROUTINE(64)


#define DEFINE_FLOAT_ROUTINE(DATAWIDTH, MATCHTYPENAME, MATCHTYPE, VALUE_TO_COMPARE_WITH, REVENDIAN, REVEND_STR) \
    extern inline unsigned int scan_routine_FLOAT##DATAWIDTH##_##MATCHTYPENAME##REVEND_STR SCAN_ROUTINE_ARGUMENTS \
    { \
        if (memlength < (DATAWIDTH)/8) return 0; \
        int ret = 0; \
        mem64_t val; \
        if (REVENDIAN) { \
            val.uint##DATAWIDTH##_value = swap_bytes##DATAWIDTH (memory_ptr->uint##DATAWIDTH##_value); \
            memory_ptr = &val; \
        } \
        if (MEMORY_COMP(VALUE_TO_COMPARE_WITH, f##DATAWIDTH##b, MATCHTYPE)) { \
            SET_FLAG(saveflags,f##DATAWIDTH##b); \
            ret = (DATAWIDTH)/8; \
        } \
        return ret; \
    }

#define DEFINE_FLOAT_ROUTINE_FOR_ALL_FLOAT_TYPES(MATCHTYPENAME, MATCHTYPE, VALUE_TO_COMPARE_WITH, REVENDIAN, REVEND_STR) \
    DEFINE_FLOAT_ROUTINE(32, MATCHTYPENAME, MATCHTYPE, VALUE_TO_COMPARE_WITH, REVENDIAN, REVEND_STR) \
    DEFINE_FLOAT_ROUTINE(64, MATCHTYPENAME, MATCHTYPE, VALUE_TO_COMPARE_WITH, REVENDIAN, REVEND_STR)

#define DEFINE_FLOAT_ROUTINE_FOR_ALL_FLOAT_TYPES_AND_ENDIANS(MATCHTYPENAME, MATCHTYPE, VALUE_TO_COMPARE_WITH) \
    DEFINE_FLOAT_ROUTINE_FOR_ALL_FLOAT_TYPES(MATCHTYPENAME, MATCHTYPE, VALUE_TO_COMPARE_WITH, 0, ) \
    DEFINE_FLOAT_ROUTINE_FOR_ALL_FLOAT_TYPES(MATCHTYPENAME, MATCHTYPE, VALUE_TO_COMPARE_WITH, 1, _REVENDIAN)

DEFINE_FLOAT_ROUTINE_FOR_ALL_FLOAT_TYPES_AND_ENDIANS(EQUALTO, ==, user_value)
DEFINE_FLOAT_ROUTINE_FOR_ALL_FLOAT_TYPES_AND_ENDIANS(NOTEQUALTO, !=, user_value)
DEFINE_FLOAT_ROUTINE_FOR_ALL_FLOAT_TYPES_AND_ENDIANS(GREATERTHAN, >, user_value)
DEFINE_FLOAT_ROUTINE_FOR_ALL_FLOAT_TYPES_AND_ENDIANS(LESSTHAN, <, user_value)
DEFINE_FLOAT_ROUTINE_FOR_ALL_FLOAT_TYPES(NOTCHANGED, ==, old_value, 0, )
DEFINE_FLOAT_ROUTINE_FOR_ALL_FLOAT_TYPES(CHANGED, !=, old_value, 0, )
DEFINE_FLOAT_ROUTINE_FOR_ALL_FLOAT_TYPES(INCREASED, >, old_value, 0, )
DEFINE_FLOAT_ROUTINE_FOR_ALL_FLOAT_TYPES(DECREASED, <, old_value, 0, )

/********************/
/* Special routines */
/********************/

/*---------------------------------*/
/* for INCREASEDBY and DECREASEDBY */
/*---------------------------------*/
#define DEFINE_INTEGER_OPERATIONBY_ROUTINE(DATAWIDTH, NAME, OP) \
    extern inline unsigned int scan_routine_INTEGER##DATAWIDTH##_##NAME SCAN_ROUTINE_ARGUMENTS \
    { \
        if (memlength < (DATAWIDTH)/8) return 0; \
        int ret = 0; \
        if ((GET_FLAG(old_value, s##DATAWIDTH##b)) && (GET_FLAG(user_value, s##DATAWIDTH##b)) && \
            (get_s##DATAWIDTH##b(memory_ptr) == get_s##DATAWIDTH##b(old_value) OP get_s##DATAWIDTH##b(user_value))) \
            { ret = (DATAWIDTH)/8; SET_FLAG(saveflags, s##DATAWIDTH##b); } \
        if ((GET_FLAG(old_value, u##DATAWIDTH##b)) && (GET_FLAG(user_value, u##DATAWIDTH##b)) && \
            (get_u##DATAWIDTH##b(memory_ptr) == get_u##DATAWIDTH##b(old_value) OP get_u##DATAWIDTH##b(user_value))) \
            { ret = (DATAWIDTH)/8; SET_FLAG(saveflags, u##DATAWIDTH##b); } \
        return ret; \
    }

#define DEFINE_INTEGER_INCREASEDBY_DECREASEDBY_ROUTINE(DATAWIDTH) \
    DEFINE_INTEGER_OPERATIONBY_ROUTINE(DATAWIDTH, INCREASEDBY, +) \
    DEFINE_INTEGER_OPERATIONBY_ROUTINE(DATAWIDTH, DECREASEDBY, -)

DEFINE_INTEGER_INCREASEDBY_DECREASEDBY_ROUTINE( 8)
DEFINE_INTEGER_INCREASEDBY_DECREASEDBY_ROUTINE(16)
DEFINE_INTEGER_INCREASEDBY_DECREASEDBY_ROUTINE(32)
DEFINE_INTEGER_INCREASEDBY_DECREASEDBY_ROUTINE(64)

#define DEFINE_FLOAT_OPERATIONBY_ROUTINE(DATAWIDTH, NAME, OP) \
    extern inline unsigned int scan_routine_FLOAT##DATAWIDTH##_##NAME SCAN_ROUTINE_ARGUMENTS \
    { \
        if (memlength < (DATAWIDTH)/8) return 0; \
        int ret = 0; \
        if ((GET_FLAG(old_value, f##DATAWIDTH##b)) && (GET_FLAG(user_value, f##DATAWIDTH##b)) && \
            (get_f##DATAWIDTH##b(memory_ptr) == get_f##DATAWIDTH##b(old_value) OP get_f##DATAWIDTH##b(user_value))) \
            { ret = (DATAWIDTH)/8; SET_FLAG(saveflags, f##DATAWIDTH##b); } \
        return ret; \
    }

#define DEFINE_FLOAT_INCREASEDBY_DECREASEDBY_ROUTINE(DATAWIDTH) \
    DEFINE_FLOAT_OPERATIONBY_ROUTINE(DATAWIDTH, INCREASEDBY, +) \
    DEFINE_FLOAT_OPERATIONBY_ROUTINE(DATAWIDTH, DECREASEDBY, -)

DEFINE_FLOAT_INCREASEDBY_DECREASEDBY_ROUTINE(32)
DEFINE_FLOAT_INCREASEDBY_DECREASEDBY_ROUTINE(64)

/*-----------*/
/* for RANGE */
/*-----------*/

#define DEFINE_INTEGER_RANGE_ROUTINE(DATAWIDTH, REVENDIAN, REVEND_STR) \
    extern inline unsigned int scan_routine_INTEGER##DATAWIDTH##_RANGE##REVEND_STR SCAN_ROUTINE_ARGUMENTS \
    { \
        int ret = 0; \
        mem64_t val; \
        if (REVENDIAN) { \
            val.uint##DATAWIDTH##_value = swap_bytes##DATAWIDTH (memory_ptr->uint##DATAWIDTH##_value); \
            memory_ptr = &val; \
        } \
        if ((memlength >= (DATAWIDTH)/8) \
                && (user_value[0].flags & flag_s##DATAWIDTH##b) \
                && (get_s##DATAWIDTH##b(memory_ptr) >= get_s##DATAWIDTH##b(&user_value[0])) \
                && (get_s##DATAWIDTH##b(memory_ptr) <= get_s##DATAWIDTH##b(&user_value[1]))) \
            { ret = (DATAWIDTH)/8; SET_FLAG(saveflags, s##DATAWIDTH##b); } \
        if ((memlength >= (DATAWIDTH)/8) \
                && (user_value[0].flags & flag_u##DATAWIDTH##b) \
                && (get_u##DATAWIDTH##b(memory_ptr) >= get_u##DATAWIDTH##b(&user_value[0])) \
                && (get_u##DATAWIDTH##b(memory_ptr) <= get_u##DATAWIDTH##b(&user_value[1]))) \
            { ret = (DATAWIDTH)/8; SET_FLAG(saveflags, u##DATAWIDTH##b); } \
        return ret; \
    }

DEFINE_INTEGER_RANGE_ROUTINE( 8, 0, )
DEFINE_INTEGER_RANGE_ROUTINE(16, 0, )
DEFINE_INTEGER_RANGE_ROUTINE(16, 1, _REVENDIAN)
DEFINE_INTEGER_RANGE_ROUTINE(32, 0, )
DEFINE_INTEGER_RANGE_ROUTINE(32, 1, _REVENDIAN)
DEFINE_INTEGER_RANGE_ROUTINE(64, 0, )
DEFINE_INTEGER_RANGE_ROUTINE(64, 1, _REVENDIAN)

#define DEFINE_FLOAT_RANGE_ROUTINE(DATAWIDTH, REVENDIAN, REVEND_STR) \
    extern inline unsigned int scan_routine_FLOAT##DATAWIDTH##_RANGE##REVEND_STR SCAN_ROUTINE_ARGUMENTS \
    { \
        int ret = 0; \
        mem64_t val; \
        if (REVENDIAN) { \
            val.uint##DATAWIDTH##_value = swap_bytes##DATAWIDTH (memory_ptr->uint##DATAWIDTH##_value); \
            memory_ptr = &val; \
        } \
        if ((memlength >= (DATAWIDTH)/8) \
                && (user_value[0].flags & flag_f##DATAWIDTH##b) \
                && (get_f##DATAWIDTH##b(memory_ptr) >= get_f##DATAWIDTH##b(&user_value[0])) \
                && (get_f##DATAWIDTH##b(memory_ptr) <= get_f##DATAWIDTH##b(&user_value[1]))) \
            { ret = (DATAWIDTH)/8; SET_FLAG(saveflags, f##DATAWIDTH##b); } \
        return ret; \
    }

DEFINE_FLOAT_RANGE_ROUTINE(32, 0, )
DEFINE_FLOAT_RANGE_ROUTINE(32, 1, _REVENDIAN)
DEFINE_FLOAT_RANGE_ROUTINE(64, 0, )
DEFINE_FLOAT_RANGE_ROUTINE(64, 1, _REVENDIAN)


/*------------------------*/
/* Any-xxx types specific */
/*------------------------*/
/* this is for anynumber, anyinteger, anyfloat */
#define DEFINE_ANYTYPE_ROUTINE(MATCHTYPENAME, REVEND_STR) \
    extern inline unsigned int scan_routine_ANYINTEGER_##MATCHTYPENAME##REVEND_STR SCAN_ROUTINE_ARGUMENTS \
    { \
        int ret = scan_routine_INTEGER8_##MATCHTYPENAME (memory_ptr, memlength, old_value, user_value, saveflags); \
        int tmp_ret; \
        if ((tmp_ret = scan_routine_INTEGER16_##MATCHTYPENAME##REVEND_STR (memory_ptr, memlength, old_value, user_value, saveflags)) > ret) { ret = tmp_ret; } \
        if ((tmp_ret = scan_routine_INTEGER32_##MATCHTYPENAME##REVEND_STR (memory_ptr, memlength, old_value, user_value, saveflags)) > ret) { ret = tmp_ret; } \
        if ((tmp_ret = scan_routine_INTEGER64_##MATCHTYPENAME##REVEND_STR (memory_ptr, memlength, old_value, user_value, saveflags)) > ret) { ret = tmp_ret; } \
        return ret; \
    } \
    extern inline unsigned int scan_routine_ANYFLOAT_##MATCHTYPENAME##REVEND_STR SCAN_ROUTINE_ARGUMENTS \
    { \
        int ret = scan_routine_FLOAT32_##MATCHTYPENAME##REVEND_STR (memory_ptr, memlength, old_value, user_value, saveflags); \
        int tmp_ret; \
        if ((tmp_ret = scan_routine_FLOAT64_##MATCHTYPENAME##REVEND_STR (memory_ptr, memlength, old_value, user_value, saveflags)) > ret) { ret = tmp_ret; } \
        return ret; \
    } \
    extern inline unsigned int scan_routine_ANYNUMBER_##MATCHTYPENAME##REVEND_STR SCAN_ROUTINE_ARGUMENTS \
    { \
        int ret1 = scan_routine_ANYINTEGER_##MATCHTYPENAME##REVEND_STR (memory_ptr, memlength, old_value, user_value, saveflags); \
        int ret2 = scan_routine_ANYFLOAT_##MATCHTYPENAME##REVEND_STR (memory_ptr, memlength, old_value, user_value, saveflags); \
        return (ret1 > ret2 ? ret1 : ret2); \
    } \

DEFINE_ANYTYPE_ROUTINE(ANY, )
DEFINE_ANYTYPE_ROUTINE(UPDATE, )

DEFINE_ANYTYPE_ROUTINE(EQUALTO, )
DEFINE_ANYTYPE_ROUTINE(NOTEQUALTO, )
DEFINE_ANYTYPE_ROUTINE(CHANGED, )
DEFINE_ANYTYPE_ROUTINE(NOTCHANGED, )
DEFINE_ANYTYPE_ROUTINE(INCREASED, )
DEFINE_ANYTYPE_ROUTINE(DECREASED, )
DEFINE_ANYTYPE_ROUTINE(GREATERTHAN, )
DEFINE_ANYTYPE_ROUTINE(LESSTHAN, )
DEFINE_ANYTYPE_ROUTINE(INCREASEDBY, )
DEFINE_ANYTYPE_ROUTINE(DECREASEDBY, )
DEFINE_ANYTYPE_ROUTINE(RANGE, )

DEFINE_ANYTYPE_ROUTINE(EQUALTO, _REVENDIAN)
DEFINE_ANYTYPE_ROUTINE(NOTEQUALTO, _REVENDIAN)
DEFINE_ANYTYPE_ROUTINE(GREATERTHAN, _REVENDIAN)
DEFINE_ANYTYPE_ROUTINE(LESSTHAN, _REVENDIAN)
DEFINE_ANYTYPE_ROUTINE(RANGE, _REVENDIAN)

/*----------------------------------------*/
/* for generic VLT (Variable Length Type) */
/*----------------------------------------*/

extern inline unsigned int scan_routine_VLT_ANY SCAN_ROUTINE_ARGUMENTS
{
   return *saveflags = MIN(memlength, (uint16_t)(-1));
}

extern inline unsigned int scan_routine_VLT_UPDATE SCAN_ROUTINE_ARGUMENTS
{
    /* memlength here is already MIN(memlength, old_value->flags.length) */
   return *saveflags = memlength;
}

/*---------------*/
/* for BYTEARRAY */
/*---------------*/

/* Used only for length>8 */
extern inline unsigned int scan_routine_BYTEARRAY_EQUALTO SCAN_ROUTINE_ARGUMENTS
{
    const uint8_t *bytes_array = user_value->bytearray_value;
    const wildcard_t *wildcards_array = user_value->wildcard_value;
    unsigned int length = user_value->flags;
    if (memlength < length ||
        *((uint64_t*)bytes_array) != (memory_ptr->uint64_value & *((uint64_t*)wildcards_array)))
    {
        /* not matched */
        return 0;
    }

    unsigned int i, j;
    for(i = sizeof(uint64_t); i + sizeof(uint64_t) <= length; i += sizeof(uint64_t))
    {
        if (*((uint64_t*)(bytes_array+i)) != (((mem64_t*)(memory_ptr->bytes+i))->uint64_value & *((uint64_t*)(wildcards_array+i))))
        {
            /* not matched */
            return 0;
        }

    }

    /* match bytes left */
    if (i < length)
    {
        for(j = 0; j < length - i; ++j)
        {
            if ((bytes_array+i)[j] != (((mem64_t*)(memory_ptr->bytes+i))->bytes[j] & (wildcards_array+i)[j]))
            {
                /* not matched */
                return 0;
            }
        }
    }

    /* matched */
    *saveflags = length;

    return length;
}

/* optimized routines for small lengths
   careful: WIDTH = 8*LENGTH */

#define DEFINE_BYTEARRAY_POW2_EQUALTO_ROUTINE(WIDTH) \
    extern inline unsigned int scan_routine_BYTEARRAY##WIDTH##_EQUALTO SCAN_ROUTINE_ARGUMENTS \
    { \
        if (memlength >= (WIDTH)/8 && \
            ((get_u##WIDTH##b(memory_ptr) & *(uint##WIDTH##_t*)user_value->wildcard_value) \
              == *(uint##WIDTH##_t*)(user_value->bytearray_value))) \
        { \
            /* matched */ \
            *saveflags = (WIDTH)/8; \
            return (WIDTH)/8; \
        } \
        else \
        { \
            /* not matched */ \
            return 0; \
        } \
    }

DEFINE_BYTEARRAY_POW2_EQUALTO_ROUTINE(8)
DEFINE_BYTEARRAY_POW2_EQUALTO_ROUTINE(16)
DEFINE_BYTEARRAY_POW2_EQUALTO_ROUTINE(32)
DEFINE_BYTEARRAY_POW2_EQUALTO_ROUTINE(64)

#define DEFINE_BYTEARRAY_SMALLOOP_EQUALTO_ROUTINE(WIDTH) \
    extern inline unsigned int scan_routine_BYTEARRAY##WIDTH##_EQUALTO SCAN_ROUTINE_ARGUMENTS \
    { \
        if (memlength < (WIDTH)/8) \
        { \
            /* new_value is not actually a valid bytearray */ \
            return 0; \
        } \
        const uint8_t *bytes_array = user_value->bytearray_value; \
        const wildcard_t *wildcards_array = user_value->wildcard_value; \
        int i; \
        for(i = 0; i < (WIDTH)/8; ++i) \
        { \
            if(bytes_array[i] != (memory_ptr->bytes[i] & wildcards_array[i])) \
            { \
                /* not matched */ \
                return 0; \
            } \
        } \
        /* matched */ \
        *saveflags = (WIDTH)/8; \
        return (WIDTH)/8; \
    }

DEFINE_BYTEARRAY_SMALLOOP_EQUALTO_ROUTINE(24)
DEFINE_BYTEARRAY_SMALLOOP_EQUALTO_ROUTINE(40)
DEFINE_BYTEARRAY_SMALLOOP_EQUALTO_ROUTINE(48)
DEFINE_BYTEARRAY_SMALLOOP_EQUALTO_ROUTINE(56)

/*------------*/
/* for STRING */
/*------------*/

/* Used only for length>8 */
extern inline unsigned int scan_routine_STRING_EQUALTO SCAN_ROUTINE_ARGUMENTS
{
    const char *scan_string = user_value->string_value;
    unsigned int length = user_value->flags;
    if(memlength < length ||
       memory_ptr->int64_value != *((int64_t*)scan_string))
    {
        /* not matched */
        return 0;
    }

    unsigned int i, j;
    for(i = sizeof(int64_t); i + sizeof(int64_t) <= length; i += sizeof(int64_t))
    {
        if(((mem64_t*)(memory_ptr->chars+i))->int64_value != *((int64_t*)(scan_string+i)))
        {
            /* not matched */
            return 0;
        }
    }

    /* match bytes left */
    if (i < length)
    {
        for(j = 0; j < length - i; ++j)
        {
            if(((mem64_t*)(memory_ptr->chars+i))->chars[j] != (scan_string+i)[j])
            {
                /* not matched */
                return 0;
            }
        }
    }
    
    /* matched */
    *saveflags = length;

    return length;
}

/* optimized routines for small strings
   careful: WIDTH = 8*LENGTH */

#define DEFINE_STRING_POW2_EQUALTO_ROUTINE(WIDTH) \
    extern inline unsigned int scan_routine_STRING##WIDTH##_EQUALTO SCAN_ROUTINE_ARGUMENTS \
    { \
        if (memlength >= (WIDTH)/8 && \
            (get_s##WIDTH##b(memory_ptr) == *(int##WIDTH##_t*)(user_value->string_value))) \
        { \
            /* matched */ \
            *saveflags = (WIDTH)/8; \
            return (WIDTH)/8; \
        } \
        else \
        { \
            /* not matched */ \
            return 0; \
        } \
    }

DEFINE_STRING_POW2_EQUALTO_ROUTINE(8)
DEFINE_STRING_POW2_EQUALTO_ROUTINE(16)
DEFINE_STRING_POW2_EQUALTO_ROUTINE(32)
DEFINE_STRING_POW2_EQUALTO_ROUTINE(64)

#define DEFINE_STRING_SMALLOOP_EQUALTO_ROUTINE(WIDTH) \
    extern inline unsigned int scan_routine_STRING##WIDTH##_EQUALTO SCAN_ROUTINE_ARGUMENTS \
    { \
        if (memlength < (WIDTH)/8) \
        { \
            /* new_value is not actually a valid string */ \
            return 0; \
        } \
        const char *scan_string = user_value->string_value; \
        int i; \
        for(i = 0; i < (WIDTH)/8; ++i) \
        { \
            if(memory_ptr->chars[i] != scan_string[i]) \
            { \
                /* not matched */ \
                return 0; \
            } \
        } \
        /* matched */ \
        *saveflags = (WIDTH)/8; \
        return (WIDTH)/8; \
    }

DEFINE_STRING_SMALLOOP_EQUALTO_ROUTINE(24)
DEFINE_STRING_SMALLOOP_EQUALTO_ROUTINE(40)
DEFINE_STRING_SMALLOOP_EQUALTO_ROUTINE(48)
DEFINE_STRING_SMALLOOP_EQUALTO_ROUTINE(56)


/***************************************************************/
/* choose a routine according to scan_data_type and match_type */
/***************************************************************/


#define CHOOSE_ROUTINE(SCANDATATYPE, ROUTINEDATATYPENAME, SCANMATCHTYPE, ROUTINEMATCHTYPENAME) \
    if ((dt == SCANDATATYPE) && (mt == SCANMATCHTYPE)) \
    { \
        return &scan_routine_##ROUTINEDATATYPENAME##_##ROUTINEMATCHTYPENAME; \
    }

#define CHOOSE_ROUTINE_FOR_BOTH_ENDIANS(SCANDATATYPE, ROUTINEDATATYPENAME, SCANMATCHTYPE, ROUTINEMATCHTYPENAME) \
    if ((dt == SCANDATATYPE) && (mt == SCANMATCHTYPE)) { \
        if (reverse_endianness) { \
            return &scan_routine_##ROUTINEDATATYPENAME##_##ROUTINEMATCHTYPENAME##_REVENDIAN; \
        } \
        else { \
            return &scan_routine_##ROUTINEDATATYPENAME##_##ROUTINEMATCHTYPENAME; \
        } \
    }

#define CHOOSE_ROUTINE_FOR_ALL_NUMBER_TYPES(SCANMATCHTYPE, ROUTINEMATCHTYPENAME) \
    CHOOSE_ROUTINE(INTEGER8,   INTEGER8,   SCANMATCHTYPE, ROUTINEMATCHTYPENAME) \
    CHOOSE_ROUTINE(INTEGER16,  INTEGER16,  SCANMATCHTYPE, ROUTINEMATCHTYPENAME) \
    CHOOSE_ROUTINE(INTEGER32,  INTEGER32,  SCANMATCHTYPE, ROUTINEMATCHTYPENAME) \
    CHOOSE_ROUTINE(INTEGER64,  INTEGER64,  SCANMATCHTYPE, ROUTINEMATCHTYPENAME) \
    CHOOSE_ROUTINE(FLOAT32,    FLOAT32,    SCANMATCHTYPE, ROUTINEMATCHTYPENAME) \
    CHOOSE_ROUTINE(FLOAT64,    FLOAT64,    SCANMATCHTYPE, ROUTINEMATCHTYPENAME) \
    CHOOSE_ROUTINE(ANYINTEGER, ANYINTEGER, SCANMATCHTYPE, ROUTINEMATCHTYPENAME) \
    CHOOSE_ROUTINE(ANYFLOAT,   ANYFLOAT,   SCANMATCHTYPE, ROUTINEMATCHTYPENAME) \
    CHOOSE_ROUTINE(ANYNUMBER,  ANYNUMBER,  SCANMATCHTYPE, ROUTINEMATCHTYPENAME)

#define CHOOSE_ROUTINE_FOR_ALL_NUMBER_TYPES_AND_ENDIANS(SCANMATCHTYPE, ROUTINEMATCHTYPENAME) \
    CHOOSE_ROUTINE(INTEGER8, INTEGER8, SCANMATCHTYPE, ROUTINEMATCHTYPENAME) \
    CHOOSE_ROUTINE_FOR_BOTH_ENDIANS(INTEGER16,  INTEGER16,  SCANMATCHTYPE, ROUTINEMATCHTYPENAME) \
    CHOOSE_ROUTINE_FOR_BOTH_ENDIANS(INTEGER32,  INTEGER32,  SCANMATCHTYPE, ROUTINEMATCHTYPENAME) \
    CHOOSE_ROUTINE_FOR_BOTH_ENDIANS(INTEGER64,  INTEGER64,  SCANMATCHTYPE, ROUTINEMATCHTYPENAME) \
    CHOOSE_ROUTINE_FOR_BOTH_ENDIANS(FLOAT32,    FLOAT32,    SCANMATCHTYPE, ROUTINEMATCHTYPENAME) \
    CHOOSE_ROUTINE_FOR_BOTH_ENDIANS(FLOAT64,    FLOAT64,    SCANMATCHTYPE, ROUTINEMATCHTYPENAME) \
    CHOOSE_ROUTINE_FOR_BOTH_ENDIANS(ANYINTEGER, ANYINTEGER, SCANMATCHTYPE, ROUTINEMATCHTYPENAME) \
    CHOOSE_ROUTINE_FOR_BOTH_ENDIANS(ANYFLOAT,   ANYFLOAT,   SCANMATCHTYPE, ROUTINEMATCHTYPENAME) \
    CHOOSE_ROUTINE_FOR_BOTH_ENDIANS(ANYNUMBER,  ANYNUMBER,  SCANMATCHTYPE, ROUTINEMATCHTYPENAME)

#define SELECTION_CASE(ROUTINEDATATYPENAME, WIDTH, ROUTINEMATCHTYPENAME) \
    case (WIDTH): \
        return &scan_routine_##ROUTINEDATATYPENAME##WIDTH##_##ROUTINEMATCHTYPENAME; \
        break; \

#define CHOOSE_ROUTINE_VLT(SCANDATATYPE, ROUTINEDATATYPENAME, SCANMATCHTYPE, ROUTINEMATCHTYPENAME, WIDTH) \
    if ((dt == SCANDATATYPE) && (mt == SCANMATCHTYPE)) \
    { \
        switch (WIDTH) \
        { \
            case 0: \
                assert(false); \
                break; \
            SELECTION_CASE(ROUTINEDATATYPENAME,  8, ROUTINEMATCHTYPENAME) \
            SELECTION_CASE(ROUTINEDATATYPENAME, 16, ROUTINEMATCHTYPENAME) \
            SELECTION_CASE(ROUTINEDATATYPENAME, 24, ROUTINEMATCHTYPENAME) \
            SELECTION_CASE(ROUTINEDATATYPENAME, 32, ROUTINEMATCHTYPENAME) \
            SELECTION_CASE(ROUTINEDATATYPENAME, 40, ROUTINEMATCHTYPENAME) \
            SELECTION_CASE(ROUTINEDATATYPENAME, 48, ROUTINEMATCHTYPENAME) \
            SELECTION_CASE(ROUTINEDATATYPENAME, 56, ROUTINEMATCHTYPENAME) \
            SELECTION_CASE(ROUTINEDATATYPENAME, 64, ROUTINEMATCHTYPENAME) \
            default: \
                return &scan_routine_##ROUTINEDATATYPENAME##_##ROUTINEMATCHTYPENAME; \
                break; \
        } \
    }


scan_routine_t sm_get_scanroutine(scan_data_type_t dt, scan_match_type_t mt, match_flags uflags, bool reverse_endianness)
{
    CHOOSE_ROUTINE_FOR_ALL_NUMBER_TYPES(MATCHANY, ANY)
    CHOOSE_ROUTINE_FOR_ALL_NUMBER_TYPES(MATCHUPDATE, UPDATE)
    CHOOSE_ROUTINE_FOR_ALL_NUMBER_TYPES_AND_ENDIANS(MATCHEQUALTO, EQUALTO)
    CHOOSE_ROUTINE_FOR_ALL_NUMBER_TYPES_AND_ENDIANS(MATCHNOTEQUALTO, NOTEQUALTO)
    CHOOSE_ROUTINE_FOR_ALL_NUMBER_TYPES_AND_ENDIANS(MATCHGREATERTHAN, GREATERTHAN)
    CHOOSE_ROUTINE_FOR_ALL_NUMBER_TYPES_AND_ENDIANS(MATCHLESSTHAN, LESSTHAN)
    CHOOSE_ROUTINE_FOR_ALL_NUMBER_TYPES(MATCHCHANGED, CHANGED)
    CHOOSE_ROUTINE_FOR_ALL_NUMBER_TYPES(MATCHNOTCHANGED, NOTCHANGED)
    CHOOSE_ROUTINE_FOR_ALL_NUMBER_TYPES(MATCHINCREASED, INCREASED)
    CHOOSE_ROUTINE_FOR_ALL_NUMBER_TYPES(MATCHDECREASED, DECREASED)
    CHOOSE_ROUTINE_FOR_ALL_NUMBER_TYPES(MATCHINCREASEDBY, INCREASEDBY)
    CHOOSE_ROUTINE_FOR_ALL_NUMBER_TYPES(MATCHDECREASEDBY, DECREASEDBY)
    CHOOSE_ROUTINE_FOR_ALL_NUMBER_TYPES_AND_ENDIANS(MATCHRANGE, RANGE)

    CHOOSE_ROUTINE(BYTEARRAY, VLT, MATCHANY, ANY)
    CHOOSE_ROUTINE(BYTEARRAY, VLT, MATCHUPDATE, UPDATE)
    CHOOSE_ROUTINE_VLT(BYTEARRAY, BYTEARRAY, MATCHEQUALTO, EQUALTO, uflags*8)

    CHOOSE_ROUTINE(STRING, VLT, MATCHANY, ANY)
    CHOOSE_ROUTINE(STRING, VLT, MATCHUPDATE, UPDATE)
    CHOOSE_ROUTINE_VLT(STRING, STRING, MATCHEQUALTO, EQUALTO, uflags*8)

    return NULL;
}

/* Possible flags per scan data type: if an incoming uservalue has none of the
 * listed flags we're sure it's not going to be matched by the scan,
 * so we reject it without even trying */
static match_flags possible_flags_for_scan_data_type[] = {
    [ANYNUMBER]  = flags_all,
    [ANYINTEGER] = flags_integer,
    [ANYFLOAT]   = flags_float,
    [INTEGER8]   = flags_i8b,
    [INTEGER16]  = flags_i16b,
    [INTEGER32]  = flags_i32b,
    [INTEGER64]  = flags_i64b,
    [FLOAT32]    = flag_f32b,
    [FLOAT64]    = flag_f64b,
    [BYTEARRAY]  = flags_max,
    [STRING]     = flags_max
};

bool sm_choose_scanroutine(scan_data_type_t dt, scan_match_type_t mt, const uservalue_t* uval, bool reverse_endianness)
{
    match_flags uflags = uval ? uval->flags : flags_empty;

    /* Check scans that need an uservalue */
    if (mt == MATCHEQUALTO     ||
        mt == MATCHNOTEQUALTO  ||
        mt == MATCHGREATERTHAN ||
        mt == MATCHLESSTHAN    ||
        mt == MATCHRANGE       ||
        mt == MATCHINCREASEDBY ||
        mt == MATCHDECREASEDBY)
    {
        match_flags possible_flags = possible_flags_for_scan_data_type[dt];
        if ((possible_flags & uflags) == flags_empty) {
            /* There's no possibility to have a match, just abort */
            sm_scan_routine = NULL;
            return false;
        }
    }

    sm_scan_routine = sm_get_scanroutine(dt, mt, uflags, reverse_endianness);
    return (sm_scan_routine != NULL);
}

match_flags scan_data_type_flags(scan_data_type_t dt)
{
    return possible_flags_for_scan_data_type[dt];
}

/*----------------*/
/* Block routines */
/*----------------*/

/* what sm_checkmatches() passes as memlength for old flags of a number */
static inline size_t number_memlength(match_flags flags)
{
    return flags & flags_64b ? 8 : flags & flags_32b ? 4
                               : flags & flags_16b   ? 2
                               : flags & flags_8b    ? 1
                                                     : 0;
}

/* flatten: inline the routine, and the routines it calls, into the loops.
 * The routine reads a local copy of the `USERVALUES` user values (2 for a
 * range), which the compiler can keep in registers. */
#define DEFINE_BLOCK_ROUTINE(NAME, USERVALUES)                                              \
    static size_t __attribute__((flatten))                                                  \
    block_routine_##NAME(const uint8_t *memory, size_t count,                               \
                         const old_value_and_match_info *old,                               \
                         const uservalue_t *user_value, match_flags *restrict flags)        \
    {                                                                                       \
        uservalue_t values[USERVALUES];                                                     \
        const uservalue_t *uv = NULL;                                                       \
        size_t k, found = 0;                                                                \
        if (user_value) {                                                                   \
            memcpy(values, user_value, sizeof(values));                                     \
            uv = values;                                                                    \
        }                                                                                   \
        if (!old) {                                                                         \
            for (k = 0; k < count; k++) {                                                   \
                match_flags f = flags_empty;                                                \
                scan_routine_##NAME((const mem64_t *) (memory + k), 8, NULL, uv, &f);       \
                flags[k] = f;                                                               \
                found += f != flags_empty;                                                  \
            }                                                                               \
            return found;                                                                   \
        }                                                                                   \
        for (k = 0; k < count; k++) {                                                       \
            value_t old_value;                                                              \
            match_flags f = flags_empty;                                                    \
            old_value.flags = old[k].match_info;                                            \
            if (old_value.flags != flags_empty) {                                           \
                for (size_t b = 0; b < sizeof(old_value.bytes); b++)                        \
                    old_value.bytes[b] = old[k + b].old_value;                              \
                scan_routine_##NAME((const mem64_t *) (memory + k),                         \
                                    number_memlength(old_value.flags), &old_value, uv, &f); \
            }                                                                               \
            flags[k] = f;                                                                   \
            found += f != flags_empty;                                                      \
        }                                                                                   \
        return found;                                                                       \
    }

/* clang-format does not understand these lists */
// clang-format off
#define NUMBER_ROUTINES(X, MATCH, USERVALUES) \
    X(INTEGER8_##MATCH, USERVALUES)           \
    X(INTEGER16_##MATCH, USERVALUES)          \
    X(INTEGER32_##MATCH, USERVALUES)          \
    X(INTEGER64_##MATCH, USERVALUES)          \
    X(FLOAT32_##MATCH, USERVALUES)            \
    X(FLOAT64_##MATCH, USERVALUES)            \
    X(ANYINTEGER_##MATCH, USERVALUES)         \
    X(ANYFLOAT_##MATCH, USERVALUES)           \
    X(ANYNUMBER_##MATCH, USERVALUES)

#define NUMBER_ROUTINES_REVENDIAN(X, MATCH, USERVALUES) \
    X(INTEGER16_##MATCH##_REVENDIAN, USERVALUES)        \
    X(INTEGER32_##MATCH##_REVENDIAN, USERVALUES)        \
    X(INTEGER64_##MATCH##_REVENDIAN, USERVALUES)        \
    X(FLOAT32_##MATCH##_REVENDIAN, USERVALUES)          \
    X(FLOAT64_##MATCH##_REVENDIAN, USERVALUES)          \
    X(ANYINTEGER_##MATCH##_REVENDIAN, USERVALUES)       \
    X(ANYFLOAT_##MATCH##_REVENDIAN, USERVALUES)         \
    X(ANYNUMBER_##MATCH##_REVENDIAN, USERVALUES)

/* every routine that has a block form */
#define BLOCK_ROUTINES(X)                           \
    NUMBER_ROUTINES(X, EQUALTO, 1)                  \
    NUMBER_ROUTINES_REVENDIAN(X, EQUALTO, 1)        \
    NUMBER_ROUTINES(X, NOTEQUALTO, 1)               \
    NUMBER_ROUTINES_REVENDIAN(X, NOTEQUALTO, 1)     \
    NUMBER_ROUTINES(X, GREATERTHAN, 1)              \
    NUMBER_ROUTINES_REVENDIAN(X, GREATERTHAN, 1)    \
    NUMBER_ROUTINES(X, LESSTHAN, 1)                 \
    NUMBER_ROUTINES_REVENDIAN(X, LESSTHAN, 1)       \
    NUMBER_ROUTINES(X, RANGE, 2)                    \
    NUMBER_ROUTINES_REVENDIAN(X, RANGE, 2)          \
    NUMBER_ROUTINES(X, INCREASED, 1)                \
    NUMBER_ROUTINES(X, DECREASED, 1)                \
    NUMBER_ROUTINES(X, INCREASEDBY, 1)              \
    NUMBER_ROUTINES(X, DECREASEDBY, 1)
// clang-format on

BLOCK_ROUTINES(DEFINE_BLOCK_ROUTINE)

#define CHOOSE_BLOCK_ROUTINE(NAME, USERVALUES) \
    if (routine == &scan_routine_##NAME)       \
        return &block_routine_##NAME;

block_routine_t get_block_routine(scan_routine_t routine)
{
    BLOCK_ROUTINES(CHOOSE_BLOCK_ROUTINE)
    return NULL;
}

/*----------------*/
/* Vector compare */
/*----------------*/

/* Each width is compared in lanes loaded at every byte offset it needs: one
 * load for 8-bit values, 2 for 16-bit (positions 2j and 2j + 1), 4 for 32-bit
 * and 8 for 64-bit, giving a bit mask of the 32 positions per flag, which is
 * then spread into the flags. Unsigned values compare as signed ones with the
 * sign bit flipped, and floats with the predicates that match C's operators:
 * ordered for ==, <, >, <=, >= (false with NaN), unordered for != (true). */
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
#    define HAVE_VECTOR_COMPARE 1
#    include <immintrin.h>
#    include <string.h>

/* bit j to bit 4j, and bit j to bit 8j */
static uint32_t spread4[256], spread8[16];

#    define VC_INLINE static inline __attribute__((always_inline, target("avx2")))

#    define DEFINE_VC_CMP(BITS)                                                                    \
        VC_INLINE __m256i vc_cmp##BITS(scan_match_type_t mt, __m256i v, __m256i lo, __m256i hi)    \
        {                                                                                          \
            __m256i ones = _mm256_set1_epi8(-1);                                                   \
            switch (mt) {                                                                          \
            case MATCHEQUALTO:                                                                     \
                return _mm256_cmpeq_epi##BITS(v, lo);                                              \
            case MATCHNOTEQUALTO:                                                                  \
                return _mm256_xor_si256(_mm256_cmpeq_epi##BITS(v, lo), ones);                      \
            case MATCHGREATERTHAN:                                                                 \
                return _mm256_cmpgt_epi##BITS(v, lo);                                              \
            case MATCHLESSTHAN:                                                                    \
                return _mm256_cmpgt_epi##BITS(lo, v);                                              \
            default: /* MATCHRANGE */                                                              \
                return _mm256_xor_si256(                                                           \
                    _mm256_or_si256(_mm256_cmpgt_epi##BITS(lo, v), _mm256_cmpgt_epi##BITS(v, hi)), \
                    ones);                                                                         \
            }                                                                                      \
        }

DEFINE_VC_CMP(8)
DEFINE_VC_CMP(16)
DEFINE_VC_CMP(32)
DEFINE_VC_CMP(64)

#    define DEFINE_VC_CMPF(SUFFIX, TYPE)                                              \
        VC_INLINE TYPE vc_cmp##SUFFIX(scan_match_type_t mt, TYPE v, TYPE lo, TYPE hi) \
        {                                                                             \
            switch (mt) {                                                             \
            case MATCHEQUALTO:                                                        \
                return _mm256_cmp_##SUFFIX(v, lo, _CMP_EQ_OQ);                        \
            case MATCHNOTEQUALTO:                                                     \
                return _mm256_cmp_##SUFFIX(v, lo, _CMP_NEQ_UQ);                       \
            case MATCHGREATERTHAN:                                                    \
                return _mm256_cmp_##SUFFIX(v, lo, _CMP_GT_OQ);                        \
            case MATCHLESSTHAN:                                                       \
                return _mm256_cmp_##SUFFIX(v, lo, _CMP_LT_OQ);                        \
            default: /* MATCHRANGE */                                                 \
                return _mm256_and_##SUFFIX(_mm256_cmp_##SUFFIX(v, lo, _CMP_GE_OQ),    \
                                           _mm256_cmp_##SUFFIX(v, hi, _CMP_LE_OQ));   \
            }                                                                         \
        }

DEFINE_VC_CMPF(ps, __m256)
DEFINE_VC_CMPF(pd, __m256d)

/* the position mask of 16-bit compares at offsets 0 (positions 2j) and 1 */
VC_INLINE uint32_t vc_mask16(__m256i even, __m256i odd)
{
    /* bytes of each 128-bit half: even lanes, then odd ones; put them in order */
    const __m256i order = _mm256_setr_epi8(0, 8, 1, 9, 2, 10, 3, 11, 4, 12, 5, 13, 6, 14, 7, 15,
                                           0, 8, 1, 9, 2, 10, 3, 11, 4, 12, 5, 13, 6, 14, 7, 15);
    return (uint32_t) _mm256_movemask_epi8(
        _mm256_shuffle_epi8(_mm256_packs_epi16(even, odd), order));
}

VC_INLINE size_t vc_kernel(const vector_compare_t *vc, const uint8_t *memory, size_t count,
                           match_flags *flags, scan_match_type_t mt)
{
    const match_flags want = vc->flags;
    const bool reverse = vc->reverse;
    const __m256i bias8 = _mm256_set1_epi8((char) 0x80);
    const __m256i bias16 = _mm256_set1_epi16((short) 0x8000);
    const __m256i bias32 = _mm256_set1_epi32((int) 0x80000000);
    const __m256i bias64 = _mm256_set1_epi64x((long long) 0x8000000000000000ULL);
    const __m256i swap16 = _mm256_setr_epi8(1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14,
                                            1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14);
    const __m256i swap32 = _mm256_setr_epi8(3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12,
                                            3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12);
    const __m256i swap64 = _mm256_setr_epi8(7, 6, 5, 4, 3, 2, 1, 0, 15, 14, 13, 12, 11, 10, 9, 8,
                                            7, 6, 5, 4, 3, 2, 1, 0, 15, 14, 13, 12, 11, 10, 9, 8);
    const __m256i lane_bits = _mm256_setr_epi16(1 << 0, 1 << 1, 1 << 2, 1 << 3, 1 << 4, 1 << 5,
                                                1 << 6, 1 << 7, 1 << 8, 1 << 9, 1 << 10, 1 << 11,
                                                1 << 12, 1 << 13, 1 << 14, (short) (1 << 15));
    const __m256i lo8u = _mm256_set1_epi8((char) vc->lo[0]), hi8u = _mm256_set1_epi8((char) vc->hi[0]);
    const __m256i lo8s = _mm256_set1_epi8((char) vc->lo[1]), hi8s = _mm256_set1_epi8((char) vc->hi[1]);
    const __m256i lo16u = _mm256_set1_epi16((short) vc->lo[2]), hi16u = _mm256_set1_epi16((short) vc->hi[2]);
    const __m256i lo16s = _mm256_set1_epi16((short) vc->lo[3]), hi16s = _mm256_set1_epi16((short) vc->hi[3]);
    const __m256i lo32u = _mm256_set1_epi32((int) vc->lo[4]), hi32u = _mm256_set1_epi32((int) vc->hi[4]);
    const __m256i lo32s = _mm256_set1_epi32((int) vc->lo[5]), hi32s = _mm256_set1_epi32((int) vc->hi[5]);
    const __m256i lo64u = _mm256_set1_epi64x((long long) vc->lo[6]);
    const __m256i hi64u = _mm256_set1_epi64x((long long) vc->hi[6]);
    const __m256i lo64s = _mm256_set1_epi64x((long long) vc->lo[7]);
    const __m256i hi64s = _mm256_set1_epi64x((long long) vc->hi[7]);
    const __m256 lof = _mm256_castsi256_ps(_mm256_set1_epi32((int) vc->lo[8]));
    const __m256 hif = _mm256_castsi256_ps(_mm256_set1_epi32((int) vc->hi[8]));
    const __m256d lod = _mm256_castsi256_pd(_mm256_set1_epi64x((long long) vc->lo[9]));
    const __m256d hid = _mm256_castsi256_pd(_mm256_set1_epi64x((long long) vc->hi[9]));
    size_t found = 0;

    for (size_t k = 0; k < count; k += VECTOR_POSITIONS) {
        const uint8_t *p = memory + k;
        uint32_t m[10] = {0};

        if (want & flags_i8b) {
            __m256i v = _mm256_loadu_si256((const __m256i *) p);
            if (want & flag_u8b)
                m[0] = (uint32_t) _mm256_movemask_epi8(
                    vc_cmp8(mt, _mm256_xor_si256(v, bias8), lo8u, hi8u));
            if (want & flag_s8b)
                m[1] = (uint32_t) _mm256_movemask_epi8(vc_cmp8(mt, v, lo8s, hi8s));
        }
        if (want & flags_i16b) {
            __m256i even = _mm256_loadu_si256((const __m256i *) p);
            __m256i odd = _mm256_loadu_si256((const __m256i *) (p + 1));
            if (reverse) {
                even = _mm256_shuffle_epi8(even, swap16);
                odd = _mm256_shuffle_epi8(odd, swap16);
            }
            if (want & flag_u16b)
                m[2] = vc_mask16(vc_cmp16(mt, _mm256_xor_si256(even, bias16), lo16u, hi16u),
                                 vc_cmp16(mt, _mm256_xor_si256(odd, bias16), lo16u, hi16u));
            if (want & flag_s16b)
                m[3] = vc_mask16(vc_cmp16(mt, even, lo16s, hi16s), vc_cmp16(mt, odd, lo16s, hi16s));
        }
        if (want & (flags_i32b | flag_f32b)) {
            for (unsigned o = 0; o < 4; o++) {
                __m256i v = _mm256_loadu_si256((const __m256i *) (p + o));
                if (reverse)
                    v = _mm256_shuffle_epi8(v, swap32);
                if (want & flag_u32b)
                    m[4] |= spread4[_mm256_movemask_ps(_mm256_castsi256_ps(
                                vc_cmp32(mt, _mm256_xor_si256(v, bias32), lo32u, hi32u)))]
                            << o;
                if (want & flag_s32b)
                    m[5] |= spread4[_mm256_movemask_ps(
                                _mm256_castsi256_ps(vc_cmp32(mt, v, lo32s, hi32s)))]
                            << o;
                if (want & flag_f32b)
                    m[8] |= spread4[_mm256_movemask_ps(
                                vc_cmpps(mt, _mm256_castsi256_ps(v), lof, hif))]
                            << o;
            }
        }
        if (want & (flags_i64b | flag_f64b)) {
            for (unsigned o = 0; o < 8; o++) {
                __m256i v = _mm256_loadu_si256((const __m256i *) (p + o));
                if (reverse)
                    v = _mm256_shuffle_epi8(v, swap64);
                if (want & flag_u64b)
                    m[6] |= spread8[_mm256_movemask_pd(_mm256_castsi256_pd(
                                vc_cmp64(mt, _mm256_xor_si256(v, bias64), lo64u, hi64u)))]
                            << o;
                if (want & flag_s64b)
                    m[7] |= spread8[_mm256_movemask_pd(
                                _mm256_castsi256_pd(vc_cmp64(mt, v, lo64s, hi64s)))]
                            << o;
                if (want & flag_f64b)
                    m[9] |= spread8[_mm256_movemask_pd(
                                vc_cmppd(mt, _mm256_castsi256_pd(v), lod, hid))]
                            << o;
            }
        }

        /* lane i of `first` is position i, of `second` position 16 + i */
        __m256i first = _mm256_setzero_si256(), second = _mm256_setzero_si256();
        uint32_t any = 0;
        for (unsigned b = 0; b < 10; b++) {
            if (!m[b])
                continue;
            __m256i flag = _mm256_set1_epi16((short) (1 << b));
            __m256i bits = _mm256_set1_epi16((short) (m[b] & 0xffff));
            first = _mm256_or_si256(first, _mm256_and_si256(_mm256_cmpeq_epi16(_mm256_and_si256(bits, lane_bits), lane_bits), flag));
            bits = _mm256_set1_epi16((short) (m[b] >> 16));
            second = _mm256_or_si256(second, _mm256_and_si256(_mm256_cmpeq_epi16(_mm256_and_si256(bits, lane_bits), lane_bits), flag));
            any |= m[b];
        }
        _mm256_storeu_si256((__m256i *) (flags + k), first);
        _mm256_storeu_si256((__m256i *) (flags + k + 16), second);
        found += (size_t) __builtin_popcount(any);
    }
    return found;
}

#    define DEFINE_VC_FOR(MT)                                                                \
        static size_t __attribute__((target("avx2")))                                        \
        vector_compare_##MT(const vector_compare_t *vc, const uint8_t *memory, size_t count, \
                            match_flags *flags)                                              \
        {                                                                                    \
            return vc_kernel(vc, memory, count, flags, MT);                                  \
        }

DEFINE_VC_FOR(MATCHEQUALTO)
DEFINE_VC_FOR(MATCHNOTEQUALTO)
DEFINE_VC_FOR(MATCHGREATERTHAN)
DEFINE_VC_FOR(MATCHLESSTHAN)
DEFINE_VC_FOR(MATCHRANGE)
#else
#    define HAVE_VECTOR_COMPARE 0
#endif

bool vector_compare_init(vector_compare_t *vc, scan_data_type_t dt, scan_match_type_t mt,
                         const uservalue_t *uval, bool reverse_endianness)
{
#if HAVE_VECTOR_COMPARE
    if (!uval || dt == BYTEARRAY || dt == STRING)
        return false;
    if (mt != MATCHEQUALTO && mt != MATCHNOTEQUALTO && mt != MATCHGREATERTHAN &&
        mt != MATCHLESSTHAN && mt != MATCHRANGE)
        return false;
    __builtin_cpu_init();
    if (!__builtin_cpu_supports("avx2"))
        return false;

    for (unsigned i = 0; i < 256; i++) {
        spread4[i] = 0;
        for (unsigned j = 0; j < 8; j++)
            spread4[i] |= (uint32_t) ((i >> j) & 1) << (4 * j);
    }
    for (unsigned i = 0; i < 16; i++) {
        spread8[i] = 0;
        for (unsigned j = 0; j < 4; j++)
            spread8[i] |= (uint32_t) ((i >> j) & 1) << (8 * j);
    }

    /* a range compares with both bounds, the rest with the one value, and
     * only on the flags the first bound has (the routines check only those) */
    const uservalue_t *lo = &uval[0], *hi = mt == MATCHRANGE ? &uval[1] : &uval[0];
    vc->flags = possible_flags_for_scan_data_type[dt] & uval[0].flags;
    vc->mt = mt;
    vc->reverse = reverse_endianness;
    vc->lo[0] = lo->uint8_value ^ 0x80u;
    vc->hi[0] = hi->uint8_value ^ 0x80u;
    vc->lo[1] = (uint8_t) lo->int8_value;
    vc->hi[1] = (uint8_t) hi->int8_value;
    vc->lo[2] = lo->uint16_value ^ 0x8000u;
    vc->hi[2] = hi->uint16_value ^ 0x8000u;
    vc->lo[3] = (uint16_t) lo->int16_value;
    vc->hi[3] = (uint16_t) hi->int16_value;
    vc->lo[4] = lo->uint32_value ^ 0x80000000u;
    vc->hi[4] = hi->uint32_value ^ 0x80000000u;
    vc->lo[5] = (uint32_t) lo->int32_value;
    vc->hi[5] = (uint32_t) hi->int32_value;
    vc->lo[6] = lo->uint64_value ^ 0x8000000000000000ULL;
    vc->hi[6] = hi->uint64_value ^ 0x8000000000000000ULL;
    vc->lo[7] = (uint64_t) lo->int64_value;
    vc->hi[7] = (uint64_t) hi->int64_value;
    uint32_t f32;
    memcpy(&f32, &lo->float32_value, sizeof(f32));
    vc->lo[8] = f32;
    memcpy(&f32, &hi->float32_value, sizeof(f32));
    vc->hi[8] = f32;
    memcpy(&vc->lo[9], &lo->float64_value, sizeof(vc->lo[9]));
    memcpy(&vc->hi[9], &hi->float64_value, sizeof(vc->hi[9]));
    return true;
#else
    (void) vc;
    (void) dt;
    (void) mt;
    (void) uval;
    (void) reverse_endianness;
    return false;
#endif
}

size_t vector_compare(const vector_compare_t *vc, const uint8_t *memory, size_t count,
                      match_flags *flags)
{
#if HAVE_VECTOR_COMPARE
    switch (vc->mt) {
    case MATCHEQUALTO:
        return vector_compare_MATCHEQUALTO(vc, memory, count, flags);
    case MATCHNOTEQUALTO:
        return vector_compare_MATCHNOTEQUALTO(vc, memory, count, flags);
    case MATCHGREATERTHAN:
        return vector_compare_MATCHGREATERTHAN(vc, memory, count, flags);
    case MATCHLESSTHAN:
        return vector_compare_MATCHLESSTHAN(vc, memory, count, flags);
    case MATCHRANGE:
        return vector_compare_MATCHRANGE(vc, memory, count, flags);
    default:
        break;
    }
#endif
    (void) vc;
    (void) memory;
    (void) count;
    (void) flags;
    assert(false);
    return 0;
}

/*-----------*/
/* Prefilter */
/*-----------*/

static bool is_common_byte(uint8_t b)
{
    return b == 0x00 || b == 0xff;
}

/* Add the window that best identifies `pattern` (where `fixed`, if given, says
 * which bytes must match): two adjacent fixed bytes, preferring ones that are
 * not 0x00 or 0xff, or else one fixed byte. Returns false if the window would
 * be too common to filter anything, or there is no room. */
static bool add_window(prefilter_t *pf, const uint8_t *pattern, const wildcard_t *fixed,
                       unsigned length)
{
    int best = -1, best_score = 3;
    bool pair = false;
    unsigned i;

    for (i = 0; i + 1 < length; i++) {
        if (fixed && (fixed[i] != FIXED || fixed[i + 1] != FIXED))
            continue;
        int score = is_common_byte(pattern[i]) + is_common_byte(pattern[i + 1]);
        if (score < best_score) {
            best = i;
            best_score = score;
            pair = true;
        }
    }
    if (best < 0) {
        for (i = 0; i < length; i++) {
            if (fixed && fixed[i] != FIXED)
                continue;
            int score = 2 * is_common_byte(pattern[i]);
            if (score < best_score) {
                best = i;
                best_score = score;
            }
        }
    }
    /* nothing fixed, or only bytes that are everywhere in memory */
    if (best < 0 || best_score == 2)
        return false;

    for (i = 0; i < pf->num_windows; i++) {
        if (pf->windows[i].offset == (unsigned) best && pf->windows[i].pair == pair &&
            pf->windows[i].first == pattern[best] &&
            (!pair || pf->windows[i].second == pattern[best + 1]))
            return true;
    }
    if (pf->num_windows == PREFILTER_MAX_WINDOWS)
        return false;
    pf->windows[pf->num_windows].offset = best;
    pf->windows[pf->num_windows].first = pattern[best];
    pf->windows[pf->num_windows].second = pair ? pattern[best + 1] : 0;
    pf->windows[pf->num_windows].pair = pair;
    pf->num_windows++;
    return true;
}

/* The bytes a number routine compares memory with: the routines swap the
 * memory bytes before comparing when reverse_endianness is set. */
static bool add_number_window(prefilter_t *pf, const void *value, unsigned width,
                              bool reverse_endianness)
{
    uint8_t pattern[8];
    unsigned i;

    for (i = 0; i < width; i++)
        pattern[i] = ((const uint8_t *) value)[reverse_endianness ? width - 1 - i : i];
    return add_window(pf, pattern, NULL, width);
}

bool prefilter_init(prefilter_t *pf, scan_data_type_t dt, scan_match_type_t mt,
                    const uservalue_t *uval, bool reverse_endianness)
{
    pf->num_windows = 0;
    if (mt != MATCHEQUALTO || uval == NULL)
        return false;

    if (dt == BYTEARRAY)
        return add_window(pf, uval->bytearray_value, uval->wildcard_value, uval->flags);
    if (dt == STRING)
        return add_window(pf, (const uint8_t *) uval->string_value, NULL, uval->flags);

    /* the flags the routine for `dt` looks at */
    match_flags flags = uval->flags & possible_flags_for_scan_data_type[dt];
    bool ok = true;

    if (flags & flag_u8b)
        ok = ok && add_number_window(pf, &uval->uint8_value, 1, false);
    if (flags & flag_s8b)
        ok = ok && add_number_window(pf, &uval->int8_value, 1, false);
    if (flags & flag_u16b)
        ok = ok && add_number_window(pf, &uval->uint16_value, 2, reverse_endianness);
    if (flags & flag_s16b)
        ok = ok && add_number_window(pf, &uval->int16_value, 2, reverse_endianness);
    if (flags & flag_u32b)
        ok = ok && add_number_window(pf, &uval->uint32_value, 4, reverse_endianness);
    if (flags & flag_s32b)
        ok = ok && add_number_window(pf, &uval->int32_value, 4, reverse_endianness);
    if (flags & flag_u64b)
        ok = ok && add_number_window(pf, &uval->uint64_value, 8, reverse_endianness);
    if (flags & flag_s64b)
        ok = ok && add_number_window(pf, &uval->int64_value, 8, reverse_endianness);
    /* float == float compares equal exactly when the bits do, except that
     * +0.0 == -0.0 and NaN never compares equal */
    if (flags & flag_f32b && uval->float32_value == uval->float32_value) {
        if (uval->float32_value == 0)
            ok = false; /* zero bytes are everywhere */
        else
            ok = ok && add_number_window(pf, &uval->float32_value, 4, reverse_endianness);
    }
    if (flags & flag_f64b && uval->float64_value == uval->float64_value) {
        if (uval->float64_value == 0)
            ok = false;
        else
            ok = ok && add_number_window(pf, &uval->float64_value, 8, reverse_endianness);
    }
    return ok && pf->num_windows > 0;
}

static inline bool is_candidate(const prefilter_t *pf, const uint8_t *buf, size_t pos, size_t avail)
{
    for (unsigned j = 0; j < pf->num_windows; j++) {
        size_t at = pos + pf->windows[j].offset;
        if (at + pf->windows[j].pair < avail && buf[at] == pf->windows[j].first &&
            (!pf->windows[j].pair || buf[at + 1] == pf->windows[j].second))
            return true;
    }
    return false;
}

/* 16 bytes at a time with GCC vector extensions: SSE2 on x86-64, NEON on ARM */
typedef uint8_t prefilter_vec_t __attribute__((vector_size(16)));
typedef uint64_t prefilter_vec64_t __attribute__((vector_size(16)));

static inline prefilter_vec_t load_vec(const uint8_t *p)
{
    prefilter_vec_t v;
    memcpy(&v, p, sizeof(v));
    return v;
}

size_t prefilter_next(const prefilter_t *pf, const uint8_t *buf, size_t pos, size_t end,
                      size_t avail)
{
    /* the furthest a block of 16 positions reads */
    size_t reach = 16;
    for (unsigned j = 0; j < pf->num_windows; j++)
        if (pf->windows[j].offset + pf->windows[j].pair + 16 > reach)
            reach = pf->windows[j].offset + pf->windows[j].pair + 16;

    for (; pos < end && pos + reach <= avail; pos += 16) {
        prefilter_vec_t any = {0};
        for (unsigned j = 0; j < pf->num_windows; j++) {
            const uint8_t *p = buf + pos + pf->windows[j].offset;
            prefilter_vec_t first = (prefilter_vec_t){0} + pf->windows[j].first;
            prefilter_vec_t hit = (prefilter_vec_t) (load_vec(p) == first);
            if (pf->windows[j].pair) {
                prefilter_vec_t second = (prefilter_vec_t){0} + pf->windows[j].second;
                hit &= (prefilter_vec_t) (load_vec(p + 1) == second);
            }
            any |= hit;
        }
        prefilter_vec64_t any64 = (prefilter_vec64_t) any;
        if (any64[0] | any64[1]) {
            for (size_t k = pos; k < pos + 16 && k < end; k++)
                if (is_candidate(pf, buf, k, avail))
                    return k;
        }
    }
    for (; pos < end; pos++)
        if (is_candidate(pf, buf, pos, avail))
            return pos;
    return end;
}
