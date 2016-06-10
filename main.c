/*
    Main function, option parsing and help text.

    Copyright (C) 2006,2007,2009 Tavis Ormandy <taviso@sdf.lonestar.org>
    Copyright (C) 2009           Eli Dupree <elidupree@charter.net>
    Copyright (C) 2009,2010      WANG Lu <coolwanglu@gmail.com>
 
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
# define _GNU_SOURCE
#endif

#include "config.h"

#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <stdlib.h>
#include <getopt.h>             /*lint -e537 */
#include <stdbool.h>

#include "scanmem.h"
#include "commands.h"
#include "show_message.h"

/* print quick usage message to stderr */
static void printhelp()
{
    printversion(stderr);

    show_user("Usage: scanmem [OPTION]... [PID]\n"
            "Interactively locate and modify variables in an executing process.\n"
            "\n"
            "-p, --pid=pid\t\tset the target process pid\n"
            "-h, --help\t\tprint this message\n"
            "-v, --version\t\tprint version information\n"
            "-d, --debug\t\tenable debug mode\n"
            "\n"
            "scanmem is an interactive debugging utility, enter `help` at the prompt\n"
            "for further assistance.\n"
            "\n" "Report bugs to <%s>.\n", PACKAGE_BUGREPORT);
    return;
}

static void parse_parameter(int argc, char ** argv)
{
    struct option longopts[] = {
        {"pid", 1, NULL, 'p'},  /* target pid */
        {"version", 0, NULL, 'v'},      /* print version */
        {"help", 0, NULL, 'h'}, /* print help summary */
        {"debug", 0, NULL, 'd'}, /* enable debug mode */
        {NULL, 0, NULL, 0},
    };

    char *end;
    int optindex;
    bool done = false;
    /* process command line */
    while (!done) {
        switch (getopt_long(argc, argv, "vhbdp:", longopts, &optindex)) {
            case 'p':
                globals.target = (pid_t) strtoul(optarg, &end, 0);

                /* check if that parsed correctly */
                if (*end != '\0' || *optarg == '\0' || globals.target == 0) {
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
                globals.options.debug = 1;
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
        globals.target = (pid_t) strtoul(argv[optind], &end, 0);

        /* check if that parsed correctly */
        if (*end != '\0' || argv[optind][0] == '\0' || globals.target == 0) {
            show_error("invalid pid specified.\n");
            exit(EXIT_FAILURE);
        }
    }
}

int main(int argc, char **argv)
{
    parse_parameter(argc, argv);

    if (getuid() != 0)
    {
        show_warn("*** YOU ARE NOT RUNNING scanmem AS ROOT, IT MAY NOT WORK WELL. ***\n\n");
    }

    int ret = EXIT_SUCCESS;
    globals_t *vars = &globals;

    printversion(stderr);

    if (!init()) {
        show_error("Initialization failed.\n");
        ret = EXIT_FAILURE;
        goto end;
    }

    /* this will initialise matches and regions */
    if (execcommand(vars, "reset") == false) {
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

        /* reads in a commandline from the user, and returns a pointer to it in *line */
        if (getcommand(vars, &line) == false) {
            show_error("failed to read in a command.\n");
            ret = EXIT_FAILURE;
            break;
        }

        /* execcommand returning failure isnt fatal, just the a command couldnt complete. */
        if (execcommand(vars, line) == false) {
            if (vars->target == 0) {
                show_user("Enter the pid of the process to search using the \"pid\" command.\n");
                show_user("Enter \"help\" for other commands.\n");
            } else {
                show_user("Please enter current value, or \"help\" for other commands.\n");
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
    (void) detach(vars->target);

    return ret;
}
