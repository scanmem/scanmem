#ifndef _SCANMEM_INC
#define _SCANMEM_INC            /* include guard */

/*lint +libh(config.h) */
#include "config.h"

#include <stdio.h>
#include <stdint.h>
#include <sys/types.h>          /*lint !e537 */

#include "scanroutines.h"
#include "list.h"
#include "value.h"
#include "target_memory_info_array.h"

/* list of functions where i dont want to be warned about ignored return value */

/*lint -esym(534,detach,printversion,strftime,fflush,sleep) */

#ifndef PACKAGE_VERSION
# define  PACKAGE_VERSION "(unknown)"
#endif

/*
#ifndef NDEBUG
# define eprintf(x, y...) fprintf(stderr, x, ## y)
#else
# define eprintf(x, y...)
#endif
*/

#ifdef _lint
/*lint -save -e652 -e683 -e547 */
# define snprintf(a, b, c...) (((void) b), sprintf(a, ## c))
# define strtoll(a,b,c) ((long long) strtol(a,b,c))
# define WIFSTOPPED
# define sighandler_t _sigfunc_t
/*lint -restore */
/*lint -save -esym(526,getline,strdupa,strdup,strndupa,strtoll,pread) */
ssize_t getline(char **lineptr, size_t * n, FILE * stream);
char *strndupa(const char *s, size_t n);
char *strdupa(const char *s);
char *strdup(const char *s);
ssize_t pread(int fd, void *buf, size_t count, off_t offset);
/*lint -restore */
#endif
#ifdef __CSURF__
# define waitpid(x,y,z) ((*(y)=0),-rand())
# define WIFSTOPPED(x) (rand())
# define ptrace(w,x,y,z) ((errno=rand()),(ptrace(w,x,y,z)))
#endif
#ifndef MIN
# define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

/* global settings */
typedef struct {
    unsigned exit:1;
    pid_t target;
    matches_and_old_values_array *matches;
    long num_matches;
    double scan_progress;
    list_t *regions;
    list_t *commands;      /* command handlers */
    const char *current_cmdline; /* the command being executed */
    struct {
        unsigned short alignment;
        unsigned short debug;
        unsigned short backend; /* if 1, scanmem will work as a backend, and output would be more machine-readable */

        /* options that can be changed during runtime */
        scan_data_type_t scan_data_type;
        region_scan_level_t region_scan_level;
        unsigned short detect_reverse_change;
        unsigned short dump_with_ascii;
    } options;
} globals_t;

/* this structure represents one known match, its address and type. */
#if 0
typedef struct {
    void *address;              /* address of variable */
    region_t *region;           /* region it belongs to */
    value_t lvalue;             /* last seen value */
    unsigned matchid;           /* unique identifier */
} match_t;
#endif

/* global settings */
extern globals_t globals;

bool init();

/* ptrace.c */
bool detach(pid_t target);
bool setaddr(pid_t target, void *addr, const value_t * to);
bool checkmatches(globals_t * vars, scan_match_type_t match_type, const uservalue_t *uservalue);
bool searchregions(globals_t * vars, scan_match_type_t match_type, const uservalue_t *uservalue);
bool peekdata(pid_t pid, void *addr, value_t * result);
bool attach(pid_t target);
bool read_array(pid_t target, void *addr, char *buf, int len);
bool write_array(pid_t target, void *addr, const void *data, int len);

/* menu.c */
bool getcommand(globals_t * vars, char **line);
void printversion();

#endif
