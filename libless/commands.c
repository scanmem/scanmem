/*
 $Id: commands.c,v 1.8 2007-06-05 19:58:03+01 taviso Exp taviso $

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
#include <stdlib.h>
#include <getopt.h>
#include <assert.h>
#include <strings.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>

#include <readline/readline.h>

#include "commands.h"
#include "show_message.h"

/*
 * registercommand - add the command and a pointer to its handler to the commands list.
 *
 * so that free(data) works when destroying the list, I just concatenate the string
 * with the command structure. I could have used a static vector of commands, but this
 * way I can add aliases and macros at runtime (planned in future).
 *
 */

/*lint -esym(818,handler) dont declare handler const */
bool registercommand(const char *command, void *handler, list_t * commands,
                     char *shortdoc, char *longdoc)
{
    command_t *data;

    assert(commands != NULL);

    if (command != NULL) {
        if ((data = malloc(sizeof(command_t) + strlen(command) + 1)) == NULL) {
            show_error("sorry, there was a memory allocation problem.\n");
            return false;
        }
        data->command = (char *) data + sizeof(*data);

        /* command points to the extra space allocated after data */
        strcpy(data->command, command);
    } else {
        if ((data = malloc(sizeof(command_t))) == NULL) {
            show_error("sorry, there was a memory allocation problem.\n");
            return false;
        }
        data->command = NULL;
    }

    data->handler = handler;
    data->shortdoc = shortdoc;
    data->longdoc = longdoc;

    /* add new command to list */
    if (l_append(commands, NULL, data) == -1) {
        free(data);
        return false;
    }

    return true;
}

bool execcommand(globals_t * vars, const char *commandline)
{
    unsigned argc;
    char *str = NULL, *tok = NULL;
    char **argv = NULL;
    command_t *err = NULL;
    bool ret = false;
    list_t *commands = vars->commands;
    element_t *np = NULL;

    assert(commandline != NULL);
    assert(commands != NULL);

    vars->current_cmdline = commandline;

    np = commands->head;

    str = tok = strdupa(commandline);

    /* tokenize command line into an argument vector */
    for (argc = 0; tok; argc++, str = NULL) {

        /* make enough size for another pointer (+1 for NULL at end) */
        if ((argv = realloc(argv, (argc + 1) * sizeof(char *))) == NULL) {
            show_error("sorry there was a memory allocation error.\n");
            return false;
        }

        /* insert next token */
        argv[argc] = tok = strtok(str, " \t");
    }

    assert(argc >= 1);
    assert(argv != NULL);

    /* just leading whitespace? */
    if (argv[0] == NULL) {
        free(argv);
        
        /* legal i guess, just dont do anything */
        return true;
    }
    
    /* search commands list for appropriate handler */
    while (np) {
        command_t *command = np->data;

        /* check if this command matches */

        if (command->command == NULL) {
            /* the default handler has a NULL command */
            err = command;
        } else if (strcasecmp(argv[0], command->command) == 0) {

            /* match found, execute handler */
            ret = command->handler(vars, argv, argc - 1);

            free(argv);
            return ret;
        }

        np = np->next;
    }

    /* no match, if there was a default handler found, run it now */
    if (err != NULL) {
        ret = err->handler(vars, argv, argc - 1);
    }

    free(argv);

    return ret;
}
