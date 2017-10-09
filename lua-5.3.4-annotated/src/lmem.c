/*
** $Id: lmem.c,v 1.91 2015/03/06 19:45:54 roberto Exp $
** Interface to Memory Manager
** See Copyright Notice in lua.h
*/

#define lmem_c
#define LUA_CORE

#include "lprefix.h"


#include <stddef.h>

#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lgc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"



// This comment specifies what the behavior of the realloc function should be.
// The realloc function can be set by passing it to lua_newstate() when creating
// a new state. lauxlib.c provides a default realloc function called l_alloc()
// which uses the standard library's realloc() and free() functions. The
// luaL_newstate() function in lauxlib.c creates a lua_State with that default
// function.
//
// The realloc function is stored in the global state, the shared state of
// possibly multiple Lua threads. The first argument to it is `void *ud`, which
// stands for userdata, which can be any kind of data that is useful to the
// allocator. (It's common in C to pass a void* userdata value when calling a
// user-provided function pointer.) The default allocator doesn't use userdata.
/*
** About the realloc function:
** void * frealloc (void *ud, void *ptr, size_t osize, size_t nsize);
** ('osize' is the old size, 'nsize' is the new size)
**
** * frealloc(ud, NULL, x, s) creates a new block of size 's' (no
** matter 'x').
**
** * frealloc(ud, p, x, 0) frees the block 'p'
** (in this specific case, frealloc must return NULL);
** particularly, frealloc(ud, NULL, 0, 0) does nothing
** (which is equivalent to free(NULL) in ISO C)
**
** frealloc returns NULL if it cannot create or reallocate the area
** (any reallocation to an equal or smaller size cannot fail!)
*/



// When growing a vector, it will always grow to at least this many elements
// long. I guess this helps prevent "slow starts" when doubling the size of a
// vector from 1, to 2, to 4, etc.
#define MINSIZEARRAY	4


// Auxiliary function for the luaM_growvector() macro in lmem.h. Grows the
// vector such that there is room for at least one more element. Usually doubles
// the size of the vector, unless that would surpass the given size limit, in
// which case it sets the size to the limit. If the size is already at the
// limit, it throws a runtime error. `size` is an in-out parameter: it specifies
// the current size of the vector, and will be set to the new size on success.
void *luaM_growaux_ (lua_State *L, void *block, int *size, size_t size_elems,
                     int limit, const char *what) {
  void *newblock;
  int newsize;
  // Avoid overflow by dividing limit by 2, instead of doing
  // `(*size) * 2 >= limit`.
  if (*size >= limit/2) {  /* cannot double it? */
    if (*size >= limit)  /* cannot grow even a little? */
      // This function doesn't return (it throws an error).
      luaG_runerror(L, "too many %s (limit is %d)", what, limit);
    newsize = limit;  /* still have at least one free place */
  }
  else {
    newsize = (*size)*2;
    if (newsize < MINSIZEARRAY)
      newsize = MINSIZEARRAY;  /* minimum size */
  }
  newblock = luaM_reallocv(L, block, *size, newsize, size_elems);
  *size = newsize;  /* update only when everything else is OK */
  return newblock;
}


// Throws a runtime error when the number of bytes being allocated wouldn't fit
// in a size_t. (Used by lmem.h:luaM_reallocv(). Also randomly used in
// lstring.c when allocating strings/userdata.)
l_noret luaM_toobig (lua_State *L) {
  luaG_runerror(L, "memory allocation error: block too big");
}



/*
** generic allocation routine.
*/
// Used by multiple macros in lmem.h. The macros are public, and this function
// is intended to be private to this module.
void *luaM_realloc_ (lua_State *L, void *block, size_t osize, size_t nsize) {
  void *newblock;
  global_State *g = G(L);
  // luaM_newobject() passes the type tag of the object being allocated as the
  // `osize` argument, for some reason. This makes sure the "real" osize is 0
  // when `block` is NULL (which is the case when allocating a new object).
  size_t realosize = (block) ? osize : 0;
  // The above line ensures that realosize is 0 when block is NULL. So here
  // we're also asserting that block is NULL when osize is 0.
  lua_assert((realosize == 0) == (block == NULL));
  // Hard memory tests are when we run a full garbage collection cycle as often
  // as possible. (Whenever a GC cycle *might* be triggered during ordinary
  // execution?)
#if defined(HARDMEMTESTS)
  // Run GC cycle if we're allocating more memory, and if GC hasn't been turned
  // off.
  if (nsize > realosize && g->gcrunning)
    luaC_fullgc(L, 1);  /* force a GC whenever possible */
#endif
  // Call the realloc function stored in the global state.
  newblock = (*g->frealloc)(g->ud, block, osize, nsize);
  // If we were trying to allocate memory (rather than free), and the realloc
  // function returned NULL, that means it failed due to there not being enough
  // memory. So we'll do a garbage collection cycle to hopefully free up some
  // memory and try again.
  if (newblock == NULL && nsize > 0) {
    lua_assert(nsize > realosize);  /* cannot fail when shrinking a block */
    // When a lua_State is initialized, it has to allocate a bunch of
    // garbage-collectable objects as part of initializing the state. When it's
    // done that, it sets g->version. (See `lstate.c:f_luaopen()`.) We don't
    // attempt a GC cycle if the out of memory error occurred while building the
    // state. (Is that because there shouldn't be any garbage memory to collect,
    // or because GC simply cannot start before the state is initialized?)
    if (g->version) {  /* is state fully built? */
      // The `1` makes it run a GC cycle in "emergency mode", meaning finalizers
      // won't be run (yet). I guess finalizers tend to cause more memory to be
      // allocated, which we really want to avoid because we've already run into
      // one out-of-memory error...
      luaC_fullgc(L, 1);  /* try to free some memory... */
      newblock = (*g->frealloc)(g->ud, block, osize, nsize);  /* try again */
    }
    // If it still failed, throw a Lua error.
    if (newblock == NULL)
      luaD_throw(L, LUA_ERRMEM);
  }
  // If we freed an object, make sure the realloc function returned NULL.
  // Otherwise, make sure it returned non-NULL.
  lua_assert((nsize == 0) == (newblock == NULL));
  // Update the GC debt value to the net number of bytes we just allocated or
  // freed.
  g->GCdebt = (g->GCdebt + nsize) - realosize;
  return newblock;
}

