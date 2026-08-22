/*
    Functions to access the memory of the target process.
 
    Copyright (C) 2006,2007,2009 Tavis Ormandy <taviso@sdf.lonestar.org>
    Copyright (C) 2009           Eli Dupree <elidupree@charter.net>
    Copyright (C) 2009,2010      WANG Lu <coolwanglu@gmail.com>
    Copyright (C) 2015           Sebastian Parschauer <s.parschauer@gmx.de>
    Copyright (C) 2017-2018      Andrea Stacchiotti <andreastacchiotti(a)gmail.com>
 
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

#include "config.h"

/* for pread */
# ifdef _XOPEN_SOURCE
#  undef _XOPEN_SOURCE
# endif
# define _XOPEN_SOURCE 500

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
#include <sys/uio.h>
#ifdef HAVE_PTHREAD
#include <pthread.h>
#endif
#include <stdbool.h>
#include <limits.h>
#include <fcntl.h>

// dirty hack for FreeBSD
#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
#define PTRACE_ATTACH PT_ATTACH
#define PTRACE_DETACH PT_DETACH
#define PTRACE_PEEKDATA PT_READ_D
#define PTRACE_POKEDATA PT_WRITE_D
#endif

#include "common.h"
#include "value.h"
#include "scanroutines.h"
#include "scanmem.h"
#include "show_message.h"
#include "targetmem.h"
#include "interrupt.h"

/* progress handling */
#define NUM_DOTS (10)
#define NUM_SAMPLES (100)
#define MAX_PROGRESS (1.0)  /* 100% */
#if (!NUM_DOTS || !NUM_SAMPLES || NUM_SAMPLES % NUM_DOTS != 0)
#error Invalid NUM_DOTS to NUM_SAMPLES proportion!
#endif
#define SAMPLES_PER_DOT (NUM_SAMPLES / NUM_DOTS)
#define PROGRESS_PER_SAMPLE (MAX_PROGRESS / NUM_SAMPLES)

/* ptrace peek buffer, used by peekdata() as a mirror of the process memory.
 * Max size is the maximum allowed rounded VLT scan length, aka UINT16_MAX,
 * plus a `PEEKDATA_CHUNK`, to store a full extra chunk for maneuverability */
#if HAVE_PROCMEM
# define PEEKDATA_CHUNK 2048
#else
# define PEEKDATA_CHUNK sizeof(long)
#endif
#define MAX_PEEKBUF_SIZE ((1<<16) + PEEKDATA_CHUNK)
/* how far ahead we are willing to read in one go when the scan is walking
 * forwards. grows while access stays sequential, resets on a jump, so a dense
 * scan gets big reads and a sparse one does not pay for bytes it will not use */
#define READAHEAD_MAX (1<<16)
static struct {
    uint8_t cache[MAX_PEEKBUF_SIZE];  /* read from ptrace()  */
    size_t readahead;                 /* current readahead window */
    unsigned size;              /* amount of valid memory stored (in bytes) */
    const char *base;           /* base address of cached region */
    pid_t pid;                  /* pid of scanned process */
#if HAVE_PROCMEM
    int procmem_fd;             /* file descriptor of the opened `/proc/<pid>/mem` file */
#endif
} peekbuf;

#ifdef HAVE_PROCESS_VM_READV
/* cleared if the kernel or a sandbox refuses process_vm_readv, see below */
static bool vm_readv_ok = true;
#endif


/* Who is already ptracing `target`? Returns 0 if nobody, or if we cannot tell
   (no /proc, so BSD just falls back to the plain errno message). */
static pid_t tracer_pid_of(pid_t target)
{
    char path[64];
    char line[256];
    FILE *f;
    pid_t tracer = 0;

    snprintf(path, sizeof(path), "/proc/%d/status", target);
    if ((f = fopen(path, "r")) == NULL)
        return 0;

    while (fgets(line, sizeof(line), f) != NULL) {
        if (strncmp(line, "TracerPid:", 10) == 0) {
            tracer = (pid_t)strtol(line + 10, NULL, 10);
            break;
        }
    }

    fclose(f);
    return tracer;
}

bool sm_attach(pid_t target)
{
    if (!sm_globals.options.no_ptrace)
    {
        int status;

        /* attach to the target application, which should cause a SIGSTOP */
        if (ptrace(PTRACE_ATTACH, target, NULL, NULL) == -1L) {
            int attach_errno = errno;
            pid_t tracer = tracer_pid_of(target);

            show_error("failed to attach to %d, %s\n", target,
                       strerror(attach_errno));

            /* "Operation not permitted" covers two very different problems and
               the bare errno sends people down the wrong one, see #392, #393 */
            if (tracer > 0) {
                show_info("%d is already being traced by process %d, "
                          "detach that first.\n", target, tracer);
            } else if (attach_errno == EPERM) {
                show_info("run scanmem as root, or check whether "
                          "/proc/sys/kernel/yama/ptrace_scope allows it.\n");
            }
            return false;
        }

        /* wait for the SIGSTOP to take place. */
        if (waitpid(target, &status, 0) == -1 || !WIFSTOPPED(status)) {
            show_error("there was an error waiting for the target to stop.\n");
            show_info("%s\n", strerror(errno));
            return false;
        }
    }

    /* reset the peek buffer */
    peekbuf.size = 0;
    peekbuf.base = NULL;

#if HAVE_PROCMEM
    { /* open the `/proc/<pid>/mem` file */
        char mem[32];
        int fd;

        /* print the path to mem file */
        snprintf(mem, sizeof(mem), "/proc/%d/mem", target);

        /* attempt to open the file */
        if ((fd = open(mem, O_RDWR)) == -1) {
            show_error("unable to open %s.\n", mem);
            return false;
        }
        peekbuf.procmem_fd = fd;
    }
#endif

    peekbuf.pid = target;

#ifdef HAVE_PROCESS_VM_READV
    /* re-arm each attach, and let the old path be forced without a rebuild so
     * the two backends can be diffed against each other */
    vm_readv_ok = (getenv("SCANMEM_NO_PROCESS_VM_READV") == NULL);
#endif

    /* everything looks okay */
    return true;

}

bool sm_detach(pid_t target)
{
#if HAVE_PROCMEM
    /* close the mem file before detaching */
    close(peekbuf.procmem_fd);
#endif

    if (!sm_globals.options.no_ptrace)
    {
        /* addr is ignored on Linux, but should be 1 on FreeBSD in order to let
        * the child process continue execution where it had been interrupted */
        return ptrace(PTRACE_DETACH, target, 1, 0) == 0;
    }
    else
    {
        return true;
    }
}


/* Reads data from the target process, and places it on the `dest_buffer`
 * using either `ptrace` or `pread` on `/proc/pid/mem`.
 * The target process is not passed, but read from the static peekbuf.
 * `sm_attach()` MUST be called before this function. */
#ifdef HAVE_PROCESS_VM_READV
/* process_vm_readv copies straight between address spaces, no /proc file and
 * no VFS layer in the way. Measured against pread on the same 64KB sequential
 * pattern it runs about 2.8x faster (5.3 GB/s vs 14.7 GB/s here).
 *
 * It is not always permitted: seccomp sandboxes and some container policies
 * answer ENOSYS or EPERM. Give up the first time that happens and use the
 * older path for the rest of the session rather than paying a failing syscall
 * per read. A short read is not a refusal, that just means we reached the end
 * of the mapping, same as pread. */
static inline size_t readmemory_vm(uint8_t *dest_buffer,
                                   const char *target_address, size_t size)
{
    size_t nread = 0;

    while (nread < size) {
        struct iovec local = { dest_buffer + nread, size - nread };
        struct iovec remote = { (void *)(target_address + nread), size - nread };
        ssize_t ret = process_vm_readv(peekbuf.pid, &local, 1, &remote, 1, 0);

        if (ret <= 0) {
            if (nread == 0 && (errno == ENOSYS || errno == EPERM))
                vm_readv_ok = false;
            break;
        }
        nread += ret;
    }

    return nread;
}
#endif

static inline size_t readmemory(uint8_t *dest_buffer, const char *target_address, size_t size)
{
    size_t nread = 0;

#ifdef HAVE_PROCESS_VM_READV
    if (LIKELY(vm_readv_ok)) {
        nread = readmemory_vm(dest_buffer, target_address, size);
        if (LIKELY(vm_readv_ok))
            return nread;
        /* refused, so it is not usable here at all. drop through and let the
         * build's normal backend answer this read */
        nread = 0;
    }
#endif

#if HAVE_PROCMEM
    do {
        ssize_t ret = pread(peekbuf.procmem_fd, dest_buffer + nread,
                            size - nread, (unsigned long)(target_address + nread));
        if (ret == -1) {
            /* we can't read further, report what was read */
            return nread;
        }
        else {
            /* some data was read */
            nread += ret;
        }
    } while (nread < size);
#else
    /* Read the memory with `ptrace()`: the API specifies that `ptrace()` returns a `long`, which
     * is the size of a word for the current architecture, so this section will deal in `long`s */
    assert(size % sizeof(long) == 0);
    errno = 0;
    for (nread = 0; nread < size; nread += sizeof(long)) {
        const char *ptrace_address = target_address + nread;
        long ptraced_long = ptrace(PTRACE_PEEKDATA, peekbuf.pid, ptrace_address, NULL);

        /* check if ptrace() succeeded */
        if (UNLIKELY(ptraced_long == -1L && errno != 0)) {
            /* it's possible i'm trying to read partially oob */
            if (errno == EIO || errno == EFAULT) {
                int j;
                /* read backwards until we get a good read, then shift out the right value */
                for (j = 1, errno = 0; j < sizeof(long); j++, errno = 0) {
                    /* try for a shifted ptrace - 'continue' (i.e. try an increased shift) if it fails */
                    ptraced_long = ptrace(PTRACE_PEEKDATA, peekbuf.pid, ptrace_address - j, NULL);
                    if ((ptraced_long == -1L) && (errno == EIO || errno == EFAULT))
                        continue;

                    /* store it with the appropriate offset */
                    uint8_t* new_memory_ptr = (uint8_t*)(&ptraced_long) + j;
                    memcpy(dest_buffer + nread, new_memory_ptr, sizeof(long) - j);
                    nread += sizeof(long) - j;

                    /* interrupt the partial gathering process */
                    break;
                }
            }
            /* interrupt the gathering process */
            break;
        }
        /* otherwise, ptrace() worked - store the data */
        memcpy(dest_buffer + nread, &ptraced_long, sizeof(long));
    }
#endif
    return nread;
}

/*
 * sm_peekdata - fills the peekbuf cache with memory from the process
 * 
 * This routine calls either `ptrace(PEEKDATA, ...)` or `pread(...)`,
 * and fills the peekbuf cache, to make a local mirror of the process memory we're interested in.
 * `sm_attach()` MUST be called before this function.
 */

extern inline bool sm_peekdata(const void *addr, uint16_t length, const mem64_t **result_ptr, size_t *memlength)
{
    const char *reqaddr = addr;
    unsigned int missing_bytes;

    assert(peekbuf.size <= MAX_PEEKBUF_SIZE);
    assert(result_ptr != NULL);
    assert(memlength != NULL);

    /* check if we have a full cache hit */
    if (peekbuf.base != NULL &&
        reqaddr >= peekbuf.base &&
        (unsigned long) (reqaddr + length - peekbuf.base) <= peekbuf.size)
    {
        *result_ptr = (mem64_t*)&peekbuf.cache[reqaddr - peekbuf.base];
        *memlength = peekbuf.base - reqaddr + peekbuf.size;
        return true;
    }
    else if (peekbuf.base != NULL &&
             reqaddr >= peekbuf.base &&
             (unsigned long) (reqaddr - peekbuf.base) < peekbuf.size)
    {
        assert(peekbuf.size != 0);

        /* partial hit, we have some of the data but not all, so remove old entries - shift the frame by as far as is necessary */
        missing_bytes = (reqaddr + length) - (peekbuf.base + peekbuf.size);
        /* round up to the nearest PEEKDATA_CHUNK multiple, that is what could
         * potentially be read and we have to fit it all */
        missing_bytes = PEEKDATA_CHUNK * (1 + (missing_bytes-1) / PEEKDATA_CHUNK);

        /* Head shift if necessary. Also shift when the cache is too full to
         * take a whole readahead window, otherwise steady state leaves only
         * the few bytes just consumed free and every refill degenerates back
         * to a single chunk read. */
        if (peekbuf.size + missing_bytes > MAX_PEEKBUF_SIZE ||
            peekbuf.size + peekbuf.readahead > MAX_PEEKBUF_SIZE)
        {
            unsigned int shift_size = reqaddr - peekbuf.base;
            shift_size = PEEKDATA_CHUNK * (shift_size / PEEKDATA_CHUNK);

            memmove(peekbuf.cache, &peekbuf.cache[shift_size], peekbuf.size-shift_size);

            peekbuf.size -= shift_size;
            peekbuf.base += shift_size;
        }
    }
    else {
        /* Cache miss, invalidate the cache.
         *
         * A forward walk lands exactly on base+size every time it runs off the
         * end of the window, which reaches here rather than the partial hit
         * branch above. That is still sequential access, so only treat a real
         * jump as a reason to shrink the readahead back down. */
        const char *cached_end = peekbuf.base ? peekbuf.base + peekbuf.size : NULL;
        if (reqaddr != cached_end)
            peekbuf.readahead = PEEKDATA_CHUNK;

        missing_bytes = length;
        peekbuf.size = 0;
        peekbuf.base = reqaddr;
    }

    /* we need to retrieve memory to complete the request.
     *
     * this used to loop issuing one PEEKDATA_CHUNK sized read at a time, which
     * cost a syscall per 2KB no matter how much of the region we were about to
     * walk through. ask for the readahead window in a single call instead, and
     * let it grow while the scan keeps moving forwards. */
    if (peekbuf.readahead < PEEKDATA_CHUNK)
        peekbuf.readahead = PEEKDATA_CHUNK;

    size_t want = missing_bytes > peekbuf.readahead ? missing_bytes : peekbuf.readahead;

    /* keep it a whole number of chunks, the ptrace fallback reads in words */
    want = PEEKDATA_CHUNK * (1 + (want - 1) / PEEKDATA_CHUNK);

    if (peekbuf.size + want > MAX_PEEKBUF_SIZE)
        want = MAX_PEEKBUF_SIZE - peekbuf.size;

    /* the shift above guarantees there is room for what was actually asked for */
    if (want < missing_bytes)
        want = missing_bytes;

    if (want > 0)
    {
        const char *target_address = peekbuf.base + peekbuf.size;
        size_t len = readmemory(&peekbuf.cache[peekbuf.size], target_address, want);

        if (UNLIKELY(len < missing_bytes)) {
            if (len == 0) {
                /* hard failure to retrieve memory */
                *result_ptr = NULL;
                *memlength = 0;
                return false;
            }
            /* partial read, most likely we ran into the end of the region.
             * keep what we got and stop reaching further ahead */
            peekbuf.size += len;
            peekbuf.readahead = PEEKDATA_CHUNK;
            *result_ptr = (mem64_t*)&peekbuf.cache[reqaddr - peekbuf.base];
            *memlength = peekbuf.base - reqaddr + peekbuf.size;
            return true;
        }

        peekbuf.size += len;

        /* that worked, so reach a bit further next time */
        if (peekbuf.readahead < READAHEAD_MAX)
            peekbuf.readahead *= 2;
    }

    /* return result to caller */
    *result_ptr = (mem64_t*)&peekbuf.cache[reqaddr - peekbuf.base];
    *memlength = peekbuf.base - reqaddr + peekbuf.size;
    return true;
}

/* The scan loop calls this once per byte, and once a window is filled nearly
   every one of those is a plain cache hit. sm_peekdata() is too big for gcc to
   inline, so the hit was costing a real call each time. Do the hit test here
   and only call out when the window actually has to move. */
static inline bool peekdata_cached(const void *addr, uint16_t length,
                                   const mem64_t **result_ptr, size_t *memlength)
{
    const char *reqaddr = addr;

    if (LIKELY(peekbuf.base != NULL && reqaddr >= peekbuf.base &&
               (unsigned long)(reqaddr + length - peekbuf.base) <= peekbuf.size))
    {
        *result_ptr = (mem64_t *)&peekbuf.cache[reqaddr - peekbuf.base];
        *memlength = peekbuf.base - reqaddr + peekbuf.size;
        return true;
    }

    return sm_peekdata(addr, length, result_ptr, memlength);
}

static inline void print_a_dot(void)
{
    fprintf(stderr, ".");
    fflush(stderr);
}

static inline uint16_t flags_to_memlength(scan_data_type_t scan_data_type, match_flags flags)
{
    switch(scan_data_type)
    {
        case BYTEARRAY:
        case STRING:
            return flags;
            break;
        default: /* numbers */
                 if (flags & flags_64b) return 8;
            else if (flags & flags_32b) return 4;
            else if (flags & flags_16b) return 2;
            else if (flags & flags_8b ) return 1;
            else    /* it can't be a variable of any size */ return 0;
            break;
    }
}

/* This is the function that handles when you enter a value (or >, <, =) for the second or later time (i.e. when there's already a list of matches);
 * it reduces the list to those that still match. It returns false on failure to attach, detach, or reallocate memory, otherwise true. */
static bool checkmatches_impl(globals_t *vars,
                              scan_match_type_t match_type,
                              const uservalue_t *uservalue)
{
    matches_and_old_values_swath *reading_swath_index = vars->matches->swaths;
    matches_and_old_values_swath reading_swath = *reading_swath_index;

    unsigned long bytes_scanned = 0;
    unsigned long total_scan_bytes = 0;
    matches_and_old_values_swath *tmp_swath_index = reading_swath_index;
    unsigned int samples_remaining = NUM_SAMPLES;
    unsigned int samples_to_dot = SAMPLES_PER_DOT;
    size_t bytes_at_next_sample;
    size_t bytes_per_sample;

    if (sm_choose_scanroutine(vars->options.scan_data_type, match_type, uservalue, vars->options.reverse_endianness) == false)
    {
        show_error("unsupported scan for current data type.\n");
        return false;
    }

    assert(sm_scan_routine);

    while(tmp_swath_index->number_of_bytes)
    {
        total_scan_bytes += tmp_swath_index->number_of_bytes;
        tmp_swath_index = (matches_and_old_values_swath *)(&tmp_swath_index->data[tmp_swath_index->number_of_bytes]);
    }
    bytes_per_sample = total_scan_bytes / NUM_SAMPLES;
    bytes_at_next_sample = bytes_per_sample;
    /* for user, just print the first dot */
    print_a_dot();

    size_t reading_iterator = 0;
    matches_and_old_values_swath *writing_swath_index = vars->matches->swaths;
    matches_and_old_values_swath *new_swath;
    writing_swath_index->first_byte_in_child = NULL;
    writing_swath_index->number_of_bytes = 0;

    int required_extra_bytes_to_record = 0;
    vars->num_matches = 0;
    vars->scan_progress = 0.0;
    vars->stop_flag = false;

    /* stop and attach to the target */
    if (sm_attach(vars->target) == false)
        return false;

    INTERRUPTABLESCAN();

    while (reading_swath.first_byte_in_child) {
        unsigned int match_length = 0;
        const mem64_t *memory_ptr;
        size_t memlength;
        match_flags checkflags;

        match_flags old_flags = reading_swath_index->data[reading_iterator].match_info;
        unsigned int old_length = flags_to_memlength(vars->options.scan_data_type, old_flags);
        void *address = reading_swath.first_byte_in_child + reading_iterator;

        /* read value from this address */
        if (UNLIKELY(peekdata_cached(address, old_length, &memory_ptr, &memlength) == false))
        {
            /* If we can't look at the data here, just abort the whole recording, something bad happened */
            required_extra_bytes_to_record = 0;
        }
        else if (old_flags != flags_empty) /* Test only valid old matches */
        {
            value_t old_val = data_to_val_aux(reading_swath_index, reading_iterator, reading_swath.number_of_bytes);
            memlength = old_length < memlength ? old_length : memlength;

            checkflags = flags_empty;

            match_length = (*sm_scan_routine)(memory_ptr, memlength, &old_val, uservalue, &checkflags);
        }

        if (match_length > 0)
        {
            assert(match_length <= memlength);

            /* Still a candidate. Write data.
               - We can get away with overwriting in the same array because it is guaranteed to take up the same number of bytes or fewer,
                 and because we copied out the reading swath metadata already.
               - We can get away with assuming that the pointers will stay valid,
                 because as we never add more data to the array than there was before, it will not reallocate. */

            new_swath = add_element_fast(&(vars->matches), writing_swath_index, address,
                                              get_u8b(memory_ptr), checkflags);
            if (UNLIKELY(new_swath == NULL))
                goto oom;
            writing_swath_index = new_swath;

            ++vars->num_matches;

            required_extra_bytes_to_record = match_length - 1;
        }
        else if (required_extra_bytes_to_record)
        {
            new_swath = add_element_fast(&(vars->matches), writing_swath_index, address,
                                              get_u8b(memory_ptr), flags_empty);
            if (UNLIKELY(new_swath == NULL))
                goto oom;
            writing_swath_index = new_swath;
            --required_extra_bytes_to_record;
        }

        if (UNLIKELY(bytes_scanned >= bytes_at_next_sample)) {
            bytes_at_next_sample += bytes_per_sample;
            /* handle rounding */
            if (LIKELY(--samples_remaining > 0)) {
                /* for front-end, update percentage */
                vars->scan_progress += PROGRESS_PER_SAMPLE;
                if (UNLIKELY(--samples_to_dot == 0)) {
                    samples_to_dot = SAMPLES_PER_DOT;
                    /* for user, just print a dot */
                    print_a_dot();
                }
                /* stop scanning if asked to */
                if (vars->stop_flag) {
                    printf("\n");
                    break;
                }
            }
        }
        ++bytes_scanned;
        
        /* go on to the next one... */
        ++reading_iterator;
        if (reading_iterator >= reading_swath.number_of_bytes)
        {
            reading_swath_index = (matches_and_old_values_swath *)
                (&reading_swath_index->data[reading_swath.number_of_bytes]);
            reading_swath = *reading_swath_index;
            reading_iterator = 0;
            required_extra_bytes_to_record = 0; /* just in case */
        }
    }

    ENDINTERRUPTABLE();

    if (!(vars->matches = null_terminate(vars->matches, writing_swath_index)))
    {
        show_error("memory allocation error while reducing matches-array size\n");
        return false;
    }


    show_user("ok\n");

    /* tell front-end we've done */
    vars->scan_progress = MAX_PROGRESS;

    show_info("we currently have %ld matches.\n", vars->num_matches);

    /* okay, detach */
    return sm_detach(vars->target);

oom:
    /* the array is untouched and still ours, we just cannot record any more
       into it. bail rather than write through the NULL, which is what used to
       segfault on the next element (#307). */
    ENDINTERRUPTABLE();
    show_error("out of memory recording matches, the match list is unchanged.\n");
    sm_detach(vars->target);
    return false;
}


/* ---- parallel initial scan -------------------------------------------------
 *
 * Nearly all of the initial scan is the scan routine running at every byte
 * offset, and no offset depends on another one, so it splits across threads
 * cleanly. Emitting the records is the part that does not split: a match of
 * length L is followed by L-1 filler entries, so a chunk boundary can land in
 * the middle of one.
 *
 * A chunk works out its own carry by rescanning the L-1 bytes behind it. Only
 * the last match before the boundary can still be carrying, since a match
 * resets the counter rather than adding to it, so that window is enough. For
 * number scans it is 7 bytes.
 *
 * Each chunk records into its own buffer and a serial pass stitches them
 * together in address order through the same add_element path the old serial
 * loop used, so the swath layout is unchanged. That pass is O(records), not
 * O(bytes), so it stays out of the way on a normal scan.
 */

/* Chunk size is worked out from the target, see below. Defining
 * SCAN_CHUNK_BYTES pins it instead, which is how the tests force a tiny chunk
 * and hammer the boundary carry that otherwise only gets hit by luck. */
#define SCAN_CHUNK_MIN (1<<18)
#define SCAN_CHUNK_MAX (1<<22)
/* aim for a few chunks each so a slow one cannot leave threads idle at the end */
#define SCAN_CHUNKS_PER_THREAD 4

/* what one recorded byte looks like on the way back to the merge. keeping
 * these packed rather than a flag per scanned byte is what keeps the merge
 * O(matches) instead of O(bytes), which matters because the merge is the one
 * part that cannot be run in parallel */
typedef struct {
    uint32_t off;                      /* offset within the owned range */
    uint8_t old_value;
    match_flags match_info;
} scan_record;

typedef struct {
    region_t *region;
    size_t offset;                     /* owned range start, from region start */
    size_t owned;                      /* bytes this chunk is responsible for */
    const uservalue_t *uservalue;
    size_t maxlen;                     /* longest match the routine can return */
    size_t align;                      /* only test addresses that are a multiple of this */

    scan_record *recs;                 /* what to hand to add_element, in order */
    size_t nrecs;
    uint8_t *buf;
    size_t bufsize;
    unsigned long matches;
    bool read_failed;
} scan_chunk;

/* How long a match can be. That is both how far back a carry can reach and how
 * far past the owned range the routine may read. For bytearray and string the
 * length is what is kept in the flags. */
static size_t scan_max_match_length(scan_data_type_t type, const uservalue_t *uv)
{
    switch (type) {
        case BYTEARRAY:
        case STRING:
            return uv->flags ? (size_t)uv->flags : 1;
        default:
            return sizeof(uint64_t);
    }
}

static void scan_chunk_run(scan_chunk *c)
{
    region_t *r = c->region;
    size_t back = c->maxlen > 0 ? c->maxlen - 1 : 0;
    size_t start = c->offset > back ? c->offset - back : 0;
    size_t head = c->offset - start;
    size_t want = head + c->owned + c->maxlen;
    size_t nread, valid_len, required, j;

    if (start + want > r->size)
        want = r->size - start;
    if (want > c->bufsize)
        want = c->bufsize;

    nread = readmemory(c->buf, (char *)r->start + start, want);
    if (nread == 0) {
        c->read_failed = true;
        return;
    }

    /* a short read means the region really ends there */
    valid_len = (nread < want) ? start + nread : r->size;

    /* replay the bytes behind us just far enough to see what is still carrying */
    required = 0;
    for (j = 0; j < head; j++) {
        size_t p = start + j;
        match_flags f = flags_empty;
        unsigned int ml;

        if (p >= valid_len)
            break;

        ml = (*sm_scan_routine)((const mem64_t *)(c->buf + j), valid_len - p,
                                NULL, c->uservalue, &f);
        if (ml > 0)
            required = (p + ml > c->offset) ? p + ml - c->offset : 0;
    }

    /* hoist the end of region check out of the loop and let `remaining` count
     * down, the way the serial scan did. Doing it per byte costs a compare and
     * a subtract on every offset, which is measurable over a big region */
    {
    size_t limit = c->owned;
    size_t remaining;
    const uint8_t *mp8 = c->buf + head;
    /* alignment is about where the variable sits in the target, so it has to
     * be measured on the target address, not on our offset into the buffer */
    uintptr_t addr = (uintptr_t)r->start + c->offset;
    uintptr_t alignmask = c->align > 1 ? (uintptr_t)c->align - 1 : 0;

    if (c->offset >= valid_len)
        limit = 0;
    else if (valid_len - c->offset < limit)
        limit = valid_len - c->offset;

    remaining = valid_len - c->offset;

    for (j = 0; j < limit; j++, remaining--, mp8++, addr++) {
        const mem64_t *mp = (const mem64_t *)mp8;
        match_flags f = flags_empty;
        unsigned int ml;

        /* skipping the routine is the whole point of the option, but the
         * bytes a match owns still have to be recorded below, old values are
         * kept per byte and a wide match needs all of its own */
        if (alignmask && (addr & alignmask))
            ml = 0;
        else
            ml = (*sm_scan_routine)(mp, remaining, NULL, c->uservalue, &f);

        if (UNLIKELY(ml > 0)) {
            c->recs[c->nrecs].off = (uint32_t)j;
            c->recs[c->nrecs].old_value = get_u8b(mp);
            c->recs[c->nrecs].match_info = f;
            c->nrecs++;
            c->matches++;
            required = ml - 1;
        }
        else if (required) {
            c->recs[c->nrecs].off = (uint32_t)j;
            c->recs[c->nrecs].old_value = get_u8b(mp);
            c->recs[c->nrecs].match_info = flags_empty;
            c->nrecs++;
            required--;
        }
    }
    }
}

#ifdef HAVE_PTHREAD
static void *scan_chunk_thread(void *arg)
{
    scan_chunk_run((scan_chunk *)arg);
    return NULL;
}
#endif

static unsigned scan_thread_count(const globals_t *vars)
{
    long n;

    if (vars->options.threads > 0)
        return vars->options.threads;

#ifdef HAVE_PTHREAD
    n = sysconf(_SC_NPROCESSORS_ONLN);
    if (n < 1)
        n = 1;
    /* past this the serial stitch is the limit, not the scanning */
    if (n > 32)
        n = 32;
    return (unsigned)n;
#else
    (void)n;
    return 1;
#endif
}

/* Thin wrapper so scan_in_progress is set on every path out, including the
   early returns. sm_reset() refuses to free the matches while it is set. */
bool sm_checkmatches(globals_t *vars,
                     scan_match_type_t match_type,
                     const uservalue_t *uservalue)
{
    bool ret;

    /* snapshot before we narrow, so undo restores the pre-scan set */
    sm_history_record();
    vars->scan_in_progress = true;
    ret = checkmatches_impl(vars, match_type, uservalue);
    vars->scan_in_progress = false;
    return ret;
}

/* sm_searchregions() performs an initial search of the process for values matching `uservalue` */
static bool searchregions_impl(globals_t *vars, scan_match_type_t match_type, const uservalue_t *uservalue)
{
    matches_and_old_values_swath *writing_swath_index;
    unsigned long total_size = 0;
    element_t *n = vars->regions->head;
    region_t *r;
    unsigned long total_scan_bytes = 0;
    scan_chunk *chunks = NULL;
    size_t maxlen, bufsize, chunk_bytes, align;
    unsigned nthreads, t;
    bool ok = true;
    unsigned long done_bytes = 0;
    unsigned samples_to_dot = SAMPLES_PER_DOT;

    if (sm_choose_scanroutine(vars->options.scan_data_type, match_type, uservalue, vars->options.reverse_endianness) == false)
    {
        show_error("unsupported scan for current data type.\n");
        return false;
    }

    assert(sm_scan_routine);

    /* stop and attach to the target */
    if (sm_attach(vars->target) == false)
        return false;

    /* make sure we have some regions to search */
    if (vars->regions->size == 0) {
        show_warn("no regions defined, perhaps you deleted them all?\n");
        show_info("use the \"reset\" command to refresh regions.\n");
        return sm_detach(vars->target);
    }

    INTERRUPTABLESCAN();

    total_size = sizeof(matches_and_old_values_array);

    while (n) {
        total_size += ((region_t *)(n->data))->size * sizeof(old_value_and_match_info) + sizeof(matches_and_old_values_swath);
        n = n->next;
    }

    total_size += sizeof(matches_and_old_values_swath); /* for null terminate */

    show_debug("allocate array, max size %ld\n", total_size);

    if (!(vars->matches = allocate_array(vars->matches, total_size)))
    {
        show_error("could not allocate match array\n");
        return false;
    }

    writing_swath_index = vars->matches->swaths;
    writing_swath_index->first_byte_in_child = NULL;
    writing_swath_index->number_of_bytes = 0;

    for (n = vars->regions->head; n; n = n->next)
        total_scan_bytes += ((region_t *)n->data)->size;

    vars->scan_progress = 0.0;
    vars->stop_flag = false;

    maxlen = scan_max_match_length(vars->options.scan_data_type, uservalue);
    /* 0 would be a broken option value, treat anything odd as every byte */
    align = vars->options.alignment > 1 ? vars->options.alignment : 1;
    nthreads = scan_thread_count(vars);

#ifdef SCAN_CHUNK_BYTES
    chunk_bytes = SCAN_CHUNK_BYTES;
#else
    {
        /* Testing hook. A boundary straddling match is the one thing the
         * chunking can get wrong, and at the normal chunk size you only hit
         * one by luck, so the suite turns this down to force them. */
        const char *env = getenv("SCANMEM_SCAN_CHUNK_BYTES");

        if (env && *env) {
            chunk_bytes = strtoul(env, NULL, 10);
            if (chunk_bytes < 64)
                chunk_bytes = 64;
        }
        else {
            /* Big enough that the per chunk work disappears, small enough that
             * a small target still has something for every thread. */
            chunk_bytes = total_scan_bytes / (nthreads * SCAN_CHUNKS_PER_THREAD);
            if (chunk_bytes < SCAN_CHUNK_MIN)
                chunk_bytes = SCAN_CHUNK_MIN;
            if (chunk_bytes > SCAN_CHUNK_MAX)
                chunk_bytes = SCAN_CHUNK_MAX;
        }
    }
#endif

    /* One descriptor per thread, refilled as the walk goes. Building one for
     * every slice up front would mean a serial pass and a lot of memory before
     * any scanning starts, and a process with a big mapped address space has a
     * very large number of slices. */
    chunks = calloc(nthreads, sizeof(*chunks));
    if (!chunks) {
        show_error("sorry, there was a memory allocation error.\n");
        return false;
    }

    bufsize = maxlen + chunk_bytes + maxlen;
    {
        uint8_t **bufs = calloc(nthreads, sizeof(*bufs));
        scan_record **recs = calloc(nthreads, sizeof(*recs));
#ifdef HAVE_PTHREAD
        pthread_t *tids = calloc(nthreads, sizeof(*tids));
#endif
        size_t cur_off = 0;
        region_t *dead_region = NULL;

        if (!bufs || !recs
#ifdef HAVE_PTHREAD
            || !tids
#endif
           ) {
            show_error("sorry, there was a memory allocation error.\n");
            ok = false;
        }

        for (t = 0; ok && t < nthreads; t++) {
            bufs[t] = malloc(bufsize);
            recs[t] = malloc(chunk_bytes * sizeof(**recs));
            if (!bufs[t] || !recs[t]) {
                show_error("sorry, there was a memory allocation error.\n");
                ok = false;
            }
        }

        show_user("searching %lu regions over %u threads",
                  vars->regions->size, nthreads);
        print_a_dot();

        n = vars->regions->head;

        while (ok) {
            size_t batch = 0;
            size_t k;

            /* hand out the next slices */
            while (batch < nthreads && n) {
                r = n->data;
                if (cur_off >= r->size) {
                    n = n->next;
                    cur_off = 0;
                    continue;
                }
                memset(&chunks[batch], 0, sizeof(chunks[batch]));
                chunks[batch].region = r;
                chunks[batch].offset = cur_off;
                chunks[batch].owned = MIN(chunk_bytes, r->size - cur_off);
                chunks[batch].uservalue = uservalue;
                chunks[batch].maxlen = maxlen;
                chunks[batch].align = align;
                cur_off += chunks[batch].owned;
                batch++;
            }

            if (batch == 0)
                break;

            for (k = 0; k < batch; k++) {
                scan_chunk *c = &chunks[k];
                c->buf = bufs[k];
                c->bufsize = bufsize;
                c->recs = recs[k];
                c->nrecs = 0;
                c->matches = 0;
                c->read_failed = false;
            }

#ifdef HAVE_PTHREAD
            for (k = 1; k < batch; k++) {
                if (pthread_create(&tids[k], NULL, scan_chunk_thread, &chunks[k]) != 0) {
                    /* out of threads, just do it here */
                    scan_chunk_run(&chunks[k]);
                    tids[k] = 0;
                }
            }
#endif
            /* this thread takes the first one instead of sitting idle */
            scan_chunk_run(&chunks[0]);

#ifdef HAVE_PTHREAD
            for (k = 1; k < batch; k++) {
                if (tids[k])
                    pthread_join(tids[k], NULL);
            }
#else
            for (k = 1; k < batch; k++)
                scan_chunk_run(&chunks[k]);
#endif

            /* stitch this batch in address order, same call sequence the
             * serial scan made, so the swaths come out the same */
            for (k = 0; k < batch; k++) {
                scan_chunk *c = &chunks[k];
                size_t j;

                /* A region that will not read at all is not going to start
                 * working further in, so drop the rest of it. The serial scan
                 * broke out of the region here too. Without this a big
                 * unreadable mapping costs a failed read for every chunk in
                 * it, and there can be an enormous number of those. */
                if (c->read_failed) {
                    if (dead_region != c->region) {
                        dead_region = c->region;
                        show_warn("reading a region failed.\n");
                    }
                    continue;
                }
                if (dead_region == c->region)
                    continue;

                for (j = 0; j < c->nrecs; j++) {
                    matches_and_old_values_swath *new_swath;

                    new_swath = add_element_fast(&(vars->matches),
                            writing_swath_index,
                            (char *)c->region->start + c->offset + c->recs[j].off,
                            c->recs[j].old_value, c->recs[j].match_info);
                    /* this is where #307 crashed: the array stops growing,
                       add_element handed back NULL, and the next element
                       dereferenced it. stop instead. */
                    if (UNLIKELY(new_swath == NULL)) {
                        show_error("out of memory recording matches, giving up "
                                   "on this scan.\n");
                        ok = false;
                        break;
                    }
                    writing_swath_index = new_swath;
                }
                if (!ok)
                    break;
                vars->num_matches += c->matches;
                done_bytes += c->owned;

                vars->scan_progress = total_scan_bytes
                        ? (double)done_bytes / total_scan_bytes : MAX_PROGRESS;
                if (--samples_to_dot == 0) {
                    samples_to_dot = SAMPLES_PER_DOT;
                    print_a_dot();
                }
            }

            if (!ok)
                break;

            /* skip whatever is left of a region that would not read */
            if (dead_region && n && (region_t *)n->data == dead_region) {
                n = n->next;
                cur_off = 0;
            }

            if (vars->stop_flag) {
                printf("\n");
                break;
            }
        }

        for (t = 0; t < nthreads; t++) {
            if (bufs) free(bufs[t]);
            if (recs) free(recs[t]);
        }
        free(bufs);
        free(recs);
#ifdef HAVE_PTHREAD
        free(tids);
#endif
    }

    free(chunks);
    (void)done_bytes;

    ENDINTERRUPTABLE();

    if (!ok)
        return false;

    show_user("ok\n");

    /* tell front-end we've finished */
    vars->scan_progress = MAX_PROGRESS;

    if (!(vars->matches = null_terminate(vars->matches, writing_swath_index)))
    {
        show_error("memory allocation error while reducing matches-array size\n");
        return false;
    }

    show_info("we currently have %ld matches.\n", vars->num_matches);

    /* okay, detach */
    return sm_detach(vars->target);
}

/* see the note on sm_checkmatches above */
bool sm_searchregions(globals_t *vars, scan_match_type_t match_type, const uservalue_t *uservalue)
{
    bool ret;

    /* snapshot before we narrow, so undo restores the pre-scan set */
    sm_history_record();
    vars->scan_in_progress = true;
    ret = searchregions_impl(vars, match_type, uservalue);
    vars->scan_in_progress = false;
    return ret;
}

/* Needs to support only ANYNUMBER types */
bool sm_setaddr(pid_t target, void *addr, const value_t *to)
{
    unsigned int i;
    uint8_t memarray[sizeof(uint64_t)] = {0};
    size_t memlength;

    if (sm_attach(target) == false) {
        return false;
    }

    memlength = readmemory(memarray, addr, sizeof(uint64_t));
    if (memlength == 0) {
        show_error("couldn't access the target address %10p\n", addr);
        return false;
    }

    unsigned int val_length = flags_to_memlength(ANYNUMBER, to->flags);
    if (val_length > 0) {
        /* Basically, overwrite as much of the data as makes sense, and no more. */
        memcpy(memarray, to->bytes, val_length);
    }
    else {
        show_error("could not determine type to poke.\n");
        return false;
    }

    if (sm_globals.options.no_ptrace)
    {
#if HAVE_PROCMEM
        if (pwrite(peekbuf.procmem_fd, memarray, sizeof(uint64_t), (long)addr) == -1)
        {
            return false;
        }
#else
        return false;
#endif
    }
    else
    {
        /* Assume `sizeof(uint64_t)` is a multiple of `sizeof(long)` */
        for (i = 0; i < sizeof(uint64_t); i += sizeof(long))
        {
            if (ptrace(PTRACE_POKEDATA, target, addr + i, *(long*)(memarray + i)) == -1L) {
                return false;
            }
        }
    }

    return sm_detach(target);
}

bool sm_read_array(pid_t target, const void *addr, void *buf, size_t len)
{
    if (sm_attach(target) == false) {
        return false;
    }

    size_t nread = readmemory(buf, addr, len);
    if (nread < len)
    {
        sm_detach(target);
        return false;
    }

    return sm_detach(target);
}

/* TODO: may use /proc/<pid>/mem here */
bool sm_write_array(pid_t target, void *addr, const void *data, size_t len)
{
    unsigned int i,j;
    long peek_value;

    if (sm_attach(target) == false) {
        return false;
    }

    if (sm_globals.options.no_ptrace)
    {
#if HAVE_PROCMEM
        if (pwrite(peekbuf.procmem_fd, data, len, (long)addr) == -1)
        {
            return false;
        }
#else
        return false;
#endif
    }
    else
    {
        for (i = 0; i + sizeof(long) < len; i += sizeof(long))
        {
            if (ptrace(PTRACE_POKEDATA, target, addr + i, *(long *)(data + i)) == -1L) {
                return false;
            }
        }

        if (len - i > 0) /* something left (shorter than a long) */
        {
            if (len > sizeof(long)) /* rewrite last sizeof(long) bytes of the buffer */
            {
                if (ptrace(PTRACE_POKEDATA, target, addr + len - sizeof(long), *(long *)(data + len - sizeof(long))) == -1L) {
                    return false;
                }
            }
            else /* we have to play with bits... */
            {
                /* try all possible shifting read and write */
                for(j = 0; j <= sizeof(long) - (len - i); ++j)
                {
                    errno = 0;
                    if(((peek_value = ptrace(PTRACE_PEEKDATA, target, addr - j, NULL)) == -1L) && (errno != 0))
                    {
                        if (errno == EIO || errno == EFAULT) /* may try next shift */
                            continue;
                        else
                        {
                            show_error("%s failed.\n", __func__);
                            return false;
                        }
                    }
                    else /* peek success */
                    {
                        /* write back */
                        memcpy(((int8_t*)&peek_value)+j, data+i, len-i);

                        if (ptrace(PTRACE_POKEDATA, target, addr - j, peek_value) == -1L)
                        {
                            show_error("%s failed.\n", __func__);
                            return false;
                        }

                        break;
                    }
                }
            }
        }
    }

    return sm_detach(target);
}
