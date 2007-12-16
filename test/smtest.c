/*
*
* $Id: smtest.c,v 1.2 2007-04-14 18:47:39+01 taviso Exp taviso $
*/

#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif

#include <sys/mman.h>
#include <stdio.h>
#include <time.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>             /*lint -e537 */
#include <stdbool.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>

#define MSIZE 4096

/* list of functions where i dont want to be warned about ignored return value */

/*lint -esym(534,strftime,fflush,sleep,alarm) */

typedef enum { TYPE_CHAR, TYPE_SHORT, TYPE_FLOAT, TYPE_INT, TYPE_LONG } type_t;
typedef enum { MODE_UNKNOWN, MODE_POPULATE, MODE_INDEX, MODE_SEARCH, MODE_ITER, MODE_EXIT } tmode_t;

/* empty signal handler for use with pause() */
static void null(int n) { (void) n; return; }

int main(int argc, char **argv)
{
    unsigned offset = 0, value = 0xff, count = MSIZE;
    int fd, optindex, i = 0;
    type_t type = TYPE_CHAR;
    tmode_t mode = MODE_UNKNOWN;
    pid_t pid;
    char *end;
    struct option longopts[] = {
        {"type", 0, NULL, 't'},
        {"value", 1, NULL, 'v'},
        {"mode", 1, NULL, 'm'},
        {"offset", 1, NULL, 'o'},
        {"count", 1, NULL, 'c'},
        {NULL, 0, NULL, 0},
    };
    union {
        void *region;
        unsigned char *region_char;
        unsigned short *region_short;
        unsigned int *region_int;
        unsigned long *region_long;
        float *region_float;
    } region_ptr;
    
    /* process command line */
    while (true) {
        switch (getopt_long(argc, argv, "c:m:v:t:o:", longopts, &optindex)) {
            case 'm':
                /* mode of operation */
                if (strcmp(optarg, "populate") == 0) {
                    mode = MODE_POPULATE;
                } else if (strcmp(optarg, "index") == 0) {
                    mode = MODE_INDEX;
                } else if (strcmp(optarg, "search") == 0) {
                    mode = MODE_SEARCH;
                } else if (strcmp(optarg, "iter") == 0) {
                    mode = MODE_ITER;
                } else if (strcmp(optarg, "exit") == 0) {
                    mode = MODE_EXIT;
                } else {
                    fprintf(stderr, "error: unrecognised mode %s.\n", optarg);
                    return 1;
                }
                break;
            case 'o':
                /* offset to place into region */
                offset = strtoul(optarg, &end, 0x00);
                if (*end != '\0' || *optarg == '\0') {
                    fprintf(stderr, "error: unable to parse offset.\n");
                    return 1;
                }
                break;
            case 'v':
                /* value to use */
                value = strtoul(optarg, &end, 0x00);
                if (*end != '\0' || *optarg == '\0') {
                    fprintf(stderr, "error: unable to parse value.\n");
                    return 1;
                }
                break;
            case 't':
                if (strcmp(optarg, "char") == 0) {
                    type = TYPE_CHAR;
                } else if (strcmp(optarg, "short") == 0) {
                    type = TYPE_SHORT;
                } else if (strcmp(optarg, "int") == 0) {
                    type = TYPE_INT;
                } else if (strcmp(optarg, "long") == 0) {
                    type = TYPE_LONG;
                } else {
                    fprintf(stderr, "error: unrecognised type.\n");
                    return 1;
                }
                break;
            case 'c':
                /* value to use */
                count = strtoul(optarg, &end, 0x00);
                if (*end != '\0' || *optarg == '\0') {
                    fprintf(stderr, "error: unable to parse count.\n");
                    return 1;
                }
                break;                                    
            case -1:
                goto done;
            default:
                return 1;
        }
    }

done:
    
    /* now fork() and parent exit() */
    switch ((pid = fork())) {
        case -1:
            fprintf(stderr, "error: fork() failed.\n");
            return 1;
        case 0:
            /* child */
            break;
        default:
            fprintf(stdout, "smtest pid %u\n", pid);
            fflush(stdout);
            return 0;
    }
    
    /* schedule an alarm() to kill us if we're forgotten about */
    alarm(600);
    
    /* catch SIGUSR1 for use with pause() */    
    (void) signal(SIGUSR1, null);
    
    if ((fd = open("./smtest.dat", O_RDONLY)) == -1) {
        fprintf(stderr, "error: couldnt open %s.\n", argv[1]);
        return 1;
    }
    
    if ((region_ptr.region = mmap(NULL, MSIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0)) == MAP_FAILED) {
        fprintf(stderr, "error: failed to mmap.\n");
        return 1;
    }
    
    switch (mode) {
        case MODE_POPULATE:
            while (pause()) {
                region_ptr.region_char[i++] = value;
                if (i >= count)
                    break;
            }
            pause();
            break;
        case MODE_ITER:
            pause();
            region_ptr.region_char[0] = 0xff;
            
            while (pause()) {
                region_ptr.region_char[++i] = value;
                region_ptr.region_char[i-1] = 0x00;
                if (i > count)
                    break;
            }
            pause();
            break;
        case MODE_EXIT:
            pause();
            break;
        case MODE_UNKNOWN:
        case MODE_SEARCH:
        case MODE_INDEX:
        default:
            break;
    }
    
    return 0;
}
