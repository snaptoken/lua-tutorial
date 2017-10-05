/*
** $Id: lobject.h,v 2.117 2016/08/01 19:51:24 roberto Exp $
** Type definitions for Lua objects
** See Copyright Notice in lua.h
*/


#ifndef lobject_h
#define lobject_h


#include <stdarg.h>


#include "llimits.h"
#include "lua.h"

// Lua values are represented as a type tag followed by the actual data. The
// type tag specifies the object's basic type, a possible variant of the basic
// type, and whether the object is garbage collected.

/*
** Extra tags for non-values
*/
// "Normal" type tags are defined in lua.h, under the heading "basic types".
// They live there so that they're available in the Lua API. These extra tags
// are for internal use and so are private.
#define LUA_TPROTO	LUA_NUMTAGS		/* function prototypes */
// LUA_NUMTAGS is the number of "basic types", and is defined along with the
// basic type tags in lua.h. LUA_TPROTO is the type tag for Lua function
// prototypes, of which there is one for each lexical function block that is
// parsed. Why are function prototypes represented as tagged values? (Because
// they are garbage collected?)
#define LUA_TDEADKEY	(LUA_NUMTAGS+1)		/* removed keys in tables */
// This one is used more as a sentinel value, for tables with weak references
// that get garbage collected. See lgc.c:removeentry().

/*
** number of all possible tags (including LUA_TNONE but excluding DEADKEY)
*/
// LUA_TNONE is defined as -1 in lua.h. LUA_TOTALTAGS is used when defining the
// array ltm.c:luaT_typenames_ which maps type tags to string type names.
#define LUA_TOTALTAGS	(LUA_TPROTO + 2)


/*
** tags for Tagged Values have the following use of bits:
** bits 0-3: actual tag (a LUA_T* value)
** bits 4-5: variant bits
** bit 6: whether value is collectable
*/
// The collectable bit is set iff the type is a garbage collected type. It does
// NOT mean "this value can now be garbage collected" or anything. (Right?)


// Some Lua types have variants: different bits are set in the type tag
// depending on what *kind* of function, string, or number it is. For the most
// part, variants of types aren't visible to users of the Lua language.

/*
** LUA_TFUNCTION variants:
** 0 - Lua function
** 1 - light C function
** 2 - regular C function (closure)
*/

/* Variant tags for functions */
#define LUA_TLCL	(LUA_TFUNCTION | (0 << 4))  /* Lua closure */
// LUA_TLCL stands for LUA Type: Lua CLosure. A closure instantiated from a
// function prototype, with upvalues. Garbage collected.
#define LUA_TLCF	(LUA_TFUNCTION | (1 << 4))  /* light C function */
// LUA_TLCF stands for LUA Type: Light C Function. This is represented simply as
// a pointer to the C function.
#define LUA_TCCL	(LUA_TFUNCTION | (2 << 4))  /* C closure */
// LUA_TCCL stands for LUA Type: C CLosure. Contains a pointer to a C function,
// and also an array of upvalues. Garbage collected.

// It takes a while to get to know the acronyms LCL, LCF, and CCL...


/* Variant tags for strings */
#define LUA_TSHRSTR	(LUA_TSTRING | (0 << 4))  /* short strings */
// Strings up to a certain length are interned, so that two separately
// instantiated strings with the same contents will point to the same garbage
// collected object. What is the max length of a short string?
#define LUA_TLNGSTR	(LUA_TSTRING | (1 << 4))  /* long strings */
// Long strings aren't interned, and are treated a little differently for
// performance reasons.


/* Variant tags for numbers */
// Lua numbers can be stored internally as floats or ints, and can be converted
// back and forth implicitly. What are the general rules for when a number is an
// int and when it's a float?
#define LUA_TNUMFLT	(LUA_TNUMBER | (0 << 4))  /* float numbers */
#define LUA_TNUMINT	(LUA_TNUMBER | (1 << 4))  /* integer numbers */


/* Bit mark for collectable types */
// This bit is used in garbage collection code to make sure it doesn't try to
// garbage collect noncollectable types (it's often used in asserts).
#define BIT_ISCOLLECTABLE	(1 << 6)

/* mark a tag as collectable */
// Helper used in macros for type-checking, later in this file.
#define ctb(t)			((t) | BIT_ISCOLLECTABLE)


/*
** Common type for all collectable objects
*/
// This struct defines the fields that will be at the beginning of every
// garbage-collectable type's struct.
typedef struct GCObject GCObject;


/*
** Common Header for all collectable objects (in macro form, to be
** included in other objects)
*/
// Every garbage-collectable type's struct will have "CommonHeader;" at the top,
// giving it these three fields. The 'next' pointer is used to link together all
// garbage collectable objects in a giant linked list, which is traversed in the
// sweep phase of garbage collection. The 'tt' field is the type tag of the
// value, used internally by the garbage collector when traversing the linked
// list of GCObjects. It needs to know what type each GCObject is so that it
// knows how to traverse that object (e.g. if it's a table it then loops over
// the key-value pairs of the table and recurses). Finally, the 'marked' field
// is used in the mark phase of garbage collection to record that the object is
// referenced and should not be collected in the sweep phase.
#define CommonHeader	GCObject *next; lu_byte tt; lu_byte marked


/*
** Common type has only the common header
*/
struct GCObject {
  CommonHeader;
};




/*
** Tagged Values. This is the basic representation of values in Lua,
** an actual value plus a tag with its type.
*/

/*
** Union of all Lua values
*/
// The data part of a tagged value. Depending on the type, the data is stored
// and accessed as one of the following types.
typedef union Value {
  GCObject *gc;    /* collectable objects */
  void *p;         /* light userdata */
  int b;           /* booleans */
  lua_CFunction f; /* light C functions */
  lua_Integer i;   /* integer numbers */
  lua_Number n;    /* float numbers */
  // lua_CFunction, lua_Integer, and lua_Number are defined in lua.h, and can be
  // configured in luaconf.h. For example, lua_Integer might be typedef'd as
  // long int, and lua_Number as double.
} Value;


// Similar to CommonHeader above, the fields of a TValue are defined as a
// TValuefields macro, which can then be inserted into a struct definition. This
// is used in the definition of TKey later in this file, as well as the TValue
// definition below.
#define TValuefields	Value value_; int tt_


// TValue: A Tagged Value. A Value + a type tag. This is the thing that gets
// stored in your Lua variables and passed around to functions and so on.
typedef struct lua_TValue {
  TValuefields;
} TValue;



/* macro defining a nil value */
// Nil in Lua is represented as having the LUA_TNIL type tag (which happens to
// be 0) and a NULL Value. NILCONSTANT is used to instantiate luaO_nilobject_ in
// lobject.c, as well as dummynode_ in ltable.c.
#define NILCONSTANT	{NULL}, LUA_TNIL


// Helper to get the Value of a TValue. Used below in this file only.
#define val_(o)		((o)->value_)


/* raw type tag of a TValue */
// Helper to get the type tag of a TValue. Used below in this file only.
#define rttype(o)	((o)->tt_)

/* tag with no variants (bits 0-3) */
// Helper used below, and in a couple other files as well. Notice it strips the
// ISCOLLECTABLE bit, as well as the variant bits.
#define novariant(x)	((x) & 0x0F)

/* type tag of a TValue (bits 0-3 for tags + variant bits 4-5) */
// Helper that strips the ISCOLLECTABLE bit, used below for type tag
// comparisons. Used in quite a few other files too.
#define ttype(o)	(rttype(o) & 0x3F)

/* type tag of a TValue with no variants (bits 0-3) */
// Type Tag, NO Variant. Takes a TValue and returns the basic type tag. This
// one's also used in other files.
#define ttnov(o)	(novariant(rttype(o)))


// Now to put all the above helpers to use: Macros to check if a TValue is a
// certain type.
/* Macros to test type */
// checktag() compares the entire raw type tag of a TValue, including variant
// bits and the ISCOLLECTABLE bit.
#define checktag(o,t)		(rttype(o) == (t))
// checktype() only compares the basic type tag of a TValue (bits 0-3).
#define checktype(o,t)		(ttnov(o) == (t))
#define ttisnumber(o)		checktype((o), LUA_TNUMBER)
#define ttisfloat(o)		checktag((o), LUA_TNUMFLT)
#define ttisinteger(o)		checktag((o), LUA_TNUMINT)
#define ttisnil(o)		checktag((o), LUA_TNIL)
#define ttisboolean(o)		checktag((o), LUA_TBOOLEAN)
#define ttislightuserdata(o)	checktag((o), LUA_TLIGHTUSERDATA)
#define ttisstring(o)		checktype((o), LUA_TSTRING)
// Use ctb() to set the ISCOLLECTABLE bit to 1 for type tags of collectable
// types, before comparing.
#define ttisshrstring(o)	checktag((o), ctb(LUA_TSHRSTR))
#define ttislngstring(o)	checktag((o), ctb(LUA_TLNGSTR))
#define ttistable(o)		checktag((o), ctb(LUA_TTABLE))
#define ttisfunction(o)		checktype(o, LUA_TFUNCTION)
// This checks that bit 5 is 0, in addition to bits 0-3 equalling LUA_TFUNCTION.
// If so, then it's either a Lua closure (LUA_TLCL) or a C closure (LUA_TCCL).
// If bit 5 is 1, then it's just a light C function (LUA_TLCF), and isn't a
// closure.
#define ttisclosure(o)		((rttype(o) & 0x1F) == LUA_TFUNCTION)
#define ttisCclosure(o)		checktag((o), ctb(LUA_TCCL))
#define ttisLclosure(o)		checktag((o), ctb(LUA_TLCL))
#define ttislcf(o)		checktag((o), LUA_TLCF)
#define ttisfulluserdata(o)	checktag((o), ctb(LUA_TUSERDATA))
#define ttisthread(o)		checktag((o), ctb(LUA_TTHREAD))
#define ttisdeadkey(o)		checktag((o), LUA_TDEADKEY)


// Accessors into TValue's. Once you know what type a TValue holds, you can use
// one of these macros to get the value. This involves selecting a field from
// the Value union, and if it's a GCObject, casting the GCObject pointer to the
// struct for that TValue's type.
//
// check_exp() is used in these macros to ensure each accessor is used on the
// right type of TValue. check_exp() is defined in llimits.h and has no effect
// if asserts are turned off.
/* Macros to access values */
#define ivalue(o)	check_exp(ttisinteger(o), val_(o).i)
#define fltvalue(o)	check_exp(ttisfloat(o), val_(o).n)
// cast_num() is a macro defined in llimits.h. It's just cleaner syntax for
// (lua_Number)(ivalue(o)).
#define nvalue(o)	check_exp(ttisnumber(o), \
	(ttisinteger(o) ? cast_num(ivalue(o)) : fltvalue(o)))
#define gcvalue(o)	check_exp(iscollectable(o), val_(o).gc)
#define pvalue(o)	check_exp(ttislightuserdata(o), val_(o).p)
// The gco2xxx() macros are defined in lstate.h. They just cast a GCObject
// pointer to the specified struct type.
#define tsvalue(o)	check_exp(ttisstring(o), gco2ts(val_(o).gc))
#define uvalue(o)	check_exp(ttisfulluserdata(o), gco2u(val_(o).gc))
#define clvalue(o)	check_exp(ttisclosure(o), gco2cl(val_(o).gc))
#define clLvalue(o)	check_exp(ttisLclosure(o), gco2lcl(val_(o).gc))
#define clCvalue(o)	check_exp(ttisCclosure(o), gco2ccl(val_(o).gc))
#define fvalue(o)	check_exp(ttislcf(o), val_(o).f)
#define hvalue(o)	check_exp(ttistable(o), gco2t(val_(o).gc))
#define bvalue(o)	check_exp(ttisboolean(o), val_(o).b)
#define thvalue(o)	check_exp(ttisthread(o), gco2th(val_(o).gc))
/* a dead value may get the 'gc' field, but cannot access its contents */
// So a dead value is kind of like a "garbage-collectable" Nil?
#define deadvalue(o)	check_exp(ttisdeadkey(o), cast(void *, val_(o).gc))

// Defines "falsy" values in Lua: only nil and boolean false are "falsy", every
// other value is "truthy".
#define l_isfalse(o)	(ttisnil(o) || (ttisboolean(o) && bvalue(o) == 0))


// Used by some garbage collection code to make sure it's not garbage collecting
// noncollectable values.
#define iscollectable(o)	(rttype(o) & BIT_ISCOLLECTABLE)


/* Macros for internal tests */
// Used to assert that the type tag of a TValue agrees with type tag stored in
// the GCObject it points to. (Which it always should, right?)
#define righttt(obj)		(ttype(obj) == gcvalue(obj)->tt)

// Used to check liveness of `GCObject`s whenever they are set using the setter
// macros just below this. If the object is collectable it makes sure its type
// tags agree, and then if there is a lua_State it checks that the object is
// marked as alive. isdead() is a macro defined in lgc.h, which just checks the
// `marked` field of the GCObject.
#define checkliveness(L,obj) \
	lua_longassert(!iscollectable(obj) || \
		(righttt(obj) && (L == NULL || !isdead(G(L),gcvalue(obj)))))


/* Macros to set values */
// Helper to set the type tag field of a TValue, used in this file only.
#define settt_(o,t)	((o)->tt_=(t))

// Sets the TValue pointed to by obj to the float x. I guess io is used so that
// obj isn't duplicated twice?
#define setfltvalue(obj,x) \
  { TValue *io=(obj); val_(io).n=(x); settt_(io, LUA_TNUMFLT); }

// Change a float TValue to store another float x. Used exactly once in lvm.c. A
// little faster I guess since it doesn't have to set the type tag, assuming
// it's already set to LUA_TNUMFLT (and ensuring it with an assert).
#define chgfltvalue(obj,x) \
  { TValue *io=(obj); lua_assert(ttisfloat(io)); val_(io).n=(x); }

// Same thing for integer values.
#define setivalue(obj,x) \
  { TValue *io=(obj); val_(io).i=(x); settt_(io, LUA_TNUMINT); }

#define chgivalue(obj,x) \
  { TValue *io=(obj); lua_assert(ttisinteger(io)); val_(io).i=(x); }

// Only the type tag matters for nil values, the Value part can be anything.
#define setnilvalue(obj) settt_(obj, LUA_TNIL)

#define setfvalue(obj,x) \
  { TValue *io=(obj); val_(io).f=(x); settt_(io, LUA_TLCF); }

#define setpvalue(obj,x) \
  { TValue *io=(obj); val_(io).p=(x); settt_(io, LUA_TLIGHTUSERDATA); }

#define setbvalue(obj,x) \
  { TValue *io=(obj); val_(io).b=(x); settt_(io, LUA_TBOOLEAN); }

// Used exactly once in lgc.c. Takes unused L parameter to be consistent with
// the following setters, I guess. Why not checkliveness() here?
#define setgcovalue(L,obj,x) \
  { TValue *io = (obj); GCObject *i_g=(x); \
    val_(io).gc = i_g; settt_(io, ctb(i_g->tt)); }

#define setsvalue(L,obj,x) \
  { TValue *io = (obj); TString *x_ = (x); \
    val_(io).gc = obj2gco(x_); settt_(io, ctb(x_->tt)); \
    checkliveness(L,io); }

#define setuvalue(L,obj,x) \
  { TValue *io = (obj); Udata *x_ = (x); \
    val_(io).gc = obj2gco(x_); settt_(io, ctb(LUA_TUSERDATA)); \
    checkliveness(L,io); }

#define setthvalue(L,obj,x) \
  { TValue *io = (obj); lua_State *x_ = (x); \
    val_(io).gc = obj2gco(x_); settt_(io, ctb(LUA_TTHREAD)); \
    checkliveness(L,io); }

#define setclLvalue(L,obj,x) \
  { TValue *io = (obj); LClosure *x_ = (x); \
    val_(io).gc = obj2gco(x_); settt_(io, ctb(LUA_TLCL)); \
    checkliveness(L,io); }

#define setclCvalue(L,obj,x) \
  { TValue *io = (obj); CClosure *x_ = (x); \
    val_(io).gc = obj2gco(x_); settt_(io, ctb(LUA_TCCL)); \
    checkliveness(L,io); }

#define sethvalue(L,obj,x) \
  { TValue *io = (obj); Table *x_ = (x); \
    val_(io).gc = obj2gco(x_); settt_(io, ctb(LUA_TTABLE)); \
    checkliveness(L,io); }

// Like nil, the Value part doesn't matter. It doesn't even need the collectable
// bit to be set?
#define setdeadvalue(obj)	settt_(obj, LUA_TDEADKEY)



// Used internally all over the Lua source code, I guess so that liveness checks
// can be performed all over when testing.
#define setobj(L,obj1,obj2) \
	{ TValue *io1=(obj1); *io1 = *(obj2); \
	  (void)L; checkliveness(L,io1); }


/*
** different types of assignments, according to destination
*/

// Are these just for self-documentation?
/* from stack to (same) stack */
#define setobjs2s	setobj
/* to stack (not from same stack) */
#define setobj2s	setobj
#define setsvalue2s	setsvalue
#define sethvalue2s	sethvalue
// setptvalue2s isn't used anywhere, anymore. Does pt stand for prototype?
#define setptvalue2s	setptvalue
/* from table to same table */
#define setobjt2t	setobj
/* to new object */
#define setobj2n	setobj
#define setsvalue2n	setsvalue

/* to table (define it as an expression to be used in macros) */
// The cost of defining it as an expression is that o1 appears twice, I guess.
#define setobj2t(L,o1,o2)  ((void)L, *(o1)=*(o2), checkliveness(L,(o1)))




/*
** {======================================================
** types and prototypes
** =======================================================
*/


typedef TValue *StkId;  /* index to stack elements */




/*
** Header for string value; string bytes follow the end of this structure
** (aligned according to 'UTString'; see next).
*/
typedef struct TString {
  CommonHeader;
  lu_byte extra;  /* reserved words for short strings; "has hash" for longs */
  lu_byte shrlen;  /* length for short strings */
  unsigned int hash;
  union {
    size_t lnglen;  /* length for long strings */
    struct TString *hnext;  /* linked list for hash table */
  } u;
} TString;


/*
** Ensures that address after this type is always fully aligned.
*/
typedef union UTString {
  L_Umaxalign dummy;  /* ensures maximum alignment for strings */
  TString tsv;
} UTString;


/*
** Get the actual string (array of bytes) from a 'TString'.
** (Access to 'extra' ensures that value is really a 'TString'.)
*/
#define getstr(ts)  \
  check_exp(sizeof((ts)->extra), cast(char *, (ts)) + sizeof(UTString))


/* get the actual string (array of bytes) from a Lua value */
#define svalue(o)       getstr(tsvalue(o))

/* get string length from 'TString *s' */
#define tsslen(s)	((s)->tt == LUA_TSHRSTR ? (s)->shrlen : (s)->u.lnglen)

/* get string length from 'TValue *o' */
#define vslen(o)	tsslen(tsvalue(o))


/*
** Header for userdata; memory area follows the end of this structure
** (aligned according to 'UUdata'; see next).
*/
typedef struct Udata {
  CommonHeader;
  lu_byte ttuv_;  /* user value's tag */
  struct Table *metatable;
  size_t len;  /* number of bytes */
  union Value user_;  /* user value */
} Udata;


/*
** Ensures that address after this type is always fully aligned.
*/
typedef union UUdata {
  L_Umaxalign dummy;  /* ensures maximum alignment for 'local' udata */
  Udata uv;
} UUdata;


/*
**  Get the address of memory block inside 'Udata'.
** (Access to 'ttuv_' ensures that value is really a 'Udata'.)
*/
#define getudatamem(u)  \
  check_exp(sizeof((u)->ttuv_), (cast(char*, (u)) + sizeof(UUdata)))

#define setuservalue(L,u,o) \
	{ const TValue *io=(o); Udata *iu = (u); \
	  iu->user_ = io->value_; iu->ttuv_ = rttype(io); \
	  checkliveness(L,io); }


#define getuservalue(L,u,o) \
	{ TValue *io=(o); const Udata *iu = (u); \
	  io->value_ = iu->user_; settt_(io, iu->ttuv_); \
	  checkliveness(L,io); }


/*
** Description of an upvalue for function prototypes
*/
typedef struct Upvaldesc {
  TString *name;  /* upvalue name (for debug information) */
  lu_byte instack;  /* whether it is in stack (register) */
  lu_byte idx;  /* index of upvalue (in stack or in outer function's list) */
} Upvaldesc;


/*
** Description of a local variable for function prototypes
** (used for debug information)
*/
typedef struct LocVar {
  TString *varname;
  int startpc;  /* first point where variable is active */
  int endpc;    /* first point where variable is dead */
} LocVar;


/*
** Function Prototypes
*/
typedef struct Proto {
  CommonHeader;
  lu_byte numparams;  /* number of fixed parameters */
  lu_byte is_vararg;
  lu_byte maxstacksize;  /* number of registers needed by this function */
  int sizeupvalues;  /* size of 'upvalues' */
  int sizek;  /* size of 'k' */
  int sizecode;
  int sizelineinfo;
  int sizep;  /* size of 'p' */
  int sizelocvars;
  int linedefined;  /* debug information  */
  int lastlinedefined;  /* debug information  */
  TValue *k;  /* constants used by the function */
  Instruction *code;  /* opcodes */
  struct Proto **p;  /* functions defined inside the function */
  int *lineinfo;  /* map from opcodes to source lines (debug information) */
  LocVar *locvars;  /* information about local variables (debug information) */
  Upvaldesc *upvalues;  /* upvalue information */
  struct LClosure *cache;  /* last-created closure with this prototype */
  TString  *source;  /* used for debug information */
  GCObject *gclist;
} Proto;



/*
** Lua Upvalues
*/
typedef struct UpVal UpVal;


/*
** Closures
*/

#define ClosureHeader \
	CommonHeader; lu_byte nupvalues; GCObject *gclist

typedef struct CClosure {
  ClosureHeader;
  lua_CFunction f;
  TValue upvalue[1];  /* list of upvalues */
} CClosure;


typedef struct LClosure {
  ClosureHeader;
  struct Proto *p;
  UpVal *upvals[1];  /* list of upvalues */
} LClosure;


typedef union Closure {
  CClosure c;
  LClosure l;
} Closure;


#define isLfunction(o)	ttisLclosure(o)

#define getproto(o)	(clLvalue(o)->p)


/*
** Tables
*/

typedef union TKey {
  struct {
    TValuefields;
    int next;  /* for chaining (offset for next node) */
  } nk;
  TValue tvk;
} TKey;


/* copy a value into a key without messing up field 'next' */
#define setnodekey(L,key,obj) \
	{ TKey *k_=(key); const TValue *io_=(obj); \
	  k_->nk.value_ = io_->value_; k_->nk.tt_ = io_->tt_; \
	  (void)L; checkliveness(L,io_); }


typedef struct Node {
  TValue i_val;
  TKey i_key;
} Node;


typedef struct Table {
  CommonHeader;
  lu_byte flags;  /* 1<<p means tagmethod(p) is not present */
  lu_byte lsizenode;  /* log2 of size of 'node' array */
  unsigned int sizearray;  /* size of 'array' array */
  TValue *array;  /* array part */
  Node *node;
  Node *lastfree;  /* any free position is before this position */
  struct Table *metatable;
  GCObject *gclist;
} Table;



/*
** 'module' operation for hashing (size is always a power of 2)
*/
#define lmod(s,size) \
	(check_exp((size&(size-1))==0, (cast(int, (s) & ((size)-1)))))


#define twoto(x)	(1<<(x))
#define sizenode(t)	(twoto((t)->lsizenode))


/*
** (address of) a fixed nil value
*/
#define luaO_nilobject		(&luaO_nilobject_)


LUAI_DDEC const TValue luaO_nilobject_;

/* size of buffer for 'luaO_utf8esc' function */
#define UTF8BUFFSZ	8

LUAI_FUNC int luaO_int2fb (unsigned int x);
LUAI_FUNC int luaO_fb2int (int x);
LUAI_FUNC int luaO_utf8esc (char *buff, unsigned long x);
LUAI_FUNC int luaO_ceillog2 (unsigned int x);
LUAI_FUNC void luaO_arith (lua_State *L, int op, const TValue *p1,
                           const TValue *p2, TValue *res);
LUAI_FUNC size_t luaO_str2num (const char *s, TValue *o);
LUAI_FUNC int luaO_hexavalue (int c);
LUAI_FUNC void luaO_tostring (lua_State *L, StkId obj);
LUAI_FUNC const char *luaO_pushvfstring (lua_State *L, const char *fmt,
                                                       va_list argp);
LUAI_FUNC const char *luaO_pushfstring (lua_State *L, const char *fmt, ...);
LUAI_FUNC void luaO_chunkid (char *out, const char *source, size_t len);


#endif

