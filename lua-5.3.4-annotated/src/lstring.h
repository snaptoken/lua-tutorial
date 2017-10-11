/*
** $Id: lstring.h,v 1.61 2015/11/03 15:36:01 roberto Exp $
** String table (keep all strings handled by Lua)
** See Copyright Notice in lua.h
*/

#ifndef lstring_h
#define lstring_h

#include "lgc.h"
#include "lobject.h"
#include "lstate.h"


// Size of a TString including C string data of the given length, also counting
// the nul char. Also takes alignment of the C string into account, using the
// UTString union.
#define sizelstring(l)  (sizeof(union UTString) + ((l) + 1) * sizeof(char))

// Userdata is represented the same way strings are, so we define some userdata
// related macros/functions in this module too.

// Size of a Udata object including the actual data, which is of the given
// length. Also takes alignment of the data into account, using the UUdata
// union.
#define sizeludata(l)	(sizeof(union UUdata) + (l))
// Calls the above macro passing the given Udata's length, to get the size of
// that particular Udata.
#define sizeudata(u)	sizeludata((u)->len)

// Version of luaS_newlstr() specific to string literals, so the length can be
// calculated at compile-time instead of calling strlen(). `"" s` ensures that
// `s` is a string literal by using adjacent string literal concatenation on it
// with an empty string.
#define luaS_newliteral(L, s)	(luaS_newlstr(L, "" s, \
                                 (sizeof(s)/sizeof(char))-1))


/*
** test whether a string is a reserved word
*/
// This is useful for the lexer/parser. It makes sure that strings containing
// reserved words set the `extra` field of the string to a non-zero code that
// corresponds to that reserved word. See llex.h:RESERVED for the enumeration of
// reserved words.
#define isreserved(s)	((s)->tt == LUA_TSHRSTR && (s)->extra > 0)


/*
** equality for short strings, which are always internalized
*/
// Interned strings can be compared by reference, since only one exists for each
// unique string. The check_exp() ensures that this is only used on short
// strings, i.e. interned strings.
#define eqshrstr(a,b)	check_exp((a)->tt == LUA_TSHRSTR, (a) == (b))


LUAI_FUNC unsigned int luaS_hash (const char *str, size_t l, unsigned int seed);
LUAI_FUNC unsigned int luaS_hashlongstr (TString *ts);
LUAI_FUNC int luaS_eqlngstr (TString *a, TString *b);
LUAI_FUNC void luaS_resize (lua_State *L, int newsize);
LUAI_FUNC void luaS_clearcache (global_State *g);
LUAI_FUNC void luaS_init (lua_State *L);
LUAI_FUNC void luaS_remove (lua_State *L, TString *ts);
LUAI_FUNC Udata *luaS_newudata (lua_State *L, size_t s);
LUAI_FUNC TString *luaS_newlstr (lua_State *L, const char *str, size_t l);
LUAI_FUNC TString *luaS_new (lua_State *L, const char *str);
LUAI_FUNC TString *luaS_createlngstrobj (lua_State *L, size_t l);


#endif
