# Blot

A compiler-explorer clone, but works with your toolchain and your project.

Very embryonic for now, but the goals is for this to be a building
block in :

* **source code editor plugins** (either as a linked-in library or as
  a subprocess) to get live disassembly of your sources as you edit
  your code in your project.

  Not unlike the Emacs plugin https://github.com/joaotavora/beardbolt,
  but much faster and more powerful.

  In fact this Vim plugin is already using blot!
  https://github.com/adromanov/vim-blot
  
* **project exploration tools** (for example part of a code forge)

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
JSON and Process.V2, `fmtlib` for formatting, `CLI11` for CLI
handling, `libclang` for C++/C parsing, `doctest` for tests)

## Usage

Blot can:

1. Compile and annotate C++ files:

   This requires an entry for the file in `compile_commands.json`.
   Currently only C/C++ translation units are supported, header files
   are not!
  
   ```bash
   blot --ccj path/to/proj/compile_commands.json path/to/proj/src/bla.cpp
   ```
   
   This uses your project's actual build configuration to compile the
   source file and generate assembly.

2. Annotate some pre-compiled code with `gcc` or `clang`

   Easiest is to pipe into stdin:
   
   ```bash
   echo 'int main() { return 42; }' | g++ -S -g -x c++ - -o - | blot
   ```

   You can pass in a file containing the assembly.

   ```bash
   echo 'int main() { return 42; }' | g++ -S -g -x c++ - -o file.s
   blot --asm-file=file.s
   ``` 

Add `--json` for structured output with line mappings (beware format may change!)
Add `--demangle` for demangled output (this should probably be on by default)

## Roadmap

* *100%* `compile_commands.json` support.

* *90%* Test coverage

  Achieve some kind of parity with Compiler Explorer filtering options
  (filter directives, library functions, etc).  The final 10% is I
  need to go over the Compiler Explorer options one by one to check if
  it really makes sense.
  
* *100%* Auto-demangling support using `cxxabi.h`
  
  Other ABI's not in the roadmap for now.

* *95%* Compilation error handling

  Errors are decently reported and serialized in the JSON.  Just
  wondering if I should also parse some of the error messages for
  richer line/column info.  I'm leaning no, it's not really blot's job
  as a tool.

* *30%* Header file annotation

  To annotate header files, we have heuristically infer its "includer"
  `.cpp` translation unit(s) from the set that `clangd` does this, for
  example. This is because header files don't show up in
  `compile_commands.json` and even if we could somehow pretend they
  did there's no guaran instantiate templates from header files we'd
  like to annotate.

* *0%* Unsaved buffer support

  Enable live assembly updates without saving files (critical for
  editor integration).  The last challenge is particularly complex as
  compilers supports compiling filesystem-based translation units
  while substituting specific `#included` headers with piped input.
  So this means some sort of virtual file system, where we trick
  invoked compilers into seeing in-memory file representations as
  filesystem files
  
* *0%* Console-based project-explorer tool.  Web-based
  project-explorer tool.
   
* *0%* Decent-ish C/C++ stable API and ABI.  The so-called
  "hourglass" pattern might come handy
  
* *90%* Package management
 
  Conan 2 support implemented for most dependencies.
  
* *0%* Documentation

* 0% CI system, code coverage

## Building with Conan (may be useful later for CI)

If you have Conan 2 installed, you can automatically download and
build the dependencies (except `libclang` which must be installed
separately, for now).

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


