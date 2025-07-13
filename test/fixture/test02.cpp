// Test demangling with classes and methods
class Calculator {
 public:
  int add(int a, int b) { return a + b; }

  template <typename T>
  T multiply(T a, T b) {
    return a * b;
  }
};

namespace math {
void complex_function() {
  // Empty function for symbol testing
}
}  // namespace math

int main() {
  Calculator calc;
  int result = calc.add(5, 3);
  auto product = calc.multiply(2.5, 4.0);
  math::complex_function();
  return result;
}