#include <cstdio>
#include <cstring>
#include <iostream>
#include <cilk/cilk.h>

double global = 0.0;

void global_printf_test(int n) {
  // Write-read race
  cilk_spawn { global += n; }
  printf("global = %f\n", global);
}

void global_cout_test(int n) {
  // Write-read race
  cilk_spawn { global *= n; }
  std::cout << "global = " << global << "\n";
}

static void arr_helper(int *x, int n) {
  for (int i = 0; i < n; i++)
    x[i]++;
}

void malloc_free_test(int size) {
  int *x = (int*)malloc(size * sizeof(int));
  // Write-free race
  cilk_spawn arr_helper(x, size);
  free(x);
}

void malloc_printf_test(int size) {
  int *x = (int*)malloc(size * sizeof(int));
  // Write-read race
  cilk_spawn arr_helper(x, size);
  printf("x[0] = %d\n", x[0]);
  cilk_sync;
  free(x);
}

void malloc_cout_test(int size) {
  int *x = (int*)malloc(size * sizeof(int));
  // Write-read race
  cilk_spawn arr_helper(x, size);
  std::cout << "x[0] = " << x[0] << "\n";
  cilk_sync;
  free(x);
}

void str_printf_test() {
  const char *str = "Hello, world!";
  char *cpy = (char *)malloc(sizeof(*str));
  cilk_spawn strcpy(cpy, str);
  // No race
  printf("str len = %ld\n", strlen(str));
  // Race with spawned strcpy
  cilk_spawn printf("cpy len = %ld\n", strlen(cpy));
  // Race with spawned strcpy
  char *str2 = cilk_spawn strdup(cpy);
  // Race with spawned strdup
  printf("str2: %s\n", str2);
  // Race with spawned strcpy
  char *str3 = cilk_spawn strndup(cpy, 5);
  // Race with spawned strndup
  printf("str3: %s\n", str3);
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
