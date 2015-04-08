
/* Copyright (C) 1991-2009 Free Software Foundation, Inc.

   This file is part of the GNU Readline Library (Readline), a library
   for reading lines of text with interactive input and history editing.      

   Readline is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   Readline is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with Readline.  If not, see <http://www.gnu.org/licenses/>.
*/

#if !defined (LIBREPLACEMENTS_H)
#define LIBREPLACEMENTS_H

#define RL_SIG_RECEIVED() (_rl_caught_signal != 0)

#if !defined (PARAMS)
#  if defined (__STDC__) || defined (__GNUC__) || defined (__cplusplus)
#    define PARAMS(protos) protos
#  else
#    define PARAMS(protos) ()
#  endif
#endif

typedef char *rl_compentry_func_t PARAMS((const char *, int));
typedef char **rl_completion_func_t PARAMS((const char *, int, int));

extern int rl_attempted_completion_over;
extern const char *rl_readline_name;
extern rl_completion_func_t *rl_attempted_completion_function;

extern char **rl_completion_matches PARAMS((const char *, rl_compentry_func_t *));
extern char *readline PARAMS((const char *));
extern void add_history PARAMS((const char *));


// Referenced functions
extern void *xmalloc PARAMS((size_t));
extern void *xrealloc PARAMS((void *, size_t));
extern void xfree PARAMS((void *));

#endif

