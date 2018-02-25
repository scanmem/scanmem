/*
    Provide interfaces for front-ends.

    Copyright (C) 2006,2007,2009 Tavis Ormandy <taviso@sdf.lonestar.org>
    Copyright (C) 2009           Eli Dupree <elidupree@charter.net>
    Copyright (C) 2009-2013      WANG Lu <coolwanglu@gmail.com>
    Copyright (C) 2016           Sebastian Parschauer <s.parschauer@gmx.de>

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

#ifndef SCANMEM_H
#define SCANMEM_H

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/types.h>

#include "scanroutines.h"
#include "list.h"
#include "maps.h"
#include "value.h"
#include "targetmem.h"

#if HAVE_SECURE_GETENV
#define util_getenv secure_getenv
#else
#define util_getenv getenv
#endif


/* global settings */
typedef struct {
    unsigned exit:1;
    pid_t target;
    matches_and_old_values_array *matches;
    unsigned long num_matches;
    double scan_progress;
    volatile bool stop_flag;
    list_t *regions;
    list_t *commands;              /* command handlers */
    const char *current_cmdline;   /* the command being executed */
    void (*printversion)(FILE *outfd);
    struct {
        unsigned short alignment;
        unsigned short debug;
        unsigned short backend;    /* if 1, scanmem will work as a backend and
                                      output will be more machine-readable */

        /* options that can be changed during runtime */
        scan_data_type_t scan_data_type;
        region_scan_level_t region_scan_level;
        unsigned short dump_with_ascii;
        unsigned short reverse_endianness;
    } options;
} globals_t;

/* global settings */
extern globals_t sm_globals;

bool sm_init(void);
void sm_cleanup(void);
void sm_printversion(FILE *outfd);
void sm_set_backend(void);
void sm_backend_exec_cmd(const char *commandline);
unsigned long sm_get_num_matches(void);
const char *sm_get_version(void);
double sm_get_scan_progress(void);
void sm_set_stop_flag(bool stop_flag);

/* ptrace.c */
bool sm_detach(pid_t target);
bool sm_setaddr(pid_t target, void *addr, const value_t *to);
bool sm_checkmatches(globals_t *vars, scan_match_type_t match_type,
                     const uservalue_t *uservalue);
bool sm_searchregions(globals_t *vars, scan_match_type_t match_type,
                      const uservalue_t *uservalue);
bool sm_peekdata(const void *addr, uint16_t length, const mem64_t **result_ptr, size_t *memlength);
bool sm_attach(pid_t target);
bool sm_read_array(pid_t target, const void *addr, void *buf, size_t len);
bool sm_write_array(pid_t target, void *addr, const void *data, size_t len);

#endif /* SCANMEM_H */
