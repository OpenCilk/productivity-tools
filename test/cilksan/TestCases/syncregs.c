// RUN: %clang_cilksan -fopencilk -Og %s -o %t
// RUN: %run %t 2>&1 | FileCheck %s --check-prefixes=CHECK,CHECK-OG
// RUN: %clang_cilksan -fopencilk -O2 %s -o %t
// RUN: %run %t 2>&1 | FileCheck %s --check-prefixes=CHECK,CHECK-O2

#include <cilk/cilk.h>
#include <stdio.h>
#include <stdlib.h>

int globl = 0;
void bar() {
  // Location of write-write race
  globl++;
}

__attribute__((noinline))
void foo(int n) {
  int sum = 0;
  printf("sum %p\n", (void*)&sum);
  cilk_spawn bar();
  // Write-write race
  cilk_for(int i = 0; i < n; ++i)
    sum += i;
  bar();
  cilk_sync;
}

int main(int argc, char *argv[]) {
  int n = 4096;
  if (argc > 1)
    n = atoi(argv[1]);
  printf("globl %p\n",(void*)&globl);
  foo(n);
  /* cilk_sync; */
  printf("globl = %d\n", globl);
  return 0;
}

// CHECK: globl 0x[[GLOBAL:[0-9a-f]+]]
// CHECK: sum 0x[[SUM:[0-9a-f]+]]

// CHECK: Race detected on location [[SUM]]
// CHECK-NEXT: * Write {{[0-9a-f]+}} foo
// CHECK-NEXT: to variable sum
// CHECK-NEXT: * Read {{[0-9a-f]+}} foo
// CHECK-NEXT: to variable sum
// CHECK: Common calling context
// CHECK-NEXT: Parfor {{[0-9a-f]+}} foo
// CHECK: Stack object sum

// CHECK: Race detected on location [[SUM]]
// CHECK-NEXT: * Write {{[0-9a-f]+}} foo
// CHECK-NEXT: to variable sum
// CHECK-NEXT: * Write {{[0-9a-f]+}} foo
// CHECK-NEXT: to variable sum
// CHECK: Common calling context
// CHECK-NEXT: Parfor {{[0-9a-f]+}} foo
// CHECK: Stack object sum

// CHECK: Race detected on location [[GLOBAL]]
// CHECK-NEXT: * Write {{[0-9a-f]+}} bar
// CHECK-NEXT: to variable globl
// CHECK-NEXT: Spawn {{[0-9a-f]+}} foo
// CHECK-NEXT: * Read {{[0-9a-f]+}} bar
// CHECK-NEXT: to variable globl
// CHECK-NEXT-OG: Call {{[0-9a-f]+}} foo
// CHECK: Common calling context
// CHECK-NEXT: Call {{[0-9a-f]+}} main

// CHECK: Race detected on location [[GLOBAL]]
// CHECK-NEXT: * Write {{[0-9a-f]+}} bar
// CHECK-NEXT: to variable globl
// CHECK-NEXT: Spawn {{[0-9a-f]+}} foo
// CHECK-NEXT: * Write {{[0-9a-f]+}} bar
// CHECK-NEXT: to variable globl
// CHECK: Common calling context
// CHECK-NEXT: Call {{[0-9a-f]+}} main

// CHECK-O2: Race detected on location [[GLOBAL]]
// CHECK-O2-NEXT: * Read {{[0-9a-f]+}} bar
// CHECK-O2-NEXT: to variable globl
// CHECK-O2-NEXT: Spawn {{[0-9a-f]+}} foo
// CHECK-O2-NEXT: * Write {{[0-9a-f]+}} bar
// CHECK-O2-NEXT: to variable globl
// CHECK-O2: Common calling context
// CHECK-O2-NEXT: Call {{[0-9a-f]+}} main

// CHECK-OG: Cilksan detected 4 distinct races.
// CHECK-O2: Cilksan detected 5 distinct races.
// CHECK-NEXT: Cilksan suppressed {{[0-9]+}} duplicate race reports.
