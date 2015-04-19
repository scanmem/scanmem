/*
    libreplacements.c  Minimal function replacements for libraries. 
    Copyright (C) 2015 Jonathan Pelletier <funmungus(a)gmail.com>

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

#include <stdio.h>

#include "libreplacements.h"

int rl_attempted_completion_over = 0;
const char *rl_readline_name = "scanmem";
rl_completion_func_t *rl_attempted_completion_function = NULL;

char **
rl_completion_matches(text, entry_function)
    const char *text;
    rl_compentry_func_t *entry_function;
{
    (void) text;
    (void) entry_function;
    return 0;
}

char *
readline(prompt)
    const char *prompt;
{
    char *line = NULL; /* let getline malloc it */
    size_t n = 0;
    ssize_t bytes_read ;
    int success ;

    printf("%s", prompt);
    fflush(stdout); /* otherwise front-end may not receive this */
    bytes_read = getline(&line, &n, stdin);
    success = (bytes_read > 0);
    if (success)
        line[bytes_read-1] = '\0'; /* remove the trialing newline */

    return line;
}

void add_history(line)
    const char *line;
{
    (void) line;
}

