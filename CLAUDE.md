# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

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

# Run the tool
$BUILD_DIR/blot --debug=3 test/test01.cpp
```

The project generates `compile_commands.json` automatically in both build directories, with a symlink at the project root pointing to `build-Release/compile_commands.json`.

## Project Architecture

**Blot** is a compiler-explorer clone that works with your local toolchain and project. It processes assembly output to create annotated, cleaned assembly with source-to-assembly line mappings.

### Core Components

- **Main executable**: `blot` - processes source files or assembly input
- **Assembly processing**: Two-pass algorithm that filters and annotates assembly
- **Compile commands integration**: Automatically finds and uses `compile_commands.json`
- **Line mapping**: Tracks correspondence between source and assembly lines

### Key Modules

- `src/blot/main.cpp` - Entry point and workflow orchestration
- `include/blot/blot.hpp` - Core annotation engine with two-pass processing
- `src/blot/ccj.{hpp,cpp}` - `compile_commands.json` parsing and lookup
- `src/blot/assembly.{hpp,cpp}` - Assembly generation from compiler commands
- `src/blot/options.{hpp,cpp}` - CLI argument parsing
- `include/blot/logger.hpp` - Logging infrastructure
- `include/blot/linespan.hpp` - Efficient line-based text processing

### Processing Pipeline

1. **Input Resolution**: Source file → compile command → assembly generation, or direct assembly input
2. **First Pass**: Parse assembly to identify functions, labels, and source mappings
3. **Intermediate**: Determine which symbols to preserve based on options
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
- **`src/libblot/`**: Core reusable functionality (assembly, ccj)
- **`src/blot/`**: Program-specific code (main, options)
- **`include/blot/`**: Public API headers with clean separation
- **`test/fixture/`**: Test files with dedicated compile_commands.json

### Testing Strategy
- Automated tests compare `xpto::blot::annotate()` output against expected JSON
- Generate expectations with `blot --json | jq` for human readability
- Tests use real compile commands and call core functions directly
- **Next step**: Integrate with Compiler Explorer API (godbolt.org/api) for automated fixture generation and validation

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
- Use GNU ChangeLog format with file-level entries
- Structure: headline, explanation paragraphs, then `* file.cpp (function): Change.`

### Major Future Challenges

#### Header File Support
**The Big Problem**: Supporting assembly generation for header files (.hpp). Templates and inline functions in headers only generate code when instantiated. Solution requires:
1. Walking inclusion graphs to find .cpp files that include the target header
2. Finding suitable translation units that actually instantiate the templates

#### Unsaved Changes Support  
**The BIGGEST Problem**: Supporting unsaved editor buffers. Easy for .cpp files (pipe to stdin), but extremely difficult for headers. No known compiler supports compiling a filesystem-based translation unit while substituting piped stdin for specific #included headers during compilation. This is critical for live-coding workflows where users don't want to save files to see assembly changes.