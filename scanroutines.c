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
    uint length = user_value->flags;
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
    uint length = user_value->flags;
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
