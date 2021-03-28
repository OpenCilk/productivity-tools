// RUN: %clang_cilksan -fopencilk -O0 %s -o %t
// RUN: %run %t 10000 2>&1 | FileCheck %s --check-prefixes=CHECK,CHECK-O0
// RUN: %clang_cilksan -fopencilk -Og %s -o %t
// RUN: %run %t 10000 2>&1 | FileCheck %s --check-prefixes=CHECK,CHECK-OPT
// RUN: %clang_cilksan -fopencilk -O2 %s -o %t
// RUN: %run %t 10000 2>&1 | FileCheck %s --check-prefixes=CHECK,CHECK-OPT
// RUN: %clang_cilksan -fopencilk -O2 -fno-vectorize -fno-stripmine -fno-unroll-loops %s -o %t
// RUN: %run %t 10000 2>&1 | FileCheck %s --check-prefixes=CHECK,CHECK-OPT
// RUN: %clang_cilksan -fopencilk -fcilktool=cilkscale -O0 %s -o %t
// RUN: %run %t 10000 2>&1 | FileCheck %s --check-prefixes=CHECK,CHECK-O0
// RUN: %clang_cilksan -fopencilk -fcilktool=cilkscale -Og %s -o %t
// RUN: %run %t 10000 2>&1 | FileCheck %s --check-prefixes=CHECK,CHECK-OPT
// RUN: %clang_cilksan -fopencilk -fcilktool=cilkscale -O2 %s -o %t
// RUN: %run %t 10000 2>&1 | FileCheck %s --check-prefixes=CHECK,CHECK-OPT
// RUN: %clang_cilksan -fopencilk -fcilktool=cilkscale -O2 -fno-vectorize -fno-stripmine -fno-unroll-loops %s -o %t
// RUN: %run %t 10000 2>&1 | FileCheck %s --check-prefixes=CHECK,CHECK-OPT

/**		-*- C -*-
 *
 * \file	sum-vector-int.c
 *
 * @brief	Sum-accumulation of integer vector values.
 *
 * @author	Alexandros-Stavros Iliopoulos (ailiop@mit.edu)
 *
 */



/* ==================================================
 * INCLUDE DEPENDENCIES
 */


#include <stdio.h>
#include <cilk/cilk.h>
#include <pthread.h>
#include <stdlib.h>

#include <cilk/cilk_api.h>

#include <cilk/cilksan.h>
#include <cilk/cilkscale.h>

/* ==================================================
 * ALIASES
 */


typedef long int	num_t;
typedef long long int	sum_t;



/* ==================================================
 * CONSTANTS
 */


#define N (10 * 1000 * 1000)

#define MAX_VAL 100



/* ================================================== 
 * FORWARD DECLARATIONS
 */


sum_t accum_true    (num_t const *, num_t const);
sum_t accum_wrong   (num_t const *, num_t const);
sum_t accum_lock    (num_t const *, num_t const);
sum_t accum_spawn   (num_t const *, num_t const);
sum_t accum_wls     (num_t const *, num_t const);

void run_accum (sum_t (*) (num_t const *, num_t const),
		num_t const *, num_t const, sum_t const, char const *);



/* ==================================================
 * FUNCTIONS
 */



/* ******************************
 * main
 */
int main (int argc, char* argv[]) {

  /* variables */
  
  num_t   n;
  num_t * vals;
  
  /* syntax check and input parsing */
  
  switch (argc) {
    
  case 1:			// default N
    n = N;
    break;
    
  case 2:			// user-specified N
    n = atoi( argv[1] );
    break;

  default:
    fprintf( stderr, "Usage: %s [N]\n", argv[0] );
    return 1;

  }  // end switch (# input arguments)

  /* create vector of N integers */

  vals = (num_t*) malloc( n * sizeof(num_t) );
  if (!vals) {
    fprintf( stderr, "Could not allocate memory for 'vals'" );
    return 2;
  }

  for (num_t i = 0; i < n; i++)
    vals[i] = (rand() % MAX_VAL) + 1;

  printf( "Calculate sum of %ld integers\n", n );

  /* run accumulation functions */

  wsp_t start = wsp_getworkspan();
  sum_t const sum_true = accum_true( vals, n );
  wsp_t last = wsp_getworkspan();
  wsp_dump(wsp_sub(last, start), "sum_true");

  run_accum( accum_wrong, vals, n, sum_true, "wrong" );

  run_accum( accum_lock , vals, n, sum_true, "lock"  );

  run_accum( accum_spawn, vals, n, sum_true, "spawn" );

  run_accum( accum_wls, vals, n, sum_true, "wls" );

  /* exit */

  free( vals );
  return 0;

}



/* ******************************
 * run_accum */
/**
 * @brief	Run and time accumulation function, and output resulting time to
 *		stdout.
 */
void run_accum (sum_t (*f) (num_t const *, num_t const),
		num_t const * vals, num_t const n,
		sum_t const sum_true, char const * desc) {

  printf( "...%s...\n", desc );

  wsp_t start = wsp_getworkspan();
  sum_t const sum = (*f)( vals, n );
  wsp_t end = wsp_getworkspan();
  wsp_dump(wsp_sub(end, start), desc);

  printf( "   - %s\n", (sum == sum_true ? "PASS" : "FAIL") );

  return;

}



/* ******************************
 * accum_true */
/**
 * @brief	Calculate sum of vector elements using `std::accumulate`.
 */
sum_t accum_true (num_t const * vals, num_t const n) {

  sum_t sum = 0;
  for (num_t i = 0; i < n; i++)
    sum += vals[i];

  return sum;
  
}

/* ******************************
 * accum_wrong */
/**
 * @brief	Parallel accumulation of vector values using cilk_for, without 
 *		special handling of critical section.
 */
sum_t accum_wrong (num_t const * vals, num_t const n) {

  sum_t sum = 0;
  cilk_for (num_t i = 0; i < n; i++)
    sum += vals[i];

  return sum;

}

// CHECK-LABEL: wrong

// CHECK: Race detected on location [[SUM:[0-9a-f]+]]
// CHECK-NEXT: * Write {{[0-9a-f]+}} accum_wrong
// CHECK-O0: Spawn {{[0-9a-f]+}} accum_wrong
// CHECK: * Read {{[0-9a-f]+}}
// CHECK-O0: Spawn {{[0-9a-f]+}} accum_wrong
// CHECK: Common calling context
// CHECK-O0-NEXT: Call {{[0-9a-f]+}}
// CHECK-OPT-NEXT: Parfor {{[0-9a-f]+}} accum_wrong
// CHECK: Stack object

// CHECK: Race detected on location [[SUM]]
// CHECK-NEXT: * Write {{[0-9a-f]+}} accum_wrong
// CHECK-O0: Spawn {{[0-9a-f]+}} accum_wrong
// CHECK: * Write {{[0-9a-f]+}} accum_wrong
// CHECK-O0: Spawn {{[0-9a-f]+}} accum_wrong
// CHECK: Common calling context
// CHECK-O0-NEXT: Call {{[0-9a-f]+}}
// CHECK-OPT-NEXT: Parfor {{[0-9a-f]+}} accum_wrong
// CHECK: Stack object

/* ******************************
 * accum_lock */
/**
 * @brief	Parallel accumulation of vector values using cilk_for, with a 
 *		POSIX mutex lock to avoid races.
 */
sum_t accum_lock (num_t const * vals, num_t const n) {

  pthread_mutex_t mutex;
  pthread_mutex_init( &mutex, NULL );

  sum_t sum = 0;
  cilk_for (num_t i = 0; i < n; i++) {
    pthread_mutex_lock( &mutex );
    sum += vals[i];
    pthread_mutex_unlock( &mutex );
  }

  return sum;

}

// CHECK-LABEL: lock

// CHECK-NOT: Race detected on location

/* ******************************
 * accum_spawn */
/**
 * @brief	Parallel accumulation of vector values using an explicit
 *		divide-and-conquer reduction via `cilk_spawn`.
 */
sum_t accum_spawn (num_t const * vals, num_t const n) {

  switch (n) {

  case 1:
    return *vals;

  case 2:
    return (*vals + *(vals+1));

  default: {
    num_t const nhalf = n/2 + 1;
    sum_t const s1 = cilk_spawn accum_spawn( vals        , nhalf     );
    sum_t const s2 = cilk_spawn accum_spawn( vals + nhalf, n - nhalf );
    cilk_sync;
    return (s1 + s2); }

  }  // end switch (n)

}


// CHECK-LABEL: spawn

// CHECK-NOT: Race detected on location

/* ******************************
 * accum_wls */
/**
 * @brief	Parallel accumulation of vector values using 
 *              worker-local storage.
 */
sum_t accum_wls (num_t const * vals, num_t const n) {
  sum_t wls_sum[__cilkrts_get_nworkers()];
  sum_t sum = 0;

  for (int i = 0; i < __cilkrts_get_nworkers(); ++i)
    wls_sum[i] = 0;

  Cilksan_fake_mutex fake_lock;
  __cilksan_register_lock_explicit(&fake_lock);
  cilk_for (num_t i = 0; i < n; i++) {
    __cilksan_acquire_lock(&fake_lock);
    wls_sum[__cilkrts_get_worker_number()] += vals[i];
    __cilksan_release_lock(&fake_lock);
  }
  __cilksan_unregister_lock_explicit(&fake_lock);

  for (int i = 0; i < __cilkrts_get_nworkers(); ++i)
    sum += wls_sum[i];

  return sum;
}

// CHECK-LABEL: wls

// CHECK-NOT: Race detected on location

// CHECK: Cilksan detected 2 distinct races.
// CHECK-NEXT: Cilksan suppressed {{[0-9]+}} duplicate race reports.
