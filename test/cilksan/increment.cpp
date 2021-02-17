#include <iostream>
#include <cilk/cilk.h>
//#include <cilk/cilk_api.h>

int global = 0;

void increment(int *x, int n) {
  for (int i = 0; i < n; i++)
    x[i]++;
}

__attribute__((noinline))
void helper(int *x) {
  (*x)++;
}

int main(int argc, char** argv) {
  int n = 1;
  if (argc == 2) n = atoi(argv[1]);

  cilk_for (int i = 0; i < 1000; i++)
    helper(&global);
  std::cout << global << '\n';

  int local = 1;
  cilk_for (int i = 0; i < 1000; i++)
    helper(&local);
  std::cout << local << '\n';

  int *x = (int*)malloc(n * sizeof(int));
  cilk_for (int i = 0; i < 1000; i++)
    increment(x, n);
  std::cout << x[0] << '\n';

  int *y = (int*)calloc(n, sizeof(int));
  cilk_for (int i = 0; i < 1000; i++)
    increment(y, n);
  std::cout << y[0] << '\n';

  int *z = (int*)realloc(x, 4 * n * sizeof(int));
  cilk_for (int i = 0; i < 1000; i++)
    increment(z, 2 * n);
  std::cout << z[0] << '\n';

  return 0;
}
