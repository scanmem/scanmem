/*
 $Id: $

 Copyright (C) 2014           Hraban Luyat

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.
 
 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/


#ifndef _ENDIANNESS_INC
#define _ENDIANNESS_INC

#include "scanmem.h"
#include "value.h"

static const union { short x; char y; } endiantest = { .x = 0xabcd };
// True iff host is big endian
static const bool big_endian = endiantest.y == 0xab;

void fix_endianness(globals_t *vars, value_t *data_value);
void swap_bytes_var(void *p, size_t num);

#endif

