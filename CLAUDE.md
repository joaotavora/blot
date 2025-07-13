# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when
working with code in this repository.

## Build System & Commands

This is a CMake-based C++23 project using modern build practices:

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

The project generates `compile_commands.json` automatically in both
build directories, with a symlink at the project root pointing to
`build-Release/compile_commands.json`.

## Project Architecture

**Blot** is a compiler-explorer clone that works with your local
toolchain and project. It processes assembly output to create
annotated, cleaned assembly with source-to-assembly line mappings.

### Core Components

- **Main executable**: `blot` - processes source files or assembly input
- **Assembly processing**: Two-pass algorithm that filters and annotates assembly
- **Compile commands integration**: Automatically finds and uses `compile_commands.json`
- **Line mapping**: Tracks correspondence between source and assembly lines

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

1. **Input Resolution**: Source file → compile command → assembly
   generation, direct assembly input from passed-in filename or from
   stdin.
2. **First Pass**: Parse assembly to identify functions, labels, and
   source mappings
3. **Intermediate**: Determine which symbols to preserve based on
   options
4. **Second Pass**: Generate clean output with line mappings

### Dependencies

- **RE2**: Regular expression engine for assembly parsing
- **Boost**: Process execution, JSON parsing, headers
- **fmt**: Modern C++ formatting
- **CLI11**: Command-line argument parsing

### Testing

The project includes test files in `test/` directory that are compiled into a fixture library for reference in `compile_commands.json`.

### Build Configuration

- Uses AddressSanitizer and UBSan in Debug builds
- Requires C++23 standard
- Generates export compile commands for tooling integration

## Development Practices & Future Work

### Code Organization
- **`include/blot/`**: Public API headers only (blot.hpp, assembly.hpp, ccj.hpp)
- **`src/libblot/`**: Core implementation (blot.cpp, assembly.cpp, ccj.cpp) and internal utilities (logger.hpp, linespan.hpp)
- **`src/blot/`**: Application-specific code (main.cpp, options.{hpp,cpp})
- **`test/fixture/`**: Test files with dedicated compile_commands.json

The architecture enforces clean separation between public interface and implementation details. Users of the library only need to include headers from `include/blot/`, while all internal utilities and implementation are encapsulated in `src/libblot/`.

### Testing Strategy
- Automated tests compare `xpto::blot::annotate()` output against expected JSON
- Generate expectations with `blot --json | jq` for human readability
- Tests use real compile commands and call core functions directly
- **Next step**: Integrate with Compiler Explorer API (godbolt.org/api) for automated fixture generation and validation

#### Running Tests
```bash
# Run tests without colored output (prevents terminal formatting issues)
build-Debug/test_blot --no_color_output

# Regenerate test expectations from project root
build-Debug/blot --compile_commands test/fixture/compile_commands.json test/fixture/test02.cpp --json | jq > test/fixture/test02.json
```

### JSON Output Structure
```json
{
  "assembly": ["instruction1", "instruction2", ...],
  "line_mappings": {
    "source_line": [{"start": asm_line, "end": asm_line}, ...]
  }
}
```

Note: Single source lines can map to multiple assembly ranges.

### Commit Style
- Use GNU ChangeLog format with concise file-level entries
- Structure: headline + brief explanation (1-2 sentences max) + file breakdown
- File-by-file patterns:
  - `* path/file.ext: New file.`
  - `* file.cpp: Include header.h.`  
  - `* file.cpp (function_name): Rework.`
  - `* CMakeLists.txt (target): Add/Remove/Update setting.`
- Keep descriptions minimal - focus on WHAT changed, not HOW
- "Rework" is the default for substantial changes, "Tweak" for minor changes
- Be more specific when it adds clarity ("Add error handling", "Fix path resolution")
- Key: stay concise while being appropriately descriptive
- NO "Co-Authored-By" footers

### Major Future Challenges

#### Header File Support
**The Big Problem**: Supporting assembly generation for header files (.hpp). Templates and inline functions in headers only generate code when instantiated. Solution requires:
1. Walking inclusion graphs to find .cpp files that include the target header
2. Finding suitable translation units that actually instantiate the templates

#### Unsaved Changes Support  
**The BIGGEST Problem**: Supporting unsaved editor buffers. Easy for .cpp files (pipe to stdin), but extremely difficult for headers. No known compiler supports compiling a filesystem-based translation unit while substituting piped stdin for specific #included headers during compilation. This is critical for live-coding workflows where users don't want to save files to see assembly changes.
