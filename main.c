/*
    Scanmem main function, option parsing and help text.

    Copyright (C) 2006,2007,2009 Tavis Ormandy <taviso@sdf.lonestar.org>
    Copyright (C) 2009           Eli Dupree <elidupree@charter.net>
    Copyright (C) 2009-2013      WANG Lu <coolwanglu@gmail.com>
    Copyright (C) 2016           Sebastian Parschauer <s.parschauer@gmx.de>

    This file is part of scanmem.
 
    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "config.h"

#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <stdlib.h>
#include <getopt.h>
#include <stdbool.h>

#include "scanmem.h"
#include "commands.h"
#include "show_message.h"


static const char copy_text[] =
"Copyright (C) 2006-2009 Tavis Ormandy\n"
"Copyright (C) 2009,2010 Tavis Ormandy, Eli Dupree, WANG Lu\n"
"Copyright (C) 2011-2015 WANG Lu\n"
"Copyright (C) 2015-2016 Sebastian Parschauer\n"
"scanmem comes with ABSOLUTELY NO WARRANTY; for details type `show warranty'.\n"
"This is free software, and you are welcome to redistribute it\n"
"under certain conditions; type `show copying' for details.\n\n";

/* print scanmem and libscanmem version */
static void printversion(FILE *outfd)
{
    fprintf(outfd, "scanmem version %s\n", PACKAGE_VERSION);
    sm_printversion(outfd);
}

/* print scanmem version and copyright info */
static void printcopyright(FILE *outfd)
{
    printversion(outfd);
    fprintf(outfd, "\n%s", copy_text);
}

static const char help_text[] =
"Usage: scanmem [OPTION]... [PID]\n"
"Interactively locate and modify variables in an executing process.\n"
"\n"
"-p, --pid=pid\t\tset the target process pid\n"
"-h, --help\t\tprint this message\n"
"-v, --version\t\tprint version information\n"
"\n"
"scanmem is an interactive debugging utility, enter `help` at the prompt\n"
"for further assistance.\n"
"\n"
"Report bugs to <" PACKAGE_BUGREPORT ">.\n";

/* print quick usage message to stderr */
static void printhelp(void)
{
    printcopyright(stderr);

    show_user("%s", help_text);
    return;
}

static void parse_parameter(int argc, char **argv)
{
    struct option longopts[] = {
        {"pid",     1, NULL, 'p'},      /* target pid */
        {"version", 0, NULL, 'v'},      /* print version */
        {"help",    0, NULL, 'h'},      /* print help summary */
        {"debug",   0, NULL, 'd'},      /* enable debug mode */
        {NULL, 0, NULL, 0},
    };
    char *end;
    int optindex;
    bool done = false;
    globals_t *vars = &sm_globals;

    /* process command line */
    while (!done) {
        switch (getopt_long(argc, argv, "vhdp:", longopts, &optindex)) {
            case 'p':
                vars->target = (pid_t) strtoul(optarg, &end, 0);

                /* check if that parsed correctly */
                if (*end != '\0' || *optarg == '\0' || vars->target == 0) {
                    show_error("invalid pid specified.\n");
                    exit(EXIT_FAILURE);
                }
                break;
            case 'v':
                printversion(stderr);
                exit(EXIT_FAILURE);
            case 'h':
                printhelp();
                exit(EXIT_FAILURE);
            case 'd':
                vars->options.debug = 1;
                break;
            case -1:
                done = true;
                break;
            default:
                printhelp();
                exit(EXIT_FAILURE);
        }
    }
    /* parse any pid specified after arguments */
    if (optind <= argc && argv[optind]) {
        vars->target = (pid_t) strtoul(argv[optind], &end, 0);

        /* check if that parsed correctly */
        if (*end != '\0' || argv[optind][0] == '\0' || vars->target == 0) {
            show_error("invalid pid specified.\n");
            exit(EXIT_FAILURE);
        }
    }
}

int main(int argc, char **argv)
{
    parse_parameter(argc, argv);

    int ret = EXIT_SUCCESS;
    globals_t *vars = &sm_globals;

    printcopyright(stderr);
    vars->printversion = printversion;

    if (!sm_init()) {
        show_error("Initialization failed.\n");
        ret = EXIT_FAILURE;
        goto end;
    }

    if (getuid() != 0) {
        show_warn("Run scanmem as root if memory regions are missing. "
                  "See scanmem man page.\n\n");
    }

    /* this will initialize matches and regions */
    if (sm_execcommand(vars, "reset") == false) {
        vars->target = 0;
    }

    /* check if there is a target already specified */
    if (vars->target == 0) {
        show_user("Enter the pid of the process to search using the \"pid\" command.\n");
        show_user("Enter \"help\" for other commands.\n");
    } else {
        show_user("Please enter current value, or \"help\" for other commands.\n");
    }

    /* main loop, read input and process commands */
    while (!vars->exit) {
        char *line;

        /* reads in a commandline from the user and returns a pointer to it in *line */
        if (sm_getcommand(vars, &line) == false) {
            show_error("failed to read in a command.\n");
            ret = EXIT_FAILURE;
            break;
        }

        /* sm_execcommand() returning failure is not fatal, it just means the command could not complete. */
        if (sm_execcommand(vars, line) == false) {
            if (vars->target == 0) {
                show_user("Enter the pid of the process to search using the \"pid\" command.\n");
                show_user("Enter \"help\" for other commands.\n");
            } else {
                show_user("Please enter current value or \"help\" for other commands.\n");
            }
        }

        free(line);

        fflush(stdout);
        fflush(stderr);
    }

end:

    /* now free any allocated memory used */
    l_destroy(vars->regions);
    l_destroy(vars->commands);

    /* attempt to detach just in case */
    (void) sm_detach(vars->target);

    return ret;
}
