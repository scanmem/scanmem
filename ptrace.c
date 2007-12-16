/*
 $Id: ptrace.c,v 1.22 2007-06-07 15:16:46+01 taviso Exp taviso $
 
 Copyright (C) 2006,2007 Tavis Ormandy <taviso@sdf.lonestar.org>
 
 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.
 
 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

/* prepare LARGEFILE support, i'll autoconf this later */
#if HAVE_PROCMEM
# ifdef _FILE_OFFSET_BITS
#  undef _FILE_OFFSET_BITS
# endif
# define _FILE_OFFSET_BITS 64
# ifdef _LARGEFILE64_SOURCE
#  undef _LARGEFILE64_SOURCE
# endif
# define _LARGEFILE64_SOURCE
# ifdef _XOPEN_SOURCE
#  undef _XOPEN_SOURCE
# endif
# define _XOPEN_SOURCE 500
#endif

#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <limits.h>
#include <fcntl.h>

#ifdef __GNUC__
# define EXPECT(x,y) __builtin_expect(x, y)
#else
# define EXPECT(x,y) x
#endif

#include "value.h"
#include "scanmem.h"

/* ptrace peek buffer, used by peekdata() */
static struct {
    union {
        signed long peeks[2];   /* read from ptrace() */
        unsigned char bytes[sizeof(long) * 2];  /* used to retrieve unaligned hits */
    } cache;
    unsigned size;              /* number of entries (in longs) */
    char *base;                 /* base address of cached region */
    pid_t pid;                  /* what pid this applies to */
} peekbuf;


bool attach(pid_t target)
{
    int status;

    /* attach, to the target application, which should cause a SIGSTOP */
    if (ptrace(PTRACE_ATTACH, target, NULL, NULL) == -1L) {
        fprintf(stderr, "error: failed to attach to %d, %s\n", target,
                strerror(errno));
        return false;
    }

    /* wait for the SIGSTOP to take place. */
    if (waitpid(target, &status, 0) == -1 || !WIFSTOPPED(status)) {
        fprintf(stderr,
                "error: there was an error waiting for the target to stop.\n");
        fprintf(stdout, "info: %s\n", strerror(errno));
        return false;
    }

    /* flush the peek buffer */
    memset(&peekbuf, 0x00, sizeof(peekbuf));

    /* everything looks okay */
    return true;

}

bool detach(pid_t target)
{
    return ptrace(PTRACE_DETACH, target, NULL, 0) == 0;
}

/*
 * peekdata - caches overlapping ptrace reads to improve performance.
 * 
 * This routine could just call ptrace(PEEKDATA), but using a cache reduces
 * the number of peeks required by 70% when reading large chunks of
 * consecutive addresses.
 */

bool peekdata(pid_t pid, void *addr, value_t * result)
{
    char *reqaddr = addr;

    assert(peekbuf.size < 3);
    assert(result != NULL);

    memset(result, 0x00, sizeof(value_t));

    valnowidth(result);

    /* check if we have a cache hit */
    if (pid == peekbuf.pid && reqaddr >= peekbuf.base
        && (unsigned) (reqaddr + sizeof(long) - peekbuf.base) <=
        sizeof(long) * peekbuf.size) {

        result->value.tslong = *(long *) &peekbuf.cache.bytes[reqaddr - peekbuf.base];  /*lint !e826 */
        return true;
    } else if (pid == peekbuf.pid && reqaddr >= peekbuf.base
               && (unsigned) (reqaddr - peekbuf.base) <
               sizeof(long) * peekbuf.size) {

        assert(peekbuf.size != 0);

        /* partial hit, we have some of the data but not all, so remove old entry */

        if (EXPECT(peekbuf.size == 2, true)) {
            peekbuf.cache.peeks[0] = peekbuf.cache.peeks[1];
            peekbuf.size = 1;
            peekbuf.base += sizeof(long);
        }
    } else {

        /* cache miss, invalidate cache */
        peekbuf.pid = pid;
        peekbuf.size = 0;
        peekbuf.base = addr;
    }

    /* we need a ptrace() to complete request */
    errno = 0;

    peekbuf.cache.peeks[peekbuf.size] =
        ptrace(PTRACE_PEEKDATA, pid,
               peekbuf.base + sizeof(long) * peekbuf.size, NULL);

    /* check if ptrace() succeeded */
    if (EXPECT(peekbuf.cache.peeks[peekbuf.size] == -1L && errno != 0, false)) {
        /* its possible i'm trying to read partially oob */
        if (errno == EIO || errno == EFAULT) {
            unsigned i;
            long l;
            
            /* read backwards until we get a good read, then shift out the right value */
            for (i = 1, errno = 0; i < sizeof(long); i++, errno = 0) {
                if ((l = ptrace(PTRACE_PEEKDATA, pid, reqaddr - i, NULL)) == -1L && 
                    (errno == EIO || errno == EFAULT))
                        continue;
                /* wow, that worked */
                result->value.tslong = l;
                result->value.tulong >>= i * CHAR_BIT;
                
                /* note: some of these are impossible */
                if (sizeof(float) > sizeof(long) - i)
                    result->flags.tfloat = 0;
                if (sizeof(long) > sizeof(long) - i)
                    result->flags.wlong = 0;
                if (sizeof(int) > sizeof(long) - i)
                    result->flags.wint = 0;
                if (sizeof(short) > sizeof(long) - i)
                    result->flags.wshort = 0;
                if (sizeof(char) > sizeof(long) - i)
                    result->flags.wchar = 0;
                return true;
            }
        }
        /* i wont print a message here, would be very noisy if a region is unmapped */
        return false;
    }

    /* record new entry in cache */
    peekbuf.size++;

    /* return result to caller */
    result->value.tslong = *(long *) &peekbuf.cache.bytes[reqaddr - peekbuf.base];      /*lint !e826 */

    /* success */
    return true;
}

bool checkmatches(list_t * matches, pid_t target, value_t value,
                  matchtype_t type)
{
    element_t *n, *p;

    assert(matches != NULL);
    assert(matches->size);

    p = NULL;
    n = matches->head;

    /* stop and attach to the target */
    if (attach(target) == false)
        return false;

    /* shouldnt happen */
    if (matches->size == 0) {
        fprintf(stderr, "warn: cant check non-existant matches.\n");
        return false;
    }

    while (n) {
        match_t *match = n->data;
        value_t check;

        /* read value from this address */
        if (EXPECT(peekdata(target, match->address, &check) == false, false)) {

            /* assume this was in unmapped region, so remove */
            l_remove(matches, p, NULL);

            /* confirm this isnt the head element */
            n = p ? p->next : matches->head;
            continue;
        }

        truncval(&check, &match->lvalue);

        /* XXX: this sucks. */
        if (type != MATCHEXACT) {
            valcpy(&value, &match->lvalue);
        }

        if (EXPECT(valuecmp(&check, type, &value, &check), false)) {
            /* still a candidate */
            match->lvalue = check;
            p = n;
            n = n->next;
        } else {
            /* no match */
            l_remove(matches, p, NULL);

            /* confirm this isnt the head element */
            n = p ? p->next : matches->head;
        }
    }

    eprintf("info: we currently have %d matches.\n", matches->size);

    /* okay, detach */
    return detach(target);
}

ssize_t readregion(void *buf, pid_t target, const region_t *region, size_t offset, size_t max)
{
    char mem[32];
    int fd;
    ssize_t len;
    
    /* print the path to mem file */
    snprintf(mem, sizeof(mem), "/proc/%d/mem", target);
    
    /* attempt to open the file */
    if ((fd = open(mem, O_RDONLY)) == -1) {
        fprintf(stderr, "warn: unable to open %s.\n", mem);
        return -1;
    }
    
    /* check offset is sane */
    if (offset > region->size) {
        fprintf(stderr, "warn: unexpected offset while reading region.\n");
        return -1;
    }
    
    /* try to honour the request */
    len = pread(fd, buf, MIN(region->size - offset, max), region->start + offset);
    
    /* clean up */
    close(fd);
    
    return len;
}
    

/* searchregion() performs an initial search of the process for values matching value */
bool searchregions(list_t * matches, const list_t * regions, pid_t target,
                value_t value, bool snapshot)
{
    unsigned regnum = 0;
    unsigned char *data = NULL;
    element_t *n = regions->head;
    region_t *r;

    /* stop and attach to the target */
    if (attach(target) == false)
        return false;

    /* make sure we have some regions to search */
    if (regions->size == 0) {
        fprintf(stderr,
                "warn: no regions defined, perhaps you deleted them all?\n");
        fprintf(stderr,
                "info: use the \"reset\" command to refresh regions.\n");
        return detach(target);
    }

    /* check every memory region */
    while (n) {
        unsigned offset, nread = 0;
        ssize_t len = 0;

        /* load the next region */
        r = n->data;

#if HAVE_PROCMEM        
        /* over allocate by 3 bytes, which are set to zero so the last bytes can be read as longs */
        if ((data = calloc(r->size + sizeof(long) - 1, 1)) == NULL) {
            fprintf(stderr, "error: sorry, there was a memory allocation error.\n");
            return false;
        }
    
        /* keep reading until completed */
        while (nread < r->size) {
            if ((len = readregion(data + nread, target, r, nread, r->size)) == -1) {
                /* no, continue with whatever data was read */
                break;
            } else {
                /* some data was read */
                nread += len;
            }
        }
#else     
        /* cannot use /proc/pid/mem */
        nread = r->size;
#endif
        /* print a progress meter so user knows we havnt crashed */
        fprintf(stderr, "info: %02u/%02u searching %#10x - %#10x.", ++regnum,
                regions->size, r->start, r->start + r->size);
        fflush(stderr);

        /* for every offset, check if we have a match */
        for (offset = 0; offset < nread; offset++) {
            value_t check;
           
            /* initialise check */
            memset(&check, 0x00, sizeof(check));
            
            valnowidth(&check);

#if HAVE_PROCMEM           
            check.value.tulong = *(unsigned long *)(&data[offset]);
#else            
            if (EXPECT(peekdata(target, r->start + offset, &check) == false, false)) {
                break;
            }
#endif
            
            /* check if we have a match */
            if (snapshot || EXPECT(valuecmp(&value, MATCHEQUAL, &check, &check), false)) {
                match_t *match;
               
                /* save this new location */
                if ((match = calloc(1, sizeof(match_t))) == NULL) {
                    fprintf(stderr, "error: memory allocation failed.\n");
                    (void) detach(target);
                    return false;
                }

                match->address = r->start + offset;
                match->region = r;
                match->lvalue = check;

                if (EXPECT(l_append(matches, NULL, match) == -1, false)) {
                    fprintf(stderr, "error: unable to add match to list.\n");
                    (void) detach(target);
                    free(match);
                    return false;
                }
            }

            /* print a simple progress meter. */
            if (EXPECT(offset % ((r->size - (r->size % 10)) / 10) == 10, false)) {
                fprintf(stderr, ".");
                fflush(stderr);
            }
        }

        n = n->next;
        fprintf(stderr, "ok\n");
#if HAVE_PROCMEM
        free(data);
#endif        
    }

    eprintf("info: we currently have %d matches.\n", matches->size);

    /* okay, detach */
    return detach(target);
}

bool setaddr(pid_t target, void *addr, const value_t * to)
{
    value_t saved;

    if (attach(target) == false) {
        return false;
    }

    if (peekdata(target, addr, &saved) == false) {
        fprintf(stderr, "error: couldnt access the target address %10p\n",
                addr);
        return false;
    }

    /* XXX: oh god */
    if (to->flags.wlong)
        saved.value.tulong = to->value.tulong;
    else if (to->flags.wint)
        saved.value.tuint = to->value.tuint;
    else if (to->flags.wshort)
        saved.value.tushort = to->value.tushort;
    else if (to->flags.wchar)
        saved.value.tuchar = to->value.tuchar;
    else if (to->flags.tfloat)
        saved.value.tfloat = to->value.tslong;
    else {
        fprintf(stderr, "error: could not determine type to poke.\n");
        return false;
    }

    if (ptrace(PTRACE_POKEDATA, target, addr, saved.value.tulong) == -1L) {
        return false;
    }

    return detach(target);
}
