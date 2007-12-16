/*

 $Id: interrupt.h,v 1.5 2007-04-11 10:43:26+01 taviso Exp $

*/

#ifndef _INTERRUPT_INC
#define _INTERRUPT_INC

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

#endif
