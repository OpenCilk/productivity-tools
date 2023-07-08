// RUN: %clang_cilksan -fopencilk %s -o %t
// RUN: %run %t 2>&1 | FileCheck %s --check-prefixes=CHECK,CHECK-N3
// RUN: %run %t 20 2>&1 | FileCheck %s --check-prefixes=CHECK,CHECK-N20

// RUN: %clang_cilksan -fopencilk -Og %s -o %t
// RUN: %run %t 2>&1 | FileCheck %s --check-prefixes=CHECK,CHECK-N3
// RUN: %run %t 20 2>&1 | FileCheck %s --check-prefixes=CHECK,CHECK-N20

// RUN: %clang_cilksan -fopencilk -O3 %s -o %t
// RUN: %run %t 2>&1 | FileCheck %s --check-prefixes=CHECK,CHECK-N3
// RUN: %run %t 20 2>&1 | FileCheck %s --check-prefixes=CHECK,CHECK-N20

#include <stdio.h>
#include <stdlib.h>

void add_long(void *l, void *r) {
  *(long *)l += *(long *)r;
}
void zero_long(void *v) {*(long *)v = 0;}

typedef long _Hyperobject(zero_long, add_long) long_hyper;
long_hyper sum = 0;

long recurse(int x)
{
  long_hyper var1 = 1, var2 = 1;
  if (x > 1)
    _Cilk_spawn var1 += recurse(x - 1);
  if (x > 2)
    _Cilk_spawn var2 += recurse(x - 2);
  sum += 2;
  _Cilk_sync;
  return var1 + var2;
}

int main(int argc, char *argv[])
{
  long arg = 3;
  if (argc > 1) {
    arg = atol(argv[1]);
    if (arg < 3)
      arg = 3;
  }
  long out = recurse(arg);
  printf("f(%ld) = %ld (sum = %ld)\n", arg, out, sum);
  return fflush(stdout) != 0;
}

// CHECK-N3: f(3) = 8 (sum = 8)
// CHECK-N20: f(20) = 35420 (sum = 35420)
// CHECK: Cilksan detected 0 distinct races
