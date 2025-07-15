// Test fixture with compile errors
// Invalid include and undeclared variable

#include <frankinbogen>  // Invalid/non-existent header

int main() {
    return some_undefined_var;  // Undeclared variable
}