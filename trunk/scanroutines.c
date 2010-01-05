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
#include <config.h>

#include "scanroutines.h"
#include "value.h"
#include "scanmem.h"

bool (*g_scan_routine)(const value_t *v1, const value_t *v2, match_flags *saveflags);


/********************/
/* Integer specific */

/* for MATCHANY */
#define DEFINE_INTEGER_MATCHANY_ROUTINE(DATATYPENAME, DATAWIDTH) \
    bool scan_routine_##DATATYPENAME##_ANY (const value_t *v1, const value_t *v2, match_flags *saveflags) \
    { \
        bool ret = false; \
        if ((v1)->flags.s##DATAWIDTH##b) { ret = true; saveflags->s##DATAWIDTH##b = 1; } \
        if ((v1)->flags.u##DATAWIDTH##b) { ret = true; saveflags->u##DATAWIDTH##b = 1; } \
        return ret; \
    }

DEFINE_INTEGER_MATCHANY_ROUTINE(INTEGER8, 8)
DEFINE_INTEGER_MATCHANY_ROUTINE(INTEGER16, 16)
DEFINE_INTEGER_MATCHANY_ROUTINE(INTEGER32, 32)
DEFINE_INTEGER_MATCHANY_ROUTINE(INTEGER64, 64)

#define VALUE_COMP(a,b,field,op)    (((a)->flags.field && (b)->flags.field) && (get_##field(a) op get_##field(b)))
#define VALUE_COPY(a,b,field)       ((set_##field(a, get_##field(b))), ((a)->flags.field = 1))
#define SET_FLAG(f, field)          ((f)->field = 1)

#define DEFINE_INTEGER_ROUTINE(DATATYPENAME, DATAWIDTH, MATCHTYPENAME, MATCHTYPE) \
    bool scan_routine_##DATATYPENAME##_##MATCHTYPENAME (const value_t *v1, const value_t *v2, match_flags *saveflags) \
    { \
        bool ret = false; \
        if(VALUE_COMP(v1, v2, s##DATAWIDTH##b, MATCHTYPE)) { \
            SET_FLAG(saveflags, s##DATAWIDTH##b); \
            ret = true; \
        } \
        if(VALUE_COMP(v1, v2, u##DATAWIDTH##b, MATCHTYPE)) { \
            SET_FLAG(saveflags, u##DATAWIDTH##b); \
            ret = true; \
        } \
        return ret; \
    }

#define DEFINE_INTEGER_ROUTINE_FOR_ALL_INTEGER_TYPE(MATCHTYPENAME, MATCHTYPE) \
    DEFINE_INTEGER_ROUTINE(INTEGER8, 8, MATCHTYPENAME, MATCHTYPE) \
    DEFINE_INTEGER_ROUTINE(INTEGER16, 16, MATCHTYPENAME, MATCHTYPE) \
    DEFINE_INTEGER_ROUTINE(INTEGER32, 32, MATCHTYPENAME, MATCHTYPE) \
    DEFINE_INTEGER_ROUTINE(INTEGER64, 64, MATCHTYPENAME, MATCHTYPE) 

DEFINE_INTEGER_ROUTINE_FOR_ALL_INTEGER_TYPE(EQUALTO, ==)
DEFINE_INTEGER_ROUTINE_FOR_ALL_INTEGER_TYPE(NOTCHANGED, ==)
DEFINE_INTEGER_ROUTINE_FOR_ALL_INTEGER_TYPE(CHANGED, !=)
DEFINE_INTEGER_ROUTINE_FOR_ALL_INTEGER_TYPE(NOTEQUALTO, !=)
DEFINE_INTEGER_ROUTINE_FOR_ALL_INTEGER_TYPE(INCREASED, >)
DEFINE_INTEGER_ROUTINE_FOR_ALL_INTEGER_TYPE(DECREASED, <)

/******************/
/* Float specific */

/* for MATCHANY */
#define DEFINE_FLOAT_MATCHANY_ROUTINE(DATATYPENAME, DATAWIDTH) \
    bool scan_routine_##DATATYPENAME##_ANY (const value_t *v1, const value_t *v2, match_flags *saveflags) \
    { \
        bool ret = false; \
        if ((v1)->flags.f##DATAWIDTH##b) { ret = true; saveflags->f##DATAWIDTH##b = 1; } \
        return ret; \
    }

DEFINE_FLOAT_MATCHANY_ROUTINE(FLOAT32, 32)
DEFINE_FLOAT_MATCHANY_ROUTINE(FLOAT64, 64)

#define DEFINE_FLOAT_ROUTINE(DATATYPENAME, DATAWIDTH, MATCHTYPENAME, MATCHTYPE) \
    bool scan_routine_##DATATYPENAME##_##MATCHTYPENAME (const value_t *v1, const value_t *v2, match_flags *saveflags) \
    { \
        bool ret = false; \
        if(VALUE_COMP(v1, v2, f##DATAWIDTH##b, MATCHTYPE)) { \
            SET_FLAG(saveflags, f##DATAWIDTH##b); \
            ret = true; \
        } \
        return ret; \
    } \

#define DEFINE_FLOAT_ROUTINE_FOR_ALL_FLOAT_TYPE(MATCHTYPENAME, MATCHTYPE) \
    DEFINE_FLOAT_ROUTINE(FLOAT32, 32, MATCHTYPENAME, MATCHTYPE) \
    DEFINE_FLOAT_ROUTINE(FLOAT64, 64, MATCHTYPENAME, MATCHTYPE) 

DEFINE_FLOAT_ROUTINE_FOR_ALL_FLOAT_TYPE(NOTEQUALTO, !=)   /* TODO: this should be consistent with !EQUALTO */
DEFINE_FLOAT_ROUTINE_FOR_ALL_FLOAT_TYPE(NOTCHANGED, ==)         /* this is bad */
DEFINE_FLOAT_ROUTINE_FOR_ALL_FLOAT_TYPE(CHANGED, !=)      /* this is bad, but better than above */
DEFINE_FLOAT_ROUTINE_FOR_ALL_FLOAT_TYPE(INCREASED, >)
DEFINE_FLOAT_ROUTINE_FOR_ALL_FLOAT_TYPE(DECREASED, <)

/********************/
/* Special routines */

/*************************************/
/* special EQUALTO for float numbers */
/* currently we round both of them to integers and compare them */
/* TODO: let user specify a float number */
#define DEFINE_FLOAT_EQUALTO_ROUTINE(FLOATTYPENAME, WIDTH) \
    bool scan_routine_##FLOATTYPENAME##_EQUALTO (const value_t *v1, const value_t *v2, match_flags *saveflags) \
    { \
        bool ret = false; \
        if((v1->flags.f##WIDTH##b) && (v2->flags.f##WIDTH##b) && ((int##WIDTH##_t)get_f##WIDTH##b(v1) == (int##WIDTH##_t)get_f##WIDTH##b(v2))) \
        { \
            SET_FLAG(saveflags, f##WIDTH##b); \
            ret = true; \
        } \
        return ret; \
    }

DEFINE_FLOAT_EQUALTO_ROUTINE(FLOAT32, 32)
DEFINE_FLOAT_EQUALTO_ROUTINE(FLOAT64, 64)

/*******************************/
/* for reverse changing detect */
#define DEFINE_ROUTINE_WITH_REVERSE_DETECT(DATATYPENAME, DATAWIDTH, MATCHTYPENAME, REVERSEMATCHTYPENAME) \
    bool scan_routine_##DATATYPENAME##_##MATCHTYPENAME##_WITH_REVERSE (const value_t *v1, const value_t *v2, match_flags *saveflags) \
    { \
        bool ret = false; \
        if ((v1->flags.ineq_forwards) && scan_routine_##DATATYPENAME##_##MATCHTYPENAME (v1, v2, saveflags)) { ret = true; saveflags->ineq_forwards = 1; } \
        if ((v1->flags.ineq_reverse) && scan_routine_##DATATYPENAME##_##REVERSEMATCHTYPENAME (v1, v2, saveflags)) { ret = true; saveflags->ineq_reverse = 1; } \
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


/***************************/
/* Any-xxx types specifiec */
/* this is for anynumber, anyinteger, anyfloat */
#define DEFINE_ANYTYPE_ROUTINE(MATCHTYPENAME) \
    bool scan_routine_ANYINTEGER_##MATCHTYPENAME (const value_t *v1, const value_t *v2, match_flags *saveflags) \
    { \
        bool ret = false; \
        if (scan_routine_INTEGER8_##MATCHTYPENAME (v1, v2, saveflags)) { ret = true; } \
        if (scan_routine_INTEGER16_##MATCHTYPENAME (v1, v2, saveflags)) { ret = true; } \
        if (scan_routine_INTEGER32_##MATCHTYPENAME (v1, v2, saveflags)) { ret = true; } \
        if (scan_routine_INTEGER64_##MATCHTYPENAME (v1, v2, saveflags)) { ret = true; } \
        return ret; \
    } \
    bool scan_routine_ANYFLOAT_##MATCHTYPENAME (const value_t *v1, const value_t *v2, match_flags *saveflags) \
    { \
        bool ret = false; \
        if (scan_routine_FLOAT32_##MATCHTYPENAME (v1, v2, saveflags)) { ret = true; } \
        if (scan_routine_FLOAT64_##MATCHTYPENAME (v1, v2, saveflags)) { ret = true; } \
        return ret; \
    } \
    bool scan_routine_ANYNUMBER_##MATCHTYPENAME (const value_t *v1, const value_t *v2, match_flags *saveflags) \
    { \
        bool ret = false; \
        if (scan_routine_ANYINTEGER_##MATCHTYPENAME (v1, v2, saveflags)) { ret = true; } \
        if (scan_routine_ANYFLOAT_##MATCHTYPENAME (v1, v2, saveflags)) { ret = true; } \
        return ret; \
    } \

DEFINE_ANYTYPE_ROUTINE(ANY)
DEFINE_ANYTYPE_ROUTINE(EQUALTO)
DEFINE_ANYTYPE_ROUTINE(NOTEQUALTO)
DEFINE_ANYTYPE_ROUTINE(CHANGED)
DEFINE_ANYTYPE_ROUTINE(NOTCHANGED)
DEFINE_ANYTYPE_ROUTINE(INCREASED)
DEFINE_ANYTYPE_ROUTINE(DECREASED)
DEFINE_ANYTYPE_ROUTINE(INCREASED_WITH_REVERSE)
DEFINE_ANYTYPE_ROUTINE(DECREASED_WITH_REVERSE)

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



/* TODO: fix ineq_reserve things */
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

    return NULL;
}


