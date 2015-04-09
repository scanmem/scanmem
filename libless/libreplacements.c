
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

// Referenced functions
static int compute_lcd_of_matches (char **, int, const char *);

char **
rl_completion_matches (text, entry_function)
     const char *text;
     rl_compentry_func_t *entry_function;
{
  register int i;

  /* Number of slots in match_list. */
  int match_list_size;

  /* The list of matches. */
  char **match_list;

  /* Number of matches actually found. */
  int matches;

  /* Temporary string binder. */
  char *string;

  matches = 0;
  match_list_size = 10;
  match_list = (char **)xmalloc ((match_list_size + 1) * sizeof (char *));
  match_list[1] = (char *)NULL;

  while (string = (*entry_function) (text, matches))
    {
      if (0)
	{
	  xfree (match_list);
	  match_list = 0;
	  match_list_size = 0;
	  matches = 0;
	  RL_CHECK_SIGNALS ();
	}

      if (matches + 1 >= match_list_size)
	match_list = (char **)xrealloc
	  (match_list, ((match_list_size += 10) + 1) * sizeof (char *));

      if (match_list == 0)
	return (match_list);

      match_list[++matches] = string;
      match_list[matches + 1] = (char *)NULL;
    }

  /* If there were any matches, then look through them finding out the
     lowest common denominator.  That then becomes match_list[0]. */
  if (matches)
    compute_lcd_of_matches (match_list, matches, text);
  else				/* There were no matches. */
    {
      xfree (match_list);
      match_list = (char **)NULL;
    }
  return (match_list);
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

static int
qs_strcasecmp (lhs, rhs)
    const void *lhs, *rhs;
{
  return strcasecmp(lhs,rhs);
}

/* Find the common prefix of the list of matches, and put it into
   matches[0]. */
static int
compute_lcd_of_matches (match_list, matches, text)
     char **match_list;
     int matches;
     const char *text;
{
  register int i, c1, c2, si;
  int low;		/* Count of max-matched characters. */
  int lx;

  /* If only one match, just use that.  Otherwise, compare each
     member of the list with the next, finding out where they
     stop matching. */
  if (matches == 1)
    {
      match_list[0] = match_list[1];
      match_list[1] = (char *)NULL;
      return 1;
    }

  for (i = 1, low = 100000; i < matches; i++)
    {
	  for (si = 0;
	       (c1 = tolower(match_list[i][si])) &&
	       (c2 = tolower(match_list[i + 1][si]));
	       si++)
	    if (c1 != c2)
	      break;

      if (low > si)
	low = si;
    }

  /* If there were multiple matches, but none matched up to even the
     first character, and the user typed something, use that as the
     value of matches[0]. */
  if (low == 0 && text && *text)
    {
      match_list[0] = (char *)xmalloc (strlen (text) + 1);
      strcpy (match_list[0], text);
    }
  else
    {
      match_list[0] = (char *)xmalloc (low + 1);

     /* XXX - this might need changes in the presence of multibyte chars */

     /* If we are ignoring case, try to preserve the case of the string
 the user typed in the face of multiple matches differing in case. */

  /* sort the list to get consistent answers. */
  qsort (match_list+1, matches, sizeof(char *), qs_strcasecmp);

  si = strlen (text);
  lx = (si <= low) ? si : low;	/* check shorter of text and matches */
  /* Try to preserve the case of what the user typed in the presence of
     multiple matches: check each match for something that matches
     what the user typed taking case into account; use it up to common
     length of matches if one is found.  If not, just use first match. */
  for (i = 1; i <= matches; i++)
  {
    if (strncmp (match_list[i], text, lx) == 0)
      {
	strncpy (match_list[0], match_list[i], low);
	break;
      }
  }
  /* no casematch, use first entry */
  if (i > matches)
    strncpy (match_list[0], match_list[1], low);

      match_list[0][low] = '\0';
    }

  return matches;
}

