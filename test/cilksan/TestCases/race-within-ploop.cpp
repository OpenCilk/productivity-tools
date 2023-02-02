// RUN: %clangxx_cilksan -fopencilk -Og %s -o %t
// RUN: %run %t 2>&1 | FileCheck %s --check-prefixes=CHECK,CHECK-Og
// RUN: %clangxx_cilksan -fopencilk -O2 %s -o %t
// RUN: %run %t 2>&1 | FileCheck %s --check-prefixes=CHECK,CHECK-O2
#include <cstdio>
#include <cstdlib>
#include <vector>

#include <cilk/cilk.h>
#include <cilk/cilksan.h>

template <typename T> __attribute__((weak)) void plus(long i, T& val) {
  val += i;
}

template <typename T> __attribute__((weak)) void minus(long i, T& val) {
  val -= i;
}

template<typename T> void sum(std::vector<T> &v, long thresh) {
#pragma cilk grainsize(32)
  cilk_for (long i = 0; i < v.size(); ++i) {
    T inc_racy = 0;
    T inc_racy2 = 0;
    T inc_racy3 = 0;
    if (i < thresh)
      continue;
    cilk_spawn {
      plus(i, inc_racy);
      inc_racy2 += i;
      plus(i, inc_racy3);
    }
    minus(i, inc_racy);
    minus(i, inc_racy2);
    inc_racy3 -= i;
  }
}

// Find races on inc_racy within functions.

// CHECK: Race detected on location [[INC_RACY:[a-fA-F0-9]+]]
// CHECK: Write
// CHECK-NEXT: val
// CHECK: Spawn
// CHECK: Read
// CHECK-NEXT: val
// CHECK: Call
// CHECK: Common calling context
// CHECK: Parfor

// CHECK: Race detected on location [[INC_RACY]]
// CHECK: Write
// CHECK-NEXT: val
// CHECK: Spawn
// CHECK: Write
// CHECK-NEXT: val
// CHECK: Call
// CHECK: Common calling context
// CHECK: Parfor

// CHECK: Race detected on location [[INC_RACY]]
// CHECK: Read
// CHECK-NEXT: val
// CHECK: Spawn
// CHECK: Write
// CHECK-NEXT: val
// CHECK: Call
// CHECK: Common calling context
// CHECK: Parfor

// Find races on inc_racy2 between spawned function and direct access.

// CHECK: Race detected on location [[INC_RACY2:[a-fA-F0-9]+]]
// CHECK: Write
// CHECK-NEXT: inc_racy2
// CHECK: Spawn
// CHECK: Read
// CHECK-NEXT: val
// CHECK: Call
// CHECK: Common calling context
// CHECK: Parfor

// CHECK: Race detected on location [[INC_RACY2]]
// CHECK: Write
// CHECK-NEXT: inc_racy2
// CHECK: Spawn
// CHECK: Write
// CHECK-NEXT: val
// CHECK: Call
// CHECK: Common calling context
// CHECK: Parfor

// CHECK: Race detected on location [[INC_RACY2]]
// CHECK: Read
// CHECK-NEXT: inc_racy2
// CHECK: Spawn
// CHECK: Write
// CHECK-NEXT: val
// CHECK: Call
// CHECK: Common calling context
// CHECK: Parfor

// Find races on inc_racy3 between spawned direct access and function.

// CHECK: Race detected on location [[INC_RACY3:[a-fA-F0-9]+]]
// CHECK: Write
// CHECK-NEXT: val
// CHECK: Spawn
// CHECK: Read
// CHECK-NEXT: inc_racy3
// CHECK: Common calling context
// CHECK: Parfor

// CHECK: Race detected on location [[INC_RACY3]]
// CHECK: Write
// CHECK-NEXT: val
// CHECK: Spawn
// CHECK: Write
// CHECK-NEXT: inc_racy3
// CHECK: Common calling context
// CHECK: Parfor

// CHECK: Race detected on location [[INC_RACY3]]
// CHECK: Read
// CHECK-NEXT: val
// CHECK: Spawn
// CHECK: Write
// CHECK-NEXT: inc_racy3
// CHECK: Common calling context
// CHECK: Parfor

// Find races in epilogue loop on inc_racy within functions.

// CHECK-O2: Race detected on location [[INC_RACY_EPIL:[a-fA-F0-9]+]]
// CHECK-O2: Write
// CHECK-O2-NEXT: val
// CHECK-O2: Spawn
// CHECK-O2: Read
// CHECK-O2-NEXT: val
// CHECK-O2: Call
// CHECK-O2: Common calling context
// CHECK-O2: Call

// CHECK-O2: Race detected on location [[INC_RACY_EPIL]]
// CHECK-O2: Write
// CHECK-O2-NEXT: val
// CHECK-O2: Spawn
// CHECK-O2: Write
// CHECK-O2-NEXT: val
// CHECK-O2: Call
// CHECK-O2: Common calling context
// CHECK-O2: Call

// CHECK-O2: Race detected on location [[INC_RACY_EPIL]]
// CHECK-O2: Read
// CHECK-O2-NEXT: val
// CHECK-O2: Spawn
// CHECK-O2: Write
// CHECK-O2-NEXT: val
// CHECK-O2: Call
// CHECK-O2: Common calling context
// CHECK-O2: Call

// Find races in epilogue loop on inc_racy2 between spawned function and direct access.

// CHECK-O2: Race detected on location [[INC_RACY2_EPIL:[a-fA-F0-9]+]]
// CHECK-O2: Write
// CHECK-O2-NEXT: inc_racy2
// CHECK-O2: Spawn
// CHECK-O2: Read
// CHECK-O2-NEXT: val
// CHECK-O2: Call
// CHECK-O2: Common calling context
// CHECK-O2: Call

// CHECK-O2: Race detected on location [[INC_RACY2_EPIL]]
// CHECK-O2: Write
// CHECK-O2-NEXT: inc_racy2
// CHECK-O2: Spawn
// CHECK-O2: Write
// CHECK-O2-NEXT: val
// CHECK-O2: Call
// CHECK-O2: Common calling context
// CHECK-O2: Call

// CHECK-O2: Race detected on location [[INC_RACY2_EPIL]]
// CHECK-O2: Read
// CHECK-O2-NEXT: inc_racy2
// CHECK-O2: Spawn
// CHECK-O2: Write
// CHECK-O2-NEXT: val
// CHECK-O2: Call
// CHECK-O2: Common calling context
// CHECK-O2: Call

// Find races in epilogue loop on inc_racy3 between spawned direct access and function.

// CHECK-O2: Race detected on location [[INC_RACY3_EPIL:[a-fA-F0-9]+]]
// CHECK-O2: Write
// CHECK-O2-NEXT: val
// CHECK-O2: Spawn
// CHECK-O2: Read
// CHECK-O2-NEXT: inc_racy3
// CHECK-O2: Common calling context
// CHECK-O2: Call

// CHECK-O2: Race detected on location [[INC_RACY3_EPIL]]
// CHECK-O2: Write
// CHECK-O2-NEXT: val
// CHECK-O2: Spawn
// CHECK-O2: Write
// CHECK-O2-NEXT: inc_racy3
// CHECK-O2: Common calling context
// CHECK-O2: Call

// CHECK-O2: Race detected on location [[INC_RACY3_EPIL]]
// CHECK-O2: Read
// CHECK-O2-NEXT: val
// CHECK-O2: Spawn
// CHECK-O2: Write
// CHECK-O2-NEXT: inc_racy3
// CHECK-O2: Common calling context
// CHECK-O2: Call

// CHECK-Og: Cilksan detected 9 distinct races.
// CHECK-Og: Cilksan suppressed 801 duplicate race reports.
// CHECK-O2: Cilksan detected 18 distinct races.
// CHECK-O2: Cilksan suppressed 792 duplicate race reports.

int main(int argc, char *argv[]) {
  long n = 100;
  long thresh = 10;
  if (argc > 1)
    n = atol(argv[1]);
  if (argc > 2)
    thresh = atol(argv[2]);

  std::vector<long> vec;
  for (long i = 0; i < n; ++i)
    vec.push_back(i);

  sum(vec, thresh);

  return 0;
}
