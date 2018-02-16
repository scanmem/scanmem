/*
    Replace libreadline.

    Copyright (C) 2015 Jonathan Pelletier <funmungus(a)gmail.com>

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

#ifndef READLINE_H
#define READLINE_H

typedef char *rl_compentry_func_t(const char *, int);
typedef char **rl_completion_func_t(const char *, int, int);

static char *rl_line_buffer = "";

extern int rl_attempted_completion_over;
extern const char *rl_readline_name;
extern rl_completion_func_t *rl_attempted_completion_function;

char **rl_completion_matches(const char *text, rl_compentry_func_t
                             *entry_function);
char *readline(const char *prompt);
void add_history(const char *line);

#endif /* READLINE_H */
