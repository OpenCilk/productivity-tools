// Check that Cilksan correctly verifies that frees of realloc'd
// pointers do not race.
//
// Thanks to bababuck for providing the original source code for this
// issue.
//
// RUN: %clang_cilksan -fopencilk -O3 %s -o %t
// RUN: %run %t 2>&1 | FileCheck %s

#include <stdlib.h>
#include <cilk/cilk.h>

void test(int *arr, int count) {
  if (count == 0) {
    free(arr);
    return ;
  }

  cilk_scope {
    for (int i = 0; i < 15; ++i) {
      int *new_arr = malloc(10 * sizeof(int));
      new_arr = realloc(new_arr, 10 * sizeof(int));
      cilk_spawn test(new_arr, count - 1);
    }
  }
  return ;
}

int main(int argc, char *argv[]) {
  test(NULL, 1);
}

// CHECK-NOT: Race detected on location

// CHECK: Cilksan detected 0 distinct races.
// CHECK-NEXT: Cilksan suppressed 0 duplicate race reports.
