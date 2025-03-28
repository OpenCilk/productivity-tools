// RUN: %clangxx_cilksan -fopencilk -O0 -g %s -o %t
// RUN: %run %t 2>&1 | FileCheck %s
// RUN: %clangxx_cilksan -fopencilk -Og -g %s -o %t
// RUN: %run %t 2>&1 | FileCheck %s

#include <cstdlib>

int constexpr size = 1 << 10;

__attribute__((noinline))
void f(int* ptr) {
  free(ptr); 
}

__attribute__((noinline))
void g(int * ptr) {
  ptr[0] = 7;
}

int main() {
  int* arr = (int*)malloc(sizeof(int) * size);
  cilk_spawn g(arr);
  cilk_spawn f(arr);
  return 0;
}

// CHECK: Race detected
// CHECK-NEXT: * Write {{[0-9a-f]+}} g
// CHECK-NEXT: to variable
// CHECK-NEXT: Spawn {{[0-9a-f]+}} main
// CHECK-NEXT: * Free
// CHECK-NEXT: Call {{[0-9a-f]+}} main

// CHECK: Cilksan detected 1 distinct races.
// CHECK-NEXT: Cilksan suppressed 0 duplicate race reports.
