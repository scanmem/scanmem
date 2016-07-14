/*
    Prompt, command completion and version information.

    Copyright (C) 2006,2007,2009 Tavis Ormandy <taviso@sdf.lonestar.org>
    Copyright (C) 2010,2011 Lu Wang <coolwanglu@gmail.com>

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

#include "getline.h"
#include "scanmem.h"
#include "commands.h"
#include "show_message.h"

/* command generator for readline completion */
static char *commandgenerator(const char *text, int state)
{
    static unsigned index = 0;
    unsigned i;
    size_t len;
    element_t *np;

    /* reset generator if state == 0, otherwise continue from last time */
    index = state ? index : 0;

    np = globals.commands ? globals.commands->head : NULL;

    len = strlen(text);

    /* skip to the last node checked */
    for (i = 0; np && i < index; i++)
        np = np->next;

    /* traverse the commands list, checking for matches */
    while (np) {
        command_t *command = np->data;

        np = np->next;

        /* record progress */
        index++;

        /* if shortdoc is NULL, this is not supposed to be user visible */
        if (command == NULL || command->command == NULL
            || command->shortdoc == NULL)
            continue;

        /* check if we have a match */
        if (strncmp(text, command->command, len) == 0) {
            return strdup(command->command);
        }
    }

    return NULL;
}

/* custom completor program for readline */
static char **commandcompletion(const char *text, int start, int end)
{
    (void) end;

    /* never use default completer (filenames), even if I dont generate any matches */
    rl_attempted_completion_over = 1;

    /* only complete on the first word, the command */
    return start ? NULL : rl_completion_matches(text, commandgenerator);
}

/*
 * substitute `shell backtick` commands with their output.
 */
static bool shell_backticks(char **lineptr)
{
    char *line = *lineptr;
    char *cmd_stpos, *cmd_endpos, *cmd, cmdout[BUFSIZ];
    char *tmp, *tmp_st;
    FILE *cmdfp;
    size_t cmd_len, cmdout_len, line_len;

    line_len = strlen(line);

    cmd_stpos = line;
st_was_escaped:
    /* look for starting backtick */
    if ((cmd_stpos = strchr(cmd_stpos, '`')) == NULL || *cmd_stpos == '\0')
        return true; // no backticks, nothing to substitute
    if (cmd_stpos != line && cmd_stpos[-1] == '\\') {
        cmd_stpos++;
        goto st_was_escaped;
    }
    if (*++cmd_stpos == '\0') { // hop over first backtick
        fprintf(stderr, "error: no ending backtick in shell subsitution.\n");
        return false;
    }

    cmd_endpos = cmd_stpos;
end_was_escaped:
    if ((cmd_endpos = strchr(cmd_endpos, '`')) == NULL || *cmd_endpos == '\0') {
        fprintf(stderr, "error: no ending backtick in shell substitution.\n");
        return false;
    }
    if (cmd_endpos[-1] == '\\') {
        cmd_endpos++;
        goto end_was_escaped;
    }

    if (cmd_endpos-1 == cmd_stpos)
        return true; // XXX cleanup

    cmd_len = cmd_endpos - cmd_stpos; // -1 for endtick
    cmd = (char *)alloca(cmd_len);

    /* copy shell command substr into cmd */
    memcpy(cmd, cmd_stpos, cmd_len);
    cmd[cmd_len] = '\0';
    cmd_endpos++; // hop over ending backtick

    /* fflush() in preparation for popen() */
    fflush(stdout);

    bool cmdok = true;
    if ((cmdfp = popen(cmd, "r"))    == NULL ||
        fgets(cmdout, BUFSIZ, cmdfp) == NULL) {
        /*
         * If the command fails for any reason, we will simply cut out
         * the shell command from the line.
         */
        cmdok = false;
    }

    if (cmdok) {
        pclose(cmdfp);
        if ((tmp = strchr(cmdout, '\n')) != NULL)
            *tmp = '\0';
        cmdout_len = strlen(cmdout);
    }

    if ((tmp = malloc(line_len - (cmd_len+2)
            + (cmdok ? cmdout_len : 0))) == NULL) { // +2 to include btick removal
        fprintf(stderr, "error: no space for new line.\n");
        return false;
    }

    tmp_st = tmp;

    /* copy up to first backtick */
    memcpy(tmp, line, cmd_stpos-1 - line); // -1 to rewind to first btick
    tmp += cmd_stpos-1 - line;

    /* copy shell output */
    if (cmdok) {
        memcpy(tmp, cmdout, cmdout_len);
        tmp += cmdout_len;
    }

    /* copy from char after end backtick to end-of-string NUL */
    memcpy(tmp, cmd_endpos, (line+line_len+2) - cmd_endpos); // +2 for NUL

    *lineptr = tmp_st;

    return true;
}

/*
 * getcommand() reads in a command using readline, and places a pointer to the
 * read string into *line, _which must be free'd by caller_.
 * returns true on success, or false on error.
 */

bool getcommand(globals_t * vars, char **line)
{
    char prompt[64];
    bool success = true;

    assert(vars != NULL);

    snprintf(prompt, sizeof(prompt), "%ld> ", vars->num_matches);

    rl_readline_name = "scanmem";
    rl_attempted_completion_function = commandcompletion;

    while (true) {
        if (vars->options.backend == 0)
        {
            /* for normal user, read in the next command using readline library */
            success = ((*line = readline(prompt)) != NULL);
        }
        else 
        {
            /* disable readline for front-end, since readline may produce ansi escape codes, which is terrible for front-end */
            printf("%s\n", prompt); /* add a newline for front-end */
            fflush(stdout); /* otherwise front-end may not receive this */
            *line = NULL; /* let getline malloc it */
            size_t n;
            ssize_t bytes_read = getline(line, &n, stdin);
            success = (bytes_read > 0);
            if (success)
                (*line)[bytes_read-1] = '\0'; /* remove the trailing newline */
        }
        if (!success) {
            /* EOF */
            if ((*line = strdup("__eof")) == NULL) {
                fprintf(stderr,
                        "error: sorry, there was a memory allocation error.\n");
                return false;
            }
        }

        if (strlen(*line) && shell_backticks(line))
            break;

        free(*line);
    }

    /* record this line in readline history */
    add_history(*line);
    return true;
}

void printversion(FILE *outfd)
{
    fprintf(outfd, "scanmem version %s\n"
               "Copyright (C) 2009,2010 Tavis Ormandy, Eli Dupree, WANG Lu\n"
               "Copyright (C) 2006-2009 Tavis Ormandy\n"
               "scanmem comes with ABSOLUTELY NO WARRANTY; for details type `show warranty'.\n"
               "This is free software, and you are welcome to redistribute it\n"
               "under certain conditions; type `show copying' for details.\n\n", PACKAGE_VERSION);
}
