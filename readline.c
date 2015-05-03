/*
 readline.c: replaces libreadline

 Copyright (C) 2015 Jonathan Pelletier <funmungus(a)gmail.com>

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include <stdio.h>

#include "readline.h"

int rl_attempted_completion_over = 0;
const char *rl_readline_name = "scanmem";
rl_completion_func_t *rl_attempted_completion_function = NULL;

/* always return NULL to show that there are no completions */
char **rl_completion_matches(const char *text, rl_compentry_func_t
                             *entry_function)
{
    return NULL;
}

/* show the prompt, then allocate, read and
   return a line with getline() */
char *readline(const char *prompt)
{
    char *line = NULL;
    size_t n = 0;
    ssize_t bytes_read;

    printf("%s", prompt);
    fflush(stdout);
    bytes_read = getline(&line, &n, stdin);
    if (bytes_read > 0)
        line[bytes_read - 1] = '\0';  /* remove the trailing newline */

    return line;
}

/* don't maintain a history */
void add_history(const char *line)
{
}
