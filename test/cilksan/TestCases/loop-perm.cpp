// RUN: %clangxx_cilksan -fopencilk -Og %s -o %t
// RUN: %run %t 2>&1 | FileCheck %s
// RUN: %clangxx_cilksan -fopencilk -Og %s -o %t -DDYNAMIC_ARRAYS
// RUN: %run %t 1000 5 2>&1 | FileCheck %s
#include <iostream>
#include <numeric>
#include <algorithm>
#include <cilk/cilk.h>

#ifdef DYNAMIC_ARRAYS
#include <sstream>
#else // AUTOMATIC_ARRAYS
#define M 1000
#define N 5
#endif

// Syntax: <program> <M> <N>
int main( int argc, char* argv[] ) {

#ifdef DYNAMIC_ARRAYS
  int M, N;
  std::istringstream ss1( argv[1] );
  std::istringstream ss2( argv[2] );
  ss1 >> M;                   // *** assume correct calling syntax
  ss2 >> N;                   // *** assume correct calling syntax
  std::cout << "Using dynamic array storage." << std::endl;
#else // AUTOMATIC_ARRAYS
  std::cout << "Using automatic array storage." << std::endl;
#endif

  std::cout << "M = " << M << " | "
            << "N = " << N << std::endl;

  // initialize MxN array
#ifdef DYNAMIC_ARRAYS
  int* array = new int [M*N] ();
#else // AUTOMATIC_ARRAYS
  int array[M*N] = {0};
#endif

#ifdef PERM
  // random M-permutation vector
#ifdef DYNAMIC_ARRAYS
  int* perm = new int [M];
#else // AUTOMATIC_ARRAYS
  int perm[M];
#endif
  std::iota( perm, perm+M, 0 );
  std::random_shuffle( perm, perm+M );
#endif

  // update rows of array in random order
  cilk_for (int i = 0; i < M; i++) {
    for (int j = 0; j < N; j++) {
#ifdef PERM
      array[perm[i]*N + j] += perm[i] + j;
#else // NO_PERM
      array[i*N + j] += i + j;
#endif
    }
  }

#ifdef DYNAMIC_ARRAYS
  delete [] array;
#ifdef PERM
  delete [] perm;
#endif
#endif

  return 0;

}

// CHECK: Cilksan detected 0 distinct races.
// CHECK-NEXT: Cilksan suppressed 0 duplicate race reports.
