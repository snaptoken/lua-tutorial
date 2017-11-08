# Macro step order

1. Tagged values (nil, bool, pointer, float)
1. Strings (no caching/interning/variants)
1. Freeing objects (strings) on lua_close() using a gclist
1. Tables (minimal naive version, no array part)
1. ...everything below this is unordered as of yet...
1. Basic stack (so functions can have `int ()(lua_State *L)` prototype)
    * basic push and pop API
    * stack grows dynamically
1. Minimal mark-and-sweep GC
1. 
