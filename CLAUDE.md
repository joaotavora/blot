# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when
working with code in this repository.  Actually, it should provide
guidance to anyone wanting to contribute to this, this is sort of a
`CONTRIBUTING.md`.

Claude and other AI's and anyone reading this should of course also
read `README.md`.

## Dev builds

This is a CMake-based C++23 project using modern build practices.
Refer to `README.md` and it's "Build" section.

```
# Run the tool (see README for more ways to run it)
echo 'int main() { return 42; }' | g++ -S -g -x c++ - -o - | build-Debug/blot
```

Cmake builds generate `compile_commands.json` automatically in both
build directories, with a symlink at the project root pointing to
`build-Release/compile_commands.json`.

## Non-dev builds

There are so far.  No Cmake `install` target.  No way to
`addSubdirectory` this `CMakeLists.txt` file.  `fetchContent` won't
work for the same reason.  So if someone wants to vendor this you're
on your own for now :-)

## Package manager

There is none, for now.  There are runtime dependencies, yes, and
Cmake will look for them.  Install them however you see fit so that
Cmake can find them.

## Project Architecture

Blot is a compiler-explorer clone that works with your local toolchain
and project. It's basic job is to processes assembly output to create
annotated, cleaned assembly with source-to-assembly line mappings.

Another main feature (and presumably its main added value) is that it
knows how to invoke your project's compilers appropriate for the user.

### Core Components

- Main executable: `blot` - processes source files or assembly input
- Assembly processing: Two-pass algorithm that filters and annotates assembly
- Compile commands integration: Automatically finds and uses `compile_commands.json`
- Line mapping: Tracks correspondence between source and assembly lines

### Key Modules

#### Public API (`include/blot/`)
- `blot.hpp` - Core annotation interface and types
- `assembly.hpp` - Assembly generation interface
- `ccj.hpp` - Compile commands interface
- `infer.hpp` - Given a header file, infers translation unit to
  compile (could perhaps be merged with `ccj.hpp`)

#### Implementation (`src/libblot/`)
- `blot.cpp` - Core annotation engine with two-pass processing and C++ demangling
- `assembly.cpp` - Assembly generation from compiler commands
- `ccj.cpp` - `compile_commands.json` parsing and lookup
- `logger.hpp` - Internal logging infrastructure
- `linespan.hpp` - Internal line-based text processing utility

#### Application (`src/blot/`)
- `main.cpp` - Entry point and workflow orchestration
- `options.{hpp,cpp}` - CLI argument parsing and file options

### Processing Pipeline

1. Input Resolution: Source file → compile command → assembly
   generation, direct assembly input from passed-in filename or from
   stdin.
2. First Pass: Parse assembly to identify functions, labels, and
   source mappings
3. Intermediate: Determine which symbols to preserve based on
   options
4. Second Pass: Generate clean output with line mappings

### Runtime dependencies

- RE2: Regular expression engine for assembly parsing.  This one
  doesn't work so well with Cmake find_package, so there is a bit of
  PkgConfig in it.

- Boost: Process execution, JSON parsing, headers

- fmt: Modern C++ formatting

- CLI11: Command-line argument parsing

- libclang: C/C++ file parsing.  From LLVM

### Testing

The project has a comprehensive (ahem...) test suite with distinct
test families.  The C++ API tests use the `doctest` framework, the
others with Cmake and some sh scripts.

#### End-to-end API Tests (`test/annotation-tests.cpp`)

- Naming: Prefixed with `api_` (e.g., `api_basic`, `api_demangle`)

- Purpose: Test the core library functions directly using doctest

- Approach: Call `xpto::blot::find_compile_command` to get the
  command, then `xpto::blot::get_asm` to get the asm, then
  `xpto::blot::annotate()` to get the annotation.  Then compare
  results against expected JSON.  Come to think of this these are
  `api` "integration" tests, but

#### Inference API Tests (`test/infer-tests.cpp`)

- Naming: prefix with `api-infer-`.

- Purpose: test just the includer-inferring part

#### CLI Tests (`test/system-tests.cmake`)

- Naming: Prefixed with `cli-` (e.g., `cli-basic-json`)

- Purpose: Test the blot executable's command-line interface

- Approach: Run the blot executable and compare full JSON output

- Implementation: Uses custom script `test/blot-and-compare.sh` for
  JSON comparison

### Test Fixtures (`test/fixture/`)

Inside this directory, we would find a fictitions project of sorts,
whose only purpose is to help check Blot.  There is a
`compile_commands.json`.

Note that both API (C++) and CLI tests use the same fixture files.

Files here:
- `fxt-*.{cpp,hpp,h}` - C++ fixture source files for testing
- `fxt-*.json` - Expected fixture JSON output for comparison
- `compile_commands.json` - Compile commands for fixture files

The `fxt-*.json` files are generated with sth like:

```blot --ccj test/fixture/compile_commands.json test/fixture/fxt-clang-preserve-library-functions.cpp -pl --json | jq```

Where `-pl` was added there because this particular feature is
specifically about testing the "preserve library functions"
functionality.

### Challenges

Obviously, the `blot` program should be working correctly be OK when
making the test fixture, else it'll be like the blind leading the
blind.

Some (if not all) of the fixtures rely on compiler-specifics so these
tests are probably extremely brittle.

### Build Configuration

- Uses AddressSanitizer and UBSan in Debug builds
- Requires C++23 standard
- Generates export compile commands for tooling integration

## Development Practices & Future Work

### Code Organization

- `include/blot/`: Public API headers only (blot.hpp,
  assembly.hpp, ccj.hpp, infer.hpp)

- `src/libblot/`: Core implementation (blot.cpp, assembly.cpp,
  ccj.cpp) and internal utilities (logger.hpp, linespan.hpp,
  utils.hpp, auto.hpp, etc)

- `src/blot/`: Code specific to the CLI util (main.cpp,
  options.{hpp,cpp})

- `test/fixture/`: Test files with dedicated compile_commands.json

The architecture enforces clean separation between public interface
and implementation details. Users of the library only need to include
headers from `include/blot/`, while all internal utilities and
implementation are encapsulated in `src/libblot/`.

### JSON Output Structure

While the project is still young and in flux, this may change.  So
just look at one of the fixture files like
`test/fixture/fxt-clang-demangle.cpp`.

### Major Future Challenges

Read the `README.md` and its "Roadmap" section!
