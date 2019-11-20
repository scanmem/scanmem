/*
    Message printing helper functions.

    Copyright (C) 2010 WANG Lu  <coolwanglu(a)gmail.com>

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

#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif

#include "config.h"

#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>

#include "common.h"
#include "show_message.h"
#include "scanmem.h"

void show_info(const char *fmt, ...)
{
    va_list args;
    va_start (args, fmt);
    fprintf(stderr, "info: ");
    vfprintf(stderr, fmt, args);
    va_end (args);
}

void show_error(const char *fmt, ...)
{
    va_list args;
    va_start (args, fmt);
    fprintf(stderr, "error: ");
    vfprintf(stderr, fmt, args);
    va_end (args);
}

void show_warn(const char *fmt, ...)
{
    va_list args;
    va_start (args, fmt);
    fprintf(stderr, "warn: ");
    vfprintf(stderr, fmt, args);
    va_end (args);
}

void show_user(const char *fmt, ...)
{
    va_list args;
    va_start (args, fmt);
    if (!(sm_globals.options.backend))
    {
        vfprintf(stderr, fmt, args);
    }
    va_end (args);
}

void show_debug(const char *fmt, ...)
{
    va_list args;
    va_start (args, fmt);
    if (sm_globals.options.debug)
    {
        fprintf(stderr, "debug: ");
        vfprintf(stderr, fmt, args);
    }
    va_end (args);
}

FILE *get_pager(FILE *fallback_output)
{
    const char *pager;
    pid_t pgpid;
    int pgret;
    int pgpipe[2];
    bool pgcmdfail = false;
    FILE *retfd = NULL;
    char *const emptyvec[1] = { NULL };

    assert(fallback_output != NULL && fileno(fallback_output) != -1);

    if (sm_globals.options.backend || !isatty(fileno(fallback_output)))
        return fallback_output;

    if ((pager = util_getenv("PAGER")) == NULL || *pager == '\0') {
        show_warn("get_pager(): couldn't get $PAGER, falling back to `more`\n");
        pager = "more";
    }

    if (pipe2(pgpipe, O_NONBLOCK) == -1) {
        show_error("get_pager(): pipe2() error `%s`. falling back to normal output\n", strerror(errno));
        return fallback_output;
    }
    /*
     * we write here to ensure we will always
     * have something to read() into pgcmdfail.
     */
    write(pgpipe[1], "", 1);

    /* XXX: is $PATH modified prior? */
retry:
    switch ((pgpid = fork())) {
    case -1:
        show_warn("get_pager(): fork() failed. falling back to normal output\n");
        return fallback_output;
    case 0:
        execvp(pager, emptyvec);
        /*
         * if we got here, it means execvp() failed.
         * errno contains the error, so pass it back
         * up to parent. we use a pipe here to let
         * the parent know that we are indeed returning
         * the return value of the failed execvp().
         */
        char nullbuf;
        /* read() to empty pipe */
        read(pgpipe[0], &nullbuf, 1);
        write(pgpipe[1], "1", 2);
        exit(errno);
        /* NOTREACHED */
    default:
        if (waitpid(pgpid, &pgret, 0) == -1) {
            show_debug("pager: waitpid() error `%s`\n", strerror(errno));
            show_warn("pager: waitpid() error. falling back to normal output\n");
            return fallback_output;
        } else {
            pgret = WEXITSTATUS(pgret);
            if (read(pgpipe[0], &pgcmdfail, 1) == -1) {
                show_error("pager: pipe read() error `%s`. falling back to normal output\n", strerror(errno));
                return fallback_output;
            }
            if (pgcmdfail) {
                show_debug("pager: execvp(pg=%s) ret -> %d (%s)\n", pager, pgret, strerror(pgret));
                if (!strcmp(pager, "more")) {
                    show_warn("pager: sh failed to execute more. falling back to normal output\n");
                    return fallback_output;
                } else {
                    show_warn("pager: sh failed to execute `%s`. trying `more`...\n", pager);
                    pager = "more";
                    goto retry;
                }
            }
        }
    }

    if ((retfd = popen(pager, "w")) == NULL) {
        show_warn("pager: couldn't popen() pager, falling back to normal output\n");
        return fallback_output;
    }

    /*
     * we need to ignore SIGPIPE in case the pager exits wihout draining the
     * pipe (less seems to do this if you exit a large listing without
     *       scrolling down)
     */
    signal(SIGPIPE, SIG_IGN);

    assert(retfd != NULL);

    return retfd;
}
