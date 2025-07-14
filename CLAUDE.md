# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when
working with code in this repository.  Actually, it should provide
guidance to anyone wanting to contribute to this, this is sort of a
`CONTRIBUTING.md`.

## Dev builds

This is a CMake-based C++23 project using modern build practices.

```bash
# Debug build (recommended for development)
BUILD_TYPE=Debug
BUILD_DIR=build-$BUILD_TYPE
cmake -B $BUILD_DIR -DCMAKE_BUILD_TYPE=$BUILD_TYPE
cmake --build $BUILD_DIR -j

# Release build
BUILD_TYPE=Release
BUILD_DIR=build-$BUILD_TYPE
cmake -B $BUILD_DIR -DCMAKE_BUILD_TYPE=$BUILD_TYPE
cmake --build $BUILD_DIR -j

# Run the tool (see README for more ways to run it)
echo 'int main() { return 42; }' | g++ -S -g -x c++ - -o - | build-Debug/blot
```

Cmake builds generate `compile_commands.json` automatically in both
build directories, with a symlink at the project root pointing to
`build-Release/compile_commands.json`.

## Non-dev builds

There are none.  If you want to vendor this you're on your own for now.

## Package manager

There is none, for now.  There are runtime dependencies, yes, and
Cmake will look for them.  Install them however you see fit so that
Cmake can find them.

## Project Architecture

Blot is a compiler-explorer clone that works with your local
toolchain and project. It processes assembly output to create
annotated, cleaned assembly with source-to-assembly line mappings.

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

### Testing

The project has a comprehensive (ahem...) test suite with two distinct
test families:

#### API Tests (`test/test_blot.cpp`)

- Naming: Prefixed with `api_` (e.g., `api_basic`, `api_demangle`)

- Purpose: Test the core library functions directly using doctest

- Approach: Call `xpto::blot::annotate()` and compare results against
  expected JSON

- Framework: Uses doctest for test execution and assertions

#### CLI Tests (`test/system_tests.cmake`)

- Naming: Prefixed with `cli_` (e.g., `cli_basic_json`)

- Purpose: Test the blot executable's command-line interface

- Approach: Run the blot executable and compare full JSON output

- Implementation: Uses custom script `test/blot_and_compare.sh` for
  JSON comparison

#### Test Fixtures (`test/fixture/`)

- Naming: Prefixed with `fxt_` (e.g., `fxt_basic.cpp`,
  `fxt_basic.json`)

- Shared Usage: Both API and CLI tests use the same fixture files

- Components:
  - `fxt_*.cpp` - C++ source files for testing
  - `fxt_*.json` - Expected JSON output for comparison
  - `compile_commands.json` - Compile commands for fixture files
  
- Build Integration: Fixture files are compiled into a library for
  `compile_commands.json` reference

### Build Configuration

- Uses AddressSanitizer and UBSan in Debug builds
- Requires C++23 standard
- Generates export compile commands for tooling integration

## Development Practices & Future Work

### Code Organization

- `include/blot/`: Public API headers only (blot.hpp,
  assembly.hpp, ccj.hpp)

- `src/libblot/`: Core implementation (blot.cpp, assembly.cpp,
  ccj.cpp) and internal utilities (logger.hpp, linespan.hpp)

- `src/blot/`: Application-specific code (main.cpp,
  options.{hpp,cpp})

- `test/fixture/`: Test files with dedicated compile_commands.json

The architecture enforces clean separation between public interface
and implementation details. Users of the library only need to include
headers from `include/blot/`, while all internal utilities and
implementation are encapsulated in `src/libblot/`.

### Testing Strategy
- Automated tests compare `xpto::blot::annotate()` output against
  expected JSON

- Generate expectations with `blot --json | jq` for human readability

- Tests use real compile commands and call core functions directly

- Next step: Integrate with Compiler Explorer API (godbolt.org/api)
  for automated fixture generation and validation


#### Running Tests
```bash
# Run all tests (doctest automatically provides clean output)
build-Debug/test_blot

# Run with CTest for individual test case reporting
ctest --verbose

# Regenerate test expectations from project root
build-Debug/blot --compile_commands test/fixture/compile_commands.json test/fixture/fxt_basic.cpp --json | jq > test/fixture/fxt_basic.json
```

### JSON Output Structure

While the project is still young and in flux, this may change.  So
just look at one of the fixture files like
`test/fixture/preserve_directives.json`

### Major Future Challenges

#### Header File Support
The Big Problem: Supporting assembly generation for header files
(.hpp). Templates and inline functions in headers only generate code
when instantiated. Solution requires:

1. Walking inclusion graphs to find .cpp files that include the target header

2. Finding suitable translation units that actually instantiate the
   templates

#### Unsaved Changes Support
The BIGGEST Problem: Supporting unsaved editor buffers.

Easy for .cpp files (pipe to stdin), but extremely difficult for
headers. No known compiler supports compiling a filesystem-based
translation unit while substituting piped stdin for specific #included
headers during compilation. This is critical for live-coding workflows
where users don't want to save files to see assembly changes.
