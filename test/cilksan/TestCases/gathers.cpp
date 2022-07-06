// RUN: %clang_cilksan -fopencilk -Og -mavx2 -g %s -o %t
// RUN: %run %t 2>&1 | FileCheck %s
// REQUIRES: x86_64-target-arch

#include <cilk/cilk.h>
#include <immintrin.h>
#include <stdio.h>

__attribute__((noinline))
int test_mm_i32gather_epi32(int *x, int n) {
  __m128i indx = _mm_set_epi32(1, 100, 1000, 100000);
  __m128i y = _Cilk_spawn _mm_i32gather_epi32(x, indx, 4);

  for (int i = 1; i < n; i *= 10)
    x[i] = i;

  cilk_sync;

  int res = _mm_extract_epi32(y, 0) + _mm_extract_epi32(y, 1) +
    _mm_extract_epi32(y, 2) + _mm_extract_epi32(y, 3);

  return res;
}

__attribute__((noinline))
int test_mm256_i32gather_epi32(int *x, int n) {
  __m256i indx = _mm256_set_epi32(1, 50, 100, 500, 1000, 5000, 10000, 100000);
  __m256i y = _Cilk_spawn _mm256_i32gather_epi32(x, indx, 4);

  for (int i = 1; i < n; i *= 10)
    x[i] = i;

  cilk_sync;

  int res = _mm256_extract_epi32(y, 0) + _mm256_extract_epi32(y, 1) +
    _mm256_extract_epi32(y, 2) + _mm256_extract_epi32(y, 3) +
    _mm256_extract_epi32(y, 4) + _mm256_extract_epi32(y, 5) +
    _mm256_extract_epi32(y, 6) + _mm256_extract_epi32(y, 7);

  return res;
}

__attribute__((noinline))
int test_mm_mask_i32gather_epi32(int *x, int n) {
  __m128i indx = _mm_set_epi32(1, 100, 1000, 100000);
  __m128i src = {0};
  __m128i mask = _mm_set_epi32(-1, 0, -1, 0);
  __m128i y = _Cilk_spawn _mm_mask_i32gather_epi32(src, x, indx, mask, 4);

  for (int i = 1; i < n; i *= 10)
    x[i] = i;

  cilk_sync;

  int res = _mm_extract_epi32(y, 0) + _mm_extract_epi32(y, 1) +
    _mm_extract_epi32(y, 2) + _mm_extract_epi32(y, 3);

  return res;
}

__attribute__((noinline))
int test_mm256_mask_i32gather_epi32(int *x, int n) {
  __m256i indx = _mm256_set_epi32(1, 50, 100, 500, 1000, 5000, 10000, 100000);
  __m256i src = {0};
  __m256i mask = _mm256_set_epi32(0, -1, 0, -1, 0, -1, 0, -1);
  __m256i y = _Cilk_spawn _mm256_mask_i32gather_epi32(src, x, indx, mask, 4);

  for (int i = 1; i < n; i *= 10)
    x[i] = i;

  cilk_sync;

  int res = _mm256_extract_epi32(y, 0) + _mm256_extract_epi32(y, 1) +
    _mm256_extract_epi32(y, 2) + _mm256_extract_epi32(y, 3) +
    _mm256_extract_epi32(y, 4) + _mm256_extract_epi32(y, 5) +
    _mm256_extract_epi32(y, 6) + _mm256_extract_epi32(y, 7);

  return res;
}

static void initi(int *x, int n) {
  for (int i = 1; i < n; ++i)
    x[i] = 1;
}

__attribute__((noinline))
double test_mm_i32gather_pd(double *x, int n) {
  __m128i indx = _mm_set_epi32(0, 0, 1, 100);
  __m128d y = _Cilk_spawn _mm_i32gather_pd(x, indx, 8);

  for (int i = 1; i < n; i *= 10)
    x[i] = i;

  cilk_sync;

  double res = y[0] + y[1];

  return res;
}

__attribute__((noinline))
double test_mm256_i32gather_pd(double *x, int n) {
  __m128i indx = _mm_set_epi32(1, 100, 1000, 100000);
  __m256d y = _Cilk_spawn _mm256_i32gather_pd(x, indx, 8);

  for (int i = 1; i < n; i *= 10)
    x[i] = i;

  cilk_sync;
  double res = y[0] + y[1] + y[2] + y[3];

  return res;
}

__attribute__((noinline))
double test_mm_mask_i32gather_pd(double *x, int n) {
  __m128i indx = _mm_set_epi64x(1, 100);
  __m128d src = {0};
  __m128d mask = {-1, 0};
  __m128d y = _Cilk_spawn _mm_mask_i32gather_pd(src, x, indx, mask, 8);

  for (int i = 1; i < n; i *= 10)
    x[i] = i;

  cilk_sync;

  double res = y[0] + y[1];

  return res;
}

__attribute__((noinline))
double test_mm256_mask_i32gather_pd(double *x, int n) {
  __m128i indx = _mm_set_epi32(1, 100, 1000, 100000);
  __m256d src = {0};
  __m256d mask = {0, -1, 0, -1};
  __m256d y = cilk_spawn _mm256_mask_i32gather_pd(src, x, indx, mask, 8);

  for (int i = 1; i < n; i *= 10)
    x[i] = i;

  cilk_sync;
  double res = y[0] + y[1] + y[2] + y[3];

  return res;
}

static void initd(double *x, int n) {
  for (int i = 1; i < n; ++i)
    x[i] = 1.0;
}

int main() {
  int n = 1000000;
  int *x = (int *)malloc(n * sizeof(int));
  initi(x, n);

  // 1 distinct race, 3 duplicates
  printf("test_mm_i32gather_epi32: %d\n", test_mm_i32gather_epi32(x, n));

  // 1 distinct race, 4 duplicates
  printf("test_mm256_i32gather_epi32: %d\n", test_mm256_i32gather_epi32(x, n));

  // 1 distinct race, 1 duplicate
  printf("test_mm_mask_i32gather_epi32: %d\n", test_mm_mask_i32gather_epi32(x, n));

  // 1 distinct race, 0 duplicates
  printf("test_mm256_mask_i32gather_epi32: %d\n", test_mm256_mask_i32gather_epi32(x, n));

  double *a = (double *)malloc(n * sizeof(double));
  initd(a, n);

  // 1 distinct race, 1 duplicate
  printf("test_mm_i32gather_pd: %f\n", test_mm_i32gather_pd(a, n));

  // 1 distinct race, 3 duplicates
  printf("test_mm256_i32gather_pd: %f\n", test_mm256_i32gather_pd(a, n));

  // 1 distinct race, 0 duplicates
  printf("test_mm_mask_i32gather_pd: %f\n", test_mm_mask_i32gather_pd(a, n));

  // 1 distinct race, 1 duplicates
  printf("test_mm256_mask_i32gather_pd: %f\n", test_mm256_mask_i32gather_pd(a, n));

  free(x);
  free(a);

  return 0;
}

// CHECK: Race detected
// CHECK-NEXT: * Read {{[0-9a-f]+}} test_mm_i32gather_epi32
// CHECK-NEXT: + Spawn {{[0-9a-f]+}} test_mm_i32gather_epi32
// CHECK-NEXT: * Write {{[0-9a-f]+}} test_mm_i32gather_epi32
// CHECK: test_mm_i32gather_epi32: 4

// CHECK: Race detected
// CHECK-NEXT: * Read {{[0-9a-f]+}} test_mm256_i32gather_epi32
// CHECK-NEXT: + Spawn {{[0-9a-f]+}} test_mm256_i32gather_epi32
// CHECK-NEXT: * Write {{[0-9a-f]+}} test_mm256_i32gather_epi32
// CHECK: test_mm256_i32gather_epi32: 111104

// CHECK: Race detected
// CHECK-NEXT: * Read {{[0-9a-f]+}} test_mm_mask_i32gather_epi32
// CHECK-NEXT: + Spawn {{[0-9a-f]+}} test_mm_mask_i32gather_epi32
// CHECK-NEXT: * Write {{[0-9a-f]+}} test_mm_mask_i32gather_epi32
// CHECK: test_mm_mask_i32gather_epi32: 1001

// CHECK: Race detected
// CHECK-NEXT: * Read {{[0-9a-f]+}} test_mm256_mask_i32gather_epi32
// CHECK-NEXT: + Spawn {{[0-9a-f]+}} test_mm256_mask_i32gather_epi32
// CHECK-NEXT: * Write {{[0-9a-f]+}} test_mm256_mask_i32gather_epi32
// CHECK: test_mm256_mask_i32gather_epi32: 100003

// CHECK: Race detected
// CHECK-NEXT: * Read {{[0-9a-f]+}} test_mm_i32gather_pd
// CHECK-NEXT: + Spawn {{[0-9a-f]+}} test_mm_i32gather_pd
// CHECK-NEXT: * Write {{[0-9a-f]+}} test_mm_i32gather_pd
// CHECK: test_mm_i32gather_pd: 2.0

// CHECK: Race detected
// CHECK-NEXT: * Read {{[0-9a-f]+}} test_mm256_i32gather_pd
// CHECK-NEXT: + Spawn {{[0-9a-f]+}} test_mm256_i32gather_pd
// CHECK-NEXT: * Write {{[0-9a-f]+}} test_mm256_i32gather_pd
// CHECK: test_mm256_i32gather_pd: 101101.0

// CHECK: Race detected
// CHECK-NEXT: * Read {{[0-9a-f]+}} test_mm_mask_i32gather_pd
// CHECK-NEXT: + Spawn {{[0-9a-f]+}} test_mm_mask_i32gather_pd
// CHECK-NEXT: * Write {{[0-9a-f]+}} test_mm_mask_i32gather_pd
// CHECK: test_mm_mask_i32gather_pd: 100.0

// CHECK: Race detected
// CHECK-NEXT: * Read {{[0-9a-f]+}} test_mm256_mask_i32gather_pd
// CHECK-NEXT: + Spawn {{[0-9a-f]+}} test_mm256_mask_i32gather_pd
// CHECK-NEXT: * Write {{[0-9a-f]+}} test_mm256_mask_i32gather_pd
// CHECK: test_mm256_mask_i32gather_pd: 1001.0

// CHECK: Cilksan detected 8 distinct races.
// CHECK-NEXT: Cilksan suppressed 13 duplicate race reports.
