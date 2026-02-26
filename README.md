# Blot

A Compiler Explorer clone, but works with your toolchain and your
project.

Somewhat embryonic for now, and only for C/C++.  But the goal is for
this to be a building block in:

* **source code editor plugins** (either as a linked-in library or as
  a subprocess) to get live disassembly of your sources as you edit
  your code in your project.

  Not unlike the Emacs [Beardbolt][beardbolt] plugin but much faster
  and more powerful.

  In fact this [Vim plugin][vim-blot] is already using blot!

* **project exploration tools** (for example part of a code forge)

## Usage

Blot can:

1. Compile and annotate C++ source files _and_ headers:

   This uses knowledge in `compile_commands.json`.  For source files a
   direct entry is needed; for a header file, blot searches for a
   translation unit that includes it.

   ```bash
   blot --ccj path/to/proj/compile_commands.json path/to/proj/src/bla.cpp
   blot --ccj path/to/proj/compile_commands.json path/to/proj/include/bla.hpp
   ```

   Most importantly, this uses your project's actual build
   configuration to compile the source file and generate assembly.

2. Annotate some pre-compiled code with `gcc` or `clang`

   Easiest is to pipe into stdin:

   ```bash
   echo 'int main() { return 42; }' | g++ -S -g -x c++ - -o - | blot
   main.cpp:70 INFO: Reading from stdin
   blot.cpp:497 INFO: Annotating 2303 bytes of asm
   main:
           pushq   %rbp
           movq    %rsp, %rbp
           movl    $42, %eax
           popq    %rbp
           ret
   ```

   You can pass in a file containing the assembly.

   ```bash
   echo 'int main() { return 42; }' | g++ -S -g -x c++ - -o file.s
   blot --asm-file=file.s
   ```

Add `--json` for structured output with line mappings (beware format may change!)
Add `--demangle` for demangled output (this should probably be on by default)

3. Launch a local web UI (horribly alpha, use at your own risk):

   ```bash
   blot --web --ccj path/to/proj/compile_commands.json
   # then open http://localhost:4242
   ```

   The default port is 4242; override with `--port N`.  The server
   stays running until you press Ctrl-C.

## Build

For now, you'll have to build it yourself with a somewhat modern C++
toolchain.

```bash
# Configure and build development version
cmake -B build-Debug -DCMAKE_BUILD_TYPE=Debug # aka cmake --preset=dev
cmake --build build-Debug

# Or configure and build release version
cmake --preset=release
cmake --build build-Release

# Run tests
ctest --test-dir build-Debug

# Format code
cmake --build build-Debug --target=format

# Regenerate test fixture files
cmake --build build-Debug --target=regenerate-fixtures
```

The `blot` program will live in `build-Debug/blot` or
`build-Release/blot`.

There are dependencies (nothing exotic: `RE2` for regexps, `Boost` for
JSON, Process.V2, and "Beast", `fmtlib` for formatting, `CLI11` for
CLI handling, LLVM/Clang C++ libraries for C++/C parsing, `doctest`
for tests)

## Roadmap

* *100%* `compile_commands.json` support.

* *90%* Test coverage

  Achieve some kind of parity with Compiler Explorer filtering options
  (filter directives, library functions, etc).  The final 10% is that I
  need to go over the Compiler Explorer options one by one to check if
  it really makes sense.

* *100%* Auto-demangling support using `cxxabi.h`

  Other ABI's not in the roadmap for now.

* *95%* Compilation error handling

  Errors are decently reported and serialized in the JSON.  Just
  wondering if I should also parse some of the error messages for
  richer line/column info.  I'm leaning no, it's not really blot's job
  as a tool.

* *95%* Header file annotation

  Header files don't appear in `compile_commands.json`, so blot
  heuristically infers the "includer" translation unit by preprocessing
  each entry and walking its inclusion tree via Clang's `PPCallbacks`.

  There is decent test coverage for this, but edge cases remain.  One
  of them has to do with code injected by sanitizers (UBSan and ASAN),
  which makes for very noisy annotation results.  The other has to
  do with inlining.  In higher optimization settings, the functions
  that inline the header's code are often not in the header file
  itself, but rather in the inferred includer.  What to do?  If we
  show the includer function we'll be more accurate but likely we'll
  also show _all_ its callees that have nothing to do with the annotation
  target.  Or we could push a -fno-inline...

* *80%* Unsaved buffer support

  Enable live assembly updates without saving files, critical for
  editor integration.  A filesystem spoofing proof-of-concept
  (`src/spoof/`) is in place that tricks compilers into reading
  in-memory content as if it were on disk. Linux only, unfortunately.
  The remaining work is wiring it into the full compilation and
  JSONRPC pipeline.

* *30%* Web UI (`--web`)

  A browser-based project explorer (`blot --web`) is implemented but
  very rough.  It serves a Vue 3 single-page app backed by a
  JSONRPC/WebSocket server.  Many things are missing.  The
  highlighting is horribly slow for one.

* *20%* Editor tooling

  A JSONRPC/stdio server proof-of-concept (`src/jsonrpc_server/`) is
  implemented.  The API methods it currently exposes are bunk though.
  It should serve the same API as the Web UI and be integrated into
  `blot --stdio` one day, so we can build an Emacs extension that is a
  cross between [Eglot][eglot] and [Beardbolt][beardbolt].

* *20%* Decent-ish C/C++ stable API and ABI.  The so-called
  "hourglass" pattern might come in handy

* *90%* Package management

  Conan 2 support implemented for most dependencies.

* *60%* Documentation

  Public C++ API headers (`include/blot/`) have Doxygen comments.
  No generated HTML docs yet.

* 0% CI system, code coverage

  The main challenge is that test fixtures encode exact compiler
  output, so CI would need pinned compiler versions to avoid
  spurious failures.

* 0% Packaging

  Oh boy, not looking forward to this one.

## Building with Conan (may be useful later for CI)

If you have Conan 2 installed, you can automatically download and
build the dependencies (except the LLVM/Clang libraries which must be
installed separately, for now).

There are some profiles `conan-dev` and `conan-release` that make use
of [conan-cmake][1] integration, so no manual `conan install` is
needed - CMake will automatically invoke Conan during configuration.

1. For a Conan-enabled **development** build of Blot

   ```bash
   cmake --preset=conan-dev
   cmake --build build-Debug
   ```

2. For a Conan-enabled **release** build of Blot

   ```bash
   cmake --preset=conan-release
   cmake --build build-Release
   ```

[1]: https://github.com/conan-io/cmake-conan/tree/develop2
[beardbolt]: https://github.com/joaotavora/beardbolt
[eglot]: https://github.com/joaotavora/eglot
[vim-blot]: https://github.com/adromanov/vim-blot

