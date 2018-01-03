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

#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif

#include <stdio.h>
#include <sys/types.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <alloca.h>
#include <stdbool.h>
#include <unistd.h>

#include "list.h"
#include "maps.h"
#include "getline.h"
#include "show_message.h"

const char *region_type_names[] = REGION_TYPE_NAMES;

bool sm_readmaps(pid_t target, list_t *regions, region_scan_level_t region_scan_level)
{
    FILE *maps;
    char name[128], *line = NULL;
    char exelink[128];
    size_t len = 0;
    unsigned int code_regions = 0, exe_regions = 0;
    unsigned long prev_end = 0, load_addr = 0, exe_load = 0;
    bool is_exe = false;

#define MAX_LINKBUF_SIZE 256
    char linkbuf[MAX_LINKBUF_SIZE], *exename = linkbuf;
    int linkbuf_size;
    char binname[MAX_LINKBUF_SIZE];

    /* check if target is valid */
    if (target == 0)
        return false;

    /* construct the maps filename */
    snprintf(name, sizeof(name), "/proc/%u/maps", target);

    /* attempt to open the maps file */
    if ((maps = fopen(name, "r")) == NULL) {
        show_error("failed to open maps file %s.\n", name);
        return false;
    }

    show_info("maps file located at %s opened.\n", name);

    /* get executable name */
    snprintf(exelink, sizeof(exelink), "/proc/%u/exe", target);
    linkbuf_size = readlink(exelink, exename, MAX_LINKBUF_SIZE - 1);
    if (linkbuf_size > 0)
    {
        exename[linkbuf_size] = 0;
    } else {
        /* readlink may fail for special processes, just treat as empty in
           order not to miss those regions */
        exename[0] = 0;
    }

    /* read every line of the maps file */
    while (getline(&line, &len, maps) != -1) {
        unsigned long start, end;
        region_t *map = NULL;
        char read, write, exec, cow, *filename;
        int offset, dev_major, dev_minor, inode;
        region_type_t type = REGION_TYPE_MISC;

        /* slight overallocation */
        if ((filename = alloca(len)) == NULL) {
            show_error("failed to allocate %lu bytes for filename.\n", (unsigned long)len);
            goto error;
        }

        /* initialise to zero */
        memset(filename, '\0', len);

        /* parse each line */
        if (sscanf(line, "%lx-%lx %c%c%c%c %x %x:%x %u %[^\n]", &start, &end, &read,
                &write, &exec, &cow, &offset, &dev_major, &dev_minor, &inode, filename) >= 6) {
            /*
             * get the load address for regions of the same ELF file
             *
             * When the ELF loader loads an executable or a library into
             * memory, there is one region per ELF segment created:
             * .text (r-x), .rodata (r--), .data (rw-) and .bss (rw-). The
             * 'x' permission of .text is used to detect the load address
             * (region start) and the end of the ELF file in memory. All
             * these regions have the same filename. The only exception
             * is the .bss region. Its filename is empty and it is
             * consecutive with the .data region. But the regions .bss and
             * .rodata may not be present with some ELF files. This is why
             * we can't rely on other regions to be consecutive in memory.
             * There should never be more than these four regions.
             * The data regions use their variables relative to the load
             * address. So determining it makes sense as we can get the
             * variable address used within the ELF file with it.
             * But for the executable there is the special case that there
             * is a gap between .text and .rodata. Other regions might be
             * loaded via mmap() to it. So we have to count the number of
             * regions belonging to the exe separately to handle that.
             * References:
             * http://en.wikipedia.org/wiki/Executable_and_Linkable_Format
             * http://wiki.osdev.org/ELF
             * http://lwn.net/Articles/531148/
             */

            /* detect further regions of the same ELF file and its end */
            if (code_regions > 0) {
                if (exec == 'x' || (strncmp(filename, binname,
                  MAX_LINKBUF_SIZE) != 0 && (filename[0] != '\0' ||
                  start != prev_end)) || code_regions >= 4) {
                    code_regions = 0;
                    is_exe = false;
                    /* exe with .text and without .data is impossible */
                    if (exe_regions > 1)
                        exe_regions = 0;
                } else {
                    code_regions++;
                    if (is_exe)
                        exe_regions++;
                }
            }
            if (code_regions == 0) {
                /* detect the first region belonging to an ELF file */
                if (exec == 'x' && filename[0] != '\0') {
                    code_regions++;
                    if (strncmp(filename, exename, MAX_LINKBUF_SIZE) == 0) {
                        exe_regions = 1;
                        exe_load = start;
                        is_exe = true;
                    }
                    strncpy(binname, filename, MAX_LINKBUF_SIZE);
                    binname[MAX_LINKBUF_SIZE - 1] = '\0';  /* just to be sure */
                /* detect the second region of the exe after skipping regions */
                } else if (exe_regions == 1 && filename[0] != '\0' &&
                  strncmp(filename, exename, MAX_LINKBUF_SIZE) == 0) {
                    code_regions = ++exe_regions;
                    load_addr = exe_load;
                    is_exe = true;
                    strncpy(binname, filename, MAX_LINKBUF_SIZE);
                    binname[MAX_LINKBUF_SIZE - 1] = '\0';  /* just to be sure */
                }
                if (exe_regions < 2)
                    load_addr = start;
            }
            prev_end = end;

            /* must have permissions to read and write, and be non-zero size */
            if ((write == 'w') && (read == 'r') && ((end - start) > 0)) {
                bool useful = false;

                /* determine region type */
                if (is_exe)
                    type = REGION_TYPE_EXE;
                else if (code_regions > 0)
                    type = REGION_TYPE_CODE;
                else if (!strcmp(filename, "[heap]"))
                    type = REGION_TYPE_HEAP;
                else if (!strcmp(filename, "[stack]"))
                    type = REGION_TYPE_STACK;

                /* determine if this region is useful */
                switch (region_scan_level)
                {
                    case REGION_ALL:
                        useful = true;
                        break;
                    case REGION_HEAP_STACK_EXECUTABLE_BSS:
                        if (filename[0] == '\0')
                        {
                            useful = true;
                            break;
                        }
                        /* fall through */
                    case REGION_HEAP_STACK_EXECUTABLE:
                        if (type == REGION_TYPE_HEAP || type == REGION_TYPE_STACK)
                        {
                            useful = true;
                            break;
                        }
                        /* test if the region is mapped to the executable */
                        if (type == REGION_TYPE_EXE ||
                          strncmp(filename, exename, MAX_LINKBUF_SIZE) == 0)
                            useful = true;
                    break;
                }

                if (!useful)
                    continue;

                /* allocate a new region structure */
                if ((map = calloc(1, sizeof(region_t) + strlen(filename))) == NULL) {
                    show_error("failed to allocate memory for region.\n");
                    goto error;
                }

                /* initialize this region */
                map->flags.read = true;
                map->flags.write = true;
                map->start = (void *) start;
                map->size = (unsigned long) (end - start);
                map->type = type;
                map->load_addr = load_addr;

                /* setup other permissions */
                map->flags.exec = (exec == 'x');
                map->flags.shared = (cow == 's');
                map->flags.private = (cow == 'p');

                /* save pathname */
                if (strlen(filename) != 0) {
                    /* the pathname is concatenated with the structure */
                    strcpy(map->filename, filename);
                }

                /* add a unique identifier */
                map->id = regions->size;
                
                /* okay, add this guy to our list */
                if (l_append(regions, regions->tail, map) == -1) {
                    show_error("failed to save region.\n");
                    goto error;
                }
            }
        }
    }

    show_info("%d suitable regions found.\n", regions->size);
    
    /* release memory allocated */
    free(line);
    fclose(maps);

    return true;

error:
    free(line);
    fclose(maps);

    return false;
}
