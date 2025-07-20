# Spoof: Filesystem Spoofing Utility

A proof-of-concept utility for intercepting filesystem reads using Linux
user+mount namespaces and overlayfs.

## Purpose

This utility demonstrates a technique needed for blot's "unsaved buffer
support" feature (see main README roadmap). Editor plugins need to show
assembly for unsaved buffers without requiring users to save files first.
Since compilers read from the filesystem, we need to trick them into seeing
in-memory content as if it were a real file.

## How It Works

`spoof_exe` uses Linux kernel features to create an isolated view of the
filesystem:

1. **Reads content from stdin** - This represents the "unsaved buffer" content
2. **Creates user+mount namespace** - Isolates the process from the system's
   normal filesystem view
3. **Sets up overlayfs** - Overlays a temporary directory (containing the
   spoofed file) over the current working directory
4. **Runs the target command** - The command sees the spoofed content when
   reading the specified file
5. **Cleans up** - Original files remain completely unchanged

The overlayfs configuration uses two read-only lower layers, with the
temporary directory having higher priority. When the command tries to open the
target file, the kernel finds it first in the overlay layer.

## Building

From the blot project root:

```bash
cmake --build build-Debug --target spoof_exe
```

The executable will be at `build-Debug/spoof_exe`.

## Usage

```bash
spoof_exe <filename> <command> [args...]
```

**Arguments:**
- `<filename>` - The file to spoof (relative to current directory)
- `<command>` - The command to run (use full path like `/usr/bin/cat`)
- `[args...]` - Arguments to pass to the command

**Important:** Content to spoof must be provided via stdin.

## Examples

### Basic File Reading

```bash
# Create a test file
echo 'int main() { return 0; }' > test.cpp

# Show original content
cat test.cpp
# Output: int main() { return 0; }

# Run cat with spoofed content
echo 'int main() { return 99; }' | build-Debug/spoof_exe test.cpp /usr/bin/cat test.cpp
# Output: int main() { return 99; }

# Verify original file is unchanged
cat test.cpp
# Output: int main() { return 0; }
```

### Compiler Example (Real Use Case)

This demonstrates the actual use case for blot - compiling unsaved buffer
content:

```bash
# Create original source file
echo 'int main() { return 0; }' > /tmp/source.cpp

# Compile with spoofed content that returns a different value
echo 'int main() { return 42; }' | \
  build-Debug/spoof_exe /tmp/source.cpp /usr/bin/g++ -S -O2 /tmp/source.cpp -o -

# The compiler sees and compiles the spoofed content (return 42)
# But source.cpp on disk still contains the original (return 0)
```

### Validating the Principle

To verify the spoofing works correctly:

```bash
# 1. Create a test file
echo "original content" > myfile.txt

# 2. View it normally
cat myfile.txt
# Output: original content

# 3. View it with spoofing
echo "spoofed content" | build-Debug/spoof_exe myfile.txt /usr/bin/cat myfile.txt
# Output: spoofed content

# 4. Verify original unchanged
cat myfile.txt
# Output: original content
```

The file on disk never changes, but the spoofed command sees the stdin content.

## Requirements

- Linux kernel with namespace support (user and mount namespaces)
- overlayfs support (standard in modern kernels)
- Unprivileged user namespace support enabled

## Limitations

- **Linux-only** - Uses Linux-specific namespace and overlayfs features
- **Command path** - Must provide full path to executables (e.g., `/usr/bin/cat`
  not just `cat`) due to namespace isolation
- **Working directory** - The spoofed file path is relative to the current
  directory where you run spoof_exe
- **Single file** - Currently spoofs only one file per invocation

## Integration with Blot

This proof-of-concept validates the core technique. Future blot integration
will:

1. Accept source content via stdin or as a parameter
2. Use spoof internally when invoking compilers
3. Handle multiple files (source + headers)
4. Manage the complexity transparently for editor plugins

The editor plugin workflow will be:

```
Editor (unsaved buffer) → blot (spoofs files) → compiler (sees spoofed content) → assembly
```

All without touching the filesystem or requiring file saves.
