/*

 $Id: value.h,v 1.4 2007-04-11 10:43:27+01 taviso Exp $

*/

#ifndef _VALUE_INC
#define _VALUE_INC

#include <stdint.h>

/* some routines for working with value_t structures */

typedef enum {
    MATCHEXACT,
    MATCHNOTEQUAL,
    MATCHEQUAL,
    MATCHGREATERTHAN,
    MATCHLESSTHAN,
    MATCHANY
} matchtype_t;

/* This union trick only works on little-endian systems. It no longer needs to be size-efficient, so it could be made into a struct, but it is not the only thing that would need to be worked out to make scanmem work without on a different-endianness system. */
typedef union {
    uint8_t  tu8b ;
    int8_t   ts8b ;
    uint16_t tu16b;
    int16_t  ts16b;
    uint32_t tu32b;
    int32_t  ts32b;
    uint64_t tu64b;
    int64_t  ts64b;
                                /* const */ float tf32b;
                                /* READ ONLY */
                                /* const */ double tf64b;
                                /* READ ONLY */

    /* A few places in the code need to rely on the implementation type specifics. These are for those places. */
    unsigned char  tucharsize;
      signed char  tscharsize;
    unsigned short tushortsize;
      signed short tsshortsize;
    unsigned int   tuintsize;
      signed int   tsintsize;
    unsigned long  tulongsize;
      signed long  tslongsize;
    unsigned long long tulonglongsize;
      signed long long tslonglongsize;
} types_t;

typedef struct __attribute__ ((packed)) {
	unsigned  u8b:1;        /* could be an unsigned  8-bit variable (e.g. unsigned char)      */
	unsigned u16b:1;        /* could be an unsigned 16-bit variable (e.g. unsigned short)     */
	unsigned u32b:1;        /* could be an unsigned 32-bit variable (e.g. unsigned int)       */
	unsigned u64b:1;        /* could be an unsigned 64-bit variable (e.g. unsigned long long) */
	unsigned  s8b:1;        /* could be a    signed  8-bit variable (e.g. signed char)        */
	unsigned s16b:1;        /* could be a    signed 16-bit variable (e.g. short)              */
	unsigned s32b:1;        /* could be a    signed 32-bit variable (e.g. int)                */
	unsigned s64b:1;        /* could be a    signed 64-bit variable (e.g. long long)          */
	unsigned f32b:1;        /* could be a 32-bit floating point variable (i.e. float)         */
	unsigned f64b:1;        /* could be a 64-bit floating point variable (i.e. double)        */
	
	unsigned ineq_forwards:1; /* Whether this value has matched inequalities used the normal way */
	unsigned ineq_reverse:1; /* Whether this value has matched inequalities in reverse */
} match_flags;

typedef struct {
    types_t value;
    match_flags flags;
} value_t;

bool valtostr(const value_t * val, char *str, size_t n);
void strtoval(const char *nptr, char **endptr, int base, value_t * val);
void valcpy(value_t * dst, const value_t * src);
void truncval_to_flags(value_t * dst, match_flags flags);
void truncval(value_t * dst, const value_t * src);
bool valuecmp(const value_t * v1, matchtype_t operator, const value_t * v2,
              value_t * save);
void valnowidth(value_t * val);
int flags_to_max_width_in_bytes(match_flags flags);
int val_max_width_in_bytes(value_t *val);
unsigned char val_byte(value_t *val, int which);

#endif
