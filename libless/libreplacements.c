
#include "libreplacements.h"

/* Return an array of (char *) which is a list of completions for TEXT.
   If there are no completions, return a NULL pointer.
   The first entry in the returned array is the substitution for TEXT.
   The remaining entries are the possible completions.
   The array is terminated with a NULL pointer.

   ENTRY_FUNCTION is a function of two args, and returns a (char *).
     The first argument is TEXT.
     The second is a state argument; it should be zero on the first call, and
     non-zero on subsequent calls.  It returns a NULL pointer to the caller
     when there are no more matches.
 */
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
      if (RL_SIG_RECEIVED ())
	{
	  /* Start at 1 because we don't set matches[0] in this function.
	     Only free the list members if we're building match list from
	     rl_filename_completion_function, since we know that doesn't
	     free the strings it returns. */
	  if (entry_function == rl_filename_completion_function)
	    {
	      for (i = 1; match_list[i]; i++)
		xfree (match_list[i]);
	    }
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


