/*
 $Id: value.c,v 1.5 2007-04-11 10:43:27+01 taviso Exp taviso $

 simple routines for working with the value_t data structure.

 Copyright (C) 2006,2007 Tavis Ormandy <taviso@sdf.lonestar.org>

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

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <limits.h>
#include <assert.h>
#include <string.h>
#include <stdbool.h>

#include "value.h"
#include "scanmem.h"

bool valtostr(const value_t * val, char *str, size_t n)
{
    char buf[10];

    /* set the flags */
    snprintf(buf, sizeof(buf), "%c%c%c%c%c%c%c, ",
             val->flags.wchar ? 'C' : 'c',
             val->flags.wshort ? 'S' : 's',
             val->flags.wint ? 'I' : 'i',
             val->flags.wlong ? 'L' : 'l',
             val->flags.tfloat ? 'F' : 'f',
             val->flags.sign ? 'N' : 'n', val->flags.zero ? 'Z' : 'z');

    /* find the right format */
    if (val->flags.wlong) {
        if (val->flags.sign)
            snprintf(str, n, "%s%ld", buf, val->value.tslong);
        else
            snprintf(str, n, "%s%lu", buf, val->value.tulong);
    } else if (val->flags.wint) {
        if (val->flags.sign)
            snprintf(str, n, "%s%d", buf, val->value.tsint);
        else
            snprintf(str, n, "%s%u", buf, val->value.tuint);
    } else if (val->flags.wshort) {
        if (val->flags.sign)
            snprintf(str, n, "%s%hd", buf, val->value.tsshort);
        else
            snprintf(str, n, "%s%hu", buf, val->value.tushort);
    } else if (val->flags.wchar) {
        if (val->flags.sign)
            snprintf(str, n, "%s%hhd", buf, val->value.tschar);
        else
            snprintf(str, n, "%s%hhu", buf, val->value.tuchar);
    } else if (val->flags.tfloat) {
        snprintf(str, n, "%s%ld", buf, (signed long int) val->value.tfloat);
    } else {
        snprintf(str, n, "%s%#lx?", buf, val->value.tulong);
        return false;
    }

    return true;
}

void valcpy(value_t * dst, const value_t * src)
{
    memcpy(dst, src, sizeof(value_t));
    return;
}

/* XXX: this doesnt handle all cases yet */
void truncval(value_t * dst, const value_t * src)
{
    assert(dst != NULL);
    assert(src != NULL);

    memcpy(&dst->flags, &src->flags, sizeof(dst->flags));
    return;
}

/* set all possible width flags, if nothing is known about val */
void valnowidth(value_t * val)
{
    assert(val);

    val->flags.wchar = 1;
    val->flags.wshort = 1;
    val->flags.wint = 1;
    val->flags.wlong = 1;
    val->flags.zero = 0;
    val->flags.tfloat = 1;

    return;
}

void strtoval(const char *nptr, char **endptr, int base, value_t * val)
{
    signed long long num;

    assert(nptr != NULL);
    assert(val != NULL);

    memset(val, 0x00, sizeof(value_t));

    /* skip past any whitespace */
    while (isspace(*nptr))
        nptr++;

    /* now parse it using strtoul */
    num = strtoll(nptr, endptr, base);

    /* determine correct flags */

    /* if the first non-space character is '-' and the value isnt zero
       assume it's signed */
    if (*nptr == '-' && num != 0)
        val->flags.sign = 1;

    /* test what types this number could be represented with, set flags accordingly */
    if (val->flags.sign) {
        if (num >= SCHAR_MIN)
            val->flags.wchar = 1;
        if (num >= SHRT_MIN)
            val->flags.wshort = 1;
        if (num >= INT_MIN)
            val->flags.wint = 1;
        if (num >= LONG_MIN)
            val->flags.wlong = 1;
    } else {
        if (num <= UCHAR_MAX)
            val->flags.wchar = 1;
        if (num <= USHRT_MAX)
            val->flags.wshort = 1;
        if (num <= UINT_MAX)
            val->flags.wint = 1;
        if (num <= ULONG_MAX)
            val->flags.wlong = 1;
    }

    /* set zero based by default */
    val->flags.zero = 0;

    /* finally insert the number */
    val->value.tslong = (long) num;

    return;

}

/* eg: valuecmp(v1, (is) GREATERTHAN, v2), best match is put into save */
bool valuecmp(const value_t * v1, matchtype_t operator, const value_t * v2,
              value_t * save)
{
    value_t s, *v = &s;
    bool ret = false, invert = false;

    assert(v1 != NULL);
    assert(v2 != NULL);

    memset(v, 0x00, sizeof(value_t));

    switch (operator) {
    case MATCHNOTEQUAL:
        operator = MATCHEQUAL;
        invert = true;
        /*lint -fallthrough */
    case MATCHEQUAL:
    case MATCHEXACT:
        if (valueequal(v1, v2, wlong, tulong)) {
            valuecopy(v, v1, wlong, tulong);
            ret = true;
        }

        if (valueequal(v1, v2, wint, tuint)) {
            valuecopy(v, v1, wint, tuint);
            ret = true;
        }

        if (valueequal(v1, v2, wshort, tushort)) {
            valuecopy(v, v1, wshort, tushort);
            ret = true;
        }

        if (valueequal(v1, v2, wchar, tuchar)) {
            valuecopy(v, v1, wchar, tuchar);
            ret = true;
        }

        break;
    case MATCHLESSTHAN:
        if (valuelt(v1, v2, wlong, tulong)) {
            valuecopy(v, v1, wlong, tulong);
            ret = true;
        }

        if (valuelt(v1, v2, wint, tuint)) {
            valuecopy(v, v1, wint, tuint);
            ret = true;
        }

        if (valuelt(v1, v2, wshort, tushort)) {
            valuecopy(v, v1, wshort, tushort);
            ret = true;
        }

        if (valuelt(v1, v2, wchar, tuchar)) {
            valuecopy(v, v1, wchar, tuchar);
            ret = true;
        }

        /* could be a float */
        if (v1->flags.tfloat & v2->flags.tfloat) {
            if (v1->value.tfloat - v2->value.tfloat < 0) {
                v->flags.tfloat = 1;
                ret = true;
            }
        }

        break;
    case MATCHGREATERTHAN:
        if (valuegt(v1, v2, wlong, tulong)) {
            valuecopy(v, v1, wlong, tulong);
            ret = true;
        }

        if (valuegt(v1, v2, wint, tuint)) {
            valuecopy(v, v1, wint, tuint);
            ret = true;
        }

        if (valuegt(v1, v2, wshort, tushort)) {
            valuecopy(v, v1, wshort, tushort);
            ret = true;
        }

        if (valuegt(v1, v2, wchar, tuchar)) {
            valuecopy(v, v1, wchar, tuchar);
            ret = true;
        }

        /* could be a float */
        if (v1->flags.tfloat & v2->flags.tfloat) {
            if (v1->value.tfloat - v2->value.tfloat > 0) {
                v->flags.tfloat = 1;
                ret = true;
            }
        }

        break;
    }

    /* check floating point match recursively */
    /* XXX: next version, multiple rounding techniques */
    if (v1->flags.tfloat | v2->flags.tfloat) {
        value_t f, g;

        memset(&f, 0x00, sizeof(f));
        memset(&g, 0x00, sizeof(g));

        f.flags.wlong = g.flags.wlong = 1;

        if (v1->flags.tfloat && !v2->flags.tfloat) {
            f.value.tslong = (long) v1->value.tfloat;
            if (valuecmp(&f, operator, v2, NULL)) {
                v->value.tfloat = v1->value.tfloat;
                v->flags.tfloat = 1;
                ret = true;
            }
        } else if (v2->flags.tfloat && !v1->flags.tfloat) {
            f.value.tslong = (long) v2->value.tfloat;
            if (valuecmp(v1, operator, &f, NULL)) {
                v->value.tfloat = v2->value.tfloat;
                v->flags.tfloat = 1;
                ret = true;
            }
        } else {
            /* both could be fp */
            f.value.tslong = (long) v1->value.tfloat;
            g.value.tslong = (long) v2->value.tfloat;

            if (valuecmp(&f, operator, &g, NULL)) {
                /* XXX: v1 or v2? average of both? dunno.... */
                v->value.tfloat = v1->value.tfloat;
                v->flags.tfloat = 1;
                ret = true;
            }
        }
    }

    /* copy signedness over if we think this is a match */
    if (ret) {
        v->flags.sign = v1->flags.sign | v2->flags.sign;
    }

    if (save != NULL)
        memcpy(save, v, sizeof(value_t));

    if (invert)
        ret = !ret;

    return ret;
}
