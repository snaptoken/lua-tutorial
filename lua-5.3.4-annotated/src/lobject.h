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

// "Normal" type tags are defined in lua.h, under the heading "basic types".
// They live there so that they're available in the Lua API. These extra tags
// are for internal use and so are private.
/*
** Extra tags for non-values
*/
// LUA_NUMTAGS is the number of "basic types", and is defined along with the
// basic type tags in lua.h. LUA_TPROTO is the type tag for Lua function
// prototypes, of which there is one for each lexical function block that is
// parsed. Why are function prototypes represented as tagged values? (Because
// they are garbage collected?)
#define LUA_TPROTO	LUA_NUMTAGS		/* function prototypes */
// This one is used more as a sentinel value, for tables with weak references
// that get garbage collected. See lgc.c:removeentry().
#define LUA_TDEADKEY	(LUA_NUMTAGS+1)		/* removed keys in tables */

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
// LUA_TLCL stands for LUA Type: Lua CLosure. A closure instantiated from a
// function prototype, with upvalues. Garbage collected.
#define LUA_TLCL	(LUA_TFUNCTION | (0 << 4))  /* Lua closure */
// LUA_TLCF stands for LUA Type: Light C Function. This is represented simply as
// a pointer to the C function.
#define LUA_TLCF	(LUA_TFUNCTION | (1 << 4))  /* light C function */
// LUA_TCCL stands for LUA Type: C CLosure. Contains a pointer to a C function,
// and also an array of upvalues. Garbage collected.
#define LUA_TCCL	(LUA_TFUNCTION | (2 << 4))  /* C closure */

// It takes a while to get to know the acronyms LCL, LCF, and CCL...


/* Variant tags for strings */
// Strings up to a certain length are interned, so that two separately
// instantiated strings with the same contents will point to the same garbage
// collected object. What is the max length of a short string?
#define LUA_TSHRSTR	(LUA_TSTRING | (0 << 4))  /* short strings */
// Long strings aren't interned, and are treated a little differently for
// performance reasons.
#define LUA_TLNGSTR	(LUA_TSTRING | (1 << 4))  /* long strings */


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
  // lua_CFunction, lua_Integer, and lua_Number are defined in lua.h, and can be
  // configured in luaconf.h. For example, lua_Integer might be typedef'd as
  // long int, and lua_Number as double.
  lua_CFunction f; /* light C functions */
  lua_Integer i;   /* integer numbers */
  lua_Number n;    /* float numbers */
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


// The Lua stack is an array of `TValue`s. A StkId is a pointer to a slot within
// the stack.
typedef TValue *StkId;  /* index to stack elements */




/*
** Header for string value; string bytes follow the end of this structure
** (aligned according to 'UTString'; see next).
*/
// Lua's string type. It is garbage-collectable, so it includes the
// CommonHeader. This allows a TValue to contain a GCObject pointer to it, and
// cast the pointer to a TString pointer to access the rest of the struct. What
// does the T stand for in TString?
typedef struct TString {
  CommonHeader;
  // The Lua lexer uses this `extra` field to store an integer code for each of
  // Lua's reserved words (see `llex.c:luaX_init()`). This field has a separate
  // use for long strings: it contains 1 if the `hash` field below already
  // contains a hash of the string, so it doesn't have to recompute the hash
  // more than once for expensively long strings (see
  // `lstring.c:luaS_hashlongstr()`). `lu_byte` is defined in `llimits.h`, and
  // is used to differentiate between numeric bytes and actual characters (which
  // use `char` or `unsigned char`).
  lu_byte extra;  /* reserved words for short strings; "has hash" for longs */
  // So short strings can't be longer than 255 characters.
  lu_byte shrlen;  /* length for short strings */
  // Used for hash tables. See lstring.c:luaS_hash().
  unsigned int hash;
  // This field is a union containing the string length if it's a long string,
  // and a linked list if it's a short string. The linked list part of the
  // global string hash table used for string interning. See
  // `lstring.c:internshrstr()`.
  union {
    size_t lnglen;  /* length for long strings */
    struct TString *hnext;  /* linked list for hash table */
  } u;
} TString;


/*
** Ensures that address after this type is always fully aligned.
*/
// Note this is a union. L_Umaxalign is defined in llimits.h. I believe the
// dummy field causes sizeof(UTString) to be a multiple of 4 or 8 or whatever
// machine's word size is? To be aligned anyway. Which is used in the getstr()
// macro just below to decide where the actual string data is stored. So this
// helps keep string data, which is an array of bytes, aligned on a word
// boundary.
typedef union UTString {
  L_Umaxalign dummy;  /* ensures maximum alignment for strings */
  TString tsv;
} UTString;


/*
** Get the actual string (array of bytes) from a 'TString'.
** (Access to 'extra' ensures that value is really a 'TString'.)
*/
// String data is stored starting somewhere after the end of the TString struct
// in memory.
#define getstr(ts)  \
  check_exp(sizeof((ts)->extra), cast(char *, (ts)) + sizeof(UTString))


/* get the actual string (array of bytes) from a Lua value */
#define svalue(o)       getstr(tsvalue(o))

/* get string length from 'TString *s' */
// Remember short strings store their length in a single byte field and long
// strings store their length in a separate size_t field.
#define tsslen(s)	((s)->tt == LUA_TSHRSTR ? (s)->shrlen : (s)->u.lnglen)

/* get string length from 'TValue *o' */
#define vslen(o)	tsslen(tsvalue(o))


/*
** Header for userdata; memory area follows the end of this structure
** (aligned according to 'UUdata'; see next).
*/
// Userdata is a Lua type that stores any kind of custom data (generally a
// user-defined C struct). It's often used when exposing a C API to Lua. It's
// similar to a TString in that the data is stored at the end of the Udata
// struct.
typedef struct Udata {
  CommonHeader;
  // I guess a Udata can also store a Lua value? In ttuv_ and user_. I guess
  // this would be useful for giving a basic Lua value its own metatable?
  lu_byte ttuv_;  /* user value's tag */
  // The metatable can be used to give the Udata its own "object-oriented" type,
  // and/or define methods and operators on the value.
  struct Table *metatable;
  size_t len;  /* number of bytes */
  // The Value part that goes with the ttuv_ type tag above. Any reason why
  // they're split up like this in the struct?
  union Value user_;  /* user value */
} Udata;


/*
** Ensures that address after this type is always fully aligned.
*/
// Similar to UTString above.
typedef union UUdata {
  L_Umaxalign dummy;  /* ensures maximum alignment for 'local' udata */
  Udata uv;
} UUdata;


/*
**  Get the address of memory block inside 'Udata'.
** (Access to 'ttuv_' ensures that value is really a 'Udata'.)
*/
// Similar to getstr() above.
#define getudatamem(u)  \
  check_exp(sizeof((u)->ttuv_), (cast(char*, (u)) + sizeof(UUdata)))

// Sets the user_ and ttuv_ fields of a Udata to the given TValue.
#define setuservalue(L,u,o) \
	{ const TValue *io=(o); Udata *iu = (u); \
	  iu->user_ = io->value_; iu->ttuv_ = rttype(io); \
	  checkliveness(L,io); }


// Gets the user_ and ttuv_ fields of a Udata, constructing a TValue from them.
#define getuservalue(L,u,o) \
	{ TValue *io=(o); const Udata *iu = (u); \
	  io->value_ = iu->user_; settt_(io, iu->ttuv_); \
	  checkliveness(L,io); }


/*
** Description of an upvalue for function prototypes
*/
// The Proto struct below contains an array of these, one for each upvalue.
typedef struct Upvaldesc {
  TString *name;  /* upvalue name (for debug information) */
  // If the upvalue is a local variable of the outer function, this contains
  // true and the following idx field contains the local variable's index in the
  // stack. Otherwise the upvalue comes from an upvalue of the outer function,
  // in which case instack is 0 and idx contains the index of the upvalue in the
  // outer function's Upvaldesc array. I think??
  lu_byte instack;  /* whether it is in stack (register) */
  lu_byte idx;  /* index of upvalue (in stack or in outer function's list) */
} Upvaldesc;


/*
** Description of a local variable for function prototypes
** (used for debug information)
*/
// The Proto struct below contains an array of these, one for each local
// variable. Mainly for debug info?
typedef struct LocVar {
  TString *varname;
  // These are indexes into the Proto struct's `code` field, which contains an
  // array of VM instructions.
  int startpc;  /* first point where variable is active */
  int endpc;    /* first point where variable is dead */
} LocVar;


/*
** Function Prototypes
*/
// This is an internally-used garbage-collected Lua type. Remember LUA_TPROTO is
// defined as an "extra type for non-values" at the top of this file. A Proto is
// a function prototype, which contains information about a Lua function. This
// is where all executable Lua VM instructions are stored. (Like C, everything
// is a function, Lua's just not as explicit about it.) Note that this is
// different from a Lua closure, which is an actual first-class Lua value
// instantiated from a Proto. `Proto`s are created by the parser. There is one
// for each lexical function expression in a piece of parsed Lua code. `Proto`s
// contain "upvalue information", whereas Lua closures contain actual upvalues.
typedef struct Proto {
  CommonHeader;
  // Number of named parameters.
  lu_byte numparams;  /* number of fixed parameters */
  // Whether the parameter list ends with "...", meaning any number of arguments
  // may be passed after the fixed parameters.
  lu_byte is_vararg;
  // Determined by the code generator, and used to grow the stack before calling
  // a function of this prototype.
  lu_byte maxstacksize;  /* number of registers needed by this function */
  // Lengths of array fields that follow.
  int sizeupvalues;  /* size of 'upvalues' */
  int sizek;  /* size of 'k' */
  int sizecode;
  int sizelineinfo;
  int sizep;  /* size of 'p' */
  int sizelocvars;
  // Line number this function was defined on.
  int linedefined;  /* debug information  */
  // Line number the function definition ends on.
  int lastlinedefined;  /* debug information  */
  // Array of constant values. VM instructions in `code` refer to these by
  // index.
  TValue *k;  /* constants used by the function */
  // VM instructions to be executed when calling this function.
  Instruction *code;  /* opcodes */
  // Where is this used?
  struct Proto **p;  /* functions defined inside the function */
  int *lineinfo;  /* map from opcodes to source lines (debug information) */
  LocVar *locvars;  /* information about local variables (debug information) */
  Upvaldesc *upvalues;  /* upvalue information */
  // In functional code you often have an anonymous function being instantiated
  // over and over inside a loop. This probably allows such a closure to be
  // reused somehow for better performance?
  struct LClosure *cache;  /* last-created closure with this prototype */
  // The source code of the function.
  TString  *source;  /* used for debug information */
  // Used for garbage collection, but how exactly?
  GCObject *gclist;
} Proto;



/*
** Lua Upvalues
*/
// Forward declaration of an upvalue. Struct is defined in lfunc.h. It's used
// below.
typedef struct UpVal UpVal;


/*
** Closures
*/

// There are C closure and there are Lua closures. The ClosureHeader defines
// what fields they have in common: number of upvalues, and a gclist.
#define ClosureHeader \
	CommonHeader; lu_byte nupvalues; GCObject *gclist

// A C closure is a C function pointer and an array of upvalues which it owns
// and has private access to.
typedef struct CClosure {
  ClosureHeader;
  // A lua_CFunction is a function that takes a lua_State* and returns an int.
  lua_CFunction f;
  TValue upvalue[1];  /* list of upvalues */
} CClosure;


// A Lua closure is more complicated. It points to a Lua function (a Proto) and
// also contains an array of upvalues, but here the upvalues can either be local
// variables of an outer function or `TValue`s owned by the closure (in the case
// that the closure outlives the scope that the outer variables live in). The
// UpVal struct keeps track of all this.
typedef struct LClosure {
  ClosureHeader;
  struct Proto *p;
  UpVal *upvals[1];  /* list of upvalues */
} LClosure;


// A Closure is just a union of CClosure and LClosure. It's useful because those
// structs have a common ClosureHeader, which can identify what type of closure
// they are using the type tag in the CommonHeader.
typedef union Closure {
  CClosure c;
  LClosure l;
} Closure;


#define isLfunction(o)	ttisLclosure(o)

#define getproto(o)	(clLvalue(o)->p)


/*
** Tables
*/

// A key in a Lua table is any Lua value (except nil).
typedef union TKey {
  struct {
    // The Value and type tag fields of the TValue part.
    TValuefields;
    // Key-value pairs are stored in an array inside a Lua table. The `next`
    // field contains the index, relative to the current key's position, of the
    // next key in the chain.
    int next;  /* for chaining (offset for next node) */
  } nk;
  // An alternate way to access the Value and type tag of the key as a TValue.
  // This is solely used to define the gkey() macro in ltable.h. Why are there
  // two ways to access the TValue in a TKey?
  TValue tvk;
} TKey;


/* copy a value into a key without messing up field 'next' */
// Similar to setfltvalue(), etc. earlier in this file.
#define setnodekey(L,key,obj) \
	{ TKey *k_=(key); const TValue *io_=(obj); \
	  k_->nk.value_ = io_->value_; k_->nk.tt_ = io_->tt_; \
	  (void)L; checkliveness(L,io_); }


// A key-value pair in a Lua table. The table contains an array of these. Keys
// and values are pretty much just `TValue`s, except the `TKey`s contain the
// `next` field for chaining.
typedef struct Node {
  TValue i_val;
  TKey i_key;
} Node;


// A Lua table object.
typedef struct Table {
  CommonHeader;
  // This is used to very quickly check a metatable for operator overrides when
  // evaluating operators and such. See ltm.h:gfasttm().
  lu_byte flags;  /* 1<<p means tagmethod(p) is not present */
  // The node array's length is always a power of 2, so this stores the length
  // as an exponent.
  lu_byte lsizenode;  /* log2 of size of 'node' array */
  // Lua doesn't have arrays, it uses tables for everything. So tables also
  // include an array part for performance. This is the length of the array
  // part.
  unsigned int sizearray;  /* size of 'array' array */
  TValue *array;  /* array part */
  // Hash table part.
  Node *node;
  // Tables are searched for free positions from the end, so remember where the
  // search ended last time and start searching from there next time. Right?
  // (See `ltable.c:getfreepos()`.)
  Node *lastfree;  /* any free position is before this position */
  // Like Udata, Tables can have their own metatable. (Other types just have one
  // global metatable shared by all objects of that type.)
  struct Table *metatable;
  // Used for garbage collection, but how exactly?
  GCObject *gclist;
} Table;



/*
** 'module' operation for hashing (size is always a power of 2)
*/
// Used by hash functions in ltable.c as well as the string table in lstring.c.
#define lmod(s,size) \
	(check_exp((size&(size-1))==0, (cast(int, (s) & ((size)-1)))))


#define twoto(x)	(1<<(x))
// Get the actual number of elements in a hash table by calculating the power of
// two that is stored in `lsizenode`.
#define sizenode(t)	(twoto((t)->lsizenode))


/*
** (address of) a fixed nil value
*/
// luaO_nilobject_ is a static TValue containing nil, defined in lobject.c.
#define luaO_nilobject		(&luaO_nilobject_)


// LUAI_DDEC is usually defined as `extern`, in luaconf.h.
LUAI_DDEC const TValue luaO_nilobject_;

/* size of buffer for 'luaO_utf8esc' function */
#define UTF8BUFFSZ	8

// LUAI_FUNC is usually defined as `extern`, in luaconf.h.
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

