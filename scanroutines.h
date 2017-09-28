/*
    Definition of routines of scanning for different data types.

    Copyright (C) 2009,2010 WANG Lu  <coolwanglu(a)gmail.com>
    Copyright (C) 2015      Vyacheslav Shegai <v.shegai(a)netris.ru>

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
    MATCHANY,                /* for snapshot */
    /* following: compare with a given value */
    MATCHEQUALTO,
    MATCHNOTEQUALTO,
    MATCHGREATERTHAN,
    MATCHLESSTHAN,
    MATCHRANGE,
    /* following: compare with the old value */
    MATCHUPDATE,
    MATCHNOTCHANGED,
    MATCHCHANGED,
    MATCHINCREASED,
    MATCHDECREASED,
    /* following: compare with both given value and old value */
    MATCHINCREASEDBY,
    MATCHDECREASEDBY
} scan_match_type_t;


/* Matches a memory area given by `memory_ptr` and `memlength` against `user_value` or `old_value`
 * (or both, depending on the matching type), stores the result into saveflags.
 * NOTE: saveflags must be set to 0, since only useful bits are set, but extra bits are not cleared!
 * Returns the number of bytes needed to store said match, 0 for not matched
 */
typedef unsigned int (*scan_routine_t)(const mem64_t *memory_ptr, size_t memlength,
                                       const value_t *old_value, const uservalue_t *user_value, match_flags *saveflags);
extern scan_routine_t sm_scan_routine;

/* 
 * Choose the global scanroutine according to the given parameters, sm_scan_routine will be set.
 * Returns whether a proper routine has been found.
 */
bool sm_choose_scanroutine(scan_data_type_t dt, scan_match_type_t mt, const uservalue_t* uval, bool reverse_endianness);

scan_routine_t sm_get_scanroutine(scan_data_type_t dt, scan_match_type_t mt, match_flags uflags, bool reverse_endianness);

#endif /* SCANROUTINES_H */
