/*
    Specific command handling.

    Copyright (C) 2006,2007,2009 Tavis Ormandy <taviso@sdf.lonestar.org>
    Copyright (C) 2009           Eli Dupree <elidupree@charter.net>
    Copyright (C) 2009,2010      WANG Lu <coolwanglu@gmail.com>
    Copyright (C) 2014-2016      Sebastian Parschauer <s.parschauer@gmx.de>

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
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <getopt.h>
#include <signal.h>
#include <assert.h>
#include <setjmp.h>
#include <alloca.h>
#include <strings.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <sys/time.h>
#include <limits.h>            /* to determine the word width */
#include <errno.h>
#include <inttypes.h>
#include <ctype.h>

#include "common.h"
#include "commands.h"
#include "endianness.h"
#include "handlers.h"
#include "interrupt.h"
#include "scanmem.h"
#include "scanroutines.h"
#include "sets.h"
#include "show_message.h"

#define USEPARAMS() ((void) vars, (void) argv, (void) argc)     /* macro to hide gcc unused warnings */

/*
 * This file defines all the command handlers used, each one is registered using
 * registercommand(). When a matching command is entered, the commandline is
 * tokenized and parsed into an argv/argc.
 * 
 * argv[0] will contain the command entered, so one handler can handle multiple
 * commands by checking what's in there. You still need seperate documentation
 * for each command when you register it.
 *
 * Most commands will also need some documentation, see handlers.h for the format.
 *
 * Commands are allowed to read and modify settings in the vars structure.
 *
 */

#define calloca(x,y) (memset(alloca((x) * (y)), 0x00, (x) * (y)))

/* try to determine the size of a pointer */
#ifndef ULONG_MAX
#warning ULONG_MAX is not defined!
#endif
#if ULONG_MAX == 4294967295UL
#define POINTER_FMT "%8lx"
#elif ULONG_MAX == 18446744073709551615UL
#define POINTER_FMT "%12lx"
#else
#define POINTER_FMT "%12lx"
#endif

/* pager support */
static FILE *get_pager(FILE *fallback_output)
{
    const char *pager;
    pid_t pgpid;
    int pgret;
    int pgpipe[2];
    bool pgcmdfail = false;
    FILE *retfd = NULL;
    char *const emptyvec[1] = { NULL };

    assert(fallback_output != NULL);

    if (sm_globals.options.backend)
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

static inline void close_pager(FILE *pager)
{
    if (pager != stdout && pager != stderr) {
        if (pclose(pager) == -1 && errno != EPIPE)
            show_warn("pclose() error: %s\n", strerror(errno));
        signal(SIGPIPE, SIG_DFL);
    }
}

bool handler__set(globals_t * vars, char **argv, unsigned argc)
{
    unsigned block, seconds = 1;
    char *delay = NULL;
    bool cont = false;
    struct setting {
        char *matchids;
        char *value;
        unsigned seconds;
    } *settings = NULL;

    assert(argc != 0);
    assert(argv != NULL);
    assert(vars != NULL);


    if (argc < 2) {
        show_error("expected an argument, type `help set` for details.\n");
        return false;
    }

    /* supporting `set` for bytearray will cause annoying syntax problems... */
    if ((vars->options.scan_data_type == BYTEARRAY)
       ||(vars->options.scan_data_type == STRING))
    {
        show_error("`set` is not supported for bytearray or string, use `write` instead.\n");
        return false;
    }

    /* check if there are any matches */
    if (vars->num_matches == 0) {
        show_error("no matches are known.\n");
        return false;
    }

    /* --- parse arguments into settings structs --- */

    settings = calloca(argc - 1, sizeof(struct setting));

    /* parse every block into a settings struct */
    for (block = 0; block < argc - 1; block++) {

        /* first separate the block into matches and value, which are separated by '=' */
        if ((settings[block].value = strchr(argv[block + 1], '=')) == NULL) {

            /* no '=' found, whole string must be the value */
            settings[block].value = argv[block + 1];
        } else {
            /* there is a '=', value+1 points to value string. */

            /* use strndupa() to copy the matchids into a new buffer */
            settings[block].matchids =
                strndupa(argv[block + 1],
                         (size_t) (settings[block].value++ - argv[block + 1]));
        }

        /* value points to the value string, possibly with a delay suffix */

        /* matchids points to the match-ids (possibly multiple) or NULL */

        /* now check for a delay suffix (meaning continuous mode), eg 0xff/10 */
        if ((delay = strchr(settings[block].value, '/')) != NULL) {
            char *end = NULL;

            /* parse delay count */
            settings[block].seconds = strtoul(delay + 1, &end, 10);

            if (*(delay + 1) == '\0') {
                /* empty delay count, eg: 12=32/ */
                show_error("you specified an empty delay count, `%s`, see `help set`.\n", settings[block].value);
                return false;
            } else if (*end != '\0') {
                /* parse failed before end, probably trailing garbage, eg 34=9/16foo */
                show_error("trailing garbage after delay count, `%s`.\n", settings[block].value);
                return false;
            } else if (settings[block].seconds == 0) {
                /* 10=24/0 disables continuous mode */
                show_info("you specified a zero delay, disabling continuous mode.\n");
            } else {
                /* valid delay count seen and understood */
                show_info("setting %s every %u seconds until interrupted...\n", settings[block].matchids ? settings[block].  matchids : "all", settings[block].seconds);

                /* continuous mode on */
                cont = true;
            }

            /* remove any delay suffix from the value */
            settings[block].value =
                strndupa(settings[block].value,
                         (size_t) (delay - settings[block].value));
        }                       /* if (strchr('/')) */
    }                           /* for(block...) */

    /* --- setup a longjmp to handle interrupt --- */
    if (INTERRUPTABLE()) {
        
        /* control returns here when interrupted */
// settings is allocated with alloca, do not free it
//        free(settings);
        sm_detach(vars->target);
        ENDINTERRUPTABLE();
        return true;
    }

    /* --- execute the parsed setting structs --- */

    while (true) {
        uservalue_t userval;

        /* for every settings struct */
        for (block = 0; block < argc - 1; block++) {

            /* check if this block has anything to do in this iteration */
            if (seconds != 1) {
                /* not the first iteration (all blocks get executed first iteration) */

                /* if settings.seconds is zero, then this block is only executed once */
                /* if seconds % settings.seconds is zero, then this block must be executed */
                if (settings[block].seconds == 0
                    || (seconds % settings[block].seconds) != 0)
                    continue;
            }

            /* convert value */
            if (!parse_uservalue_number(settings[block].value, &userval)) {
                show_error("bad number `%s` provided\n", settings[block].value);
                goto fail;
            }

            /* check if specific match(s) were specified */
            if (settings[block].matchids != NULL) {
                struct set match_set;
                match_location loc;

                if (parse_uintset(settings[block].matchids, &match_set, vars->num_matches)
                        == false) {
                    show_error("failed to parse the set, try `help set`.\n");
                    goto fail;
                }

                foreach_set_fw(i, &match_set) {
                    loc = nth_match(vars->matches, match_set.buf[i]);
                    if (loc.swath) {
                        value_t v;
                        void *address = remote_address_of_nth_element(loc.swath, loc.index);

                        v = data_to_val(loc.swath, loc.index);
                        /* copy userval onto v */
                        /* XXX: valcmp? make sure the sizes match */
                        uservalue2value(&v, &userval);
                        
                        show_info("setting *%p to %#"PRIx64"...\n", address, v.int64_value);

                        /* set the value specified */
                        fix_endianness(&v, vars->options.reverse_endianness);
                        if (sm_setaddr(vars->target, address, &v) == false) {
                            show_error("failed to set a value.\n");
                            set_cleanup(&match_set);
                            goto fail;
                        }
                    } else {
                        show_error("BUG: set: id <%zu> match failure\n", match_set.buf[i]);
                        set_cleanup(&match_set);
                        goto fail;
                    }
                }
                set_cleanup(&match_set);
            } else {
                matches_and_old_values_swath *reading_swath_index = vars->matches->swaths;
                size_t reading_iterator = 0;

                /* user wants to set all matches */
                while (reading_swath_index->first_byte_in_child) {

                    /* only actual matches are considered */
                    if (reading_swath_index->data[reading_iterator].match_info != flags_empty)
                    {
                        void *address = remote_address_of_nth_element(reading_swath_index, reading_iterator);
                        value_t v;

                        v = data_to_val(reading_swath_index, reading_iterator);
                        /* XXX: as above : make sure the sizes match */
                        uservalue2value(&v, &userval);

                        show_info("setting *%p to %#"PRIx64"...\n", address, v.int64_value);

                        fix_endianness(&v, vars->options.reverse_endianness);
                        if (sm_setaddr(vars->target, address, &v) == false) {
                            show_error("failed to set a value.\n");
                            goto fail;
                        }
                    }
                     
                     /* go on to the next one... */
                    ++reading_iterator;
                    if (reading_iterator >= reading_swath_index->number_of_bytes)
                    {
                        reading_swath_index = local_address_beyond_last_element(reading_swath_index);
                        reading_iterator = 0;
                    }
                }
            }                   /* if (matchid != NULL) else ... */
        }                       /* for(block) */

        if (cont) {
            sleep(1);
        } else {
            break;
        }

        seconds++;
    }                           /* while(true) */

    ENDINTERRUPTABLE();
    return true;

fail:
    ENDINTERRUPTABLE();
    return false;
    
}

/* Accepts a numerical argument to print up to N matches, defaults to 10k
 * FORMAT (don't change, front-end depends on this):
 * [#no] addr, value, [possible types (separated by space)]
 */
bool handler__list(globals_t *vars, char **argv, unsigned argc)
{
    unsigned long num = 0;
    size_t buf_len = 128; /* will be realloc'd later if necessary */
    element_t *np = NULL;
    char *v = NULL;
    const char *bytearray_suffix = ", [bytearray]";
    const char *string_suffix = ", [string]";
    struct winsize w;
    FILE *pager;

    unsigned long max_to_print = 10000;
    if (argc > 1) {
        max_to_print = strtoul(argv[1], NULL, 0x00);

        if (max_to_print == 0) {
            show_error("`%s` is not a valid positive integer.\n", argv[1]);
            return false;
        }
    }

    if (vars->num_matches == 0)
        return false;

    if ((v = malloc(buf_len)) == NULL)
    {
        show_error("memory allocation failed.\n");
        return false;
    }

    if (vars->regions)
        np = vars->regions->head;

    matches_and_old_values_swath *reading_swath_index = vars->matches->swaths;
    size_t reading_iterator = 0;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == -1) {
        if (!vars->options.backend)
            show_warn("handler__list(): couldn't get terminal size.\n");
        pager = get_pager(stdout);
    } else {
        /* check if the output fits in the terminal window */
        pager = (w.ws_row >= MIN(max_to_print, vars->num_matches)) ? stdout : get_pager(stdout);
    }

    /* list all known matches */
    while (reading_swath_index->first_byte_in_child) {
        if (num == max_to_print) {
            if (num < vars->num_matches && !vars->options.backend)
                fprintf(pager, "[...]\n");
            break;
        }

        match_flags flags = reading_swath_index->data[reading_iterator].match_info;

        /* only actual matches are considered */
        if (flags != flags_empty)
        {
            switch(vars->options.scan_data_type)
            {
            case BYTEARRAY:
                buf_len = flags * 3 + 32;
                v = realloc(v, buf_len); /* for each byte and the suffix, this should be enough */

                if (v == NULL)
                {
                    show_error("memory allocation failed.\n");
                    goto fail;
                }
                data_to_bytearray_text(v, buf_len, reading_swath_index, reading_iterator, flags);
                assert(strlen(v) + strlen(bytearray_suffix) + 1 <= buf_len); /* or maybe realloc is better? */
                strcat(v, bytearray_suffix);
                break;
            case STRING:
                buf_len = flags + strlen(string_suffix) + 32; /* for the string and suffix, this should be enough */
                v = realloc(v, buf_len);
                if (v == NULL)
                {
                    show_error("memory allocation failed.\n");
                    goto fail;
                }
                data_to_printable_string(v, buf_len, reading_swath_index, reading_iterator, flags);
                assert(strlen(v) + strlen(string_suffix) + 1 <= buf_len); /* or maybe realloc is better? */
                strcat(v, string_suffix);
                break;
            default: /* numbers */
                ; /* cheat gcc */
                value_t val = data_to_val(reading_swath_index, reading_iterator);

                valtostr(&val, v, buf_len);
                break;
            }

            void *address = remote_address_of_nth_element(reading_swath_index, reading_iterator);
            unsigned long address_ul = (unsigned long)address;
            unsigned int region_id = 99;
            unsigned long match_off = 0;
            const char *region_type = "??";
            /* get region info belonging to the match -
             * note: we assume the regions list and matches are sorted
             */
            while (np) {
                region_t *region = np->data;
                unsigned long region_start = (unsigned long)region->start;
                if (address_ul < region_start + region->size &&
                  address_ul >= region_start) {
                    region_id = region->id;
                    match_off = address_ul - region->load_addr;
                    region_type = region_type_names[region->type];
                    break;
                }
                np = np->next;
            }
            fprintf(pager, "[%2lu] "POINTER_FMT", %2u + "POINTER_FMT", %5s, %s\n",
                   num++, address_ul, region_id, match_off, region_type, v);
        }

        /* go on to the next one... */
        ++reading_iterator;
        if (reading_iterator >= reading_swath_index->number_of_bytes)
        {
            reading_swath_index = local_address_beyond_last_element(reading_swath_index);
            reading_iterator = 0;
        }
    }

    free(v);
    close_pager(pager);
    return true;
fail:
    free(v);
    close_pager(pager);
    return false;
}

bool handler__delete(globals_t * vars, char **argv, unsigned argc)
{
    struct set del_set;

    if (argc != 2) {
        show_error("was expecting one argument, see `help delete`.\n");
        return false;
    }

    if (vars->num_matches == 0) {
        show_error("nothing to delete.\n");
        return false;
    }

    if (!parse_uintset(argv[1], &del_set, (size_t)vars->num_matches)) {
        show_error("failed to parse the set, try `help delete`.\n");
        return false;
    }

    size_t match_counter = 0;
    size_t set_idx = 0;

    matches_and_old_values_swath *reading_swath_index = vars->matches->swaths;

    size_t reading_iterator = 0;

    while (reading_swath_index->first_byte_in_child) {
        /* only actual matches are considered */
        if (reading_swath_index->data[reading_iterator].match_info != flags_empty) {

            if (match_counter++ == del_set.buf[set_idx]) {
                /* It is not reasonable to check if the matches array can be
                 * downsized after the deletion.
                 * So just zero its flags, to mark it as not a REAL match */
                reading_swath_index->data[reading_iterator].match_info = flags_empty;
                vars->num_matches--;

                if (set_idx++ == del_set.size - 1) {
                    set_cleanup(&del_set);
                    return true;
                }
            }
        }

        /* go on to the next one... */
        ++reading_iterator;
        if (reading_iterator >= reading_swath_index->number_of_bytes) {
            reading_swath_index =
                local_address_beyond_last_element(reading_swath_index);

            reading_iterator = 0;
        }
    }

    show_error("BUG: delete: id <%zu> match failure\n", del_set.buf[set_idx]);
    set_cleanup(&del_set);
    return false;
}

bool handler__reset(globals_t * vars, char **argv, unsigned argc)
{
    USEPARAMS();

    /* reset scan progress */
    vars->scan_progress = 0;

    if (vars->matches) { free(vars->matches); vars->matches = NULL; vars->num_matches = 0; }

    /* refresh list of regions */
    l_destroy(vars->regions);

    /* create a new linked list of regions */
    if ((vars->regions = l_init()) == NULL) {
        show_error("sorry, there was a problem allocating memory.\n");
        return false;
    }

    /* read in maps if a pid is known */
    if (vars->target && sm_readmaps(vars->target, vars->regions, vars->options.region_scan_level) != true) {
        show_error("sorry, there was a problem getting a list of regions to search.\n");
        show_warn("the pid may be invalid, or you don't have permission.\n");
        vars->target = 0;
        return false;
    }

    return true;
}

bool handler__pid(globals_t * vars, char **argv, unsigned argc)
{
    char *resetargv[] = { "reset", NULL };
    char *end = NULL;

    if (argc == 2) {
        vars->target = (pid_t) strtoul(argv[1], &end, 0x00);

        if (vars->target == 0) {
            show_error("`%s` does not look like a valid pid.\n", argv[1]);
            return false;
        }
    } else if (vars->target) {
        /* print the pid of the target program */
        show_info("target pid is %u.\n", vars->target);
        return true;
    } else {
        show_info("no target is currently set.\n");
        return false;
    }

    return handler__reset(vars, resetargv, 1);
}

bool handler__snapshot(globals_t *vars, char **argv, unsigned argc)
{
    USEPARAMS();
    

    /* check that a pid has been specified */
    if (vars->target == 0) {
        show_error("no target set, type `help pid`.\n");
        return false;
    }

    /* remove any existing matches */
    if (vars->matches) { free(vars->matches); vars->matches = NULL; vars->num_matches = 0; }

    if (sm_searchregions(vars, MATCHANY, NULL) != true) {
        show_error("failed to save target address space.\n");
        return false;
    }

    return true;
}

/* dregion <region-id set> */
bool handler__dregion(globals_t *vars, char **argv, unsigned argc)
{
    struct set reg_set;

    /* need an argument */
    if (argc < 2) {
        show_error("expected an argument, see `help dregion`.\n");
        return false;
    }

    /* check that there is a process known */
    if (vars->target == 0) {
        show_error("no target specified, see `help pid`\n");
        return false;
    }

    /* check if there are any regions at all */
    if (vars->regions->size == 0) {
        show_error("no regions are known.\n");
        return false;
    }

    size_t last_region_id = ((region_t*)(vars->regions->tail->data))->id;

    if (!parse_uintset(argv[1], &reg_set, last_region_id + 1)) {
        show_error("failed to parse the set, try `help dregion`.\n");
        return false;
    }

    /* loop for every reg_id in the set */
    for (size_t set_idx = 0; set_idx < reg_set.size; set_idx++) {
        size_t reg_id = reg_set.buf[set_idx];
        
        /* initialize list pointers */
        element_t *np = vars->regions->head;
        element_t *pp = NULL;
        
        /* find the correct region node */
        while (np) {
            region_t *r = np->data;
            
            /* compare the node id to the id the user specified */
            if (r->id == reg_id)
                break;
            
            pp = np; /* keep track of prev for l_remove() */
            np = np->next;
        }

        /* check if a match was found */
        if (np == NULL) {
            show_warn("no region matching %u, or already removed.\n", reg_id);
            continue;
        }
        
        /* check for any affected matches before removing it */
        if(vars->num_matches > 0)
        {
            region_t *reg_to_delete = np->data;

            void *start_address = reg_to_delete->start;
            void *end_address = reg_to_delete->start + reg_to_delete->size;
            vars->matches = delete_in_address_range(vars->matches, &vars->num_matches,
                                                    start_address, end_address);
            if (vars->matches == NULL)
            {
                show_error("memory allocation error while deleting matches\n");
            }
        }

        l_remove(vars->regions, pp, NULL);
    }

    return true;
}

bool handler__lregions(globals_t * vars, char **argv, unsigned argc)
{
    element_t *np = vars->regions->head;

    USEPARAMS();

    if (vars->target == 0) {
        show_error("no target has been specified, see `help pid`.\n");
        return false;
    }

    if (vars->regions->size == 0) {
        show_info("no regions are known.\n");
    }
    
    /* print a list of regions that have been searched */
    while (np) {
        region_t *region = np->data;

        fprintf(stderr, "[%2u] "POINTER_FMT", %7lu bytes, %5s, "POINTER_FMT", %c%c%c, %s\n", 
                region->id,
                (unsigned long)region->start, region->size,
                region_type_names[region->type], region->load_addr,
                region->flags.read ? 'r' : '-',
                region->flags.write ? 'w' : '-',
                region->flags.exec ? 'x' : '-',
                region->filename[0] ? region->filename : "unassociated");
        np = np->next;
    }

    return true;
}

/* handles every scan that starts with an operator */
bool handler__operators(globals_t * vars, char **argv, unsigned argc)
{
    uservalue_t val;
    scan_match_type_t m;

    if (argc == 1)
    {
        zero_uservalue(&val);
    }
    else if (argc > 2)
    {
        show_error("too many values specified, see `help %s`", argv[0]);
        return false;
    }
    else
    {
        if (!parse_uservalue_number(argv[1], &val)) {
            show_error("bad value specified, see `help %s`", argv[0]);
            return false;
        }
    }


    if (strcmp(argv[0], "=") == 0)
    {
        m = (argc == 1) ? MATCHNOTCHANGED : MATCHEQUALTO;
    }
    else if (strcmp(argv[0], "!=") == 0)
    {
        m = (argc == 1) ? MATCHCHANGED : MATCHNOTEQUALTO;
    }
    else if (strcmp(argv[0], "<") == 0)
    {
        m = (argc == 1) ? MATCHDECREASED : MATCHLESSTHAN;
    }
    else if (strcmp(argv[0], ">") == 0)
    {
        m = (argc == 1) ? MATCHINCREASED : MATCHGREATERTHAN;
    }
    else if (strcmp(argv[0], "+") == 0)
    {
        m = (argc == 1) ? MATCHINCREASED : MATCHINCREASEDBY;
    }
    else if (strcmp(argv[0], "-") == 0)
    {
        m = (argc == 1) ? MATCHDECREASED : MATCHDECREASEDBY;
    }
    else
    {
        show_error("unrecognized operator seen at handler_operators: \"%s\".\n", argv[0]);
        return false;
    }

    if (vars->matches) {
        if (vars->num_matches == 0) {
            show_error("there are currently no matches.\n");
            return false;
        }
        if (sm_checkmatches(vars, m, &val) == false) {
            show_error("failed to search target address space.\n");
            return false;
        }
    } else {
        /* Cannot be used on first scan:
         *   =, !=, <, >, +, + N, -, - N
         * Can be used on first scan:
         *   = N, != N, < N, > N
         */
        if (m == MATCHNOTCHANGED  ||
            m == MATCHCHANGED     ||
            m == MATCHDECREASED   ||
            m == MATCHINCREASED   ||
            m == MATCHDECREASEDBY ||
            m == MATCHINCREASEDBY )
        {
            show_error("cannot use that search without matches\n");
            return false;
        }
        else
        {
            if (sm_searchregions(vars, m, &val) != true) {
                show_error("failed to search target address space.\n");
                return false;
            }
        }
    }

    if (vars->num_matches == 1) {
        show_info("match identified, use \"set\" to modify value.\n");
        show_info("enter \"help\" for other commands.\n");
    }

    return true;
}

bool handler__version(globals_t *vars, char **argv, unsigned argc)
{
    USEPARAMS();

    vars->printversion(stderr);
    return true;
}

bool handler__string(globals_t * vars, char **argv, unsigned argc)
{
    USEPARAMS();

    /* test scan_data_type */
    if (vars->options.scan_data_type != STRING)
    {
        show_error("scan_data_type is not string, see `help option`.\n");
        return false;
    }

    /* test the length */
    size_t cmdline_length = strlen(vars->current_cmdline);
    if (cmdline_length < 3) /* cmdline too short */
    {
        show_error("please specify a string\n");
        return false;
    }
    size_t string_length = cmdline_length-2;
    if (string_length > (uint16_t)(-1)) /* string too long */
    {
        show_error("String length is limited to %u\n", (uint16_t)(-1));
        return false;
    }

    /* Allocate a copy of the target string. While it would be possible to reuse
     * the incoming string, truncating the first 2 chars means it is aligned
     * at most at a 2 bytes boundary, which will generate unaligned accesses
     * when the string will be read as a sequence of int64 during a scan.
     * `malloc()` instead ensures enough alignment for any type.
     */
    char *string_value = malloc((string_length+1)*sizeof(char));
    if (string_value == NULL)
    {
        show_error("memory allocation for string failed.\n");
        return false;
    }
    strcpy(string_value, vars->current_cmdline+2);

    uservalue_t val;
    val.string_value = string_value;
    val.flags = string_length;
 
    /* need a pid for the rest of this to work */
    if (vars->target == 0) {
        goto fail;
    }

    /* user has specified an exact value of the variable to find */
    if (vars->matches) {
        if (vars->num_matches == 0) {
            show_error("there are currently no matches.\n");
            return false;
        }
        /* already know some matches */
        if (sm_checkmatches(vars, MATCHEQUALTO, &val) != true) {
            show_error("failed to search target address space.\n");
            goto fail;
        }
    } else {
        /* initial search */
        if (sm_searchregions(vars, MATCHEQUALTO, &val) != true) {
            show_error("failed to search target address space.\n");
            goto fail;
        }
    }

    /* check if we now know the only possible candidate */
    if (vars->num_matches == 1) {
        show_info("match identified, use \"set\" to modify value.\n");
        show_info("enter \"help\" for other commands.\n");
    }

    free(string_value);
    return true;

fail:
    free(string_value);
    return false;
}

static inline bool parse_uservalue_default(const char *str, uservalue_t *val)
{
    bool ret = true;

    if (!parse_uservalue_number(str, val)) {
        show_error("unable to parse number `%s`\n", str);
        ret = false;
    }
    return ret;
}

bool handler__default(globals_t * vars, char **argv, unsigned argc)
{
    uservalue_t vals[2];
    uservalue_t *val = &vals[0];
    scan_match_type_t m = MATCHEQUALTO;
    char *ustr = argv[0];
    char *pos;
    bool ret = false;

    zero_uservalue(val);

    switch(vars->options.scan_data_type)
    {
    case ANYNUMBER:
    case ANYINTEGER:
    case ANYFLOAT:
    case INTEGER8:
    case INTEGER16:
    case INTEGER32:
    case INTEGER64:
    case FLOAT32:
    case FLOAT64:
        /* attempt to parse command as a number */
        if (argc != 1)
        {
            show_error("unknown command\n");
            goto retl;
        }
        /* detect a range */
        pos = strstr(ustr, "..");
        if (pos) {
            *pos = '\0';
            if (!parse_uservalue_default(ustr, &vals[0]))
                goto retl;
            ustr = pos + 2;
            if (!parse_uservalue_default(ustr, &vals[1]))
                goto retl;

            /* Check that the range is nonempty */
            if (vals[0].float64_value > vals[1].float64_value) {
                show_error("Empty range\n");
                goto retl;
            }

            /* Store the bitwise AND of both flags in the first value,
             * so that range scanroutines need only one flag testing. */
            vals[0].flags &= vals[1].flags;
            m = MATCHRANGE;
        }
        else {
            if (!parse_uservalue_default(ustr, val))
                goto retl;
        }
        break;
    case BYTEARRAY:
        /* attempt to parse command as a bytearray */
        if (!parse_uservalue_bytearray(argv, argc, val)) {
            show_error("unable to parse command `%s`\n", ustr);
            goto retl;
        }
        break;
    case STRING:
        show_error("unable to parse command `%s`\nIf you want to scan"
                   " for a string, use command `\"`.\n", ustr);
        goto retl;
        break;
    default:
        assert(false);
        break;
    }

    /* need a pid for the rest of this to work */
    if (vars->target == 0) {
        goto retl;
    }

    /* user has specified an exact value of the variable to find */
    if (vars->matches) {
        if (vars->num_matches == 0) {
            show_error("there are currently no matches.\n");
            goto retl;
        }
        /* already know some matches */
        if (sm_checkmatches(vars, m, val) != true) {
            show_error("failed to search target address space.\n");
            goto retl;
        }
    } else {
        /* initial search */
        if (sm_searchregions(vars, m, val) != true) {
            show_error("failed to search target address space.\n");
            goto retl;
        }
    }

    /* check if we now know the only possible candidate */
    if (vars->num_matches == 1) {
        show_info("match identified, use \"set\" to modify value.\n");
        show_info("enter \"help\" for other commands.\n");
    }

    ret = true;

retl:
    free_uservalue(val);

    return ret;
}

bool handler__update(globals_t *vars, char **argv, unsigned argc)
{

    USEPARAMS();
    if (vars->num_matches) {
        if (sm_checkmatches(vars, MATCHUPDATE, NULL) == false) {
            show_error("failed to scan target address space.\n");
            return false;
        }
    } else {
        show_error("cannot use that command without matches\n");
        return false;
    }

    return true;
}

bool handler__exit(globals_t *vars, char **argv, unsigned argc)
{
    USEPARAMS();

    vars->exit = 1;
    return true;
}

#define DOC_COLUMN  11           /* which column descriptions start on with the help command */

bool handler__help(globals_t *vars, char **argv, unsigned argc)
{
    bool ret = false;
    list_t *commands = vars->commands;
    element_t *np = NULL;
    command_t *def = NULL;
    FILE *pager;
    assert(commands != NULL);
    assert(argc >= 1);

    np = commands->head;

    pager = get_pager(stderr);

    /* print version information for generic help */
    if (argv[1] == NULL) {
        vars->printversion(pager);
        fprintf(pager, "\n");
    }

    /* traverse the commands list, printing out the relevant documentation */
    while (np) {
        command_t *command = np->data;

        /* remember the default command */
        if (command->command == NULL)
            def = command;

        /* just `help` with no argument */
        if (argv[1] == NULL) {
            /* NULL shortdoc means don't print in the help listing */
            if (command->shortdoc == NULL) {
                np = np->next;
                continue;
            }

            /* print out command name */
            fprintf(pager, "%-*s%s\n", DOC_COLUMN, command->command ? command->command : "default", command->shortdoc);

            /* detailed information requested about specific command */
        } else if (command->command
                   && strcasecmp(argv[1], command->command) == 0) {
            fprintf(pager, "%s\n", command->longdoc ? command-> longdoc : "missing documentation");
            ret = true;
            goto retl;
        }

        np = np->next;
    }

    if (argc > 1) {
        show_error("unknown command `%s`\n", argv[1]);
        ret = false;
    } else if (def) {
        fprintf(pager, "\n%s\n", def->longdoc ? def->longdoc : "");
        ret = true;
    }
    
    ret = true;

retl:
    close_pager(pager);

    return ret;
}

bool handler__eof(globals_t * vars, char **argv, unsigned argc)
{
    show_user("exit\n");
    return handler__exit(vars, argv, argc);
}

/* XXX: handle !ls style escapes */
bool handler__shell(globals_t * vars, char **argv, unsigned argc)
{
    size_t len = argc;
    unsigned i;
    char *command;

    USEPARAMS();

    if (argc < 2) {
        show_error("shell command requires an argument, see `help shell`.\n");
        return false;
    }

    /* convert arg vector into single string, first calculate length */
    for (i = 1; i < argc; i++)
        len += strlen(argv[i]);

    /* allocate space */
    command = calloca(len, 1);

    /* concatenate strings */
    for (i = 1; i < argc; i++) {
        strcat(command, argv[i]);
        strcat(command, " ");
    }

    /* finally execute command */
    if (system(command) == -1) {
// command is allocated with alloca, do not free it
//        free(command);
        show_error("system() failed, command was not executed.\n");
        return false;
    }

// command is allocated with alloca, do not free it
//    free(command);
    return true;
}

bool handler__watch(globals_t * vars, char **argv, unsigned argc)
{
    size_t id;
    char *end = NULL, buf[128], timestamp[64];
    time_t t;
    match_location loc;
    value_t val;
    void *address;
    scan_data_type_t data_type = vars->options.scan_data_type;
    scan_routine_t valuecmp_routine;

    if (argc != 2) {
        show_error("was expecting one argument, see `help watch`.\n");
        return false;
    }
    if ((data_type == BYTEARRAY) || (data_type == STRING)) {
        show_error("`watch` is not supported for bytearray or string.\n");
        return false;
    }

    /* parse argument */
    id = strtoul(argv[1], &end, 0x00);

    /* check that strtoul() worked */
    if (argv[1][0] == '\0' || *end != '\0') {
        show_error("sorry, couldn't parse `%s`, try `help watch`\n",
                argv[1]);
        return false;
    }
    
    loc = nth_match(vars->matches, id);

    /* check that this is a valid match-id */
    if (!loc.swath) {
        show_error("you specified a non-existent match `%u`.\n", id);
        show_info("use \"list\" to list matches, or \"help\" for other commands.\n");
        return false;
    }
    
    address = remote_address_of_nth_element(loc.swath, loc.index);
    
    val = data_to_val(loc.swath, loc.index);

    if (INTERRUPTABLE()) {
        (void) sm_detach(vars->target);
        ENDINTERRUPTABLE();
        return true;
    }

    /* every entry is timestamped */
    t = time(NULL);
    strftime(timestamp, sizeof(timestamp), "[%T]", localtime(&t));

    show_info("%s monitoring %10p for changes until interrupted...\n", timestamp, address);

    valuecmp_routine = sm_get_scanroutine(ANYNUMBER, MATCHCHANGED, flags_empty, vars->options.reverse_endianness);
    while (true) {
        const mem64_t *memory_ptr;
        size_t memlength;

        if (sm_attach(vars->target) == false)
            return false;

        if (sm_peekdata(address, sizeof(uint64_t), &memory_ptr, &memlength) == false)
            return false;

        /* check if the new value is different */
        match_flags tmpflags = flags_empty;
        if ((*valuecmp_routine)(memory_ptr, memlength, &val, NULL, &tmpflags)) {

            memcpy(val.bytes, memory_ptr, memlength);

            valtostr(&val, buf, sizeof(buf));

            /* fetch new timestamp */
            t = time(NULL);
            strftime(timestamp, sizeof(timestamp), "[%T]", localtime(&t));

            show_info("%s %10p -> %s\n", timestamp, address, buf);
        }

        /* detach after valuecmp_routine, since it may read more data (e.g. bytearray) */
        sm_detach(vars->target);

        (void) sleep(1);
    }
}

#include "licence.h"

bool handler__show(globals_t * vars, char **argv, unsigned argc)
{
    USEPARAMS();
    
    if (argv[1] == NULL) {
        show_error("expecting an argument.\n");
        return false;
    }
    
    if (strcmp(argv[1], "copying") == 0)
        show_user(SM_COPYING);
    else if (strcmp(argv[1], "warranty") == 0)
        show_user(SM_WARRANTY);
    else if (strcmp(argv[1], "version") == 0)
        vars->printversion(stderr);
    else {
        show_error("unrecognized show command `%s`\n", argv[1]);
        return false;
    }
    
    return true;
}

bool handler__dump(globals_t * vars, char **argv, unsigned argc)
{
    void *addr;
    char *endptr;
    char *buf = NULL;
    int len;
    bool dump_to_file = false;
    FILE *dump_f = NULL;

    if (argc < 3 || argc > 4)
    {
        show_error("bad argument, see `help dump`.\n");
        return false;
    }
    
    /* check address */
    errno = 0;
    addr = (void *)(strtoll(argv[1], &endptr, 16));
    if ((errno != 0) || (*endptr != '\0'))
    {
        show_error("bad address, see `help dump`.\n");
        return false;
    }

    /* check length */
    errno = 0;
    len = strtoll(argv[2], &endptr, 0);
    if ((errno != 0) || (*endptr != '\0'))
    {
        show_error("bad length, see `help dump`.\n");
        return false;
    }

    /* check filename */
    if (argc == 4)
    {
        if((dump_f = fopen(argv[3], "wb")) == NULL)
        {
            show_error("failed to open file\n");
            return false;
        }
        dump_to_file = true;
    }

    buf = malloc(len + sizeof(long));
    if (buf == NULL)
    {
        if (dump_f)
            fclose(dump_f);
        show_error("memory allocation failed.\n");
        return false;
    }

    if (!sm_read_array(vars->target, addr, buf, len))
    {
        if (dump_f)
            fclose(dump_f);
        show_error("read memory failed.\n");
        free(buf);
        return false;
    }

    if(dump_to_file)
    {
        size_t s = fwrite(buf,1,len,dump_f);
        fclose(dump_f);
        if (s != len)
        {
            show_error("write to file failed.\n");
            free(buf);
            return false;
        }  
    }
    else
    {
        if (vars->options.backend == 1)
        {
            /* dump raw memory to stdout, the front-end will handle it */
            fwrite(buf, sizeof(char), len, stdout);
        }
        else
        {
            /* print it out nicely */
            int i,j;
            int buf_idx = 0;
            for (i = 0; i + 16 < len; i += 16)
            {
                printf("%p: ", addr+i);
                for (j = 0; j < 16; ++j)
                {
                    printf("%02X ", (unsigned char)(buf[buf_idx++]));
                }
                if(vars->options.dump_with_ascii == 1)
                {
                    for (j = 0; j < 16; ++j)
                    {
                        char c = buf[i+j];
                        printf("%c", isprint(c) ? c : '.');
                    }
                }
                printf("\n");
            }
            if (i < len)
            {
                printf("%p: ", addr+i);
                for (j = i; j < len; ++j)
                {
                    printf("%02X ", (unsigned char)(buf[buf_idx++]));
                }
                if(vars->options.dump_with_ascii == 1)
                {
                    while(j%16 !=0) // skip "empty" numbers
                    {
                        printf("   ");
                        ++j;
                    }
                    for (j = 0; i+j < len; ++j)
                    {
                        char c = buf[i+j];
                        printf("%c", isprint(c) ? c : '.');
                    }
                }
                printf("\n");
            }
        }
    }

    free(buf);
    return true;
}

/* Returns (scan_data_type_t)(-1) on parse failure */
static inline scan_data_type_t parse_scan_data_type(const char *str)
{
    /* Anytypes */
    if ((strcasecmp(str, "number") == 0)  || (strcasecmp(str, "anynumber") == 0))
        return ANYNUMBER;
    if ((strcasecmp(str, "int") == 0)     || (strcasecmp(str, "anyint") == 0) ||
        (strcasecmp(str, "integer") == 0) || (strcasecmp(str, "anyinteger") == 0))
        return ANYINTEGER;
    if ((strcasecmp(str, "float") == 0)   || (strcasecmp(str, "anyfloat") == 0))
        return ANYFLOAT;

    /* Ints */
    if ((strcasecmp(str, "i8") == 0)  || (strcasecmp(str, "int8") == 0)  ||
        (strcasecmp(str, "integer8") == 0))
        return INTEGER8;
    if ((strcasecmp(str, "i16") == 0) || (strcasecmp(str, "int16") == 0) ||
        (strcasecmp(str, "integer16") == 0))
        return INTEGER16;
    if ((strcasecmp(str, "i32") == 0) || (strcasecmp(str, "int32") == 0) ||
        (strcasecmp(str, "integer32") == 0))
        return INTEGER32;
    if ((strcasecmp(str, "i64") == 0) || (strcasecmp(str, "int64") == 0) ||
        (strcasecmp(str, "integer64") == 0))
        return INTEGER64;

    /* Floats */
    if ((strcasecmp(str, "f32") == 0) || (strcasecmp(str, "float32") == 0))
        return FLOAT32;
    if ((strcasecmp(str, "f64") == 0) || (strcasecmp(str, "float64") == 0) ||
        (strcasecmp(str, "double") == 0))
        return FLOAT64;

    /* VLT */
    if (strcasecmp(str, "bytearray") == 0) return BYTEARRAY;
    if (strcasecmp(str, "string") == 0)    return STRING;

    /* Not a valid type */
    return (scan_data_type_t)(-1);
}

/* write value_type address value */
bool handler__write(globals_t * vars, char **argv, unsigned argc)
{
    int data_width = 0;
    const char *fmt = NULL;
    void *addr;
    char *buf = NULL;
    char *endptr;
    int datatype; /* 0 for numbers, 1 for bytearray, 2 for string */
    bool ret;
    const char *string_parameter = NULL; /* used by string type */

    if (argc < 4)
    {
        show_error("bad arguments, see `help write`.\n");
        ret = false;
        goto retl;
    }

    scan_data_type_t st = parse_scan_data_type(argv[1]);

    /* try int first */
    if (st == INTEGER8)
    {
        data_width = 1;
        datatype = 0;
        fmt = "%"PRId8;
    }
    else if (st == INTEGER16)
    {
        data_width = 2;
        datatype = 0;
        fmt = "%"PRId16;
    }
    else if (st == INTEGER32)
    {
        data_width = 4;
        datatype = 0;
        fmt = "%"PRId32;
    }
    else if (st == INTEGER64)
    {
        data_width = 8;
        datatype = 0;
        fmt = "%"PRId64;
    }
    else if (st == FLOAT32)
    {
        data_width = 4;
        datatype = 0;
        fmt = "%f";
    }
    else if (st == FLOAT64)
    {
        data_width = 8;
        datatype = 0;
        fmt = "%lf";
    }
    else if (st == BYTEARRAY)
    {
        data_width = argc - 3;
        datatype = 1;
    }
    else if (st == STRING)
    {
        /* locate the string parameter, say locate the beginning of the 4th parameter (2 characters after the end of the 3rd paramter)*/
        int i;
        string_parameter = vars->current_cmdline;
        for(i = 0; i < 3; ++i)
        {
            while(isspace(*string_parameter))
                ++ string_parameter;
            while(!isspace(*string_parameter))
                ++ string_parameter;
        }
        ++ string_parameter;
        data_width = strlen(string_parameter);
        datatype = 2;
    }
    /* may support more types here */
    else
    {
        show_error("bad data_type, see `help write`.\n");
        ret = false;
        goto retl;
    }

    /* check argc again */
    if ((datatype == 0) && (argc != 4))
    {
        show_error("bad arguments, see `help write`.\n");
        ret = false;
        goto retl;
    }

    /* check address */
    errno = 0;
    addr = (void *)strtoll(argv[2], &endptr, 16);
    if ((errno != 0) || (*endptr != '\0'))
    {
        show_error("bad address, see `help write`.\n");
        ret = false;
        goto retl;
    }

    buf = malloc(data_width + 8); /* allocate a little bit more, just in case */
    if (buf == NULL)
    {
        show_error("memory allocation failed.\n");
        ret = false;
        goto retl;
    }

    /* load value into buffer */
    switch(datatype)
    {
    case 0: // numbers
        if(sscanf(argv[3], fmt, buf) < 1) /* should be OK even for max uint64 */
        {
            show_error("bad value, see `help write`.\n");
            ret = false;
            goto retl;
        }
        if (1 < data_width && vars->options.reverse_endianness) {
            swap_bytes_var(buf, data_width);
        }
        break;
    case 1: // bytearray
        ; /* cheat gcc */
        /* call parse_uservalue_bytearray */
        uservalue_t val_buf;
        if(!parse_uservalue_bytearray(argv+3, argc-3, &val_buf))
        {
            show_error("bad byte array specified.\n");
            free_uservalue(&val_buf);
            ret = false;
            goto retl;
        }
        int i;

        {
            // if wildcard is provided in the bytearray, we need the original data.
            bool wildcard_used = false;
            for(i = 0; i < data_width; ++i)
            {
                if(val_buf.wildcard_value[i] == WILDCARD)
                {
                    wildcard_used = true;
                    break;
                }
            }
            if (wildcard_used)
            {
                if(!sm_read_array(vars->target, addr, buf, data_width))
                {
                    show_error("read memory failed.\n");
                    free_uservalue(&val_buf);
                    ret = false;
                    goto retl;
                }
            }
        }

        for(i = 0; i < data_width; ++i)
        {
            if(val_buf.wildcard_value[i] == FIXED)
            {
                buf[i] = val_buf.bytearray_value[i];
            }
        }
        free_uservalue(&val_buf);
        break;
    case 2: //string
        strncpy(buf, string_parameter, data_width);
        break;
    default:
        assert(false);
    }

    /* write into memory */
    ret = sm_write_array(vars->target, addr, buf, data_width);

retl:
    if(buf)
        free(buf);
    return ret;
}

bool handler__option(globals_t * vars, char **argv, unsigned argc)
{
    /* this might need to change */
    if (argc != 3)
    {
        show_error("bad arguments, see `help option`.\n");
        return false;
    }

    if (strcasecmp(argv[1], "scan_data_type") == 0)
    {
        scan_data_type_t st = parse_scan_data_type(argv[2]);
        if (st != (scan_data_type_t)(-1)) {
            vars->options.scan_data_type = st;
        }
        else
        {
            show_error("bad value for scan_data_type, see `help option`.\n");
            return false;
        }
    }
    else if (strcasecmp(argv[1], "region_scan_level") == 0)
    {
        if (strcmp(argv[2], "1") == 0) {vars->options.region_scan_level = REGION_HEAP_STACK_EXECUTABLE; }
        else if (strcmp(argv[2], "2") == 0) {vars->options.region_scan_level = REGION_HEAP_STACK_EXECUTABLE_BSS; }
        else if (strcmp(argv[2], "3") == 0) {vars->options.region_scan_level = REGION_ALL; }
        else
        {
            show_error("bad value for region_scan_level, see `help option`.\n");
            return false;
        }
    }
    else if (strcasecmp(argv[1], "dump_with_ascii") == 0)
    {
        if (strcmp(argv[2], "0") == 0) {vars->options.dump_with_ascii = 0; }
        else if (strcmp(argv[2], "1") == 0) {vars->options.dump_with_ascii = 1; }
        else
        {
            show_error("bad value for dump_with_ascii, see `help option`.\n");
            return false;
        }
    }
    else if (strcasecmp(argv[1], "endianness") == 0)
    {
        // data is host endian: don't swap
        if (strcmp(argv[2], "0") == 0) {vars->options.reverse_endianness = 0; }
        // data is little endian: swap if host is big endian
        else if (strcmp(argv[2], "1") == 0) {vars->options.reverse_endianness = big_endian; }
        // data is big endian: swap if host is little endian
        else if (strcmp(argv[2], "2") == 0) {vars->options.reverse_endianness = !big_endian; }
        else
        {
            show_error("bad value for endianness, see `help option`.\n");
            return false;
        }
    }
    else
    {
        show_error("unknown option specified, see `help option`.\n");
        return false;
    }
    return true;
}
