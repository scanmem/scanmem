/*
 *
 * scanmem.c : interfaces 

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

#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <stdlib.h>
#include <signal.h>
#include <stdbool.h>

#include "scanmem.h"
#include "commands.h"
#include "handlers.h"
#include "show_message.h"

/* global settings */
globals_t globals = {
    0,                          /* exit flag */
    0,                          /* pid target */
    NULL,                       /* matches */
    0,                          /* match count */
    0,                          /* scan progress */
    NULL,                       /* regions */
    NULL,                       /* commands */
    NULL,                       /* current_cmdline */
    /* options */
    {
        1,                      /* alignment */
        0,                      /* debug */
        0,                      /* backend */
        ANYINTEGER,             /* scan_data_type */
        REGION_HEAP_STACK_EXECUTABLE_BSS, /* region_detail_level */ 
        0,                      /* detect_reverse_change */
        1,                      /* dump_with_ascii */
    }
};

static void sighandler(int n)
{
    show_error("\nKilled by signal %d.\n", n);

    if (globals.target) {
        (void) detach(globals.target);
    }

    exit(EXIT_FAILURE);
}


bool init()
{
    globals_t *vars = &globals;

    /* before attaching to target, install signal handler to detach on error */
    if (vars->options.debug == 0) /* in debug mode, let it crash and see the core dump */
    {
        (void) signal(SIGHUP, sighandler);
        (void) signal(SIGINT, sighandler);
        (void) signal(SIGSEGV, sighandler);
        (void) signal(SIGABRT, sighandler);
        (void) signal(SIGILL, sighandler);
        (void) signal(SIGFPE, sighandler);
        (void) signal(SIGTERM, sighandler);
    }

    /* linked list of commands, and function pointers to their handlers */
    if ((vars->commands = l_init()) == NULL) {
        show_error("sorry, there was a memory allocation error.\n");
        return false;
    }

    /* NULL shortdoc means dont display this command in `help` listing */
    registercommand("set", handler__set, vars->commands, SET_SHRTDOC,
                    SET_LONGDOC);
    registercommand("list", handler__list, vars->commands, LIST_SHRTDOC,
                    LIST_LONGDOC);
    registercommand("delete", handler__delete, vars->commands, DELETE_SHRTDOC,
                    DELETE_LONGDOC);
    registercommand("reset", handler__reset, vars->commands, RESET_SHRTDOC,
                    RESET_LONGDOC);
    registercommand("pid", handler__pid, vars->commands, PID_SHRTDOC,
                    PID_LONGDOC);
    registercommand("snapshot", handler__snapshot, vars->commands,
                    SNAPSHOT_SHRTDOC, SNAPSHOT_LONGDOC);
    registercommand("dregion", handler__dregion, vars->commands,
                    DREGION_SHRTDOC, DREGION_LONGDOC);
    registercommand("dregions", handler__dregion, vars->commands,
                    NULL, DREGION_LONGDOC);
    registercommand("lregions", handler__lregions, vars->commands,
                    LREGIONS_SHRTDOC, LREGIONS_LONGDOC);
    registercommand("version", handler__version, vars->commands,
                    VERSION_SHRTDOC, VERSION_LONGDOC);
    registercommand("=", handler__decinc, vars->commands, NOTCHANGED_SHRTDOC,
                    NOTCHANGED_LONGDOC);
    registercommand("!=", handler__decinc, vars->commands, CHANGED_SHRTDOC,
                    CHANGED_LONGDOC);
    registercommand("<", handler__decinc, vars->commands, LESSTHAN_SHRTDOC,
                    LESSTHAN_LONGDOC);
    registercommand(">", handler__decinc, vars->commands, GREATERTHAN_SHRTDOC,
                    GREATERTHAN_LONGDOC);
    registercommand("+", handler__decinc, vars->commands, INCREASED_SHRTDOC,
                    INCREASED_LONGDOC);
    registercommand("-", handler__decinc, vars->commands, DECREASED_SHRTDOC,
                    DECREASED_LONGDOC);
    registercommand("\"", handler__string, vars->commands, STRING_SHRTDOC,
                    STRING_LONGDOC);
    registercommand("update", handler__update, vars->commands, UPDATE_SHRTDOC,
                    UPDATE_LONGDOC);
    registercommand("exit", handler__exit, vars->commands, EXIT_SHRTDOC,
                    EXIT_LONGDOC);
    registercommand("quit", handler__exit, vars->commands, NULL, EXIT_LONGDOC);
    registercommand("q", handler__exit, vars->commands, NULL, EXIT_LONGDOC);
    registercommand("help", handler__help, vars->commands, HELP_SHRTDOC,
                    HELP_LONGDOC);
    registercommand("shell", handler__shell, vars->commands, SHELL_SHRTDOC, SHELL_LONGDOC);
    registercommand("watch", handler__watch, vars->commands, WATCH_SHRTDOC,
                    WATCH_LONGDOC);
    registercommand("show", handler__show, vars->commands, SHOW_SHRTDOC, SHOW_LONGDOC);
    registercommand("dump", handler__dump, vars->commands, DUMP_SHRTDOC, DUMP_LONGDOC);
    registercommand("write", handler__write, vars->commands, WRITE_SHRTDOC, WRITE_LONGDOC);
    registercommand("option", handler__option, vars->commands, OPTION_SHRTDOC, OPTION_LONGDOC);

    /* commands beginning with __ have special meaning */
    registercommand("__eof", handler__eof, vars->commands, NULL, NULL);

    /* special value NULL means no other matches */
    registercommand(NULL, handler__default, vars->commands, DEFAULT_SHRTDOC,
                    DEFAULT_LONGDOC);

    return true;
}

/* for front-ends */
void set_backend()
{
    globals.options.backend = 1;
}

void backend_exec_cmd(const char * commandline)
{
    execcommand(&globals, commandline);
    fflush(stdout);
    fflush(stderr);
}

long get_num_matches()
{
    return globals.num_matches;
}

const char * get_version()
{
    return PACKAGE_VERSION;
}

double get_scan_progress()
{
    return globals.scan_progress;
}

void reset_scan_progress()
{
    globals.scan_progress = 0;
}
