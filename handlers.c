/*
    Specific command handling.

    Copyright (C) 2006,2007,2009 Tavis Ormandy <taviso@sdf.lonestar.org>
    Copyright (C) 2009           Eli Dupree <elidupree@charter.net>
    Copyright (C) 2009,2010      WANG Lu <coolwanglu@gmail.com>

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

#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif

#include "config.h"

#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <getopt.h>
#include <signal.h>
#include <assert.h>
#include <setjmp.h>
#include <alloca.h>
#include <strings.h>            /*lint -esym(526,strcasecmp) */
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <sys/time.h>
#include <limits.h>            /* to determine the word width */
#include <errno.h>
#include <inttypes.h>
#include <ctype.h>

#include "commands.h"
#include "endianness.h"
#include "handlers.h"
#include "interrupt.h"
#include "show_message.h"
#include "vector.h"

#define USEPARAMS() ((void) vars, (void) argv, (void) argc)     /* macro to hide gcc unused warnings */

/*lint -esym(818, vars, argv) dont want want to declare these as const */

/*
 * This file defines all the command handlers used, each one is registered using
 * registercommand(), and when a matching command is entered, the commandline is
 * tokenized and parsed into an argv/argc.
 * 
 * argv[0] will contain the command entered, so one handler can handle multiple
 * commands by checking whats in there, but you still need seperate documentation
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

bool handler__set(globals_t * vars, char **argv, unsigned argc)
{
    unsigned block, seconds = 1;
    char *delay = NULL;
    char *end;
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

        /* first seperate the block into matches and value, which are separated by '=' */
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
                /* 10=24/0 disables continous mode */
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
        detach(vars->target);
        ENDINTERRUPTABLE();
        return true;
    }

    /* --- execute the parsed setting structs --- */

    while (true) {
        uservalue_t userval;

        /* for every settings struct */
        for (block = 0; block < argc - 1; block++) {

            /* check if this block has anything to do this iteration */
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
                char *id, *lmatches = NULL;
                unsigned num = 0;

                /* create local copy of the matchids for strtok() to modify */
                lmatches = strdupa(settings[block].matchids);

                /* now seperate each match, spearated by commas */
                while ((id = strtok(lmatches, ",")) != NULL) {
                    match_location loc;

                    /* set to NULL for strtok() */
                    lmatches = NULL;

                    /* parse this id */
                    num = strtoul(id, &end, 0x00);

                    /* check that succeeded */
                    if (*id == '\0' || *end != '\0') {
                        show_error("could not parse match id `%s`\n", id);
                        goto fail;
                    }

                    /* check this is a valid match-id */
                    loc = nth_match(vars->matches, num);
                    if (loc.swath) {
                        value_t v;
                        value_t old;
                        void *address = remote_address_of_nth_element(loc.swath, loc.index /* ,MATCHES_AND_VALUES */);

                        /* copy val onto v */
                        /* XXX: valcmp? make sure the sizes match */
                        old = data_to_val(loc.swath, loc.index /* ,MATCHES_AND_VALUES */);
                        zero_value(&v);
                        v.flags = old.flags = loc.swath->data[loc.index].match_info;
                        uservalue2value(&v, &userval);
                        
                        show_info("setting *%p to %#"PRIx64"...\n", address, v.int64_value);

                        /* set the value specified */
                        fix_endianness(vars, &v);
                        if (setaddr(vars->target, address, &v) == false) {
                            show_error("failed to set a value.\n");
                            goto fail;
                        }

                    } else {
                        /* match-id > than number of matches */
                        show_error("found an invalid match-id `%s`\n", id);
                        goto fail;
                    }
                }
            } else {
                
                matches_and_old_values_swath *reading_swath_index = (matches_and_old_values_swath *)vars->matches->swaths;
                int reading_iterator = 0;

                /* user wants to set all matches */
                while (reading_swath_index->first_byte_in_child) {

                    /* Only actual matches are considered */
                    if (flags_to_max_width_in_bytes(reading_swath_index->data[reading_iterator].match_info) > 0)
                    {
                        void *address = remote_address_of_nth_element(reading_swath_index, reading_iterator /* ,MATCHES_AND_VALUES */);

                        /* XXX: as above : make sure the sizes match */
                                    
                        value_t old = data_to_val(reading_swath_index, reading_iterator /* ,MATCHES_AND_VALUES */);
                        value_t v;
                        zero_value(&v);
                        v.flags = old.flags = reading_swath_index->data[reading_iterator].match_info;
                        uservalue2value(&v, &userval);

                        show_info("setting *%p to %#"PRIx64"...\n", address, v.int64_value);

                        fix_endianness(vars, &v);
                        if (setaddr(vars->target, address, &v) == false) {
                            show_error("failed to set a value.\n");
                            goto fail;
                        }
                    }
                     
                     /* Go on to the next one... */
                    ++reading_iterator;
                    if (reading_iterator >= reading_swath_index->number_of_bytes)
                    {
                        reading_swath_index = local_address_beyond_last_element(reading_swath_index /* ,MATCHES_AND_VALUES */);
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

/* XXX: add yesno command to check if matches > 099999 */
/* FORMAT (don't change, front-end depends on this): 
 * [#no] addr, value, [possible types (separated by space)]
 */
bool handler__list(globals_t * vars, char **argv, unsigned argc)
{
    unsigned i = 0;
    int buf_len = 128; /* will be realloc later if necessary */
    element_t *np = NULL;
    char *v = malloc(buf_len);
    if (v == NULL)
    {
        show_error("memory allocation failed.\n");
        return false;
    }
    char *bytearray_suffix = ", [bytearray]";
    char *string_suffix = ", [string]";

    USEPARAMS();

    if(!(vars->matches))
        return true;

    if (vars->regions)
        np = vars->regions->head;

    matches_and_old_values_swath *reading_swath_index = (matches_and_old_values_swath *)vars->matches->swaths;
    int reading_iterator = 0;

    /* list all known matches */
    while (reading_swath_index->first_byte_in_child) {

        match_flags flags = reading_swath_index->data[reading_iterator].match_info;

        /* Only actual matches are considered */
        if (flags_to_max_width_in_bytes(flags) > 0)
        {
            switch(globals.options.scan_data_type)
            {
            case BYTEARRAY:
                ; /* cheat gcc */ 
                buf_len = flags.bytearray_length * 3 + 32;
                v = realloc(v, buf_len); /* for each byte and the suffix', this should be enough */

                if (v == NULL)
                {
                    show_error("memory allocation failed.\n");
                    return false;
                }
                data_to_bytearray_text(v, buf_len, reading_swath_index, reading_iterator, flags.bytearray_length);
                assert(strlen(v) + strlen(bytearray_suffix) + 1 <= buf_len); /* or maybe realloc is better? */
                strcat(v, bytearray_suffix);
                break;
            case STRING:
                ; /* cheat gcc */
                buf_len = flags.string_length + strlen(string_suffix) + 32; /* for the string and suffix, this should be enough */
                v = realloc(v, buf_len);
                if (v == NULL)
                {
                    show_error("memory allocation failed.\n");
                    return false;
                }
                data_to_printable_string(v, buf_len, reading_swath_index, reading_iterator, flags.string_length);
                assert(strlen(v) + strlen(string_suffix) + 1 <= buf_len); /* or maybe realloc is better? */
                strcat(v, string_suffix);
                break;
            default: /* numbers */
                ; /* cheat gcc */
                value_t val = data_to_val(reading_swath_index, reading_iterator /* ,MATCHES_AND_VALUES */);
                truncval_to_flags(&val, flags);

                valtostr(&val, v, buf_len);
                break;
            }

            void *address = remote_address_of_nth_element(reading_swath_index,
                reading_iterator /* ,MATCHES_AND_VALUES */);
            unsigned long address_ul = (unsigned long)address;
            int region_id = 99;
            unsigned long match_off = 0;
            const char *region_type = "??";
            /* get region info belonging to the match -
             * note: we assume the regions list and matches to be sorted
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
            fprintf(stdout, "[%2u] "POINTER_FMT", %2u + "POINTER_FMT", %5s, %s\n", 
                    i++, address_ul, region_id, match_off, region_type, v);
        }

        /* Go on to the next one... */
        ++reading_iterator;
        if (reading_iterator >= reading_swath_index->number_of_bytes)
        {
            assert(((matches_and_old_values_swath *)(local_address_beyond_last_element(reading_swath_index /* ,MATCHES_AND_VALUES */)))->number_of_bytes >= 0);
            reading_swath_index = local_address_beyond_last_element(reading_swath_index /* ,MATCHES_AND_VALUES */);
            reading_iterator = 0;
        }
    }

    free(v);
    return true;
}

/* XXX: handle multiple deletes, eg delete !1 2 3 4 5 6 */
bool handler__delete(globals_t * vars, char **argv, unsigned argc)
{
    unsigned id;
    char *end = NULL;
    match_location loc;

    if (argc != 2) {
        show_error("was expecting one argument, see `help delete`.\n");
        return false;
    }

    /* parse argument */
    id = strtoul(argv[1], &end, 0x00);

    /* check that strtoul() worked */
    if (argv[1][0] == '\0' || *end != '\0') {
        show_error("sorry, couldnt parse `%s`, try `help delete`\n", argv[1]);
        return false;
    }
    
    loc = nth_match(vars->matches, id);
    
    if (loc.swath)
    {
        /* It is not convenient to check whether anything else relies on this,
           so just mark it as not a REAL match */
        zero_match_flags(&loc.swath->data[loc.index].match_info);
        return true;
    }
    else
    {
        /* I guess this is not a valid match-id */
        show_warn("you specified a non-existant match `%u`.\n", id);
        show_info("use \"list\" to list matches, or \"help\" for other commands.\n");
        return false;
    }
}

bool handler__reset(globals_t * vars, char **argv, unsigned argc)
{
    USEPARAMS();

    if (vars->matches) { free(vars->matches); vars->matches = NULL; vars->num_matches = 0; }

    /* refresh list of regions */
    l_destroy(vars->regions);

    /* create a new linked list of regions */
    if ((vars->regions = l_init()) == NULL) {
        show_error("sorry, there was a problem allocating memory.\n");
        return false;
    }

    /* read in maps if a pid is known */
    if (vars->target && readmaps(vars->target, vars->regions) != true) {
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

bool handler__snapshot(globals_t * vars, char **argv, unsigned argc)
{
    USEPARAMS();
    

    /* check that a pid has been specified */
    if (vars->target == 0) {
        show_error("no target set, type `help pid`.\n");
        return false;
    }

    /* remove any existing matches */
    if (vars->matches) { free(vars->matches); vars->matches = NULL; vars->num_matches = 0; }

    if (searchregions(vars, MATCHANY, NULL) != true) {
        show_error("failed to save target address space.\n");
        return false;
    }

    return true;
}


/* dregion [!][x][,x,...] */
bool handler__dregion(globals_t * vars, char **argv, unsigned argc)
{
    unsigned id;
    bool invert = false;
    bool retval = false;
    char *end = NULL, *idstr = NULL, *block = NULL;
    element_t *np, *pp;
    struct vector_t * parsed_regions = NULL;

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
    
    /* check for an inverted match */
    if (*argv[1] == '!') {
        invert = true;
        /* create a copy of the argument for strtok(), +1 to skip '!' */
        block = strdupa(argv[1] + 1);
        
        /* check for lone '!' */
        if (*block == '\0') {
            show_error("inverting an empty set, maybe try `reset` instead?\n");
            goto dregion_failed;
        }
    } else {
        invert = false;
        block = strdupa(argv[1]);
    }

    np = vars->regions->tail;
    unsigned max_id = ((region_t *)np->data)->id;

    const unsigned int parsed_regions_cap = max_id+1;
    const unsigned int parsed_regions_count = 0;

    parsed_regions = vector_init(parsed_regions_count, parsed_regions_cap, sizeof(char));

    if (!parsed_regions) {
        show_error("no memory for parsed regions\n");
        goto dregion_failed;
    }

    memset( vector_at(parsed_regions, 0), (invert ? 1 : 0), parsed_regions_cap );

    // create list of region ids, and then loop over it
    char * block_end = NULL;
    char * idstr_copy = NULL;
    while ((idstr = strtok_r(block, ",", &block_end)) != NULL) {
        /* set block to NULL for strtok() */
        block = NULL;

        char * range_block_end = NULL, * range_idstr;
        unsigned range_ids[2], current_token = 0;

        idstr_copy = strdupa(idstr);

        if (!idstr_copy)
            goto dregion_failed;

        while ((range_idstr = strtok_r(idstr_copy, "..", &range_block_end)) != NULL ) {
            idstr_copy = NULL;

            id = strtoul(range_idstr, &end, 0x00);
            if (*end == '\0' && *idstr != '\0') {
                range_ids[current_token] = id;
                current_token++;
                if (current_token>1) {
                    unsigned idx;
                    for ( idx = range_ids[0]; idx <= range_ids[1]; ++idx ) {
                        char * at = vector_at(parsed_regions, idx);
                        if (!at) {
                            show_error("no memory for parsed regions\n");
                            goto dregion_failed;
                        }
                        *at = (invert ? 0 : 1);
                    }
                    break;
                }
            }
        }

        /* attempt to parse as a regionid */
        id = strtoul(idstr, &end, 0x00);

        /* good region */
        if (*end == '\0' && *idstr != '\0') {
            char * at = vector_at(parsed_regions, id);
            if (!at) {
                show_error("no memory for parsed regions\n");
                goto dregion_failed;
            }
            *at = (invert ? 0 : 1);
        }
    }

    /* initialise list pointers */
    np = vars->regions->head;
    pp = NULL;
    /* find the correct region node */
    while (np) {
        region_t * r = np->data;

        char * at = vector_at(parsed_regions, r->id);
        if (!at) {
            show_error("no memory for parsed regions\n");
            goto dregion_failed;
        }

        if (*at) {
            l_remove(vars->regions, pp, NULL);
            if (!pp) {
                np = vars->regions->head;
                continue;
            }
            np = pp->next;
            continue;
        }

        pp = np; /* keep track of prev for l_remove() */
        np = np->next;
    }

    retval = true;
    dregion_failed:

    if (parsed_regions)
        vector_destroy(parsed_regions);

    return retval;
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
    
    /* print a list of regions that are searched */
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

/* the name of the function is for history reason, now GREATERTHAN & LESSTHAN are also handled by this function */
bool handler__decinc(globals_t * vars, char **argv, unsigned argc)
{
    uservalue_t val;
    scan_match_type_t m;

    USEPARAMS();

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
        m = MATCHNOTCHANGED;
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
        show_error("unrecogised match type seen at decinc handler.\n");
        return false;
    }

    if (vars->matches) {
        if (checkmatches(vars, m, &val) == false) {
            show_error("failed to search target address space.\n");
            return false;
        }
    } else {
        /* < > = != cannot be the initial scan */
        if (argc == 1)
        {
            show_error("cannot use that search without matches\n");
            return false;
        }
        else
        {
            if (searchregions(vars, m, &val) != true) {
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

bool handler__version(globals_t * vars, char **argv, unsigned argc)
{
    USEPARAMS();

    printversion(stderr);
    return true;
}

bool handler__string(globals_t * vars, char **argv, unsigned argc)
{
    /* test scan_data_type */
    if (vars->options.scan_data_type != STRING)
    {
        show_error("scan_data_type is not string, see `help option`.\n");
        return false;
    }

    /* test the length */
    int i;
    for(i = 0; (i < 3) && vars->current_cmdline[i]; ++i) {}
    if (i != 3) /* cmdline too short */
    {
        show_error("please specify a string\n");
        return false;
    }
 
    /* the string being scanned */
    uservalue_t val;
    val.string_value = vars->current_cmdline+2;
    val.flags.string_length = strlen(val.string_value);
 
    /* need a pid for the rest of this to work */
    if (vars->target == 0) {
        return false;
    }

    /* user has specified an exact value of the variable to find */
    if (vars->matches) {
        /* already know some matches */
        if (checkmatches(vars, MATCHEQUALTO, &val) != true) {
            show_error("failed to search target address space.\n");
            return false;
        }
    } else {
        /* initial search */
        if (searchregions(vars, MATCHEQUALTO, &val) != true) {
            show_error("failed to search target address space.\n");
            return false;
        }
    }

    /* check if we now know the only possible candidate */
    if (vars->num_matches == 1) {
        show_info("match identified, use \"set\" to modify value.\n");
        show_info("enter \"help\" for other commands.\n");
    }

    return true;
}

bool handler__default(globals_t * vars, char **argv, unsigned argc)
{
    uservalue_t val;
    bool ret;
    bytearray_element_t *array = NULL;

    USEPARAMS();

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
            ret = false;
            goto retl;
        }
        if (!parse_uservalue_number(argv[0], &val)) {
            show_error("unable to parse command `%s`\n", argv[0]);
            ret = false;
            goto retl;
        }
        break;
    case BYTEARRAY:
        /* attempt to parse command as a bytearray */
        array = calloc(argc, sizeof(bytearray_element_t));
    
        if (array == NULL)
        {
            show_error("there's a memory allocation error.\n");
            ret = false;
            goto retl;
        }
        if (!parse_uservalue_bytearray(argv, argc, array, &val)) {
            show_error("unable to parse command `%s`\n", argv[0]);
            ret = false;
            goto retl;
        }
        break;
    case STRING:
        show_error("unable to parse command `%s`\nIf you want to scan for a string, use command `\"`.\n", argv[0]);
        ret = false;
        goto retl;
        break;
    default:
        assert(false);
        break;
    }

    /* need a pid for the rest of this to work */
    if (vars->target == 0) {
        ret = false;
        goto retl;
    }

    /* user has specified an exact value of the variable to find */
    if (vars->matches) {
        /* already know some matches */
        if (checkmatches(vars, MATCHEQUALTO, &val) != true) {
            show_error("failed to search target address space.\n");
            ret = false;
            goto retl;
        }
    } else {
        /* initial search */
        if (searchregions(vars, MATCHEQUALTO, &val) != true) {
            show_error("failed to search target address space.\n");
            ret = false;
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
    if (array)
        free(array);

    return ret;
}

bool handler__update(globals_t * vars, char **argv, unsigned argc)
{

    USEPARAMS();
    if (vars->matches) {
        if (checkmatches(vars, MATCHANY, NULL) == false) {
            show_error("failed to scan target address space.\n");
            return false;
        }
    } else {
        show_error("cannot use that command without matches\n");
        return false;
    }

    return true;
}

bool handler__exit(globals_t * vars, char **argv, unsigned argc)
{
    USEPARAMS();

    vars->exit = 1;
    return true;
}

#define DOC_COLUMN  11           /* which column descriptions start on with help command */

bool handler__help(globals_t * vars, char **argv, unsigned argc)
{
    bool ret = false;
    list_t *commands = vars->commands;
    element_t *np = NULL;
    command_t *def = NULL;
    assert(commands != NULL);
    assert(argc >= 1);

    np = commands->head;

    /* pager support, dirty ugly hardcoded */
    FILE *outfd = popen("more", "w"); 
    if (outfd == NULL)
    {
        show_warn("Cannot execute pager, fall back to normal output\n");
        outfd = stderr;
    }

    /* print version information for generic help */
    if (argv[1] == NULL)
        printversion(outfd);

    /* traverse the commands list, printing out the relevant documentation */
    while (np) {
        command_t *command = np->data;

        /* remember the default command */
        if (command->command == NULL)
            def = command;

        /* just `help` with no argument */
        if (argv[1] == NULL) {
            /* NULL shortdoc means dont print in help listing */
            if (command->shortdoc == NULL) {
                np = np->next;
                continue;
            }

            /* print out command name */
            fprintf(outfd, "%-*s%s\n", DOC_COLUMN, command->command ? command->command : "default", command->shortdoc);

            /* detailed information requested about specific command */
        } else if (command->command
                   && strcasecmp(argv[1], command->command) == 0) {
            fprintf(outfd, "%s\n", command->longdoc ? command-> longdoc : "missing documentation");
            ret = true;
            goto retl;
        }

        np = np->next;
    }

    if (argc > 1) {
        show_error("unknown command `%s`\n", argv[1]);
        ret = false;
    } else if (def) {
        fprintf(outfd, "\n%s\n", def->longdoc ? def->longdoc : "");
        ret = true;
    }
    
    ret = true;

retl:
    if (outfd != stderr)
        pclose(outfd);

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
    value_t o, n;
    unsigned id;
    char *end = NULL, buf[128], timestamp[64];
    time_t t;
    match_location loc;
    value_t old_val;
    void *address;

    if (argc != 2) {
        show_error("was expecting one argument, see `help watch`.\n");
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

    /* check this is a valid match-id */
    if (!loc.swath) {
        show_error("you specified a non-existent match `%u`.\n", id);
        show_info("use \"list\" to list matches, or \"help\" for other commands.\n");
        return false;
    }
    
    address = remote_address_of_nth_element(loc.swath, loc.index /* ,MATCHES_AND_VALUES */);
    
    old_val = data_to_val(loc.swath, loc.index /* ,MATCHES_AND_VALUES */);
    valcpy(&o, &old_val);
    valcpy(&n, &o);

    valtostr(&o, buf, sizeof(buf));

    if (INTERRUPTABLE()) {
        (void) detach(vars->target);
        ENDINTERRUPTABLE();
        return true;
    }

    /* every entry is timestamped */
    t = time(NULL);
    strftime(timestamp, sizeof(timestamp), "[%T]", localtime(&t));

    show_info("%s monitoring %10p for changes until interrupted...\n", timestamp, address);

    while (true) {

        if (attach(vars->target) == false)
            return false;

        if (peekdata(vars->target, address, &n) == false)
            return false;


        truncval(&n, &old_val);

        /* check if the new value is different */
        match_flags tmpflags;
        zero_match_flags(&tmpflags);
        scan_routine_t valuecmp_routine = (get_scanroutine(ANYNUMBER, MATCHCHANGED));
        if (valuecmp_routine(&o, &n, NULL, &tmpflags, address)) {

            valcpy(&o, &n);
            truncval(&o, &old_val);

            valtostr(&o, buf, sizeof(buf));

            /* fetch new timestamp */
            t = time(NULL);
            strftime(timestamp, sizeof(timestamp), "[%T]", localtime(&t));

            show_info("%s %10p -> %s\n", timestamp, address,
                    buf);
        }

        /* detach after valuecmp_routine, since it may read more data (e.g. bytearray) */
        detach(vars->target);

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
        printversion(stderr);
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
        show_error("memory allocation failed.\n");
        return false;
    }

    if (!read_array(vars->target, addr, buf, len))
    {
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
        /* print it out */
        int i,j;
        int buf_idx = 0;
        for (i = 0; i + 16 < len; i += 16)
        {
            if (vars->options.backend == 0)
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
            if (vars->options.backend == 0)
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

    free(buf);
    return true;
}

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

    /* try int first */
    if ((strcasecmp(argv[1], "i8") == 0)
      ||(strcasecmp(argv[1], "int8") == 0))
    {
        data_width = 1;
        datatype = 0;
        fmt = "%"PRId8;
    }
    else if ((strcasecmp(argv[1], "i16") == 0)
           ||(strcasecmp(argv[1], "int16") == 0))
    {
        data_width = 2;
        datatype = 0;
        fmt = "%"PRId16;
    }
    else if ((strcasecmp(argv[1], "i32") == 0)
           ||(strcasecmp(argv[1], "int32") == 0))
    {
        data_width = 4;
        datatype = 0;
        fmt = "%"PRId32;
    }
    else if ((strcasecmp(argv[1], "i64") == 0)
           ||(strcasecmp(argv[1], "int64") == 0))
    {
        data_width = 8;
        datatype = 0;
        fmt = "%"PRId64;
    }
    else if ((strcasecmp(argv[1], "f32") == 0)
           ||(strcasecmp(argv[1], "float32") == 0))  
    {
        data_width = 4;
        datatype = 0;
        fmt = "%f";
    }
    else if ((strcasecmp(argv[1], "f64") == 0)
           ||(strcasecmp(argv[1], "float64") == 0))
    {
        data_width = 8;
        datatype = 0;
        fmt = "%lf";
    }
    else if (strcasecmp(argv[1], "bytearray") == 0)
    {
        data_width = argc - 3;
        datatype = 1;
    }
    else if (strcasecmp(argv[1], "string") == 0)
    {
        /* locate the string parameter, say locate the beginning of the 4th parameter (2 characters after the end of the 3rd paramter)*/
        int i;
        string_parameter = vars->current_cmdline;
        for(i = 0; i < 3; ++i)
        {
            while(((*string_parameter) == ' ')
                 ||(*string_parameter) == '\t') 
                ++ string_parameter;
            while(((*string_parameter) != ' ')
                 &&(*string_parameter) != '\t') 
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
        bytearray_element_t *array = calloc(data_width, sizeof(bytearray_element_t));
        uservalue_t val_buf;
        if (array == NULL)
        {
            show_error("memory allocation failed.\n");
            ret = false;
            goto retl;
        }
        if(!parse_uservalue_bytearray(argv+3, argc-3, array, &val_buf)) 
        {
            show_error("bad byte array speicified.\n");
            free(array);
            ret = false;
            goto retl;
        }
        int i;

        {
            // if wildcard is provided in the bytearray, we need the original data.
            bool wildcard_used = false;
            for(i = 0; i < data_width; ++i)
            {
                if(array[i].is_wildcard == 1)
                {
                    wildcard_used = true;
                    break;
                }
            }
            if (wildcard_used)
            {
                if(!read_array(vars->target, addr, buf, data_width))
                {
                    show_error("read memory failed.\n");
                    free(array);
                    ret = false;
                    goto retl;
                }
            }
        }

        for(i = 0; i < data_width; ++i)
        {
            bytearray_element_t *cur_element = array+i;
            if(cur_element->is_wildcard == 0)
            {
                buf[i] = cur_element->byte;
            }
        }
        free(array);
        break;
    case 2: //string
        strncpy(buf, string_parameter, data_width);
        break;
    default:
        assert(false);
    }

    /* write into memory */
    ret = write_array(vars->target, addr, buf, data_width);

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
        if (strcasecmp(argv[2], "number") == 0) { vars->options.scan_data_type = ANYNUMBER; }
        else if (strcasecmp(argv[2], "int") == 0) { vars->options.scan_data_type = ANYINTEGER; }
        else if (strcasecmp(argv[2], "int8") == 0) { vars->options.scan_data_type = INTEGER8; }
        else if (strcasecmp(argv[2], "int16") == 0) { vars->options.scan_data_type = INTEGER16; }
        else if (strcasecmp(argv[2], "int32") == 0) { vars->options.scan_data_type = INTEGER32; }
        else if (strcasecmp(argv[2], "int64") == 0) { vars->options.scan_data_type = INTEGER64; }
        else if (strcasecmp(argv[2], "float") == 0) { vars->options.scan_data_type = ANYFLOAT; }
        else if (strcasecmp(argv[2], "float32") == 0) { vars->options.scan_data_type = FLOAT32; }
        else if (strcasecmp(argv[2], "float64") == 0) { vars->options.scan_data_type = FLOAT64; }
        else if (strcasecmp(argv[2], "bytearray") == 0) { vars->options.scan_data_type = BYTEARRAY; }
        else if (strcasecmp(argv[2], "string") == 0) { vars->options.scan_data_type = STRING; }
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
    else if (strcasecmp(argv[1], "detect_reverse_change") == 0)
    {
        if (strcmp(argv[2], "0") == 0) {vars->options.detect_reverse_change = 0; }
        else if (strcmp(argv[2], "1") == 0) {vars->options.detect_reverse_change = 1; }
        else
        {
            show_error("bad value for detect_reverse_change, see `help option`.\n");
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
