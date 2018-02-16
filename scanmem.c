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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
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


void sm_printversion(FILE *outfd)
{
    fprintf(outfd, "libscanmem version %s\n", PACKAGE_VERSION);
}

/* global settings */
globals_t sm_globals = {
    0,                          /* exit flag */
    0,                          /* pid target */
    NULL,                       /* matches */
    0,                          /* match count */
    0,                          /* scan progress */
    false,                      /* stop flag */
    NULL,                       /* regions */
    NULL,                       /* commands */
    NULL,                       /* current_cmdline */
    sm_printversion,            /* printversion() pointer */
    /* options */
    {
        1,                      /* alignment */
        0,                      /* debug */
        0,                      /* backend */
        ANYINTEGER,             /* scan_data_type */
        REGION_HEAP_STACK_EXECUTABLE_BSS, /* region_detail_level */ 
        1,                      /* dump_with_ascii */
        0,                      /* reverse_endianness */
    }
};

/* signal handler - use async-signal safe functions ONLY! */
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


bool sm_init(void)
{
    globals_t *vars = &sm_globals;

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

    /* linked list of commands and function pointers to their handlers */
    if ((vars->commands = l_init()) == NULL) {
        show_error("sorry, there was a memory allocation error.\n");
        return false;
    }

    /* NULL shortdoc means don't display this command in `help` listing */
    sm_registercommand("set", handler__set, vars->commands, SET_SHRTDOC,
                       SET_LONGDOC, NULL);
    sm_registercommand("list", handler__list, vars->commands, LIST_SHRTDOC,
                       LIST_LONGDOC, NULL);
    sm_registercommand("delete", handler__delete, vars->commands, DELETE_SHRTDOC,
                       DELETE_LONGDOC, NULL);
    sm_registercommand("reset", handler__reset, vars->commands, RESET_SHRTDOC,
                       RESET_LONGDOC, NULL);
    sm_registercommand("pid", handler__pid, vars->commands, PID_SHRTDOC,
                       PID_LONGDOC, NULL);
    sm_registercommand("snapshot", handler__snapshot, vars->commands,
                       SNAPSHOT_SHRTDOC, SNAPSHOT_LONGDOC, NULL);
    sm_registercommand("dregion", handler__dregion, vars->commands,
                       DREGION_SHRTDOC, DREGION_LONGDOC, NULL);
    sm_registercommand("dregions", handler__dregion, vars->commands,
                       NULL, DREGION_LONGDOC, NULL);
    sm_registercommand("lregions", handler__lregions, vars->commands,
                       LREGIONS_SHRTDOC, LREGIONS_LONGDOC, NULL);
    sm_registercommand("version", handler__version, vars->commands,
                       VERSION_SHRTDOC, VERSION_LONGDOC, NULL);
    sm_registercommand("=", handler__operators, vars->commands, NOTCHANGED_SHRTDOC,
                       NOTCHANGED_LONGDOC, NULL);
    sm_registercommand("!=", handler__operators, vars->commands, CHANGED_SHRTDOC,
                       CHANGED_LONGDOC, NULL);
    sm_registercommand("<", handler__operators, vars->commands, LESSTHAN_SHRTDOC,
                       LESSTHAN_LONGDOC, NULL);
    sm_registercommand(">", handler__operators, vars->commands, GREATERTHAN_SHRTDOC,
                       GREATERTHAN_LONGDOC, NULL);
    sm_registercommand("+", handler__operators, vars->commands, INCREASED_SHRTDOC,
                       INCREASED_LONGDOC, NULL);
    sm_registercommand("-", handler__operators, vars->commands, DECREASED_SHRTDOC,
                       DECREASED_LONGDOC, NULL);
    sm_registercommand("\"", handler__string, vars->commands, STRING_SHRTDOC,
                       STRING_LONGDOC, NULL);
    sm_registercommand("update", handler__update, vars->commands, UPDATE_SHRTDOC,
                       UPDATE_LONGDOC, NULL);
    sm_registercommand("exit", handler__exit, vars->commands, EXIT_SHRTDOC,
                       EXIT_LONGDOC, NULL);
    sm_registercommand("quit", handler__exit, vars->commands, NULL,
                       EXIT_LONGDOC, NULL);
    sm_registercommand("q", handler__exit, vars->commands, NULL,
                       EXIT_LONGDOC, NULL);
    sm_registercommand("help", handler__help, vars->commands, HELP_SHRTDOC,
                       HELP_LONGDOC, HELP_COMPLETE);
    sm_registercommand("shell", handler__shell, vars->commands, SHELL_SHRTDOC,
                       SHELL_LONGDOC, NULL);
    sm_registercommand("!", handler__shell, vars->commands, NULL, SHELL_LONGDOC,
                       NULL);
    sm_registercommand("watch", handler__watch, vars->commands, WATCH_SHRTDOC,
                       WATCH_LONGDOC, NULL);
    sm_registercommand("show", handler__show, vars->commands, SHOW_SHRTDOC,
                       SHOW_LONGDOC, SHOW_COMPLETE);
    sm_registercommand("dump", handler__dump, vars->commands, DUMP_SHRTDOC,
                       DUMP_LONGDOC, NULL);
    sm_registercommand("write", handler__write, vars->commands, WRITE_SHRTDOC,
                       WRITE_LONGDOC, WRITE_COMPLETE);
    sm_registercommand("option", handler__option, vars->commands, OPTION_SHRTDOC,
                       OPTION_LONGDOC, OPTION_COMPLETE);

    /* commands beginning with __ have special meaning */
    sm_registercommand("__eof", handler__eof, vars->commands, NULL, NULL, NULL);

    /* special value NULL means no other matches */
    sm_registercommand(NULL, handler__default, vars->commands, DEFAULT_SHRTDOC,
                       DEFAULT_LONGDOC, NULL);

    return true;
}

void sm_cleanup(void)
{
    /* free any allocated memory used */
    l_destroy(sm_globals.regions);
    if (sm_globals.commands)
        sm_free_all_completions(sm_globals.commands);
    l_destroy(sm_globals.commands);

    /* free matches array */
    if (sm_globals.matches)
        free(sm_globals.matches);

    /* attempt to detach just in case */
    sm_detach(sm_globals.target);
}

/* for front-ends */
void sm_set_backend(void)
{
    sm_globals.options.backend = 1;
}

void sm_backend_exec_cmd(const char *commandline)
{
    sm_execcommand(&sm_globals, commandline);
    fflush(stdout);
    fflush(stderr);
}

unsigned long sm_get_num_matches(void)
{
    return sm_globals.num_matches;
}

const char *sm_get_version(void)
{
    return PACKAGE_VERSION;
}

double sm_get_scan_progress(void)
{
    return sm_globals.scan_progress;
}

void sm_set_stop_flag(bool stop_flag)
{
    sm_globals.stop_flag = stop_flag;
}
