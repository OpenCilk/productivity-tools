// RUN: %clang_cilksan -fopencilk -Og %s -o %t
// RUN: %run %t 2>&1 | FileCheck %s --check-prefix=CHECK-N0
// RUN: %run %t 1 2>&1 | FileCheck %s --check-prefix=CHECK-N1

#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include <cilk/cilk.h>

int main(int argc, char *argv[]) {
  long n = 0;
  if (argc > 1)
    n = atol(argv[1]);

  // Check that instrumentation on this allocation is handled
  // correctly, even when n == 0.
  long x[n];

  // Ensure that we have racing accesses on x, so that x is
  // instrumented.
  cilk_spawn {
    cilk_for (long i = 0; i < n; ++i)
      x[i] = sin(i);
  }
  cilk_for (long i = 0; i < n; ++i)
    x[i] = cos(i);

  for (long i = 0; i < n; ++i)
    printf("sin(%ld) = %ld\n", i, x[i]);

  return 0;
}

// CHECK-N0: Cilksan detected 0 distinct races.
// CHECK-NEXT-N0: Cilksan suppressed 0 duplicate race reports.

// CHECK-N1: Race detected on location {{[0-9a-f]+}}
// CHECK-N1: * Write
// CHECK-N1: Spawn
// CHECK-N1: * Write

// CHECK-N1: sin(0) = 1

// CHECK-N1: Cilksan detected 2 distinct races.
// CHECK-NEXT-N1: Cilksan suppressed 0 duplicate race reports.
