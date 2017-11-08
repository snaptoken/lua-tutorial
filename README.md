# Lua From Scratch

This is a tutorial that walks you through writing a complete, production-ready
implementation of a real programming language in 17,000 lines of C. You will be
re-writing the [Lua](http://www.lua.org/) source code step-by-step, and by the
end you will have a thorough understanding of every detail of every line of
code of the Lua implementation.

The tutorial isn't done yet. It's actually still in the planning stages. It
will probably take a long time to get done. You can
[sign up here](http://eepurl.com/cIOGCD) to be notified when it's done, or
maybe when part of it is ready for consumption.

## What you will implement

Everything that's inside
[lua-5.3.4.tar.gz](http://www.lua.org/ftp/lua-5.3.4.tar.gz), you will write by
hand. This includes:

* Parser/lexer (handwritten, not generated)
* Virtual machine
* Code generator (compiling to bytecode)
  * Constant folding
  * Code optimizations
* Value representation
* The Lua C API
* Prototype inheritance (metatables)
* Hash table implementation (chained scatter table)
* Garbage collection (incremental mark-and-sweep)
  * Weak references
* String interning
* Closures
* Coroutines
* Operator overloading (tag methods)
* Error handling
* The Lua standard library
  * String pattern matching (similar to regular expressions)
* Debugging features
* `lua`, the Lua REPL executable
* `luac`, the Lua compiler
* Portable `Makefile`
* And much, much more...

## The Plan

Here are the main steps I plan to take in developing this tutorial:

1. Get familiar with Lua. ([lua.org/docs.html](http://www.lua.org/docs.html) has lots of good stuff, make sure to read the papers!)
2. Get familiar with the Lua source code.
   ([Mike Pall wrote a nice guide](https://www.reddit.com/r/programming/comments/63hth/ask_reddit_which_oss_codebases_out_there_are_so/c02pxbp/))
3. Annotate the Lua source code, in great detail.
4. Decide on a general order in which to implement everything in Lua.
5. Split the Lua source code into a series of 20 to 70 "macro-steps", where
   each step adds about a chapter's worth of functionality.
6. Iterate on these macro-steps, adding/removing/reordering them until I find a
   good order for them to go in that will best suit the book.
7. Turn each macro-step into a chapter by splitting it into micro-steps,
   writing 1-3 paragraphs to explain each micro-step, and also explaining any
   higher level concepts (like explaining garbage collection in general and
   then giving an overview of how Lua's GC algorithm works).

I'm well into step 2, and am starting work on step 3.

Update: I'm skipping step 3, it's not worth it. I'm diving right in to step 4.
See `MACROSTEPS.md`.

### Possible orders

* Classic: repl -> lexing -> parsing -> evaluating expressions -> ...
  * Not good because Lua does parsing/compiling in one single, tightly-written
    step. Also this is how every other programming language implementation book
    does it, it's getting old.
* Augmenting C: tvalue -> stack -> c api -> hash tables -> GC -> ...
  * Starts by adding duck-typing features to C, then gets right into the lua
    API and then goes into actual interesting language features like hash
    tables and GC, with no need for boring parsing
* Stages: Implement a super simple version of Lua as the first stage, then add
  more advanced features (like closures, metatables) in the second stage, and
  then add optimizations and better algorithms (like incremental GC) in the
  third stage.
* "Topic after topic after topic": Jump around to different parts of the
   codebase early and often, to keep it interesting and to help with holding
   the entire codebase in your head. Like a TV show that jumps to many
   different story arcs each episode, making just a bit of progress on each
   one.
   * I really like the idea of this but will have to figure out if it would
     actually work in practice...
* Historical: Implement things in the order they were actually implemented in
  Lua over the years.
  * Let's not do that, okay? ("Chapter 105: Adding `true` and `false` to our
    language")
  * Although, Lua did start out as a data-description language (kinda like
    JSON), so for the parser we could start by parsing literal values/tables
    before going on to parse executable code.

I think the "Augmenting C" order will be used for the beginning, and then
we'll try to keep it interesting by doing the "topic after topic" thing. Some
features might use the "Stages" idea, where a basic version will be
implemented in say the first half of the book, then the more advanced version
will come much later. So there, it'll be a combination of the above ordering
ideas. (Even the "Classic" order will probably make an appearance... it just
won't be anywhere near the beginning of the book.)

### Ideas

* Self-contained chapters: Might be nice to make at least certain chapters more
  or less self-contained. So if chapter #38 is all about GC, don't assume
  they've worked through the entire book up to that point, or that they even
  know anything about Lua. They might just be someone interested in learning
  about GC. Keep those people in mind.
* "Final" lines of code: mark lines of code that are in their "final" state.
  That is, lines that exist in the final version of the code. Give them a
  subtle grey background maybe, like the line is "set in stone".
