/*
    Prompt and command completion.

    Copyright (C) 2006,2007,2009 Tavis Ormandy <taviso@sdf.lonestar.org>
    Copyright (C) 2010,2011      Lu Wang <coolwanglu@gmail.com>
    Copyright (C) 2018           Sebastian Parschauer <s.parschauer@gmx.de>

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
# define _GNU_SOURCE
#endif

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <string.h>
#include <stdbool.h>

#if HAVE_LIBREADLINE
#include <readline/readline.h>
#include <readline/history.h>
#else
#include "readline.h"
#endif

#include "menu.h"
#include "list.h"
#include "getline.h"
#include "scanmem.h"
#include "commands.h"
#include "show_message.h"

/* sub-command generator for readline completion */
static char *subcommandgenerator(char *start, int state, list_t *list,
                                 unsigned *index)
{
    unsigned i;
    element_t *np;
    size_t len;
    char *spos = strchr(start, ' ');

    len = strlen(start);
    if (spos)
        len = spos - start;

    /* reset generator if state == 0, otherwise continue from last time */
    *index = (state && !spos) ? *index : 0;

    np = list->head;

    /* skip to the last node checked */
    for (i = 0; np && i < *index; i++)
        np = np->next;

    /* traverse the completion list, checking for matches */
    while (np) {
        completion_t *compl = np->data;

        np = np->next;

        /* record progress */
        (*index)++;

        if (compl == NULL || compl->word == NULL)
            continue;

        /* check if we have a match */
        if (strncmp(start, compl->word, len) == 0) {
            if (!spos)
                return strdup(compl->word);
            if (!compl->list)
                return NULL;
            while (*spos == ' ')
                spos++;
            return subcommandgenerator(spos, state, compl->list, &compl->index);
        }
    }

    return NULL;
}

static char *find_command(const char *start, int state, unsigned *index,
                          bool is_subcmd)
{
    unsigned i;
    size_t len;
    element_t *np;
    globals_t *vars = &sm_globals;
    char *spos = strchr(start, ' ');

    /* reset generator if state == 0, otherwise continue from last time */
    *index = (state && !spos) ? *index : 0;

    np = vars->commands ? vars->commands->head : NULL;

    len = strlen(start);
    if (spos)
        len = spos - start;

    /* skip to the last node checked */
    for (i = 0; np && i < *index; i++)
        np = np->next;

    /* traverse the commands list, checking for matches */
    while (np) {
        command_t *command = np->data;

        np = np->next;

        /* record progress */
        (*index)++;

        /* if shortdoc is NULL, this is not supposed to be user visible */
        if (command == NULL || command->command == NULL
            || command->shortdoc == NULL)
            continue;

        /* check if we have a match */
        if (strncmp(start, command->command, len) == 0) {
            if (!spos)
                return strdup(command->command);
            if (!command->completions || is_subcmd)
                return NULL;
            while (*spos == ' ')
                spos++;
            if (strncmp(command->command, "help", sizeof("help") - 1) == 0)
                return find_command(spos, state, &command->complidx, true);
            return subcommandgenerator(spos, state, command->completions,
                                       &command->complidx);
        }
    }

    return NULL;
}

/* command generator for readline completion */
static char *commandgenerator(const char *text, int state)
{
    char *start = rl_line_buffer;
    static unsigned index = 0;

    while (*start == ' ')
        start++;
    return find_command(start, state, &index, false);
}

/* custom completor program for readline */
static char **commandcompletion(const char *text, int start, int end)
{
    (void) end;

    /* never use default completer (filenames), even if I dont generate any matches */
    rl_attempted_completion_over = 1;

    /* complete everything */
    return rl_completion_matches(text, commandgenerator);
}


/*
 * getcommand() reads in a command using readline and places a pointer to
 * the read string into *line, which must be free'd by caller.
 * returns true on success, or false on error.
 */

bool getcommand(globals_t *vars, char **line)
{
    char prompt[64];
    bool success = true;

    assert(vars != NULL);

    if (vars->matches) {
        snprintf(prompt, sizeof(prompt), "%ld> ", vars->num_matches);
    } else {
        snprintf(prompt, sizeof(prompt), "> ");
    }

    rl_readline_name = "scanmem";
    rl_attempted_completion_function = commandcompletion;

    while (true) {

        success = ((*line = readline(prompt)) != NULL);
        if (!success) {
            /* EOF */
            if ((*line = strdup("__eof")) == NULL) {
                show_error("sorry, there was a memory allocation error.\n");
                return false;
            }
        }

        if (strlen(*line)) {
            break;
        }

        free(*line);
    }

    /* record this line to readline history */
    add_history(*line);
    return true;
}
