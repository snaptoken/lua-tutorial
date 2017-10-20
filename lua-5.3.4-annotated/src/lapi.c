/*
** $Id: lapi.c,v 2.259 2016/02/29 14:27:14 roberto Exp $
** Lua API
** See Copyright Notice in lua.h
*/

#define lapi_c
#define LUA_CORE

#include "lprefix.h"


#include <stdarg.h>
#include <string.h>

#include "lua.h"

#include "lapi.h"
#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lgc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "ltm.h"
#include "lundump.h"
#include "lvm.h"



// Ident string for RCS. RCS is a version control system. What is this useful
// for exactly?
const char lua_ident[] =
  "$LuaVersion: " LUA_COPYRIGHT " $"
  "$LuaAuthors: " LUA_AUTHORS " $";


/* value at a non-valid index */
// This is a special value returned by index2addr() below to indicate that the
// given index wasn't valid.
#define NONVALIDVALUE		cast(TValue *, luaO_nilobject)

/* corresponding test */
// Checks whether index2addr() returned a valid object, rather than
// NONVALIDVALUE. luaO_nilobject is located in static memory, so its address can
// be used as a sentinel value to indicate an invalid stack index. index2addr()
// only returns pointers into the stack, the registry, or upvalues when the
// index is valid. Note that a valid stack index might point to a nil object,
// but it will be a different nil object from luaO_nilobject.
#define isvalid(o)	((o) != luaO_nilobject)

/* test for pseudo index */
// LUA_REGISTRYINDEX is a special stack index that uses the registry value in
// the global state instead of a slot in the stack. Upvalues are also specified
// by special stack indexes, which are less than LUA_REGISTRYINDEX (which is a
// very low negative integer).
#define ispseudo(i)		((i) <= LUA_REGISTRYINDEX)

/* test for upvalue */
// Upvalue indexes are strictly lower than LUA_REGISTRYINDEX. (And anything
// higher than LUA_REGISTRYINDEX is a potentially valid stack index.)
#define isupvalue(i)		((i) < LUA_REGISTRYINDEX)

/* test for valid but not pseudo index */
// Only used in the api_checkstackindex() macro below.
#define isstackindex(i, o)	(isvalid(o) && !ispseudo(i))

// Assert that `o` isn't a NONVALIDVALUE. Only used in lua_copy() to check the
// destination address of the copy.
#define api_checkvalidindex(l,o)  api_check(l, isvalid(o), "invalid index")

// Assert that `i` is a stack index (not a pseudo index) and that `o` isn't a
// NONVALIDVALUE. Only used in lua_rotate() and lua_pcallk().
#define api_checkstackindex(l, i, o)  \
	api_check(l, isstackindex(i, o), "index not in the stack")


// Convert a stack (or pseudo) index to a pointer to the actual TValue it refers
// to. This is a pointer into the stack itself (if a stack index) or a pointer
// to the global registry or to an upvalue (if a pseudo index). Most Lua API
// functions take one more stack indexes as arguments, so this function is used
// all over the rest of this file.
static TValue *index2addr (lua_State *L, int idx) {
  // Get current stack frame.
  CallInfo *ci = L->ci;
  // Positive indexes are relative to the base stack slot of this stack frame,
  // which is ci->func.
  if (idx > 0) {
    TValue *o = ci->func + idx;
    // Assert that the index doesn't point outside of the current stack frame.
    api_check(L, idx <= ci->top - (ci->func + 1), "unacceptable index");
    // Return NONVALIDVALUE if the index points past the top of the stack.
    if (o >= L->top) return NONVALIDVALUE;
    else return o;
  }
  // Negative indexes are relative to the top of hte stack, so -1 refers to the
  // last thing pushed to the stack, -2 refers to the thing pushed before that,
  // and so on. Pseudo indexes are also negative, and can be differentiated from
  // stack indexes by seeing if they are <= LUA_REGISTRYINDEX (which ispseudo()
  // does).
  else if (!ispseudo(idx)) {  /* negative index */
    // Assert that the index refers to a stack slot above ci->func (which is the
    // base of the current stack frame). Also make sure the index isn't 0, which
    // has no meaning in the Lua API and should never be passed as an index.
    api_check(L, idx != 0 && -idx <= L->top - (ci->func + 1), "invalid index");
    return L->top + idx;
  }
  // If the index is LUA_REGISTRYINDEX, then just return a pointer to the global
  // registry value.
  else if (idx == LUA_REGISTRYINDEX)
    return &G(L)->l_registry;
  // Otherwise the index must be < LUA_REGISTRYINDEX, which means it refers to
  // an upvalue of the current function.
  else {  /* upvalues */
    idx = LUA_REGISTRYINDEX - idx;
    api_check(L, idx <= MAXUPVAL + 1, "upvalue index too large");
    // Only C closure and Lua closures have upvalues. Light C functions are just
    // C pointers, basic non-garbage-collected values, so they don't contain
    // upvalues.
    if (ttislcf(ci->func))  /* light C function? */
      return NONVALIDVALUE;  /* it has no upvalues */
    else {
      // It has to be a C closure (not a Lua closure) since index2addr() is only
      // used by the Lua C API.
      CClosure *func = clCvalue(ci->func);
      return (idx <= func->nupvalues) ? &func->upvalue[idx-1] : NONVALIDVALUE;
    }
  }
}


/*
** to be called by 'lua_checkstack' in protected mode, to grow stack
** capturing memory errors
*/
// The `ud` (userdata) argument points to the total size the stack needs to be.
// luaD_growstack() will either double the amount of allocated stack, or if
// that's not enough it will allocate the exact amount requested.
static void growstack (lua_State *L, void *ud) {
  int size = *(int *)ud;
  luaD_growstack(L, size);
}


// Ensures there is enough allocated stack to push `n` more elements, by
// reallocating a larger stack if necessary. Returns 1 if `n` stack slots are
// available, 0 if the stack wasn't able to grow large enough to hold `n` more
// elements (due to the LUAI_MAXSTACK limit or a memory allocation error).
LUA_API int lua_checkstack (lua_State *L, int n) {
  int res;
  CallInfo *ci = L->ci;
  lua_lock(L);
  api_check(L, n >= 0, "negative 'n'");
  // L->top points to the first free slot, and L->stack_last points one past the
  // last free slot, so subtracting them gets the number of free slots in the
  // stack, which we want to be at least n+1 (so that after `n` values are
  // pushed, L->top will still point to a valid free slot).
  if (L->stack_last - L->top > n)  /* stack large enough? */
    res = 1;  /* yes; check is OK */
  else {  /* no; need to grow stack */
    // L->top points one past the last stack slot in use, so subtracting the
    // first slot (L->stack) from it gets the number of slots in use.
    int inuse = cast_int(L->top - L->stack) + EXTRA_STACK;
    if (inuse > LUAI_MAXSTACK - n)  /* can grow without overflow? */
      res = 0;  /* no */
    else  /* try to grow stack */
      // Run the growstack() auxiliary function defined above in protected mode
      // to catch any memory allocation errors.
      res = (luaD_rawrunprotected(L, &growstack, &n) == LUA_OK);
  }
  // Update the maximum stack index for the current stack frame, if necessary.
  if (res && ci->top < L->top + n)
    ci->top = L->top + n;  /* adjust frame top */
  lua_unlock(L);
  return res;
}


// Moves `n` values from the top of one lua_State to the top of another
// lua_State. Assumes (with an assertion) that there is enough stack space in
// the destination stack frame to push `n` elements to it.
LUA_API void lua_xmove (lua_State *from, lua_State *to, int n) {
  int i;
  // If the source is the same as the destination, nothing needs to be done.
  if (from == to) return;
  lua_lock(to);
  // Assert that the source stack has `n` elements pushed to it.
  api_checknelems(from, n);
  // Assert that the `lua_State`s have the same global_State.
  api_check(from, G(from) == G(to), "moving among independent states");
  // Assert that the destination has `n` free stack slots in its stack frame.
  api_check(from, to->ci->top - to->top >= n, "stack overflow");
  // Pop values from source stack.
  from->top -= n;
  // Push values to destination stack.
  for (i = 0; i < n; i++) {
    setobj2s(to, to->top, from->top + i);
    to->top++;  /* stack already checked by previous 'api_check' */
  }
  lua_unlock(to);
}


// Registers panicf as the panic function for the global_State that the given
// lua_State belongs to. Returns the old panic function. The panic function is
// called when a Lua error is thrown and nothing catches it (i.e. when an error
// occurs during a completely unprotected call into Lua code).
LUA_API lua_CFunction lua_atpanic (lua_State *L, lua_CFunction panicf) {
  lua_CFunction old;
  lua_lock(L);
  old = G(L)->panic;
  G(L)->panic = panicf;
  lua_unlock(L);
  return old;
}


// Gets the LUA_VERSION_NUM, which is currently 503 for "Lua 5.3.x". Useful for
// doing comparisons to know what features are available.
LUA_API const lua_Number *lua_version (lua_State *L) {
  static const lua_Number version = LUA_VERSION_NUM;
  if (L == NULL) return &version;
  else return G(L)->version;
}



/*
** basic stack manipulation
*/


/*
** convert an acceptable stack index into an absolute index
*/
LUA_API int lua_absindex (lua_State *L, int idx) {
  // Pseudo indexes stay the same, and positive indexes are already absolute
  // (relative to the current stack frame). Only negative indexes need to be
  // converted, by getting the absolute index of the stack top and then adding
  // the negative index to that.
  return (idx > 0 || ispseudo(idx))
         ? idx
         : cast_int(L->top - L->ci->func) + idx;
}


// Get the stack index of the top element of the stack (in the current stack
// frame, as always).
LUA_API int lua_gettop (lua_State *L) {
  return cast_int(L->top - (L->ci->func + 1));
}


// Set the top of the stack to the given stack index. Causes elements to be
// popped off of the stack, or nil values to be pushed. `idx` may be positive or
// negative. It can also be 0, which empties the stack (so only the function
// remains at the base of the stack).
LUA_API void lua_settop (lua_State *L, int idx) {
  StkId func = L->ci->func;
  lua_lock(L);
  if (idx >= 0) {
    // Assert that enough stack has been allocated to accomodate the new top. (A
    // certain amount of stack is guaranteed to be available at all times. Isn't
    // that what L->ci->top is for? Why isn't it involved in this check?)
    api_check(L, idx <= L->stack_last - (func + 1), "new top too large");
    // Fill any intervening stack slots with nil values.
    while (L->top < (func + 1) + idx)
      setnilvalue(L->top++);
    // Set the new top.
    L->top = (func + 1) + idx;
  }
  else {
    // For negative indexes, check that they don't go below the current stack
    // frame. You can use a negative index that points to L->ci->func (the base
    // of the stack frame) to clear the stack.
    api_check(L, -(idx+1) <= (L->top - (func + 1)), "invalid new top");
    L->top += idx+1;  /* 'subtract' index (index is negative) */
  }
  lua_unlock(L);
}


/*
** Reverse the stack segment from 'from' to 'to'
** (auxiliary to 'lua_rotate')
*/
// Private function used by lua_rotate() below. Reverses the subarray of the
// stack between `from` and `to` inclusive.
static void reverse (lua_State *L, StkId from, StkId to) {
  // Swap the first and last values in the segment, then the second and second
  // last values, and so on, to reverse the segment.
  for (; from < to; from++, to--) {
    TValue temp;
    setobj(L, &temp, from);
    setobjs2s(L, from, to);
    setobj2s(L, to, &temp);
  }
}


/*
** Let x = AB, where A is a prefix of length 'n'. Then,
** rotate x n == BA. But BA == (A^r . B^r)^r.
*/
// Shifts a subarray of the stack (from `idx` to the top of the stack) `n`
// places to the right (or left if `n` is negative). Mainly used to define the
// lua_insert() and lua_remove() macros in lua.h, but also sometimes directly
// used to reorder the top few elements of the stack the way another function
// expects to find them.
//
// The rotate is performed in an interesting way: The last `n` elements are
// reversed in place, the rest of the beginning of the subarray is reversed in
// place, and finally the whole subarray is reversed in place. This way, a
// rotate operation takes the same amount of time no matter what `n` is.
LUA_API void lua_rotate (lua_State *L, int idx, int n) {
  // The segment to be rotated starts at `p` (prefix) and ends at `t` (top). `m`
  // is the "midpoint" of the segment such that there are `n` elements from `m`
  // (exclusive) to `t` (inclusive).
  StkId p, t, m;
  lua_lock(L);
  t = L->top - 1;  /* end of stack segment being rotated */
  p = index2addr(L, idx);  /* start of segment */
  // Assert that `idx` isn't a pseudo index and is valid stack index.
  api_checkstackindex(L, idx, p);
  // Assert that there are no more than `n` elements in the segment being
  // rotated.
  api_check(L, (n >= 0 ? n : -n) <= (t - p + 1), "invalid 'n'");
  m = (n >= 0 ? t - n : p - n - 1);  /* end of prefix */
  // I don't think the prefix has length 'n', the suffix does. Right?
  reverse(L, p, m);  /* reverse the prefix with length 'n' */
  reverse(L, m + 1, t);  /* reverse the suffix */
  reverse(L, p, t);  /* reverse the entire segment */
  lua_unlock(L);
}


// Think of this as a "mov" instruction and `fromidx` and `toidx` as registers.
// Simply copies the value in `fromidx` to the `toidx` slot in the stack.
LUA_API void lua_copy (lua_State *L, int fromidx, int toidx) {
  TValue *fr, *to;
  lua_lock(L);
  fr = index2addr(L, fromidx);
  // If `fromidx` is invalid, `fr` will point to a nil object, and so nil will
  // get copied to `to`. So we won't bother validating `fromidx`.
  to = index2addr(L, toidx);
  // Validate `toidx` to make sure `to` points to a stack slot we can write to.
  api_checkvalidindex(L, to);
  // Do the copy.
  setobj(L, to, fr);
  // If we overwrote an upvalue, do a GC barrier on the new value and the
  // current function (which now references the value since upvalues are owned
  // by functions). I need to come back to this, why are these barriers needed?
  if (isupvalue(toidx))  /* function upvalue? */
    luaC_barrier(L, clCvalue(L->ci->func), fr);
  /* LUA_REGISTRYINDEX does not need gc barrier
     (collector revisits it before finishing collection) */
  lua_unlock(L);
}


// Push the value at the given stack index to the top of the stack.
LUA_API void lua_pushvalue (lua_State *L, int idx) {
  lua_lock(L);
  // If `idx` is invalid, nil will be pushed.
  setobj2s(L, L->top, index2addr(L, idx));
  // Increments L->top.
  api_incr_top(L);
  lua_unlock(L);
}



/*
** access functions (stack -> C)
*/


// Get the basic type tag of a Lua value (e.g. LUA_TNUMBER). Returns LUA_TNONE
// (-1) if the `idx` is invalid.
LUA_API int lua_type (lua_State *L, int idx) {
  StkId o = index2addr(L, idx);
  return (isvalid(o) ? ttnov(o) : LUA_TNONE);
}


// Converts a basic type tag (e.g. LUA_TNUMBER) to a human-readable C string
// (e.g. "number"), as defined by ltm.c:luaT_typenames_. LUA_TNONE converts to
// the string "no value".
LUA_API const char *lua_typename (lua_State *L, int t) {
  UNUSED(L);
  api_check(L, LUA_TNONE <= t && t < LUA_NUMTAGS, "invalid tag");
  // Look up the type name in the static luaT_typenames_ table defined in ltm.c.
  return ttypename(t);
}


// Check if a value is a light C function or C closure.
LUA_API int lua_iscfunction (lua_State *L, int idx) {
  StkId o = index2addr(L, idx);
  return (ttislcf(o) || (ttisCclosure(o)));
}


// Check if a value is an integer (not a float).
LUA_API int lua_isinteger (lua_State *L, int idx) {
  StkId o = index2addr(L, idx);
  return ttisinteger(o);
}


// Check if a value is a number, or can be converted to a number. (So integers,
// floats, and numeric strings.)
LUA_API int lua_isnumber (lua_State *L, int idx) {
  lua_Number n;
  const TValue *o = index2addr(L, idx);
  return tonumber(o, &n);
}


// Check if a value is a string, or can be converted to a string. (Numbers can
// act as strings if LUA_NOCVTN2S isn't defined. See `lvm.h:cvt2str()`.)
LUA_API int lua_isstring (lua_State *L, int idx) {
  const TValue *o = index2addr(L, idx);
  return (ttisstring(o) || cvt2str(o));
}


// Check if a value is light userdata or "full" userdata.
LUA_API int lua_isuserdata (lua_State *L, int idx) {
  const TValue *o = index2addr(L, idx);
  return (ttisfulluserdata(o) || ttislightuserdata(o));
}


// Check if two values are "raw equal", meaning the __eq() tag method will be
// ignored.
LUA_API int lua_rawequal (lua_State *L, int index1, int index2) {
  StkId o1 = index2addr(L, index1);
  StkId o2 = index2addr(L, index2);
  // Always return false if one or both stack indexes are invalid. Otherwise,
  // calls lvm.c:luaV_equalobj() passing NULL for the lua_State argument to
  // indicate tag methods shouldn't be considered.
  return (isvalid(o1) && isvalid(o2)) ? luaV_rawequalobj(o1, o2) : 0;
}


// Perform an arithmetic operation on the top two (or one for unary operations)
// values on the stack, replacing them with the result value.
LUA_API void lua_arith (lua_State *L, int op) {
  lua_lock(L);
  // Unary minus and bitwise-NOT are the only two unary operators.
  if (op != LUA_OPUNM && op != LUA_OPBNOT)
    api_checknelems(L, 2);  /* all other operations expect two operands */
  else {  /* for unary operations, add fake 2nd operand */
    api_checknelems(L, 1);
    // Just duplicate the single operand so there's two copies of them on the
    // stack. We'll still only use one of them, then overwrite both with the
    // result like we would for a binary operator.
    setobjs2s(L, L->top, L->top - 1);
    api_incr_top(L);
  }
  /* first operand at top - 2, second at top - 1; result go to top - 2 */
  luaO_arith(L, op, L->top - 2, L->top - 1, L->top - 2);
  // We overwrote the first operand with the result, now we pop the second
  // operand off the stack, leaving just the result in place of the two
  // operands.
  L->top--;  /* remove second operand */
  lua_unlock(L);
}


// Perform a comparison operation on the two given values, returning 1 if the
// comparison is true and 0 if false. Three operations are handled: LUA_OPEQ
// (equality), LUA_OPLT (less than), and LUA_OPLE (less than or equal).
LUA_API int lua_compare (lua_State *L, int index1, int index2, int op) {
  StkId o1, o2;
  int i = 0;
  lua_lock(L);  /* may call tag method */
  o1 = index2addr(L, index1);
  o2 = index2addr(L, index2);
  // False is returned by default if either index is invalid.
  if (isvalid(o1) && isvalid(o2)) {
    switch (op) {
      case LUA_OPEQ: i = luaV_equalobj(L, o1, o2); break;
      case LUA_OPLT: i = luaV_lessthan(L, o1, o2); break;
      case LUA_OPLE: i = luaV_lessequal(L, o1, o2); break;
      default: api_check(L, 0, "invalid option");
    }
  }
  lua_unlock(L);
  return i;
}


// Converts a C string to a Lua number, pushing the result to the stack if
// successful. Returns the string size on success, and 0 on failure.
LUA_API size_t lua_stringtonumber (lua_State *L, const char *s) {
  size_t sz = luaO_str2num(s, L->top);
  if (sz != 0)
    api_incr_top(L);
  return sz;
}


// Converts a Lua value to a C float, returning the result. `pisnum` gets set to
// 1 if the value was able to be converted to a float, otherwise is set to 0.
// NULL may be passed for `pisnum`, as is the case with the lua_tonumber()
// macro.
LUA_API lua_Number lua_tonumberx (lua_State *L, int idx, int *pisnum) {
  lua_Number n;
  const TValue *o = index2addr(L, idx);
  int isnum = tonumber(o, &n);
  if (!isnum)
    n = 0;  /* call to 'tonumber' may change 'n' even if it fails */
  if (pisnum) *pisnum = isnum;
  return n;
}


// Converts a Lua value to a C int, returning the result. `pisnum` gets set to 1
// if the value was able to be converted to an int, otherwise is set to 0. NULL
// may be passed for `pisnum`, as is the case with the lua_tointeger() macro.
LUA_API lua_Integer lua_tointegerx (lua_State *L, int idx, int *pisnum) {
  lua_Integer res;
  const TValue *o = index2addr(L, idx);
  int isnum = tointeger(o, &res);
  if (!isnum)
    res = 0;  /* call to 'tointeger' may change 'n' even if it fails */
  if (pisnum) *pisnum = isnum;
  return res;
}


// Returns 1 if the given Lua value is truthy, or 0 if falsy. Only nil and false
// are falsy in Lua. (See `lobject.h:l_isfalse()`.)
LUA_API int lua_toboolean (lua_State *L, int idx) {
  const TValue *o = index2addr(L, idx);
  return !l_isfalse(o);
}


// Converts a Lua value to a string. If successful, the value at the given stack
// index is replaced with the Lua string, and a pointer to the C string is
// returned. Otherwise NULL is returned. `len` is set to the length of the
// string, or 0 on failure.
LUA_API const char *lua_tolstring (lua_State *L, int idx, size_t *len) {
  StkId o = index2addr(L, idx);
  if (!ttisstring(o)) {
    if (!cvt2str(o)) {  /* not convertible? */
      if (len != NULL) *len = 0;
      return NULL;
    }
    lua_lock(L);  /* 'luaO_tostring' may create a new string */
    luaO_tostring(L, o);
    // Does one step of garbage collection (`lgc.c:luaC_step()`) if the GC debt
    // is positive. Why is this here?
    luaC_checkGC(L);
    // Recalculate the address of `o` since the stack might have a different
    // address after doing GC.
    o = index2addr(L, idx);  /* previous call may reallocate the stack */
    lua_unlock(L);
  }
  // Set `len` to the string length, unless NULL was passed for `len`.
  if (len != NULL)
    *len = vslen(o);
  // Return the C string contained by `o`.
  return svalue(o);
}


// Gets length of string, table, or userdata without considering the __len()
// metamethod (which normally lets you override the `#` operator).
LUA_API size_t lua_rawlen (lua_State *L, int idx) {
  StkId o = index2addr(L, idx);
  switch (ttype(o)) {
    case LUA_TSHRSTR: return tsvalue(o)->shrlen;
    case LUA_TLNGSTR: return tsvalue(o)->u.lnglen;
    case LUA_TUSERDATA: return uvalue(o)->len;
    case LUA_TTABLE: return luaH_getn(hvalue(o));
    default: return 0;
  }
}


// Get the C function pointer from a Lua C function object, whether it's a light
// C function or a C closure. Returns NULL if the value isn't a C function.
LUA_API lua_CFunction lua_tocfunction (lua_State *L, int idx) {
  StkId o = index2addr(L, idx);
  if (ttislcf(o)) return fvalue(o);
  else if (ttisCclosure(o))
    return clCvalue(o)->f;
  else return NULL;  /* not a C function */
}


// Get a void pointer to the data of a userdata object, whether it's a light
// userdata (just a C pointer) or a "full" userdata (owns its own data). Returns
// NULL for values that aren't userdata.
LUA_API void *lua_touserdata (lua_State *L, int idx) {
  StkId o = index2addr(L, idx);
  switch (ttnov(o)) {
    case LUA_TUSERDATA: return getudatamem(uvalue(o));
    case LUA_TLIGHTUSERDATA: return pvalue(o);
    default: return NULL;
  }
}


// Gets the lua_State* that a Lua thread object contains. Returns NULL if the
// value isn't a Lua thread object.
LUA_API lua_State *lua_tothread (lua_State *L, int idx) {
  StkId o = index2addr(L, idx);
  return (!ttisthread(o)) ? NULL : thvalue(o);
}


// Gets a pointer to the C structure representing the given object. Used by
// lauxlib.c:luaL_tolstring() as a last resort to convert an object to a string
// representation suitable for debugging or printing out in the Lua REPL.
LUA_API const void *lua_topointer (lua_State *L, int idx) {
  StkId o = index2addr(L, idx);
  switch (ttype(o)) {
    // Pointer to Table struct.
    case LUA_TTABLE: return hvalue(o);
    // Pointer to LClosure struct.
    case LUA_TLCL: return clLvalue(o);
    // Pointer to CClosure struct.
    case LUA_TCCL: return clCvalue(o);
    // What, a function pointer can't be casted directly to `void*`?
    case LUA_TLCF: return cast(void *, cast(size_t, fvalue(o)));
    // Pointer to lua_State struct.
    case LUA_TTHREAD: return thvalue(o);
    // Pointer to the data contained by the userdata object.
    case LUA_TUSERDATA: return getudatamem(uvalue(o));
    // Just a pointer.
    case LUA_TLIGHTUSERDATA: return pvalue(o);
    default: return NULL;
  }
}



/*
** push functions (C -> stack)
*/


// Push a nil value to the stack.
LUA_API void lua_pushnil (lua_State *L) {
  lua_lock(L);
  setnilvalue(L->top);
  api_incr_top(L);
  lua_unlock(L);
}


// Push a float to the stack.
LUA_API void lua_pushnumber (lua_State *L, lua_Number n) {
  lua_lock(L);
  setfltvalue(L->top, n);
  api_incr_top(L);
  lua_unlock(L);
}


// Push an int to the stack.
LUA_API void lua_pushinteger (lua_State *L, lua_Integer n) {
  lua_lock(L);
  setivalue(L->top, n);
  api_incr_top(L);
  lua_unlock(L);
}


/*
** Pushes on the stack a string with given length. Avoid using 's' when
** 'len' == 0 (as 's' can be NULL in that case), due to later use of
** 'memcmp' and 'memcpy'.
*/
LUA_API const char *lua_pushlstring (lua_State *L, const char *s, size_t len) {
  TString *ts;
  lua_lock(L);
  ts = (len == 0) ? luaS_new(L, "") : luaS_newlstr(L, s, len);
  setsvalue2s(L, L->top, ts);
  api_incr_top(L);
  // Do one step of garbage collection if GC debt is positive. Why? Because we
  // just allocated some memory?
  luaC_checkGC(L);
  lua_unlock(L);
  return getstr(ts);
}


// Like lua_pushlstring() above, but the length of the string is found via
// strlen(). It also uses luaS_new() to create the string object, which uses the
// string cache to quickly return interned strings that have already been
// created from the same string pointer `s`.
LUA_API const char *lua_pushstring (lua_State *L, const char *s) {
  lua_lock(L);
  if (s == NULL)
    setnilvalue(L->top);
  else {
    TString *ts;
    ts = luaS_new(L, s);
    setsvalue2s(L, L->top, ts);
    s = getstr(ts);  /* internal copy's address */
  }
  api_incr_top(L);
  luaC_checkGC(L);
  lua_unlock(L);
  return s;
}


// Push formatted string to the stack.
LUA_API const char *lua_pushvfstring (lua_State *L, const char *fmt,
                                      va_list argp) {
  const char *ret;
  lua_lock(L);
  ret = luaO_pushvfstring(L, fmt, argp);
  luaC_checkGC(L);
  lua_unlock(L);
  return ret;
}


// Variadic version of the above function.
LUA_API const char *lua_pushfstring (lua_State *L, const char *fmt, ...) {
  const char *ret;
  va_list argp;
  lua_lock(L);
  va_start(argp, fmt);
  ret = luaO_pushvfstring(L, fmt, argp);
  va_end(argp);
  luaC_checkGC(L);
  lua_unlock(L);
  return ret;
}


// Push a new C closure with `n` upvalues onto the stack. If `n` is 0, a light
// C function (just a function pointer, non-garbage-collected) will be pushed
// instead. The upvalues' initial values will be set to the top `n` values on
// the stack, in order.
LUA_API void lua_pushcclosure (lua_State *L, lua_CFunction fn, int n) {
  lua_lock(L);
  // If no upvalues needed, just push a light C function.
  if (n == 0) {
    setfvalue(L->top, fn);
  }
  else {
    CClosure *cl;
    api_checknelems(L, n);
    api_check(L, n <= MAXUPVAL, "upvalue index too large");
    // A C closure is just a function pointer and an array of `TValue`s (the
    // upvalues).
    cl = luaF_newCclosure(L, n);
    // Set the function pointer.
    cl->f = fn;
    // Copy the upvalues' values from the top `n` elements of the stack, in
    // order.
    L->top -= n;
    while (n--) {
      setobj2n(L, &cl->upvalue[n], L->top + n);
      /* does not need barrier because closure is white */
    }
    // We popped all the values off the stack with `L->top -= n` above. Now push
    // the new C closure to the top of the stack as the result.
    setclCvalue(L, L->top, cl);
  }
  api_incr_top(L);
  luaC_checkGC(L);
  lua_unlock(L);
}


// Push a boolean to the stack.
LUA_API void lua_pushboolean (lua_State *L, int b) {
  lua_lock(L);
  setbvalue(L->top, (b != 0));  /* ensure that true is 1 */
  api_incr_top(L);
  lua_unlock(L);
}


// Push a light userdata (a void pointer) to the stack.
LUA_API void lua_pushlightuserdata (lua_State *L, void *p) {
  lua_lock(L);
  setpvalue(L->top, p);
  api_incr_top(L);
  lua_unlock(L);
}


// Push a Lua thread (a lua_State*) to the stack. (To its own stack.) Returns
// true if L is the main thread. Is there a way to push a thread to another
// thread's stack?
LUA_API int lua_pushthread (lua_State *L) {
  lua_lock(L);
  setthvalue(L, L->top, L);
  api_incr_top(L);
  lua_unlock(L);
  return (G(L)->mainthread == L);
}



/*
** get functions (Lua -> stack)
*/


// Looks up a string key in a Lua table. Creates a Lua string out of the C
// string `k` and uses that as the lookup key.
static int auxgetstr (lua_State *L, const TValue *t, const char *k) {
  const TValue *slot;
  // Convert C string to Lua string (possibly using the string cache).
  TString *str = luaS_new(L, k);
  // Looks up the key in the table using luaH_getstr() and sets `slot` to the
  // resulting value in the table. luaV_fastget() returns true iff the looked-up
  // value is not nil.
  if (luaV_fastget(L, t, str, slot, luaH_getstr)) {
    // Push the resulting value to the stack.
    setobj2s(L, L->top, slot);
    api_incr_top(L);
  }
  // If the looked-up value was nil, we have to try the __index() metamethod.
  else {
    // Push the key string to the stack, since luaV_finishget() writes its
    // result value to a StkId.
    setsvalue2s(L, L->top, str);
    api_incr_top(L);
    // Finish the table access by trying the __index() metamethod. The first
    // `L->top - 1` argument specifies the key to look up, and the second one
    // specifies where to write the result value of the lookup.
    luaV_finishget(L, t, L->top - 1, L->top - 1, slot);
  }
  // The corresponding lua_lock() as at the top of the functions that call this
  // auxiliary function.
  lua_unlock(L);
  // Returns the basic type tag of the looked-up value.
  return ttnov(L->top - 1);
}


// Push the value of the named global variable to the stack, or nil if the
// variable doesn't exist. Returns the type tag of the value (LUA_TNIL if the
// variable doesn't exist).
LUA_API int lua_getglobal (lua_State *L, const char *name) {
  // Global variables are stored in a ordinary Lua table which is itself stored
  // in another Lua table called the registry, which is kept in the
  // global_State struct.
  Table *reg = hvalue(&G(L)->l_registry);
  // Corresponding lua_unlock() call is in auxgetstr().
  lua_lock(L);
  // The luaH_getint() call looks up the globals table in the registry table,
  // then auxgetstr() looks up the variable name in the globals table.
  return auxgetstr(L, luaH_getint(reg, LUA_RIDX_GLOBALS), name);
}


// Look up a value in the table at the given index, using the value at the top
// of the stack as the key.
LUA_API int lua_gettable (lua_State *L, int idx) {
  StkId t;
  lua_lock(L);
  t = index2addr(L, idx);
  // The first `L->top - 1` specifies the key, the second one specifies where
  // the resulting value will be written to. So the key will be "popped" and the
  // resulting value will be "pushed" to the stack.
  luaV_gettable(L, t, L->top - 1, L->top - 1);
  lua_unlock(L);
  // Return the basic type tag of the resulting value.
  return ttnov(L->top - 1);
}


// Look up a string key (given as a C string) in the table at the given index.
// Returns the basic type tag of the result (nil if the key doesn't exist).
LUA_API int lua_getfield (lua_State *L, int idx, const char *k) {
  // Corresponding lua_unlock() call is in auxgetstr().
  lua_lock(L);
  return auxgetstr(L, index2addr(L, idx), k);
}


// Look up a value in the Lua table at the given index using an int `n` as the
// key. Returns the type tag of the resulting value.
LUA_API int lua_geti (lua_State *L, int idx, lua_Integer n) {
  StkId t;
  const TValue *slot;
  lua_lock(L);
  t = index2addr(L, idx);
  // Same as auxgetstr() above, but with luaH_getint().
  if (luaV_fastget(L, t, n, slot, luaH_getint)) {
    setobj2s(L, L->top, slot);
    api_incr_top(L);
  }
  else {
    setivalue(L->top, n);
    api_incr_top(L);
    luaV_finishget(L, t, L->top - 1, L->top - 1, slot);
  }
  lua_unlock(L);
  return ttnov(L->top - 1);
}


// Use the value at the top of the stack as a key to look up in the table at the
// given index, and replace the key at the top of the stack with the looked-up
// value. This is a raw lookup, so the __index() metamethod won't be tried. If
// the key doesn't exist in the table, the result is nil. Returns the basic type
// tag of the result.
LUA_API int lua_rawget (lua_State *L, int idx) {
  StkId t;
  lua_lock(L);
  t = index2addr(L, idx);
  // Most table lookup functions can take any type of Lua value that might have
  // an __index() metamethod. But for a raw lookup, it only makes sense to pass
  // an actual Lua table.
  api_check(L, ttistable(t), "table expected");
  // "Pops" the key off the stack and "pushes" the looked-up value (or nil) to
  // the stack.
  setobj2s(L, L->top - 1, luaH_get(hvalue(t), L->top - 1));
  lua_unlock(L);
  return ttnov(L->top - 1);
}


// Raw lookup using an int key, in the table at the given index. Returns the
// basic type tag of the result.
LUA_API int lua_rawgeti (lua_State *L, int idx, lua_Integer n) {
  StkId t;
  lua_lock(L);
  t = index2addr(L, idx);
  api_check(L, ttistable(t), "table expected");
  // Push the looked-up value to the stack.
  setobj2s(L, L->top, luaH_getint(hvalue(t), n));
  api_incr_top(L);
  lua_unlock(L);
  return ttnov(L->top - 1);
}


// Raw lookup using a C pointer (a light userdata value in Lua) as the key, in
// the table at the given index. Returns the basic type tag of the result.
LUA_API int lua_rawgetp (lua_State *L, int idx, const void *p) {
  StkId t;
  TValue k;
  lua_lock(L);
  t = index2addr(L, idx);
  api_check(L, ttistable(t), "table expected");
  // Convert `p` to a light userdata (just a void pointer wrapped in a TValue),
  // to use as the key.
  setpvalue(&k, cast(void *, p));
  // Push the looked-up value to the stack.
  setobj2s(L, L->top, luaH_get(hvalue(t), &k));
  api_incr_top(L);
  lua_unlock(L);
  return ttnov(L->top - 1);
}


// Pushes a new, empty Lua table to the stack. If `narray` or `nrec` aren't 0,
// preallocates the array part and/or the hash part, respectively, to be able to
// hold that many elements.
LUA_API void lua_createtable (lua_State *L, int narray, int nrec) {
  Table *t;
  lua_lock(L);
  // Create new Lua table.
  t = luaH_new(L);
  // Push it to the stack.
  sethvalue(L, L->top, t);
  api_incr_top(L);
  // Resize it if requested.
  if (narray > 0 || nrec > 0)
    luaH_resize(L, t, narray, nrec);
  // Do one step of garbage collection at this point.
  luaC_checkGC(L);
  lua_unlock(L);
}


// Pushes the metatable of the object at the given index to the stack. If the
// object doesn't have a metatable, it returns 0 and nothing is pushed to the
// stack. Otherwise it returns 1.
LUA_API int lua_getmetatable (lua_State *L, int objindex) {
  const TValue *obj;
  Table *mt;
  int res = 0;
  lua_lock(L);
  obj = index2addr(L, objindex);
  // Table and Udata objects contain their own individual metatables. All other
  // types have the same global metatable for that type, contained in the `mt`
  // field of the global_State struct.
  switch (ttnov(obj)) {
    case LUA_TTABLE:
      mt = hvalue(obj)->metatable;
      break;
    case LUA_TUSERDATA:
      mt = uvalue(obj)->metatable;
      break;
    default:
      // Look up this type's global metatable using the type tag as an index
      // into the `mt` array.
      mt = G(L)->mt[ttnov(obj)];
      break;
  }
  // If the object has a metatable, push it to the stack and return 1.
  if (mt != NULL) {
    sethvalue(L, L->top, mt);
    api_incr_top(L);
    res = 1;
  }
  lua_unlock(L);
  return res;
}


// Get the uservalue of the full userdata object at the given index. Pushes the
// uservalue to the stack.
LUA_API int lua_getuservalue (lua_State *L, int idx) {
  StkId o;
  lua_lock(L);
  o = index2addr(L, idx);
  api_check(L, ttisfulluserdata(o), "full userdata expected");
  // Get the uservalue and put it on top of the stack.
  getuservalue(L, uvalue(o), L->top);
  api_incr_top(L);
  lua_unlock(L);
  return ttnov(L->top - 1);
}


/*
** set functions (stack -> Lua)
*/
// The functions in this section are exactly parallel to the functions in the
// above section. Only these are setters, the above section contains getters. So
// I'll annotate the functions in this section more lightly, since a lot of what
// they do has been explained in the corresponding getter functions above.

/*
** t[k] = value at the top of the stack (where 'k' is a string)
*/
// Used by lua_setglobal() and lua_setfield() below.
static void auxsetstr (lua_State *L, const TValue *t, const char *k) {
  const TValue *slot;
  TString *str = luaS_new(L, k);
  // Assert that a value exists at the top of the stack before we use it.
  api_checknelems(L, 1);
  // If the key already exists in the table, this sets it to the new value.
  // Otherwise, if it's a table and the key doesn't exist, this sets `slot` to
  // point to the slot where the value should be inserted (but first we need to
  // call the __newindex() tag method). If it's not a table, the __newindex()
  // tag method needs to be called instead, and `slot` is set to NULL.
  if (luaV_fastset(L, t, str, slot, luaH_getstr, L->top - 1))
    L->top--;  /* pop value */
  else {
    setsvalue2s(L, L->top, str);  /* push 'str' (to make it a TValue) */
    api_incr_top(L);
    // Try the __newindex() tag method. If the tag method doesn't exist, create
    // the key in the table and assign it the value.
    luaV_finishset(L, t, L->top - 1, L->top - 2, slot);
    L->top -= 2;  /* pop value and key */
  }
  lua_unlock(L);  /* lock done by caller */
}


// Set the global variable with the given name to the value at the top of the
// stack (and popping the value off the stack afterwards).
LUA_API void lua_setglobal (lua_State *L, const char *name) {
  Table *reg = hvalue(&G(L)->l_registry);
  lua_lock(L);  /* unlock done in 'auxsetstr' */
  auxsetstr(L, luaH_getint(reg, LUA_RIDX_GLOBALS), name);
}


// This function expects a key and a value to be pushed to the stack, in that
// order. Then it assigns the value to the key in the table at the given index.
// Both the key and value are popped off the stack afterwards.
LUA_API void lua_settable (lua_State *L, int idx) {
  StkId t;
  lua_lock(L);
  api_checknelems(L, 2);
  t = index2addr(L, idx);
  luaV_settable(L, t, L->top - 2, L->top - 1);
  L->top -= 2;  /* pop index and value */
  lua_unlock(L);
}


// Sets the field `k` of the table at the given index to the value at the top of
// the stack, popping the value off the stack afterwards.
LUA_API void lua_setfield (lua_State *L, int idx, const char *k) {
  lua_lock(L);  /* unlock done in 'auxsetstr' */
  auxsetstr(L, index2addr(L, idx), k);
}


// Set the value in the table with the given index with the integer key `n` to
// the value at the top of the stack, popping the value off the stack
// afterwards.
LUA_API void lua_seti (lua_State *L, int idx, lua_Integer n) {
  StkId t;
  const TValue *slot;
  lua_lock(L);
  api_checknelems(L, 1);
  t = index2addr(L, idx);
  if (luaV_fastset(L, t, n, slot, luaH_getint, L->top - 1))
    L->top--;  /* pop value */
  else {
    setivalue(L->top, n);
    api_incr_top(L);
    luaV_finishset(L, t, L->top - 1, L->top - 2, slot);
    L->top -= 2;  /* pop value and key */
  }
  lua_unlock(L);
}


// Like lua_settable() above, but ignore the __newindex() metamethod.
LUA_API void lua_rawset (lua_State *L, int idx) {
  StkId o;
  TValue *slot;
  lua_lock(L);
  api_checknelems(L, 2);
  o = index2addr(L, idx);
  api_check(L, ttistable(o), "table expected");
  slot = luaH_set(L, hvalue(o), L->top - 2);
  setobj2t(L, slot, L->top - 1);
  // Clears all the flags that cache the nonexistence of each tag method of a
  // table object. (Flags in the tag method cache are set to 1 if that tag
  // method doesn't exist, so then the tag method doesn't have to be searched
  // for when doing otherwise simple operations like arithmetic, table lookups,
  // etc.). Normally, luaV_finishset() takes care of invalidating the cache, but
  // this is a raw set.
  invalidateTMcache(hvalue(o));
  // If the table is marked black, and the value is marked white, mark the table
  // gray again. (Why?) This is also normally taken care of by luaV_finishset().
  luaC_barrierback(L, hvalue(o), L->top-1);
  L->top -= 2;
  lua_unlock(L);
}


// Like lua_seti() above, but ignore the __newindex() metamethod.
LUA_API void lua_rawseti (lua_State *L, int idx, lua_Integer n) {
  StkId o;
  lua_lock(L);
  api_checknelems(L, 1);
  o = index2addr(L, idx);
  api_check(L, ttistable(o), "table expected");
  luaH_setint(L, hvalue(o), n, L->top - 1);
  // The invalidateTMcache() step is skipped here, since tag methods are only
  // set/unset with string keys.
  luaC_barrierback(L, hvalue(o), L->top-1);
  L->top--;
  lua_unlock(L);
}


// Like lua_rawseti() above, but using a C pointer (light userdata) as the key.
LUA_API void lua_rawsetp (lua_State *L, int idx, const void *p) {
  StkId o;
  TValue k, *slot;
  lua_lock(L);
  api_checknelems(L, 1);
  o = index2addr(L, idx);
  api_check(L, ttistable(o), "table expected");
  setpvalue(&k, cast(void *, p));
  slot = luaH_set(L, hvalue(o), &k);
  setobj2t(L, slot, L->top - 1);
  luaC_barrierback(L, hvalue(o), L->top - 1);
  L->top--;
  lua_unlock(L);
}


// Set the metatable of the object at the given index to the table at the top of
// the stack. Unset the metatable of the object if nil is the top value of the
// stack. If the object has a type other than table or userdata, this sets the
// global metatable for that type.
LUA_API int lua_setmetatable (lua_State *L, int objindex) {
  TValue *obj;
  Table *mt;
  lua_lock(L);
  api_checknelems(L, 1);
  obj = index2addr(L, objindex);
  if (ttisnil(L->top - 1))
    mt = NULL;
  else {
    api_check(L, ttistable(L->top - 1), "table expected");
    mt = hvalue(L->top - 1);
  }
  switch (ttnov(obj)) {
    case LUA_TTABLE: {
      hvalue(obj)->metatable = mt;
      if (mt) {
        luaC_objbarrier(L, gcvalue(obj), mt);
        // Finalizers for objects in Lua are set by attaching a __gc() tag
        // method to the object. So this checks for that tag method and
        // registers the object for finalization if needed.
        luaC_checkfinalizer(L, gcvalue(obj), mt);
      }
      break;
    }
    case LUA_TUSERDATA: {
      uvalue(obj)->metatable = mt;
      if (mt) {
        luaC_objbarrier(L, uvalue(obj), mt);
        luaC_checkfinalizer(L, gcvalue(obj), mt);
      }
      break;
    }
    default: {
      G(L)->mt[ttnov(obj)] = mt;
      break;
    }
  }
  L->top--;
  lua_unlock(L);
  return 1;
}


// Set the uservalue of the Udata at the given index to the value at the top of
// the stack (popping the value off the stack afterwards).
LUA_API void lua_setuservalue (lua_State *L, int idx) {
  StkId o;
  lua_lock(L);
  api_checknelems(L, 1);
  o = index2addr(L, idx);
  api_check(L, ttisfulluserdata(o), "full userdata expected");
  setuservalue(L, uvalue(o), L->top - 1);
  luaC_barrier(L, gcvalue(o), L->top - 1);
  L->top--;
  lua_unlock(L);
}


/*
** 'load' and 'call' functions (run Lua code)
*/


#define checkresults(L,na,nr) \
     api_check(L, (nr) == LUA_MULTRET || (L->ci->top - L->top >= (nr) - (na)), \
	"results from function overflow current stack size")


LUA_API void lua_callk (lua_State *L, int nargs, int nresults,
                        lua_KContext ctx, lua_KFunction k) {
  StkId func;
  lua_lock(L);
  api_check(L, k == NULL || !isLua(L->ci),
    "cannot use continuations inside hooks");
  api_checknelems(L, nargs+1);
  api_check(L, L->status == LUA_OK, "cannot do calls on non-normal thread");
  checkresults(L, nargs, nresults);
  func = L->top - (nargs+1);
  if (k != NULL && L->nny == 0) {  /* need to prepare continuation? */
    L->ci->u.c.k = k;  /* save continuation */
    L->ci->u.c.ctx = ctx;  /* save context */
    luaD_call(L, func, nresults);  /* do the call */
  }
  else  /* no continuation or no yieldable */
    luaD_callnoyield(L, func, nresults);  /* just do the call */
  adjustresults(L, nresults);
  lua_unlock(L);
}



/*
** Execute a protected call.
*/
struct CallS {  /* data to 'f_call' */
  StkId func;
  int nresults;
};


static void f_call (lua_State *L, void *ud) {
  struct CallS *c = cast(struct CallS *, ud);
  luaD_callnoyield(L, c->func, c->nresults);
}



LUA_API int lua_pcallk (lua_State *L, int nargs, int nresults, int errfunc,
                        lua_KContext ctx, lua_KFunction k) {
  struct CallS c;
  int status;
  ptrdiff_t func;
  lua_lock(L);
  api_check(L, k == NULL || !isLua(L->ci),
    "cannot use continuations inside hooks");
  api_checknelems(L, nargs+1);
  api_check(L, L->status == LUA_OK, "cannot do calls on non-normal thread");
  checkresults(L, nargs, nresults);
  if (errfunc == 0)
    func = 0;
  else {
    StkId o = index2addr(L, errfunc);
    api_checkstackindex(L, errfunc, o);
    func = savestack(L, o);
  }
  c.func = L->top - (nargs+1);  /* function to be called */
  if (k == NULL || L->nny > 0) {  /* no continuation or no yieldable? */
    c.nresults = nresults;  /* do a 'conventional' protected call */
    status = luaD_pcall(L, f_call, &c, savestack(L, c.func), func);
  }
  else {  /* prepare continuation (call is already protected by 'resume') */
    CallInfo *ci = L->ci;
    ci->u.c.k = k;  /* save continuation */
    ci->u.c.ctx = ctx;  /* save context */
    /* save information for error recovery */
    ci->extra = savestack(L, c.func);
    ci->u.c.old_errfunc = L->errfunc;
    L->errfunc = func;
    setoah(ci->callstatus, L->allowhook);  /* save value of 'allowhook' */
    ci->callstatus |= CIST_YPCALL;  /* function can do error recovery */
    luaD_call(L, c.func, nresults);  /* do the call */
    ci->callstatus &= ~CIST_YPCALL;
    L->errfunc = ci->u.c.old_errfunc;
    status = LUA_OK;  /* if it is here, there were no errors */
  }
  adjustresults(L, nresults);
  lua_unlock(L);
  return status;
}


LUA_API int lua_load (lua_State *L, lua_Reader reader, void *data,
                      const char *chunkname, const char *mode) {
  ZIO z;
  int status;
  lua_lock(L);
  if (!chunkname) chunkname = "?";
  luaZ_init(L, &z, reader, data);
  status = luaD_protectedparser(L, &z, chunkname, mode);
  if (status == LUA_OK) {  /* no errors? */
    LClosure *f = clLvalue(L->top - 1);  /* get newly created function */
    if (f->nupvalues >= 1) {  /* does it have an upvalue? */
      /* get global table from registry */
      Table *reg = hvalue(&G(L)->l_registry);
      const TValue *gt = luaH_getint(reg, LUA_RIDX_GLOBALS);
      /* set global table as 1st upvalue of 'f' (may be LUA_ENV) */
      setobj(L, f->upvals[0]->v, gt);
      luaC_upvalbarrier(L, f->upvals[0]);
    }
  }
  lua_unlock(L);
  return status;
}


LUA_API int lua_dump (lua_State *L, lua_Writer writer, void *data, int strip) {
  int status;
  TValue *o;
  lua_lock(L);
  api_checknelems(L, 1);
  o = L->top - 1;
  if (isLfunction(o))
    status = luaU_dump(L, getproto(o), writer, data, strip);
  else
    status = 1;
  lua_unlock(L);
  return status;
}


LUA_API int lua_status (lua_State *L) {
  return L->status;
}


/*
** Garbage-collection function
*/

LUA_API int lua_gc (lua_State *L, int what, int data) {
  int res = 0;
  global_State *g;
  lua_lock(L);
  g = G(L);
  switch (what) {
    case LUA_GCSTOP: {
      g->gcrunning = 0;
      break;
    }
    case LUA_GCRESTART: {
      luaE_setdebt(g, 0);
      g->gcrunning = 1;
      break;
    }
    case LUA_GCCOLLECT: {
      luaC_fullgc(L, 0);
      break;
    }
    case LUA_GCCOUNT: {
      /* GC values are expressed in Kbytes: #bytes/2^10 */
      res = cast_int(gettotalbytes(g) >> 10);
      break;
    }
    case LUA_GCCOUNTB: {
      res = cast_int(gettotalbytes(g) & 0x3ff);
      break;
    }
    case LUA_GCSTEP: {
      l_mem debt = 1;  /* =1 to signal that it did an actual step */
      lu_byte oldrunning = g->gcrunning;
      g->gcrunning = 1;  /* allow GC to run */
      if (data == 0) {
        luaE_setdebt(g, -GCSTEPSIZE);  /* to do a "small" step */
        luaC_step(L);
      }
      else {  /* add 'data' to total debt */
        debt = cast(l_mem, data) * 1024 + g->GCdebt;
        luaE_setdebt(g, debt);
        luaC_checkGC(L);
      }
      g->gcrunning = oldrunning;  /* restore previous state */
      if (debt > 0 && g->gcstate == GCSpause)  /* end of cycle? */
        res = 1;  /* signal it */
      break;
    }
    case LUA_GCSETPAUSE: {
      res = g->gcpause;
      g->gcpause = data;
      break;
    }
    case LUA_GCSETSTEPMUL: {
      res = g->gcstepmul;
      if (data < 40) data = 40;  /* avoid ridiculous low values (and 0) */
      g->gcstepmul = data;
      break;
    }
    case LUA_GCISRUNNING: {
      res = g->gcrunning;
      break;
    }
    default: res = -1;  /* invalid option */
  }
  lua_unlock(L);
  return res;
}



/*
** miscellaneous functions
*/


LUA_API int lua_error (lua_State *L) {
  lua_lock(L);
  api_checknelems(L, 1);
  luaG_errormsg(L);
  /* code unreachable; will unlock when control actually leaves the kernel */
  return 0;  /* to avoid warnings */
}


LUA_API int lua_next (lua_State *L, int idx) {
  StkId t;
  int more;
  lua_lock(L);
  t = index2addr(L, idx);
  api_check(L, ttistable(t), "table expected");
  more = luaH_next(L, hvalue(t), L->top - 1);
  if (more) {
    api_incr_top(L);
  }
  else  /* no more elements */
    L->top -= 1;  /* remove key */
  lua_unlock(L);
  return more;
}


LUA_API void lua_concat (lua_State *L, int n) {
  lua_lock(L);
  api_checknelems(L, n);
  if (n >= 2) {
    luaV_concat(L, n);
  }
  else if (n == 0) {  /* push empty string */
    setsvalue2s(L, L->top, luaS_newlstr(L, "", 0));
    api_incr_top(L);
  }
  /* else n == 1; nothing to do */
  luaC_checkGC(L);
  lua_unlock(L);
}


LUA_API void lua_len (lua_State *L, int idx) {
  StkId t;
  lua_lock(L);
  t = index2addr(L, idx);
  luaV_objlen(L, L->top, t);
  api_incr_top(L);
  lua_unlock(L);
}


LUA_API lua_Alloc lua_getallocf (lua_State *L, void **ud) {
  lua_Alloc f;
  lua_lock(L);
  if (ud) *ud = G(L)->ud;
  f = G(L)->frealloc;
  lua_unlock(L);
  return f;
}


LUA_API void lua_setallocf (lua_State *L, lua_Alloc f, void *ud) {
  lua_lock(L);
  G(L)->ud = ud;
  G(L)->frealloc = f;
  lua_unlock(L);
}


LUA_API void *lua_newuserdata (lua_State *L, size_t size) {
  Udata *u;
  lua_lock(L);
  u = luaS_newudata(L, size);
  setuvalue(L, L->top, u);
  api_incr_top(L);
  luaC_checkGC(L);
  lua_unlock(L);
  return getudatamem(u);
}



static const char *aux_upvalue (StkId fi, int n, TValue **val,
                                CClosure **owner, UpVal **uv) {
  switch (ttype(fi)) {
    case LUA_TCCL: {  /* C closure */
      CClosure *f = clCvalue(fi);
      if (!(1 <= n && n <= f->nupvalues)) return NULL;
      *val = &f->upvalue[n-1];
      if (owner) *owner = f;
      return "";
    }
    case LUA_TLCL: {  /* Lua closure */
      LClosure *f = clLvalue(fi);
      TString *name;
      Proto *p = f->p;
      if (!(1 <= n && n <= p->sizeupvalues)) return NULL;
      *val = f->upvals[n-1]->v;
      if (uv) *uv = f->upvals[n - 1];
      name = p->upvalues[n-1].name;
      return (name == NULL) ? "(*no name)" : getstr(name);
    }
    default: return NULL;  /* not a closure */
  }
}


LUA_API const char *lua_getupvalue (lua_State *L, int funcindex, int n) {
  const char *name;
  TValue *val = NULL;  /* to avoid warnings */
  lua_lock(L);
  name = aux_upvalue(index2addr(L, funcindex), n, &val, NULL, NULL);
  if (name) {
    setobj2s(L, L->top, val);
    api_incr_top(L);
  }
  lua_unlock(L);
  return name;
}


LUA_API const char *lua_setupvalue (lua_State *L, int funcindex, int n) {
  const char *name;
  TValue *val = NULL;  /* to avoid warnings */
  CClosure *owner = NULL;
  UpVal *uv = NULL;
  StkId fi;
  lua_lock(L);
  fi = index2addr(L, funcindex);
  api_checknelems(L, 1);
  name = aux_upvalue(fi, n, &val, &owner, &uv);
  if (name) {
    L->top--;
    setobj(L, val, L->top);
    if (owner) { luaC_barrier(L, owner, L->top); }
    else if (uv) { luaC_upvalbarrier(L, uv); }
  }
  lua_unlock(L);
  return name;
}


static UpVal **getupvalref (lua_State *L, int fidx, int n, LClosure **pf) {
  LClosure *f;
  StkId fi = index2addr(L, fidx);
  api_check(L, ttisLclosure(fi), "Lua function expected");
  f = clLvalue(fi);
  api_check(L, (1 <= n && n <= f->p->sizeupvalues), "invalid upvalue index");
  if (pf) *pf = f;
  return &f->upvals[n - 1];  /* get its upvalue pointer */
}


LUA_API void *lua_upvalueid (lua_State *L, int fidx, int n) {
  StkId fi = index2addr(L, fidx);
  switch (ttype(fi)) {
    case LUA_TLCL: {  /* lua closure */
      return *getupvalref(L, fidx, n, NULL);
    }
    case LUA_TCCL: {  /* C closure */
      CClosure *f = clCvalue(fi);
      api_check(L, 1 <= n && n <= f->nupvalues, "invalid upvalue index");
      return &f->upvalue[n - 1];
    }
    default: {
      api_check(L, 0, "closure expected");
      return NULL;
    }
  }
}


LUA_API void lua_upvaluejoin (lua_State *L, int fidx1, int n1,
                                            int fidx2, int n2) {
  LClosure *f1;
  UpVal **up1 = getupvalref(L, fidx1, n1, &f1);
  UpVal **up2 = getupvalref(L, fidx2, n2, NULL);
  luaC_upvdeccount(L, *up1);
  *up1 = *up2;
  (*up1)->refcount++;
  if (upisopen(*up1)) (*up1)->u.open.touched = 1;
  luaC_upvalbarrier(L, *up1);
}


