/*
    Functions to access the memory of the target process.
 
    Copyright (C) 2006,2007,2009 Tavis Ormandy <taviso@sdf.lonestar.org>
    Copyright (C) 2009           Eli Dupree <elidupree@charter.net>
    Copyright (C) 2009,2010      WANG Lu <coolwanglu@gmail.com>
    Copyright (C) 2015           Sebastian Parschauer <s.parschauer@gmx.de>
 
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

#ifdef __GNUC__
# define EXPECT(x,y) __builtin_expect(x, y)
#else
# define EXPECT(x,y) x
#endif

// dirty hack for FreeBSD
#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
#define PTRACE_ATTACH PT_ATTACH
#define PTRACE_DETACH PT_DETACH
#define PTRACE_PEEKDATA PT_READ_D
#define PTRACE_POKEDATA PT_WRITE_D
#endif

#include "value.h"
#include "endianness.h"
#include "scanroutines.h"
#include "scanmem.h"
#include "show_message.h"

/* progress handling */
#define NUM_DOTS (10)
#define NUM_SAMPLES (100)
#define MAX_PROGRESS (1.0)  /* 100% */
#if (!NUM_DOTS || !NUM_SAMPLES || NUM_SAMPLES % NUM_DOTS != 0)
#error Invalid NUM_DOTS to NUM_SAMPLES proportion!
#endif
#define SAMPLES_PER_DOT (NUM_SAMPLES / NUM_DOTS)
#define PROGRESS_PER_SAMPLE (MAX_PROGRESS / NUM_SAMPLES)

/* ptrace peek buffer, used by peekdata() */
/* make it larger in order to reduce shift */
/* #define MAX_PEEKBUF_SIZE (4*sizeof(int64_t)) */
#define MAX_PEEKBUF_SIZE (1024)
static struct {
    uint8_t cache[MAX_PEEKBUF_SIZE];  /* read from ptrace()  */
    unsigned size;              /* number of entries (in bytes) */
    char *base;                 /* base address of cached region */
    pid_t pid;                  /* what pid this applies to */
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

    /* flush the peek buffer */
    memset(&peekbuf, 0x00, sizeof(peekbuf));

    /* everything looks okay */
    return true;

}

bool sm_detach(pid_t target)
{
    // addr is ignored on Linux, but should be 1 on FreeBSD in order to let the child process continue execution where it had been interrupted
    return ptrace(PTRACE_DETACH, target, 1, 0) == 0;
}


/*
 * sm_peekdata - caches overlapping ptrace reads to improve performance.
 * 
 * This routine could just call ptrace(PEEKDATA), but using a cache reduces
 * the number of peeks required by 70% when reading large chunks of
 * consecutive addresses.
 */

bool sm_peekdata(pid_t pid, void *addr, value_t * result)
{
    char *reqaddr = addr;
    int i, j;
    int shift_size1 = 0, shift_size2 = 0;
    char *last_address_gathered = 0;

    assert(peekbuf.size <= MAX_PEEKBUF_SIZE);
    assert(result != NULL);

    /* initialize result */
    zero_value(result);
    valnowidth(result);

    /* check if we have a cache hit */
    if (pid == peekbuf.pid &&
            reqaddr >= peekbuf.base &&
            (unsigned long) (reqaddr + sizeof(int64_t) - peekbuf.base) <= peekbuf.size) {
        memcpy(&result->int64_value, &peekbuf.cache[reqaddr - peekbuf.base], sizeof(result->int64_value));
        return true;
    } else if (pid == peekbuf.pid &&
            reqaddr >= peekbuf.base &&
            (unsigned long) (reqaddr - peekbuf.base) < peekbuf.size) {

        assert(peekbuf.size != 0);

        /* partial hit, we have some of the data but not all, so remove old entries - shift the frame by as far as is necessary */
        /* tail shift, round up to nearest long size for ptrace efficiency */
        shift_size1 = (reqaddr + sizeof(int64_t)) - (peekbuf.base + peekbuf.size);
        shift_size1 = sizeof(long) * (1 + (shift_size1-1) / sizeof(long));

        /* head shift if necessary */
        if (peekbuf.size + shift_size1 > MAX_PEEKBUF_SIZE) 
        {
            shift_size2 = reqaddr-peekbuf.base;
            shift_size2 = sizeof(long) * (shift_size2 / sizeof(long));

            for (i = shift_size2; i < peekbuf.size; ++i)
            {
                peekbuf.cache[i - shift_size2] = peekbuf.cache[i];
            }
            peekbuf.size -= shift_size2;
            peekbuf.base += shift_size2;
        }
    } else {

        /* cache miss, invalidate the cache */
        shift_size1 = shift_size2 = sizeof(int64_t);
        peekbuf.pid = pid;
        peekbuf.size = 0;
        peekbuf.base = addr;
    }

    /* we need a ptrace() to complete the request */
    errno = 0;
    
    for (i = 0; i < shift_size1; i += sizeof(long))
    {
        char *ptrace_address = peekbuf.base + peekbuf.size;
        long ptraced_long = ptrace(PTRACE_PEEKDATA, pid, ptrace_address, NULL);

        /* check if ptrace() succeeded */
        if (EXPECT(ptraced_long == -1L && errno != 0, false)) {
            /* it's possible i'm trying to read partially oob */
            if (errno == EIO || errno == EFAULT) {
                
                /* read backwards until we get a good read, then shift out the right value */
                for (j = 1, errno = 0; j < sizeof(long); j++, errno = 0) {
                
                    /* try for a shifted ptrace - 'continue' (i.e. try an increased shift) if it fails */
                    if ((ptraced_long = ptrace(PTRACE_PEEKDATA, pid, ptrace_address - j, NULL)) == -1L && 
                        (errno == EIO || errno == EFAULT))
                            continue;
                    
                    /* cache it with the appropriate offset */
                    if(peekbuf.size >= j)
                    {
                        memcpy(&peekbuf.cache[peekbuf.size - j], &ptraced_long, sizeof(long));
                    }
                    else
                    {
                        memcpy(&peekbuf.cache[0], &ptraced_long, sizeof(long));
                        peekbuf.base -= j;
                    }
                    peekbuf.size += sizeof(long) - j;
                    last_address_gathered = ptrace_address + sizeof(long) - j;
                    
                    /* interrupt the gathering process */
                    goto doublebreak;
                }
            }
            
            /* I wont print a message here, would be very noisy if a region is unmapped */
            return false;
        }
        
        /* otherwise, ptrace() worked - cache the data and increase the size */
        memcpy(&peekbuf.cache[peekbuf.size], &ptraced_long, sizeof(long));
        peekbuf.size += sizeof(long);
        last_address_gathered = ptrace_address + sizeof(long);
    }
    
    doublebreak:
    
    /* return result to caller */
    if (reqaddr + sizeof(int64_t) <= last_address_gathered)
    {
        /* the values are fine - read away */
        memcpy(&result->int64_value, &peekbuf.cache[reqaddr - peekbuf.base], sizeof(result->int64_value));
    }
    else
    {
        int successful_gathering = last_address_gathered - reqaddr;
        
        /* we didn't get enough - add zeroes at the end */
        for (i = 0; i < sizeof(int64_t); ++i)
        {
            uint8_t val = (i < successful_gathering) ? peekbuf.cache[reqaddr - peekbuf.base + i] : 0;
            *(((uint8_t *)&result->int64_value)    + i) = val;
        }
        
        /* mark which values this can't be */
        if (successful_gathering < sizeof(int64_t))
            result->flags.u64b = result->flags.s64b = result->flags.f64b = 0;
        if (successful_gathering < sizeof(int32_t))
            result->flags.u32b = result->flags.s32b = result->flags.f32b = 0;
        if (successful_gathering < sizeof(int16_t))
            result->flags.u16b = result->flags.s16b = 0;
        if (successful_gathering < sizeof(int8_t))
            result->flags.u8b  = result->flags.s8b  = 0;
    }

    /* success */
    return true;
}

static inline void print_a_dot(void)
{
    fprintf(stderr, ".");
    fflush(stderr);
}

/* This is the function that handles when you enter a value (or >, <, =) for the second or later time (i.e. when there's already a list of matches); it reduces the list to those that still match. It returns false on failure to attach, detach, or reallocate memory; otherwise, true.
"value" is what to compare to. It is meaningless when the match type is not MATCHEXACT. */
bool sm_checkmatches(globals_t *vars,
                     scan_match_type_t match_type,
                     const uservalue_t *uservalue)
{
    matches_and_old_values_swath *reading_swath_index = (matches_and_old_values_swath *)vars->matches->swaths;
    matches_and_old_values_swath reading_swath = *reading_swath_index;

    long bytes_scanned = 0;
    long total_scan_bytes = 0;
    matches_and_old_values_swath *tmp_swath_index = reading_swath_index;
    int samples_remaining = NUM_SAMPLES;
    int samples_to_dot = SAMPLES_PER_DOT;
    size_t bytes_at_next_sample;
    size_t bytes_per_sample;

    while(tmp_swath_index->number_of_bytes)
    {
        total_scan_bytes += tmp_swath_index->number_of_bytes;
        tmp_swath_index = (matches_and_old_values_swath *)(&tmp_swath_index->data[tmp_swath_index->number_of_bytes]);
    }
    bytes_per_sample = total_scan_bytes / NUM_SAMPLES;
    bytes_at_next_sample = bytes_per_sample;
    /* for user, just print the first dot */
    print_a_dot();

    int reading_iterator = 0;
    matches_and_old_values_swath *writing_swath_index = (matches_and_old_values_swath *)vars->matches->swaths;
    writing_swath_index->first_byte_in_child = NULL;
    writing_swath_index->number_of_bytes = 0;
    
    /* used to fill in non-match regions */
    match_flags zero_flag;
    zero_match_flags(&zero_flag);

    int required_extra_bytes_to_record = 0;
    vars->num_matches = 0;
    vars->scan_progress = 0.0;
    vars->stop_flag = false;
    
    if (sm_choose_scanroutine(vars->options.scan_data_type, match_type) == false)
    {
        show_error("unsupported scan for current data type.\n"); 
        return false;
    }

    assert(sm_scan_routine);
    
    /* stop and attach to the target */
    if (sm_attach(vars->target) == false)
        return false;
    while (reading_swath.first_byte_in_child) {
        int match_length = 0;
        value_t data_value;
        match_flags checkflags;

        void *address = reading_swath.first_byte_in_child + reading_iterator;
        
        /* read value from this address */
        if (EXPECT(sm_peekdata(vars->target, address, &data_value) == false, false)) {
            /* Uhh, what? We couldn't look at the data there? I guess this doesn't count as a match then */
        }
        else
        {
            value_t old_val = data_to_val_aux(reading_swath_index, reading_iterator, reading_swath.number_of_bytes /* ,MATCHES_AND_VALUES */);

            match_flags flags = reading_swath_index->data[reading_iterator].match_info;
            /* these are not harmful for bytearray routine, since it will ignore flags of new_value and old_value */
            truncval_to_flags(&old_val, flags);
            truncval_to_flags(&data_value, flags);

            fix_endianness(vars, &data_value);

            zero_match_flags(&checkflags);

            match_length = (*sm_scan_routine)(&data_value, &old_val, uservalue, &checkflags, address);
        }
        
        if (match_length > 0)
        {
            /* Still a candidate. Write data. 
                (We can get away with overwriting in the same array because it is guaranteed to take up the same number of bytes or fewer, and because we copied out the reading swath metadata already.)
                (We can get away with assuming that the pointers will stay valid, because as we never add more data to the array than there was before, it will not reallocate.) */
          
            old_value_and_match_info new_value = { get_u8b(&data_value), checkflags };
            writing_swath_index = add_element((&vars->matches), writing_swath_index, address, &new_value /* ,MATCHES_AND_VALUES */);
            
            ++vars->num_matches;
            
            required_extra_bytes_to_record = match_length - 1;
        }
        else if (required_extra_bytes_to_record)
        {
            old_value_and_match_info new_value = { get_u8b(&data_value), zero_flag };
            writing_swath_index = add_element(&vars->matches, writing_swath_index, address, &new_value /* ,MATCHES_AND_VALUES */);
            --required_extra_bytes_to_record;
        }

        if (EXPECT(bytes_scanned >= bytes_at_next_sample, false)) {
            bytes_at_next_sample += bytes_per_sample;
            /* handle rounding */
            if (EXPECT(--samples_remaining > 0, true)) {
                /* for front-end, update percentage */
                vars->scan_progress += PROGRESS_PER_SAMPLE;
                if (EXPECT(--samples_to_dot == 0, false)) {
                    samples_to_dot = SAMPLES_PER_DOT;
                    /* for user, just print a dot */
                    print_a_dot();
                }
                /* stop scanning if asked to */
                if (vars->stop_flag) break;
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
    
    if (!(vars->matches = null_terminate(vars->matches, writing_swath_index /* ,MATCHES_AND_VALUES */)))
    {
        show_error("memory allocation error while reducing matches-array size\n");
        return false;
    }

    /* tell front-end we've finished */
    vars->scan_progress = MAX_PROGRESS;

    show_info("we currently have %ld matches.\n", vars->num_matches);

    /* okay, detach */
    return sm_detach(vars->target);
}

/* read region using /proc/pid/mem */
ssize_t readregion(pid_t target, void *buf, size_t count, unsigned long offset)
{
    char mem[32];
    int fd;
    ssize_t len;
    
    /* print the path to mem file */
    snprintf(mem, sizeof(mem), "/proc/%d/mem", target);
    
    /* attempt to open the file */
    if ((fd = open(mem, O_RDONLY)) == -1) {
        show_error("unable to open %s.\n", mem);
        return -1;
    }

    /* try to honor the request */
    len = pread(fd, buf, count, offset);
    
    /* clean up */
    close(fd);
    
    return len;
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
    void *address = NULL;

#if HAVE_PROCMEM
    unsigned char *data = NULL;
    ssize_t len = 0;
#endif

    /* used to fill in non-match regions */
    match_flags zero_flag;
    zero_match_flags(&zero_flag);
    
    if (sm_choose_scanroutine(vars->options.scan_data_type, match_type) == false)
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
    
    writing_swath_index = (matches_and_old_values_swath *)vars->matches->swaths;
    
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
        unsigned offset, nread = 0;
        int dots_remaining = NUM_DOTS;
        size_t bytes_at_next_dot;
        size_t bytes_per_dot;
        double progress_per_dot;

        /* load the next region */
        r = n->data;
        bytes_per_dot = r->size / NUM_DOTS;
        bytes_at_next_dot = bytes_per_dot;
        progress_per_dot = (double)bytes_per_dot / total_scan_bytes;

#if HAVE_PROCMEM        
        /* overallocate by enough bytes set to zero that the last bytes can be read as 64-bit ints */
        if ((data = calloc(r->size + sizeof(int64_t) - 1, 1)) == NULL) {
            show_error("sorry, there was a memory allocation error.\n");
            return false;
        }
    
        /* keep reading until completed */
        while (nread < r->size) {
            if ((len = readregion(vars->target, data+nread, r->size-nread, (unsigned long)(r->start+nread))) == -1) {
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
        /* print a progress meter so user knows we haven't crashed */
        /* cannot use show_info here because it'll append a '\n' */
        show_user("%02u/%02u searching %#10lx - %#10lx.", ++regnum,
                vars->regions->size, (unsigned long)r->start, (unsigned long)r->start + r->size);
        fflush(stderr);

        /* for every offset, check if we have a match */
        for (offset = 0; offset < nread; offset++) {
            match_flags checkflags;
            value_t data_value;
           
            /* initialize data_value */
            zero_value(&data_value);
            valnowidth(&data_value);

            address = r->start + offset;

#if HAVE_PROCMEM
            /* Don't dereference as this causes an alignment issue e.g. on ARM.
               GCC replaces memcpy() with dereferencing wherever possible. */
            memcpy(&data_value.int64_value, &data[offset], sizeof(int64_t));

            /* mark which values this can't be */
            if (EXPECT((nread - offset < sizeof(int64_t)), false))
            {
                data_value.flags.u64b = data_value.flags.s64b = data_value.flags.f64b = 0;
                if (nread - offset < sizeof(int32_t))
                {
                    data_value.flags.u32b = data_value.flags.s32b = data_value.flags.f32b = 0;
                    if (nread - offset < sizeof(int16_t))
                    {
                        data_value.flags.u16b = data_value.flags.s16b = 0;
                        if (nread - offset < sizeof(int8_t))
                        {
                            data_value.flags.u8b  = data_value.flags.s8b  = 0;
                        }
                    }
                }
            }
#else
            if (EXPECT(sm_peekdata(vars->target, address, &data_value) == false, false)) {
                break;
            }
#endif

            fix_endianness(vars, &data_value);

            /* initialize checkflags */
            zero_match_flags(&checkflags);

            int match_length;
            /* check if we have a match */
            if (EXPECT(((match_length = (*sm_scan_routine)(&data_value, NULL,
                    uservalue, &checkflags, address)) > 0), false)) {
                /* only set these flags for numbers */
                if ((vars->options.scan_data_type != BYTEARRAY)
                    && (vars->options.scan_data_type != STRING))
                {
                    checkflags.ineq_forwards = checkflags.ineq_reverse = 1;
                }
                old_value_and_match_info new_value = { get_u8b(&data_value), checkflags };
                writing_swath_index = add_element((&vars->matches), writing_swath_index, r->start + offset, &new_value);
                
                ++vars->num_matches;
                
                required_extra_bytes_to_record = match_length - 1;
            }
            else if (required_extra_bytes_to_record)
            {
                old_value_and_match_info new_value = { get_u8b(&data_value), zero_flag };
                writing_swath_index = add_element((&vars->matches), writing_swath_index, r->start + offset, &new_value);
                --required_extra_bytes_to_record;
            }

            /* print a simple progress meter */
            if (EXPECT(offset >= bytes_at_next_dot, false)) {
                bytes_at_next_dot += bytes_per_dot;
                /* handle rounding */
                if (EXPECT(--dots_remaining > 0, true)) {
                    /* for user, just print a dot */
                    print_a_dot();
                    /* for front-end, update percentage */
                    vars->scan_progress += progress_per_dot;
                    /* stop scanning if asked to */
                    if (vars->stop_flag) break;
                }
            }
        }

#if HAVE_PROCMEM
        free(data);
#endif
        vars->scan_progress += progress_per_dot;
        /* stop scanning if asked to */
        if (vars->stop_flag) break;
        n = n->next;
        show_user("ok\n");
    }

    /* tell front-end we've finished */
    vars->scan_progress = MAX_PROGRESS;
    
    if (!(vars->matches = null_terminate(vars->matches, writing_swath_index /* ,MATCHES_AND_VALUES */)))
    {
        show_error("memory allocation error while reducing matches-array size\n");
        return false;
    }

    show_info("we currently have %ld matches.\n", vars->num_matches);

    /* okay, detach */
    return sm_detach(vars->target);
}

bool sm_setaddr(pid_t target, void *addr, const value_t *to)
{
    value_t saved;
    int i;

    if (sm_attach(target) == false) {
        return false;
    }

    if (sm_peekdata(target, addr, &saved) == false) {
        show_error("couldnt access the target address %10p\n", addr);
        return false;
    }
    
    /* Basically, overwrite as much of the data as makes sense, and no more. */
    /* about float/double: now value_t is a union, we can use the following way instead of the commented way, in order to avoid compiler warnings */
         if (saved.flags.u64b && to->flags.u64b) { set_u64b(&saved, get_u64b(to)); }
    else if (saved.flags.s64b && to->flags.s64b) { set_s64b(&saved, get_s64b(to)); }
    else if (saved.flags.f64b && to->flags.f64b) { set_s64b(&saved, get_s64b(to)); } /* *((int64_t *)&(to->float64_value))); } */
    else if (saved.flags.u32b && to->flags.u32b) { set_u32b(&saved, get_u32b(to)); }
    else if (saved.flags.s32b && to->flags.s32b) { set_s32b(&saved, get_s32b(to)); }
    else if (saved.flags.f32b && to->flags.f32b) { set_s32b(&saved, get_s32b(to)); } /* *((int32_t *)&(to->float32_value))); } */
    else if (saved.flags.u16b && to->flags.u16b) { set_u16b(&saved, get_u16b(to)); }
    else if (saved.flags.s16b && to->flags.s16b) { set_s16b(&saved, get_s16b(to)); }
    else if (saved.flags.u8b  && to->flags.u8b ) { set_u8b(&saved, get_u8b(to)); }
    else if (saved.flags.s8b  && to->flags.s8b ) { set_s8b(&saved, get_s8b(to)); }
    else {
        show_error("could not determine type to poke.\n");
        return false;
    }

    /* TODO: may use /proc/<pid>/mem here */
    /* assume that sizeof(save.int64_value) (int64_t) is multiple of sizeof(long) */
    for (i = 0; i < sizeof(saved.int64_value); i += sizeof(long)) 
    {
        if (ptrace(PTRACE_POKEDATA, target, addr + i, *(long *)(((int8_t *)&saved.int64_value) + i)) == -1L) {
            return false;
        }
    }

    return sm_detach(target);
}

bool sm_read_array(pid_t target, void *addr, char *buf, int len)
{
    if (sm_attach(target) == false) {
        return false;
    }

#if HAVE_PROCMEM
    unsigned nread=0;
    ssize_t tmpl;
    while (nread < len) {
        if ((tmpl = readregion(target, buf+nread, len-nread, (unsigned long)(addr+nread))) == -1) {
            /* no, continue with whatever data was read */
            break;
        } else {
            /* some data was read */
            nread += tmpl;
        }
    }

    if (nread < len)
    {
        sm_detach(target);
        return false;
    }

    return sm_detach(target);
#else
    int i;
    /* here we just read long by long, this should be ok for most of time */
    /* partial hit is not handled */
    for(i = 0; i < len; i += sizeof(long))
    {
        errno = 0;
        *((long *)(buf+i)) = ptrace(PTRACE_PEEKDATA, target, addr+i, NULL);
        if (EXPECT((*((long *)(buf+i)) == -1L) && (errno != 0), false)) {
            sm_detach(target);
            return false;
        }
    }
    return sm_detach(target);
#endif
}

/* TODO: may use /proc/<pid>/mem here */
bool sm_write_array(pid_t target, void *addr, const void *data, int len)
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
