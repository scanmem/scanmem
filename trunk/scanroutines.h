/*
    scanroutines.h  Definition of routines of scanning for different data types
    Copyright (C) 2009 WANG Lu  <coolwanglu(a)gmail.com>

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

#ifndef SCANROUTINES_H__
#define SCANROUTINES_H__

#include <stdbool.h>

#include "value.h"

typedef enum {
    ANYNUMBER,              /* ANYINTEGER or ANYFLOAT */
    ANYINTEGER,             /* INTEGER of whatever width */
    ANYFLOAT,               /* FLOAT of whatever width */
    INTEGER8,
    INTEGER16,
    INTEGER32,
    INTEGER64,
    FLOAT32,
    FLOAT64 
} scan_data_type_t;

typedef enum {
    MATCHANY,                 /* to update */
    /* following: compare with a given value */
    MATCHEQUALTO,
    MATCHNOTEQUALTO,          
    /* following: compare with the old value */
    MATCHNOTCHANGED,
    MATCHCHANGED,
    MATCHINCREASED,
    MATCHDECREASED
} scan_match_type_t;


/* compare v1 and v2, and store the result (if any) into save */
typedef bool (*scan_routine_t)(const value_t *v1, const value_t *v2, value_t *save);
extern scan_routine_t g_scan_routine;

/* 
 * choose the global scanroutine according to the given parameters, g_scan_routine will be set 
 * return whether a proper routine has been found
 */
bool choose_scanroutine(scan_data_type_t dt, scan_match_type_t mt);

scan_routine_t get_scanroutine(scan_data_type_t dt, scan_match_type_t mt);
#endif /* SCANROUTINES_H__ */
