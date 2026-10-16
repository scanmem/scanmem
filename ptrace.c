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
#include <stdbool.h>
#include <limits.h>
#include <fcntl.h>
#if HAVE_PROCMEM
#    include <pthread.h>
#    include <signal.h>
#    include <sys/uio.h>
#endif

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
static struct {
    uint8_t cache[MAX_PEEKBUF_SIZE];  /* read from ptrace()  */
    unsigned size;              /* amount of valid memory stored (in bytes) */
    const char *base;           /* base address of cached region */
#if HAVE_PROCMEM
    int procmem_fd;             /* file descriptor of the opened `/proc/<pid>/mem` file */
#else
    pid_t pid;                  /* pid of scanned process */
#endif
} peekbuf;


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
#else
    peekbuf.pid = target;
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
static inline size_t readmemory(uint8_t *dest_buffer, const char *target_address, size_t size)
{
    size_t nread = 0;

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
    unsigned int i;
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

        /* head shift if necessary */
        if (peekbuf.size + missing_bytes > MAX_PEEKBUF_SIZE)
        {
            unsigned int shift_size = reqaddr - peekbuf.base;
            shift_size = PEEKDATA_CHUNK * (shift_size / PEEKDATA_CHUNK);

            memmove(peekbuf.cache, &peekbuf.cache[shift_size], peekbuf.size-shift_size);

            peekbuf.size -= shift_size;
            peekbuf.base += shift_size;
        }
    }
    else {
        /* cache miss, invalidate the cache */
        missing_bytes = length;
        peekbuf.size = 0;
        peekbuf.base = reqaddr;
    }

    /* we need to retrieve memory to complete the request */
    for (i = 0; i < missing_bytes; i += PEEKDATA_CHUNK)
    {
        const char *target_address = peekbuf.base + peekbuf.size;
        size_t len = readmemory(&peekbuf.cache[peekbuf.size], target_address, PEEKDATA_CHUNK);

        /* check if the read succeeded */
        if (UNLIKELY(len < PEEKDATA_CHUNK)) {
            if (len == 0) {
                /* hard failure to retrieve memory */
                *result_ptr = NULL;
                *memlength = 0;
                return false;
            }
            /* go ahead with the partial read and stop the gathering process */
            peekbuf.size += len;
            break;
        }
        
        /* otherwise, the read worked */
        peekbuf.size += PEEKDATA_CHUNK;
    }

    /* return result to caller */
    *result_ptr = (mem64_t*)&peekbuf.cache[reqaddr - peekbuf.base];
    *memlength = peekbuf.base - reqaddr + peekbuf.size;
    return true;
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

/* A match of `match_length` bytes starts at the byte being recorded, and
 * `required` more bytes were still to be recorded before it: how many bytes
 * to record after it. A later match can be shorter than an earlier one it
 * starts inside, so the bytes the earlier one needs stay included; otherwise
 * its old value would end early, and later scans would read padding, or drop
 * its wider types, in place of its last bytes. */
static inline int bytes_to_record(int required, unsigned int match_length)
{
    int needed = (int) match_length - 1;
    return required - 1 > needed ? required - 1 : needed;
}

#if HAVE_PROCMEM
/* Chunked scans.
 *
 * Unless `option threads 1` asks for the original serial loops in
 * sm_searchregions() and sm_checkmatches(), both scans cut their work into
 * chunks, which worker threads (or the main thread, with one CPU) scan into
 * matches arrays of their own, and the main thread merges those into
 * `vars->matches` in address order. This needs /proc/<pid>/mem: `pread()` on
 * the shared fd works from any thread, while `PTRACE_PEEKDATA` only works from
 * the thread that attached. The chunked scans also skip, read and allocate
 * faster than the serial loops; the serial loops stay as the reference that
 * test/sm_test.sh compares them with.
 *
 * The merged array is exactly the one the serial loop builds. A match that
 * starts near the end of a chunk continues into the next one, so the chunk
 * keeps recording the way the serial loop would, for as many bytes as the
 * matches before its end still need: past those, the serial loop records the
 * same as if the next chunk were where it started, so the next chunk records
 * exactly what the serial loop does. The merge drops the overlap. */

#    define MAX_SCAN_THREADS 64
/* a match is at most UINT16_MAX bytes long */
#    define MAX_MATCH_LENGTH (1UL << 16)
/* how often a chunk reports progress and checks the stop flag */
#    define PROGRESS_STEP (1UL << 20)
/* positions a block routine call handles at most */
#    define BLOCK_POSITIONS 4096

typedef enum {
    CHUNK_PENDING,
    CHUNK_DONE,
    CHUNK_STOPPED, /* the stop flag was raised mid-chunk */
    CHUNK_FAILED   /* out of memory */
} chunk_state_t;

typedef struct chunked_scan chunked_scan_t;
struct chunked_scan {
    globals_t *vars;
    const uservalue_t *uservalue;
    void *chunks;
    chunk_state_t *states;
    size_t num_chunks;
    /* scan chunk `i` using a scratch buffer of `buf_size` bytes */
    chunk_state_t (*scan)(chunked_scan_t *cs, size_t i, uint8_t *buf);
    size_t buf_size;
    uint8_t *buf;                /* the main thread's scratch buffer */
    unsigned long total_units;   /* for progress: bytes or matches-array elements */
    unsigned long scanned_units; /* atomic */
    scan_match_type_t match_type;
    const prefilter_t *prefilter; /* initial search: positions worth testing */
    block_routine_t block;        /* sm_scan_routine for runs of positions, or NULL */
    bool no_vm_readv;             /* process_vm_readv() is not available */
    size_t chunk_array_bytes;     /* size of every chunk's matches array */
    /* chunk arrays to reuse, guarded by `lock` */
    matches_and_old_values_array *pool[3 * MAX_SCAN_THREADS + 2];
    unsigned pool_size;
    unsigned num_threads; /* 0 if none started: the main thread scans */
    pthread_t threads[MAX_SCAN_THREADS];
    size_t next_chunk;    /* next chunk a worker should take */
    size_t merged_chunks; /* chunks the main thread is done with */
    size_t window;        /* how far workers may run ahead of the merge */
    bool abort;
    pthread_mutex_t lock;
    pthread_cond_t chunk_done;   /* worker -> main thread */
    pthread_cond_t window_moved; /* main thread -> workers */
};

static unsigned scan_threads(const globals_t *vars)
{
    long n = vars->options.threads;

    if (n == 0) {
        n = sysconf(_SC_NPROCESSORS_ONLN);
        if (n < 1)
            n = 1;
    }
    return MIN(n, MAX_SCAN_THREADS);
}

/* Testing hook: SCANMEM_SCAN_CHUNK_BYTES sets how many bytes of target memory
 * a chunk covers, within [min, max]. A match crossing a chunk boundary is what
 * the chunking can get wrong, and at the normal sizes a small test target has
 * few boundaries; test/scan_tests.sh turns this down to get thousands. */
static size_t chunk_size_for_tests(size_t chunk_size, size_t min, size_t max)
{
    const char *env = util_getenv("SCANMEM_SCAN_CHUNK_BYTES");
    if (env && *env) {
        chunk_size = strtoul(env, NULL, 10);
        if (chunk_size < min)
            chunk_size = min;
        if (chunk_size > max)
            chunk_size = max;
    }
    return chunk_size;
}

/* Called by a chunk scan every PROGRESS_STEP units. Returns true to stop. */
static inline bool report_progress(chunked_scan_t *cs, unsigned long units)
{
    __atomic_fetch_add(&cs->scanned_units, units, __ATOMIC_RELAXED);
    return cs->vars->stop_flag;
}

static void update_progress(chunked_scan_t *cs)
{
    /* for front-end, update percentage */
    if (cs->total_units)
        cs->vars->scan_progress = (double) __atomic_load_n(&cs->scanned_units, __ATOMIC_RELAXED) / cs->total_units;
}

static void *scan_worker(void *arg)
{
    chunked_scan_t *cs = arg;
    uint8_t *buf = malloc(cs->buf_size);

    for (;;) {
        pthread_mutex_lock(&cs->lock);
        size_t i = cs->next_chunk;
        if (i >= cs->num_chunks || cs->abort) {
            pthread_mutex_unlock(&cs->lock);
            break;
        }
        cs->next_chunk++;
        /* bound the memory held by chunks waiting to be merged */
        while (i >= cs->merged_chunks + cs->window && !cs->abort)
            pthread_cond_wait(&cs->window_moved, &cs->lock);
        bool abort = cs->abort;
        pthread_mutex_unlock(&cs->lock);
        if (abort)
            break;

        chunk_state_t state = buf ? cs->scan(cs, i, buf) : CHUNK_FAILED;

        /* publishing the state under the lock also publishes the results */
        pthread_mutex_lock(&cs->lock);
        cs->states[i] = state;
        pthread_cond_signal(&cs->chunk_done);
        pthread_mutex_unlock(&cs->lock);
    }

    free(buf);
    return NULL;
}

/* Start up to `nthreads` workers. With none, the main thread scans. */
static void start_scan_threads(chunked_scan_t *cs, unsigned nthreads)
{
    pthread_mutex_init(&cs->lock, NULL);
    pthread_cond_init(&cs->chunk_done, NULL);
    pthread_cond_init(&cs->window_moved, NULL);

    cs->num_threads = 0;
    nthreads = MIN(nthreads, cs->num_chunks);
    if (nthreads < 2)
        return;

    /* merging chunk j can wait for chunk j + 1, so this must be at least 2 */
    cs->window = 2 * nthreads;

    /* SIGINT must reach the main thread, which raises the stop flag. */
    sigset_t all, old;
    sigfillset(&all);
    pthread_sigmask(SIG_BLOCK, &all, &old);
    while (cs->num_threads < nthreads &&
           pthread_create(&cs->threads[cs->num_threads], NULL, scan_worker, cs) == 0)
        cs->num_threads++;
    pthread_sigmask(SIG_SETMASK, &old, NULL);
}

/* Wait until chunk `i` is scanned, or scan it now if there are no workers.
 * Returns CHUNK_PENDING if the scan was stopped before getting to it. */
static chunk_state_t wait_for_chunk(chunked_scan_t *cs, size_t i)
{
    if (cs->num_threads == 0) {
        if (cs->states[i] == CHUNK_PENDING && !cs->vars->stop_flag) {
            if (!cs->buf && !(cs->buf = malloc(cs->buf_size)))
                return CHUNK_FAILED;
            cs->states[i] = cs->scan(cs, i, cs->buf);
            update_progress(cs);
        }
        return cs->states[i];
    }

    pthread_mutex_lock(&cs->lock);
    while (cs->states[i] == CHUNK_PENDING && !cs->vars->stop_flag) {
        struct timespec deadline;
        clock_gettime(CLOCK_REALTIME, &deadline);
        deadline.tv_nsec += 100 * 1000 * 1000;
        if (deadline.tv_nsec >= 1000 * 1000 * 1000) {
            deadline.tv_sec++;
            deadline.tv_nsec -= 1000 * 1000 * 1000;
        }
        pthread_cond_timedwait(&cs->chunk_done, &cs->lock, &deadline);
        update_progress(cs);
    }
    chunk_state_t state = cs->states[i];
    pthread_mutex_unlock(&cs->lock);
    return state;
}

/* Let the workers move past chunk `i`. */
static void chunk_merged(chunked_scan_t *cs, size_t i)
{
    if (cs->num_threads == 0)
        return;
    pthread_mutex_lock(&cs->lock);
    cs->merged_chunks = i + 1;
    pthread_cond_broadcast(&cs->window_moved);
    pthread_mutex_unlock(&cs->lock);
}

/* Stop and join the workers. After this, the main thread owns everything. */
static void stop_scan_threads(chunked_scan_t *cs)
{
    if (cs->num_threads == 0)
        return;
    pthread_mutex_lock(&cs->lock);
    cs->abort = true;
    pthread_cond_broadcast(&cs->window_moved);
    pthread_mutex_unlock(&cs->lock);
    while (cs->num_threads > 0)
        pthread_join(cs->threads[--cs->num_threads], NULL);
}

/* A matches array a chunk scan can fill without add_element() reallocating.
 * They are reused rather than freed: every page of fresh memory costs a page
 * fault, and the chunk arrays of a dense scan add up to several times its
 * result. */
static matches_and_old_values_array *get_chunk_array(chunked_scan_t *cs)
{
    matches_and_old_values_array *array = NULL;

    pthread_mutex_lock(&cs->lock);
    if (cs->pool_size)
        array = cs->pool[--cs->pool_size];
    pthread_mutex_unlock(&cs->lock);

    if (!array) {
        if (!(array = malloc(cs->chunk_array_bytes)))
            return NULL;
        advise_huge_pages(array, cs->chunk_array_bytes);
    }
    array->bytes_allocated = cs->chunk_array_bytes;
    array->max_needed_bytes = cs->chunk_array_bytes;
    array->swaths[0].first_byte_in_child = NULL;
    array->swaths[0].number_of_bytes = 0;
    return array;
}

static void put_chunk_array(chunked_scan_t *cs, matches_and_old_values_array *array)
{
    pthread_mutex_lock(&cs->lock);
    if (array && cs->pool_size < sizeof(cs->pool) / sizeof(cs->pool[0])) {
        cs->pool[cs->pool_size++] = array;
        array = NULL;
    }
    pthread_mutex_unlock(&cs->lock);
    free(array);
}

/* Stop the workers and free what the chunked scan allocated. */
static void end_chunked_scan(chunked_scan_t *cs)
{
    stop_scan_threads(cs);
    while (cs->pool_size)
        free(cs->pool[--cs->pool_size]);
    pthread_mutex_destroy(&cs->lock);
    pthread_cond_destroy(&cs->chunk_done);
    pthread_cond_destroy(&cs->window_moved);
    free(cs->states);
    free(cs->buf);
}

/* add_element() for a chunk array, which never needs to grow, with the
 * common case inline: the byte right after the swath's last one. */
static inline matches_and_old_values_swath *
append_element(matches_and_old_values_array **array, matches_and_old_values_swath *swath,
               char *address, uint8_t byte, match_flags flags)
{
    if (LIKELY(swath->number_of_bytes) &&
        address == (char *) swath->first_byte_in_child + swath->number_of_bytes) {
        old_value_and_match_info *element = &swath->data[swath->number_of_bytes++];
        element->old_value = byte;
        element->match_info = flags;
        return swath;
    }
    return add_element(array, swath, address, byte, flags);
}

static void null_terminate_chunk(matches_and_old_values_swath *swath)
{
    if (swath->number_of_bytes) {
        swath = local_address_beyond_last_element(swath);
        swath->first_byte_in_child = NULL;
        swath->number_of_bytes = 0;
    }
}

/* Append `count` elements for consecutive addresses from `remote_address` to
 * the swath `swath`, choosing between padding and a new swath exactly like
 * add_element() does. Unless `may_grow`, the array must already be large
 * enough. Returns the swath appended to, or NULL if out of memory. */
static matches_and_old_values_swath *
append_elements(matches_and_old_values_array **array,
                matches_and_old_values_swath *swath,
                char *remote_address,
                const old_value_and_match_info *elements,
                size_t count, bool may_grow)
{
    size_t bytes = count * sizeof(old_value_and_match_info);
    size_t padding = 0;
    bool new_swath = false;
    void *end;

    if (count == 0)
        return swath;

    if (swath->number_of_bytes == 0) {
        end = (void *) swath + sizeof(matches_and_old_values_swath) + bytes;
    } else {
        size_t local_index_excess =
            remote_address - (char *) remote_address_of_last_element(swath);
        size_t local_address_excess =
            local_index_excess * sizeof(old_value_and_match_info);
        size_t needed_size_for_a_new_swath =
            sizeof(matches_and_old_values_swath) +
            sizeof(old_value_and_match_info);

        new_swath = local_address_excess >= needed_size_for_a_new_swath;
        if (new_swath)
            end = local_address_beyond_last_element(swath) +
                  sizeof(matches_and_old_values_swath) + bytes;
        else {
            padding = local_address_excess - sizeof(old_value_and_match_info);
            end = local_address_beyond_last_element(swath) + padding + bytes;
        }
    }

    if (!may_grow && (size_t) (end - (void *) *array) > (*array)->bytes_allocated)
        return NULL;
    *array = allocate_enough_to_reach(*array, end, &swath);
    if (!*array)
        return NULL;

    if (swath->number_of_bytes == 0) {
        swath->first_byte_in_child = remote_address;
    } else if (new_swath) {
        swath = local_address_beyond_last_element(swath);
        swath->first_byte_in_child = remote_address;
        swath->number_of_bytes = 0;
    } else {
        memset(local_address_beyond_last_element(swath), 0, padding);
        swath->number_of_bytes += padding / sizeof(old_value_and_match_info);
    }

    memcpy(&swath->data[swath->number_of_bytes], elements, bytes);
    swath->number_of_bytes += count;
    return swath;
}

/* Append a chunk's matches, minus the addresses already recorded while the
 * previous chunk finished a match across the boundary. */
static matches_and_old_values_swath *
merge_chunk(matches_and_old_values_array **array,
            matches_and_old_values_swath *writing_swath_index,
            const matches_and_old_values_array *chunk, bool may_grow)
{
    const matches_and_old_values_swath *s = chunk->swaths;

    while (s->number_of_bytes) {
        char *first = s->first_byte_in_child;
        size_t skip = 0;

        if (writing_swath_index->number_of_bytes) {
            char *last = remote_address_of_last_element(writing_swath_index);
            if (last >= first)
                skip = MIN(s->number_of_bytes, (size_t) (last - first) + 1);
        }

        writing_swath_index = append_elements(array, writing_swath_index, first + skip,
                                              &s->data[skip], s->number_of_bytes - skip,
                                              may_grow);
        if (!writing_swath_index)
            return NULL;

        s = (const matches_and_old_values_swath *) &s->data[s->number_of_bytes];
    }
    return writing_swath_index;
}

/* Rescans, which keep the matches that still match.
 *
 * The chunks are runs of elements of the current matches array, which the
 * merge overwrites in place: the result never takes more room than the input
 * up to the same point (the serial loop relies on this too). A chunk's result
 * reaches into the next chunk's input, so chunk j is merged only once chunk
 * j + 1 is done too; no later chunk reads that far back. */

typedef struct {
    matches_and_old_values_swath *swath; /* where the chunk starts */
    /* that swath's header: an earlier merge may overwrite it */
    void *first_byte_in_child;
    size_t number_of_bytes;
    size_t index;        /* first element of `swath` the chunk owns */
    size_t num_elements; /* elements it owns, across swaths */
    size_t input_bytes;  /* room those take in the array */
    matches_and_old_values_array *matches;
    unsigned long num_matches;  /* matches in the owned elements */
    unsigned long tail_matches; /* matches found while finishing past the end */
} check_chunk_t;

/* Target memory is read in batches: the bytes of the next elements, as ranges
 * merged across small gaps, all read with one process_vm_readv() call. After
 * the first scans the matches are usually far apart, so reading them one by
 * one costs a syscall each, and reading everything around them costs
 * bandwidth. */
#    define READ_BATCH (1UL << 20) /* bytes */
#    define READ_BATCH_RANGES 1024 /* process_vm_readv() takes up to IOV_MAX */
#    define READ_GAP 3072          /* read gaps up to this (measured best) */

typedef struct {
    char *start;
    size_t len;
    size_t got;    /* bytes read from `start` on */
    size_t offset; /* of the bytes in `data` */
} read_range_t;

typedef struct {
    read_range_t ranges[READ_BATCH_RANGES];
    unsigned num_ranges;
    unsigned current;
    uint8_t data[READ_BATCH];
    uint8_t single[MAX_MATCH_LENGTH]; /* an element past a failed read */
} read_batch_t;

/* Plan reading the elements from `s` index `i` on, with `left` owned ones to
 * go and then up to a match's length more, to finish one. Returns how many
 * elements the batch covers. */
static size_t plan_batch(read_batch_t *b, scan_data_type_t scan_data_type,
                         matches_and_old_values_swath *s, char *first, size_t n,
                         size_t i, size_t left)
{
    bool vlt = scan_data_type == BYTEARRAY || scan_data_type == STRING;
    size_t count = 0, used = 0, past_end = 0;
    read_range_t *r = NULL;

    b->num_ranges = 0;
    b->current = 0;
    for (;;) {
        if (i == n) {
            if (left == 0)
                break;
            s = (matches_and_old_values_swath *) &s->data[n];
            first = s->first_byte_in_child;
            n = s->number_of_bytes;
            i = 0;
            continue;
        }
        if (left == 0 && past_end == MAX_MATCH_LENGTH)
            break;

        /* Numbers need at most 8 bytes each, and the elements of a swath are
         * consecutive: take as many as fit at once. Reading a few bytes too
         * many is harmless, even into memory that cannot be read. */
        size_t take = MIN(n - i, left ? left : MAX_MATCH_LENGTH - past_end);
        size_t need = 8;
        if (vlt) {
            take = 1;
            need = flags_to_memlength(scan_data_type, s->data[i].match_info);
            if (need == 0)
                need = 1;
        }
        char *address = first + i;

        /* the furthest this range may reach */
        char *limit;
        if (r && address <= r->start + r->len + READ_GAP) {
            limit = r->start + r->len + (READ_BATCH - used);
        } else {
            if (b->num_ranges == READ_BATCH_RANGES)
                break;
            r = NULL;
            limit = address + (READ_BATCH - used);
        }
        if (address + need > limit)
            break;
        take = MIN(take, (size_t) (limit - need - address) + 1);

        size_t end = address + take - 1 + need - (r ? r->start : address);
        if (!r) {
            r = &b->ranges[b->num_ranges++];
            r->start = address;
            r->len = 0;
            r->offset = used;
        }
        if (end > r->len) {
            used += end - r->len;
            r->len = end;
        }

        count += take;
        i += take;
        if (left)
            left -= take;
        else
            past_end += take;
    }
    return count;
}

/* Read the planned ranges. process_vm_readv() stops at the first range it
 * cannot read whole; pread() on /proc/pid/mem then reads that one as far as
 * it can. That also reads pages the target itself cannot, like the serial
 * loop does. */
static void read_batch(chunked_scan_t *cs, read_batch_t *b)
{
    unsigned k = 0;

    while (k < b->num_ranges) {
        if (!__atomic_load_n(&cs->no_vm_readv, __ATOMIC_RELAXED)) {
            struct iovec local[READ_BATCH_RANGES], remote[READ_BATCH_RANGES];
            unsigned count = b->num_ranges - k;
            for (unsigned j = 0; j < count; j++) {
                local[j].iov_base = b->data + b->ranges[k + j].offset;
                local[j].iov_len = b->ranges[k + j].len;
                remote[j].iov_base = b->ranges[k + j].start;
                remote[j].iov_len = b->ranges[k + j].len;
            }
            ssize_t got = process_vm_readv(cs->vars->target, local, count, remote, count, 0);
            if (got < 0 && (errno == ENOSYS || errno == EPERM)) {
                __atomic_store_n(&cs->no_vm_readv, true, __ATOMIC_RELAXED);
                continue;
            }
            for (; got > 0 && k < b->num_ranges && (size_t) got >= b->ranges[k].len; k++) {
                b->ranges[k].got = b->ranges[k].len;
                got -= b->ranges[k].len;
            }
            if (k == b->num_ranges)
                break;
        }
        read_range_t *r = &b->ranges[k++];
        r->got = readmemory(b->data + r->offset, r->start, r->len);
    }
}

/* The bytes of a planned element, as many as are readable up to `need`. This
 * depends only on the target, never on what was read before: next to memory
 * that cannot be read, sm_peekdata() can fail where reading afresh gets part
 * of the bytes. */
static inline read_range_t *batch_range(read_batch_t *b, char *address)
{
    while (address >= b->ranges[b->current].start + b->ranges[b->current].len)
        b->current++;
    return &b->ranges[b->current];
}

static inline size_t batch_bytes(read_batch_t *b, char *address, size_t need,
                                 const uint8_t **mem)
{
    read_range_t *r = batch_range(b, address);
    size_t offset = address - r->start;

    if (offset < r->got) {
        *mem = b->data + r->offset + offset;
        return MIN(need, r->got - offset);
    }
    /* past where the range stopped being readable; this may be again */
    *mem = b->single;
    return readmemory(b->single, address, need);
}

/* Rescan one chunk, following the serial loop in sm_checkmatches(). */
static chunk_state_t check_chunk(chunked_scan_t *cs, size_t ci, uint8_t *buf)
{
    check_chunk_t *c = &((check_chunk_t *) cs->chunks)[ci];
    scan_data_type_t scan_data_type = cs->vars->options.scan_data_type;
    read_batch_t *batch = (read_batch_t *) buf;
    size_t batch_left = 0; /* elements the batch covers from here on */
    matches_and_old_values_swath *s = c->swath;
    char *first = c->first_byte_in_child;
    size_t n = c->number_of_bytes;
    size_t i = c->index;
    size_t left = c->num_elements;
    size_t reported = 0;
    int required_extra_bytes_to_record = 0;
    int pending = -1; /* once finishing: elements the matches before the end still need */
    /* Locals, not fields of `c`: neighbouring chunks share cache lines. */
    unsigned long num_matches = 0;
    /* =, != and update on numbers need no call per type and width */
    bool quick = scan_data_type != BYTEARRAY && scan_data_type != STRING &&
                 (cs->match_type == MATCHNOTCHANGED || cs->match_type == MATCHCHANGED ||
                  cs->match_type == MATCHUPDATE);
    match_flags allowed = scan_data_type_flags(scan_data_type);

    matches_and_old_values_array *matches = get_chunk_array(cs);
    if (!(c->matches = matches))
        return CHUNK_FAILED;
    matches_and_old_values_swath *writing_swath_index = matches->swaths;

    for (;;) {
        if (i == n) {
            /* a match does not continue into the next swath */
            if (left == 0)
                break;
            s = (matches_and_old_values_swath *) &s->data[n];
            first = s->first_byte_in_child;
            n = s->number_of_bytes;
            i = 0;
            required_extra_bytes_to_record = 0;
            continue;
        }

        /* Past the owned elements, finish the matches crossing the end: record
         * what the serial loop does, for as many elements as they still need
         * (see search_chunk()). */
        bool finishing = left == 0;
        if (finishing && pending < 0)
            pending = required_extra_bytes_to_record;
        if (finishing && pending == 0)
            break;

        if (!finishing && UNLIKELY(c->num_elements - left - reported >= PROGRESS_STEP)) {
            if (report_progress(cs, c->num_elements - left - reported)) {
                c->matches = matches;
                return CHUNK_STOPPED;
            }
            reported = c->num_elements - left;
        }

        if (batch_left == 0) {
            batch_left = plan_batch(batch, scan_data_type, s, first, n, i, left);
            read_batch(cs, batch);
        }

        /* A run of elements of this swath with all their bytes read and
         * 8 old bytes after them: no call per element. */
        if ((quick || cs->block) && !finishing && i + 8 <= n) {
            char *address = first + i;
            read_range_t *r = batch_range(batch, address);
            size_t offset = address - r->start;
            size_t run = 0;
            if (offset + 8 <= r->got) {
                run = MIN(r->got - offset - 7, n - i - 7);
                run = MIN(run, MIN(left, batch_left));
                run = MIN(run, PROGRESS_STEP);
            }
            if (run > 0 && cs->block) {
                const uint8_t *memory = batch->data + r->offset + offset;
                for (size_t done = 0; done < run; done += BLOCK_POSITIONS) {
                    match_flags flags[BLOCK_POSITIONS];
                    size_t count = MIN(run - done, BLOCK_POSITIONS);
                    size_t found = cs->block(memory + done, count, &s->data[i + done], cs->uservalue, flags);
                    /* without matches, only the elements earlier matches still need */
                    size_t end = done + (found ? count : MIN(count, (size_t) required_extra_bytes_to_record));
                    for (size_t k = done; k < end; k++) {
                        match_flags checkflags = flags[k - done];
                        if (checkflags != flags_empty) {
                            writing_swath_index = append_element(&matches, writing_swath_index,
                                                                 address + k, memory[k], checkflags);
                            ++num_matches;
                            required_extra_bytes_to_record = bytes_to_record(
                                required_extra_bytes_to_record, flags_to_memlength(scan_data_type, checkflags));
                        } else if (required_extra_bytes_to_record) {
                            writing_swath_index = append_element(&matches, writing_swath_index,
                                                                 address + k, memory[k], flags_empty);
                            --required_extra_bytes_to_record;
                        }
                    }
                }
                i += run;
                left -= run;
                batch_left -= run;
                continue;
            }
#    if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
            /* keep the old bytes in a sliding register and load the new ones
             * 8 at a time */
            if (run > 0) {
                const uint8_t *memory = batch->data + r->offset + offset;
                uint64_t before = 0;
                for (size_t k = 0; k < 8; k++)
                    before |= (uint64_t) s->data[i + k].old_value << (8 * k);
                for (size_t k = 0; k < run; k++) {
                    match_flags old_flags = s->data[i + k].match_info;
                    match_flags checkflags = flags_empty;
                    unsigned int match_length = 0;
                    if (old_flags != flags_empty) {
                        uint64_t now;
                        memcpy(&now, memory + k, sizeof(now));
                        match_length = quick_old_value_routine(
                            cs->match_type, allowed, now, before,
                            flags_to_memlength(scan_data_type, old_flags), old_flags, &checkflags);
                    }
                    if (match_length > 0) {
                        writing_swath_index = append_element(&matches, writing_swath_index,
                                                             address + k, memory[k], checkflags);
                        ++num_matches;
                        required_extra_bytes_to_record = bytes_to_record(required_extra_bytes_to_record, match_length);
                    } else if (required_extra_bytes_to_record) {
                        writing_swath_index = append_element(&matches, writing_swath_index,
                                                             address + k, memory[k], flags_empty);
                        --required_extra_bytes_to_record;
                    }
                    if (k + 1 < run)
                        before = (before >> 8) | (uint64_t) s->data[i + k + 8].old_value << 56;
                }
                i += run;
                left -= run;
                batch_left -= run;
                continue;
            }
#    endif
        }
        --batch_left;

        match_flags old_flags = s->data[i].match_info;
        char *address = first + i;
        unsigned int match_length = 0;
        match_flags checkflags = flags_empty;
        const uint8_t *memory_ptr = NULL;

        if (old_flags != flags_empty || required_extra_bytes_to_record) {
            unsigned int old_length = flags_to_memlength(scan_data_type, old_flags);
            size_t memlength = batch_bytes(batch, address, old_length ? old_length : 1,
                                           &memory_ptr);
            if (memlength == 0) {
                /* If we can't look at the data here, just abort the whole recording, something bad happened */
                required_extra_bytes_to_record = 0;
            } else if (old_flags != flags_empty && quick) {
                /* the old value, as data_to_val_aux() makes it */
                uint8_t old_bytes[8] = {0};
                size_t max_bytes = MIN(n - i, 8);
                match_flags valid = max_bytes >= 8   ? flags_max
                                    : max_bytes >= 4 ? (match_flags) ~flags_64b
                                    : max_bytes >= 2 ? (match_flags) ~(flags_64b | flags_32b)
                                                     : flags_8b;
                for (size_t k = 0; k < max_bytes; k++)
                    old_bytes[k] = s->data[i + k].old_value;
                uint64_t now = 0, before;
                memlength = old_length < memlength ? old_length : memlength;
                memcpy(&now, memory_ptr, MIN(memlength, 8));
                memcpy(&before, old_bytes, sizeof(before));
                match_length = quick_old_value_routine(cs->match_type, allowed, now, before,
                                                       memlength, old_flags & valid, &checkflags);
            } else if (old_flags != flags_empty) {
                value_t old_val = data_to_val_aux(s, i, n);
                memlength = old_length < memlength ? old_length : memlength;
                match_length = (*sm_scan_routine)((const mem64_t *) memory_ptr, memlength,
                                                  &old_val, cs->uservalue, &checkflags);
            }
        }

        if (match_length > 0) {
            writing_swath_index = append_element(&matches, writing_swath_index, address,
                                                 memory_ptr[0], checkflags);
            if (finishing)
                c->tail_matches++; /* the next chunk's, which counts them too */
            else
                ++num_matches;
            required_extra_bytes_to_record =
                bytes_to_record(required_extra_bytes_to_record, match_length);
        } else if (required_extra_bytes_to_record) {
            writing_swath_index = append_element(&matches, writing_swath_index, address,
                                                 memory_ptr[0], flags_empty);
            --required_extra_bytes_to_record;
        }

        ++i;
        if (!finishing)
            --left;
        else /* stop where the serial loop stops recording, at unreadable memory */
            pending = MIN(pending - 1, required_extra_bytes_to_record);
    }

    null_terminate_chunk(writing_swath_index);
    c->matches = matches;
    c->num_matches = num_matches;
    report_progress(cs, c->num_elements - reported);
    return CHUNK_DONE;
}

/* Cut the matches array into chunks of `chunk_elements` elements. */
static check_chunk_t *make_check_chunks(matches_and_old_values_array *matches,
                                        unsigned long total_elements, size_t chunk_elements,
                                        size_t *num_chunks)
{
    check_chunk_t *chunks, *c = NULL;
    matches_and_old_values_swath *s;

    *num_chunks = (total_elements + chunk_elements - 1) / chunk_elements;
    if (*num_chunks == 0 || !(chunks = calloc(*num_chunks, sizeof(check_chunk_t))))
        return NULL;

    for (s = matches->swaths; s->number_of_bytes;
         s = (matches_and_old_values_swath *) &s->data[s->number_of_bytes]) {
        size_t index = 0;
        if (c)
            c->input_bytes += sizeof(matches_and_old_values_swath);
        while (index < s->number_of_bytes) {
            if (!c || c->num_elements == chunk_elements) {
                c = c ? c + 1 : chunks;
                c->swath = s;
                c->first_byte_in_child = s->first_byte_in_child;
                c->number_of_bytes = s->number_of_bytes;
                c->index = index;
                c->input_bytes = sizeof(matches_and_old_values_swath);
            }
            size_t take = MIN(s->number_of_bytes - index, chunk_elements - c->num_elements);
            c->num_elements += take;
            c->input_bytes += take * sizeof(old_value_and_match_info);
            index += take;
        }
    }
    return chunks;
}

#    define MIN_CHECK_CHUNK (1UL << 20) /* elements, must exceed MAX_MATCH_LENGTH */
#    define MAX_CHECK_CHUNK (4UL << 20)

/* Elements per chunk of a rescan with `nthreads`. Several chunks per thread
 * keep the threads busy to the end. */
static size_t check_chunk_elements(unsigned long total_elements, unsigned nthreads)
{
    size_t chunk_elements = total_elements / (nthreads * 8);
    if (chunk_elements < MIN_CHECK_CHUNK)
        chunk_elements = MIN_CHECK_CHUNK;
    chunk_elements = MIN(chunk_elements, MAX_CHECK_CHUNK);
    /* a chunk's tail reaches up to a match length into the next chunk, and
     * never further (see the merge below) */
    return chunk_size_for_tests(chunk_elements, MAX_MATCH_LENGTH, MAX_CHECK_CHUNK);
}

/* The main thread's part of a rescan: merge chunks in order, overwriting the
 * matches array, and print the same progress as the serial loop. Sets
 * `started` to false if it changed nothing, so the serial loop can run. */
static bool check_matches_parallel(globals_t *vars, scan_match_type_t match_type,
                                   const uservalue_t *uservalue,
                                   unsigned long total_elements, unsigned nthreads,
                                   matches_and_old_values_swath **writing_swath_index,
                                   bool *started)
{
    chunked_scan_t cs = {
        .vars = vars,
        .uservalue = uservalue,
        .match_type = match_type,
        .scan = check_chunk,
        .block = get_block_routine(sm_scan_routine),
        .buf_size = sizeof(read_batch_t),
        .total_units = total_elements,
    };
    bool ok = true;
    size_t i;

    size_t chunk_elements = check_chunk_elements(total_elements, nthreads);

    check_chunk_t *chunks = make_check_chunks(vars->matches, total_elements, chunk_elements,
                                              &cs.num_chunks);
    cs.chunks = chunks;
    cs.states = calloc(cs.num_chunks, sizeof(chunk_state_t));
    *started = chunks && cs.states;
    if (!*started) {
        free(chunks);
        free(cs.states);
        return true;
    }

    /* room for a chunk's result, which never outgrows its input, plus a match
     * finished past its end */
    for (i = 0; i < cs.num_chunks; i++)
        if (chunks[i].input_bytes > cs.chunk_array_bytes)
            cs.chunk_array_bytes = chunks[i].input_bytes;
    cs.chunk_array_bytes += sizeof(matches_and_old_values_array) +
                            2 * sizeof(matches_and_old_values_swath) +
                            MAX_MATCH_LENGTH * sizeof(old_value_and_match_info);

    start_scan_threads(&cs, nthreads);

    /* The first chunk took a copy of this header. */
    *writing_swath_index = vars->matches->swaths;
    (*writing_swath_index)->first_byte_in_child = NULL;
    (*writing_swath_index)->number_of_bytes = 0;

    unsigned long merged_elements = 0;
    unsigned dots = 1;
    for (i = 0; i < cs.num_chunks; i++) {
        chunk_state_t state = wait_for_chunk(&cs, i);
        chunk_state_t next = state == CHUNK_DONE && i + 1 < cs.num_chunks ? wait_for_chunk(&cs, i + 1)
                                                                          : CHUNK_DONE;
        bool last = state != CHUNK_DONE || next != CHUNK_DONE;
        if (last) {
            /* nothing reads the matches array any more */
            stop_scan_threads(&cs);
            if (state == CHUNK_FAILED || next == CHUNK_FAILED) {
                show_error("sorry, there was a memory allocation error.\n");
                ok = false;
            } else {
                printf("\n");
            }
        }

        if (state == CHUNK_DONE) {
            *writing_swath_index = merge_chunk(&vars->matches, *writing_swath_index,
                                               chunks[i].matches, false);
            /* cannot happen, the result never outgrows the input */
            assert(*writing_swath_index != NULL);
            vars->num_matches += chunks[i].num_matches;
            if (last)
                vars->num_matches += chunks[i].tail_matches;
            merged_elements += chunks[i].num_elements;
        }
        put_chunk_array(&cs, chunks[i].matches);
        chunks[i].matches = NULL;
        if (last)
            break;
        chunk_merged(&cs, i);

        /* for user, just print a dot */
        for (; dots < 1 + (NUM_DOTS - 1) * merged_elements / total_elements; dots++)
            print_a_dot();
    }

    stop_scan_threads(&cs);
    for (i = 0; i < cs.num_chunks; i++)
        put_chunk_array(&cs, chunks[i].matches);
    free(chunks);
    end_chunked_scan(&cs);
    return ok;
}

/* The initial search. Regions are cut into chunks of bytes; reading this far
 * past a chunk gives every byte it tests (including the ones it tests to
 * finish a match crossing its end) the same lookahead the serial loop gives
 * it. */
#    define MIN_CHUNK_SIZE (1UL << 20)
#    define MAX_CHUNK_SIZE (16UL << 20)
#    define CHUNK_TAIL (2 * MAX_MATCH_LENGTH)

/* Bytes of target memory per chunk of an initial search with `nthreads`.
 * Several chunks per thread keep the threads busy to the end. */
static size_t search_chunk_size(unsigned long total_scan_bytes, unsigned nthreads)
{
    size_t chunk_size = total_scan_bytes / (nthreads * 8);
    chunk_size = (chunk_size + MIN_CHUNK_SIZE - 1) & ~(MIN_CHUNK_SIZE - 1);
    if (chunk_size < MIN_CHUNK_SIZE)
        chunk_size = MIN_CHUNK_SIZE;
    chunk_size = MIN(chunk_size, MAX_CHUNK_SIZE);
    return chunk_size_for_tests(chunk_size, 64, MAX_CHUNK_SIZE);
}

/* room for a byte per element, so that add_element() never reallocates */
static size_t search_chunk_array_bytes(size_t chunk_size)
{
    return sizeof(matches_and_old_values_array) + 2 * sizeof(matches_and_old_values_swath) +
           (chunk_size + CHUNK_TAIL) * sizeof(old_value_and_match_info);
}

typedef struct {
    const region_t *region;
    unsigned long regnum; /* 1-based, for messages */
    size_t index_in_region;
    size_t chunks_in_region;
    char *start;        /* first byte this chunk tests */
    size_t size;        /* number of bytes this chunk owns */
    size_t region_left; /* bytes from `start` to the end of the region */
    char *readable_end; /* where reading stopped short, or NULL */
    matches_and_old_values_array *matches;
    unsigned long num_matches;  /* matches starting in the owned bytes */
    unsigned long tail_matches; /* matches found while finishing past the end */
} search_chunk_t;

/* Scan one chunk, following the serial loop in sm_searchregions(). */
static chunk_state_t search_chunk(chunked_scan_t *cs, size_t ci, uint8_t *buf)
{
    search_chunk_t *c = &((search_chunk_t *) cs->chunks)[ci];
    size_t want = c->size + MIN(c->region_left - c->size, CHUNK_TAIL);
    size_t got = readmemory(buf, c->start, want);
    size_t owned = MIN(got, c->size);
    size_t reported = 0;
    size_t i;
    int required_extra_bytes_to_record = 0;
    /* Locals, not fields of `c`: neighbouring chunks share cache lines. */
    unsigned long num_matches = 0;

    if (got < want)
        c->readable_end = c->start + got;

    matches_and_old_values_array *matches = get_chunk_array(cs);
    if (!(c->matches = matches))
        return CHUNK_FAILED;
    matches_and_old_values_swath *writing_swath_index = matches->swaths;

    i = 0;
    if (cs->match_type == MATCHANY) {
        /* A snapshot matches everywhere, the same way wherever the longest
         * possible match fits: record those bytes without a call each. */
        scan_data_type_t dt = cs->vars->options.scan_data_type;
        size_t longest = dt == BYTEARRAY || dt == STRING ? MAX_MATCH_LENGTH - 1 : 8;
        size_t end = got >= longest ? MIN(owned, got - longest + 1) : 0;
        match_flags flags = flags_empty;
        unsigned int match_length = end ? (*sm_scan_routine)((const mem64_t *) buf, got, NULL,
                                                             cs->uservalue, &flags)
                                        : 0;
        if (match_length > 0) {
            writing_swath_index = append_element(&matches, writing_swath_index, c->start,
                                                 buf[0], flags);
            old_value_and_match_info *element = &writing_swath_index->data[1];
            for (i = 1; i < end; i++, element++) {
                element->old_value = buf[i];
                element->match_info = flags;
            }
            writing_swath_index->number_of_bytes = end;
            num_matches = end;
            required_extra_bytes_to_record = match_length - 1;
            if (report_progress(cs, end))
                return CHUNK_STOPPED;
            reported = end;
        }
    }

    for (; i < owned; i++) {
        /* skip what cannot match, unless recording the rest of a match */
        if (cs->prefilter && required_extra_bytes_to_record == 0) {
            i = prefilter_next(cs->prefilter, buf, i, owned, got);
            if (i == owned)
                break;
        }
        if (UNLIKELY(i - reported >= PROGRESS_STEP)) {
            if (report_progress(cs, i - reported))
                return CHUNK_STOPPED;
            reported = i;
        }

        /* positions with 8 bytes after them: a run of them in one call */
        if (cs->block && i + 8 <= got) {
            match_flags flags[BLOCK_POSITIONS];
            size_t run = MIN(MIN(owned, got - 7) - i, BLOCK_POSITIONS);
            size_t found = cs->block(buf + i, run, NULL, cs->uservalue, flags);
            /* without matches, only the bytes earlier matches still need */
            size_t end = found ? run : MIN(run, (size_t) required_extra_bytes_to_record);
            for (size_t k = 0; k < end; k++) {
                if (flags[k] != flags_empty) {
                    writing_swath_index = append_element(&matches, writing_swath_index,
                                                         c->start + i + k, buf[i + k], flags[k]);
                    ++num_matches;
                    required_extra_bytes_to_record = bytes_to_record(
                        required_extra_bytes_to_record, flags_to_memlength(cs->vars->options.scan_data_type, flags[k]));
                } else if (required_extra_bytes_to_record) {
                    writing_swath_index = append_element(&matches, writing_swath_index,
                                                         c->start + i + k, buf[i + k], flags_empty);
                    --required_extra_bytes_to_record;
                }
            }
            i += run - 1;
            continue;
        }

        const mem64_t *memory_ptr = (mem64_t *) (buf + i);
        match_flags checkflags = flags_empty;
        unsigned int match_length = (*sm_scan_routine)(memory_ptr, got - i, NULL,
                                                       cs->uservalue, &checkflags);
        if (UNLIKELY(match_length > 0)) {
            writing_swath_index = append_element(&matches, writing_swath_index,
                                                 c->start + i, get_u8b(memory_ptr), checkflags);
            ++num_matches;
            required_extra_bytes_to_record = bytes_to_record(required_extra_bytes_to_record, match_length);
        } else if (required_extra_bytes_to_record) {
            writing_swath_index = append_element(&matches, writing_swath_index,
                                                 c->start + i, get_u8b(memory_ptr), flags_empty);
            --required_extra_bytes_to_record;
        }
    }

    /* Finish the matches crossing the end: record what the serial loop does,
     * for as many bytes as they still need. Past those, what the serial loop
     * records no longer depends on matches before the end, so the next chunk
     * records the same (and the matches it finds here). */
    for (int pending = required_extra_bytes_to_record; pending > 0 && i < got; pending--, i++) {
        const mem64_t *memory_ptr = (mem64_t *) (buf + i);
        match_flags checkflags = flags_empty;
        unsigned int match_length = (*sm_scan_routine)(memory_ptr, got - i, NULL,
                                                       cs->uservalue, &checkflags);
        if (match_length > 0) {
            writing_swath_index = add_element(&matches, writing_swath_index,
                                              c->start + i, get_u8b(memory_ptr), checkflags);
            c->tail_matches++;
            required_extra_bytes_to_record =
                bytes_to_record(required_extra_bytes_to_record, match_length);
        } else {
            writing_swath_index = add_element(&matches, writing_swath_index,
                                              c->start + i, get_u8b(memory_ptr), flags_empty);
            --required_extra_bytes_to_record;
        }
    }

    null_terminate_chunk(writing_swath_index);
    c->matches = matches;
    c->num_matches = num_matches;

    /* bytes not read still count as done */
    report_progress(cs, c->size - reported);
    return CHUNK_DONE;
}

/* The main thread's part of the initial search: merge chunks in order,
 * printing the same per-region progress as the serial scan. */
static bool search_regions_parallel(globals_t *vars, scan_match_type_t match_type,
                                    const uservalue_t *uservalue,
                                    unsigned long total_scan_bytes, unsigned nthreads,
                                    matches_and_old_values_swath **writing_swath_index)
{
    chunked_scan_t cs = {
        .vars = vars,
        .uservalue = uservalue,
        .match_type = match_type,
        .scan = search_chunk,
        .total_units = total_scan_bytes,
    };
    prefilter_t prefilter;
    search_chunk_t *chunks;
    bool ok = true;
    element_t *n;
    size_t i;

    size_t chunk_size = search_chunk_size(total_scan_bytes, nthreads);
    cs.buf_size = chunk_size + CHUNK_TAIL;
    cs.chunk_array_bytes = search_chunk_array_bytes(chunk_size);
    if (prefilter_init(&prefilter, vars->options.scan_data_type, match_type, uservalue,
                       vars->options.reverse_endianness))
        cs.prefilter = &prefilter;
    else
        cs.block = get_block_routine(sm_scan_routine);

    for (n = vars->regions->head; n; n = n->next) {
        region_t *r = n->data;
        cs.num_chunks += (r->size + chunk_size - 1) / chunk_size;
    }
    cs.chunks = chunks = calloc(cs.num_chunks, sizeof(search_chunk_t));
    cs.states = calloc(cs.num_chunks, sizeof(chunk_state_t));
    if (!chunks || !cs.states) {
        show_error("sorry, there was a memory allocation error.\n");
        free(chunks);
        free(cs.states);
        return false;
    }

    unsigned long regnum = 0;
    i = 0;
    for (n = vars->regions->head; n; n = n->next) {
        region_t *r = n->data;
        size_t chunks_in_region = (r->size + chunk_size - 1) / chunk_size;
        ++regnum;
        for (size_t j = 0; j < chunks_in_region; j++, i++) {
            size_t offset = j * chunk_size;
            search_chunk_t *c = &chunks[i];
            c->region = r;
            c->regnum = regnum;
            c->index_in_region = j;
            c->chunks_in_region = chunks_in_region;
            c->start = (char *) r->start + offset;
            c->size = MIN(chunk_size, r->size - offset);
            c->region_left = r->size - offset;
        }
    }

    /* Reserve the most room the result can take up front: growing the array
     * while merging moves it, and touches its memory a small page at a time.
     * Untouched memory costs nothing, and growing still works if this fails.
     * Not when that is more than the machine's RAM: huge sparse mappings in
     * the target (such as ASan's shadow memory) can make it terabytes, and an
     * ASan build of scanmem aborts on such a request instead of failing it. */
    long phys_pages = sysconf(_SC_PHYS_PAGES), page_size = sysconf(_SC_PAGESIZE);
    if (phys_pages > 0 && page_size > 0 &&
        vars->matches->max_needed_bytes / page_size <= (unsigned long) phys_pages) {
        size_t writing_offset = (char *) *writing_swath_index - (char *) vars->matches;
        matches_and_old_values_array *whole = realloc(vars->matches, vars->matches->max_needed_bytes);
        if (whole) {
            whole->bytes_allocated = whole->max_needed_bytes;
            advise_huge_pages(whole, whole->bytes_allocated);
            *writing_swath_index = (matches_and_old_values_swath *) ((char *) whole + writing_offset);
            vars->matches = whole;
        }
    }

    start_scan_threads(&cs, nthreads);

    const region_t *region = NULL;
    char *readable_end = NULL; /* of the current region */
    unsigned dots = 0;
    for (i = 0; i < cs.num_chunks; i++) {
        search_chunk_t *c = &chunks[i];
        chunk_state_t state = wait_for_chunk(&cs, i);

        if (state == CHUNK_FAILED) {
            show_error("sorry, there was a memory allocation error.\n");
            ok = false;
            break;
        }
        if (state != CHUNK_DONE) {
            /* stopped: keep what was merged, like the serial scan does */
            if (i > 0)
                vars->num_matches += chunks[i - 1].tail_matches;
            printf("\n");
            break;
        }

        if (c->region != region) {
            region = c->region;
            readable_end = NULL;
            dots = 0;
            show_user("%02lu/%02lu searching %#10lx - %#10lx", c->regnum,
                      vars->regions->size, (unsigned long) region->start,
                      (unsigned long) region->start + region->size);
            fflush(stderr);
        }

        /* The serial scan stops a region at the first byte it cannot read. */
        if (readable_end == NULL || c->start < readable_end) {
            if (c->readable_end && c->index_in_region == 0 && c->readable_end == c->start)
                show_warn("reading region %02lu failed.\n", c->regnum);
            if (c->readable_end && (readable_end == NULL || c->readable_end < readable_end))
                readable_end = c->readable_end;

            *writing_swath_index = merge_chunk(&vars->matches, *writing_swath_index,
                                               c->matches, true);
            if (!*writing_swath_index) {
                show_error("could not allocate match array\n");
                ok = false;
                break;
            }
            vars->num_matches += c->num_matches;
        }

        /* print a simple progress meter */
        for (; dots < (c->index_in_region + 1) * NUM_DOTS / c->chunks_in_region; dots++)
            print_a_dot();
        if (c->index_in_region + 1 == c->chunks_in_region)
            show_user("ok\n");

        put_chunk_array(&cs, c->matches);
        c->matches = NULL;
        chunk_merged(&cs, i);
    }

    stop_scan_threads(&cs);
    for (i = 0; i < cs.num_chunks; i++)
        put_chunk_array(&cs, chunks[i].matches);
    free(chunks);
    end_chunked_scan(&cs);
    return ok;
}
#endif /* HAVE_PROCMEM */

/* This is the function that handles when you enter a value (or >, <, =) for the second or later time (i.e. when there's already a list of matches);
 * it reduces the list to those that still match. It returns false on failure to attach, detach, or reallocate memory, otherwise true. */
bool sm_checkmatches(globals_t *vars,
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
    bool ok = true;

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

#if HAVE_PROCMEM
    unsigned nthreads = scan_threads(vars);
    if (vars->options.threads != 1 && total_scan_bytes > 0) {
        bool started;
        /* put back the header the chunks start from */
        *vars->matches->swaths = reading_swath;
        ok = check_matches_parallel(vars, match_type, uservalue, total_scan_bytes, nthreads,
                                    &writing_swath_index, &started);
        if (started) {
            reading_swath.first_byte_in_child = NULL; /* skip the serial loop */
        } else {
            writing_swath_index->first_byte_in_child = NULL;
            writing_swath_index->number_of_bytes = 0;
        }
    }
#endif

    while (reading_swath.first_byte_in_child) {
        unsigned int match_length = 0;
        const mem64_t *memory_ptr;
        size_t memlength;
        match_flags checkflags;

        match_flags old_flags = reading_swath_index->data[reading_iterator].match_info;
        unsigned int old_length = flags_to_memlength(vars->options.scan_data_type, old_flags);
        void *address = reading_swath.first_byte_in_child + reading_iterator;

        /* read value from this address */
        if (UNLIKELY(sm_peekdata(address, old_length, &memory_ptr, &memlength) == false))
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

            writing_swath_index = add_element(&(vars->matches), writing_swath_index, address,
                                              get_u8b(memory_ptr), checkflags);

            ++vars->num_matches;

            required_extra_bytes_to_record = bytes_to_record(required_extra_bytes_to_record, match_length);
        }
        else if (required_extra_bytes_to_record)
        {
            writing_swath_index = add_element(&(vars->matches), writing_swath_index, address,
                                              get_u8b(memory_ptr), flags_empty);
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
    return sm_detach(vars->target) && ok;
}

/* sm_searchregions() performs an initial search of the process for values matching `uservalue` */
bool sm_searchregions(globals_t *vars, scan_match_type_t match_type, const uservalue_t *uservalue)
{
    matches_and_old_values_swath *writing_swath_index;
    int required_extra_bytes_to_record = 0;
    unsigned long total_size = 0;
    unsigned long regnum = 0;
    element_t *n = vars->regions->head;
    region_t *r;
    unsigned long total_scan_bytes = 0;
    unsigned char *data = NULL;

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
    
    /* get total number of bytes */
    for(n = vars->regions->head; n; n = n->next)
        total_scan_bytes += ((region_t *)n->data)->size;

    vars->scan_progress = 0.0;
    vars->stop_flag = false;
    n = vars->regions->head;

#if HAVE_PROCMEM
    if (vars->options.threads != 1) {
        if (!search_regions_parallel(vars, match_type, uservalue, total_scan_bytes,
                                     scan_threads(vars), &writing_swath_index)) {
            ENDINTERRUPTABLE();
            sm_detach(vars->target);
            return false;
        }
        n = NULL; /* skip the serial scan */
    }
#endif

    /* check every memory region */
    while (n) {
        size_t bytes_remaining;
        size_t bytes_per_dot;
        double progress_per_dot;

        /* load the next region */
        r = n->data;
        bytes_per_dot = r->size / NUM_DOTS;
        bytes_remaining = bytes_per_dot * NUM_DOTS;
        progress_per_dot = (double)bytes_per_dot / total_scan_bytes;

/* The maximum logical size is a comfortable 1MiB (increasing it does not help).
 * The actual allocation is that plus the rounded size of the maximum possible VLT.
 * This is needed because the last byte might be scanned as max size VLT,
 * thus need (2^16 - 2) extra bytes after it */
#define MAX_BUFFER_SIZE (1<<20)
#define MAX_ALLOC_SIZE  (MAX_BUFFER_SIZE + (1<<16))

        /* allocate data array */
        size_t alloc_size = MIN(r->size, MAX_ALLOC_SIZE);
        if ((data = malloc(alloc_size * sizeof(char))) == NULL) {
            show_error("sorry, there was a memory allocation error.\n");
            return false;
        }

        /* print a progress meter so user knows we haven't crashed */
        show_user("%02lu/%02lu searching %#10lx - %#10lx", ++regnum,
                vars->regions->size, (unsigned long)r->start, (unsigned long)r->start + r->size);
        fflush(stderr);

        /* For every offset, check if we have a match. */
        size_t memlength = r->size;
        size_t buffer_size = 0;
        void *reg_pos = r->start;
        const uint8_t *buf_pos = NULL;
        for ( ; ; memlength--, buffer_size--, reg_pos++, buf_pos++) {

            /* check if the buffer is finished (or we just started) */
            if (UNLIKELY(buffer_size == 0)) {

                /* print a simple progress meter */
                for ( ; memlength < bytes_remaining; bytes_remaining -= bytes_per_dot) {
                    /* for user, just print a dot */
                    print_a_dot();
                    /* for front-end, update percentage */
                    vars->scan_progress += progress_per_dot;
                }

                /* the whole region is finished */
                if (memlength == 0) break;

                /* stop scanning if asked to */
                if (vars->stop_flag) break;

                /* load the next buffer block */
                size_t read_size = MIN(memlength, MAX_ALLOC_SIZE);
                size_t nread = readmemory(data, reg_pos, read_size);
                if (nread < read_size) {
                    /* the region ends here, update `memlength` */
                    memlength = nread;
                    if ((nread == 0) && (reg_pos == r->start)) {
                        /* Failed on first read, which means region not exist. */
                        show_warn("reading region %02u failed.\n", regnum);
                        break;
                    }
                }
                /* If less than `MAX_ALLOC_SIZE` bytes remain, we have all of them
                 * in the buffer, so go all the way.
                 * Otherwise we need to stop at `MAX_BUFFER_SIZE`, so that
                 * the last byte we look at has a full VLT after it */
                buffer_size = memlength <= MAX_ALLOC_SIZE ? memlength : MAX_BUFFER_SIZE;
                buf_pos = data;
            }

            const mem64_t* memory_ptr = (mem64_t*)buf_pos;
            unsigned int match_length;
            match_flags checkflags;

            /* initialize checkflags */
            checkflags = flags_empty;

            /* check if we have a match */
            match_length = (*sm_scan_routine)(memory_ptr, memlength, NULL, uservalue, &checkflags);
            if (UNLIKELY(match_length > 0))
            {
                assert(match_length <= memlength);
                writing_swath_index = add_element(&(vars->matches), writing_swath_index, reg_pos,
                                                  get_u8b(memory_ptr), checkflags);
                
                ++vars->num_matches;
                
                required_extra_bytes_to_record = bytes_to_record(required_extra_bytes_to_record, match_length);
            }
            else if (required_extra_bytes_to_record)
            {
                writing_swath_index = add_element(&(vars->matches), writing_swath_index, reg_pos,
                                                  get_u8b(memory_ptr), flags_empty);
                --required_extra_bytes_to_record;
            }

        }

        free(data);

        /* stop scanning if asked to */
        if (vars->stop_flag) {
            printf("\n");
            break;
        }
        n = n->next;
        show_user("ok\n");
    }

    ENDINTERRUPTABLE();

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
        if (val_length > sizeof(memarray))
            val_length = sizeof(memarray);

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
