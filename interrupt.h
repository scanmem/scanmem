/*
    Interrupt handling.

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

#ifndef _GNU_SOURCE
# define _GNU_SOURCE    /* for sighandler_t */
#endif

#include <setjmp.h>
#include <signal.h>

extern sigjmp_buf jmpbuf;       /* used when aborting a command due to an interrupt */
extern sighandler_t oldsig;     /* reinstalled before longjmp */
extern volatile sig_atomic_t intr_used;

/* signal handler used to handle an interrupt during commands */
void interrupted(int);

/* signal handler used to handle an interrupt during scans */
void interrupt_scan(int);

#define INTERRUPTABLE() ((oldsig = signal(SIGINT, interrupted)), intr_used = 1, sigsetjmp(jmpbuf, 1))
#define INTERRUPTABLESCAN() ((oldsig = signal(SIGINT, interrupt_scan)), intr_used = 1)
#define ENDINTERRUPTABLE() (intr_used ? ((void) signal(SIGINT, oldsig), intr_used = 0) : (intr_used = 0))

#endif /* INTERRUPT_H */
