/*
    Interrupt handling to stop watching or locking variables.

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

#ifndef INTERRUPT_H
#define INTERRUPT_H

#include <setjmp.h>
#include <signal.h>

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
