/*
 $Id: maps.h,v 1.2 2007-06-05 01:45:35+01 taviso Exp $
 
 Copyright (C) 2009,2010      WANG  Lu      <coolwanglu(a)gmail.com>
 Copyright (C) 2009           Eli   Dupree  <elidupree(a)charter.net>
 Copyright (C) 2006,2007,2009 Tavis Ormandy <taviso@sdf.lonestar.org>
 
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

#ifndef _MAPS_INC
#define _MAPS_INC            /* include guard */

#include "list.h"

/* determine what regions we need */
typedef enum {
    REGION_ALL,                            /* each of them */
    REGION_HEAP_STACK_EXECUTABLE,          /* heap, stack, executable */
    REGION_HEAP_STACK_EXECUTABLE_BSS       /* heap, stack, executable, bss */
} region_scan_level_t;

/* a region obtained from /proc/pid/maps, these are searched for matches */
typedef struct {
    void *start;             /* start address. Hack: If HAVE_PROCMEM, this is actually an (unsigned long) offset into /proc/{pid}/mem */
    unsigned long size;              /* size */
    struct __attribute__((packed)) {
        unsigned read:1;
        unsigned write:1;
        unsigned exec:1;
        unsigned shared:1;
        unsigned private:1;
    } flags;
    unsigned id;                /* unique identifier */
    char filename[1];           /* associated file, must be last */
} region_t;

bool readmaps(pid_t target, list_t * regions);
int compare_region_id(const region_t *a, const region_t *b);

#endif /* _MAPS_INC */
