/*
 $Id: main.c,v 1.25 2007-06-07 15:16:46+01 taviso Exp taviso $

 Copyright (C) 2006,2007 Tavis Ormandy <taviso@sdf.lonestar.org>
 
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
#include <stdlib.h>
#include <getopt.h>             /*lint -e537 */
#include <signal.h>
#include <stdbool.h>

#include "scanmem.h"
#include "commands.h"
#include "handlers.h"

static void printhelp(void);
static void sighandler(int n);

/* global settings */
globals_t globals = {
    0,                          /* exit flag */
    0,                          /* pid target */
    NULL,                       /* matches */
    0,                          /* match count */
    NULL,                       /* regions */
    NULL,                       /* commands */
    /* options */
    {
        1,                      /* alignment */
        1,                      /* debug */
        0,                      /* backend */
    }
};

int main(int argc, char **argv)
{
    char *end;
    int optindex, ret = EXIT_SUCCESS;
    globals_t *vars = &globals;
    struct option longopts[] = {
        {"pid", 1, NULL, 'p'},  /* target pid */
        {"version", 0, NULL, 'v'},      /* print version */
        {"help", 0, NULL, 'h'}, /* print help summary */
        {"backend", 0, NULL, 'b'}, /* run as backend */
        {NULL, 0, NULL, 0},
    };

    /* process command line */
    while (true) {
        switch (getopt_long(argc, argv, "vhbp:", longopts, &optindex)) {
        case 'p':
            vars->target = (pid_t) strtoul(optarg, &end, 0);

            /* check if that parsed correctly */
            if (*end != '\0' || *optarg == '\0' || vars->target == 0) {
                fprintf(stderr, "error: invalid pid specified.\n");
                return EXIT_FAILURE;
            }
            break;
        case 'v':
            printversion(stdout);
            return EXIT_SUCCESS;
        case 'h':
            printhelp();
            return EXIT_SUCCESS;
        case 'b':
            vars->options.backend = 1;
            break;
        case -1:
            goto done;
        default:
            printhelp();
            return EXIT_FAILURE;
        }
    }

  done:

    /* parse any pid specified after arguments */
    if (optind <= argc && argv[optind]) {
        vars->target = (pid_t) strtoul(argv[optind], &end, 0);

        /* check if that parsed correctly */
        if (*end != '\0' || argv[optind][0] == '\0' || vars->target == 0) {
            fprintf(stderr, "error: invalid pid specified.\n");
            return EXIT_FAILURE;
        }
    }

    /* before attaching to target, install signal handler to detach on error */
    (void) signal(SIGHUP, sighandler);
    (void) signal(SIGINT, sighandler);
    (void) signal(SIGSEGV, sighandler);
    (void) signal(SIGABRT, sighandler);
    (void) signal(SIGILL, sighandler);
    (void) signal(SIGFPE, sighandler);
    (void) signal(SIGTERM, sighandler);

    /* linked list of commands, and function pointers to their handlers */
    if ((vars->commands = l_init()) == NULL) {
        fprintf(stderr, "error: sorry, there was a memory allocation error.\n");
        ret = EXIT_FAILURE;
        goto end;
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
    registercommand("=", handler__decinc, vars->commands, EQU_SHRTDOC,
                    EQU_LONGDOC);
    registercommand("<", handler__decinc, vars->commands, DEC_SHRTDOC,
                    DEC_LONGDOC);
    registercommand(">", handler__decinc, vars->commands, INC_SHRTDOC,
                    INC_LONGDOC);
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
    registercommand("write", handler__write, vars->commands, WRITE_SHRTDOC, WRITE_LONGDOC);

    /* commands beginning with __ have special meaning */
    registercommand("__eof", handler__eof, vars->commands, NULL, NULL);

    /* special value NULL means no other matches */
    registercommand(NULL, handler__default, vars->commands, DEFAULT_SHRTDOC,
                    DEFAULT_LONGDOC);

    printversion(stdout);

    /* this will initialise matches and regions */
    if (execcommand(vars, "reset") == false) {
        vars->target = 0;
    }

    /* check if there is a target already specified */
    if (vars->target == 0) {
        fprintf(stdout,
                "Enter the pid of the process to search using the \"pid\" command.\n");
        fprintf(stdout, "Enter \"help\" for other commands.\n");
    } else {
        fprintf(stdout,
                "Please enter current value, or \"help\" for other commands.\n");
    }

    /* main loop, read input and process commands */
    while (!vars->exit) {
        char *line;

        /* reads in a commandline from the user, and returns a pointer to it in *line */
        if (getcommand(vars, &line) == false) {
            fprintf(stderr, "error: failed to read in a command.\n");
            ret = EXIT_FAILURE;
            break;
        }

        /* execcommand returning failure isnt fatal, just the a command couldnt complete. */
        if (execcommand(vars, line) == false) {
            if (vars->target == 0) {
                fprintf(stdout,
                        "Enter the pid of the process to search using the \"pid\" command.\n");
                fprintf(stdout, "Enter \"help\" for other commands.\n");
            } else {
                fprintf(stderr,
                        "Please enter current value, or \"help\" for other commands.\n");
            }
        }

        free(line);
    }

  end:

    /* now free any allocated memory used */
    l_destroy(vars->regions);
    l_destroy(vars->commands);

    /* attempt to detach just in case */
    (void) detach(vars->target);

    return ret;
}

void sighandler(int n)
{
    fprintf(stderr, "\nKilled by signal %d.\n", n);

    if (globals.target) {
        (void) detach(globals.target);
    }

    exit(EXIT_FAILURE);
}

/* print quick usage message to stdout */
void printhelp(void)
{
    printversion(stdout);

    fprintf(stdout, "Usage: scanmem [OPTION]... [PID]\n"
            "Interactively locate and modify variables in an executing process.\n"
            "\n"
            "-p, --pid=pid\t\tset the target process pid\n"
            "-b, --backend\t\trun as backend, used by frontend\n"
            "-h, --help\t\tprint this message\n"
            "-v, --version\t\tprint version information\n"
            "\n"
            "scanmem is an interactive debugging utility, enter `help` at the prompt\n"
            "for further assistance.\n"
            "\n" "Report bugs to <%s>.\n", PACKAGE_BUGREPORT);
    return;
}
