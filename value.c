/*
    Simple routines for working with the value_t data structure.

    Copyright (C) 2006,2007,2009 Tavis Ormandy <taviso@sdf.lonestar.org>
    Copyright (C) 2009           Eli Dupree <elidupree@charter.net>
    Copyright (C) 2009,2010      WANG Lu <coolwanglu@gmail.com>
    Copyright (C) 2015           Sebastian Parschauer <s.parschauer@gmx.de>

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

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <limits.h>
#include <assert.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <inttypes.h> /* for fixed-width formatters */

#include "value.h"
#include "show_message.h"

void valtostr(const value_t *val, char *str, size_t n)
{
    char buf[128];
    int np = 0;

#define FLAG_MACRO(bytes, string) \
    (val->flags & flag_u##bytes##b && val->flags & flag_s##bytes##b) ? (string " ") : \
        (val->flags & flag_u##bytes##b) ? (string "u ") : \
            (val->flags & flag_s##bytes##b) ? (string "s ") : ""
    
    /* set the flags */
    np = snprintf(buf, sizeof(buf), "[%s%s%s%s%s%s]",
         FLAG_MACRO(64, "I64"),
         FLAG_MACRO(32, "I32"),
         FLAG_MACRO(16, "I16"),
         FLAG_MACRO(8,  "I8"),
         (val->flags & flag_f64b) ? "F64 " : "",
         (val->flags & flag_f32b) ? "F32 " : "");
    /* handle having no type at all */
    if (np <= 2) {
        show_debug("BUG: No type\n");
        goto err;
    }

         if (val->flags & flag_u64b) np = snprintf(str, n, "%" PRIu64 ", %s", get_u64b(val), buf);
    else if (val->flags & flag_s64b) np = snprintf(str, n, "%" PRId64 ", %s", get_s64b(val), buf);
    else if (val->flags & flag_u32b) np = snprintf(str, n, "%" PRIu32 ", %s", get_u32b(val), buf);
    else if (val->flags & flag_s32b) np = snprintf(str, n, "%" PRId32 ", %s", get_s32b(val), buf);
    else if (val->flags & flag_u16b) np = snprintf(str, n, "%" PRIu16 ", %s", get_u16b(val), buf);
    else if (val->flags & flag_s16b) np = snprintf(str, n, "%" PRId16 ", %s", get_s16b(val), buf);
    else if (val->flags & flag_u8b)  np = snprintf(str, n, "%" PRIu8 ", %s",  get_u8b(val),  buf);
    else if (val->flags & flag_s8b)  np = snprintf(str, n, "%" PRId8 ", %s",  get_s8b(val),  buf);
    else if (val->flags & flag_f64b) np = snprintf(str, n, "%lg, %s", get_f64b(val), buf);
    else if (val->flags & flag_f32b) np = snprintf(str, n, "%g, %s", get_f32b(val), buf);
    else {
        show_debug("BUG: No formatting found\n");
        goto err;
    }
    if (np <= 0 || np >= (n - 1))
        goto err;

    return;
err:
    /* always print a value and a type to not crash front-ends */
    strncpy(str, "unknown, [unknown]", n);
}

void valcpy(value_t * dst, const value_t * src)
{
    memcpy(dst, src, sizeof(value_t));
    return;
}

/* dst.flags must be set beforehand. Prefer setting floats to ints */
void uservalue2value(value_t *dst, const uservalue_t *src)
{
    /* Zero whole value union, in case high bytes won't be set */
    dst->uint64_value = 0;

         if (dst->flags & flag_f64b) set_f64b(dst, get_f64b(src));
    else if (dst->flags & flag_u64b) set_u64b(dst, get_u64b(src));
    else if (dst->flags & flag_s64b) set_s64b(dst, get_s64b(src));

    else if (dst->flags & flag_f32b) set_f32b(dst, get_f32b(src));
    else if (dst->flags & flag_u32b) set_u32b(dst, get_u32b(src));
    else if (dst->flags & flag_s32b) set_s32b(dst, get_s32b(src));

    else if (dst->flags & flag_u16b) set_u16b(dst, get_u16b(src));
    else if (dst->flags & flag_s16b) set_s16b(dst, get_s16b(src));

    else if (dst->flags & flag_u8b)  set_u8b (dst, get_u8b(src));
    else if (dst->flags & flag_s8b)  set_s8b (dst, get_s8b(src));

    else assert(false);
}

/* parse bytearray, it will allocate the arrays itself, then needs to be free'd by `free_uservalue()` */
bool parse_uservalue_bytearray(char *const *argv, unsigned argc, uservalue_t *val)
{
    int i,j;
    uint8_t *bytes_array = malloc(argc*sizeof(uint8_t));
    wildcard_t *wildcards_array = malloc(argc*sizeof(wildcard_t));

    if (bytes_array == NULL || wildcards_array == NULL)
    {
        show_error("memory allocation for bytearray failed.\n");
        goto err;
    }

    const char *cur_str;
    char *endptr;

    for(i = 0; i < argc; ++i)
    {
        /* get current string */
        cur_str = argv[i];
        /* test its length */
        for(j = 0; (j < 3) && (cur_str[j]); ++j) {}
        if (j != 2) /* length is not 2 */
            goto err;

        if (strcmp(cur_str, "??") == 0)
        {
            wildcards_array[i] = WILDCARD;
            bytes_array[i] = 0x00;
        }
        else
        {
            /* parse as hex integer */
            uint8_t cur_byte = (uint8_t)strtoul(cur_str, &endptr, 16);
            if (*endptr != '\0')
                goto err;

            wildcards_array[i] = FIXED;
            bytes_array[i] = cur_byte;
        }
    }

    /* everything is ok */
    val->bytearray_value = bytes_array;
    val->wildcard_value = wildcards_array;
    val->flags = argc;
    return true;

err:
    if (bytes_array) free(bytes_array);
    if (wildcards_array) free(wildcards_array);
    zero_uservalue(val);
    return false;
}

bool parse_uservalue_number(const char *nptr, uservalue_t * val)
{
    if (parse_uservalue_int(nptr, val))
    {
        val->flags |= flags_float;
        if (val->flags & flag_s64b) {
            val->float32_value = (float) val->int64_value;
            val->float64_value = (double) val->int64_value;
        }
        else {
            val->float32_value = (float) val->uint64_value;
            val->float64_value = (double) val->uint64_value;
        }
        return true;
    }
    else if(parse_uservalue_float(nptr, val))
    {
        double num = val->float64_value;
        if (num >=         0 && num <=  UINT8_MAX) { val->flags |= flag_u8b;  set_u8b(val,   (uint8_t)num); }
        if (num >=  INT8_MIN && num <=   INT8_MAX) { val->flags |= flag_s8b;  set_s8b(val,    (int8_t)num); }
        if (num >=         0 && num <= UINT16_MAX) { val->flags |= flag_u16b; set_u16b(val, (uint16_t)num); }
        if (num >= INT16_MIN && num <=  INT16_MAX) { val->flags |= flag_s16b; set_s16b(val,  (int16_t)num); }
        if (num >=         0 && num <= UINT32_MAX) { val->flags |= flag_u32b; set_u32b(val, (uint32_t)num); }
        if (num >= INT32_MIN && num <=  INT32_MAX) { val->flags |= flag_s32b; set_s32b(val,  (int32_t)num); }
        if (num >=         0 && num <= UINT64_MAX) { val->flags |= flag_u64b; set_u64b(val, (uint64_t)num); }
        if (num >= INT64_MIN && num <=  INT64_MAX) { val->flags |= flag_s64b; set_s64b(val,  (int64_t)num); }
        return true;
    }

    return false;
}

bool parse_uservalue_int(const char *nptr, uservalue_t * val)
{
    int64_t snum;
    bool valid_sint;
    uint64_t unum;
    bool valid_uint;
    char *endptr;

    assert(nptr != NULL);
    assert(val != NULL);

    zero_uservalue(val);

    /* skip past any whitespace */
    while (isspace(*nptr))
        ++nptr;

    /* parse it as signed int */
    errno = 0;
    snum = strtoll(nptr, &endptr, 0);
    valid_sint = (errno == 0) && (*endptr == '\0');

    /* parse it as unsigned int */
    errno = 0;
    unum = strtoull(nptr, &endptr, 0);
    valid_uint = (*nptr != '-') && (errno == 0) && (*endptr == '\0');

    if (!valid_sint && !valid_uint)
        return false;

    /* determine correct flags */
    if (valid_uint &&                      unum <=  UINT8_MAX) { val->flags |= flag_u8b;  set_u8b(val,   (uint8_t)unum); }
    if (valid_sint && snum >=  INT8_MIN && snum <=   INT8_MAX) { val->flags |= flag_s8b;  set_s8b(val,    (int8_t)snum); }
    if (valid_uint &&                      unum <= UINT16_MAX) { val->flags |= flag_u16b; set_u16b(val, (uint16_t)unum); }
    if (valid_sint && snum >= INT16_MIN && snum <=  INT16_MAX) { val->flags |= flag_s16b; set_s16b(val,  (int16_t)snum); }
    if (valid_uint &&                      unum <= UINT32_MAX) { val->flags |= flag_u32b; set_u32b(val, (uint32_t)unum); }
    if (valid_sint && snum >= INT32_MIN && snum <=  INT32_MAX) { val->flags |= flag_s32b; set_s32b(val,  (int32_t)snum); }
    if (valid_uint &&                      unum <= UINT64_MAX) { val->flags |= flag_u64b; set_u64b(val, (uint64_t)unum); }
    if (valid_sint && snum >= INT64_MIN && snum <=  INT64_MAX) { val->flags |= flag_s64b; set_s64b(val,  (int64_t)snum); }

    return true;
}

bool parse_uservalue_float(const char *nptr, uservalue_t * val)
{
    double num;
    char *endptr;
    assert(nptr);
    assert(val);

    zero_uservalue(val);
    while (isspace(*nptr))
        ++nptr;

    errno = 0;
    num = strtod(nptr, &endptr);
    if ((errno != 0) || (*endptr != '\0'))
        return false;
    
    /* I'm not sure how to distinguish between float and double, but I guess it's not necessary here */
    val->flags |= flags_float;
    val->float32_value = (float) num;
    val->float64_value =  num;   
    return true;
}

void free_uservalue(uservalue_t *uval)
{
    /* bytearray arrays are dynamically allocated and have to be freed, strings are not */
    if (uval->bytearray_value)
        free((void*)uval->bytearray_value);
    if (uval->wildcard_value)
        free((void*)uval->wildcard_value);
}
