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
    int64_t int_value;
    double double_value;
    float float_value;
    
    enum { UNDEFINED, BY_POINTER_SHIFTING, BY_BIT_MATH } how_to_calculate_values;  /* This is the most confusing field in the value_t struct.
      You see, some value_t structs represent data gathered from the target process's memory, so their values
      for different variables sizes are calculated by pointer shifting, but others are given as strings
      by the user, so their different values must be calculated by bit math, to keep them consistent. This is the same thing on
      little-endian systems, but not on big-endian systems. XXX XXX - provide an example of the difference */
    
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

#define DECLARE_GET_SET_FUNCTIONS(bits) \
    int##bits##_t get_s##bits##b(value_t const* val); \
    void set_s##bits##b(value_t *val, int##bits##_t data); \
    uint##bits##_t get_u##bits##b(value_t const* val); \
    void set_u##bits##b(value_t *val, uint##bits##_t data)

DECLARE_GET_SET_FUNCTIONS(8);
DECLARE_GET_SET_FUNCTIONS(16);
DECLARE_GET_SET_FUNCTIONS(32);
DECLARE_GET_SET_FUNCTIONS(64);

#define DECLARE_GET_BY_SYSTEM_DEPENDENT_TYPE_FUNCTIONS(type, typename) \
unsigned type get_u##typename (value_t const* val); \
  signed type get_s##typename (value_t const* val)

DECLARE_GET_BY_SYSTEM_DEPENDENT_TYPE_FUNCTIONS(char, char);
DECLARE_GET_BY_SYSTEM_DEPENDENT_TYPE_FUNCTIONS(short, short);
DECLARE_GET_BY_SYSTEM_DEPENDENT_TYPE_FUNCTIONS(int, int);
DECLARE_GET_BY_SYSTEM_DEPENDENT_TYPE_FUNCTIONS(long, long);
DECLARE_GET_BY_SYSTEM_DEPENDENT_TYPE_FUNCTIONS(long long, longlong);

#endif
