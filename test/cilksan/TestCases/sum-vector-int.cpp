// RUN: %clangxx_cilksan -fopencilk -O0 %s -o %t
// RUN: %run %t 10000 2>&1 | FileCheck %s --check-prefixes=CHECK,CHECK-O0
// RUN: %clangxx_cilksan -fopencilk -Og %s -o %t
// RUN: %run %t 10000 2>&1 | FileCheck %s --check-prefixes=CHECK,CHECK-OPT
// RUN: %clangxx_cilksan -fopencilk -O2 %s -o %t
// RUN: %run %t 10000 2>&1 | FileCheck %s --check-prefixes=CHECK,CHECK-OPT
// RUN: %clangxx_cilksan -fopencilk -O2 -fno-vectorize -fno-stripmine -fno-unroll-loops %s -o %t
// RUN: %run %t 10000 2>&1 | FileCheck %s --check-prefixes=CHECK,CHECK-OPT
// RUN: %clangxx_cilksan -fopencilk -fcilktool=cilkscale -O0 %s -o %t
// RUN: %run %t 10000 2>&1 | FileCheck %s --check-prefixes=CHECK,CHECK-O0
// RUN: %clangxx_cilksan -fopencilk -fcilktool=cilkscale -Og %s -o %t
// RUN: %run %t 10000 2>&1 | FileCheck %s --check-prefixes=CHECK,CHECK-OPT
// RUN: %clangxx_cilksan -fopencilk -fcilktool=cilkscale -O2 %s -o %t
// RUN: %run %t 10000 2>&1 | FileCheck %s --check-prefixes=CHECK,CHECK-OPT
// RUN: %clangxx_cilksan -fopencilk -fcilktool=cilkscale -O2 -fno-vectorize -fno-stripmine -fno-unroll-loops %s -o %t
// RUN: %run %t 10000 2>&1 | FileCheck %s --check-prefixes=CHECK,CHECK-OPT

/**		-*- C++ -*-
 *
 * \file	sum-vector-int.cpp
 *
 * @brief	Sum-accumulation of integer vector values.
 *
 * @author	Alexandros-Stavros Iliopoulos (ailiop@mit.edu)
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
#include <unistd.h>

#include <cilk/opadd_reducer.h>

#ifdef WLS
#include <cilk/cilk_api.h>
#endif


/* ==================================================
 * ALIASES
 */


using num_t = long int;
using sum_t = long long int;

using num_vec_t = std::vector<num_t>;

using chrono_clock = std::chrono::high_resolution_clock;

using accum_func_t = sum_t (*) (num_vec_t const &);



/* ==================================================
 * CONSTANTS
 */


constexpr num_t N = 10 * 1000 * 1000;

constexpr num_t MAX_VAL = 100; 



/* ================================================== 
 * FORWARD DECLARATIONS
 */


sum_t accum_true    (num_vec_t const &);
sum_t accum_wrong   (num_vec_t const &);
sum_t accum_lock    (num_vec_t const &);
sum_t accum_spawn   (num_vec_t const &);
sum_t accum_reducer (num_vec_t const &);
#ifdef WLS
sum_t accum_wls     (num_vec_t const &);
#endif

template <accum_func_t f>
sum_t time_accum_func (num_vec_t const &,
		       sum_t const = -1,
		       std::string const = "accumulation function (no description)");



/* ==================================================
 * ANONYMOUS NAMESPACE (PRIVATE FUNCTIONS)
 */


namespace {

  sum_t accum_spawn_helper (num_vec_t::const_iterator itBegin,
  			    num_vec_t::const_iterator itEnd) {

    auto const n = std::distance( itBegin, itEnd );

    switch (n) {
      
    case 1:
      return *itBegin;

    case 2:
      return (*itBegin + *(itBegin+1));

    default:
      auto const nhalf = n/2 + 1;
      auto const s1 = cilk_spawn accum_spawn_helper( itBegin, itBegin + nhalf );
      auto const s2 = cilk_spawn accum_spawn_helper( itBegin + nhalf, itEnd );
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

// #ifdef __linux__
//   // Copied from rrcalls.h
// #define RR_CALL_BASE 1000
// #define SYS_rrcall_check_presence (RR_CALL_BASE + 8)
// #define SYS_rrcall_current_time (RR_CALL_BASE + 10)
//   {
//     fprintf(stderr, "RR syscall from sum-vector-int\n");
//     int res = syscall(SYS_rrcall_check_presence, 0, 0, 0, 0, 0, 0);
//     fprintf(stderr, "syscall(rr_check_presence) returned %d\n",
//             res);
//     if (-1 == res)
//       perror("Error calling rrcall_check_presence");
//   }
// #endif

  /* variables */
  
  num_t n;

  /* syntax check and input parsing */
  
  switch (argc) {
    
  case 1: {			// default N
    n = N;
    break; }
    
  case 2: {			// user-specified N
    std::istringstream iss( argv[1] );
    if (!(iss >> n)) {
      std::cerr << "Could not read input (" << argv[1] << ")"
		<< " as type '" << typeid(n).name() << "'" << std::endl;
      return 2;
    }
    break; }
    
  default: {			// wrong syntax
    std::cerr << "Usage: sum-vector-int [N]"   << std::endl
	      << "	 (N: sum upper limit)" << std::endl;
    return 1; }
    
  }  // end switch (# input arguments)

  /* create vector of N random integers */

  num_vec_t vals (n);
  
  // std::random_device rd;
  std::default_random_engine rng;
  std::uniform_int_distribution< num_t > rand( 1, MAX_VAL );
  std::generate( vals.begin(), vals.end(), [&] () { return rand(rng); } );

  std::cout << "Calculate sum of " << n << " integers" << std::endl;

  /* run various accumulation functions */
  
  sum_t const sum_true =
    time_accum_func< accum_true >( vals, -1, "true solution (std::accumulate)" );
  
  time_accum_func< accum_wrong >( vals, sum_true, "racy cilk_for (*WRONG*)" );

  time_accum_func< accum_lock >( vals, sum_true, "cilk_for w/ POSIX lock" );

  time_accum_func< accum_spawn >( vals, sum_true, "cilk_spawn reduction" );

  time_accum_func< accum_reducer >( vals, sum_true, "cilk reducer" );

#ifdef WLS
  time_accum_func< accum_wls >( vals, sum_true, "cilk WLS" );
#endif
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
sum_t time_accum_func (num_vec_t const & vals,
		       sum_t const sum_true,
		       std::string const desc) {

  std::cout << "..." << desc << "..." << std::endl;
  
  auto  const tstart = chrono_clock::now();
  sum_t const sum    = f( vals );
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
sum_t accum_true (num_vec_t const & vals) {

  return std::accumulate( vals.cbegin(), vals.cend(), static_cast<sum_t>( 0 ) );
  
}

// CHECK-LABEL: true solution

// CHECK-NOT: Race detected on location

/* ******************************
 * accum_wrong */
/**
 * @brief	Parallel accumulation of vector values using cilk_for, without 
 *		special handling of critical section.
 */
sum_t accum_wrong (num_vec_t const & vals) {

  sum_t sum = 0;
  cilk_for (auto i = 0; i < vals.size(); i++)
    sum += vals[i];

  return sum;

}

// CHECK-LABEL: racy cilk_for

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
sum_t accum_lock (num_vec_t const & vals) {

  pthread_mutex_t mutex;
  pthread_mutex_init( &mutex, NULL );
  sum_t sum = 0;
  cilk_for (auto i = 0; i < vals.size(); i++) {
    pthread_mutex_lock( &mutex );
    sum += vals[i];
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
sum_t accum_spawn (num_vec_t const & vals) {

  return accum_spawn_helper( vals.cbegin(), vals.cend() );

}

// CHECK-LABEL: cilk_spawn reduction

// CHECK-NOT: Race detected on location

/* ******************************
 * accum_reducer */
/**
 * @brief	Parallel accumulation of vector values using a reducer
 *		hyperobject.
 */
sum_t accum_reducer (num_vec_t const & vals) {

  cilk::opadd_reducer<sum_t> sum(0);
  cilk_for (auto i = 0; i < vals.size(); i++)
    sum += vals[i];
  
  return sum;
  
}

// CHECK-LABEL: cilk reducer

// CHECK-NOT: Race detected on location

#ifdef WLS
/* ******************************
 * accum_wls */
/**
 * @brief	Parallel accumulation of vector values using 
 *              worker-local storage.
 */
sum_t accum_wls (num_vec_t const & vals) {
  sum_t wls_sum[__cilkrts_get_nworkers()];
  sum_t sum = 0;

  for (int i = 0; i < __cilkrts_get_nworkers(); ++i)
    wls_sum[i] = 0;

  cilk_for (auto i = 0; i < vals.size(); i++)
    wls_sum[__cilkrts_get_worker_number()] += vals[i];

  for (int i = 0; i < __cilkrts_get_nworkers(); ++i)
    sum += wls_sum[i];

  return sum;
}
#endif

// CHECK: Cilksan detected 2 distinct races.
// CHECK-NEXT: Cilksan suppressed {{[0-9]+}} duplicate race reports.
