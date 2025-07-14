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

Blot can annotate from 2 sources:

1. C++ Source files

   This requires an entry for the file in `compile_commands.json`.
   Currently only C/C++ translation units are supported, header files
   are not!
  
   ```bash
   build-Debug/blot --ccj test/fixture/compile_commands.json test/fixture/fxt_basic.cpp
   ```
   
   This uses your project's actual build configuration to compile the
   source file and generate assembly.

2. Some assembly code you might have generated with `gcc` or `clang`

   Either pass it a file containing this assembly:

   ```bash
   echo 'int main() { return 42; }' | g++ -S -g -x c++ - -o file.s
   build-Debug/blot --asm-file=file.s
   ```
   
   Or pipe it into `blot`'s stdin:

   ```bash
   echo 'int main() { return 42; }' | g++ -S -g -x c++ - -o - | build-Debug/blot
   ```

Add `--json` for structured output with line mappings.
Add `--demangle` for demangled output.

### Roadmap

#### Phase 1: Core Functionality
1. Expand test coverage.  Achieve parity with Compiler Explorer filtering.
2. Auto-demangling support.  Demangle symbols using `cxxabi.h` (mostly done)
3. Compilation error handling.  Decide where and how to display errors

#### Phase 2: Header File Support
4. Header file annotation.  Heuristically find "includer" translation
   units that instantiate templates from header files
5. Inclusion graph walking.  Analyze #include dependencies to find
   suitable compilation targets

#### Phase 3: Live Editing Support
6. Virtual file system.  Trick compilers into seeing in-memory file 
   representations as filesystem files
7. Unsaved buffer support.  Enable live assembly updates without
   saving files (critical for editor integration)
   
#### Phase 4: Other niceties
8. Decent-ish C/C++ stable API and ABI.  The so-called
   "hourglass" pattern might come handy
9. CI system

The last challenge is particularly complex as no known compiler supports 
compiling filesystem-based translation units while substituting specific 
#included headers with piped input. 
