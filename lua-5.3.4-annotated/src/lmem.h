/*
** $Id: lmem.h,v 1.43 2014/12/19 17:26:14 roberto Exp $
** Interface to Memory Manager
** See Copyright Notice in lua.h
*/

#ifndef lmem_h
#define lmem_h


#include <stddef.h>

#include "llimits.h"
#include "lua.h"

// All of the macros in this file are public interfaces to the private functions
// luaM_realloc_() and luaM_growaux_().

// This macro is only used by the following macros in this file, and
// lmem.c:luaM_growaux_(). It just calls luaM_realloc_() after doing some tricky
// overflow checking, explained below.
/*
** This macro reallocs a vector 'b' from 'on' to 'n' elements, where
** each element has size 'e'. In case of arithmetic overflow of the
** product 'n'*'e', it raises an error (calling 'luaM_toobig'). Because
** 'e' is always constant, it avoids the runtime division MAX_SIZET/(e).
**
** (The macro is somewhat complex to avoid warnings:  The 'sizeof'
** comparison avoids a runtime comparison when overflow cannot occur.
** The compiler should be able to optimize the real test by itself, but
** when it does it, it may give a warning about "comparison is always
** false due to limited range of data type"; the +1 tricks the compiler,
** avoiding this warning but also this optimization.)
*/
// `n` stands for "number of elements", `on` stands for "old n[umber of
// elements]", and `e` stands for "element size". `b` stands for "block", and is
// the pointer to the block of memory being allocated.
//
// lmem.c:luaM_toobig() throws a runtime error.
#define luaM_reallocv(L,b,on,n,e) \
  (((sizeof(n) >= sizeof(size_t) && cast(size_t, (n)) + 1 > MAX_SIZET/(e)) \
      ? luaM_toobig(L) : cast_void(0)) , \
   luaM_realloc_(L, (b), (on)*(e), (n)*(e)))

/*
** Arrays of chars do not need any test
*/
// Doesn't use luaM_reallocv() because with chars there's no chance of overflow.
// Is that true even when chars are more than one byte long?
#define luaM_reallocvchar(L,b,on,n)  \
    cast(char *, luaM_realloc_(L, (b), (on)*sizeof(char), (n)*sizeof(char)))

// The free() functions call luaM_realloc_() with an `n` of 0.

// Free a block of memory that was `s` bytes long. Used in lgc.c in a couple
// places.
#define luaM_freemem(L, b, s)	luaM_realloc_(L, (b), (s), 0)
// Free an object. The size of the object is gotten from the pointer type of
// `b`.
#define luaM_free(L, b)		luaM_realloc_(L, (b), sizeof(*(b)), 0)
// Free an array of `n` objects. The size of each object is gotten from the
// pointer type of `b`.
#define luaM_freearray(L, b, n)   luaM_realloc_(L, (b), (n)*sizeof(*(b)), 0)

// New blocks of memory are allocated by passing a NULL pointer as the `block`,
// and an "old n" of 0.

// This macro is only used in defining the luaM_new() macro directly below it.
#define luaM_malloc(L,s)	luaM_realloc_(L, NULL, 0, (s))
// Allocates memory for a new object. `t` is the type of the object, e.g. UpVal.
#define luaM_new(L,t)		cast(t *, luaM_malloc(L, sizeof(t)))
// Allocates memory for a vector of `n` objects of type `t`. This uses
// luaM_reallocv() to check for overflow.
#define luaM_newvector(L,n,t) \
		cast(t *, luaM_reallocv(L, NULL, 0, n, sizeof(t)))

// This is used in lgc.c:luaC_newobj(), which is how most Lua objects are
// allocated. For some reason it takes the type tag of the object (with no
// variant bits), and passes that as the "old n" argument. The "old n" argument
// is generally unused when allocating a new object (which is indicated by the
// the NULL argument). The type tag is passed along to the external allocator
// (which is generally the standard library's `realloc()` but can be overridden)
// so I guess you could write a special allocator for Lua that takes the type of
// object being allocated into account? Or maybe it can be used for
// testing/debugging?
#define luaM_newobject(L,tag,s)	luaM_realloc_(L, NULL, tag, (s))

// Here's an example call to luaM_growvector():
//
// luaM_growvector(L, f->p, fs->np, f->sizep, Proto *, MAXARG_Bx, "functions");
//
// It grows the vector (`f->p`) by a factor of 2 if there isn't enough space to
// hold 1 more element than `fs->np`. `f->sizep` points to the current size of
// the vector, and will be updated with the new size of the vector. `MAXARG_Bx`
// is the `limit`, and `"functions"` is a human-readable description of the
// array used to generate an error message if the array has hit the given limit.
// In this example, the number of function prototypes accessible to the
// `OP_CLOSURE` VM instruction is limited by the Bx argument of that
// instruction. If the limit is hit, the user will get an error saying "too many
// functions (limit is 262143)".
#define luaM_growvector(L,v,nelems,size,t,limit,e) \
          if ((nelems)+1 > (size)) \
            ((v)=cast(t *, luaM_growaux_(L,v,&(size),sizeof(t),limit,e)))

// It looks like this is used when we're done growing a vector incrementally,
// and we want to specify the exact size of the vector being used so no memory
// is wasted (because luaM_growvector() keeps multiplying the size by 2)? See
// lparser.c:close_func() for example.
#define luaM_reallocvector(L, v,oldn,n,t) \
   ((v)=cast(t *, luaM_reallocv(L, v, oldn, n, sizeof(t))))

LUAI_FUNC l_noret luaM_toobig (lua_State *L);

/* not to be called directly */
LUAI_FUNC void *luaM_realloc_ (lua_State *L, void *block, size_t oldsize,
                                                          size_t size);
LUAI_FUNC void *luaM_growaux_ (lua_State *L, void *block, int *size,
                               size_t size_elem, int limit,
                               const char *what);

#endif

