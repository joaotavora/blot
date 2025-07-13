// Test with library functions and external calls
#include <cstdlib>
#include <cstring>

static int internal_function() { return 100; }

int main() {
  // Mix of library calls and local functions
  char* buffer = (char*)malloc(64);
  strcpy(buffer, "test");

  int local_result = internal_function();

  free(buffer);
  return local_result;
}