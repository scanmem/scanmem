/*
    Interrupt handling to stop watching or locking variables.
*/

#ifndef INTERRUPT_H
#define INTERRUPT_H

/* small header file to manage interrupted commands */

static sigjmp_buf jmpbuf;       /* used when aborting a command due to interrupt */
static sighandler_t oldsig;     /* reinstalled before longjmp */
static unsigned intused;

/* signal handler to handle interrupt during a commands */
static void interrupted(int n)
{
    (void) n;
    siglongjmp(jmpbuf, 1);
}

#define INTERRUPTABLE() ((oldsig = signal(SIGINT, interrupted)), intused = 1, sigsetjmp(jmpbuf, 1))
#define ENDINTERRUPTABLE() (intused ? ((void) signal(SIGINT, oldsig), intused = 0) : (intused = 0))

#endif /* INTERRUPT_H */
