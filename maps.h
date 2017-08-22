/*
    Reading the data from /proc/pid/maps into a regions list.

    Copyright (C) 2006,2007,2009 Tavis Ormandy <taviso@sdf.lonestar.org>
    Copyright (C) 2009           Eli Dupree <elidupree@charter.net>
    Copyright (C) 2009,2010      WANG Lu <coolwanglu@gmail.com>
    Copyright (C) 2014-2016      Sebastian Parschauer <s.parschauer@gmx.de>

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

#ifndef MAPS_H
#define MAPS_H

#include <stdbool.h>
#include <sys/types.h>

#include "list.h"

/* determine which regions we need */
typedef enum {
    REGION_ALL,                            /* each of them */
    REGION_HEAP_STACK_EXECUTABLE,          /* heap, stack, executable */
    REGION_HEAP_STACK_EXECUTABLE_BSS       /* heap, stack, executable, bss */
} region_scan_level_t;

typedef enum {
    REGION_TYPE_MISC,
    REGION_TYPE_CODE,
    REGION_TYPE_EXE,
    REGION_TYPE_HEAP,
    REGION_TYPE_STACK
} region_type_t;

#define REGION_TYPE_NAMES { "misc", "code", "exe", "heap", "stack" }
extern const char *region_type_names[];

/* a region obtained from /proc/pid/maps, these are searched for matches */
typedef struct {
    void *start;             /* Start address. Hack: If HAVE_PROCMEM is defined, this is actually an (unsigned long) offset into /proc/{pid}/mem */
    unsigned long size;              /* size */
    region_type_t type;
    unsigned long load_addr;         /* e.g. load address of the executable */
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

bool sm_readmaps(pid_t target, list_t *regions, region_scan_level_t region_scan_level);

#endif /* MAPS_H */
