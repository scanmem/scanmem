
#define RL_SIG_RECEIVED() (_rl_caught_signal != 0)

#if !defined (PARAMS)
#  if defined (__STDC__) || defined (__GNUC__) || defined (__cplusplus)
#    define PARAMS(protos) protos
#  else
#    define PARAMS(protos) ()
#  endif
#endif

typedef char *rl_compentry_func_t PARAMS((const char *, int));

extern int volatile _rl_caught_signal;

extern char **rl_completion_matches PARAMS((const char *, rl_compentry_func_t *));

