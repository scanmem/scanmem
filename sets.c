/*
    Create sets.

    Copyright (C) 2016-2017 Bijan Kazemi <bkazemi@users.sf.net>

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

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/types.h>
#include <ctype.h>
#include <assert.h>
#include <errno.h>

#include "show_message.h"
#include "sets.h"

#define DEFAULT_UINTLS_SZ (64)

/*
 * -= The Set Data-Type =-
 *
 * A 'set' is a type similar in vein to the mathematical definition of a set.
 * See: https://foldoc.org/set
 *
 * Sets support ranges. See the `delete` longdoc for more information.
 *
 * Restrictions of our set data-type:
 * - No infinite sets.
 * - No empty sets.
 * - No duplicate elements.
 * - All the elements must be of the same type.
 */

/*
 * Used with `qsort()`.
 *
 * This function uses the less elegant branching solution
 * to deal with possible overflow.
 */
static int _size_t_cmp(const void *_i1, const void *_i2)
{
    const size_t i1 = *(const size_t *)_i1, i2 = *(const size_t *)_i2;

    if      (i1 <  i2) return -1;
    else if (i1 == i2) return  0;
    else               return  1;
}

/*
 * Function used to grow the set array.
 *
 * We must use a separate pointer with realloc(), in case it fails. When that
 * happens and we re-assign valarr, valarr is _not_ free()'d by realloc(), and
 * valarr is then assigned to NULL, leaving the original valarr address lost.
 */
static inline bool inc_arr_sz(size_t **valarr, size_t *arr_maxsz, size_t maxsz)
{
    size_t *valarr_tmpptr;

    if (*arr_maxsz > maxsz / 2)
        *arr_maxsz = maxsz;
    else
        *arr_maxsz *= 2;

    if ((valarr_tmpptr = realloc(*valarr, *arr_maxsz * sizeof(size_t))) == NULL)
        return false;

    *valarr = valarr_tmpptr;

    return true;
}

bool parse_uintset(const char *lptr, struct set *set, size_t maxsz)
{
    const char        *tok, *tmpnum = NULL, *tmpnum_end;
    char              *tmpnum_endptr = NULL, *fail_reason = "BUG";
    bool               got_num, is_hex = false, invert = false;
    size_t             last_num = 0, arr_szfilled, arr_maxsz;
    size_t            *valarr;
    size_t             vaidx = 0;
    unsigned long long toknum;

    assert(lptr && set);

    /* ensure set is initialized */
    memset(set, 0, sizeof(*set));

    enum {
        NIL = -1,
        NUMBER_TOK,
        RANGE_TOK,
        COMMA_TOK,
    } last_type = NIL;

    /* allocate space for value pointers */
    if ((valarr = malloc(DEFAULT_UINTLS_SZ * sizeof(size_t))) == NULL) {
        show_error("%s(): OOM (Out Of Memory)\n", __func__);
        return false;
    }

    arr_szfilled = 0;
    arr_maxsz    = DEFAULT_UINTLS_SZ;
    got_num      = false;

    for (tok = lptr; *tok; tok++) {
        /* skip spaces */
        while (tok && isspace(*tok))
            tok++;

        /* check if we just went over trailing space */
        if (!tok || !*tok)
            break;

        assert(arr_szfilled <= arr_maxsz); // should never fail
        if (arr_szfilled == arr_maxsz)
            if (!inc_arr_sz(&valarr, &arr_maxsz, maxsz)) {
                fail_reason = "OOM (Out Of Memory)";
                goto error;
            }

        switch (*tok) {
        case '!':
            if (last_type != NIL) {
                fail_reason = "inversion only allowed at beginning of set";
                goto error;
            }
            invert = true;
            continue;
        case ',':
            if (last_type == RANGE_TOK) {
                fail_reason = "invalid range";
                goto error;
            }
            last_type = COMMA_TOK;
            continue;
        case '.':
            if (last_type == COMMA_TOK || last_type == RANGE_TOK) {
                fail_reason = "invalid range";
                goto error;
            }
            /* check for consecutive `..` */
            if (tok[1] != '.') {
                fail_reason = "bad token";
                goto error;
            }
            last_type = RANGE_TOK;
            tok++;
            continue;
        }

        if (isdigit(*tok)) {
            /* check for 0x hex prefix */
            if (*tok == '0' && tolower(tok[1]) == 'x') {
                if (!isxdigit(tok[2]))
                    continue;
                is_hex = true;
                tok   += 2;
            }

            /* find the end of this number in string */
            tmpnum_end = tok;
            for ( ; isxdigit(*tmpnum_end); tmpnum_end++)
                ;
            tmpnum = strndup(tok, tmpnum_end - tok);

            errno  = 0;
            toknum = strtoull(tmpnum, &tmpnum_endptr, is_hex ? 16 : 10);
            if (is_hex)
                is_hex = false;
            if (errno || *tmpnum_endptr != '\0') {
                fail_reason = "strtoull() failed";
                goto error;
            }

            free((void *)tmpnum);
            tmpnum = NULL;

            if (last_type == RANGE_TOK) {
                if (!got_num) {
                    /* we've got a {0 .. n} range */
                    last_num  = toknum;
                    last_type = NUMBER_TOK;
                    got_num   = true;
                    /* move token position to last number found (needed for 2+ digit nums */
                    tok = tmpnum_end-1;
                    /* bounds check */
                    if (last_num >= maxsz) {
                        fail_reason = "0..n range OOB (Out Of Bounds)";
                        goto error;
                    }
                    /* pre-set the new size filled and grow array if necessary */
                    if ((arr_szfilled += last_num+1) > arr_maxsz) // +1 for `0`
                        while (arr_szfilled > arr_maxsz) {
                            if (!inc_arr_sz(&valarr, &arr_maxsz, maxsz)) {
                                fail_reason = "OOM (Out Of Memory)";
                                goto error;
                            }
                        }
                    /* fill up array with range */
                    for (size_t i = 0; i <= last_num; i++)
                        valarr[vaidx++] = i;
                    continue;
                }
                if (toknum <= last_num || toknum >= maxsz) {
                    fail_reason = "invalid range";
                    goto error;
                }

                /* pre-set the new size filled and grow array if necessary */
                if ((arr_szfilled += (toknum - last_num)) > arr_maxsz)
                    while (arr_szfilled > arr_maxsz) {
                        if (!inc_arr_sz(&valarr, &arr_maxsz, maxsz)) {
                            fail_reason = "OOM (Out Of Memory)";
                            goto error;
                        }
                    }

                /* fill up array with range */
                for (size_t i = last_num+1; i <= toknum; i++)
                    valarr[vaidx++] = i;
            } else if (last_type == NUMBER_TOK) {
                /* sanity check */
                fail_reason = "impossible condition (last_type == NUMBER_TOK)";
                goto error;
            } else {
                valarr[vaidx++] = toknum;
                arr_szfilled++;
            }
            last_num  = toknum;
            last_type = NUMBER_TOK;
            if (!got_num)
                got_num = true;
            /* move token position to last number found (needed for 2+ digit nums) */
            tok = tmpnum_end-1;
        } else {
            fail_reason = "bad token";
            goto error;
        }
    }

    /* check for {n .. end} range */
    if (last_type == RANGE_TOK) {
        if (!got_num) {
            fail_reason = "invalid range";
            goto error;
        }
        if (last_num >= maxsz) {
            fail_reason = "n..end range OOB (Out Of Bounds)";
            goto error;
        }
        if ((arr_szfilled += (maxsz-1 - last_num)) > arr_maxsz)
            while (arr_szfilled > arr_maxsz) {
                if (!inc_arr_sz(&valarr, &arr_maxsz, maxsz)) {
                    fail_reason = "OOM (Out Of Memory)";
                    goto error;
                }
            }
        /* fill up array with range */
        for (size_t i = last_num+1; i < maxsz; i++)
            valarr[vaidx++] = i;
    }

    /* consider empty sets invalid */
    if (arr_szfilled == 0) {
        fail_reason = "empty set";
        goto error;
    }

    /* sort the value array */
    qsort(valarr, arr_szfilled, sizeof(valarr[0]), _size_t_cmp);

    /* check if there are any duplicates */
    for (size_t i = 0; i+1 < arr_szfilled; i++)
        if (valarr[i] == valarr[i+1]) {
            fail_reason = "duplicate element";
            goto error;
        }

    /* check range (for individual entries, faster than checking each one) */
    if (valarr[arr_szfilled-1] >= maxsz) {
        fail_reason = "OOB (Out Of Bounds) element(s)";
        goto error;
    }

    /* handle inverted sets */
    if (invert) {
        size_t *inv_valarr;
        if (arr_szfilled == maxsz) {
            fail_reason = "cannot invert the entire set!";
            goto error;
        }
        if ((inv_valarr = malloc((maxsz - arr_szfilled) * sizeof(size_t))) == NULL) {
            fail_reason = "OOM (Out Of Memory)";
            goto error;
        }
        for (size_t matchid = 0, va_idx = 0, inv_idx = 0; matchid < maxsz; matchid++) {
            if (va_idx == arr_szfilled || valarr[va_idx] > matchid)
                inv_valarr[inv_idx++] = matchid;
            else
                va_idx++;
        }
        free(valarr);
        valarr       = inv_valarr;
        arr_szfilled = maxsz - arr_szfilled;
    } else {
        size_t *tmp_vaptr;
        if ((tmp_vaptr = realloc(valarr, arr_szfilled * sizeof(size_t))) == NULL) {
            fail_reason = "couldn't deallocate possibly unused end of the buffer";
            goto error;
        }
        valarr = tmp_vaptr;
    }

    set->buf  = valarr;
    set->size = arr_szfilled;
    return true;

error:
    show_error("%s(): %s\n", __func__, fail_reason);
    free((void *)tmpnum);
    free(valarr);
    set_cleanup(set);
    return false;
}
