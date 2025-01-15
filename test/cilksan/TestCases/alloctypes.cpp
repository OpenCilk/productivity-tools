// RUN: %clangxx_cilksan -fopencilk -Og %s -o %t -DGLOBAL
// RUN: %run %t 2>&1 | FileCheck %s --check-prefix=CHECK-GLOBAL
// RUN: %clangxx_cilksan -fopencilk -Og %s -o %t -DLOCAL
// RUN: %run %t 2>&1 | FileCheck %s --check-prefix=CHECK-LOCAL
// RUN: %clangxx_cilksan -fopencilk -Og %s -o %t -DPARAM
// RUN: %run %t 2>&1 | FileCheck %s --check-prefix=CHECK-PARAM
// RUN: %clangxx_cilksan -fopencilk -Og %s -o %t -g -DMALLOC
// RUN: %run %t 2>&1 | FileCheck %s --check-prefix=CHECK-MALLOC
// RUN: %clangxx_cilksan -fopencilk -Og %s -o %t -g -DCALLOC
// RUN: %run %t 2>&1 | FileCheck %s --check-prefix=CHECK-CALLOC
// RUN: %clangxx_cilksan -fopencilk -Og %s -o %t -g -DREALLOC
// RUN: %run %t 2>&1 | FileCheck %s --check-prefix=CHECK-REALLOC
// RUN: %clangxx_cilksan -fopencilk -Og %s -o %t -g -DNEW
// RUN: %run %t 2>&1 | FileCheck %s --check-prefix=CHECK-NEW

#include <iostream>
#include <cilk/cilk.h>

#define ITERS 5

struct Foo_st {
  int a = 0;
  double b = 0.0;
};

class Foo {
  int val = 0;
public:
  Foo() {}
  ~Foo() {}
  int &getVal() { return val; }
  __attribute__((noinline))
  void incVal() { val++; }
};

class Bar {
  int val[4] = {0,0,0,0};
public:
  Bar() {}
  ~Bar() {}
  int &getVal(int i) { return val[i]; }
  __attribute__((noinline))
  void incVal(int i) { val[i]++; }
};

int global = 0;

__attribute__((noinline))
static void helper(int &x) {
  x++;
}

__attribute__((noinline))
static void arr_helper(int *x, int n) {
  for (int i = 0; i < n; i++)
    x[i]++;
}

void global_test() {
  std::cout << "global_test: " << (void*)&global << "\n";
  cilk_for (int i = 0; i < ITERS; i++)
    helper(global);

  cilk_for (int i = 0; i < ITERS; i++)
    global--;
  std::cout << global << '\n';
}

// CHECK-GLOBAL-LABEL: global_test:
// CHECK-GLOBAL: 0x[[GLOBAL:[0-9a-f]+]]

// CHECK-GLOBAL: Race detected on location [[GLOBAL]]
// CHECK-GLOBAL-NEXT: * Write {{[0-9a-f]+}} helper
// CHECK-GLOBAL: Call {{[0-9a-f]+}} global_test
// CHECK-GLOBAL-NEXT: * Read {{[0-9a-f]+}} helper
// CHECK-GLOBAL: Call {{[0-9a-f]+}} global_test
// CHECK-GLOBAL-NEXT: Common calling context
// CHECK-GLOBAL-NEXT: Parfor

// CHECK-GLOBAL: Race detected on location [[GLOBAL]]
// CHECK-GLOBAL-NEXT: * Write {{[0-9a-f]+}} helper
// CHECK-GLOBAL: Call {{[0-9a-f]+}} global_test
// CHECK-GLOBAL-NEXT: * Write {{[0-9a-f]+}} helper
// CHECK-GLOBAL: Call {{[0-9a-f]+}} global_test
// CHECK-GLOBAL-NEXT: Common calling context
// CHECK-GLOBAL-NEXT: Parfor

// CHECK-GLOBAL: Race detected on location [[GLOBAL]]
// CHECK-GLOBAL: * Write {{[0-9a-f]+}} global_test
// CHECK-GLOBAL: * Read {{[0-9a-f]+}} global_test
// CHECK-GLOBAL: Common calling context
// CHECK-GLOBAL-NEXT: Parfor

// CHECK-GLOBAL: Race detected on location [[GLOBAL]]
// CHECK-GLOBAL: * Write {{[0-9a-f]+}} global_test
// CHECK-GLOBAL: * Write {{[0-9a-f]+}} global_test
// CHECK-GLOBAL: Common calling context
// CHECK-GLOBAL-NEXT: Parfor

// CHECK-GLOBAL: Cilksan detected 4 distinct races.
// CHECK-GLOBAL-NEXT: Cilksan suppressed 20 duplicate race reports.

void local_test() {
  std::cout << "local_test\n";
  int local = 1;
  cilk_for (int i = 0; i < ITERS; i++)
    helper(local);
  std::cout << local << '\n';
}

// CHECK-LOCAL-LABEL: local_test

// CHECK-LOCAL: Race detected on location [[LOCAL:[0-9a-f]+]]
// CHECK-LOCAL-NEXT: * Write {{[0-9a-f]+}} helper
// CHECK-LOCAL: Call {{[0-9a-f]+}} local_test
// CHECK-LOCAL-NEXT: * Read {{[0-9a-f]+}} helper
// CHECK-LOCAL: Call {{[0-9a-f]+}} local_test
// CHECK-LOCAL-NEXT: Common calling context
// CHECK-LOCAL-NEXT: Parfor
// CHECK-LOCAL: Stack object local

// CHECK-LOCAL: Race detected on location [[LOCAL]]
// CHECK-LOCAL-NEXT: * Write {{[0-9a-f]+}} helper
// CHECK-LOCAL: Call {{[0-9a-f]+}} local_test
// CHECK-LOCAL-NEXT: * Write {{[0-9a-f]+}} helper
// CHECK-LOCAL: Call {{[0-9a-f]+}} local_test
// CHECK-LOCAL-NEXT: Common calling context
// CHECK-LOCAL-NEXT: Parfor
// CHECK-LOCAL: Stack object local

// CHECK-LOCAL: Cilksan detected 2 distinct races.
// CHECK-LOCAL-NEXT: Cilksan suppressed 10 duplicate race reports.

void param_test_helper(int &param) {
  cilk_for (int i = 0; i < ITERS; i++)
    helper(param);
}

void param_test() {
  std::cout << "param_test\n";
  int parent_local = 2;
  param_test_helper(parent_local);
  std::cout << parent_local << '\n';
}

// CHECK-PARAM-LABEL: param_test

// CHECK-PARAM: Race detected on location [[PARAM:[0-9a-f]+]]
// CHECK-PARAM-NEXT: * Write {{[0-9a-f]+}} helper
// CHECK-PARAM: Call {{[0-9a-f]+}} param_test
// CHECK-PARAM-NEXT: * Read {{[0-9a-f]+}} helper
// CHECK-PARAM: Call {{[0-9a-f]+}} param_test
// CHECK-PARAM-NEXT: Common calling context
// CHECK-PARAM-NEXT: Parfor

// CHECK-PARAM: Race detected on location [[PARAM]]
// CHECK-PARAM-NEXT: * Write {{[0-9a-f]+}} helper
// CHECK-PARAM: Call {{[0-9a-f]+}} param_test
// CHECK-PARAM-NEXT: * Write {{[0-9a-f]+}} helper
// CHECK-PARAM: Call {{[0-9a-f]+}} param_test
// CHECK-PARAM-NEXT: Common calling context
// CHECK-PARAM-NEXT: Parfor

// CHECK-PARAM: Cilksan detected 2 distinct races.
// CHECK-PARAM-NEXT: Cilksan suppressed 10 duplicate race reports.

int *malloc_test(int size) {
  std::cout << "malloc_test\n";
  int *x = (int*)malloc(size * sizeof(int));
  x[0] = 0;
  cilk_for (int i = 0; i < ITERS; i++)
    arr_helper(x, size);
  std::cout << x[0] << '\n';
  return x;
}

// CHECK-MALLOC-LABEL: malloc_test

// CHECK-MALLOC: Race detected on location [[MALLOC:[0-9a-f]+]]
// CHECK-MALLOC-NEXT: * Write {{[0-9a-f]+}} arr_helper
// CHECK-MALLOC: Call {{[0-9a-f]+}} malloc_test
// CHECK-MALLOC-NEXT: * Read {{[0-9a-f]+}} arr_helper
// CHECK-MALLOC: Call {{[0-9a-f]+}} malloc_test
// CHECK-MALLOC-NEXT: Common calling context
// CHECK-MALLOC-NEXT: Parfor
// CHECK-MALLOC: Heap object x

// CHECK-MALLOC: Race detected on location [[MALLOC]]
// CHECK-MALLOC-NEXT: * Write {{[0-9a-f]+}} arr_helper
// CHECK-MALLOC: Call {{[0-9a-f]+}} malloc_test
// CHECK-MALLOC-NEXT: * Write {{[0-9a-f]+}} arr_helper
// CHECK-MALLOC: Call {{[0-9a-f]+}} malloc_test
// CHECK-MALLOC-NEXT: Common calling context
// CHECK-MALLOC-NEXT: Parfor
// CHECK-MALLOC: Heap object x

// CHECK-MALLOC: Cilksan detected 2 distinct races.
// CHECK-MALLOC-NEXT: Cilksan suppressed 10 duplicate race reports.

void calloc_test(int size) {
  std::cout << "calloc_test\n";
  int *y = (int*)calloc(size, sizeof(int));
  cilk_for (int i = 0; i < ITERS; i++)
    arr_helper(y, size);
  std::cout << y[0] << '\n';
  free(y);
}

// CHECK-CALLOC-LABEL: calloc_test

// CHECK-CALLOC: Race detected on location [[CALLOC:[0-9a-f]+]]
// CHECK-CALLOC-NEXT: * Write {{[0-9a-f]+}} arr_helper
// CHECK-CALLOC: Call {{[0-9a-f]+}} calloc_test
// CHECK-CALLOC-NEXT: * Read {{[0-9a-f]+}} arr_helper
// CHECK-CALLOC: Call {{[0-9a-f]+}} calloc_test
// CHECK-CALLOC-NEXT: Common calling context
// CHECK-CALLOC-NEXT: Parfor
// CHECK-CALLOC: Heap object y

// CHECK-CALLOC: Race detected on location [[CALLOC]]
// CHECK-CALLOC-NEXT: * Write {{[0-9a-f]+}} arr_helper
// CHECK-CALLOC: Call {{[0-9a-f]+}} calloc_test
// CHECK-CALLOC-NEXT: * Write {{[0-9a-f]+}} arr_helper
// CHECK-CALLOC: Call {{[0-9a-f]+}} calloc_test
// CHECK-CALLOC-NEXT: Common calling context
// CHECK-CALLOC-NEXT: Parfor
// CHECK-CALLOC: Heap object y

// CHECK-CALLOC: Cilksan detected 2 distinct races.
// CHECK-CALLOC-NEXT: Cilksan suppressed 10 duplicate race reports.

int *realloc_test(int *x, int size) {
  std::cout << "realloc_test\n";
  x = (int*)realloc(x, size * sizeof(int));
  cilk_for (int i = 0; i < ITERS; i++)
    arr_helper(x, size);
  std::cout << x[0] << '\n';
  return x;
}

// CHECK-REALLOC-LABEL: realloc_test

// CHECK-REALLOC: Race detected on location [[REALLOC:[0-9a-f]+]]
// CHECK-REALLOC-NEXT: * Write {{[0-9a-f]+}} arr_helper
// CHECK-REALLOC: Call {{[0-9a-f]+}} realloc_test
// CHECK-REALLOC-NEXT: * Read {{[0-9a-f]+}} arr_helper
// CHECK-REALLOC: Call {{[0-9a-f]+}} realloc_test
// CHECK-REALLOC-NEXT: Common calling context
// CHECK-REALLOC-NEXT: Parfor
// CHECK-REALLOC: Heap object x

// CHECK-REALLOC: Race detected on location [[REALLOC]]
// CHECK-REALLOC-NEXT: * Write {{[0-9a-f]+}} arr_helper
// CHECK-REALLOC: Call {{[0-9a-f]+}} realloc_test
// CHECK-REALLOC-NEXT: * Write {{[0-9a-f]+}} arr_helper
// CHECK-REALLOC: Call {{[0-9a-f]+}} realloc_test
// CHECK-REALLOC-NEXT: Common calling context
// CHECK-REALLOC-NEXT: Parfor
// CHECK-REALLOC: Heap object x

// CHECK-REALLOC: Cilksan detected 4 distinct races.
// CHECK-REALLOC-NEXT: Cilksan suppressed 20 duplicate race reports.

void new_test() {
  std::cout << "new_test\n";
  Foo *x = new Foo();
  cilk_for (int i = 0; i < ITERS; i++)
    // 2 distinct races, 10 duplicate
    x->getVal()--;
  std::cout << "x->getVal() = " << x->getVal() << '\n';

  cilk_for (int i = 0; i < ITERS; i++)
    // 2 distinct races, 10 duplicate
    x->incVal();
  std::cout << "x->getVal() = " << x->getVal() << '\n';
  delete x;

  Bar *y = new Bar();
  cilk_for (int i = 0; i < ITERS; i++)
    // 2 distinct races, 1 duplicate
    y->getVal(i % 4)--;

  cilk_for (int i = 0; i < ITERS; i++) {
    // 2 distinct races, 1 duplicate
    y->incVal(i % 4);
  }
  std::cout << "y->getVal(0) = " << y->getVal(0) << '\n';
  delete y;

  Foo_st *z = new Foo_st();
  cilk_for (int i = 0; i < ITERS; i++) {
    // 4 distinct races, 20 duplicate
    z->a++;
    z->b += z->a * i;
  }
  std::cout << "z->b = " << z->b << '\n';
  delete z;
}

// CHECK-NEW-LABEL: new_test

// CHECK-NEW: Race detected on location [[X:[0-9a-f]+]]
// CHECK-NEW-NEXT: * Write {{[0-9a-f]+}} new_test
// CHECK-NEW: * Read {{[0-9a-f]+}} new_test
// CHECK-NEW: Common calling context
// CHECK-NEW-NEXT: Parfor
// CHECK-NEW: Heap object this

// CHECK-NEW: Race detected on location [[X]]
// CHECK-NEW-NEXT: * Write {{[0-9a-f]+}} new_test
// CHECK-NEW: * Write {{[0-9a-f]+}} new_test
// CHECK-NEW: Common calling context
// CHECK-NEW-NEXT: Parfor
// CHECK-NEW: Heap object this

// CHECK-NEW: x->getVal()

// CHECK-NEW: Race detected on location [[X]]
// CHECK-NEW-NEXT: * Write {{[0-9a-f]+}} incVal
// CHECK-NEW: Call {{[0-9a-f]+}} new_test
// CHECK-NEW-NEXT: * Read {{[0-9a-f]+}} incVal
// CHECK-NEW: Call {{[0-9a-f]+}} new_test
// CHECK-NEW-NEXT: Common calling context
// CHECK-NEW-NEXT: Parfor
// CHECK-NEW: Heap object this

// CHECK-NEW: Race detected on location [[X]]
// CHECK-NEW-NEXT: * Write {{[0-9a-f]+}} incVal
// CHECK-NEW: Call {{[0-9a-f]+}} new_test
// CHECK-NEW-NEXT: * Write {{[0-9a-f]+}} incVal
// CHECK-NEW: Call {{[0-9a-f]+}} new_test
// CHECK-NEW-NEXT: Common calling context
// CHECK-NEW-NEXT: Parfor
// CHECK-NEW: Heap object this

// CHECK-NEW: x->getVal()

// CHECK-NEW: Race detected on location [[Y:[0-9a-f]+]]
// CHECK-NEW-NEXT: * Write {{[0-9a-f]+}} new_test
// CHECK-NEW: * Read {{[0-9a-f]+}} new_test
// CHECK-NEW: Common calling context
// CHECK-NEW-NEXT: Parfor
// CHECK-NEW: Heap object this

// CHECK-NEW: Race detected on location [[Y]]
// CHECK-NEW-NEXT: * Write {{[0-9a-f]+}} new_test
// CHECK-NEW: * Write {{[0-9a-f]+}} new_test
// CHECK-NEW: Common calling context
// CHECK-NEW-NEXT: Parfor
// CHECK-NEW: Heap object this

// CHECK-NEW: Race detected on location [[Y]]
// CHECK-NEW-NEXT: * Write {{[0-9a-f]+}} incVal
// CHECK-NEW: Call {{[0-9a-f]+}} new_test
// CHECK-NEW-NEXT: * Read {{[0-9a-f]+}} incVal
// CHECK-NEW: Call {{[0-9a-f]+}} new_test
// CHECK-NEW-NEXT: Common calling context
// CHECK-NEW-NEXT: Parfor
// CHECK-NEW: Heap object this

// CHECK-NEW: Race detected on location [[Y]]
// CHECK-NEW-NEXT: * Write {{[0-9a-f]+}} incVal
// CHECK-NEW: Call {{[0-9a-f]+}} new_test
// CHECK-NEW-NEXT: * Write {{[0-9a-f]+}} incVal
// CHECK-NEW: Call {{[0-9a-f]+}} new_test
// CHECK-NEW-NEXT: Common calling context
// CHECK-NEW-NEXT: Parfor
// CHECK-NEW: Heap object this

// CHECK-NEW: y->getVal(0)

// CHECK-NEW: Race detected on location [[ZA:[0-9a-f]+]]
// CHECK-NEW-NEXT: * Write {{[0-9a-f]+}} new_test
// CHECK-NEW: * Read {{[0-9a-f]+}} new_test
// CHECK-NEW: Common calling context
// CHECK-NEW-NEXT: Parfor
// CHECK-NEW: Heap object this

// CHECK-NEW: Race detected on location [[ZA]]
// CHECK-NEW-NEXT: * Write {{[0-9a-f]+}} new_test
// CHECK-NEW: * Write {{[0-9a-f]+}} new_test
// CHECK-NEW: Common calling context
// CHECK-NEW-NEXT: Parfor
// CHECK-NEW: Heap object this

// CHECK-NEW: Race detected on location [[ZB:[0-9a-f]+]]
// CHECK-NEW-NEXT: * Write {{[0-9a-f]+}} new_test
// CHECK-NEW: * Read {{[0-9a-f]+}} new_test
// CHECK-NEW: Common calling context
// CHECK-NEW-NEXT: Parfor
// CHECK-NEW: Heap object this

// CHECK-NEW: Race detected on location [[ZB]]
// CHECK-NEW-NEXT: * Write {{[0-9a-f]+}} new_test
// CHECK-NEW: * Write {{[0-9a-f]+}} new_test
// CHECK-NEW: Common calling context
// CHECK-NEW-NEXT: Parfor
// CHECK-NEW: Heap object this

// CHECK-NEW: z->b

// CHECK-NEW: Cilksan detected 12 distinct races.
// CHECK-NEW-NEXT: Cilksan suppressed 42 duplicate race reports.

int main(int argc, char** argv) {
  int arrsize = 1;
  if (argc == 2)
    arrsize = atoi(argv[1]);

#ifdef GLOBAL
  // Race 1
  global_test();
#endif

#ifdef LOCAL
  // Race 2
  local_test();
#endif

#ifdef PARAM
  // Race 3
  // int parent_local = 2;
  // param_test(parent_local);
  param_test();
#endif

#if defined(MALLOC) || defined(REALLOC)
  // Race 4
  int *x = malloc_test(arrsize);
#endif

#ifdef CALLOC
  // Race 5
  calloc_test(arrsize);
#endif

#ifdef REALLOC
  // Race 6
  x = realloc_test(x, 8 * arrsize);
  free(x);
#endif

  // // Redundant with race 4
  // x = malloc_test(arrsize);
  // free(x);
  
#ifdef NEW
  // Races 7-12
  new_test();
#endif

  return 0;
}
