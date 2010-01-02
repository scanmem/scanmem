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
#endif

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

#include "value.h"
#include "scanroutines.h"
#include "scanmem.h"

#define min(a,b) (((a)<(b))?(a):(b))

/* ptrace peek buffer, used by peekdata() */
#define MAX_PEEKBUF_SIZE (4*sizeof(int64_t))
static struct {
    uint8_t cache[MAX_PEEKBUF_SIZE];  /* read from ptrace()  */
    unsigned size;              /* number of entries (in bytes) */
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
    int i, j;
    int shift_size1 = 0, shift_size2 = 0;
    char *last_address_gathered = 0;

    assert(peekbuf.size <= MAX_PEEKBUF_SIZE);
    assert(result != NULL);

    memset(result, 0x00, sizeof(value_t));

    valnowidth(result);
    result->how_to_calculate_values = BY_POINTER_SHIFTING;

    /* check if we have a cache hit */
    if (pid == peekbuf.pid &&
            reqaddr >= peekbuf.base &&
            (unsigned) (reqaddr + sizeof(int64_t) - peekbuf.base) <= peekbuf.size) {

        result->int_value =    *((int64_t *)&peekbuf.cache[reqaddr - peekbuf.base]);  /*lint !e826 */
        result->double_value = *( (double *)&peekbuf.cache[reqaddr - peekbuf.base]);  /*lint !e826 */
        result->float_value =  *(  (float *)&peekbuf.cache[reqaddr - peekbuf.base]);  /*lint !e826 */
        return true;
    } else if (pid == peekbuf.pid &&
            reqaddr >= peekbuf.base &&
            (unsigned) (reqaddr - peekbuf.base) < peekbuf.size) {

        assert(peekbuf.size != 0);

        /* partial hit, we have some of the data but not all, so remove old entries - shift the frame by as far as is necessary */
        /* tail shift, round up to nearest long size, for ptrace efficiency */
        shift_size1 = (reqaddr + sizeof(int64_t)) - (peekbuf.base + peekbuf.size);
        shift_size1 = sizeof(long) * (1 + (shift_size1-1) / sizeof(long));

        /* head shift */
        shift_size2 = reqaddr-peekbuf.base;
        shift_size2 = sizeof(long) * (shift_size2 / sizeof(long));

        for (i = shift_size2; i < peekbuf.size; ++i)
        {
            peekbuf.cache[i - shift_size2] = peekbuf.cache[i];
        }
        peekbuf.size -= shift_size2;
        peekbuf.base += shift_size2;
    } else {

        /* cache miss, invalidate cache */
        shift_size1 = shift_size2 = sizeof(int64_t);
        peekbuf.pid = pid;
        peekbuf.size = 0;
        peekbuf.base = addr;
    }

    /* we need a ptrace() to complete request */
    errno = 0;
    
    for (i = 0; i < shift_size1; i += sizeof(long))
    {
        char *ptrace_address = peekbuf.base + peekbuf.size + i;
        long ptraced_long = ptrace(PTRACE_PEEKDATA, pid, ptrace_address, NULL);

        /* check if ptrace() succeeded */
        if (EXPECT(ptraced_long == -1L && errno != 0, false)) {
            /* its possible i'm trying to read partially oob */
            if (errno == EIO || errno == EFAULT) {
                
                /* read backwards until we get a good read, then shift out the right value */
                for (j = 1, errno = 0; j < sizeof(long); j++, errno = 0) {
                
                    /* Try for a shifted ptrace - 'continue' (i.e. try an increased shift) if it fails */
                    if ((ptraced_long = ptrace(PTRACE_PEEKDATA, pid, ptrace_address - j, NULL)) == -1L && 
                        (errno == EIO || errno == EFAULT))
                            continue;
                    
                    /* Cache it with the appropriate offset */
                    *((long *)&peekbuf.cache[peekbuf.size - j]) = ptraced_long;
                    peekbuf.size += sizeof(long) - j;
                    last_address_gathered = ptrace_address + sizeof(long) - j;
                    
                    /* Interrupt the gathering process */
                    goto doublebreak;
                }
            }
            
            /* i wont print a message here, would be very noisy if a region is unmapped */
            return false;
        }
        
        /* Otherwise, ptrace() worked - cache the data, increase the size */
        *((long *)&peekbuf.cache[peekbuf.size]) = ptraced_long;
        peekbuf.size += sizeof(long);
        last_address_gathered = ptrace_address + sizeof(long);
    }
    
    doublebreak:
    
    /* Return result to caller */
    if (reqaddr + sizeof(int64_t) <= last_address_gathered)
    {
        /* The values are fine - read away */
        result->int_value =    *((int64_t *)&peekbuf.cache[reqaddr - peekbuf.base]);  /*lint !e826 */
        result->double_value = *( (double *)&peekbuf.cache[reqaddr - peekbuf.base]);  /*lint !e826 */
        result->float_value =  *(  (float *)&peekbuf.cache[reqaddr - peekbuf.base]);  /*lint !e826 */
    }
    else
    {
        int successful_gathering = last_address_gathered - reqaddr;
        
        /* We didn't get enough - add zeroes at the end */
        for (i = 0; i < sizeof(int64_t); ++i)
        {
            uint8_t val = (i < successful_gathering) ? peekbuf.cache[reqaddr - peekbuf.base + i] : 0;
            
                *(((uint8_t *)&result->int_value)    + i) = val;
                *(((uint8_t *)&result->double_value) + i) = val;
            if (i < sizeof(float))
                *(((uint8_t *)&result->float_value)  + i) = val;
        }
        
        /* Mark which values this can't be */
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

/* This is the function that handles when you enter a value (or >, <, =) for the second or later time (i.e. when there's already a list of matches); it reduces the list to those that still match. It returns false on failure to attach, detach, or reallocate memory, otherwise true.
"value" is what to compare to. It is meaningless when the match type is not MATCHEXACT. */
bool checkmatches(globals_t * vars, 
                  value_t value, 
                  scan_match_type_t match_type)
{
    matches_and_old_values_swath *reading_swath_index = (matches_and_old_values_swath *)vars->matches->swaths;
    matches_and_old_values_swath reading_swath = *reading_swath_index;
    int reading_iterator = 0;
    matches_and_old_values_swath *writing_swath_index = (matches_and_old_values_swath *)vars->matches->swaths;
    writing_swath_index->first_byte_in_child = NULL;
    writing_swath_index->number_of_bytes = 0;
    
    int required_extra_bytes_to_record = 0;
    vars->num_matches = 0;
    
    if (choose_scanroutine(vars->options.scan_data_type, match_type) == false)
    {
        fprintf(stderr, "error: unsupported scan for current data type.\n"); 
        return false;
    }

    assert(g_scan_routine);
    
    /* stop and attach to the target */
    if (attach(vars->target) == false)
        return false;


    bool use_old_value;
    if (  (match_type == MATCHNOTCHANGED)
        ||(match_type == MATCHCHANGED)
        ||(match_type == MATCHINCREASED)
        ||(match_type == MATCHDECREASED))
    {
        use_old_value = true;
    }
    else
    {
        use_old_value = false;
    }


    while (reading_swath.first_byte_in_child) {
        bool is_match = false;
        value_t data_value;
        value_t check;

        void *address = reading_swath.first_byte_in_child + reading_iterator;
        
        /* Read value from this address */
        if (EXPECT(peekdata(vars->target, address, &data_value) == false, false)) {
            /* Uhh, what? We couldn't look at the data there? I guess this doesn't count as a match then */
        }
        else
        {
            value_t old_val = data_to_val_aux((unknown_type_of_swath *)reading_swath_index, reading_iterator, reading_swath.number_of_bytes, MATCHES_AND_VALUES);

            match_flags flags = reading_swath_index->data[reading_iterator].match_info;
            truncval_to_flags(&old_val, flags);

            truncval_to_flags(&data_value, flags);

            memset(&check, 0, sizeof(check));
            check.how_to_calculate_values = BY_POINTER_SHIFTING;

            if (use_old_value) {
                value = old_val;
            }

            is_match = (*g_scan_routine)(&data_value, &value, &check);
            
            /*
            / * Inequalities get special handling * /
            else if (type == MATCHGREATERTHAN || type == MATCHLESSTHAN)
            {
                scan_match_type_t reverse_type = (type == MATCHGREATERTHAN) ? MATCHLESSTHAN : MATCHGREATERTHAN;
                bool works_forwards = false, works_reverse = false;
                value_t saved1, saved2;
                if (flags.ineq_forwards && valuecmp(&check, type, &value, &saved1)) works_forwards = true;
                if (flags.ineq_reverse && valuecmp(&check, reverse_type, &value, &saved2)) works_reverse = true;
                
                if (works_forwards || works_reverse)
                {
                    is_match = true;
                    if (works_forwards && works_reverse) { valuecmp(&check, MATCHNOTEQUAL, &value, &check); check.flags.ineq_forwards = check.flags.ineq_reverse = 1; }
                    else if (works_forwards) { check = saved1; check.flags.ineq_forwards = 1; check.flags.ineq_reverse = 0; }
                    else if (works_reverse) { check = saved2; check.flags.ineq_reverse = 1; check.flags.ineq_forwards = 0; }
                    else { check.flags.ineq_forwards = check.flags.ineq_reverse = 0; }
                }
            }
            */
        }
        
        if (is_match)
        {
            /* Still a candidate. Write data. 
                (We can get away with overwriting in the same array because it is guaranteed to take up the same number of bytes or fewer, and because we copied out the reading swath metadata already.)
                (We can get away with assuming that the pointers will stay valid, because as we never add more data to the array than there was before, it will not reallocate.) */
          
            old_value_and_match_info new_value = { get_u8b(&data_value), check.flags };
            writing_swath_index = add_element((unknown_type_of_array **)(&vars->matches), (unknown_type_of_swath *)writing_swath_index, address, &new_value, MATCHES_AND_VALUES);
            
            ++vars->num_matches;
            
            required_extra_bytes_to_record = val_max_width_in_bytes(&check) - 1;
        }
        else if (required_extra_bytes_to_record)
        {
            old_value_and_match_info new_value = { get_u8b(&data_value), (match_flags){0} };
            writing_swath_index = add_element((unknown_type_of_array **)(&vars->matches), (unknown_type_of_swath *)writing_swath_index, address, &new_value, MATCHES_AND_VALUES);
            --required_extra_bytes_to_record;
        }
        
        /* Go on to the next one... */
        ++reading_iterator;
        if (reading_iterator >= reading_swath.number_of_bytes)
        {
            assert(((matches_and_old_values_swath *)(&reading_swath_index->data[reading_swath.number_of_bytes]))->number_of_bytes >= 0);
            reading_swath_index = (matches_and_old_values_swath *)(&reading_swath_index->data[reading_swath.number_of_bytes]);
            reading_swath = *reading_swath_index;
            reading_iterator = 0;
            required_extra_bytes_to_record = 0; /* just in case */
        }
    }
    
    if (!(vars->matches = null_terminate((unknown_type_of_array *)vars->matches, (unknown_type_of_swath *)writing_swath_index, MATCHES_AND_VALUES)))
    {
        fprintf(stderr, "error: memory allocation error while reducing matches-array size\n");
        return false;
    }

    eprintf("info: we currently have %ld matches.\n", vars->num_matches);

    /* okay, detach */
    return detach(vars->target);
}

/* read region using /proc/pid/mem */
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
    len = pread(fd, buf, MIN(region->size - offset, max), (unsigned long)region->start + offset);
    
    /* clean up */
    close(fd);
    
    return len;
}
    

/* searchregions() performs an initial search of the process for values matching value */
bool searchregions(globals_t * vars,
                value_t value, bool snapshot)
{
    matches_and_old_values_swath *writing_swath_index;
    int required_extra_bytes_to_record = 0;
    long total_size = 0;
    unsigned regnum = 0;
    unsigned char *data = NULL;
    element_t *n = vars->regions->head;
    region_t *r;

    if (choose_scanroutine(vars->options.scan_data_type, MATCHEQUALTO) == false)
    {
        fprintf(stderr, "error: unsupported scan for current data type.\n"); 
        return false;
    }

    assert(g_scan_routine);

    /* stop and attach to the target */
    if (attach(vars->target) == false)
        return false;

   
    /* make sure we have some regions to search */
    if (vars->regions->size == 0) {
        fprintf(stderr,
                "warn: no regions defined, perhaps you deleted them all?\n");
        fprintf(stderr,
                "info: use the \"reset\" command to refresh regions.\n");
        return detach(vars->target);
    }
    
    while (n) {
        total_size += ((region_t *)(n->data))->size * sizeof(old_value_and_match_info) + sizeof(matches_and_old_values_swath);
        n = n->next;
    }
    
    total_size += sizeof(matches_and_old_values_swath);
    
    if (!(vars->matches = allocate_array((unknown_type_of_array *)vars->matches, total_size)))
    {
        fprintf(stderr, "error: could not allocate match array\n");
        return false;
    }
    
    writing_swath_index = (matches_and_old_values_swath *)vars->matches->swaths;
    
    writing_swath_index->first_byte_in_child = NULL;
    writing_swath_index->number_of_bytes = 0;
    
    n = vars->regions->head;

    /* check every memory region */
    while (n) {
        unsigned offset, nread = 0;
        ssize_t len = 0;

        /* load the next region */
        r = n->data;

#if HAVE_PROCMEM        
        /* over allocate by enough bytes set to zero that the last bytes can be read as 64-bit ints */
        if ((data = calloc(r->size + sizeof(int64_t) - 1, 1)) == NULL) {
            fprintf(stderr, "error: sorry, there was a memory allocation error.\n");
            return false;
        }
    
        /* keep reading until completed */
        while (nread < r->size) {
            if ((len = readregion(data + nread, vars->target, r, nread, r->size)) == -1) {
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
        /* print a progress meter so user knows we havent crashed */
        fprintf(stderr, "info: %02u/%02u searching %#10lx - %#10lx.", ++regnum,
                vars->regions->size, (unsigned long)r->start, (unsigned long)r->start + r->size);
        fflush(stderr);

        /* for every offset, check if we have a match */
        for (offset = 0; offset < nread; offset++) {
            value_t check;
            value_t data_value;
           
            /* initialise check */
            memset(&data_value, 0x00, sizeof(data_value));
            
            valnowidth(&data_value);

#if HAVE_PROCMEM           
            data_value.int_value    = *((int64_t *)(&data[offset]));
            data_value.double_value = *( (double *)(&data[offset]));
            data_value.float_value  = *(  (float *)(&data[offset]));
            data_value.how_to_calculate_values = BY_POINTER_SHIFTING;
            
            /* Mark which values this can't be */
            if (nread - offset < sizeof(int64_t))
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
            if (EXPECT(peekdata(vars->target, r->start + offset, &data_value) == false, false)) {
                break;
            }
#endif

            check = data_value;
            memset(&check.flags, 0, sizeof(check.flags));
            
            /* check if we have a match */
            if (snapshot || EXPECT((*g_scan_routine)(&value, &data_value, &check), false)) {
                check.flags.ineq_forwards = check.flags.ineq_reverse = 1;
                old_value_and_match_info new_value = { get_u8b(&data_value), check.flags };
                writing_swath_index = add_element((unknown_type_of_array **)(&vars->matches), (unknown_type_of_swath *)writing_swath_index, r->start + offset, &new_value, MATCHES_AND_VALUES);
                
                ++vars->num_matches;
                
                required_extra_bytes_to_record = val_max_width_in_bytes(&check) - 1;
            }
            else if (required_extra_bytes_to_record)
            {
                old_value_and_match_info new_value = { get_u8b(&data_value), (match_flags){0} };
                writing_swath_index = add_element((unknown_type_of_array **)(&vars->matches), (unknown_type_of_swath *)writing_swath_index, r->start + offset, &new_value, MATCHES_AND_VALUES);
                --required_extra_bytes_to_record;
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
    
    if (!(vars->matches = null_terminate((unknown_type_of_array *)vars->matches, (unknown_type_of_swath *)writing_swath_index, MATCHES_AND_VALUES)))
    {
        fprintf(stderr, "error: memory allocation error while reducing matches-array size\n");
        return false;
    }

    eprintf("info: we currently have %ld matches.\n", vars->num_matches);

    /* okay, detach */
    return detach(vars->target);
}

bool setaddr(pid_t target, void *addr, const value_t * to)
{
    value_t saved;
    int i;

    if (attach(target) == false) {
        return false;
    }

    if (peekdata(target, addr, &saved) == false) {
        fprintf(stderr, "error: couldnt access the target address %10p\n",
                addr);
        return false;
    }
    
    /* Basically, overwrite as much of the data as makes sense, and no more. */
         if (saved.flags.u64b && to->flags.u64b) { set_u64b(&saved, get_u64b(to)); }
    else if (saved.flags.s64b && to->flags.s64b) { set_s64b(&saved, get_s64b(to)); }
    else if (saved.flags.f64b && to->flags.f64b) { set_s64b(&saved, *((int64_t *)&(to->double_value))); } 
    else if (saved.flags.u32b && to->flags.u32b) { set_u32b(&saved, get_u32b(to)); }
    else if (saved.flags.s32b && to->flags.s32b) { set_s32b(&saved, get_s32b(to)); }
    else if (saved.flags.f32b && to->flags.f32b) { set_s32b(&saved, *((int32_t *)&(to->float_value))); } 
    else if (saved.flags.u16b && to->flags.u16b) { set_u16b(&saved, get_u16b(to)); }
    else if (saved.flags.s16b && to->flags.s16b) { set_s16b(&saved, get_s16b(to)); }
    else if (saved.flags.u8b  && to->flags.u8b ) { set_u8b(&saved, get_u8b(to)); }
    else if (saved.flags.s8b  && to->flags.s8b ) { set_s8b(&saved, get_s8b(to)); }
    else {
        fprintf(stderr, "error: could not determine type to poke.\n");
        return false;
    }

    /* TODO: may use /proc/<pid>/mem here */
    /* assume that sizeof(save.int_value) (int64_t) is multiple of sizeof(long) */
    for (i = 0; i < sizeof(saved.int_value); i += sizeof(long)) 
    {
        if (ptrace(PTRACE_POKEDATA, target, addr + i, *(long *)(((int8_t *)&saved.int_value) + i)) == -1L) {
            return false;
        }
    }

    return detach(target);
}

/* TODO: may use /proc/<pid>/mem here */
bool write_array(pid_t target, void *addr, const void *data, int len)
{
    int i,j;
    long peek_value;

    if (attach(target) == false) {
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
            /* try all possible shifting read & write */
            for(j = 0; j <= sizeof(long) - (len - i); ++j)
            {
                errno = 0;
                if(((peek_value = ptrace(PTRACE_PEEKDATA, target, addr - j, NULL)) == -1L) && (errno != 0))
                {
                    if (errno == EIO || errno == EFAULT) /* may try next shift */
                        continue;
                    else
                    {
                        fprintf(stderr, "error: write_array failed.\n"); 
                        return false;
                    }
                }
                else /* peek success */
                {
                    /* write back */
                    memcpy(((int8_t*)&peek_value)+j, data+i, len-i);        

                    if (ptrace(PTRACE_POKEDATA, target, addr - j, peek_value) == -1L)
                    {
                        fprintf(stderr, "error: write_array failed.\n");
                        return false;
                    }

                    break;
                }
            }
        }
    }

    return detach(target);
}
