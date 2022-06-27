// RUN: %clangxx_cilksan -fopencilk -Og %s -o %t
// RUN: %run %t 2>&1 | FileCheck %s
// RUN: %clangxx_cilksan -fopencilk -Og %s -o %t -mllvm -cilksan-maap-checks=false
// RUN: %run %t 2>&1 | FileCheck %s

#include <cilk/cilk.h>
#include <iostream>
#include <mutex>

int x = 0;

int bar()
{
  static std::once_flag initialized;

  std::call_once(initialized, []() {
      std::cout << "initializing x = 1 in bar once" <<  std::endl;
      x = 1;
    }
    );
   return x;
}

int main(int argc, char **argv)
{
  int a = cilk_spawn bar();
  int b = cilk_spawn bar();
  cilk_sync;
  std::cout << "a + b = " << a + b <<  std::endl;

  return 0;
}

// CHECK: Cilksan detected 0 distinct races.
// CHECK-NEXT: Cilksan suppressed 0 duplicate race reports.
