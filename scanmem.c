/*
    Provide interfaces for front-ends.

    Copyright (C) 2006,2007,2009 Tavis Ormandy <taviso@sdf.lonestar.org>
    Copyright (C) 2009           Eli Dupree <elidupree@charter.net>
    Copyright (C) 2009,2010      WANG Lu <coolwanglu@gmail.com>
 
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
        0,                      /* reverse_endianness */
    }
};

/* signal handler - Use async-signal-safe functions ONLY! */
static void sighandler(int n)
{
    const char err_msg[] = "error: \nKilled by signal ";
    const char msg_end[] = ".\n";
    char num_str[4] = {0};
    ssize_t num_size;
    ssize_t wbytes;

    wbytes = write(STDERR_FILENO, err_msg, sizeof(err_msg) - 1);
    if (wbytes != sizeof(err_msg) - 1)
        goto out;
    /* manual int to str conversion */
    if (n < 10) {
        num_str[0] = (char) (0x30 + n);
        num_size = 1;
    } else if (n >= 100) {
        goto out;
    } else {
        num_str[0] = (char) (0x30 + n / 10);
        num_str[1] = (char) (0x30 + n % 10);
        num_size = 2;
    }
    wbytes = write(STDERR_FILENO, num_str, num_size);
    if (wbytes != num_size)
        goto out;
    wbytes = write(STDERR_FILENO, msg_end, sizeof(msg_end) - 1);
    if (wbytes != sizeof(msg_end) - 1)
        goto out;
out:
    _exit(EXIT_FAILURE);   /* also detaches from tracee */
}


bool init(void)
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
void set_backend(void)
{
    globals.options.backend = 1;
}

void backend_exec_cmd(const char * commandline)
{
    execcommand(&globals, commandline);
    fflush(stdout);
    fflush(stderr);
}

long get_num_matches(void)
{
    return globals.num_matches;
}

const char * get_version(void)
{
    return PACKAGE_VERSION;
}

double get_scan_progress(void)
{
    return globals.scan_progress;
}

void reset_scan_progress(void)
{
    globals.scan_progress = 0;
}
