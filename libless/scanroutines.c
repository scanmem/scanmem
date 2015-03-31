/*
    scanroutines.c  Routines of scanning for different data types
    Copyright (C) 2009,2010 WANG Lu  <coolwanglu(a)gmail.com>

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

#include <assert.h>

#include "scanroutines.h"
#include "value.h"
#include "scanmem.h"


/* for convenience */
#define SCAN_ROUTINE_ARGUMENTS (const value_t *new_value, const value_t *old_value, const uservalue_t *user_value, match_flags *saveflags, void *address) 
int (*g_scan_routine) SCAN_ROUTINE_ARGUMENTS;

#define VALUE_COMP(a,b,field,op)    (((a)->flags.field && (b)->flags.field) && (get_##field(a) op get_##field(b)))
#define VALUE_COPY(a,b,field)       ((set_##field(a, get_##field(b))), ((a)->flags.field = 1))
#define SET_FLAG(f, field)          ((f)->field = 1)

/********************/
/* Integer specific */
/********************/

/* for MATCHANY */
#define DEFINE_INTEGER_MATCHANY_ROUTINE(DATATYPENAME, DATAWIDTH) \
    int scan_routine_##DATATYPENAME##_ANY SCAN_ROUTINE_ARGUMENTS \
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
    int scan_routine_##DATATYPENAME##_##MATCHTYPENAME SCAN_ROUTINE_ARGUMENTS \
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
    int scan_routine_##DATATYPENAME##_ANY SCAN_ROUTINE_ARGUMENTS \
    { \
        int ret = 0; \
        if ((new_value)->flags.f##DATAWIDTH##b) { ret = (DATAWIDTH)/8; SET_FLAG(saveflags, f##DATAWIDTH##b); } \
        return ret; \
    }

DEFINE_FLOAT_MATCHANY_ROUTINE(FLOAT32, 32)
DEFINE_FLOAT_MATCHANY_ROUTINE(FLOAT64, 64)

#define DEFINE_FLOAT_ROUTINE(DATATYPENAME, DATAWIDTH, MATCHTYPENAME, MATCHTYPE, VALUE_TO_COMPARE_WITH) \
    int scan_routine_##DATATYPENAME##_##MATCHTYPENAME SCAN_ROUTINE_ARGUMENTS \
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

DEFINE_FLOAT_ROUTINE_FOR_ALL_FLOAT_TYPE(NOTEQUALTO, !=, user_value)   /* TODO: this should be consistent with !EQUALTO */
DEFINE_FLOAT_ROUTINE_FOR_ALL_FLOAT_TYPE(NOTCHANGED, ==, old_value)         /* this is bad */
DEFINE_FLOAT_ROUTINE_FOR_ALL_FLOAT_TYPE(CHANGED, !=, old_value)      /* this is bad, but better than above */
DEFINE_FLOAT_ROUTINE_FOR_ALL_FLOAT_TYPE(INCREASED, >, old_value)
DEFINE_FLOAT_ROUTINE_FOR_ALL_FLOAT_TYPE(DECREASED, <, old_value)
DEFINE_FLOAT_ROUTINE_FOR_ALL_FLOAT_TYPE(GREATERTHAN, >, user_value)
DEFINE_FLOAT_ROUTINE_FOR_ALL_FLOAT_TYPE(LESSTHAN, <, user_value)

/********************/
/* Special routines */
/********************/

/*-----------------------------------*/
/* special EQUALTO for float numbers */
/*-----------------------------------*/
/* currently we round both of them to integers and compare them */
/* TODO: let user specify a float number */
#define DEFINE_FLOAT_EQUALTO_ROUTINE(FLOATTYPENAME, WIDTH) \
    int scan_routine_##FLOATTYPENAME##_EQUALTO SCAN_ROUTINE_ARGUMENTS \
    { \
        int ret = 0; \
        if((new_value->flags.f##WIDTH##b) && (user_value->flags.f##WIDTH##b) && ((int##WIDTH##_t)get_f##WIDTH##b(new_value) == (int##WIDTH##_t)get_f##WIDTH##b(user_value))) \
        { \
            SET_FLAG(saveflags, f##WIDTH##b); \
            ret = (WIDTH)/8; \
        } \
        return ret; \
    }

DEFINE_FLOAT_EQUALTO_ROUTINE(FLOAT32, 32)
DEFINE_FLOAT_EQUALTO_ROUTINE(FLOAT64, 64)

/*-----------------------------*/
/* for reverse changing detect */
/*-----------------------------*/
#define DEFINE_ROUTINE_WITH_REVERSE_DETECT(DATATYPENAME, DATAWIDTH, MATCHTYPENAME, REVERSEMATCHTYPENAME) \
    int scan_routine_##DATATYPENAME##_##MATCHTYPENAME##_WITH_REVERSE SCAN_ROUTINE_ARGUMENTS \
    { \
        int ret = 0; \
        if ((new_value->flags.ineq_forwards) && scan_routine_##DATATYPENAME##_##MATCHTYPENAME (new_value, old_value, user_value, saveflags, address)) { ret = (DATAWIDTH)/8; SET_FLAG(saveflags, ineq_forwards); } \
        if ((new_value->flags.ineq_reverse) && scan_routine_##DATATYPENAME##_##REVERSEMATCHTYPENAME (new_value, old_value, user_value, saveflags, address)) { ret = (DATAWIDTH)/8; SET_FLAG(saveflags, ineq_reverse); } \
        return ret; \
    }

#define DEFINE_ROUTINE_WITH_REVERSE_DETECT_FOR_DATATYPE(DATATYPENAME, DATAWIDTH) \
    DEFINE_ROUTINE_WITH_REVERSE_DETECT(DATATYPENAME, DATAWIDTH, INCREASED, DECREASED) \
    DEFINE_ROUTINE_WITH_REVERSE_DETECT(DATATYPENAME, DATAWIDTH, DECREASED, INCREASED)

DEFINE_ROUTINE_WITH_REVERSE_DETECT_FOR_DATATYPE(INTEGER8, 8)
DEFINE_ROUTINE_WITH_REVERSE_DETECT_FOR_DATATYPE(INTEGER16, 16)
DEFINE_ROUTINE_WITH_REVERSE_DETECT_FOR_DATATYPE(INTEGER32, 32)
DEFINE_ROUTINE_WITH_REVERSE_DETECT_FOR_DATATYPE(INTEGER64, 64)
DEFINE_ROUTINE_WITH_REVERSE_DETECT_FOR_DATATYPE(FLOAT32, 32)
DEFINE_ROUTINE_WITH_REVERSE_DETECT_FOR_DATATYPE(FLOAT64, 64)

/*---------------------------------*/
/* for INCREASEDBY and DECREASEDBY */
/*---------------------------------*/
#define DEFINE_INTEGER_INCREASEDBY_DECREASEDBY_ROUTINE(DATATYPENAME, DATAWIDTH) \
    int scan_routine_##DATATYPENAME##_INCREASEDBY SCAN_ROUTINE_ARGUMENTS \
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
    int scan_routine_##DATATYPENAME##_DECREASEDBY SCAN_ROUTINE_ARGUMENTS \
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
    int scan_routine_##DATATYPENAME##_INCREASEDBY SCAN_ROUTINE_ARGUMENTS \
    { \
        int ret = 0; \
        if ((new_value->flags.f##DATAWIDTH##b) && (old_value->flags.f##DATAWIDTH##b) && (user_value->flags.f##DATAWIDTH##b) \
                && (get_f##DATAWIDTH##b(new_value) == get_f##DATAWIDTH##b(old_value) + get_f##DATAWIDTH##b(user_value))) \
            { ret = (DATAWIDTH)/8; SET_FLAG(saveflags, f##DATAWIDTH##b); } \
        return ret; \
    } \
    int scan_routine_##DATATYPENAME##_DECREASEDBY SCAN_ROUTINE_ARGUMENTS \
    { \
        int ret = 0; \
        if ((new_value->flags.f##DATAWIDTH##b) && (old_value->flags.f##DATAWIDTH##b) && (user_value->flags.f##DATAWIDTH##b) \
                && (get_f##DATAWIDTH##b(new_value) == get_f##DATAWIDTH##b(old_value) - get_f##DATAWIDTH##b(user_value))) \
            { ret = (DATAWIDTH)/8; SET_FLAG(saveflags, f##DATAWIDTH##b); } \
        return ret; \
    } 

DEFINE_FLOAT_INCREASEDBY_DECREASEDBY_ROUTINE(FLOAT32, 32)
DEFINE_FLOAT_INCREASEDBY_DECREASEDBY_ROUTINE(FLOAT64, 64)

/*----------------*/
/* for BYTEARRAY */
/*----------------*/
int scan_routine_BYTEARRAY_ANY SCAN_ROUTINE_ARGUMENTS
{
   return saveflags->bytearray_length = ((old_value)->flags.bytearray_length); 
}

int scan_routine_BYTEARRAY_EQUALTO SCAN_ROUTINE_ARGUMENTS
{
    bytearray_element_t *array = user_value->bytearray_value;
    int length = user_value->flags.bytearray_length;
    int cur_idx = 0;
    int i, j;
    value_t val_buf = *new_value;
    for(i = 0; i + sizeof(int64_t) < length; i += sizeof(int64_t))
    {
        /* match current block */
        for(j = 0; j < sizeof(int64_t); ++j)
        {
            if(     (array[cur_idx].is_wildcard == 1) 
                 || (array[cur_idx].byte == val_buf.bytes[j])) 
            {
                /* pass */
            }
            else
            {
                /* not matched */
                return 0;
            }
            ++ cur_idx;
        } 
         
        /* read next block */
        if (!peekdata(globals.target, address+i+sizeof(int64_t), &val_buf))
        {
            /* cannot read */
            return 0;
        }
    }

    /* match bytes left */
    for(j = 0; j < length - i; ++j)
    {
        if(     (array[cur_idx].is_wildcard == 1) 
             || (array[cur_idx].byte == val_buf.bytes[j])) 
        {
            /* pass */
        }
        else
        {
            /* not matched */
            return 0;
        }
        ++ cur_idx;
    } 
    
    /* matched */
    saveflags->bytearray_length = length;

    return length;
}

/*------------*/
/* for STRING */
/*------------*/
int scan_routine_STRING_ANY SCAN_ROUTINE_ARGUMENTS
{
   return saveflags->string_length = ((old_value)->flags.string_length); 
}
int scan_routine_STRING_EQUALTO SCAN_ROUTINE_ARGUMENTS
{
    const char *scan_string = user_value->string_value;
    int length = user_value->flags.string_length;
    int i, j;
    value_t val_buf = *new_value;
    for(i = 0; i + sizeof(int64_t) < length; i += sizeof(int64_t))
    {
        if(val_buf.int64_value != *((int64_t*)(scan_string+i)))
        {
            /* not matched */
            return 0;
        } 
         
        /* read next block */
        if (!peekdata(globals.target, address+i+sizeof(int64_t), &val_buf))
        {
            /* cannot read */
            return 0;
        }
    }

    /* match bytes left */
    for(j = 0; j < length - i; ++j)
    {
        if(val_buf.bytes[j] != *(scan_string+i+j))
        {
            /* not matched */
            return 0;
        }
    } 
    
    /* matched */
    saveflags->string_length = length;

    return length;
}
/*-------------------------*/
/* Any-xxx types specifiec */
/*-------------------------*/
/* this is for anynumber, anyinteger, anyfloat */
#define DEFINE_ANYTYPE_ROUTINE(MATCHTYPENAME) \
    int scan_routine_ANYINTEGER_##MATCHTYPENAME SCAN_ROUTINE_ARGUMENTS \
    { \
        int ret = 0, tmp_ret;\
        if ((tmp_ret = scan_routine_INTEGER8_##MATCHTYPENAME (new_value, old_value, user_value, saveflags, address)) > ret) { ret = tmp_ret; } \
        if ((tmp_ret = scan_routine_INTEGER16_##MATCHTYPENAME (new_value, old_value, user_value, saveflags, address)) > ret) { ret = tmp_ret; } \
        if ((tmp_ret = scan_routine_INTEGER32_##MATCHTYPENAME (new_value, old_value, user_value, saveflags, address)) > ret) { ret = tmp_ret; } \
        if ((tmp_ret = scan_routine_INTEGER64_##MATCHTYPENAME (new_value, old_value, user_value, saveflags, address)) > ret) { ret = tmp_ret; } \
        return ret; \
    } \
    int scan_routine_ANYFLOAT_##MATCHTYPENAME SCAN_ROUTINE_ARGUMENTS \
    { \
        int ret = 0, tmp_ret; \
        if ((tmp_ret = scan_routine_FLOAT32_##MATCHTYPENAME (new_value, old_value, user_value, saveflags, address)) > ret) { ret = tmp_ret; } \
        if ((tmp_ret = scan_routine_FLOAT64_##MATCHTYPENAME (new_value, old_value, user_value, saveflags, address)) > ret) { ret = tmp_ret; } \
        return ret; \
    } \
    int scan_routine_ANYNUMBER_##MATCHTYPENAME SCAN_ROUTINE_ARGUMENTS \
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
DEFINE_ANYTYPE_ROUTINE(INCREASED_WITH_REVERSE)
DEFINE_ANYTYPE_ROUTINE(DECREASED_WITH_REVERSE)
DEFINE_ANYTYPE_ROUTINE(INCREASEDBY)
DEFINE_ANYTYPE_ROUTINE(DECREASEDBY)



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



bool choose_scanroutine(scan_data_type_t dt, scan_match_type_t mt)
{
    return (g_scan_routine = get_scanroutine(dt, mt)) != NULL;
}

scan_routine_t get_scanroutine(scan_data_type_t dt, scan_match_type_t mt)
{
    if (globals.options.detect_reverse_change)
    {
        CHOOSE_ROUTINE_FOR_ALL_NUMBER_TYPES(MATCHINCREASED, INCREASED_WITH_REVERSE)
        CHOOSE_ROUTINE_FOR_ALL_NUMBER_TYPES(MATCHDECREASED, DECREASED_WITH_REVERSE)
    }

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

    CHOOSE_ROUTINE(BYTEARRAY, BYTEARRAY, MATCHEQUALTO, EQUALTO) 
    CHOOSE_ROUTINE(STRING, STRING, MATCHEQUALTO, EQUALTO) 

    return NULL;
}


