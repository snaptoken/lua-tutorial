/*
** $Id: lua.h,v 1.332 2016/12/22 15:51:20 roberto Exp $
** Lua - A Scripting Language
** Lua.org, PUC-Rio, Brazil (http://www.lua.org)
** See Copyright Notice at the end of this file
*/

// This header defines the Lua C API. It's what C programs interfacing with Lua
// will include. They might also include lauxlib.h which provides higher-level
// functions that makes interfacing with Lua much easier. But lauxlib implements
// its API using only the Lua C API defined here, it's built on top of this.
// This is the basic low-level interface to Lua from C.

#ifndef lua_h
#define lua_h

#include <stdarg.h>
#include <stddef.h>


#include "luaconf.h"


// Lua's current version number. This is Lua 5.3.4.
#define LUA_VERSION_MAJOR	"5"
#define LUA_VERSION_MINOR	"3"
// The major and minor version numbers represented as an int, which can be used
// in comparison expressions.
#define LUA_VERSION_NUM		503
#define LUA_VERSION_RELEASE	"4"

// Version/copyright strings printed out by `lua -v` or `luac -v`.
#define LUA_VERSION	"Lua " LUA_VERSION_MAJOR "." LUA_VERSION_MINOR
#define LUA_RELEASE	LUA_VERSION "." LUA_VERSION_RELEASE
#define LUA_COPYRIGHT	LUA_RELEASE "  Copyright (C) 1994-2017 Lua.org, PUC-Rio"
#define LUA_AUTHORS	"R. Ierusalimschy, L. H. de Figueiredo, W. Celes"


/* mark for precompiled code ('<esc>Lua') */
// These four bytes appear at the beginning of files containing Lua bytecode, to
// identify them as such.
#define LUA_SIGNATURE	"\x1bLua"

/* option for multiple returns in 'lua_pcall' and 'lua_call' */
// Can be passed as the "number of return values expected" from a function call,
// instead of specifying an exact number. E.g. with the expression `a, b = f()`
// exactly 2 return values are expected from `f()`, but with the expression
// `return f()` it just takes all of the return values that come and returns
// that many values itself. That's LUA_MULTRET.
#define LUA_MULTRET	(-1)


/*
** Pseudo-indices
** (-LUAI_MAXSTACK is the minimum valid index; we keep some free empty
** space after that to help overflow detection)
*/
// LUAI_MAXSTACK is defined to be 1_000_000 on most machines, so these are very
// low negative numbers. They can be used in place of a stack index in many Lua
// API function calls to refer to either the Lua registry, or a specific
// upvalue.
#define LUA_REGISTRYINDEX	(-LUAI_MAXSTACK - 1000)
#define lua_upvalueindex(i)	(LUA_REGISTRYINDEX - (i))


/* thread status */
// States that Lua threads (coroutines) can be in.
// LUA_OK means the thread is currently running.
#define LUA_OK		0
// LUA_YIELD means the thread has yielded and is not running.
#define LUA_YIELD	1
// The rest are error states. This is a runtime error.
#define LUA_ERRRUN	2
// Syntax error.
#define LUA_ERRSYNTAX	3
// Out of memory error.
#define LUA_ERRMEM	4
// Error occurred while running __gc metamethod (finalizer).
#define LUA_ERRGCMM	5
// Error occurred while handling another error. Can happen as the result of a C
// or Lua stack overflow.
#define LUA_ERRERR	6


typedef struct lua_State lua_State;


/*
** basic types
*/
// Used as a return value for lua_type().
#define LUA_TNONE		(-1)

// These basic type tags are stored in the bottom 4 bits of a Lua object's type
// tag field.
#define LUA_TNIL		0
#define LUA_TBOOLEAN		1
// A C pointer. Not garbage-collected.
#define LUA_TLIGHTUSERDATA	2
// A float or an int.
#define LUA_TNUMBER		3
// A long string or a short (interned) string.
#define LUA_TSTRING		4
#define LUA_TTABLE		5
// A Lua or C closure, or a light C function. Closures are garbage collected,
// light C functions are just basic function pointers and not garbage collected.
#define LUA_TFUNCTION		6
// Garbage collected userdata, usually used to store a C struct you want Lua
// code to be able to access.
#define LUA_TUSERDATA		7
// A coroutine. A lua_State.
#define LUA_TTHREAD		8

// Number of tags defined above.
#define LUA_NUMTAGS		9



/* minimum Lua stack available to a C function */
// Number of slots guaranteed to be available on top of the stack when Lua calls
// into a C function.
#define LUA_MINSTACK	20


/* predefined values in the registry */
// The registry is a Lua table stored in a Lua global state. At the integer
// index 1, it stores a reference to the main thread (lua_State). At the integer
// index 2, it stores the table of global variables.
#define LUA_RIDX_MAINTHREAD	1
#define LUA_RIDX_GLOBALS	2
// Number of key-value pairs in the registry table. Used by
// lstate.c:init_registry() to set the size of the registry table.
#define LUA_RIDX_LAST		LUA_RIDX_GLOBALS

// The following types are ultimately defined in luaconf.h, based on what it can
// glean about the architecture it's being compiled on.

/* type of numbers in Lua */
// Usually `double`.
typedef LUA_NUMBER lua_Number;


/* type for integer functions */
// Usually `long long` (64-bit int).
typedef LUA_INTEGER lua_Integer;

/* unsigned integer type */
// Unsigned version of LUA_INTEGER (so usually `unsigned long long`).
typedef LUA_UNSIGNED lua_Unsigned;

/* type for continuation-function contexts */
// `intptr_t` if available, otherwise `ptrdiff_t`. Continuation contexts are
// numeric values that get passed to a continuation function when calling it.
// What is the context value used for? (It's hard to find an example in the Lua
// code itself. Maybe when I get to ldo.c and coroutines.)
typedef LUA_KCONTEXT lua_KContext;


/*
** Type for C functions registered with Lua
*/
// Lua C functions take a lua_State* and return an int (the number of values
// it's returning that are on top of the stack).
typedef int (*lua_CFunction) (lua_State *L);

/*
** Type for continuation functions
*/
// Continuation functions are called when resuming a Lua thread (see
// `ldo.c:resume()`) and when "finishing a C call" (see `ldo.c:finishCcall()`).
// The `status` is a thread status. `LUA_YIELD` seems to be what's usually
// passed. What is the `status` telling the continuation function exactly?
typedef int (*lua_KFunction) (lua_State *L, int status, lua_KContext ctx);


/*
** Type for functions that read/write blocks when loading/dumping Lua chunks
*/
// A Zio (from lzio.h) wraps one of these lua_Reader functions to provide all of
// Lua's buffered stream input needs. A lua_Reader function is expected to read
// some bytes, return a pointer to the bytes read, and set `*sz` to the number
// of bytes read.
typedef const char * (*lua_Reader) (lua_State *L, void *ud, size_t *sz);

// A writer function, similar to lua_Reader (but writes!).
typedef int (*lua_Writer) (lua_State *L, const void *p, size_t sz, void *ud);


/*
** Type for memory-allocation functions
*/
// Used by lmem.c:luaM_realloc_(). An example is l_alloc() in lauxlib.c.
typedef void * (*lua_Alloc) (void *ud, void *ptr, size_t osize, size_t nsize);



/*
** generic extra include file
*/
// Allows a user of the Lua library to insert their own declarations/whatever
// here. Since lua.h is included by all other Lua files, anything defined here
// will be visible to all Lua files. What do people actually use this for? What
// are the possibilities? Here's one example:
// http://lua-users.org/lists/lua-l/2002-05/msg00012.html
// But there's no longer a LUA_USERSTATE in the lua_State struct, so that
// particular example no longer applies. (I guess they replaced LUA_USERSTATE
// with the LUA_EXTRASPACE and the `LX` struct in `lstate.c`.
#if defined(LUA_USER_H)
#include LUA_USER_H
#endif


/*
** RCS ident string
*/
// RCS is a version control system. See the definition of lua_ident in lapi.c.
// What is this useful for?
extern const char lua_ident[];


/*
** state manipulation
*/
LUA_API lua_State *(lua_newstate) (lua_Alloc f, void *ud);
LUA_API void       (lua_close) (lua_State *L);
LUA_API lua_State *(lua_newthread) (lua_State *L);

LUA_API lua_CFunction (lua_atpanic) (lua_State *L, lua_CFunction panicf);


LUA_API const lua_Number *(lua_version) (lua_State *L);


/*
** basic stack manipulation
*/
LUA_API int   (lua_absindex) (lua_State *L, int idx);
LUA_API int   (lua_gettop) (lua_State *L);
LUA_API void  (lua_settop) (lua_State *L, int idx);
LUA_API void  (lua_pushvalue) (lua_State *L, int idx);
LUA_API void  (lua_rotate) (lua_State *L, int idx, int n);
LUA_API void  (lua_copy) (lua_State *L, int fromidx, int toidx);
LUA_API int   (lua_checkstack) (lua_State *L, int n);

LUA_API void  (lua_xmove) (lua_State *from, lua_State *to, int n);


/*
** access functions (stack -> C)
*/

LUA_API int             (lua_isnumber) (lua_State *L, int idx);
LUA_API int             (lua_isstring) (lua_State *L, int idx);
LUA_API int             (lua_iscfunction) (lua_State *L, int idx);
LUA_API int             (lua_isinteger) (lua_State *L, int idx);
LUA_API int             (lua_isuserdata) (lua_State *L, int idx);
LUA_API int             (lua_type) (lua_State *L, int idx);
LUA_API const char     *(lua_typename) (lua_State *L, int tp);

LUA_API lua_Number      (lua_tonumberx) (lua_State *L, int idx, int *isnum);
LUA_API lua_Integer     (lua_tointegerx) (lua_State *L, int idx, int *isnum);
LUA_API int             (lua_toboolean) (lua_State *L, int idx);
LUA_API const char     *(lua_tolstring) (lua_State *L, int idx, size_t *len);
LUA_API size_t          (lua_rawlen) (lua_State *L, int idx);
LUA_API lua_CFunction   (lua_tocfunction) (lua_State *L, int idx);
LUA_API void	       *(lua_touserdata) (lua_State *L, int idx);
LUA_API lua_State      *(lua_tothread) (lua_State *L, int idx);
LUA_API const void     *(lua_topointer) (lua_State *L, int idx);


/*
** Comparison and arithmetic functions
*/

// Enum of Lua operators, kept in the same order as the tag method enum (TMS in
// ltm.h). (Tag methods are metamethods that override certain Lua operators.)
// These values are passed to the lua_arith() function below.
#define LUA_OPADD	0	/* ORDER TM, ORDER OP */
#define LUA_OPSUB	1
#define LUA_OPMUL	2
#define LUA_OPMOD	3
#define LUA_OPPOW	4
#define LUA_OPDIV	5
#define LUA_OPIDIV	6
#define LUA_OPBAND	7
#define LUA_OPBOR	8
#define LUA_OPBXOR	9
#define LUA_OPSHL	10
#define LUA_OPSHR	11
#define LUA_OPUNM	12
#define LUA_OPBNOT	13

LUA_API void  (lua_arith) (lua_State *L, int op);

// Comparison operators, used only for passing to lua_compare().
#define LUA_OPEQ	0
#define LUA_OPLT	1
#define LUA_OPLE	2

LUA_API int   (lua_rawequal) (lua_State *L, int idx1, int idx2);
LUA_API int   (lua_compare) (lua_State *L, int idx1, int idx2, int op);


/*
** push functions (C -> stack)
*/
LUA_API void        (lua_pushnil) (lua_State *L);
LUA_API void        (lua_pushnumber) (lua_State *L, lua_Number n);
LUA_API void        (lua_pushinteger) (lua_State *L, lua_Integer n);
LUA_API const char *(lua_pushlstring) (lua_State *L, const char *s, size_t len);
LUA_API const char *(lua_pushstring) (lua_State *L, const char *s);
LUA_API const char *(lua_pushvfstring) (lua_State *L, const char *fmt,
                                                      va_list argp);
LUA_API const char *(lua_pushfstring) (lua_State *L, const char *fmt, ...);
LUA_API void  (lua_pushcclosure) (lua_State *L, lua_CFunction fn, int n);
LUA_API void  (lua_pushboolean) (lua_State *L, int b);
LUA_API void  (lua_pushlightuserdata) (lua_State *L, void *p);
LUA_API int   (lua_pushthread) (lua_State *L);


/*
** get functions (Lua -> stack)
*/
LUA_API int (lua_getglobal) (lua_State *L, const char *name);
LUA_API int (lua_gettable) (lua_State *L, int idx);
LUA_API int (lua_getfield) (lua_State *L, int idx, const char *k);
LUA_API int (lua_geti) (lua_State *L, int idx, lua_Integer n);
LUA_API int (lua_rawget) (lua_State *L, int idx);
LUA_API int (lua_rawgeti) (lua_State *L, int idx, lua_Integer n);
LUA_API int (lua_rawgetp) (lua_State *L, int idx, const void *p);

LUA_API void  (lua_createtable) (lua_State *L, int narr, int nrec);
LUA_API void *(lua_newuserdata) (lua_State *L, size_t sz);
LUA_API int   (lua_getmetatable) (lua_State *L, int objindex);
LUA_API int  (lua_getuservalue) (lua_State *L, int idx);


/*
** set functions (stack -> Lua)
*/
LUA_API void  (lua_setglobal) (lua_State *L, const char *name);
LUA_API void  (lua_settable) (lua_State *L, int idx);
LUA_API void  (lua_setfield) (lua_State *L, int idx, const char *k);
LUA_API void  (lua_seti) (lua_State *L, int idx, lua_Integer n);
LUA_API void  (lua_rawset) (lua_State *L, int idx);
LUA_API void  (lua_rawseti) (lua_State *L, int idx, lua_Integer n);
LUA_API void  (lua_rawsetp) (lua_State *L, int idx, const void *p);
LUA_API int   (lua_setmetatable) (lua_State *L, int objindex);
LUA_API void  (lua_setuservalue) (lua_State *L, int idx);


/*
** 'load' and 'call' functions (load and run Lua code)
*/
LUA_API void  (lua_callk) (lua_State *L, int nargs, int nresults,
                           lua_KContext ctx, lua_KFunction k);
// Making a call without a continuation is much more common.
#define lua_call(L,n,r)		lua_callk(L, (n), (r), 0, NULL)

LUA_API int   (lua_pcallk) (lua_State *L, int nargs, int nresults, int errfunc,
                            lua_KContext ctx, lua_KFunction k);
// Same with protected calls.
#define lua_pcall(L,n,r,f)	lua_pcallk(L, (n), (r), (f), 0, NULL)

LUA_API int   (lua_load) (lua_State *L, lua_Reader reader, void *dt,
                          const char *chunkname, const char *mode);

LUA_API int (lua_dump) (lua_State *L, lua_Writer writer, void *data, int strip);


/*
** coroutine functions
*/
LUA_API int  (lua_yieldk)     (lua_State *L, int nresults, lua_KContext ctx,
                               lua_KFunction k);
LUA_API int  (lua_resume)     (lua_State *L, lua_State *from, int narg);
LUA_API int  (lua_status)     (lua_State *L);
LUA_API int (lua_isyieldable) (lua_State *L);

// Yield without a continuation.
#define lua_yield(L,n)		lua_yieldk(L, (n), 0, NULL)


/*
** garbage-collection function and options
*/
// Commands for controlling the garbage collector. See lua_gc() in lapi.c for
// what each command does. The command is passed as the `what` argument to
// lua_gc().
#define LUA_GCSTOP		0
#define LUA_GCRESTART		1
#define LUA_GCCOLLECT		2
#define LUA_GCCOUNT		3
#define LUA_GCCOUNTB		4
#define LUA_GCSTEP		5
#define LUA_GCSETPAUSE		6
#define LUA_GCSETSTEPMUL	7
#define LUA_GCISRUNNING		9

LUA_API int (lua_gc) (lua_State *L, int what, int data);


/*
** miscellaneous functions
*/

LUA_API int   (lua_error) (lua_State *L);

LUA_API int   (lua_next) (lua_State *L, int idx);

LUA_API void  (lua_concat) (lua_State *L, int n);
LUA_API void  (lua_len)    (lua_State *L, int idx);

LUA_API size_t   (lua_stringtonumber) (lua_State *L, const char *s);

LUA_API lua_Alloc (lua_getallocf) (lua_State *L, void **ud);
LUA_API void      (lua_setallocf) (lua_State *L, lua_Alloc f, void *ud);



/*
** {==============================================================
** some useful macros
** ===============================================================
*/

// Get a pointer to the extra space associated with a lua_State. The amount of
// extra space is defined in luaconf.h. I think the extra space is intended to
// be used for implementing concurrent threads? The extra space is located right
// above the lua_State struct in memory (see `LX` in lstate.c). By default, the
// extra space is the size of a pointer, so you can associate each lua_State
// with a pointer to some other struct.
#define lua_getextraspace(L)	((void *)((char *)(L) - LUA_EXTRASPACE))

// Convert a lua value to a float. If the value can't be converted to a number,
// it just returns 0. Usually used when you're sure the conversion won't fail.
// (The third argument to lua_tonumberx() is a pointer that gets set to 1 if the
// conversion succeeded, and 0 if it failed.)
#define lua_tonumber(L,i)	lua_tonumberx(L,(i),NULL)
// Same, but converts to an integer instead of a float.
#define lua_tointeger(L,i)	lua_tointegerx(L,(i),NULL)

// Removes the top `n` elements from the stack, by setting L->top to the (n+1)th
// element from the top of the stack.
#define lua_pop(L,n)		lua_settop(L, -(n)-1)

// Create a new empty table on the top of the stack, using the default array
// size and hash size.
#define lua_newtable(L)		lua_createtable(L, 0, 0)

// Create a light C function and assign it to a Lua global variable, by pushing
// the C function to the stack as a Lua value, and then calling lua_setglobal().
// `n` is the global variable name (a C string), and `f` is the C function
// pointer.
#define lua_register(L,n,f) (lua_pushcfunction(L, (f)), lua_setglobal(L, (n)))

// Create a light C function on top of the stack, by calling lua_pushcclosure()
// with 0 upvalues (which will not create a C closure, but a light C function).
#define lua_pushcfunction(L,f)	lua_pushcclosure(L, (f), 0)

// Type testing functions, making use of lua_type().
#define lua_isfunction(L,n)	(lua_type(L, (n)) == LUA_TFUNCTION)
#define lua_istable(L,n)	(lua_type(L, (n)) == LUA_TTABLE)
#define lua_islightuserdata(L,n)	(lua_type(L, (n)) == LUA_TLIGHTUSERDATA)
#define lua_isnil(L,n)		(lua_type(L, (n)) == LUA_TNIL)
#define lua_isboolean(L,n)	(lua_type(L, (n)) == LUA_TBOOLEAN)
#define lua_isthread(L,n)	(lua_type(L, (n)) == LUA_TTHREAD)
// lua_type() returns LUA_TNONE if the given stack index doesn't point to a
// valid slot in the stack.
#define lua_isnone(L,n)		(lua_type(L, (n)) == LUA_TNONE)
// This works because LUA_TNIL is 0 and LUA_TNONE is -1, and the rest of the
// type tags are >= 1.
#define lua_isnoneornil(L, n)	(lua_type(L, (n)) <= 0)

// Version of lua_pushstring() that only works for string literals. Why is this
// useful? Maybe at one time in the past it was like lstring.h:luaS_newliteral()
// in that it calculated the string length at compile time, avoiding a call to
// strlen()?
#define lua_pushliteral(L, s)	lua_pushstring(L, "" s)

// Pushes the table of global variables onto the stack. This table is contained
// in the registry, with the integer key LUA_RIDX_GLOBALS. The registry is
// itself a table stored in the global_State and can be accessed through the API
// using LUA_REGISTRYINDEX as the stack index. So we use lua_rawgeti() to look
// up the globals table within the registry table. "raw" because we're skipping
// tag methods, and "i" because we're using a C integer as the key instead of a
// Lua value.
#define lua_pushglobaltable(L)  \
	((void)lua_rawgeti(L, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS))

// Converts the value at stack index `i` to a Lua string (replacing the original
// value on the stack). Returns a pointer to the C string data of the new
// string, or NULL if the object couldn't be converted to a string. Use
// `lua_tolstring()` if you want to get the length of the string that was
// written.
#define lua_tostring(L,i)	lua_tolstring(L, (i), NULL)


// Move the value that's on top of the stack to the given index, bumping
// everything everything above that position up one. This is implemented by
// rotating that slice of the stack to the right once.
#define lua_insert(L,idx)	lua_rotate(L, (idx), 1)

// Remove the value at the specified stack index, moving everything above it
// down one. This is implemented by rotating that slice of the stack to the left
// once, so that the value to be removed ends up on top of the stack, and then
// popping that value off the stack.
#define lua_remove(L,idx)	(lua_rotate(L, (idx), -1), lua_pop(L, 1))

// Replaces the value at the given stack index with the value at the top of the
// stack, and removes the value from the top of the stack after it's been
// copied.
#define lua_replace(L,idx)	(lua_copy(L, -1, (idx)), lua_pop(L, 1))

/* }============================================================== */


/*
** {==============================================================
** compatibility macros for unsigned conversions
** ===============================================================
*/
// See luaconf.h for description of LUA_COMPAT_APIINTCASTS.
#if defined(LUA_COMPAT_APIINTCASTS)

// These functions to work with non-lua_Integer integer types were available in
// Lua 5.1 and 5.2 but were deprecated in 5.3. Provide them only if requested by
// defining LUA_COMPAT_APIINTCASTS. (They just call the integer version of the
// functions, with the argument or return value casted.) What were these used
// for and why were they deprecated?
#define lua_pushunsigned(L,n)	lua_pushinteger(L, (lua_Integer)(n))
#define lua_tounsignedx(L,i,is)	((lua_Unsigned)lua_tointegerx(L,i,is))
#define lua_tounsigned(L,i)	lua_tounsignedx(L,(i),NULL)

#endif
/* }============================================================== */

/*
** {======================================================================
** Debug API
** =======================================================================
*/


/*
** Event codes
*/
// Looks like these are used internally (passed to ldo.c:luaD_hook() for
// example), externally they are only used to define the mask bits below.
//
// These are the different events that you can hook into for debugging purposes.
// You use lua_sethook() to specify a hook function (see the lua_Hook typedef
// below) that will be called whenever one of the specified events occurs (you
// can specify multiple events by bitwise-ORing their mask bits together).

// Function call hook.
#define LUA_HOOKCALL	0
// Return from function hook.
#define LUA_HOOKRET	1
// Hooks into each line (or statement?) of code executed.
#define LUA_HOOKLINE	2
// Hook is called every n instructions that are executed.
#define LUA_HOOKCOUNT	3
// Tail-call hook. Overrides LUA_HOOKCALL, so the function call hook won't be
// called when a function is tail-called. (See `ldo.c:callhook()`.) There
// doesn't seem to be a way to hook into a tail-call event currently.
#define LUA_HOOKTAILCALL 4


/*
** Event masks
*/
// These can be bitwise-OR'd together and passed to lua_sethook() and returned
// from lua_gethookmask(). Why is LUA_HOOKTAILCALL missing here?
#define LUA_MASKCALL	(1 << LUA_HOOKCALL)
#define LUA_MASKRET	(1 << LUA_HOOKRET)
#define LUA_MASKLINE	(1 << LUA_HOOKLINE)
#define LUA_MASKCOUNT	(1 << LUA_HOOKCOUNT)

typedef struct lua_Debug lua_Debug;  /* activation record */


/* Functions to be called by the debugger in specific events */
typedef void (*lua_Hook) (lua_State *L, lua_Debug *ar);


LUA_API int (lua_getstack) (lua_State *L, int level, lua_Debug *ar);
LUA_API int (lua_getinfo) (lua_State *L, const char *what, lua_Debug *ar);
LUA_API const char *(lua_getlocal) (lua_State *L, const lua_Debug *ar, int n);
LUA_API const char *(lua_setlocal) (lua_State *L, const lua_Debug *ar, int n);
LUA_API const char *(lua_getupvalue) (lua_State *L, int funcindex, int n);
LUA_API const char *(lua_setupvalue) (lua_State *L, int funcindex, int n);

LUA_API void *(lua_upvalueid) (lua_State *L, int fidx, int n);
LUA_API void  (lua_upvaluejoin) (lua_State *L, int fidx1, int n1,
                                               int fidx2, int n2);

LUA_API void (lua_sethook) (lua_State *L, lua_Hook func, int mask, int count);
LUA_API lua_Hook (lua_gethook) (lua_State *L);
LUA_API int (lua_gethookmask) (lua_State *L);
LUA_API int (lua_gethookcount) (lua_State *L);


// This gets passed to the registered lua_Hook function whenever a hooked event
// occurs. You can also use lua_getinfo() to fill out different parts of this
// struct (by passing it the letter codes 'n', 'S', 'l', 'u', or 't').
struct lua_Debug {
  // The event that occurred (e.g. LUA_HOOKCALL).
  int event;
  // Name of the current function.
  const char *name;	/* (n) */
  // How the function was called (using a global or local variable, called as a
  // method of an object, or called as a field of an object). Explains what the
  // `name` field is naming.
  const char *namewhat;	/* (n) 'global', 'local', 'field', 'method' */
  // Whether it is a Lua function or a C function. Or 'main' if it's the main
  // part of a chunk. 'tail' seems unused, despite the comment?
  const char *what;	/* (S) 'Lua', 'C', 'main', 'tail' */
  // Where the function came from. Usually a filename, in which case it starts
  // with an '@'. Otherwise, contains the Lua source code where the function was
  // defined.
  const char *source;	/* (S) */
  // The current line of Lua code being executed.
  int currentline;	/* (l) */
  // What line the current function starts on.
  int linedefined;	/* (S) */
  // What line the current function ends on.
  int lastlinedefined;	/* (S) */
  // Number of upvalues the current function has.
  unsigned char nups;	/* (u) number of upvalues */
  // Number of parameters the current function has.
  unsigned char nparams;/* (u) number of parameters */
  // Whether the current function has a vararg (`...`) parameter. (Always true
  // for C functions (why?).)
  char isvararg;        /* (u) */
  // Whether this function was invoked by a tail call.
  char istailcall;	/* (t) */
  // A shortened version of `source`, used in printing error messages.
  char short_src[LUA_IDSIZE]; /* (S) */
  /* private part */
  // The actual call stack frame of the current function, used to fill out the
  // rest of this struct. lua_getstack() sets this part of the struct.
  struct CallInfo *i_ci;  /* active function */
};

/* }====================================================================== */


/******************************************************************************
* Copyright (C) 1994-2017 Lua.org, PUC-Rio.
*
* Permission is hereby granted, free of charge, to any person obtaining
* a copy of this software and associated documentation files (the
* "Software"), to deal in the Software without restriction, including
* without limitation the rights to use, copy, modify, merge, publish,
* distribute, sublicense, and/or sell copies of the Software, and to
* permit persons to whom the Software is furnished to do so, subject to
* the following conditions:
*
* The above copyright notice and this permission notice shall be
* included in all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
******************************************************************************/


#endif
