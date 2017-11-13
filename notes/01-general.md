# General notes

## Source code file organization

The source code is contained in a single flat `src/` directory. There are 35 `.c` files, 25 `.h` files, one `.hpp` file, and one `Makefile`. All source files start with the letter `l`.

Functions that are exported from one source file to others are prefixed with `lua?_`, where `?` is a capital letter denoting what source file the function is defined in. For example, `luaV_execute()` is defined in `lvm.c`, which implements the virtual machine and uses `V` as its single letter code.

Here's a list of each source and header file, what its job is, and what its single letter code is.

### C API

* `lua.h`: the public C interface to Lua (prefix: `lua_`)
* `lua.hpp`: wraps Lua headers in `extern C {...}` for C++ programs
* `lapi.c` and `lapi.h`: implementation of the C API declared in `lua.h`
* `lauxlib.c` and `lauxlib.h`: higher-level C API functions used by the standard library (prefix: `luaL_`)

### Other Headers

* `luaconf.h`: configuration file for Lua, lets you turn certain features on/off, and define what types to use for ints/floats, for example
* `lprefix.h`: defines some special compiler-dependent flags that must be defined before anything else
* `llimits.h`: typedefs some basic types, their max/min values, how to do basic math operations, and other magic low-level C stuff

### Executables

* `lua.c`: the `lua` interpreter/REPL
* `luac.c`: the `luac` bytecode compiler/inspector

### Lua Interpreter/VM

* `lstate.c` and `lstate.h`: the `lua_State` and `global_State` structs (prefix: `luaE_`)
* `lobject.c` and `lobject.h`: Lua value representation (prefix: `luaO_`)
* `lstring.c` and `lstring.h`: Lua strings, with string interning and caching (prefix: `luaS_`)
* `ltable.c` and `ltable.h`: Lua tables (prefix: `luaH_`)
* `ltm.c` and `ltm.h`: tag methods, a.k.a. metamethods (prefix: `luaT_`)
* `lmem.c` and `lmem.h`: memory allocator interface (prefix: `luaM_`)
* `lgc.c` and `lgc.h`: garbage collector (prefix: `luaC_`)
* `lzio.c` and `lzio.h`: buffered input/output streams (prefix: `luaZ_`)
* `llex.c` and `llex.h`: lexer (prefix: `luaX_`)
* `lparser.c` and `lparser.h`: parser (prefix: `luaY_`)
* `lcode.c` and `lcode.h`: functions that generate Lua bytecode during parsing (prefix: `luaK_`)
* `lfunc.c` and `lfunc.h`: handles function prototypes and closures (prefix: `luaF_`)
* `ldo.c` and `ldo.h`: handles function calling and stack (prefix: `luaD_`)
* `lopcodes.c` and `lopcodes.h`: opcodes with argument info and macros to help work with opcode arguments
* `lvm.c` and `lvm.h`: virtual machine (prefix: `luaV_`)
* `ldebug.c` and `ldebug.h`: implements the C API's debugging functions, and defines functions for raising Lua errors, like `luaG_runerror()` (prefix: `luaG_`)
* `lctype.c` and `lctype.h`: `<ctype.h>` functions (like `isalpha()`, `isspace()`, etc.), optimized for Lua (prefix: `l`, as in `lisalpha()`, `lisspace()`, etc.)
* `ldump.c`: used by the `luac` program to save compiled Lua to a file, to be "undumped" later (prefix: `luaU_`)
* `lundump.c` and `lundump.h`: reads dumped precompiled Lua code back into memory (prefix: `luaU_`)

### Standard Libraries

* `linit.c` and `lualib.h`: defines a function that loads all of the standard libraries (prefix: `luaL_` (same as `lauxlib.[ch]`))
* `lutf8lib.c`: UTF-8 library
* `lbaselib.c`: basic library functions, like `print()`, `assert()`, `dofile()`, etc.
* `lbitlib.c`: bitwise operations (deprecated by actual bitwise operators in Lua 5.3)
* `lmathlib.c`: math library
* `ltablib.c`: table library
* `lstrlib.c`: string library
* `loslib.c`: OS library (`os.time()`, `os.exit()`, `os.rename()`, etc.)
* `lcorolib.c`: coroutine library
* `ldblib.c`: debug library
* `liolib.c`: input/output library
* `loadlib.c`: dynamic library loader

## Source code patterns

Every C file begins with the following defines and includes:

1. A define for the filename, e.g. `#define lobject_c`
2. A define for whether the file is part of "Lua core" or part of the standard libraries, either `#define LUA_CORE` or `#define LUA_LIB`
3. `#include "lprefix.h"`
4. C standard library includes (e.g. `#include <string.h>`) in alphabetical order
5. `#include "lua.h"` (except a couple files that have no dependencies on the Lua API)
6. All other local includes (e.g. `#include "lobject.h"`) in alphabetical order

Every H file is wrapped in the standard `#ifndef` conditional, e.g.:

```c
#ifndef lobject_h
#define lobject_h

...

#endif
```
