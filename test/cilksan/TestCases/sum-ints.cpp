// RUN: %clangxx_cilksan -fopencilk -O2 %s -o %t
// RUN: %run %t 10000 2>&1 | FileCheck %s

/**		-*- C++ -*-
 *
 * \file	sum-ints.cpp
 *
 * @brief	Sum-accumulation of integer values.
 *
 * @author	Tao B. Schardl (neboat@mit.edu)  and Alexandros-Stavros Iliopoulos (ailiop@mit.edu)
 *
 */

/* ==================================================
 * INCLUDE DEPENDENCIES
 */

#include <iostream>
#include <sstream>
#include <cilk/cilk.h>
#include <chrono>
#include <pthread.h>
#include <typeinfo>
#include <numeric>
#include <algorithm>
#include <iterator>
#include <vector>
#include <type_traits>
#include <random>

#include <cilk/opadd_reducer.h>
#include <cilk/cilk_api.h>

#include <cilk/cilksan.h>

/* ==================================================
 * ALIASES
 */


using num_t = long int;
using sum_t = long long int;

// using num_vec_t = std::vector<num_t>;

using chrono_clock = std::chrono::high_resolution_clock;

using accum_func_t = sum_t (*) (num_t);



/* ==================================================
 * CONSTANTS
 */


constexpr num_t N = 10 * 1000 * 1000;

constexpr num_t MAX_VAL = 100; 



/* ================================================== 
 * FORWARD DECLARATIONS
 */


sum_t accum_true    (num_t);
sum_t accum_wrong   (num_t);
sum_t accum_lock    (num_t);
sum_t accum_spawn   (num_t);
sum_t accum_reducer (num_t);
sum_t accum_wls     (num_t);

template <accum_func_t f>
sum_t time_accum_func (num_t,
		       sum_t const = -1,
		       std::string const = "accumulation function (no description)");



/* ==================================================
 * ANONYMOUS NAMESPACE (PRIVATE FUNCTIONS)
 */


namespace {

  sum_t accum_spawn_helper (num_t begin,
  			    num_t end) {

    auto const n = end - begin;

    switch (n) {
      
    case 1:
      return static_cast<sum_t>(begin);

    case 2:
      return static_cast<sum_t>(begin + (end - 1));

    default:
      auto const nhalf = n/2 + 1;
      auto const s1 = cilk_spawn accum_spawn_helper( begin, begin + nhalf );
      auto const s2 = cilk_spawn accum_spawn_helper( begin + nhalf, end );
      cilk_sync;
      return (s1 + s2);

    }  // end switch (n)

  }  // end function "accum_spawn_helper"

}  // end anonymous namespace



/* ==================================================
 * FUNCTIONS
 */



/* ******************************
 * main
 */
int main (int argc, char* argv[]) {

  /* variables */
  
  num_t n;

  /* syntax check and input parsing */
  
  switch (argc) {
    
  case 1: {			// default N
    n = N;
    break; }

  case 2: {
    std::istringstream iss( argv[1] );
    if (!(iss >> n)) {
      std::cerr << "Could not read input (" << argv[1] << ")"
		<< " as type '" << typeid(n).name() << "'" << std::endl;
      return 2;
    }
    break; }

  default: {			// wrong syntax
    std::cerr << "Usage: " << argv[0] << " [N]"   << std::endl
	      << "	 (N: sum upper limit)" << std::endl;
    return 1; }
    
  }  // end switch (# input arguments)

  /* create vector of N random integers */

  // num_vec_t vals (n);
  
  // std::random_device rd;
  // std::default_random_engine rng;
  // std::uniform_int_distribution< num_t > rand( 1, MAX_VAL );
  // std::generate( vals.begin(), vals.end(), [&] () { return rand(rng); } );

  std::cout << "Calculate sum of " << n << " integers" << std::endl;

  /* run various accumulation functions */
  
  sum_t const sum_true =
    time_accum_func< accum_true >( n, -1, "true solution (std::accumulate)" );
  
  time_accum_func< accum_wrong >( n, sum_true, "racy cilk_for (*WRONG*)" );

  time_accum_func< accum_lock >( n, sum_true, "cilk_for w/ POSIX lock" );

  time_accum_func< accum_spawn >( n, sum_true, "cilk_spawn reduction" );

  time_accum_func< accum_reducer >( n, sum_true, "cilk reducer" );

  time_accum_func< accum_wls >( n, sum_true, "cilk WLS" );

  /* exit */
  
  return 0;

}  // end function "main"



/* ******************************
 * time_accum_func */
/**
 * @brief	Run and time accumulation function, and output resulting time to
 *		stdout.
 */
template <accum_func_t f>
sum_t time_accum_func (num_t n,
		       sum_t const sum_true,
		       std::string const desc) {

  std::cout << "..." << desc << "..." << std::endl;
  
  auto  const tstart = chrono_clock::now();
  sum_t const sum    = f( n );
  auto  const tend   = chrono_clock::now();

  // std::cout << "   - sum = " << sum << std::endl;

  if (sum_true != -1)
    std::cout << "   - " << (sum == sum_true ? "PASS" : "FAIL") << std::endl;

  auto const telapsed =
    std::chrono::duration_cast< std::chrono::microseconds >( tend - tstart );
  
  std::cout << "   - elapsed time: " << telapsed.count() / 1000.0 << " ms"
	    << std::endl;

  return sum;

}



/* ******************************
 * accum_true */
/**
 * @brief	Calculate sum of vector elements using `std::accumulate`.
 */
sum_t accum_true (num_t n) {

  return static_cast<sum_t>(n * (n-1) / 2); // std::accumulate( vals.cbegin(), vals.cend(), static_cast<sum_t>( 0 ) );
  
}

// CHECK-LABEL: true solution

// CHECK-NOT: Race detected on location

/* ******************************
 * accum_wrong */
/**
 * @brief	Parallel accumulation of vector values using cilk_for, without 
 *		special handling of critical section.
 */
sum_t accum_wrong (num_t n) {

  sum_t sum = 0;
  cilk_for (num_t i = 0; i < n; i++)
    sum += i;

  return sum;

}

// CHECK-LABEL: racy cilk_for

// CHECK: Race detected on location [[SUM:[0-9a-f]+]]
// CHECK-NEXT: * Write {{[0-9a-f]+}} accum_wrong
// CHECK: * Read {{[0-9a-f]+}}
// CHECK: Common calling context
// CHECK-NEXT: Parfor {{[0-9a-f]+}} accum_wrong
// CHECK-NEXT: Call {{[0-9a-f]+}} main
// CHECK: Stack object

// CHECK: Race detected on location [[SUM]]
// CHECK-NEXT: * Write {{[0-9a-f]+}} accum_wrong
// CHECK: * Write {{[0-9a-f]+}} accum_wrong
// CHECK: Common calling context
// CHECK-NEXT: Parfor {{[0-9a-f]+}} accum_wrong
// CHECK-NEXT: Call {{[0-9a-f]+}} main
// CHECK: Stack object


/* ******************************
 * accum_lock */
/**
 * @brief	Parallel accumulation of vector values using cilk_for, with a 
 *		POSIX mutex lock to avoid races.
 */
sum_t accum_lock (num_t n) {

  pthread_mutex_t mutex;
  pthread_mutex_init( &mutex, NULL );
  sum_t sum = 0;
  cilk_for (num_t i = 0; i < n; i++) {
    pthread_mutex_lock( &mutex );
    sum += i;
    pthread_mutex_unlock( &mutex );
  }

  return sum;

}

// CHECK-LABEL: cilk_for w/ POSIX lock

// CHECK-NOT: Race detected on location

/* ******************************
 * accum_spawn */
/**
 * @brief	Parallel accumulation of vector values using an explicit
 *		divide-and-conquer reduction via `cilk_spawn`.
 */
sum_t accum_spawn (num_t n) {

  return accum_spawn_helper( 0, n );

}

// CHECK-LABEL: cilk_spawn reduction

// CHECK-NOT: Race detected on location

/* ******************************
 * accum_reducer */
/**
 * @brief	Parallel accumulation of vector values using a reducer
 *		hyperobject.
 */
sum_t accum_reducer (num_t n) {

  cilk::opadd_reducer<sum_t> sum(0);
  cilk_for (num_t i = 0; i < n; i++)
    sum += i;
  
  return sum;
  
}

// CHECK-LABEL: cilk reducer

// CHECK-NOT: Race detected on location

/* ******************************
 * accum_wls */
/**
 * @brief	Parallel accumulation of vector values using 
 *              worker-local storage.
 */
#ifdef TLS_READ
struct __cilkrts_worker;
extern __thread struct __cilkrts_worker *tls_worker;
#endif
sum_t accum_wls (num_t n) {
  sum_t wls_sum[__cilkrts_get_nworkers()];
  sum_t sum = 0;

  for (int i = 0; i < __cilkrts_get_nworkers(); ++i)
    wls_sum[i] = 0;

  Cilksan_fake_mutex fake_lock;
  // __cilksan_register_lock(&fake_lock);
  cilk_for (num_t i = 0; i < n; i++) {
#ifdef TLS_READ
    // This approach to getting the Cilk worker ID is not safe in general.
    int worker_id = (int) (*(((uint64_t*) tls_worker) + 4));
#else
    int worker_id = __cilkrts_get_worker_number();
#endif
    Cilksan_fake_lock_guard guard(&fake_lock);
    // __cilksan_begin_atomic();
    wls_sum[worker_id] += i;
    // __cilksan_end_atomic();
  }
  // __cilksan_unregister_lock(&fake_lock);

  for (int i = 0; i < __cilkrts_get_nworkers(); ++i)
    sum += wls_sum[i];

  return sum;
}

// CHECK-LABEL: cilk WLS

// CHECK-NOT: Race detected on location

// CHECK: Cilksan detected 2 distinct races.
// CHECK-NEXT: Cilksan suppressed {{[0-9]+}} duplicate race reports.
