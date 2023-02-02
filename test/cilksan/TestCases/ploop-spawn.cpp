// RUN: %clangxx_cilksan -fopencilk -Og %s -o %t
// RUN: %run %t 2>&1 | FileCheck %s
#include <cstdio>
#include <cstdlib>
#include <vector>

#include <cilk/cilk.h>
#include <cilk/opadd_reducer.h>

template<typename T> void sum(std::vector<T> &v) {
  T sum_racy = 0;
  cilk::opadd_reducer<T> sum_pos = 0;
  cilk::opadd_reducer<T> sum_neg = 0;

  long size = v.size();
  constexpr long grainsize = 64;
  long n = size / grainsize;
  cilk_spawn {
    cilk_for (long ii = 0; ii < n; ++ii) {
      for (long i = ii * grainsize; i < (ii + 1) * grainsize; ++i) {
        cilk_spawn {
          sum_racy += v[i];
          sum_pos += v[i];
          sum_neg -= v[i];
        }
        sum_racy += v[i];
        sum_pos += v[i];
        sum_neg -= v[i];
        cilk_sync;
      }
    }
  }
  for (long i = n * grainsize; i < size; ++i) {
    cilk_scope {
      cilk_spawn {
        sum_racy += v[i];
        sum_pos += v[i];
        sum_neg -= v[i];
      }
      sum_racy += v[i];
      sum_pos += v[i];
      sum_neg -= v[i];
    }
  }
  cilk_sync;

  printf("sum_pos %ld\nsum_neg %ld\nsum_racy %ld\n",
         sum_pos, sum_neg, sum_racy);
  if (sum_pos != -sum_neg)
    printf("WARNING: sum_pos is not negation of sum_neg!\n");
}

// Find races on sum_racy within a parallel-loop iteration.
 
// CHECK: Race detected on location [[SUM_RACY:[a-fA-F0-9]+]]
// CHECK: Write
// CHECK-NEXT: sum_racy
// CHECK: Spawn
// CHECK: Read
// CHECK-NEXT: sum_racy
// CHECK: Common calling context
// CHECK: Parfor

// CHECK: Race detected on location [[SUM_RACY]]
// CHECK: Write
// CHECK-NEXT: sum_racy
// CHECK: Spawn
// CHECK: Write
// CHECK-NEXT: sum_racy
// CHECK: Common calling context
// CHECK: Parfor

// CHECK: Race detected on location [[SUM_RACY]]
// CHECK: Read
// CHECK-NEXT: sum_racy
// CHECK: Spawn
// CHECK: Write
// CHECK-NEXT: sum_racy
// CHECK: Common calling context
// CHECK: Parfor

// Find races on sum_racy between parallel-loop iterations.

// CHECK: Race detected on location [[SUM_RACY]]
// CHECK: Write
// CHECK-NEXT: sum_racy
// CHECK: Spawn
// CHECK: Read
// CHECK-NEXT: sum_racy
// CHECK: Spawn
// CHECK: Common calling context
// CHECK: Parfor

// CHECK: Race detected on location [[SUM_RACY]]
// CHECK: Write
// CHECK-NEXT: sum_racy
// CHECK: Spawn
// CHECK: Write
// CHECK-NEXT: sum_racy
// CHECK: Spawn
// CHECK: Common calling context
// CHECK: Parfor

// Find races between spawn in serial loop and parallel loop.

// CHECK: Race detected on location [[SUM_RACY]]
// CHECK: Write
// CHECK-NEXT: sum_racy
// CHECK: Spawn
// CHECK-NEXT: Parfor
// CHECK-NEXT: Spawn
// CHECK: Read
// CHECK-NEXT: sum_racy
// CHECK: Spawn
// CHECK: Common calling context
// CHECK: Call

// CHECK: Race detected on location [[SUM_RACY]]
// CHECK: Write
// CHECK-NEXT: sum_racy
// CHECK: Spawn
// CHECK-NEXT: Parfor
// CHECK-NEXT: Spawn
// CHECK: Write
// CHECK-NEXT: sum_racy
// CHECK: Spawn
// CHECK: Common calling context
// CHECK: Call

// CHECK: Race detected on location [[SUM_RACY]]
// CHECK: Read
// CHECK-NEXT: sum_racy
// CHECK: Spawn
// CHECK-NEXT: Parfor
// CHECK-NEXT: Spawn
// CHECK: Write
// CHECK-NEXT: sum_racy
// CHECK: Spawn
// CHECK: Common calling context
// CHECK: Call

// Find races between continuation in serial loop and parallel loop.

// CHECK: Race detected on location [[SUM_RACY]]
// CHECK: Write
// CHECK-NEXT: sum_racy
// CHECK: Spawn
// CHECK-NEXT: Parfor
// CHECK-NEXT: Spawn
// CHECK: Read
// CHECK-NEXT: sum_racy
// CHECK-NOT: Spawn
// CHECK: Common calling context
// CHECK: Call

// CHECK: Race detected on location [[SUM_RACY]]
// CHECK: Write
// CHECK-NEXT: sum_racy
// CHECK: Spawn
// CHECK-NEXT: Parfor
// CHECK-NEXT: Spawn
// CHECK: Write
// CHECK-NEXT: sum_racy
// CHECK-NOT: Spawn
// CHECK: Common calling context
// CHECK: Call

// CHECK: Race detected on location [[SUM_RACY]]
// CHECK: Read
// CHECK-NEXT: sum_racy
// CHECK: Spawn
// CHECK-NEXT: Parfor
// CHECK-NEXT: Spawn
// CHECK: Write
// CHECK-NEXT: sum_racy
// CHECK-NOT: Spawn
// CHECK: Common calling context
// CHECK: Call

// Make sure no other races are reported

// CHECK: sum_pos
// CHECK: sum_neg
// CHECK: sum_racy

// CHECK: Cilksan detected 11 distinct races.
// CHECK: Cilksan suppressed 30502 duplicate race reports.

int main(int argc, char *argv[]) {
  long n = 10000;
  if (argc > 1)
    n = atol(argv[1]);

  std::vector<long> vec;
  for (long i = 0; i < n; ++i)
    vec.push_back(i);

  sum(vec);

  return 0;
}
