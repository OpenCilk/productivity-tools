// RUN: %clangxx_cilksan -fopencilk -Og %s -o %t -g
// RUN: %run %t 2>&1 | FileCheck %s

#include <iostream>
#include <cilk/cilk.h>

int global = 0;

__attribute__((noinline))
void increment(int *x, int n) {
  for (int i = 0; i < n; i++)
    x[i]++;
}

__attribute__((noinline))
void helper(int *x) {
  (*x)++;
}

int main(int argc, char** argv) {
  int n = 1;
  if (argc == 2) n = atoi(argv[1]);

  cilk_for (int i = 0; i < 1000; i++)
    helper(&global);
  std::cout << (void*)&global << " " << global << '\n';

// CHECK: Race detected on location [[GLOBAL:[0-9a-f]+]]
// CHECK-NEXT: * Write {{[0-9a-f]+}} helper
// CHECK-NEXT: to variable x
// CHECK-NEXT: Call {{[0-9a-f]+}} main
// CHECK-NEXT: * Read {{[0-9a-f]+}} helper
// CHECK-NEXT: to variable x
// CHECK-NEXT: Call {{[0-9a-f]+}} main
// CHECK-NEXT: Common calling context
// CHECK-NEXT: Parfor

// CHECK: Race detected on location [[GLOBAL]]
// CHECK-NEXT: * Write {{[0-9a-f]+}} helper
// CHECK-NEXT: to variable x
// CHECK-NEXT: Call {{[0-9a-f]+}} main
// CHECK-NEXT: * Write {{[0-9a-f]+}} helper
// CHECK-NEXT: to variable x
// CHECK-NEXT: Call {{[0-9a-f]+}} main
// CHECK-NEXT: Common calling context
// CHECK-NEXT: Parfor

// CHECK: 0x[[GLOBAL]]

  int local = 1;
  cilk_for (int i = 0; i < 1000; i++)
    helper(&local);
  std::cout << (void*)&local << " " << local << '\n';

// CHECK: Race detected on location [[LOCAL:[0-9a-f]+]]
// CHECK-NEXT: * Write {{[0-9a-f]+}} helper
// CHECK-NEXT: to variable x
// CHECK-NEXT: Call {{[0-9a-f]+}} main
// CHECK-NEXT: * Read {{[0-9a-f]+}} helper
// CHECK-NEXT: to variable x
// CHECK-NEXT: Call {{[0-9a-f]+}} main
// CHECK-NEXT: Common calling context
// CHECK-NEXT: Parfor
// CHECK: Stack object local

// CHECK: Race detected on location [[LOCAL]]
// CHECK-NEXT: * Write {{[0-9a-f]+}} helper
// CHECK-NEXT: to variable x
// CHECK-NEXT: Call {{[0-9a-f]+}} main
// CHECK-NEXT: * Write {{[0-9a-f]+}} helper
// CHECK-NEXT: to variable x
// CHECK-NEXT: Call {{[0-9a-f]+}} main
// CHECK-NEXT: Common calling context
// CHECK-NEXT: Parfor
// CHECK: Stack object local

// CHECK: 0x[[LOCAL]]

  int *x = (int*)malloc(n * sizeof(int));
  cilk_for (int i = 0; i < 1000; i++)
    increment(x, n);
  std::cout << (void*)x << " " << x[0] << '\n';

// CHECK: Race detected on location [[MALLOC:[0-9a-f]+]]
// CHECK-NEXT: * Write {{[0-9a-f]+}} increment
// CHECK-NEXT: to variable x
// CHECK-NEXT: Call {{[0-9a-f]+}} main
// CHECK-NEXT: * Read {{[0-9a-f]+}} increment
// CHECK-NEXT: to variable x
// CHECK-NEXT: Call {{[0-9a-f]+}} main
// CHECK-NEXT: Common calling context
// CHECK-NEXT: Parfor
// CHECK: Heap object x

// CHECK: Race detected on location [[MALLOC]]
// CHECK-NEXT: * Write {{[0-9a-f]+}} increment
// CHECK-NEXT: to variable x
// CHECK-NEXT: Call {{[0-9a-f]+}} main
// CHECK-NEXT: * Write {{[0-9a-f]+}} increment
// CHECK-NEXT: to variable x
// CHECK-NEXT: Call {{[0-9a-f]+}} main
// CHECK-NEXT: Common calling context
// CHECK-NEXT: Parfor
// CHECK: Heap object x

// CHECK: 0x[[MALLOC]]

  int *y = (int*)calloc(n, sizeof(int));
  cilk_for (int i = 0; i < 1000; i++)
    increment(y, n);
  std::cout << (void*)y << " " << y[0] << '\n';

// CHECK: Race detected on location [[CALLOC:[0-9a-f]+]]
// CHECK-NEXT: * Write {{[0-9a-f]+}} increment
// CHECK-NEXT: to variable x
// CHECK-NEXT: Call {{[0-9a-f]+}} main
// CHECK-NEXT: * Read {{[0-9a-f]+}} increment
// CHECK-NEXT: to variable x
// CHECK-NEXT: Call {{[0-9a-f]+}} main
// CHECK-NEXT: Common calling context
// CHECK-NEXT: Parfor
// CHECK: Heap object y

// CHECK: Race detected on location [[CALLOC]]
// CHECK-NEXT: * Write {{[0-9a-f]+}} increment
// CHECK-NEXT: to variable x
// CHECK-NEXT: Call {{[0-9a-f]+}} main
// CHECK-NEXT: * Write {{[0-9a-f]+}} increment
// CHECK-NEXT: to variable x
// CHECK-NEXT: Call {{[0-9a-f]+}} main
// CHECK-NEXT: Common calling context
// CHECK-NEXT: Parfor
// CHECK: Heap object y

// CHECK: 0x[[CALLOC]]

  int *z = (int*)realloc(x, 4 * n * sizeof(int));
  cilk_for (int i = 0; i < 1000; i++)
    increment(z, 2 * n);
  std::cout << (void*)z << " " << z[0] << '\n';

// CHECK: Race detected on location [[REALLOC:[0-9a-f]+]]
// CHECK-NEXT: * Write {{[0-9a-f]+}} increment
// CHECK-NEXT: to variable x
// CHECK-NEXT: Call {{[0-9a-f]+}} main
// CHECK-NEXT: * Read {{[0-9a-f]+}} increment
// CHECK-NEXT: to variable x
// CHECK-NEXT: Call {{[0-9a-f]+}} main
// CHECK-NEXT: Common calling context
// CHECK-NEXT: Parfor
// CHECK: Heap object z

// CHECK: Race detected on location [[REALLOC]]
// CHECK-NEXT: * Write {{[0-9a-f]+}} increment
// CHECK-NEXT: to variable x
// CHECK-NEXT: Call {{[0-9a-f]+}} main
// CHECK-NEXT: * Write {{[0-9a-f]+}} increment
// CHECK-NEXT: to variable x
// CHECK-NEXT: Call {{[0-9a-f]+}} main
// CHECK-NEXT: Common calling context
// CHECK-NEXT: Parfor
// CHECK: Heap object z

// CHECK: 0x[[REALLOC]]

  free(z);
  free(y);
  return 0;
}

// CHECK: Cilksan detected 10 distinct races.
