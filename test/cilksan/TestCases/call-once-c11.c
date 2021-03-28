// RUN: %clang_cilksan -fopencilk -Og %s -o %t
// RUN: %run %t 2>&1 | FileCheck %s

#include <cilk/cilk.h>
#include <stdio.h>
#include <threads.h>

int x = 0;

void do_once() {
  printf("initializing x = 1 just once\n");
  x = 1;
}

int bar()
{
  static once_flag initialized;

  call_once(&initialized, do_once);
  return x;
}

int main(int argc, char **argv)
{
  int a = cilk_spawn bar();
  int b = cilk_spawn bar();
  cilk_sync;

  printf("a + b = %d\n", a+b);

  return 0;
}

// CHECK: Cilksan detected 0 distinct races.
// CHECK-NEXT: Cilksan suppressed 0 duplicate race reports.
