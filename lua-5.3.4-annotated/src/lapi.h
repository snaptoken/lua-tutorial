/*
** $Id: lapi.h,v 2.9 2015/03/06 19:49:50 roberto Exp $
** Auxiliary functions from Lua API
** See Copyright Notice in lua.h
*/

#ifndef lapi_h
#define lapi_h

// This header defines some helper macros that are mainly used in lapi.c, but
// also used in a handful of places elsewhere in the codebase.

#include "llimits.h"
#include "lstate.h"

// Increments L->top, and asserts that this is never called when L->top already
// points to the top of the stack for the current function. This is used
// whenever pushing something on top of the stack.
#define api_incr_top(L)   {L->top++; api_check(L, L->top <= L->ci->top, \
				"stack overflow");}

// Called when a function is returning. If multiple results are wanted from the
// function call (as in the case of a tail call, e.g. `return f()`), then set
// L->ci->top to L->top. (The return values start at the old stack frame's base
// and proceed up to but not including L->top.) When is L->ci->top >= L->top
// already?
#define adjustresults(L,nres) \
    { if ((nres) == LUA_MULTRET && L->ci->top < L->top) L->ci->top = L->top; }

// Used at the top of many Lua API functions, to make sure the number of
// elements on the stack for the current frame is at least `n`. Lots of API
// functions work with one or two values that are expected to be on top of the
// stack when you call them.
#define api_checknelems(L,n)	api_check(L, (n) < (L->top - L->ci->func), \
				  "not enough elements in the stack")


#endif
