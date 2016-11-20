/*
    Definition of routines of scanning for different data types.

    Copyright (C) 2009,2010 WANG Lu  <coolwanglu(a)gmail.com>

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

#ifndef SCANROUTINES_H
#define SCANROUTINES_H

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
    FLOAT64,
    BYTEARRAY,
    STRING
} scan_data_type_t;

typedef enum {
    MATCHANY,                 /* to update */
    /* following: compare with a given value */
    MATCHEQUALTO,
    MATCHNOTEQUALTO,          
    MATCHGREATERTHAN,
    MATCHLESSTHAN,
    MATCHRANGE,
    /* following: compare with the old value */
    MATCHNOTCHANGED,
    MATCHCHANGED,
    MATCHINCREASED,
    MATCHDECREASED,
    /* folowing: compare with both given value and old value */
    MATCHINCREASEDBY,
    MATCHDECREASEDBY
} scan_match_type_t;


/* match old_value against new_value or user_value (or both, depending on the matching type, store the result into save */
/* NOTE: saveflag must be set to 0, since only useful bits are set, but extra bits are not cleared! */
/*       address is pointing to new_value in TARGET PROCESS MEMORY SPACE, used when searching for a byte array */
/* return the number of bytes needed to store old_value, 0 for not matched */
typedef int (*scan_routine_t)(const value_t *new_value, const value_t *old_value, const uservalue_t *user_value, match_flags *saveflag, void *address);
extern scan_routine_t g_scan_routine;

/* 
 * choose the global scanroutine according to the given parameters, g_scan_routine will be set 
 * return whether a proper routine has been found
 */
bool choose_scanroutine(scan_data_type_t dt, scan_match_type_t mt);

scan_routine_t get_scanroutine(scan_data_type_t dt, scan_match_type_t mt);

#endif /* SCANROUTINES_H */
