/*
    Endianness conversion.

    Copyright (C) 2014 Hraban Luyat
    Copyright (C) 2015 Sebastian Parschauer <s.parschauer@gmx.de>

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


#ifndef ENDIANNESS_H
#define ENDIANNESS_H

#include "config.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "value.h"

/* true if host is big endian */
#ifdef WORDS_BIGENDIAN
static const bool big_endian = true;
#else
static const bool big_endian = false;
#endif

static inline uint8_t swap_bytes8(uint8_t i)
{
    return i;
}

static inline uint16_t swap_bytes16(uint16_t i)
{
    uint16_t res = i & 0xff;
    res <<= 8;
    res |= i >> 8;
    return res;
}

static inline uint32_t swap_bytes32(uint32_t i)
{
    uint32_t res = swap_bytes16(i);
    res <<= 16;
    res |= swap_bytes16(i >> 16);
    return res;
}

static inline uint64_t swap_bytes64(uint64_t i)
{
    uint64_t res = swap_bytes32(i);
    res <<= 32;
    res |= swap_bytes32(i >> 32);
    return res;
}

// swap endianness of 2, 4 or 8 byte word in-place.
static inline void swap_bytes_var(void *p, size_t num)
{
    switch (num) {
    case sizeof(uint16_t): ; // empty statement to cheat the compiler
        uint16_t i16 = swap_bytes16(*((uint16_t *)p));
        memcpy(p, &i16, sizeof(uint16_t));
        return;
    case sizeof(uint32_t): ;
        uint32_t i32 = swap_bytes32(*((uint32_t *)p));
        memcpy(p, &i32, sizeof(uint32_t));
        return;
    case sizeof(uint64_t): ;
        uint64_t i64 = swap_bytes64(*((uint64_t *)p));
        memcpy(p, &i64, sizeof(uint64_t));
        return;
    }
    assert(false);
    return;
}

static inline void fix_endianness(value_t *data_value, bool reverse_endianness)
{
    if (!reverse_endianness) {
        return;
    }
    if (data_value->flags & flags_64b) {
        data_value->uint64_value = swap_bytes64(data_value->uint64_value);
    } else if (data_value->flags & flags_32b) {
        data_value->uint32_value = swap_bytes32(data_value->uint32_value);
    } else if (data_value->flags & flags_16b) {
        data_value->uint16_value = swap_bytes16(data_value->uint16_value);
    }
    return;
}

#endif /* ENDIANNESS_H */
