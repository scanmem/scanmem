/*
 $Id: maps.c,v 1.16 2007-06-05 01:45:35+01 taviso Exp taviso $

 Copyright (C) 2006,2007,2009 Tavis Ormandy <taviso@sdf.lonestar.org>
 Copyright (C) 2009           Eli Dupree <elidupree@charter.net>
 Copyright (C) 2009,2010      WANG Lu <coolwanglu@gmail.com>

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

#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif

#include "config.h"

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
#include "scanmem.h"
#include "show_message.h"

const char *region_type_names[] = REGION_TYPE_NAMES;

bool readmaps(pid_t target, list_t * regions)
{
    FILE *maps;
    char name[128], *line = NULL;
    char exelink[128];
    size_t len = 0;
    unsigned int code_regions = 0;
    bool is_exe = false;
    unsigned long prev_end = 0, load_addr = 0;

#define MAX_LINKBUF_SIZE 256
    char linkbuf[MAX_LINKBUF_SIZE], *exename = linkbuf;
    int linkbuf_size;

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
        if ((linkbuf_size = readlink(exelink, exename, MAX_LINKBUF_SIZE)) > 0)
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
            if (sscanf(line, "%lx-%lx %c%c%c%c %x %x:%x %u %s", &start, &end, &read,
                    &write, &exec, &cow, &offset, &dev_major, &dev_minor, &inode, filename) >= 6) {

                /* get load address for consecutive code regions (.text, .rodata, .data) */
                if (code_regions > 0) {
                    if (exec == 'x' || (read == 'r' && write == 'w' &&
                      start != prev_end) || code_regions >= 4) {
                        code_regions = 0;
                        is_exe = false;
                    } else {
                        code_regions++;
                    }
                }
                if (code_regions == 0) {
                    if (exec == 'x' && filename[0] != '\0') {
                        code_regions++;
                        if (strncmp(filename, exename, MAX_LINKBUF_SIZE) == 0)
                            is_exe = true;
                    }
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
                    switch (globals.options.region_scan_level)
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

                /* initialise this region */
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
                    if ((map = realloc(map, sizeof(*map) + strlen(filename))) == NULL) {
                        show_error("failed to allocate memory.\n");
                        goto error;
                    }

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

int compare_region_id(const region_t *a, const region_t *b)
{    
    return (int) (a->id - b->id);
}

