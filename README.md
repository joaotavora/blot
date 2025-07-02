# Blot

A compiler-explorer clone, but works with your toolchain and your project.

Very embryonic for now, but one of the goals is for this to be a
central building block in :

* source code editor plugins (either as linked-in library or as a
  subprocess) to get live disassembly of your sources as you edit your
  code in your project.  Not unlike the Emacs plugin
  https://github.com/joaotavora/beardbolt, but much faster and more
  powerful.
  
* no-editing project source code browsers (for example inside code
  forges)

## Build and Usage

```bash
BUILD_TYPE=Debug
BUILD_DIR=build-$BUILD_TYPE
cmake -B $BUILD_DIR -DCMAKE_BUILD_TYPE=$BUILD_TYPE
cmake --build $BUILD_DIR -j
```

Blot can process assembly from three sources:

1. **Source files** (requires entry in `compile_commands.json`): 
   ```bash
   cd test/fixture && ../../build-Debug/blot test01.cpp
   ```
   
   This uses your project's actual build configuration to compile the
   source file and generate assembly.

2. **Assembly files**: 
   ```bash
   echo 'int main() { return 42; }' | g++ -S -g -x c++ - -o file.s
   build-Debug/blot --asm-file=file.s
   ```
   
   Read assembly directly from a pre-generated file.

3. **Piped input**: 
   ```bash
   echo 'int main() { return 42; }' | g++ -S -g -x c++ - -o - | build-Debug/blot
   ```
   Process assembly streamed from standard input.

Add `--json` for structured output with line mappings.

This should produce assembly output similar to Compiler Explorer:

## Development Status

### Completed Features
- ✅ **Basic assembly annotation** with source-to-assembly line mappings
- ✅ **JSON output format** for programmatic access (`--json` flag)
- ✅ **Automated testing framework** with Boost.Test integration
- ✅ **Library architecture** - separation between `libblot` (core) and 
  `blot` (CLI)
- ✅ **Three input modes** - source files, assembly files, piped input

### Roadmap

#### Phase 1: Core Functionality
1. **Expand test coverage** - achieve parity with Compiler Explorer filtering
2. **Auto-demangling support** - integrate with `c++filt` or similar tools
3. **Compilation error handling** - decide where and how to display errors

#### Phase 2: Header File Support
4. **Header file annotation** - heuristically find "includer" translation 
   units that instantiate templates from header files
5. **Inclusion graph walking** - analyze #include dependencies to find 
   suitable compilation targets

#### Phase 3: Live Editing Support  
6. **Virtual file system** - trick compilers into seeing in-memory file 
   representations as filesystem files
7. **Unsaved buffer support** - enable live assembly updates without saving 
   files (critical for editor integration)

The last challenge is particularly complex as no known compiler supports 
compiling filesystem-based translation units while substituting specific 
#included headers with piped input. 
