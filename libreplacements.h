/*
    libreplacements.h  Minimal function replacements for libraries. 
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

#if !defined (LIBREPLACEMENTS_H)
#define LIBREPLACEMENTS_H

typedef char *rl_compentry_func_t(const char *, int);
typedef char **rl_completion_func_t(const char *, int, int);

extern int rl_attempted_completion_over;
extern const char *rl_readline_name;
extern rl_completion_func_t *rl_attempted_completion_function;

extern char **rl_completion_matches(const char *, rl_compentry_func_t *);
extern char *readline(const char *);
extern void add_history(const char *);

#endif

