/*

 $Id: value.h,v 1.4 2007-04-11 10:43:27+01 taviso Exp $

*/

#ifndef _VALUE_INC
#define _VALUE_INC

/* some routines for working with value_t structures */

typedef enum {
    MATCHEXACT,
    MATCHNOTEQUAL,
    MATCHEQUAL,
    MATCHGREATERTHAN,
    MATCHLESSTHAN
} matchtype_t;

typedef union {
    unsigned char tuchar;
    signed char tschar;
    unsigned short tushort;
    signed short tsshort;
    unsigned int tuint;
    signed int tsint;
    unsigned long tulong;
    signed long tslong;
                                /* const */ float tfloat;
                                /* READ ONLY */
} types_t;

typedef struct {
    types_t value;
    struct __attribute__ ((packed)) {
        unsigned wchar:1;       /* c,C - could be a char */
        unsigned wshort:1;      /* s,S - could be a short */
        unsigned wint:1;        /* i,I - could be a int */
        unsigned wlong:1;       /* l,L - could be a long */
        unsigned tfloat:1;      /* f,F - could be a float */
        unsigned sign:1;        /* n,N - is known to be signed (eg, < 0) */
        unsigned zero:1;        /* z,Z - is zero or one based value */
    } flags;
} value_t;

bool valtostr(const value_t * val, char *str, size_t n);
void strtoval(const char *nptr, char **endptr, int base, value_t * val);
void valcpy(value_t * dst, const value_t * src);
void truncval(value_t * dst, const value_t * src);
bool valuecmp(const value_t * v1, matchtype_t operator, const value_t * v2,
              value_t * save);
void valnowidth(value_t * val);

#define valuegt(a,b,x,y) (((a)->flags.x && (b)->flags.x) && ((a)->value.y > (b)->value.y))
#define valuelt(a,b,x,y) (((a)->flags.x && (b)->flags.x) && ((a)->value.y < (b)->value.y))
#define valueequal(a,b,x,y) (((a)->flags.x && (b)->flags.x) && ((a)->value.y == (b)->value.y))
#define valuecopy(a,b,x,y) (((a)->value.y = (b)->value.y), ((a)->flags.x = 1))

#endif
