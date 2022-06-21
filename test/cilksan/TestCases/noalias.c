// RUN: %clang_cilksan -fopencilk -Og %s -o %t
// RUN: %run %t 2>&1 | FileCheck %s

#include <stdio.h>
#include <stdlib.h>
#include <cilk/cilk.h>

__attribute__((noinline))
void inc(int *x) {
  cilk_spawn x[0]++;
  x[1]++;
}

__attribute__((noinline))
void inc_loop(int *x, int *y) {
  #pragma cilk grainsize 1
  cilk_for (int i = 0; i < 2; ++i) {
    x[i]++;
    y[i]++;
  }
}

__attribute__((noinline))
int *frob(int *x) {
  return x+1;
}

int main(int argc, char *argv[]) {
  int n = 10;
  if (argc > 1)
    n = atoi(argv[1]);

  int *x = (int*)malloc(n * sizeof(int));
  cilk_for (int i = 0; i < n; ++i)
    x[i] = 0;

  // Race
  inc_loop(x, x+1);

// CHECK: Race detected on location [[X1:[0-9a-f]+]]
// CHECK-NEXT: * Write {{[0-9a-f]+}} inc_loop
// CHECK: * Read {{[0-9a-f]+}} inc_loop
// CHECK: Common calling context
// CHECK-NEXT: Parfor

// CHECK: Race detected on location [[X1]]
// CHECK-NEXT: * Write {{[0-9a-f]+}} inc_loop
// CHECK: * Write {{[0-9a-f]+}} inc_loop
// CHECK: Common calling context
// CHECK-NEXT: Parfor

// CHECK: Race detected on location [[X1]]
// CHECK-NEXT: * Read {{[0-9a-f]+}} inc_loop
// CHECK: * Write {{[0-9a-f]+}} inc_loop
// CHECK: Common calling context
// CHECK-NEXT: Parfor

  // Duplicate race
  inc_loop(x+2, frob(x+2));

  // Race
  cilk_spawn inc(x);
  inc(x+1);
  cilk_sync;

// CHECK: Race detected on location [[X1]]
// CHECK-NEXT: * Write {{[0-9a-f]+}} inc
// CHECK: Call {{[0-9a-f]+}} main
// CHECK-NEXT: Spawn {{[0-9a-f]+}} main
// CHECK-NEXT: * Read {{[0-9a-f]+}} inc
// CHECK: Spawn {{[0-9a-f]+}} inc
// CHECK-NEXT: Call {{[0-9a-f]+}} main

// CHECK: Race detected on location [[X1]]
// CHECK-NEXT: * Write {{[0-9a-f]+}} inc
// CHECK: Call {{[0-9a-f]+}} main
// CHECK-NEXT: Spawn {{[0-9a-f]+}} main
// CHECK-NEXT: * Write {{[0-9a-f]+}} inc
// CHECK: Spawn {{[0-9a-f]+}} inc
// CHECK-NEXT: Call {{[0-9a-f]+}} main

  
// CHECK: Race detected on location [[X1]]
// CHECK-NEXT: * Read {{[0-9a-f]+}} inc
// CHECK: Call {{[0-9a-f]+}} main
// CHECK-NEXT: Spawn {{[0-9a-f]+}} main
// CHECK-NEXT: * Write {{[0-9a-f]+}} inc
// CHECK: Spawn {{[0-9a-f]+}} inc
// CHECK-NEXT: Call {{[0-9a-f]+}} main

  // Duplicate race
  cilk_spawn inc(x+3);
  inc(frob(x+3));
  cilk_sync;

  for (int i = 0; i < n; ++i)
    printf("x[%d] = %d\n", i, x[i]);

  return 0;
}

// CHECK: Cilksan detected 6 distinct races.
// CHECK-NEXT: Cilksan suppressed 6 duplicate race reports.
