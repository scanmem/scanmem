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
#include <string.h>

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
        0,                      /* no_ptrace */
        0,                      /* threads, 0 = auto */
        0,                      /* undo_limit, 0 = undo disabled */
    },
    false,                      /* scan_in_progress */
    NULL, 0,                    /* undo stack */
    NULL, 0,                    /* redo stack */
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
                       RESET_LONGDOC, RESET_COMPLETE);
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
    sm_registercommand("^", handler__operators, vars->commands, XOR_SHRTDOC,
                       XOR_LONGDOC, NULL);
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
    sm_registercommand("memdiff", handler__memdiff, vars->commands,
                       MEMDIFF_SHRTDOC, MEMDIFF_LONGDOC, NULL);
    sm_registercommand("show", handler__show, vars->commands, SHOW_SHRTDOC,
                       SHOW_LONGDOC, SHOW_COMPLETE);
    sm_registercommand("dump", handler__dump, vars->commands, DUMP_SHRTDOC,
                       DUMP_LONGDOC, NULL);
    sm_registercommand("read", handler__read, vars->commands, READ_SHRTDOC,
                       READ_LONGDOC, READ_COMPLETE);
    sm_registercommand("write", handler__write, vars->commands, WRITE_SHRTDOC,
                       WRITE_LONGDOC, WRITE_COMPLETE);
    sm_registercommand("undo", handler__undo, vars->commands, UNDO_SHRTDOC,
                       UNDO_LONGDOC, NULL);
    sm_registercommand("redo", handler__redo, vars->commands, REDO_SHRTDOC,
                       REDO_LONGDOC, NULL);
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

    sm_history_clear();

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

/* ---- scan undo/redo ---------------------------------------------------- */

static void snapshot_free(scan_snapshot_t *s)
{
    free(s->matches);
    s->matches = NULL;
    s->num_matches = 0;
}

static bool snapshot_take(globals_t *vars, scan_snapshot_t *out)
{
    out->matches = NULL;
    out->num_matches = vars->num_matches;

    if (vars->matches == NULL)
        return true;    /* nothing matched yet, an empty snapshot is valid */

    out->matches = malloc(vars->matches->bytes_allocated);
    if (out->matches == NULL)
        return false;
    memcpy(out->matches, vars->matches, vars->matches->bytes_allocated);
    return true;
}

/* takes ownership of s->matches */
static void snapshot_restore(globals_t *vars, scan_snapshot_t *s)
{
    free(vars->matches);
    vars->matches = s->matches;
    vars->num_matches = s->num_matches;
    s->matches = NULL;
}

static void stack_clear(scan_snapshot_t **stack, unsigned *count)
{
    while (*count > 0)
        snapshot_free(&(*stack)[--(*count)]);
    free(*stack);
    *stack = NULL;
}

static bool stack_push(scan_snapshot_t **stack, unsigned *count,
                       const scan_snapshot_t *s)
{
    scan_snapshot_t *tmp = realloc(*stack, (*count + 1) * sizeof(**stack));

    if (tmp == NULL)
        return false;
    *stack = tmp;
    (*stack)[(*count)++] = *s;
    return true;
}

/* Called just before a scan narrows the match set, so undo restores what was
   there before it ran. Snapshotting beforehand rather than after is what
   makes the very first scan undoable, back to no matches. */
void sm_history_record(void)
{
    globals_t *vars = &sm_globals;
    unsigned limit = vars->options.undo_limit;
    scan_snapshot_t snap;

    if (limit == 0)
        return;

    if (!snapshot_take(vars, &snap) ||
        !stack_push(&vars->undo_stack, &vars->undo_count, &snap))
    {
        snapshot_free(&snap);
        show_warn("not enough memory to save an undo state, undo will not "
                  "reach past this scan.\n");
        return;
    }

    /* drop the oldest once we are over the limit */
    while (vars->undo_count > limit) {
        snapshot_free(&vars->undo_stack[0]);
        memmove(&vars->undo_stack[0], &vars->undo_stack[1],
                (vars->undo_count - 1) * sizeof(*vars->undo_stack));
        vars->undo_count--;
    }

    /* scanning forward throws away anything that was undone */
    stack_clear(&vars->redo_stack, &vars->redo_count);
}

void sm_history_clear(void)
{
    stack_clear(&sm_globals.undo_stack, &sm_globals.undo_count);
    stack_clear(&sm_globals.redo_stack, &sm_globals.redo_count);
}

/* shared by undo and redo, they differ only in which way the state moves */
static bool history_step(scan_snapshot_t **from, unsigned *from_count,
                         scan_snapshot_t **to, unsigned *to_count,
                         const char *what)
{
    globals_t *vars = &sm_globals;
    scan_snapshot_t cur;

    if (vars->options.undo_limit == 0) {
        show_error("undo is disabled, set `option undo_limit' first.\n");
        return false;
    }
    if (vars->scan_in_progress) {
        show_error("cannot %s while a scan is in progress.\n", what);
        return false;
    }
    if (*from_count == 0) {
        show_error("nothing to %s.\n", what);
        return false;
    }

    /* stash where we are now so the opposite direction can get back */
    if (!snapshot_take(vars, &cur) || !stack_push(to, to_count, &cur)) {
        snapshot_free(&cur);
        show_error("sorry, there was a problem allocating memory.\n");
        return false;
    }

    snapshot_restore(vars, &(*from)[--(*from_count)]);
    show_info("we currently have %ld matches.\n", vars->num_matches);
    return true;
}

bool sm_undo_scan(void)
{
    return history_step(&sm_globals.undo_stack, &sm_globals.undo_count,
                        &sm_globals.redo_stack, &sm_globals.redo_count, "undo");
}

bool sm_redo_scan(void)
{
    return history_step(&sm_globals.redo_stack, &sm_globals.redo_count,
                        &sm_globals.undo_stack, &sm_globals.undo_count, "redo");
}

/* Drop all matches and reread the region list. Exposed so a front end can get
   back to a clean state without going through the command parser.
   Refuses while a scan is running: the scan owns vars->matches, so freeing it
   underneath is a use after free rather than just a lost result. */
bool sm_reset(void)
{
    globals_t *vars = &sm_globals;

    if (vars->scan_in_progress) {
        show_error("cannot reset while a scan is in progress.\n");
        return false;
    }

    /* reset scan progress */
    vars->scan_progress = 0;

    if (vars->matches) { free(vars->matches); vars->matches = NULL; vars->num_matches = 0; }

    sm_history_clear();

    /* refresh list of regions */
    l_destroy(vars->regions);

    /* create a new linked list of regions */
    if ((vars->regions = l_init()) == NULL) {
        show_error("sorry, there was a problem allocating memory.\n");
        return false;
    }

    /* read in maps if a pid is known */
    if (vars->target && sm_readmaps(vars->target, vars->regions,
                                    vars->options.region_scan_level) != true) {
        show_error("sorry, there was a problem getting a list of regions to search.\n");
        show_warn("the pid may be invalid, or you don't have permission.\n");
        vars->target = 0;
        return false;
    }

    return true;
}
