
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

#include <stdlib.h>
#include <stdio.h>

#include "libreplacements.h"

int rl_attempted_completion_over = 0;
const char * rl_readline_name = "scanmem";
rl_completion_func_t * rl_attempted_completion_function = NULL;

char **
rl_completion_matches (text, entry_function)
     const char *text;
     rl_compentry_func_t *entry_function;
{
  return NULL;
}

char *
readline(prompt)
     const char *prompt;
{
  return NULL;
}

void add_history(line)
     const char *line;
{
}

// referenced functions
static void
on_alloc_error (fname)
     char *fname;
{
  fprintf (stderr, "%s: out of virtual memory\n", fname);
  exit (2);
}

void *
xmalloc(bytes)
     size_t bytes;
{
  void * allocPt;

  allocPt = malloc (bytes);
  if (allocPt == 0)
    on_alloc_error ("xmalloc");
  return (allocPt);
}

void *
xrealloc(pointer, bytes)
     void * pointer;
     size_t bytes;
{
  void * allocPt;

  allocPt = pointer ? realloc (pointer, bytes) : malloc (bytes);

  if (allocPt == 0)
    on_alloc_error ("xrealloc");
  return (allocPt);
}

void
xfree(pointer)
    void * pointer;
{
  free (pointer);
}

