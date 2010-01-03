/*
 $Id: handlers.c,v 1.12 2007-06-05 01:45:34+01 taviso Exp taviso $

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

#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif

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

#include <readline/readline.h>

#include "scanmem.h"
#include "commands.h"
#include "handlers.h"
#include "interrupt.h"

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
        fprintf(stderr,
                "error: expected an argument, type `help set` for details.\n");
        return false;
    }

    /* check if there are any matches */
    if (vars->num_matches == 0) {
        fprintf(stderr, "error: no matches are known.\n");
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
                fprintf(stderr,
                        "error: you specified an empty delay count, `%s`, see `help set`.\n",
                        settings[block].value);
                return false;
            } else if (*end != '\0') {
                /* parse failed before end, probably trailing garbage, eg 34=9/16foo */
                fprintf(stderr,
                        "error: trailing garbage after delay count, `%s`.\n",
                        settings[block].value);
                return false;
            } else if (settings[block].seconds == 0) {
                /* 10=24/0 disables continous mode */
                fprintf(stderr,
                        "info: you specified a zero delay, disabling continuous mode.\n");
            } else {
                /* valid delay count seen and understood */
                fprintf(stderr,
                        "info: setting %s every %u seconds until interrupted...\n",
                        settings[block].matchids ? settings[block].
                        matchids : "all", settings[block].seconds);

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
        detach(vars->target);
        ENDINTERRUPTABLE();
        return true;
    }

    /* --- execute the parsed setting structs --- */

    while (true) {
        value_t val;
        char *end = NULL;

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
            strtoval(settings[block].value, &end, 0x00, &val);

            /* check that converted successfully */
            if (*end != '\0') {
                fprintf(stderr, "error: could not parse value `%s`\n",
                        settings[block].value);
                goto fail;
            } else if (*settings[block].value == '\0') {
                fprintf(stderr, "error: you didn't specify a value.\n");
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
                        fprintf(stderr,
                                "error: could not parse match id `%s`\n", id);
                        goto fail;
                    }

                    /* check this is a valid match-id */
                    loc = nth_match(vars->matches, num);
                    if (loc.swath) {
                        value_t v;
                        value_t old;
                        void *address = remote_address_of_nth_element((unknown_type_of_swath *)loc.swath, loc.index, MATCHES_AND_VALUES);

                        fprintf(stderr, "info: setting *%p to %#llx...\n",
                                address, (long long)val.int_value);

                        /* copy val onto v */
                        valcpy(&v, &val);

                        /* XXX: valcmp? make sure the sizes match */
                        old = data_to_val((unknown_type_of_swath *)loc.swath, loc.index, MATCHES_AND_VALUES);
                        truncval(&v, &old);

                        /* set the value specified */
                        if (setaddr(vars->target, address, &v) == false) {
                            fprintf(stderr, "error: failed to set a value.\n");
                            goto fail;
                        }

                    } else {
                        /* match-id > than number of matches */
                        fprintf(stderr,
                                "error: found an invalid match-id `%s`\n", id);
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
                        void *address = remote_address_of_nth_element((unknown_type_of_swath *)reading_swath_index, reading_iterator, MATCHES_AND_VALUES);

                        /* XXX: as above : make sure the sizes match */
                        value_t old = data_to_val((unknown_type_of_swath *)reading_swath_index, reading_iterator, MATCHES_AND_VALUES);
                        truncval(&val, &old);

                        fprintf(stderr, "info: setting *%p to %#llx...\n",
				address, (long long)val.int_value);


                        if (setaddr(vars->target, address, &val) == false) {
                            fprintf(stderr, "error: failed to set a value.\n");
                            goto fail;
                        }
                    }
                     
                     /* Go on to the next one... */
                    ++reading_iterator;
                    if (reading_iterator >= reading_swath_index->number_of_bytes)
                    {
                        reading_swath_index = local_address_beyond_last_element((unknown_type_of_swath *)reading_swath_index, MATCHES_AND_VALUES);
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
/* example: [012] 0xffffff, csLfznu, 120, /lib/libc.so */

bool handler__list(globals_t * vars, char **argv, unsigned argc)
{
    unsigned i = 0;

    USEPARAMS();

    if(!(vars->matches))
        return true;

    matches_and_old_values_swath *reading_swath_index = (matches_and_old_values_swath *)vars->matches->swaths;
    int reading_iterator = 0;

    /* list all known matches */
    while (reading_swath_index->first_byte_in_child) {
        char v[80];

        match_flags flags = reading_swath_index->data[reading_iterator].match_info;

        /* Only actual matches are considered */
        if (flags_to_max_width_in_bytes(flags) > 0)
        {
            value_t val = data_to_val((unknown_type_of_swath *)reading_swath_index, reading_iterator, MATCHES_AND_VALUES);
            truncval_to_flags(&val, flags);

            if (valtostr(&val, v, sizeof(v)) != true) {
                strncpy(v, "unknown", sizeof(v));
            }

/* try to determine the size of a pointer */
#if ULONGMAX == 4294967295UL
#define POINTER_FMT "10"
#elif ULONGMAX == 18446744073709551615UL
#define POINTER_FMT "20"
#else
#define POINTER_FMT "20"
#endif

            fprintf(stdout, "[%2u] %"POINTER_FMT"p, %s\n", i++, remote_address_of_nth_element((unknown_type_of_swath *)reading_swath_index, reading_iterator, MATCHES_AND_VALUES), v);
        }
	
        /* Go on to the next one... */
        ++reading_iterator;
        if (reading_iterator >= reading_swath_index->number_of_bytes)
        {
            assert(((matches_and_old_values_swath *)(local_address_beyond_last_element((unknown_type_of_swath *)reading_swath_index, MATCHES_AND_VALUES)))->number_of_bytes >= 0);
            reading_swath_index = local_address_beyond_last_element((unknown_type_of_swath *)reading_swath_index, MATCHES_AND_VALUES);
            reading_iterator = 0;
        }
    }

    return true;
}

/* XXX: handle multiple deletes, eg delete !1 2 3 4 5 6 */
bool handler__delete(globals_t * vars, char **argv, unsigned argc)
{
    unsigned id;
    char *end = NULL;
    match_location loc;

    if (argc != 2) {
        fprintf(stderr,
                "error: was expecting one argument, see `help delete`.\n");
        return false;
    }

    /* parse argument */
    id = strtoul(argv[1], &end, 0x00);

    /* check that strtoul() worked */
    if (argv[1][0] == '\0' || *end != '\0') {
        fprintf(stderr, "error: sorry, couldnt parse `%s`, try `help delete`\n",
                argv[1]);
        return false;
    }
    
    loc = nth_match(vars->matches, id);
    
    if (loc.swath)
    {
        /* It is not convenient to check whether anything else relies on this, so just mark it as not a REAL match */
        loc.swath->data[loc.index].match_info = (match_flags){0};
        return true;
    }
    else
    {

        /* I guess this is not a valid match-id */
        fprintf(stderr, "warn: you specified a non-existant match `%u`.\n", id);
        fprintf(stderr, "info: use \"list\" to list matches, or \"help\" for other commands.\n");
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
        fprintf(stderr,
                "error: sorry, there was a problem allocating memory.\n");
        return false;
    }

    /* read in maps if a pid is known */
    if (vars->target && readmaps(vars->target, vars->regions) != true) {
        fprintf(stderr,
                "error: sorry, there was a problem getting a list of regions to search.\n");
        fprintf(stderr,
                "warn: the pid may be invalid, or you don't have permission.\n");
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
            fprintf(stderr, "error: `%s` does not look like a valid pid.\n",
                    argv[1]);
            return false;
        }
    } else if (vars->target) {
        /* print the pid of the target program */
        fprintf(stderr, "info: target pid is %u.\n", vars->target);
        return true;
    } else {
        fprintf(stderr, "info: no target is currently set.\n");
        return false;
    }

    return handler__reset(vars, resetargv, 1);
}

bool handler__snapshot(globals_t * vars, char **argv, unsigned argc)
{
    USEPARAMS();
    value_t v;
    
    /* unused */
    memset(&v, 0x00, sizeof(v));

    /* check that a pid has been specified */
    if (vars->target == 0) {
        fprintf(stderr, "error: no target set, type `help pid`.\n");
        return false;
    }

    /* remove any existing matches */
    if (vars->matches) { free(vars->matches); vars->matches = NULL; vars->num_matches = 0; }

    if (searchregions(vars, v, true) != true) {
        fprintf(stderr, "error: failed to save target address space.\n");
        return false;
    }

    return true;
}

/* dregion [!][x][,x,...] */
bool handler__dregion(globals_t * vars, char **argv, unsigned argc)
{
    unsigned id;
    bool invert = false;
    char *end = NULL, *idstr = NULL, *block = NULL;
    element_t *np, *p, *pp;
    list_t *keep = NULL;
    region_t *save;

    /* need an argument */
    if (argc < 2) {
        fprintf(stderr, "error: expected an argument, see `help dregion`.\n");
        return false;
    }

     /* check that there is a process known */
    if (vars->target == 0) {
        fprintf(stderr, "error: no target specified, see `help pid`\n");
        return false;
    }
    
    /* check for an inverted match */
    if (*argv[1] == '!') {
        invert = true;
        /* create a copy of the argument for strtok(), +1 to skip '!' */
        block = strdupa(argv[1] + 1);
        
        /* check for lone '!' */
        if (*block == '\0') {
            fprintf(stderr, "error: inverting an empty set, maybe try `reset` instead?\n");
            return false;
        }
        
        /* create a list to keep the specified regions */
        if ((keep = l_init()) == NULL) {
            fprintf(stderr, "error: memory allocation error.\n");
            return false;
        }
        
    } else {
        invert = false;
        block = strdupa(argv[1]);
    }

    /* loop for every number specified, eg "1,2,3,4,5" */
    while ((idstr = strtok(block, ",")) != NULL) {
        region_t *r = NULL;
        
        /* set block to NULL for strtok() */
        block = NULL;
        
        /* attempt to parse as a regionid */
        id = strtoul(idstr, &end, 0x00);

        /* check that worked, "1,abc,4,,5,6foo" */
        if (*end != '\0' || *idstr == '\0') {
            fprintf(stderr, "error: could not parse argument %s.\n", idstr);
            if (invert) {
                if (l_concat(vars->regions, &keep) == -1) {
                    fprintf(stderr, "error: there was a problem restoring saved regions.\n");
                    l_destroy(vars->regions);
                    l_destroy(keep);
                    return false;
                }
            }
            assert(keep == NULL);
            return false;
        }
        
        /* initialise list pointers */
        np = vars->regions->head;
        p = pp = NULL;
        
        /* find the correct region node */
        while (np) {
            r = np->data;
            
            /* compare the node id to the id the user specified */
            if (r->id == id)
                break;
            
            pp = np; /* keep track of prev for l_remove() */
            np = np->next;
        }

        /* check if a match was found */
        if (np == NULL) {
            fprintf(stderr, "error: no region matching %u, or already moved.\n", id);
            if (invert) {
                if (l_concat(vars->regions, &keep) == -1) {
                    fprintf(stderr, "error: there was a problem restoring saved regions.\n");
                    l_destroy(vars->regions);
                    l_destroy(keep);
                    return false;
                }
            }
            if (keep)
                l_destroy(keep);
            return false;
        }
        
        np = pp;
        
        /* save this region if the match is inverted */
        if (invert) {
            
            assert(keep != NULL);
            
            l_remove(vars->regions, np, (void *) &save);
            if (l_append(keep, keep->tail, save) == -1) {
                fprintf(stderr, "error: sorry, there was an internal memory error.\n");
                free(save);
                return false;
            }
            continue;
        }
        
        /* check for any affected matches before removing it */
        {
            region_t *s;

            /* determine the correct pointer we're supposed to be checking */
            if (np) {
                assert(np->next);
                s = np->next->data;
            } else {
                /* head of list */
                s = vars->regions->head->data;
            }
            
            if (!(vars->matches = delete_by_region(vars->matches, &vars->num_matches, s, false)))
            {
                fprintf(stderr, "error: memory allocation error while deleting matches\n");
            }
        }

        l_remove(vars->regions, np, NULL);
    }

    if (invert) {
        region_t *s = keep->head->data;
        
        if (!(vars->matches = delete_by_region(vars->matches, &vars->num_matches, s, true)))
        {
            fprintf(stderr, "error: memory allocation error while deleting matches\n");
        }
        
        /* okay, done with the regions list */
        l_destroy(vars->regions);
        
        /* and switch to the keep list */
        vars->regions = keep;
    }

    return true;
}

bool handler__lregions(globals_t * vars, char **argv, unsigned argc)
{
    element_t *np = vars->regions->head;

    USEPARAMS();

    if (vars->target == 0) {
        fprintf(stderr, "error: no target has been specified, see `help pid`.\n");
        return false;
    }

    if (vars->regions->size == 0) {
        fprintf(stdout, "info: no regions are known.\n");
    }
    
    /* print a list of regions that are searched */
    while (np) {
        region_t *region = np->data;

        fprintf(stderr, "[%2u] %#10lx, %7lu bytes, %c%c%c, %s\n",
                region->id, (unsigned long)region->start, region->size,
                region->flags.read ? 'r' : '-',
                region->flags.write ? 'w' : '-',
                region->flags.exec ? 'x' : '-',
                region->filename[0] ? region->filename : "unassociated");
        np = np->next;
    }

    return true;
}

bool handler__decinc(globals_t * vars, char **argv, unsigned argc)
{
    value_t val;
    scan_match_type_t m;

    USEPARAMS();

    memset(&val, 0x00, sizeof(val));

    switch (argv[0][0]) {
    case '=':
        m = MATCHNOTCHANGED;
        break;
    case '<':
        m = MATCHDECREASED;
        break;
    case '>':
        m = MATCHINCREASED;
        break;
    default:
        fprintf(stderr,
                "error: unrecogised match type seen at decinc handler.\n");
        return false;
    }

    /* the last seen value is still there */
    if (vars->matches) {
        if (checkmatches(vars, val, m) == false) {
            fprintf(stderr, "error: failed to search target address space.\n");
            return false;
        }
    } else {
        fprintf(stderr, "error: cannot use that search without matches\n");
        return false;
    }

    if (vars->num_matches == 1) {
        fprintf(stderr,
                "info: match identified, use \"set\" to modify value.\n");
        fprintf(stderr, "info: enter \"help\" for other commands.\n");
    }

    return true;
}

bool handler__version(globals_t * vars, char **argv, unsigned argc)
{
    USEPARAMS();

    printversion(stdout);
    return true;
}

bool handler__default(globals_t * vars, char **argv, unsigned argc)
{
    char *end = NULL;
    value_t val;

    USEPARAMS();

    /* attempt to parse command as a number */
    strtoval(argv[0], &end, 0x00, &val);

    /* check if that worked */
    if (*end != '\0') {
        fprintf(stderr,
                "error: unable to parse command `%s`, gave up at `%s`\n",
                argv[0], end);
        return false;
    }

    /* need a pid for the rest of this to work */
    if (vars->target == 0) {
        return false;
    }

    /* user has specified an exact value of the variable to find */
    if (vars->matches) {
        /* already know some matches */
        if (checkmatches(vars, val, MATCHEQUALTO) != true) {
            fprintf(stderr, "error: failed to search target address space.\n");
            return false;
        }
    } else {
        /* initial search */
        if (searchregions(vars, val, false) != true) {
            fprintf(stderr, "error: failed to search target address space.\n");
            return false;
        }
    }

    /* check if we now know the only possible candidate */
    if (vars->num_matches == 1) {
        fprintf(stderr,
                "info: match identified, use \"set\" to modify value.\n");
        fprintf(stderr, "info: enter \"help\" for other commands.\n");
    }

    return true;
}

bool handler__update(globals_t * vars, char **argv, unsigned argc)
{
    value_t val;

    USEPARAMS();
    memset(&val, 0x00, sizeof(val));
    if (vars->matches) {
        if (checkmatches(vars, val, MATCHANY) == false) {
            fprintf(stderr, "error: failed to scan target address space.\n");
            return false;
        }
    } else {
        fprintf(stderr, "error: cannot use that command without matches\n");
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

#define DOC_COLUMN 11           /* which column descriptions start on with help command */

bool handler__help(globals_t * vars, char **argv, unsigned argc)
{
    list_t *commands = vars->commands;
    element_t *np = NULL;
    command_t *def = NULL;
    assert(commands != NULL);
    assert(argc >= 1);

    np = commands->head;

    /* print version information for generic help */
    if (argv[1] == NULL)
        printversion(stdout);

    /* traverse the commands list, printing out the relevant documentation */
    while (np) {
        command_t *command = np->data;

        /* remember the default command */
        if (command->command == NULL)
            def = command;

        /* just `help` with no argument */
        if (argv[1] == NULL) {
            int width;

            /* NULL shortdoc means dont print in help listing */
            if (command->shortdoc == NULL) {
                np = np->next;
                continue;
            }

            /* print out command name */
            if ((width =
                 fprintf(stdout, "%s",
                         command->command ? command->command : "default")) <
                0) {
                /* hmm? */
                np = np->next;
                continue;
            }

            /* print out the shortdoc description */
            fprintf(stdout, "%*s%s\n", DOC_COLUMN - width, "",
                    command->shortdoc);

            /* detailed information requested about specific command */
        } else if (command->command
                   && strcasecmp(argv[1], command->command) == 0) {
            fprintf(stdout, "%s\n",
                    command->longdoc ? command->
                    longdoc : "missing documentation");
            return true;
        }

        np = np->next;
    }

    if (argc > 1) {
        fprintf(stderr, "error: unknown command `%s`\n", argv[1]);
        return false;
    } else if (def) {
        fprintf(stdout, "\n%s\n", def->longdoc ? def->longdoc : "");
    }

    return true;
}

bool handler__eof(globals_t * vars, char **argv, unsigned argc)
{
    fprintf(stdout, "exit\n");
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
        fprintf(stderr,
                "error: shell command requires an argument, see `help shell`.\n");
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
        fprintf(stderr, "error: system() failed, command was not executed.\n");
        return false;
    }

    return true;
}

bool handler__watch(globals_t * vars, char **argv, unsigned argc)
{
    value_t o, n;
    unsigned id;
    char *end = NULL, buf[64], timestamp[64];
    time_t t;
    match_location loc;
    value_t old_val;
    void *address;

    if (argc != 2) {
        fprintf(stderr,
                "error: was expecting one argument, see `help watch`.\n");
        return false;
    }

    /* parse argument */
    id = strtoul(argv[1], &end, 0x00);

    /* check that strtoul() worked */
    if (argv[1][0] == '\0' || *end != '\0') {
        fprintf(stderr, "error: sorry, couldn't parse `%s`, try `help watch`\n",
                argv[1]);
        return false;
    }
    
    loc = nth_match(vars->matches, id);

    /* check this is a valid match-id */
    if (!loc.swath) {
        fprintf(stderr, "error: you specified a non-existent match `%u`.\n",
                id);
        fprintf(stderr,
                "info: use \"list\" to list matches, or \"help\" for other commands.\n");
        return false;
    }
    
    address = remote_address_of_nth_element((unknown_type_of_swath *)loc.swath, loc.index, MATCHES_AND_VALUES);
    
    old_val = data_to_val((unknown_type_of_swath *)loc.swath, loc.index, MATCHES_AND_VALUES);
    valcpy(&o, &old_val);
    valcpy(&n, &o);

    if (valtostr(&o, buf, sizeof(buf)) == false) {
        strncpy(buf, "unknown", sizeof(buf));
    }

    if (INTERRUPTABLE()) {
        (void) detach(vars->target);
        ENDINTERRUPTABLE();
        return true;
    }

    /* every entry is timestamped */
    t = time(NULL);
    strftime(timestamp, sizeof(timestamp), "[%T]", localtime(&t));

    fprintf(stdout,
            "info: %s monitoring %10p for changes until interrupted...\n",
            timestamp, address);

    while (true) {

        if (attach(vars->target) == false)
            return false;

        if (peekdata(vars->target, address, &n) == false)
            return false;

        detach(vars->target);

        truncval(&n, &old_val);

        /* check if the new value is different */
        if (valuecmp(&o, MATCHNOTEQUALTO, &n, NULL)) {

            valcpy(&o, &n);
            truncval(&o, &old_val);

            if (valtostr(&o, buf, sizeof(buf)) == false) {
                strncpy(buf, "unknown", sizeof(buf));
            }

            /* fetch new timestamp */
            t = time(NULL);
            strftime(timestamp, sizeof(timestamp), "[%T]", localtime(&t));

            fprintf(stdout, "info: %s %10p -> %s\n", timestamp, address,
                    buf);
        }

        (void) sleep(1);
    }
}

#include "licence.h"

bool handler__show(globals_t * vars, char **argv, unsigned argc)
{
    USEPARAMS();
    
    if (argv[1] == NULL) {
        fprintf(stderr, "error: expecting an argument.\n");
        return false;
    }
    
    if (strcmp(argv[1], "copying") == 0)
        fprintf(stdout, SM_COPYING);
    else if (strcmp(argv[1], "warranty") == 0)
        fprintf(stdout, SM_WARRANTY);
    else if (strcmp(argv[1], "version") == 0)
        printversion(stdout);
    else {
        fprintf(stderr, "error: unrecognized show command `%s`\n", argv[1]);
        return false;
    }
    
    return true;
}

bool handler__options(globals_t * vars, char **argv, unsigned argc)
{
    return true;    
}

bool handler__write(globals_t * vars, char **argv, unsigned argc)
{
    int data_width = 0;
    const char *fmt;
    void *addr;
    char buf[10];
    int buf_length = 0;

    if (argc != 4)
    {
        fprintf(stderr, "error: bad arguments, see `help write`.\n");
        return false;
    }

    /* try int first */
    if ((strcasecmp(argv[1], "i8") == 0)
      ||(strcasecmp(argv[1], "int8") == 0))
    {
        data_width = 1;
        fmt = "%hhd";
    }
    else if ((strcasecmp(argv[1], "i16") == 0)
           ||(strcasecmp(argv[1], "int16") == 0))
    {
        data_width = 2;
        fmt = "%hd";
    }
    else if ((strcasecmp(argv[1], "i32") == 0)
           ||(strcasecmp(argv[1], "int32") == 0))
    {
        data_width = 4;
        fmt = "%d";
    }
    else if ((strcasecmp(argv[1], "i64") == 0)
           ||(strcasecmp(argv[1], "int64") == 0))
    {
        data_width = 8;
        fmt = "%lld";
    }
    else if ((strcasecmp(argv[1], "f32") == 0)
           ||(strcasecmp(argv[1], "float32") == 0))  
    {
        data_width = 4;
        fmt = "%f";
    }
    else if ((strcasecmp(argv[1], "f64") == 0)
           ||(strcasecmp(argv[1], "float64") == 0))
    {
        data_width = 8;
        fmt = "%lf";
    }
    /* may support more types here */
    else
    {
        fprintf(stderr, "error: bad data_type, see `help write`.\n");
        return false;
    }

    /* check address */
    if (sscanf(argv[2], "%p", &addr) < 1)
    {
        fprintf(stderr, "error: bad address, see `help write`.\n");
        return false;
    }

    /* load value into buffer */
    if(sscanf(argv[3], fmt, buf) < 1) /* should be OK even for max uint64 */
    {
        fprintf(stderr, "error: bad value, see `help write`.\n");
        return false;
    }
    buf_length = data_width; 

    /* write into memory */
    return write_array(vars->target, addr, buf, buf_length);
}

bool handler__option(globals_t * vars, char **argv, unsigned argc)
{
    if (argc != 3)
    {
        fprintf(stderr, "error: bad arguments, see `help option`.\n");
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
        else
        {
            fprintf(stderr, "error: bad value for scan_data_type, see `help option`.\n");
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
            fprintf(stderr, "error: bad value for region_scan_level, see `help option`.\n");
            return false;
        }
    }
    else
    {
        fprintf(stderr, "error: unknown option specified, see `help option`.\n");
        return false;
    }
    return true;
}
