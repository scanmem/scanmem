/*
    Regression test for the peekbuf cache overflow.

    sm_peekdata keeps a fixed size mirror of target memory. A truncated read
    leaves a partial chunk in peekbuf.size, and the head shift that is
    supposed to make room only moves whole chunks, so it comes up short by
    that remainder and the read loop is set up to run past the end of the
    cache. Getting there needs the target's mappings to change between the
    truncated read and the next request, which is normal for a target whose
    other threads keep running while only the main one is stopped. This test
    does it deliberately with no_ptrace so the child keeps running.

    Before the fix the last request writes 2045 bytes past the end of the
    cache. On a hardened build that aborts in __pread_chk, otherwise it
    corrupts whatever follows and the request fails.

    Copyright (C) 2026 scanmem authors

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/wait.h>

#include "config.h"
#include "scanmem.h"

#define SKIP 77                 /* automake's exit code for a skipped test */

/* has to line up with PEEKDATA_CHUNK and MAX_PEEKBUF_SIZE in ptrace.c */
#define CHUNK 2048
#define CACHE ((1 << 16) + CHUNK)

/* the truncated read has to leave this much of a chunk behind for the shift
   to come up short by the largest amount it can */
#define TAIL (CHUNK - 3)
#define RUN (1 << 16)           /* mapped bytes before the hole */
#define BLOCK (1 << 20)

static int to_child[2], to_parent[2];

static void child_main(void)
{
    char *blk = mmap(NULL, BLOCK, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (blk == MAP_FAILED)
        _exit(1);
    memset(blk, 0x41, BLOCK);

    /* punch a hole so the read that reaches it comes back short */
    char *hole = blk + BLOCK / 2;
    long pagesize = sysconf(_SC_PAGESIZE);
    if (pagesize < 1 || munmap(hole, (size_t)pagesize) != 0)
        _exit(1);

    /* the mapped run has to end exactly RUN+TAIL past the address we start
       reading from, that remainder is the whole point */
    char *a = hole - (RUN + TAIL);
    if (write(to_parent[1], &a, sizeof(a)) != (ssize_t)sizeof(a))
        _exit(1);

    char c;
    if (read(to_child[0], &c, 1) != 1)
        _exit(1);

    /* another thread allocating, as far as the scanner can tell */
    if (mmap(hole, (size_t)pagesize, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) == MAP_FAILED)
        _exit(1);
    memset(hole, 0x42, (size_t)pagesize);
    if (write(to_parent[1], &c, 1) != 1)
        _exit(1);

    for (;;)
        pause();
}

int main(void)
{
#if !HAVE_PROCMEM
    fprintf(stderr, "no /proc/pid/mem, nothing to test here\n");
    return SKIP;
#else
    const mem64_t *r;
    size_t memlength;
    char *a = NULL;
    char c = 1;
    pid_t pid;
    int rc = 1;

    alarm(60);                  /* do not hang the suite if a pipe read stalls */

    if (pipe(to_child) != 0 || pipe(to_parent) != 0) {
        perror("pipe");
        return 1;
    }

    if ((pid = fork()) == -1) {
        perror("fork");
        return 1;
    }
    if (pid == 0) {
        child_main();
        _exit(0);
    }

    if (read(to_parent[0], &a, sizeof(a)) != (ssize_t)sizeof(a)) {
        fprintf(stderr, "child did not come up\n");
        goto out;
    }

    if (!sm_init()) {
        fprintf(stderr, "sm_init failed\n");
        goto out;
    }
    /* the child has to keep running for its mappings to change under us */
    sm_globals.options.no_ptrace = 1;
    if (!sm_attach(pid)) {
        fprintf(stderr, "cannot attach, skipping\n");
        rc = SKIP;
        goto out;
    }

    /* fill the cache with whole chunks */
    if (!sm_peekdata(a, UINT16_MAX, &r, &memlength) || memlength < UINT16_MAX) {
        fprintf(stderr, "first read failed, cannot set the test up\n");
        rc = SKIP;
        goto detach;
    }

    /* one chunk further, which runs into the hole and comes back short.
       peekbuf.size stops being a multiple of the chunk size here */
    if (!sm_peekdata(a + RUN - 6, 10, &r, &memlength)) {
        fprintf(stderr, "second read failed, cannot set the test up\n");
        rc = SKIP;
        goto detach;
    }
    if (memlength != TAIL + 6) {
        fprintf(stderr, "expected a %d byte remainder, got %zu, "
                "the geometry does not hold on this kernel\n",
                TAIL + 6, memlength);
        rc = SKIP;
        goto detach;
    }

    /* the hole is mapped again, so reads past it start working */
    if (write(to_child[1], &c, 1) != 1 || read(to_parent[0], &c, 1) != 1) {
        fprintf(stderr, "child did not remap\n");
        goto detach;
    }

    /* this is the one that used to run off the end of the cache */
    if (!sm_peekdata(a + UINT16_MAX, UINT16_MAX, &r, &memlength)) {
        fprintf(stderr, "FAIL: the request the cache could not fit\n");
        goto detach;
    }
    if (memlength < UINT16_MAX) {
        fprintf(stderr, "FAIL: short by %zu bytes\n",
                (size_t)UINT16_MAX - memlength);
        goto detach;
    }
    if (memlength > CACHE) {
        fprintf(stderr, "FAIL: claims %zu bytes out of a %d byte cache\n",
                memlength, CACHE);
        goto detach;
    }

    printf("ok, %zu bytes back and the cache held\n", memlength);
    rc = 0;

detach:
    sm_detach(pid);
    sm_cleanup();
out:
    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);
    return rc;
#endif
}
