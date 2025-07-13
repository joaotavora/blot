// Test with comments and directives
#include <vector>

/* Block comment before function */
int process_data() {
  // Line comment inside function
  std::vector<int> data = {1, 2, 3, 4, 5};

  int sum = 0;
  for (int val : data) {  // Another line comment
    sum += val;
  }

  return sum;
}

int main() { return process_data(); }