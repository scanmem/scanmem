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

#include "config.h"

#include <assert.h>

#include "scanroutines.h"
#include "value.h"
#include "scanmem.h"


/* for convenience */
#define SCAN_ROUTINE_ARGUMENTS (const value_t *new_value, const value_t *old_value, const uservalue_t *user_value, match_flags *saveflags, const void *address)
int (*sm_scan_routine) SCAN_ROUTINE_ARGUMENTS;

#define VALUE_COMP(a,b,field,op)    (((a)->flags.field && (b)->flags.field) && (get_##field(a) op get_##field(b)))
#define SET_FLAG(f, field)          ((f)->field = 1)

/********************/
/* Integer specific */
/********************/

/* for MATCHANY */
#define DEFINE_INTEGER_MATCHANY_ROUTINE(DATATYPENAME, DATAWIDTH) \
    extern inline int scan_routine_##DATATYPENAME##_ANY SCAN_ROUTINE_ARGUMENTS \
    { \
        int ret = 0; \
        if ((new_value)->flags.s##DATAWIDTH##b) { ret = (DATAWIDTH)/8; SET_FLAG(saveflags, s##DATAWIDTH##b); } \
        if ((new_value)->flags.u##DATAWIDTH##b) { ret = (DATAWIDTH)/8; SET_FLAG(saveflags, u##DATAWIDTH##b); } \
        return ret; \
    }

DEFINE_INTEGER_MATCHANY_ROUTINE(INTEGER8, 8)
DEFINE_INTEGER_MATCHANY_ROUTINE(INTEGER16, 16)
DEFINE_INTEGER_MATCHANY_ROUTINE(INTEGER32, 32)
DEFINE_INTEGER_MATCHANY_ROUTINE(INTEGER64, 64)


#define DEFINE_INTEGER_ROUTINE(DATATYPENAME, DATAWIDTH, MATCHTYPENAME, MATCHTYPE, VALUE_TO_COMPARE_WITH) \
    extern inline int scan_routine_##DATATYPENAME##_##MATCHTYPENAME SCAN_ROUTINE_ARGUMENTS \
    { \
        assert(VALUE_TO_COMPARE_WITH); \
        int ret = 0; \
        if(VALUE_COMP(new_value, VALUE_TO_COMPARE_WITH, s##DATAWIDTH##b, MATCHTYPE)) { \
            SET_FLAG(saveflags, s##DATAWIDTH##b); \
            ret = (DATAWIDTH)/8; \
        } \
        if(VALUE_COMP(new_value, VALUE_TO_COMPARE_WITH, u##DATAWIDTH##b, MATCHTYPE)) { \
            SET_FLAG(saveflags, u##DATAWIDTH##b); \
            ret = (DATAWIDTH)/8; \
        } \
        return ret; \
    }

#define DEFINE_INTEGER_ROUTINE_FOR_ALL_INTEGER_TYPE(MATCHTYPENAME, MATCHTYPE, VALUE_TO_COMPARE_WITH) \
    DEFINE_INTEGER_ROUTINE(INTEGER8, 8, MATCHTYPENAME, MATCHTYPE, VALUE_TO_COMPARE_WITH) \
    DEFINE_INTEGER_ROUTINE(INTEGER16, 16, MATCHTYPENAME, MATCHTYPE, VALUE_TO_COMPARE_WITH) \
    DEFINE_INTEGER_ROUTINE(INTEGER32, 32, MATCHTYPENAME, MATCHTYPE, VALUE_TO_COMPARE_WITH) \
    DEFINE_INTEGER_ROUTINE(INTEGER64, 64, MATCHTYPENAME, MATCHTYPE, VALUE_TO_COMPARE_WITH) 

DEFINE_INTEGER_ROUTINE_FOR_ALL_INTEGER_TYPE(EQUALTO, ==, user_value)
DEFINE_INTEGER_ROUTINE_FOR_ALL_INTEGER_TYPE(NOTEQUALTO, !=, user_value)
DEFINE_INTEGER_ROUTINE_FOR_ALL_INTEGER_TYPE(GREATERTHAN, >, user_value)
DEFINE_INTEGER_ROUTINE_FOR_ALL_INTEGER_TYPE(LESSTHAN, <, user_value)
DEFINE_INTEGER_ROUTINE_FOR_ALL_INTEGER_TYPE(NOTCHANGED, ==, old_value)
DEFINE_INTEGER_ROUTINE_FOR_ALL_INTEGER_TYPE(CHANGED, !=, old_value)
DEFINE_INTEGER_ROUTINE_FOR_ALL_INTEGER_TYPE(INCREASED, >, old_value)
DEFINE_INTEGER_ROUTINE_FOR_ALL_INTEGER_TYPE(DECREASED, <, old_value)

/******************/
/* Float specific */
/******************/

/* for MATCHANY */
#define DEFINE_FLOAT_MATCHANY_ROUTINE(DATATYPENAME, DATAWIDTH) \
    extern inline int scan_routine_##DATATYPENAME##_ANY SCAN_ROUTINE_ARGUMENTS \
    { \
        int ret = 0; \
        if ((new_value)->flags.f##DATAWIDTH##b) { ret = (DATAWIDTH)/8; SET_FLAG(saveflags, f##DATAWIDTH##b); } \
        return ret; \
    }

DEFINE_FLOAT_MATCHANY_ROUTINE(FLOAT32, 32)
DEFINE_FLOAT_MATCHANY_ROUTINE(FLOAT64, 64)

#define DEFINE_FLOAT_ROUTINE(DATATYPENAME, DATAWIDTH, MATCHTYPENAME, MATCHTYPE, VALUE_TO_COMPARE_WITH) \
    extern inline int scan_routine_##DATATYPENAME##_##MATCHTYPENAME SCAN_ROUTINE_ARGUMENTS \
    { \
        assert(VALUE_TO_COMPARE_WITH); \
        int ret = 0; \
        if(VALUE_COMP(new_value, VALUE_TO_COMPARE_WITH, f##DATAWIDTH##b, MATCHTYPE)) { \
            SET_FLAG(saveflags, f##DATAWIDTH##b); \
            ret = (DATAWIDTH)/8; \
        } \
        return ret; \
    } \

#define DEFINE_FLOAT_ROUTINE_FOR_ALL_FLOAT_TYPE(MATCHTYPENAME, MATCHTYPE, VALUE_TO_COMPARE_WITH) \
    DEFINE_FLOAT_ROUTINE(FLOAT32, 32, MATCHTYPENAME, MATCHTYPE, VALUE_TO_COMPARE_WITH) \
    DEFINE_FLOAT_ROUTINE(FLOAT64, 64, MATCHTYPENAME, MATCHTYPE, VALUE_TO_COMPARE_WITH) 

DEFINE_FLOAT_ROUTINE_FOR_ALL_FLOAT_TYPE(EQUALTO, ==, user_value)
DEFINE_FLOAT_ROUTINE_FOR_ALL_FLOAT_TYPE(NOTEQUALTO, !=, user_value)
DEFINE_FLOAT_ROUTINE_FOR_ALL_FLOAT_TYPE(NOTCHANGED, ==, old_value)         /* this is bad */
DEFINE_FLOAT_ROUTINE_FOR_ALL_FLOAT_TYPE(CHANGED, !=, old_value)      /* this is bad, but better than above */
DEFINE_FLOAT_ROUTINE_FOR_ALL_FLOAT_TYPE(INCREASED, >, old_value)
DEFINE_FLOAT_ROUTINE_FOR_ALL_FLOAT_TYPE(DECREASED, <, old_value)
DEFINE_FLOAT_ROUTINE_FOR_ALL_FLOAT_TYPE(GREATERTHAN, >, user_value)
DEFINE_FLOAT_ROUTINE_FOR_ALL_FLOAT_TYPE(LESSTHAN, <, user_value)

/********************/
/* Special routines */
/********************/

/*---------------------------------*/
/* for INCREASEDBY and DECREASEDBY */
/*---------------------------------*/
#define DEFINE_INTEGER_INCREASEDBY_DECREASEDBY_ROUTINE(DATATYPENAME, DATAWIDTH) \
    extern inline int scan_routine_##DATATYPENAME##_INCREASEDBY SCAN_ROUTINE_ARGUMENTS \
    { \
        int ret = 0; \
        if ((new_value->flags.s##DATAWIDTH##b) && (old_value->flags.s##DATAWIDTH##b) && (user_value->flags.s##DATAWIDTH##b) \
                && (get_s##DATAWIDTH##b(new_value) == get_s##DATAWIDTH##b(old_value) + get_s##DATAWIDTH##b(user_value))) \
            { ret = (DATAWIDTH)/8; SET_FLAG(saveflags, s##DATAWIDTH##b); } \
        if ((new_value->flags.u##DATAWIDTH##b) && (old_value->flags.u##DATAWIDTH##b) && (user_value->flags.u##DATAWIDTH##b) \
                && (get_u##DATAWIDTH##b(new_value) == get_u##DATAWIDTH##b(old_value) + get_u##DATAWIDTH##b(user_value))) \
            { ret = (DATAWIDTH)/8; SET_FLAG(saveflags, u##DATAWIDTH##b); } \
        return ret; \
    } \
    extern inline int scan_routine_##DATATYPENAME##_DECREASEDBY SCAN_ROUTINE_ARGUMENTS \
    { \
        int ret = 0; \
        if ((new_value->flags.s##DATAWIDTH##b) && (old_value->flags.s##DATAWIDTH##b) && (user_value->flags.s##DATAWIDTH##b) \
                && (get_s##DATAWIDTH##b(new_value) == get_s##DATAWIDTH##b(old_value) - get_s##DATAWIDTH##b(user_value))) \
            { ret = (DATAWIDTH)/8; SET_FLAG(saveflags, s##DATAWIDTH##b); } \
        if ((new_value->flags.u##DATAWIDTH##b) && (old_value->flags.u##DATAWIDTH##b) && (user_value->flags.u##DATAWIDTH##b) \
                && (get_u##DATAWIDTH##b(new_value) == get_u##DATAWIDTH##b(old_value) - get_u##DATAWIDTH##b(user_value))) \
            { ret = (DATAWIDTH)/8; SET_FLAG(saveflags, u##DATAWIDTH##b); } \
        return ret; \
    } 

DEFINE_INTEGER_INCREASEDBY_DECREASEDBY_ROUTINE(INTEGER8, 8)
DEFINE_INTEGER_INCREASEDBY_DECREASEDBY_ROUTINE(INTEGER16, 16)
DEFINE_INTEGER_INCREASEDBY_DECREASEDBY_ROUTINE(INTEGER32, 32)
DEFINE_INTEGER_INCREASEDBY_DECREASEDBY_ROUTINE(INTEGER64, 64)

#define DEFINE_FLOAT_INCREASEDBY_DECREASEDBY_ROUTINE(DATATYPENAME, DATAWIDTH) \
    extern inline int scan_routine_##DATATYPENAME##_INCREASEDBY SCAN_ROUTINE_ARGUMENTS \
    { \
        int ret = 0; \
        if ((new_value->flags.f##DATAWIDTH##b) && (old_value->flags.f##DATAWIDTH##b) && (user_value->flags.f##DATAWIDTH##b) \
                && (get_f##DATAWIDTH##b(new_value) == get_f##DATAWIDTH##b(old_value) + get_f##DATAWIDTH##b(user_value))) \
            { ret = (DATAWIDTH)/8; SET_FLAG(saveflags, f##DATAWIDTH##b); } \
        return ret; \
    } \
    extern inline int scan_routine_##DATATYPENAME##_DECREASEDBY SCAN_ROUTINE_ARGUMENTS \
    { \
        int ret = 0; \
        if ((new_value->flags.f##DATAWIDTH##b) && (old_value->flags.f##DATAWIDTH##b) && (user_value->flags.f##DATAWIDTH##b) \
                && (get_f##DATAWIDTH##b(new_value) == get_f##DATAWIDTH##b(old_value) - get_f##DATAWIDTH##b(user_value))) \
            { ret = (DATAWIDTH)/8; SET_FLAG(saveflags, f##DATAWIDTH##b); } \
        return ret; \
    } 

DEFINE_FLOAT_INCREASEDBY_DECREASEDBY_ROUTINE(FLOAT32, 32)
DEFINE_FLOAT_INCREASEDBY_DECREASEDBY_ROUTINE(FLOAT64, 64)

/*-----------*/
/* for RANGE */
/*-----------*/

#define DEFINE_INTEGER_RANGE_ROUTINE(DATATYPENAME, DATAWIDTH) \
    extern inline int scan_routine_##DATATYPENAME##_RANGE SCAN_ROUTINE_ARGUMENTS \
    { \
        int ret = 0; \
        if ((new_value->flags.s##DATAWIDTH##b) \
                && (user_value[0].flags.s##DATAWIDTH##b) && (user_value[1].flags.s##DATAWIDTH##b) \
                && (get_s##DATAWIDTH##b(new_value) >= get_s##DATAWIDTH##b(&user_value[0])) \
                && (get_s##DATAWIDTH##b(new_value) <= get_s##DATAWIDTH##b(&user_value[1]))) \
            { ret = (DATAWIDTH)/8; SET_FLAG(saveflags, s##DATAWIDTH##b); } \
        if ((new_value->flags.u##DATAWIDTH##b) \
                && (user_value[0].flags.u##DATAWIDTH##b) && (user_value[1].flags.u##DATAWIDTH##b) \
                && (get_u##DATAWIDTH##b(new_value) >= get_u##DATAWIDTH##b(&user_value[0])) \
                && (get_u##DATAWIDTH##b(new_value) <= get_u##DATAWIDTH##b(&user_value[1]))) \
            { ret = (DATAWIDTH)/8; SET_FLAG(saveflags, u##DATAWIDTH##b); } \
        return ret; \
    }

DEFINE_INTEGER_RANGE_ROUTINE(INTEGER8, 8)
DEFINE_INTEGER_RANGE_ROUTINE(INTEGER16, 16)
DEFINE_INTEGER_RANGE_ROUTINE(INTEGER32, 32)
DEFINE_INTEGER_RANGE_ROUTINE(INTEGER64, 64)

#define DEFINE_FLOAT_RANGE_ROUTINE(DATATYPENAME, DATAWIDTH) \
    extern inline int scan_routine_##DATATYPENAME##_RANGE SCAN_ROUTINE_ARGUMENTS \
    { \
        int ret = 0; \
        if ((new_value->flags.f##DATAWIDTH##b) \
                && (user_value[0].flags.f##DATAWIDTH##b) && (user_value[1].flags.f##DATAWIDTH##b) \
                && (get_f##DATAWIDTH##b(new_value) >= get_f##DATAWIDTH##b(&user_value[0])) \
                && (get_f##DATAWIDTH##b(new_value) <= get_f##DATAWIDTH##b(&user_value[1]))) \
            { ret = (DATAWIDTH)/8; SET_FLAG(saveflags, f##DATAWIDTH##b); } \
        return ret; \
    }

DEFINE_FLOAT_RANGE_ROUTINE(FLOAT32, 32)
DEFINE_FLOAT_RANGE_ROUTINE(FLOAT64, 64)

/*-------------------------*/
/* Any-xxx types specifiec */
/*-------------------------*/
/* this is for anynumber, anyinteger, anyfloat */
#define DEFINE_ANYTYPE_ROUTINE(MATCHTYPENAME) \
    extern inline int scan_routine_ANYINTEGER_##MATCHTYPENAME SCAN_ROUTINE_ARGUMENTS \
    { \
        int ret = 0, tmp_ret;\
        if ((tmp_ret = scan_routine_INTEGER8_##MATCHTYPENAME (new_value, old_value, user_value, saveflags, address)) > ret) { ret = tmp_ret; } \
        if ((tmp_ret = scan_routine_INTEGER16_##MATCHTYPENAME (new_value, old_value, user_value, saveflags, address)) > ret) { ret = tmp_ret; } \
        if ((tmp_ret = scan_routine_INTEGER32_##MATCHTYPENAME (new_value, old_value, user_value, saveflags, address)) > ret) { ret = tmp_ret; } \
        if ((tmp_ret = scan_routine_INTEGER64_##MATCHTYPENAME (new_value, old_value, user_value, saveflags, address)) > ret) { ret = tmp_ret; } \
        return ret; \
    } \
    extern inline int scan_routine_ANYFLOAT_##MATCHTYPENAME SCAN_ROUTINE_ARGUMENTS \
    { \
        int ret = 0, tmp_ret; \
        if ((tmp_ret = scan_routine_FLOAT32_##MATCHTYPENAME (new_value, old_value, user_value, saveflags, address)) > ret) { ret = tmp_ret; } \
        if ((tmp_ret = scan_routine_FLOAT64_##MATCHTYPENAME (new_value, old_value, user_value, saveflags, address)) > ret) { ret = tmp_ret; } \
        return ret; \
    } \
    extern inline int scan_routine_ANYNUMBER_##MATCHTYPENAME SCAN_ROUTINE_ARGUMENTS \
    { \
        int ret1 = scan_routine_ANYINTEGER_##MATCHTYPENAME (new_value, old_value, user_value, saveflags, address); \
        int ret2 = scan_routine_ANYFLOAT_##MATCHTYPENAME (new_value, old_value, user_value, saveflags, address); \
        return (ret1 > ret2 ? ret1 : ret2); \
    } \

DEFINE_ANYTYPE_ROUTINE(ANY)
DEFINE_ANYTYPE_ROUTINE(EQUALTO)
DEFINE_ANYTYPE_ROUTINE(NOTEQUALTO)
DEFINE_ANYTYPE_ROUTINE(CHANGED)
DEFINE_ANYTYPE_ROUTINE(NOTCHANGED)
DEFINE_ANYTYPE_ROUTINE(INCREASED)
DEFINE_ANYTYPE_ROUTINE(DECREASED)
DEFINE_ANYTYPE_ROUTINE(GREATERTHAN)
DEFINE_ANYTYPE_ROUTINE(LESSTHAN)
DEFINE_ANYTYPE_ROUTINE(INCREASEDBY)
DEFINE_ANYTYPE_ROUTINE(DECREASEDBY)
DEFINE_ANYTYPE_ROUTINE(RANGE)

/*----------------------------------------*/
/* for generic VLT (Variable Length Type) */
/*----------------------------------------*/
extern inline int scan_routine_VLT_ANY SCAN_ROUTINE_ARGUMENTS
{
   return saveflags->length = ((old_value)->flags.length);
}

/*---------------*/
/* for BYTEARRAY */
/*---------------*/

/* Used only for length>8 */
extern inline int scan_routine_BYTEARRAY_EQUALTO SCAN_ROUTINE_ARGUMENTS
{
    const uint8_t *bytes_array = user_value->bytearray_value;
    const wildcard_t *wildcards_array = user_value->wildcard_value;
    if(!new_value->flags.length ||
       *((int64_t*)bytes_array) != (new_value->int64_value & *((int64_t*)wildcards_array)))
    {
        /* not matched */
        return 0;
    }

    uint length = user_value->flags.length;
    unsigned int i, j;
    value_t val_buf;
    for(i = sizeof(int64_t); i + sizeof(int64_t) <= length; i += sizeof(int64_t))
    {
        /* read next block */
        if (!sm_peekdata(sm_globals.target, address+i, &val_buf))
        {
            /* cannot read */
            return 0;
        }

        if (*((int64_t*)(bytes_array+i)) != (val_buf.int64_value & *((int64_t*)(wildcards_array+i))))
        {
            /* not matched */
            return 0;
        }

    }

    /* match bytes left */
    if (i < length)
    {
        /* read next block */
        if (!sm_peekdata(sm_globals.target, address + i, &val_buf))
        {
            /* cannot read */
            return 0;
        }
        for(j = 0; j < length - i; ++j)
        {
            if ((bytes_array+i)[j] != (val_buf.bytes[j] & (wildcards_array+i)[j]))
            {
                /* not matched */
                return 0;
            }
        }
    }

    /* matched */
    saveflags->length = length;

    return length;
}

/* optimized routines for small lengths
   careful: WIDTH = 8*LENGTH */

#define DEFINE_BYTEARRAY_POW2_EQUALTO_ROUTINE(WIDTH) \
    extern inline int scan_routine_BYTEARRAY##WIDTH##_EQUALTO SCAN_ROUTINE_ARGUMENTS \
    { \
        if (new_value->flags.length && \
            ((get_u##WIDTH##b(new_value) & *(uint##WIDTH##_t*)user_value->wildcard_value) \
              == *(uint##WIDTH##_t*)(user_value->bytearray_value))) \
        { \
            /* matched */ \
            saveflags->length = (WIDTH)/8; \
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
    extern inline int scan_routine_BYTEARRAY##WIDTH##_EQUALTO SCAN_ROUTINE_ARGUMENTS \
    { \
        if (!new_value->flags.length) \
        { \
            /* new_value is not actually a valid bytearray */ \
            return 0; \
        } \
        const uint8_t *bytes_array = user_value->bytearray_value; \
        const wildcard_t *wildcards_array = user_value->wildcard_value; \
        int i; \
        for(i = 0; i < (WIDTH)/8; ++i) \
        { \
            if(bytes_array[i] != (new_value->bytes[i] & wildcards_array[i])) \
            { \
                /* not matched */ \
                return 0; \
            } \
        } \
        /* matched */ \
        saveflags->length = (WIDTH)/8; \
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
extern inline int scan_routine_STRING_EQUALTO SCAN_ROUTINE_ARGUMENTS
{
    const char *scan_string = user_value->string_value;
    /* this testing order is faster than the more logical reverse */
    if(new_value->int64_value != *((int64_t*)scan_string) ||
       !new_value->flags.length)
    {
        /* not matched */
        return 0;
    }

    uint length = user_value->flags.length;
    unsigned int i, j;
    value_t val_buf;
    for(i = sizeof(int64_t); i + sizeof(int64_t) <= length; i += sizeof(int64_t))
    {
        /* read next block */
        if (!sm_peekdata(sm_globals.target, address + i, &val_buf))
        {
            /* cannot read */
            return 0;
        }

        if(val_buf.int64_value != *((int64_t*)(scan_string+i)))
        {
            /* not matched */
            return 0;
        }
    }

    /* match bytes left */
    if (i < length)
    {
        /* read next block */
        if (!sm_peekdata(sm_globals.target, address + i, &val_buf))
        {
            /* cannot read */
            return 0;
        }
        for(j = 0; j < length - i; ++j)
        {
            if(val_buf.chars[j] != (scan_string+i)[j])
            {
                /* not matched */
                return 0;
            }
        }
    }
    
    /* matched */
    saveflags->length = length;

    return length;
}

/* optimized routines for small strings
   careful: WIDTH = 8*LENGTH */

#define DEFINE_STRING_POW2_EQUALTO_ROUTINE(WIDTH) \
    extern inline int scan_routine_STRING##WIDTH##_EQUALTO SCAN_ROUTINE_ARGUMENTS \
    { \
        if (new_value->flags.length && \
            (get_u##WIDTH##b(new_value) == *(uint##WIDTH##_t*)(user_value->string_value))) \
        { \
            /* matched */ \
            saveflags->length = (WIDTH)/8; \
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
    extern inline int scan_routine_STRING##WIDTH##_EQUALTO SCAN_ROUTINE_ARGUMENTS \
    { \
        if (!new_value->flags.length) \
        { \
            /* new_value is not actually a valid string */ \
            return 0; \
        } \
        const char *scan_string = user_value->string_value; \
        int i; \
        for(i = 0; i < (WIDTH)/8; ++i) \
        { \
            if(new_value->chars[i] != scan_string[i]) \
            { \
                /* not matched */ \
                return 0; \
            } \
        } \
        /* matched */ \
        saveflags->length = (WIDTH)/8; \
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

#define CHOOSE_ROUTINE_FOR_ALL_NUMBER_TYPES(SCANMATCHTYPE, ROUTINEMATCHTYPENAME) \
    CHOOSE_ROUTINE(INTEGER8, INTEGER8, SCANMATCHTYPE, ROUTINEMATCHTYPENAME) \
    CHOOSE_ROUTINE(INTEGER16, INTEGER16, SCANMATCHTYPE, ROUTINEMATCHTYPENAME) \
    CHOOSE_ROUTINE(INTEGER32, INTEGER32, SCANMATCHTYPE, ROUTINEMATCHTYPENAME) \
    CHOOSE_ROUTINE(INTEGER64, INTEGER64, SCANMATCHTYPE, ROUTINEMATCHTYPENAME) \
    CHOOSE_ROUTINE(FLOAT32, FLOAT32, SCANMATCHTYPE, ROUTINEMATCHTYPENAME) \
    CHOOSE_ROUTINE(FLOAT64, FLOAT64, SCANMATCHTYPE, ROUTINEMATCHTYPENAME) \
    CHOOSE_ROUTINE(ANYINTEGER, ANYINTEGER, SCANMATCHTYPE, ROUTINEMATCHTYPENAME) \
    CHOOSE_ROUTINE(ANYFLOAT, ANYFLOAT, SCANMATCHTYPE, ROUTINEMATCHTYPENAME) \
    CHOOSE_ROUTINE(ANYNUMBER, ANYNUMBER, SCANMATCHTYPE, ROUTINEMATCHTYPENAME) \


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


scan_routine_t sm_get_scanroutine(scan_data_type_t dt, scan_match_type_t mt, const match_flags* uflags)
{
    CHOOSE_ROUTINE_FOR_ALL_NUMBER_TYPES(MATCHANY, ANY)
    CHOOSE_ROUTINE_FOR_ALL_NUMBER_TYPES(MATCHEQUALTO, EQUALTO)
    CHOOSE_ROUTINE_FOR_ALL_NUMBER_TYPES(MATCHNOTEQUALTO, NOTEQUALTO)
    CHOOSE_ROUTINE_FOR_ALL_NUMBER_TYPES(MATCHCHANGED, CHANGED)
    CHOOSE_ROUTINE_FOR_ALL_NUMBER_TYPES(MATCHNOTCHANGED, NOTCHANGED)
    CHOOSE_ROUTINE_FOR_ALL_NUMBER_TYPES(MATCHINCREASED, INCREASED)
    CHOOSE_ROUTINE_FOR_ALL_NUMBER_TYPES(MATCHDECREASED, DECREASED)
    CHOOSE_ROUTINE_FOR_ALL_NUMBER_TYPES(MATCHINCREASEDBY, INCREASEDBY)
    CHOOSE_ROUTINE_FOR_ALL_NUMBER_TYPES(MATCHDECREASEDBY, DECREASEDBY)
    CHOOSE_ROUTINE_FOR_ALL_NUMBER_TYPES(MATCHGREATERTHAN, GREATERTHAN)
    CHOOSE_ROUTINE_FOR_ALL_NUMBER_TYPES(MATCHLESSTHAN, LESSTHAN)
    CHOOSE_ROUTINE_FOR_ALL_NUMBER_TYPES(MATCHRANGE, RANGE)

    if (uflags) {
        CHOOSE_ROUTINE_VLT(BYTEARRAY, BYTEARRAY, MATCHEQUALTO, EQUALTO, uflags->length*8)
    }

    if (uflags) {
        CHOOSE_ROUTINE_VLT(STRING, STRING, MATCHEQUALTO, EQUALTO, uflags->length*8)
    }

    return NULL;
}


bool sm_choose_scanroutine(scan_data_type_t dt, scan_match_type_t mt, const uservalue_t* uval)
{
    const match_flags *uflags = uval ? &(uval->flags) : NULL;
    sm_scan_routine = sm_get_scanroutine(dt, mt, uflags);
    return (sm_scan_routine != NULL);
}
