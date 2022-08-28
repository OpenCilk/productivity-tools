// RUN: %clang_cilksan -fopencilk -O0 %s -o %t
// RUN: %run %t 2>&1 | FileCheck %s

#include <cilk/cilk.h>
#include <stdio.h>

_Atomic int x = 0;

void foo(void) {
  while (!x) {
#ifdef __SSE__
    __builtin_ia32_pause();
#endif
#ifdef __aarch64__
    __builtin_arm_yield();
#endif
  }
  ++x;
}

int main(int argc, char *argv[]) {
  cilk_spawn { ++x; }
  foo();
  cilk_sync;
  printf("x=%d\n", x);
  return 0;
}

// CHECK: Running Cilksan race detector
// CHECK-NEXT: x=2
// CHECK: Cilksan detected 0 distinct races.
// CHECK-NEXT: Cilksan suppressed 0 duplicate race reports.
