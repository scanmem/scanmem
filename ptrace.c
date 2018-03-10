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


bool sm_attach(pid_t target)
{
    int status;

    /* attach to the target application, which should cause a SIGSTOP */
    if (ptrace(PTRACE_ATTACH, target, NULL, NULL) == -1L) {
        show_error("failed to attach to %d, %s\n", target, strerror(errno));
        return false;
    }

    /* wait for the SIGSTOP to take place. */
    if (waitpid(target, &status, 0) == -1 || !WIFSTOPPED(status)) {
        show_error("there was an error waiting for the target to stop.\n");
        show_info("%s\n", strerror(errno));
        return false;
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
        if ((fd = open(mem, O_RDONLY)) == -1) {
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

    /* addr is ignored on Linux, but should be 1 on FreeBSD in order to let
     * the child process continue execution where it had been interrupted */
    return ptrace(PTRACE_DETACH, target, 1, 0) == 0;
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

    while (reading_swath.first_byte_in_child) {
        unsigned int match_length = 0;
        const mem64_t *memory_ptr;
        size_t memlength;
        match_flags checkflags;

        match_flags old_flags = reading_swath_index->data[reading_iterator].match_info;
        uint old_length = flags_to_memlength(vars->options.scan_data_type, old_flags);
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

            required_extra_bytes_to_record = match_length - 1;
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
    return sm_detach(vars->target);
}


/* sm_searchregions() performs an initial search of the process for values matching `uservalue` */
bool sm_searchregions(globals_t *vars, scan_match_type_t match_type, const uservalue_t *uservalue)
{
    matches_and_old_values_swath *writing_swath_index;
    int required_extra_bytes_to_record = 0;
    unsigned long total_size = 0;
    unsigned regnum = 0;
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

    /* check every memory region */
    while (n) {
        size_t nread = 0;
        int dots_remaining = NUM_DOTS;
        size_t bytes_at_next_dot;
        size_t bytes_per_dot;
        double progress_per_dot;

        /* load the next region */
        r = n->data;
        bytes_per_dot = r->size / NUM_DOTS;
        bytes_at_next_dot = bytes_per_dot;
        progress_per_dot = (double)bytes_per_dot / total_scan_bytes;

        /* allocate data array */
        if ((data = malloc(r->size * sizeof(char))) == NULL) {
            show_error("sorry, there was a memory allocation error.\n");
            return false;
        }

        /* print a progress meter so user knows we haven't crashed */
        /* cannot use show_info here because it'll append a '\n' */
        show_user("%02u/%02u searching %#10lx - %#10lx", ++regnum,
                vars->regions->size, (unsigned long)r->start, (unsigned long)r->start + r->size);
        fflush(stderr);

        /* read region into the `data` array */
        nread = readmemory(data, r->start, r->size);
        if (nread == 0) {
            show_error("reading region %02u failed.\n", regnum);
            free(data);
            return false;
        }

        /* region has been read, tell user */
        print_a_dot();

        /* For every offset, check if we have a match.
         * Testing `memlength > 0` is much faster than `offset < nread` */
        size_t memlength, offset;
        for (memlength = nread, offset = 0; memlength > 0; memlength--, offset++) {
            unsigned int match_length;
            const mem64_t* memory_ptr = (mem64_t*)(data+offset);
            match_flags checkflags;

            /* initialize checkflags */
            checkflags = flags_empty;

            /* check if we have a match */
            match_length = (*sm_scan_routine)(memory_ptr, memlength, NULL, uservalue, &checkflags);
            if (UNLIKELY(match_length > 0))
            {
                assert(match_length <= memlength);
                writing_swath_index = add_element(&(vars->matches), writing_swath_index, r->start+offset,
                                                  get_u8b(memory_ptr), checkflags);
                
                ++vars->num_matches;
                
                required_extra_bytes_to_record = match_length - 1;
            }
            else if (required_extra_bytes_to_record)
            {
                writing_swath_index = add_element(&(vars->matches), writing_swath_index, r->start+offset,
                                                  get_u8b(memory_ptr), flags_empty);
                --required_extra_bytes_to_record;
            }

            /* print a simple progress meter */
            if (UNLIKELY(offset >= bytes_at_next_dot)) {
                bytes_at_next_dot += bytes_per_dot;
                /* handle rounding */
                if (LIKELY(--dots_remaining > 0)) {
                    /* for user, just print a dot */
                    print_a_dot();
                    /* for front-end, update percentage */
                    vars->scan_progress += progress_per_dot;
                    /* stop scanning if asked to */
                    if (vars->stop_flag) break;
                }
            }
        }

        free(data);
        vars->scan_progress += progress_per_dot;
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

    uint val_length = flags_to_memlength(ANYNUMBER, to->flags);
    if (val_length > 0) {
        /* Basically, overwrite as much of the data as makes sense, and no more. */
        memcpy(memarray, to->bytes, val_length);
    }
    else {
        show_error("could not determine type to poke.\n");
        return false;
    }

    /* TODO: may use /proc/<pid>/mem here */
    /* Assume `sizeof(uint64_t)` is a multiple of `sizeof(long)` */
    for (i = 0; i < sizeof(uint64_t); i += sizeof(long))
    {
        if (ptrace(PTRACE_POKEDATA, target, addr + i, *(long*)(memarray + i)) == -1L) {
            return false;
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
    int i,j;
    long peek_value;

    if (sm_attach(target) == false) {
        return false;
    }

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

    return sm_detach(target);
}
