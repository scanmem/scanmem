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

#ifndef SETS_H
#define SETS_H

#include <stdlib.h>

struct set {
    size_t  size; /* size of set (used) */
    size_t *buf;  /* value buffer       */
};

static inline void set_cleanup(struct set *set)
{
    if (set)
        free(set->buf);
}

/* iterate over set from front (forwards) */
#define foreach_set_fw(i, set) \
    for (size_t i = 0; i < (set)->size; i++)

/*
 * iterate over set from back (backwards)
 *
 * NOTE: `_reserved_for_set_iteration_zzaw2_df_` is reserved, named randomly to avoid namespace crash
 */
#define foreach_set_bw(i, set)                                                \
    for (size_t _reserved_for_set_iteration_zzaw2_df_ = 0, i = (set)->size-1; \
         _reserved_for_set_iteration_zzaw2_df_ < (set)->size;                 \
         _reserved_for_set_iteration_zzaw2_df_++, i--)

bool parse_uintset(const char *, struct set *, size_t);
#endif /* SETS_H */
