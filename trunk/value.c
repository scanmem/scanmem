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
    char buf[50];
    int max_bytes = 0;
    bool print_as_unsigned = false;

#define FLAG_MACRO(bytes, string) (val->flags.u##bytes##b && val->flags.s##bytes##b) ? (string " ") : (val->flags.u##bytes##b) ? ("u" string " ") : (val->flags.s##bytes##b) ? ("s" string " ") : ""
    
    /* set the flags */
    snprintf(buf, sizeof(buf), "%s%s%s%s%s%s%s- ",
             FLAG_MACRO(64, "64"),
             FLAG_MACRO(32, "32"),
             FLAG_MACRO(16, "16"),
             FLAG_MACRO(8,  "8"),
             val->flags.f64b ? "D " : "",
             val->flags.f32b ? "F " : "",
             (val->flags.ineq_reverse && !val->flags.ineq_forwards) ? "(reversed inequality) " : "");

         if (val->flags.u64b) { max_bytes = 8; print_as_unsigned =  true; }
    else if (val->flags.s64b) { max_bytes = 8; print_as_unsigned = false; }
    else if (val->flags.u32b) { max_bytes = 4; print_as_unsigned =  true; }
    else if (val->flags.s32b) { max_bytes = 4; print_as_unsigned = false; }
    else if (val->flags.u16b) { max_bytes = 2; print_as_unsigned =  true; }
    else if (val->flags.s16b) { max_bytes = 2; print_as_unsigned = false; }
    else if (val->flags.u8b ) { max_bytes = 1; print_as_unsigned =  true; }
    else if (val->flags.s8b ) { max_bytes = 1; print_as_unsigned = false; }
    
    /* find the right format, considering different integer size implementations */
         if (max_bytes == sizeof(long long)) snprintf(str, n, print_as_unsigned ? "%s%llu" : "%s%lld", buf, print_as_unsigned ? val->value.tulonglongsize : val->value.tslonglongsize);
    else if (max_bytes == sizeof(long))      snprintf(str, n, print_as_unsigned ? "%s%lu"  : "%s%ld" , buf, print_as_unsigned ? val->value.tulongsize : val->value.tslongsize);
    else if (max_bytes == sizeof(int))       snprintf(str, n, print_as_unsigned ? "%s%u"   : "%s%d"  , buf, print_as_unsigned ? val->value.tuintsize : val->value.tsintsize);
    else if (max_bytes == sizeof(short))     snprintf(str, n, print_as_unsigned ? "%s%hu"  : "%s%hd" , buf, print_as_unsigned ? val->value.tushortsize : val->value.tsshortsize);
    else if (max_bytes == sizeof(char))      snprintf(str, n, print_as_unsigned ? "%s%hhu" : "%s%hhd", buf, print_as_unsigned ? val->value.tucharsize : val->value.tscharsize);
    else if (val->flags.f64b) snprintf(str, n, "%s%ld", buf, (signed long int) val->value.tf64b);
    else if (val->flags.f32b) snprintf(str, n, "%s%ld", buf, (signed long int) val->value.tf32b);
    else {
        snprintf(str, n, "%s%#lx?", buf, val->value.tulongsize);
        return false;
    }

    return true;
}

void valcpy(value_t * dst, const value_t * src)
{
    memcpy(dst, src, sizeof(value_t));
    return;
}

void truncval_to_flags(value_t * dst, match_flags flags)
{
    assert(dst != NULL);
    
    dst->flags.u64b &= flags.u64b;
    dst->flags.s64b &= flags.s64b;
    dst->flags.f64b &= flags.f64b;
    dst->flags.u32b &= flags.u32b;
    dst->flags.s32b &= flags.s32b;
    dst->flags.f32b &= flags.f32b;
    dst->flags.u16b &= flags.u16b;
    dst->flags.s16b &= flags.s16b;
    dst->flags.u8b  &= flags.u8b ;
    dst->flags.s8b  &= flags.s8b ;
    
    /* Hack - simply overwrite the inequality flags (this should have no effect except to make them display properly) */
    dst->flags.ineq_forwards = flags.ineq_forwards;
    dst->flags.ineq_reverse  = flags.ineq_reverse ;
}

void truncval(value_t * dst, const value_t * src)
{
    assert(src != NULL);

    truncval_to_flags(dst, src->flags);
}

/* set all possible width flags, if nothing is known about val */
void valnowidth(value_t * val)
{
    assert(val);

    val->flags.u64b = 1;
    val->flags.s64b = 1;
    val->flags.f64b = 1;
    val->flags.u32b = 1;
    val->flags.s32b = 1;
    val->flags.f32b = 1;
    val->flags.u16b = 1;
    val->flags.s16b = 1;
    val->flags.u8b  = 1;
    val->flags.s8b  = 1;
    
    val->flags.ineq_forwards = 1;
    val->flags.ineq_reverse = 1;

    return;
}

void strtoval(const char *nptr, char **endptr, int base, value_t * val)
{
    int64_t num;

    assert(nptr != NULL);
    assert(val != NULL);

    memset(val, 0x00, sizeof(value_t));

    /* skip past any whitespace */
    while (isspace(*nptr))
        nptr++;

    /* now parse it using strtoul */
    num = strtoll(nptr, endptr, base);

    /* determine correct flags */
    if (num >=       (0) && num < (1LL<< 8)) val->flags.u8b  = 1;
    if (num > -(1LL<< 7) && num < (1LL<< 7)) val->flags.s8b  = 1;
    if (num >=       (0) && num < (1LL<<16)) val->flags.u16b = 1;
    if (num > -(1LL<<15) && num < (1LL<<15)) val->flags.s16b = 1;
    if (num >=       (0) && num < (1LL<<32)) val->flags.u32b = 1;
    if (num > -(1LL<<31) && num < (1LL<<31)) val->flags.s32b = 1;
    if (num >=       (0) &&          (true)) val->flags.u64b = 1;
    if (          (true) &&          (true)) val->flags.s64b = 1;

    /* finally insert the number */
    val->value.ts64b = num;

    return;

}

#define valuegt(a,b,x,y) (((a)->flags.x && (b)->flags.x) && ((a)->value.y > (b)->value.y))
#define valuelt(a,b,x,y) (((a)->flags.x && (b)->flags.x) && ((a)->value.y < (b)->value.y))
#define valueequal(a,b,x,y) (((a)->flags.x && (b)->flags.x) && ((a)->value.y == (b)->value.y))
#define valuecopy(a,b,x,y) (((a)->value.y = (b)->value.y), ((a)->flags.x = 1))

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
    case MATCHANY:
        *save = *v1;
        return true;
    case MATCHEQUAL:
    case MATCHEXACT:
#define MATCH_MACRO_3(comp, signedness, bytes) \
        if (value##comp(v1, v2, signedness##bytes##b, t##signedness##bytes##b)) { \
            valuecopy(v, v1, signedness##bytes##b, t##signedness##bytes##b); \
            ret = true; \
        }
#define MATCH_MACRO_2(comp, bytes) MATCH_MACRO_3(comp, u, bytes) MATCH_MACRO_3(comp, s, bytes)
#define MATCH_MACRO(comp) MATCH_MACRO_2(comp, 8) MATCH_MACRO_2(comp, 16) MATCH_MACRO_2(comp, 32) MATCH_MACRO_2(comp, 64)

        MATCH_MACRO(equal);

        break;
    case MATCHNOTEQUAL:
        /* Fall through twice - either greater than or less than can apply */
    case MATCHLESSTHAN:
    
        MATCH_MACRO(lt);

        /* could be a float */
        /*if (v1->flags.f32b & v2->flags.f32b) {
            if (v1->value.tfloat - v2->value.tfloat < 0) {
                v->flags.f32b = 1;
                ret = true;
            }
        }*/

        if (operator == MATCHLESSTHAN) break;
    case MATCHGREATERTHAN:
    
        MATCH_MACRO(gt);

        /* could be a float */
        /*if (v1->flags.f32b & v2->flags.f32b) {
            if (v1->value.tfloat - v2->value.tfloat > 0) {
                v->flags.f32b = 1;
                ret = true;
            }
        }*/

        break;
    }

    /* check floating point match recursively */
    /* XXX: next version, multiple rounding techniques */
    /*if (v1->flags.f32b | v2->flags.f32b) {
        value_t f, g;

        memset(&f, 0x00, sizeof(f));
        memset(&g, 0x00, sizeof(g));

        f.flags.u32b = g.flags.u32b = 1;

        if (v1->flags.f32b && !v2->flags.f32b) {
            f.value.tslong = (long) v1->value.tfloat;
            if (valuecmp(&f, operator, v2, NULL)) {
                v->value.tfloat = v1->value.tfloat;
                v->flags.f32b = 1;
                ret = true;
            }
        } else if (v2->flags.f32b && !v1->flags.f32b) {
            f.value.tslong = (long) v2->value.tfloat;
            if (valuecmp(v1, operator, &f, NULL)) {
                v->value.tfloat = v2->value.tfloat;
                v->flags.f32b = 1;
                ret = true;
            }
        } else {
            / * both could be fp * /
            f.value.tslong = (long) v1->value.tfloat;
            g.value.tslong = (long) v2->value.tfloat;

            if (valuecmp(&f, operator, &g, NULL)) {
                / * XXX: v1 or v2? average of both? dunno.... * /
                v->value.tfloat = v1->value.tfloat;
                v->flags.f32b = 1;
                ret = true;
            }
        }
    }*/

    if (save != NULL)
        memcpy(save, v, sizeof(value_t));

    if (invert)
        ret = !ret;

    return ret;
}

typedef struct { bool retval; value_t saved; } valuecmp_test_t;
valuecmp_test_t valuecmp_test(const value_t v1, matchtype_t operator, const value_t v2)
{
    value_t saved;
    bool retval = valuecmp(&v1, operator, &v2, &saved);
    return (valuecmp_test_t){retval, saved};
}

int flags_to_max_width_in_bytes(match_flags flags)
{
	     if (flags.u64b || flags.s64b || flags.f64b) return 8;
	else if (flags.u32b || flags.s32b || flags.f32b) return 4;
	else if (flags.u16b || flags.s16b              ) return 2;
	else if (flags.u8b  || flags.s8b               ) return 1;
	else    /* It can't be a variable of any size */ return 0;
}

int val_max_width_in_bytes(value_t *val)
{
	return flags_to_max_width_in_bytes(val->flags);
}

unsigned char val_byte(value_t *val, int which)
{
	return *(((unsigned char *)(&val->value)) + which);
}
