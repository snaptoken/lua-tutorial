/*
** $Id: lstring.c,v 2.56 2015/11/23 11:32:51 roberto Exp $
** String table (keeps all strings handled by Lua)
** See Copyright Notice in lua.h
*/

#define lstring_c
#define LUA_CORE

#include "lprefix.h"


#include <string.h>

#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"


// This error message will be used when memory allocation isn't working as a
// result of running out of memory. In that situation, we want to handle the
// error delicately so as not to try doing more memory allocations in the course
// of handling the error. So this string will be pre-allocated in luaS_init()
// below, and stored in Lua's global state.
#define MEMERRMSG       "not enough memory"


/*
** Lua will use at most ~(2^LUAI_HASHLIMIT) bytes from a string to
** compute its hash
*/
// Used by luaS_hash() to decide the "step length" when hashing a string. If the
// step length is 1, it incorporates every character of the string to compute
// the hash. If the step length is 2, it only looks at every second character.
// And so on. With a hash limit of 5, the step length will be 1 for strings up
// to 31 characters long, and will be 2 or more for strings that are larger.
#if !defined(LUAI_HASHLIMIT)
#define LUAI_HASHLIMIT		5
#endif


/*
** equality for long strings
*/
// Long strings are compared byte-by-byte. Except we first check if the
// `TString*`s point to the same object, and then we check if their lengths are
// different. If the lengths are the same, we use memcmp() on the entire
// strings.
int luaS_eqlngstr (TString *a, TString *b) {
  size_t len = a->u.lnglen;
  // This function should only be passed two long strings. By the way, it
  // doesn't make sense to compare a short string and a long string, because ALL
  // strings with lengths up to LUAI_MAXSHORTLEN (40 by default) are short, and
  // all strings longer than that are long.
  lua_assert(a->tt == LUA_TLNGSTR && b->tt == LUA_TLNGSTR);
  return (a == b) ||  /* same instance or... */
    ((len == b->u.lnglen) &&  /* equal length and ... */
     (memcmp(getstr(a), getstr(b), len) == 0));  /* equal contents */
}


// Compute the hash of a string, based on the contents of the given string, and
// the `seed` value which comes from the `seed` in Lua's global state, which is
// some random data that makes hashes for a given lua instance unpredictable and
// random.
unsigned int luaS_hash (const char *str, size_t l, unsigned int seed) {
  // The initial hash is the seed XOR'd with the string's length.
  unsigned int h = seed ^ cast(unsigned int, l);
  // Set the step value to 1 for strings up to 31 chars in length, to 2 for
  // strings from 32-63 chars in length, and so on. If step is 1, we'll mix
  // every char in the string into our hash. If step is 2, we'll mix every
  // second char in the string into our hash. And so on. This limits the amount
  // of CPU power used to compute each hash, which could otherwise become quite
  // a bottleneck.
  size_t step = (l >> LUAI_HASHLIMIT) + 1;
  // Start with the last char of the string, and step backwards.
  for (; l >= step; l -= step)
    // Mix up the bits of the current char with the bits of the current hash!
    h ^= ((h<<5) + (h>>2) + cast_byte(str[l - 1]));
  return h;
}


// Long strings are expensive to hash, so their hash's are lazily hashed only
// when required. (New short strings compute their hash on creation.)
unsigned int luaS_hashlongstr (TString *ts) {
  lua_assert(ts->tt == LUA_TLNGSTR);
  // For a long string, the `extra` field is a boolean keeping track of whether
  // the hash has been computed and cached yet.
  if (ts->extra == 0) {  /* no hash? */
    // If it hasn't, then hash it. The current value of `ts->hash` is the `seed`
    // from the global lua state (see luaS_createlngstrobj() below), so we can
    // just pass that in as the seed.
    ts->hash = luaS_hash(getstr(ts), ts->u.lnglen, ts->hash);
    ts->extra = 1;  /* now it has its hash */
  }
  return ts->hash;
}


/*
** resizes the string table
*/
// The string table is a hash table where all interned strings are stored, keyed
// by their hash. It's stored in the `strt` field of a lua global state. The
// data structure is `stringtable`, defined in lstate.h.
//
// This sets the number of buckets in the string table. Colliding hashes get
// chained in a linked list, using the `hnext` field of TString.
//
// This is used by luaS_init() to set the initial size of the string table; by
// internshrstr() to double the size of the string table when it seems good to
// do so; and by checkSizes() in lgc.c to shrink the string table during the
// finalization step of garbage collection.
void luaS_resize (lua_State *L, int newsize) {
  int i;
  stringtable *tb = &G(L)->strt;
  // realloc the vector of buckets if the table needs to grow. If the newsize is
  // smaller than the current size, we'll realloc to shrink it at the end of
  // this function, after we've rehashed everything so that nothing is in the
  // upper buckets.
  if (newsize > tb->size) {  /* grow table if needed */
    luaM_reallocvector(L, tb->hash, tb->size, newsize, TString *);
    // Initialize new buckets to NULL.
    for (i = tb->size; i < newsize; i++)
      tb->hash[i] = NULL;
  }
  // Rehash the table, in place. For each bucket, we'll disconnect the chain of
  // hashes from that bucket and then re-insert all of them according to the new
  // table size. This means that if a hash is re-inserted into a later position,
  // we will rehash it again when we come to that bucket. So there will be some
  // duplication of work, which I guess must be preferable to inserting the
  // hashes into a brand new string table and then switching over to that table.
  for (i = 0; i < tb->size; i++) {  /* rehash */
    TString *p = tb->hash[i];
    // Disconnect the `p` linked list from the table.
    tb->hash[i] = NULL;
    // Now consume that linked list, re-inserting each interned string into its
    // new place in the hash.
    while (p) {  /* for each node in the list */
      TString *hnext = p->u.hnext;  /* save next */
      // lmod() is defined in lobject.h. Note that it requires `newsize` to be a
      // power of 2! So that's why all calls to luaS_resize() multiply or divide
      // the size by 2.
      unsigned int h = lmod(p->hash, newsize);  /* new position */
      // Make it the new head of the linked list for its new bucket.
      p->u.hnext = tb->hash[h];  /* chain it */
      tb->hash[h] = p;
      // Move to the next element of the list we are currently consuming.
      p = hnext;
    }
  }
  // Now that everything's been rehashed to only use `newsize` buckets, if the
  // old size was larger we can safely shrink the array down.
  if (newsize < tb->size) {  /* shrink table if needed */
    /* vanishing slice should be empty */
    // Ensure that the first and last buckets of the now unused part of the
    // vector are empty.
    lua_assert(tb->hash[newsize] == NULL && tb->hash[tb->size - 1] == NULL);
    luaM_reallocvector(L, tb->hash, tb->size, newsize, TString *);
  }
  tb->size = newsize;
}


/*
** Clear API string cache. (Entries cannot be empty, so fill them with
** a non-collectable string.)
*/
// The string cache is different from the string table. Both are stored in the
// global_State. But the string cache is a 2D array of many buckets, where each
// bucket just contains a couple strings. The string cache is used to cache lua
// strings constructed from C string literals, strings used by the Lua API. The
// caching occurs in luaS_new() below. Strings are keyed simply by their memory
// address. This function clears all strings that are marked to be garbage
// collected from the cache, by resetting the slots to hold a fixed (never
// garbage collected) string, g->memerrmsg.
//
// This is called in the garbage collector's atomic phase (see
// `lgc.c:atomic()`).
void luaS_clearcache (global_State *g) {
  int i, j;
  for (i = 0; i < STRCACHE_N; i++)
    for (j = 0; j < STRCACHE_M; j++) {
    if (iswhite(g->strcache[i][j]))  /* will entry be collected? */
      g->strcache[i][j] = g->memerrmsg;  /* replace it with something fixed */
    }
}


/*
** Initialize the string table and the string cache
*/
// Called by lstate.c:f_luaopen() when initializing a new lua_State.
void luaS_init (lua_State *L) {
  global_State *g = G(L);
  int i, j;
  luaS_resize(L, MINSTRTABSIZE);  /* initial size of string table */
  /* pre-create memory-error message */
  g->memerrmsg = luaS_newliteral(L, MEMERRMSG);
  // Removes memerrmsg from the allgc list and links it into the fixedgc list,
  // so that it will never be garbage collected.
  luaC_fix(L, obj2gco(g->memerrmsg));  /* it should never be collected */
  // g->memerrmsg can be conveniently used to fill the cache with valid strings.
  // Being able to assume it always contains valid strings simplifies its usage.
  for (i = 0; i < STRCACHE_N; i++)  /* fill cache with valid strings */
    for (j = 0; j < STRCACHE_M; j++)
      g->strcache[i][j] = g->memerrmsg;
}



/*
** creates a new string object
*/
// Called by luaS_createlngstrobj() and luaS_internshrstr() below. So this
// initializes what long strings and short strings have in common. `h` should
// contain the string's computed hash for short strings, and the global `seed`
// for long strings. `tag` should be either `LUA_TLNGSTR` or `LUATSHRSTR`.
static TString *createstrobj (lua_State *L, size_t l, int tag, unsigned int h) {
  TString *ts;
  GCObject *o;
  size_t totalsize;  /* total size of TString object */
  totalsize = sizelstring(l);
  // Allocate new garbage-collected object.
  o = luaC_newobj(L, tag, totalsize);
  // Cast it to a TString*.
  ts = gco2ts(o);
  // Set the hash/seed.
  ts->hash = h;
  // Mark it as not a reserved word (for short strings) or not yet hashed (for
  // long strings).
  ts->extra = 0;
  // Terminate the string data with a nul char. (Before we even copy the string
  // data itself in.)
  getstr(ts)[l] = '\0';  /* ending 0 */
  return ts;
}


// Initializes a long string TString struct, ready for string data to be copied
// in. Used by luaS_newlstr() below. Also used in lundump.c:LoadString() and
// lvm.c:luaV_concat() (which implements the `..` operator).
TString *luaS_createlngstrobj (lua_State *L, size_t l) {
  // Pass global seed as the hash, so when the hash is lazily computed, it can
  // use the seed to compute the hash.
  TString *ts = createstrobj(L, l, LUA_TLNGSTR, G(L)->seed);
  ts->u.lnglen = l;
  return ts;
}


// Remove a string from the string table (by reference). Used by lgc.c:freeobj()
// when garbage collecting strings.
void luaS_remove (lua_State *L, TString *ts) {
  stringtable *tb = &G(L)->strt;
  // Find the right bucket.
  TString **p = &tb->hash[lmod(ts->hash, tb->size)];
  // Unlink the string from the chain. Notice that if the string doesn't exist
  // in the chain, this could become an infinite loop. We are assuming the
  // caller knows that the string has to be interned in the string table.
  while (*p != ts)  /* find previous element */
    p = &(*p)->u.hnext;
  *p = (*p)->u.hnext;  /* remove element from its list */
  // Keep track of how many strings are in the string table.
  tb->nuse--;
}


/*
** checks whether short string exists and reuses it or creates a new one
*/
// Auxiliary function for luaS_newlstr() below.
static TString *internshrstr (lua_State *L, const char *str, size_t l) {
  TString *ts;
  global_State *g = G(L);
  // First, hash the incoming string.
  unsigned int h = luaS_hash(str, l, g->seed);
  // Find the bucket it belongs in in the string table.
  TString **list = &g->strt.hash[lmod(h, g->strt.size)];
  // Ensure we are avoiding undefined behavior.
  lua_assert(str != NULL);  /* otherwise 'memcmp'/'memcpy' are undefined */
  // Loop through the linked list to see if the string has already been
  // interned.
  for (ts = *list; ts != NULL; ts = ts->u.hnext) {
    // Check for equal length, then do the more expensive memcmp() to check for
    // string equality.
    if (l == ts->shrlen &&
        (memcmp(str, getstr(ts), l * sizeof(char)) == 0)) {
      /* found! */
      // If it's marked as dead by the garbage collector, but not collected yet,
      // mark it as alive since there is a reference to it now.
      if (isdead(g, ts))  /* dead (but not collected yet)? */
        changewhite(ts);  /* resurrect it */
      // And simply return it.
      return ts;
    }
  }
  // If we got here, then the string doesn't exist in the string table, so we
  // will add it to the string table. First, check if the string table is
  // getting full (there should ideally be as many buckets as there are strings
  // in the table) and resize it if so.
  if (g->strt.nuse >= g->strt.size && g->strt.size <= MAX_INT/2) {
    luaS_resize(L, g->strt.size * 2);
    // The bucket the string belongs to probably changed now that the table size
    // has changed, so recompute what bucket to use.
    list = &g->strt.hash[lmod(h, g->strt.size)];  /* recompute with new size */
  }
  // Allocate a new short string with the proper length.
  ts = createstrobj(L, l, LUA_TSHRSTR, h);
  // Copy the string data into it.
  memcpy(getstr(ts), str, l * sizeof(char));
  // Set the string length.
  ts->shrlen = cast_byte(l);
  // Link the new string to the top of its bucket (head of the list) in the
  // string table.
  ts->u.hnext = *list;
  *list = ts;
  // Keep track of how many strings are actually in the string table.
  g->strt.nuse++;
  return ts;
}


/*
** new string (with explicit length)
*/
// Used by luaS_new() below. Used in a few other places in Lua. Avoids the
// strlen() call that luaS_new() does, but more importantly this function
// doesn't use the string cache like luaS_new() does. So I believe this function
// is used to create most strings from actual Lua programs, whereas luaS_new()
// is used more internally within the Lua API and by C code that uses the Lua
// API. Correct?
TString *luaS_newlstr (lua_State *L, const char *str, size_t l) {
  // If it's a short string (40 characters or less, by default), intern it!
  if (l <= LUAI_MAXSHORTLEN)  /* short string? */
    return internshrstr(L, str, l);
  else {
    // Otherwise create a long string out of it.
    TString *ts;
    // Make sure the number of bytes to be allocated won't overflow a size_t.
    if (l >= (MAX_SIZE - sizeof(TString))/sizeof(char))
      luaM_toobig(L);
    // Allocate the TString, initializing everything except the string data.
    ts = luaS_createlngstrobj(L, l);
    // Copy in the string data.
    memcpy(getstr(ts), str, l * sizeof(char));
    return ts;
  }
}


/*
** Create or reuse a zero-terminated string, first checking in the
** cache (using the string address as a key). The cache can contain
** only zero-terminated strings, so it is safe to use 'strcmp' to
** check hits.
*/
TString *luaS_new (lua_State *L, const char *str) {
  // The string cache simply uses the string pointer as the hash. `str` is
  // assumed to be a string literal, right?
  unsigned int i = point2uint(str) % STRCACHE_N;  /* hash */
  int j;
  // This is the "bucket" we'll look through.
  TString **p = G(L)->strcache[i];
  // Look at each string in the bucket. STRCACHE_M is defined to be 2, so there
  // are only 2 strings to look at in each bucket. The string cache always
  // contains valid strings, using "dummy" strings (actually `g->memerrmsg`) for
  // "blank" slots.
  for (j = 0; j < STRCACHE_M; j++) {
    // Just do a strcmp() on each string till we find a match. We can't compare
    // lengths because these are C strings we're working with (well the input to
    // this function is at least). Clearly luaS_new() is not meant to be used
    // with long strings, just short string literals used within the C API and
    // Lua internals.
    if (strcmp(str, getstr(p[j])) == 0)  /* hit? */
      return p[j];  /* that is it */
  }
  /* normal route */
  // If it wasn't in the cache, make room in its cache bucket by bumping out the
  // last (oldest) element.
  for (j = STRCACHE_M - 1; j > 0; j--)
    p[j] = p[j - 1];  /* move out last element */
  /* new element is first in the list */
  // Create the string (note that it might be interned but not in the string
  // cache; they're two separate things) and put it in the first slot in its
  // bucket in the string cache.
  p[0] = luaS_newlstr(L, str, strlen(str));
  return p[0];
}


// Lonely little Udata constructor down here. Similar to createstrobj() above.
// Initializes everything except the userdata itself.
Udata *luaS_newudata (lua_State *L, size_t s) {
  Udata *u;
  GCObject *o;
  // Check if the number of bytes to allocate would overflow a size_t.
  if (s > MAX_SIZE - sizeof(Udata))
    luaM_toobig(L);
  // Allocate the struct, plus the space needed for the data itself.
  o = luaC_newobj(L, LUA_TUSERDATA, sizeludata(s));
  // Cast it to a Udata*.
  u = gco2u(o);
  // Set the length of the data in bytes.
  u->len = s;
  // No metatable to start out with.
  u->metatable = NULL;
  // Set the uservalue to nil by default. What is this used for again? Wrapping
  // a basic Lua type so it can have its own metatable?
  setuservalue(L, u, luaO_nilobject);
  return u;
}

