// RUN: %clangxx_cilksan -fopencilk -Og %s -o %t -g
// RUN: %run %t 2>&1 | FileCheck %s

#include <cstdio>
#include <cstring>
#include <iostream>
#include <cilk/cilk.h>

double global = 0.0;

__attribute__((noinline))
void global_printf_test(int n) {
  // Write-read race
  cilk_spawn { global += n; }
  fprintf(stderr, "global = %f\n", global);
}

__attribute__((noinline))
void global_cout_test(int n) {
  // Write-read race
  cilk_spawn { global *= n; }
  std::cout << "global = " << global << "\n";
}

__attribute__((noinline))
static void arr_helper(int *x, int n) {
  for (int i = 0; i < n; i++)
    x[i]++;
}

__attribute__((noinline))
void malloc_free_test(int size) {
  int *x = (int*)malloc(size * sizeof(int));
  // Write-free race
  cilk_spawn arr_helper(x, size);
  free(x);
}

__attribute__((noinline))
void malloc_printf_test(int size) {
  int *x = (int*)malloc(size * sizeof(int));
  // Write-read race
  cilk_spawn arr_helper(x, size);
  printf("x[0] = %d\n", x[0]);
  cilk_sync;
  free(x);
}

__attribute__((noinline))
void malloc_cout_test(int size) {
  int *x = (int*)malloc(size * sizeof(int));
  // Write-read race
  cilk_spawn arr_helper(x, size);
  std::cout << "x[0] = " << x[0] << "\n";
  cilk_sync;
  free(x);
}

__attribute__((noinline))
void str_printf_test() {
  const char *str = "Hello, world!";
  char *cpy = (char *)malloc(sizeof(*str));
  cilk_spawn strcpy(cpy, str);
  // No race
  printf("str len = %ld\n", strlen(str));
  // Race with spawned strcpy
  cilk_spawn printf("cpy %p len = %ld\n", (void*)cpy, strlen(cpy));
  // Race with spawned strcpy
  char *str2 = cilk_spawn strdup(cpy);
  // Race with spawned strdup
  printf("str2 %p: %s\n", (void*)str2, str2);
  // Race with spawned strcpy
  char *str3 = cilk_spawn strndup(cpy, 5);
  // Race with spawned strndup
  printf("str3 %p: %s\n", (void*)str3, str3);
  free(str2);
  free(str3);
}

int main(int argc, char** argv) {
  int size = 1;
  if (argc == 2)
    size = atoi(argv[1]);

  global_printf_test(size);
  global_cout_test(size);

  malloc_free_test(size);

  malloc_printf_test(size);
  malloc_cout_test(size);

  str_printf_test();

  return 0;
}

// CHECK: Race detected on location [[GLOBAL:[0-9a-f]+]]
// CHECK-NEXT: * Write {{[0-9a-f]+}} global_printf_test
// CHECK-NEXT: to variable global
// CHECK-NEXT: Spawn {{[0-9a-f]+}} global_printf_test
// CHECK-NEXT: * Read {{[0-9a-f]+}} global_printf_test
// CHECK-NEXT: to variable global
// CHECK-NEXT: Common calling context
// CHECK-NEXT: Call {{[0-9a-f]+}} main

// CHECK: Race detected on location [[GLOBAL]]
// CHECK-NEXT: * Write {{[0-9a-f]+}} global_cout_test
// CHECK-NEXT: to variable global
// CHECK-NEXT: Spawn {{[0-9a-f]+}} global_cout_test
// CHECK-NEXT: * Read {{[0-9a-f]+}} global_cout_test
// CHECK-NEXT: to variable global
// CHECK-NEXT: Common calling context
// CHECK-NEXT: Call {{[0-9a-f]+}} main

// CHECK: Race detected on location [[X:[0-9a-f]+]]
// CHECK-NEXT: * Write {{[0-9a-f]+}} arr_helper
// CHECK-NEXT: to variable x
// CHECK-NEXT: Spawn {{[0-9a-f]+}} malloc_free_test
// CHECK-NEXT: * Free {{[0-9a-f]+}} malloc_free_test
// CHECK-NEXT: Common calling context
// CHECK-NEXT: Call {{[0-9a-f]+}} main
// CHECK: Heap object x

// CHECK: Race detected on location [[X]]
// CHECK-NEXT: * Read {{[0-9a-f]+}} arr_helper
// CHECK-NEXT: to variable x
// CHECK-NEXT: Spawn {{[0-9a-f]+}} malloc_free_test
// CHECK-NEXT: * Free {{[0-9a-f]+}} malloc_free_test
// CHECK-NEXT: Common calling context
// CHECK-NEXT: Call {{[0-9a-f]+}} main
// CHECK: Heap object x

// CHECK: Race detected on location [[X:[0-9a-f]+]]
// CHECK-NEXT: * Write {{[0-9a-f]+}} arr_helper
// CHECK-NEXT: to variable x
// CHECK-NEXT: Spawn {{[0-9a-f]+}} malloc_printf_test
// CHECK-NEXT: * Read {{[0-9a-f]+}} malloc_printf_test
// CHECK-NEXT: to variable
// CHECK-NEXT: Common calling context
// CHECK-NEXT: Call {{[0-9a-f]+}} main
// CHECK: Heap object x

// CHECK: Race detected on location
// CHECK: [[X:[0-9a-f]+]]
// CHECK-NEXT: * Write {{[0-9a-f]+}} arr_helper
// CHECK-NEXT: to variable x
// CHECK-NEXT: Spawn {{[0-9a-f]+}} malloc_cout_test
// CHECK-NEXT: * Read {{[0-9a-f]+}} malloc_cout_test
// CHECK-NEXT: to variable
// CHECK-NEXT: Common calling context
// CHECK-NEXT: Call {{[0-9a-f]+}} main
// CHECK: Heap object x

// CHECK: Race detected on location
// CHECK: [[CPY:[0-9a-f]+]]
// CHECK-NEXT: * Write {{[0-9a-f]+}} str_printf_test
// CHECK-NEXT: to variable cpy
// CHECK-NEXT: Spawn {{[0-9a-f]+}} str_printf_test
// CHECK-NEXT: * Read {{[0-9a-f]+}} str_printf_test
// CHECK-NEXT: Common calling context
// CHECK-NEXT: Call {{[0-9a-f]+}} main
// CHECK: Heap object cpy

// CHECK: Race detected on location [[CPY]]
// CHECK-NEXT: * Write {{[0-9a-f]+}} str_printf_test
// CHECK-NEXT: to variable cpy
// CHECK-NEXT: Spawn {{[0-9a-f]+}} str_printf_test
// CHECK-NEXT: * Read {{[0-9a-f]+}} str_printf_test
// CHECK-NEXT: Spawn {{[0-9a-f]+}} str_printf_test
// CHECK-NEXT: Common calling context
// CHECK-NEXT: Call {{[0-9a-f]+}} main
// CHECK: Heap object cpy

// CHECK: Race detected on location [[STR2PTR:[0-9a-f]+]]
// CHECK-NEXT: * Write {{[0-9a-f]+}} str_printf_test
// CHECK-NEXT: to variable str2
// CHECK-NEXT: Spawn {{[0-9a-f]+}} str_printf_test
// CHECK-NEXT: * Read {{[0-9a-f]+}} str_printf_test
// CHECK-NEXT: to variable str2
// CHECK-NEXT: Common calling context
// CHECK-NEXT: Call {{[0-9a-f]+}} main

// CHECK: Race detected on location [[STR2:[0-9a-f]+]]
// CHECK-NEXT: * Write {{[0-9a-f]+}} str_printf_test
// CHECK-NEXT: Spawn {{[0-9a-f]+}} str_printf_test
// CHECK-NEXT: * Read {{[0-9a-f]+}} str_printf_test
// CHECK-NEXT: Common calling context
// CHECK-NEXT: Call {{[0-9a-f]+}} main

// CHECK: Race detected on location [[CPY]]
// CHECK-NEXT: * Write {{[0-9a-f]+}} str_printf_test
// CHECK-NEXT: to variable cpy
// CHECK-NEXT: Spawn {{[0-9a-f]+}} str_printf_test
// CHECK-NEXT: * Read {{[0-9a-f]+}} str_printf_test
// CHECK-NEXT: Spawn {{[0-9a-f]+}} str_printf_test
// CHECK-NEXT: Common calling context
// CHECK-NEXT: Call {{[0-9a-f]+}} main
// CHECK: Heap object cpy

// CHECK: Race detected on location [[STR3PTR:[0-9a-f]+]]
// CHECK-NEXT: * Write {{[0-9a-f]+}} str_printf_test
// CHECK-NEXT: to variable str3
// CHECK-NEXT: Spawn {{[0-9a-f]+}} str_printf_test
// CHECK-NEXT: * Read {{[0-9a-f]+}} str_printf_test
// CHECK-NEXT: to variable str3
// CHECK-NEXT: Common calling context
// CHECK-NEXT: Call {{[0-9a-f]+}} main

// CHECK: Race detected on location [[STR3:[0-9a-f]+]]
// CHECK-NEXT: * Write {{[0-9a-f]+}} str_printf_test
// CHECK-NEXT: Spawn {{[0-9a-f]+}} str_printf_test
// CHECK-NEXT: * Read {{[0-9a-f]+}} str_printf_test
// CHECK-NEXT: Common calling context
// CHECK-NEXT: Call {{[0-9a-f]+}} main

// CHECK: Race detected on location [[STR2]]
// CHECK-NEXT: * Write {{[0-9a-f]+}} str_printf_test
// CHECK-NEXT: Spawn {{[0-9a-f]+}} str_printf_test
// CHECK-NEXT: * Free {{[0-9a-f]+}} str_printf_test
// CHECK-NEXT: Common calling context
// CHECK-NEXT: Call {{[0-9a-f]+}} main

// CHECK: Race detected on location [[STR3]]
// CHECK-NEXT: * Write {{[0-9a-f]+}} str_printf_test
// CHECK-NEXT: Spawn {{[0-9a-f]+}} str_printf_test
// CHECK-NEXT: * Free {{[0-9a-f]+}} str_printf_test
// CHECK-NEXT: Common calling context
// CHECK-NEXT: Call {{[0-9a-f]+}} main

// CHECK: Cilksan detected 15 distinct races.
// CHECK-NEXT: Cilksan suppressed 0 duplicate race reports.
