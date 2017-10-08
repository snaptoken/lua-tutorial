/*
** $Id: lobject.c,v 2.113 2016/12/22 13:08:50 roberto Exp $
** Some generic functions over Lua objects
** See Copyright Notice in lua.h
*/

#define lobject_c
#define LUA_CORE

#include "lprefix.h"


#include <locale.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lua.h"

#include "lctype.h"
#include "ldebug.h"
#include "ldo.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"
#include "lvm.h"



// A global static Lua nil value. Sometimes used in assignment to make a TValue
// contain nil, sometimes used as a sentinel like NONVALIDVALUE in lapi.c.
LUAI_DDEF const TValue luaO_nilobject_ = {NILCONSTANT};


/*
** converts an integer to a "floating point byte", represented as
** (eeeeexxx), where the real value is (1xxx) * 2^(eeeee - 1) if
** eeeee != 0 and (xxx) otherwise.
*/
// Used in the OP_NEWTABLE instruction, to pack the array and table sizes into
// single byte arguments.
int luaO_int2fb (unsigned int x) {
  int e = 0;  /* exponent */
  if (x < 8) return x;
  // Consume the input number in blocks of 4 bits, for performance. This while
  // loop could be deleted and the function would still work.
  while (x >= (8 << 4)) {  /* coarse steps */
    x = (x + 0xf) >> 4;  /* x = ceil(x / 16) */
    e += 4;
  }
  while (x >= (8 << 1)) {  /* fine steps */
    x = (x + 1) >> 1;  /* x = ceil(x / 2) */
    e++;
  }
  return ((e+1) << 3) | (cast_int(x) - 8);
}


/* converts back */
// Obviously used to run the OP_NEWTABLE instruction. See vmcase(OP_NEWTABLE) in
// lvm.c:luaV_execute().
int luaO_fb2int (int x) {
  return (x < 8) ? x : ((x & 7) + 8) << ((x >> 3) - 1);
}


/*
** Computes ceil(log2(x))
*/
// Used to set the `lsizenode` field of a Table to the proper power of two for a
// given requested hash table size, in ltable.c:setnodevector(). Also used in
// ltable.c:countint() to count array-indexish keys of various magnitudes in a
// hash table in order to figure out an optimal cutoff for the array part and
// node part of the table.
int luaO_ceillog2 (unsigned int x) {
  static const lu_byte log_2[256] = {  /* log_2[i] = ceil(log2(i - 1)) */
    0,1,2,2,3,3,3,3,4,4,4,4,4,4,4,4,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
    6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8
  };
  int l = 0;
  x--;
  // Consume a byte at a time. When there's one byte left, use the lookup table.
  while (x >= 256) { l += 8; x >>= 8; }
  return l + log_2[x];
}


// Helper for luaO_arith() below. Performs integer arithmetic on two Lua integer
// operands.
static lua_Integer intarith (lua_State *L, int op, lua_Integer v1,
                                                   lua_Integer v2) {
  switch (op) {
    // intop() is a macro defined in lvm.h which simply does the given C
    // arithmetic operation, but casts the operands to unsigned types first, and
    // casts the result back to the signed type.
    case LUA_OPADD: return intop(+, v1, v2);
    case LUA_OPSUB:return intop(-, v1, v2);
    case LUA_OPMUL:return intop(*, v1, v2);
    // Integer modulo and division are handled specially by luaV_mod() and
    // luaV_div() which handle some special cases (like throwing an error when
    // dividing by 0). L (the lua_State) is required for throwing the error.
    case LUA_OPMOD: return luaV_mod(L, v1, v2);
    case LUA_OPIDIV: return luaV_div(L, v1, v2);
    case LUA_OPBAND: return intop(&, v1, v2);
    case LUA_OPBOR: return intop(|, v1, v2);
    case LUA_OPBXOR: return intop(^, v1, v2);
    // luaV_shiftl() just does a `<<` or `>>` operation (depending whether the
    // second operand is positive or negative), but also checks for the case
    // where you're trying to shift by more bits than there are in the integer,
    // in which case it returns 0 without doing a shift.
    case LUA_OPSHL: return luaV_shiftl(v1, v2);
    case LUA_OPSHR: return luaV_shiftl(v1, -v2);
    // Unary minus operator.
    case LUA_OPUNM: return intop(-, 0, v1);
    // Unary bitwise not operator. Is ~0 not a string of all 1's in some
    // architectures?
    case LUA_OPBNOT: return intop(^, ~l_castS2U(0), v1);
    default: lua_assert(0); return 0;
  }
}


// Helper for luaO_arith() below. Performs arithmetic on two Lua float operands.
static lua_Number numarith (lua_State *L, int op, lua_Number v1,
                                                  lua_Number v2) {
  switch (op) {
    // These are macros defined in llimits.h. Most simply just use C's operator
    // for that operation, e.g. (v1)+(v2) for luai_numadd(L, v1, v2).
    case LUA_OPADD: return luai_numadd(L, v1, v2);
    case LUA_OPSUB: return luai_numsub(L, v1, v2);
    case LUA_OPMUL: return luai_nummul(L, v1, v2);
    case LUA_OPDIV: return luai_numdiv(L, v1, v2);
    // Uses C's pow() function from <math.h>.
    case LUA_OPPOW: return luai_numpow(L, v1, v2);
    // Floor division, calculates floor(v1/v2).
    case LUA_OPIDIV: return luai_numidiv(L, v1, v2);
    // Unary minus operator.
    case LUA_OPUNM: return luai_numunm(L, v1);
    // Modulo operator. Uses fmod() from <math.h>, and makes a correction for
    // the case where one of the operands is negative.
    case LUA_OPMOD: {
      lua_Number m;
      luai_nummod(L, v1, v2, m);
      return m;
    }
    default: lua_assert(0); return 0;
  }
}


// Handles all evaluation of Lua operators for any type of Lua value(s). Does
// type coercion for numeric operators, and falls back on tag methods for
// operating on non-numeric types.
void luaO_arith (lua_State *L, int op, const TValue *p1, const TValue *p2,
                 TValue *res) {
  switch (op) {
    // Bitwise operators.
    case LUA_OPBAND: case LUA_OPBOR: case LUA_OPBXOR:
    case LUA_OPSHL: case LUA_OPSHR:
    case LUA_OPBNOT: {  /* operate only on integers */
      lua_Integer i1; lua_Integer i2;
      // Try to convert both operands to integers (strings and floats can be
      // converted).
      if (tointeger(p1, &i1) && tointeger(p2, &i2)) {
        setivalue(res, intarith(L, op, i1, i2));
        return;
      }
      else break;  /* go to the end */
    }
    // Non-integer division and the power operator only operate on floats.
    case LUA_OPDIV: case LUA_OPPOW: {  /* operate only on floats */
      lua_Number n1; lua_Number n2;
      // Try to convert both operands to floats.
      if (tonumber(p1, &n1) && tonumber(p2, &n2)) {
        setfltvalue(res, numarith(L, op, n1, n2));
        return;
      }
      else break;  /* go to the end */
    }
    // All other operators can operator on integers or floats, or a mixture of
    // the two.
    default: {  /* other operations */
      lua_Number n1; lua_Number n2;
      // If they're both integers, do integer arithmetic, nice and fast.
      if (ttisinteger(p1) && ttisinteger(p2)) {
        setivalue(res, intarith(L, op, ivalue(p1), ivalue(p2)));
        return;
      }
      // Otherwise convert them both to floats (whether they're strings or ints
      // or one of them's already a float) and return a float.
      else if (tonumber(p1, &n1) && tonumber(p2, &n2)) {
        setfltvalue(res, numarith(L, op, n1, n2));
        return;
      }
      else break;  /* go to the end */
    }
  }
  /* could not perform raw operation; try metamethod */
  // So NULL is passed for L when doing constant folding?
  lua_assert(L != NULL);  /* should not fail when folding (compile time) */
  // `op` is converted to a `TMS` (tag method index, see ltm.h) by arithmetic,
  // so operators and their corresponding tag methods have to be defined in the
  // same order in those enums. Anyways, this tries to call the tag method on
  // the first or second operand that is associated with this operator. If there
  // is no such tag method on either operand's metatable, an error is thrown.
  luaT_trybinTM(L, p1, p2, res, cast(TMS, (op - LUA_OPADD) + TM_ADD));
}


// Converts a hex digit (0-9 or a-f or A-F) to an int. Used below to convert a
// string to a number, and used a couple other places in Lua as well.
int luaO_hexavalue (int c) {
  // lisdigit() and ltolower() are defined in lctype.h. Lua can be configured to
  // use its own implementation of these, or to use the ones provided by the
  // standard library's <ctype.h>.
  if (lisdigit(c)) return c - '0';
  else return (ltolower(c) - 'a') + 10;
}


// Skips a positive or negative sign preceding a number, returning true if there
// was a negative sign. Used below in lua_strx2number() and l_str2int().
static int isneg (const char **s) {
  if (**s == '-') { (*s)++; return 1; }
  else if (**s == '+') (*s)++;
  return 0;
}



// lua_strx2number() specifically converts hexadecimal strings (which must begin
// with "0x" or "0X") to a float.
/*
** {==================================================================
** Lua's implementation for 'lua_strx2number'
** ===================================================================
*/

// In luaconf.h, lua_strx2number() is defined as an alias of the standard
// library's strtod() function. Only if we're compiling for C89 do we provide
// our own implementation, which follows the strtod() specification. I guess
// strtod() only gained hexadecimal support as of C99?
#if !defined(lua_strx2number)

/* maximum number of significant digits to read (to avoid overflows
   even with single floats) */
#define MAXSIGDIG	30

/*
** convert an hexadecimal numeric string to a number, following
** C99 specification for 'strtod'
*/
static lua_Number lua_strx2number (const char *s, char **endptr) {
  // Decimal point could be a dot or a comma depending on locale, for example.
  int dot = lua_getlocaledecpoint();
  lua_Number r = 0.0;  /* result (accumulator) */
  // Number of digits after any leading zeroes.
  int sigdig = 0;  /* number of significant digits */
  // Number of leading zeroes.
  int nosigdig = 0;  /* number of non-significant digits */
  // We're going to parse the number as if it doesn't have a decimal point, but
  // keep track of where we saw the decimal point, so at the end we can multiply
  // the number by a power of 16 to move the decimal point to the right place.
  int e = 0;  /* exponent correction */
  int neg;  /* 1 if number is negative */
  int hasdot = 0;  /* true after seen a dot */
  // We'll update this to point to the first character in the input string that
  // we haven't consumed while parsing the number. We'll set it whenever we're
  // done parsing a valid part of the number, i.e. the numeral part and then the
  // optional exponent part. This is entirely for the function caller to make
  // use of. If, when this function returns, s == *endptr, the caller knows that
  // the string couldn't be converted to a float.
  *endptr = cast(char *, s);  /* nothing is valid yet */
  while (lisspace(cast_uchar(*s))) s++;  /* skip initial spaces */
  // Increments s if it points to a positive or negative sign.
  neg = isneg(&s);  /* check signal */
  // Now there should be a "0x" or "0X".
  if (!(*s == '0' && (*(s + 1) == 'x' || *(s + 1) == 'X')))  /* check '0x' */
    return 0.0;  /* invalid format (no '0x') */
  for (s += 2; ; s++) {  /* skip '0x' and read numeral */
    // See if current char is a decimal point.
    if (*s == dot) {
      if (hasdot) break;  /* second dot? stop loop */
      else hasdot = 1;
    }
    // Otherwise we expect a digit.
    else if (lisxdigit(cast_uchar(*s))) {
      // Ignore leading zeroes, but keep a count of them.
      if (sigdig == 0 && *s == '0')  /* non-significant digit (zero)? */
        nosigdig++;
      // Otherwise it's a significant digit.
      else if (++sigdig <= MAXSIGDIG)  /* can read it without overflow? */
          r = (r * cast_num(16.0)) + luaO_hexavalue(*s);
      else e++; /* too many digits; ignore, but still count for exponent */
      // Stop counting decimal places if we're to the right of the decimal
      // point.
      if (hasdot) e--;  /* decimal digit? correct exponent */
    }
    else break;  /* neither a dot nor a digit */
  }
  // If there weren't any digits, that's an error.
  if (nosigdig + sigdig == 0)  /* no digits? */
    return 0.0;  /* invalid format */
  *endptr = cast(char *, s);  /* valid up to here */
  // We're going to be using ldexp() to multiple r by a power of two at the end.
  // e currently contains the power of 16 exponent that we want to multiply by.
  // So multiply that by 4 to convert e to a power of 2 exponent.
  e *= 4;  /* each digit multiplies/divides value by 2^4 */
  // The optional exponent part starts with a "p" for hexadecimal numbers, the
  // way a decimal number exponent starts with "e".
  if (*s == 'p' || *s == 'P') {  /* exponent part? */
    int exp1 = 0;  /* exponent value */
    // Not sure if signal is the right word, I think they mean sign.
    int neg1;  /* exponent signal */
    s++;  /* skip 'p' */
    neg1 = isneg(&s);  /* signal */
    if (!lisdigit(cast_uchar(*s)))
      return 0.0;  /* invalid; must have at least one digit */
    // Oh, the exponent part is in decimal.
    while (lisdigit(cast_uchar(*s)))  /* read exponent */
      exp1 = exp1 * 10 + *(s++) - '0';
    if (neg1) exp1 = -exp1;
    // Simply add exponent part to e. Both contain power of 2 exponents.
    e += exp1;
    // Tell the caller we consumed a valid number up to this point in the
    // string.
    *endptr = cast(char *, s);  /* valid up to here */
  }
  if (neg) r = -r;
  // Finally, multiply r (the result) by 2 to the power of e (the exponent).
  return l_mathop(ldexp)(r, e);
}

#endif
/* }====================================================== */


/* maximum length of a numeral */
// Size of buffer used by l_str2d() to retry a string-to-float conversion with
// the decimal point replaced by a locale-specific decimal point (say a comma
// instead of a dot).
#if !defined (L_MAXLENNUM)
#define L_MAXLENNUM	200
#endif

// Helper for l_str2d() below. Tries to convert the string s to a float, making
// sure there is nothing but whitespace remaining in the string after the parsed
// number. l_str2d() might call this twice, once with the original string, and
// then if that fails, once with the decimal point in the original string
// replaced by a possibly different decimal point character (like a comma),
// depending on the locale. That is why 'loc' is part of the function's name. If
// `mode` is 'x', that is a hint that the number to be parsed is a hexadecimal
// number.
static const char *l_str2dloc (const char *s, lua_Number *result, int mode) {
  char *endptr;
  // Remember, in C99 both lua_strx2number() and lua_str2number() are just
  // aliases for the standard library's strtod().
  *result = (mode == 'x') ? lua_strx2number(s, &endptr)  /* try to convert */
                          : lua_str2number(s, &endptr);
  // If endptr wasn't incremented at all, then nothing was parsed and the string
  // must not contain a valid number.
  if (endptr == s) return NULL;  /* nothing recognized? */
  while (lisspace(cast_uchar(*endptr))) endptr++;  /* skip trailing spaces */
  // Fail if there are any non-whitespace characters after the end of the
  // number.
  return (*endptr == '\0') ? endptr : NULL;  /* OK if no trailing characters */
}


// Helper for luaO_str2num() below. Converts a string to a float.
/*
** Convert string 's' to a Lua number (put in 'result'). Return NULL
** on fail or the address of the ending '\0' on success.
** 'pmode' points to (and 'mode' contains) special things in the string:
** - 'x'/'X' means an hexadecimal numeral
** - 'n'/'N' means 'inf' or 'nan' (which should be rejected)
** - '.' just optimizes the search for the common case (nothing special)
** This function accepts both the current locale or a dot as the radix
** mark. If the convertion fails, it may mean number has a dot but
** locale accepts something else. In that case, the code copies 's'
** to a buffer (because 's' is read-only), changes the dot to the
** current locale radix mark, and tries to convert again.
*/
static const char *l_str2d (const char *s, lua_Number *result) {
  const char *endptr;
  // strpbrk() is from the standard library, and returns a pointer to the first
  // character in s that is in the given set of characters. If the number is
  // hexadecimal, then it starts with "0x" or "0X" so we search for an 'x' or
  // 'X' to see if the number is hexadecimal. If the number is "NaN" or "Inf" it
  // contains an 'n' or 'N'. Most of the time, the number won't be hexadecimal
  // or "NaN" or "Inf", it'll just be a decimal number. In those common cases,
  // it would be nice to not have to search through the whole string for x's and
  // n's, so we include the '.' in the search so that the search is cut short as
  // soon as it hits a decimal point. That works because the x's and n's we are
  // looking for should come before any decimal point.
  const char *pmode = strpbrk(s, ".xXnN");
  // If none of those characters were found, strpbrk() returns NULL and so
  // `mode` will be set to 0.
  int mode = pmode ? ltolower(cast_uchar(*pmode)) : 0;
  if (mode == 'n')  /* reject 'inf' and 'nan' */
    return NULL;
  endptr = l_str2dloc(s, result, mode);  /* try to convert */
  if (endptr == NULL) {  /* failed? may be a different locale */
    // Copy the string into a buffer we can modify, and change the decimal
    // point to locale-specific one.
    char buff[L_MAXLENNUM + 1];
    const char *pdot = strchr(s, '.');
    if (strlen(s) > L_MAXLENNUM || pdot == NULL)
      return NULL;  /* string too long or no dot; fail */
    strcpy(buff, s);  /* copy string to buffer */
    // lua_getlocaledecpoint() is defined in luaconf.h, and uses the standard
    // library's localeconv() to get the locale's decimal point character.
    buff[pdot - s] = lua_getlocaledecpoint();  /* correct decimal point */
    endptr = l_str2dloc(buff, result, mode);  /* try again */
    if (endptr != NULL)
      // Success! endptr now points into our temporary buffer, so we need to
      // correct it to point into the original string s.
      endptr = s + (endptr - buff);  /* make relative to 's' */
  }
  return endptr;
}


// The maximum integer that can be safely multiplied by 10. Used below for
// detecting overflow when converting a string to an int.
#define MAXBY10		cast(lua_Unsigned, LUA_MAXINTEGER / 10)
// The least significant digit in the maximum integer. Used below for detecting
// *exactly* when overflow will occur.
#define MAXLASTD	cast_int(LUA_MAXINTEGER % 10)

// Helper for luaO_str2num() below. Converts a string to an integer.
static const char *l_str2int (const char *s, lua_Integer *result) {
  // 'a' for accumulator? The absolute value of the result will be accumulated
  // in this unsigned variable.
  lua_Unsigned a = 0;
  // This is set to 0 as soon as we parse a single digit. So if it's still 1 at
  // the end of parsing, that means there were no digits and the string is
  // invalid.
  int empty = 1;
  int neg;
  while (lisspace(cast_uchar(*s))) s++;  /* skip initial spaces */
  // Skip the positive or negative sign if it exists, and remember whether there
  // was a negative sign.
  neg = isneg(&s);
  // If it starts with "0x" or "0X", parse it as a hexadecimal number.
  if (s[0] == '0' &&
      (s[1] == 'x' || s[1] == 'X')) {  /* hex? */
    s += 2;  /* skip '0x' */
    for (; lisxdigit(cast_uchar(*s)); s++) {
      a = a * 16 + luaO_hexavalue(*s);
      empty = 0;
    }
  }
  // If it's not hexadecimal, it has to be decimal.
  else {  /* decimal */
    for (; lisdigit(cast_uchar(*s)); s++) {
      int d = *s - '0';
      // Integer overflow detection. Checks if multiplying `a` by 10 and adding
      // `d` would result in an integer larger than LUA_MAXINTEGER (which would
      // really result in overflow). Why does the hexadecimal parsing code above
      // not do any overflow checking?
      if (a >= MAXBY10 && (a > MAXBY10 || d > MAXLASTD + neg))  /* overflow? */
        return NULL;  /* do not accept it (as integer) */
      a = a * 10 + d;
      empty = 0;
    }
  }
  while (lisspace(cast_uchar(*s))) s++;  /* skip trailing spaces */
  // There should've been at least one digit, and there shouldn't be any
  // characters after the end of the number except the whitespace we just
  // skipped.
  if (empty || *s != '\0') return NULL;  /* something wrong in the numeral */
  else {
    // Success. Write the integer to `result`, making it negative if the number
    // had a negative sign.
    *result = l_castU2S((neg) ? 0u - a : a);
    return s;
  }
}


// Converts a string to a numeric TValue, representing it as an int if possible,
// otherwise a float. Returns size of string on success, 0 on failure. Used by
// the lexer (see `llex.c:read_numeral()`) and the VM for type coercion. Also
// exposed as part of the Lua API in lapi.c:lua_stringtonumber().
size_t luaO_str2num (const char *s, TValue *o) {
  lua_Integer i; lua_Number n;
  const char *e;
  if ((e = l_str2int(s, &i)) != NULL) {  /* try as an integer */
    setivalue(o, i);
  }
  else if ((e = l_str2d(s, &n)) != NULL) {  /* else try as a float */
    setfltvalue(o, n);
  }
  else
    return 0;  /* conversion failed */
  return (e - s) + 1;  /* success; return string size */
}


// Encodes a Unicode codepoint `x` as a utf-8 sequence of bytes, writing the
// bytes into `buff` and returning the number of bytes in the sequence. The
// buffer is filled from the end, so the resulting byte sequence is
// "right-aligned" in `buff`.
int luaO_utf8esc (char *buff, unsigned long x) {
  int n = 1;  /* number of bytes put in buffer (backwards) */
  lua_assert(x <= 0x10FFFF);
  if (x < 0x80)  /* ascii? */
    // ASCII characters remain unchanged in utf-8. Just store the ASCII byte at
    // the end of the `buff` and return 1.
    buff[UTF8BUFFSZ - 1] = cast(char, x);
  else {  /* need continuation bytes */
    // Otherwise we need to use utf-8's rules for encoding a codepoint in a
    // variable number of bytes. `mfb` stands for "Maximum that Fits in First
    // Byte", I guess.
    unsigned int mfb = 0x3f;  /* maximum that fits in first byte */
    do {  /* add continuation bytes */
      // Write the lowermost 6 bits of x into the lowermost 6 bits of the
      // current byte in the sequence, and make the upper two bits '10' to mark
      // this as a continuation byte.
      buff[UTF8BUFFSZ - (n++)] = cast(char, 0x80 | (x & 0x3f));
      x >>= 6;  /* remove added bits */
      mfb >>= 1;  /* now there is one less bit available in first byte */
    } while (x > mfb);  /* still needs continuation byte? */
    // Finally, we write the first byte. The first byte has the same number of
    // uppermost 1-bits as there are continuation bytes in the sequence. This
    // sequence of 1-bits is followed by a 0-bit, and then the final bits from
    // `x` that need to be written.
    buff[UTF8BUFFSZ - n] = cast(char, (~mfb << 1) | x);  /* add first byte */
  }
  // Return number of bytes in the sequence.
  return n;
}


/* maximum length of the conversion of a number to a string */
#define MAXNUMBER2STR	50


/*
** Convert a number object to a string
*/
// Used by luaO_pushvfstring() below. Also by the Lua API function
// lua_tolstring(). Note that StkId is just a TValue*, but it's understood that
// it's a pointer into a Lua stack (array of `TValue`s). This function replaces
// The numeric TValue at the given stack slot with a string TValue.
void luaO_tostring (lua_State *L, StkId obj) {
  char buff[MAXNUMBER2STR];
  size_t len;
  lua_assert(ttisnumber(obj));
  // lua_integer2str() and lua_number2str() are defined in luaconf.h to use the
  // standard library's snprintf(), when compiling for C99. In C89, snprintf()
  // doesn't support formatting numbers as hexadecimal, so lua provides its own
  // implementation in lstrlib.c:lua_number2strx().
  if (ttisinteger(obj))
    len = lua_integer2str(buff, sizeof(buff), ivalue(obj));
  else {
    len = lua_number2str(buff, sizeof(buff), fltvalue(obj));
    // Show floats that contain integer values as '123.0' instead of '123',
    // unless LUA_COMPAT_FLOATSTRING is defined. Lua used to use floats to
    // represent all numbers, and so omitting the '.0' for integers was
    // desirable.
#if !defined(LUA_COMPAT_FLOATSTRING)
    // strspn() from <string.h> returns the index of the first character in buff
    // that isn't in the given charset. If that character is the nul character,
    // that means that every character is a digit or minus sign, which means it
    // looks like an int. In that case, add '.0' to show that it's really a
    // float.
    if (buff[strspn(buff, "-0123456789")] == '\0') {  /* looks like an int? */
      buff[len++] = lua_getlocaledecpoint();
      buff[len++] = '0';  /* adds '.0' to result */
    }
#endif
  }
  // luaS_newlstr() creates a Lua string out of a given C string and length.
  // setsvalue2s() is just setsvalue(), but self-documents that it's setting a
  // value on the stack ('2s' stands for 'to stack').
  setsvalue2s(L, obj, luaS_newlstr(L, buff, len));
}


// Helper for luaO_pushvfstring() below. Converts the given C string to a Lua
// TValue string and pushes it onto the Lua stack.
static void pushstr (lua_State *L, const char *str, size_t l) {
  setsvalue2s(L, L->top, luaS_newlstr(L, str, l));
  luaD_inctop(L);
}


/*
** this function handles only '%d', '%c', '%f', '%p', and '%s'
   conventional formats, plus Lua-specific '%I' and '%U'
*/
const char *luaO_pushvfstring (lua_State *L, const char *fmt, va_list argp) {
  int n = 0;
  for (;;) {
    const char *e = strchr(fmt, '%');
    if (e == NULL) break;
    pushstr(L, fmt, e - fmt);
    switch (*(e+1)) {
      case 's': {  /* zero-terminated string */
        const char *s = va_arg(argp, char *);
        if (s == NULL) s = "(null)";
        pushstr(L, s, strlen(s));
        break;
      }
      case 'c': {  /* an 'int' as a character */
        char buff = cast(char, va_arg(argp, int));
        if (lisprint(cast_uchar(buff)))
          pushstr(L, &buff, 1);
        else  /* non-printable character; print its code */
          luaO_pushfstring(L, "<\\%d>", cast_uchar(buff));
        break;
      }
      case 'd': {  /* an 'int' */
        setivalue(L->top, va_arg(argp, int));
        goto top2str;
      }
      case 'I': {  /* a 'lua_Integer' */
        setivalue(L->top, cast(lua_Integer, va_arg(argp, l_uacInt)));
        goto top2str;
      }
      case 'f': {  /* a 'lua_Number' */
        setfltvalue(L->top, cast_num(va_arg(argp, l_uacNumber)));
      top2str:  /* convert the top element to a string */
        luaD_inctop(L);
        luaO_tostring(L, L->top - 1);
        break;
      }
      case 'p': {  /* a pointer */
        char buff[4*sizeof(void *) + 8]; /* should be enough space for a '%p' */
        int l = l_sprintf(buff, sizeof(buff), "%p", va_arg(argp, void *));
        pushstr(L, buff, l);
        break;
      }
      case 'U': {  /* an 'int' as a UTF-8 sequence */
        char buff[UTF8BUFFSZ];
        int l = luaO_utf8esc(buff, cast(long, va_arg(argp, long)));
        pushstr(L, buff + UTF8BUFFSZ - l, l);
        break;
      }
      case '%': {
        pushstr(L, "%", 1);
        break;
      }
      default: {
        luaG_runerror(L, "invalid option '%%%c' to 'lua_pushfstring'",
                         *(e + 1));
      }
    }
    n += 2;
    fmt = e+2;
  }
  luaD_checkstack(L, 1);
  pushstr(L, fmt, strlen(fmt));
  if (n > 0) luaV_concat(L, n + 1);
  return svalue(L->top - 1);
}


const char *luaO_pushfstring (lua_State *L, const char *fmt, ...) {
  const char *msg;
  va_list argp;
  va_start(argp, fmt);
  msg = luaO_pushvfstring(L, fmt, argp);
  va_end(argp);
  return msg;
}


/* number of chars of a literal string without the ending \0 */
#define LL(x)	(sizeof(x)/sizeof(char) - 1)

#define RETS	"..."
#define PRE	"[string \""
#define POS	"\"]"

#define addstr(a,b,l)	( memcpy(a,b,(l) * sizeof(char)), a += (l) )

void luaO_chunkid (char *out, const char *source, size_t bufflen) {
  size_t l = strlen(source);
  if (*source == '=') {  /* 'literal' source */
    if (l <= bufflen)  /* small enough? */
      memcpy(out, source + 1, l * sizeof(char));
    else {  /* truncate it */
      addstr(out, source + 1, bufflen - 1);
      *out = '\0';
    }
  }
  else if (*source == '@') {  /* file name */
    if (l <= bufflen)  /* small enough? */
      memcpy(out, source + 1, l * sizeof(char));
    else {  /* add '...' before rest of name */
      addstr(out, RETS, LL(RETS));
      bufflen -= LL(RETS);
      memcpy(out, source + 1 + l - bufflen, bufflen * sizeof(char));
    }
  }
  else {  /* string; format as [string "source"] */
    const char *nl = strchr(source, '\n');  /* find first new line (if any) */
    addstr(out, PRE, LL(PRE));  /* add prefix */
    bufflen -= LL(PRE RETS POS) + 1;  /* save space for prefix+suffix+'\0' */
    if (l < bufflen && nl == NULL) {  /* small one-line source? */
      addstr(out, source, l);  /* keep it */
    }
    else {
      if (nl != NULL) l = nl - source;  /* stop at first newline */
      if (l > bufflen) l = bufflen;
      addstr(out, source, l);
      addstr(out, RETS, LL(RETS));
    }
    memcpy(out, POS, (LL(POS) + 1) * sizeof(char));
  }
}

