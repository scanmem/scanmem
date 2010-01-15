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

bool readmaps(pid_t target, list_t * regions)
{
    FILE *maps;
    char name[128], *line = NULL;
    char exename[128];
    size_t len = 0;

#define MAX_LINKBUF_SIZE 256
    char linkbuf[MAX_LINKBUF_SIZE];
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

        /* read every line of the maps file */
        while (getline(&line, &len, maps) != -1) {
            unsigned long start, end;
            region_t *map = NULL;
            char read, write, exec, cow, *filename;
            int offset, dev_major, dev_minor, inode;

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

                /* must have permissions to read and write, and be non-zero size */
                if ((write == 'w') && (read == 'r') && ((end - start) > 0)) {
                    
                    /* determine if this region is useful */
                    bool useful = false;
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
                            if ((!strcmp(filename, "[heap]")) || (!strcmp(filename, "[stack]")))
                            {
                                useful = true;
                                break;
                            }
                            /* test if the region is mapped to the executable */
                            snprintf(exename, sizeof(exename), "/proc/%u/exe", target);
                            if((linkbuf_size = readlink(exename, linkbuf, MAX_LINKBUF_SIZE)) == -1)
                            {
                                show_error("failed to read executable link.\n");
                                goto error;
                            }
                            if (linkbuf_size >= MAX_LINKBUF_SIZE)
                            {
                                show_error("path to the executable is too long.\n");
                                goto error;
                            }
                            linkbuf[linkbuf_size] = 0;
                            if (strcmp(filename, linkbuf) == 0)
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

