/*
    Registration and general execution of commands.

    Copyright (C) 2006,2007,2009 Tavis Ormandy <taviso@sdf.lonestar.org>
    Copyright (C) 2009           Eli Dupree <elidupree@charter.net>
    Copyright (C) 2009,2010      WANG Lu <coolwanglu@gmail.com>
    Copyright (C) 2018           Sebastian Parschauer <s.parschauer@gmx.de>

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

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <assert.h>
#include <strings.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>

#include "commands.h"
#include "common.h"
#include "show_message.h"

static void free_completions(list_t *list)
{
    element_t *np = list->head;
    completion_t *compl = NULL;

    while (np) {
        compl = np->data;
        np = np->next;
        if (!compl)
            continue;
        if (compl->list) {
            free_completions(compl->list);
            l_destroy(compl->list);
            compl->list = NULL;
        }
        if (compl->word) {
            free(compl->word);
            compl->word = NULL;
        }
    }
}

void sm_free_all_completions(list_t *commands)
{
    element_t *np = commands->head;
    command_t *command = NULL;

    while (np) {
        command = np->data;
        np = np->next;
        if (command && command->completions) {
            free_completions(command->completions);
            l_destroy(command->completions);
        }
    }
}

static bool add_completion(list_t *wlist, char **start, size_t wlen)
{
    char *word = NULL;
    completion_t *compl = NULL;

    word = malloc(wlen + 1);
    if (!word)
        goto err;
    compl = calloc(1, sizeof(completion_t));
    if (!compl)
        goto err_free;
    memcpy(word, *start, wlen);
    word[wlen] = '\0';
    *start += wlen + 1;
    compl->word = word;
    if (l_append(wlist, NULL, compl) == -1)
        goto err_free;

    return true;
err_free:
    if (compl)
        free(compl);
    if (word)
        free(word);
err:
    return false;
}

static list_t *init_subcmdlist(command_t *command, const char *complstr)
{
    char *cpos, *bopos = NULL, *bcpos = NULL;
    char *start = (char *)complstr;
    bool in_sublist = false;
    list_t *wlist = NULL, *list;
    completion_t *compl = NULL;

    wlist = l_init();
    if (!wlist)
        return NULL;
    list = wlist;

    bopos = strchr(start, '{');
    while ((cpos = strchr(start, ','))) {
        if (in_sublist) {
            bcpos = strchr(start, '}');
            if (bcpos && cpos > bcpos) {
                if (cpos != bcpos + 1)
                    goto err;
                cpos = bcpos;
                in_sublist = false;
                bopos = strchr(start, '{');
            }
        } else if (bopos && cpos > bopos) {
            cpos = bopos;
            in_sublist = true;
            bopos = NULL;
        }
        if (!add_completion(list, &start, cpos - start))
            goto err;
        if (list == wlist && in_sublist && list->head->data) {
            compl = list->head->data;
            compl->list = l_init();
            list = compl->list;
        } else if (list != wlist && !in_sublist) {
            list = wlist;
            if (bcpos && cpos && cpos == bcpos)
                start++;
            bcpos = NULL;
        }
    }
    if (in_sublist) {
        bcpos = strchr(start, '}');
        if (!bcpos)
            goto err;
        if (!add_completion(list, &start, strlen(start) - 1))
            goto err;
    } else {
        if (!add_completion(wlist, &start, strlen(start)))
            goto err;
    }

    command->completions = wlist;
    return wlist;

err:
    free_completions(wlist);
    l_destroy(wlist);
    return NULL;
}

/*
 * sm_registercommand - add the command and a pointer to its handler to the commands list.
 *
 * So that free(data) works when destroying the list, I just concatenate the string
 * with the command structure. I could have used a static vector of commands, but this
 * way I can add aliases and macros at runtime (planned in future).
 *
 */

bool sm_registercommand(const char *command, handler_ptr handler, list_t *commands,
                        char *shortdoc, char *longdoc, const char *complstr)
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
    data->completions = (complstr) ? init_subcmdlist(data, complstr) : NULL;
    data->complidx = 0;

    /* add new command to list */
    if (l_append(commands, NULL, data) == -1) {
        free(data);
        return false;
    }

    return true;
}

bool sm_execcommand(globals_t *vars, const char *commandline)
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
        
        /* legal I guess, just don't do anything */
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
