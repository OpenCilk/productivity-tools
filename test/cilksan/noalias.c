#include <stdio.h>
#include <stdlib.h>
#include <cilk/cilk.h>

void inc(int *x) {
  cilk_spawn x[0]++;
  x[1]++;
}

void inc_loop(int *x, int *y) {
  #pragma cilk grainsize 1
  cilk_for (int i = 0; i < 2; ++i) {
    x[i]++;
    y[i]++;
  }
}

__attribute__((noinline))
int *frob(int *x) {
  return x+1;
}

int main(int argc, char *argv[]) {
  int n = 10;
  if (argc > 1)
    n = atoi(argv[1]);

  int *x = (int*)malloc(n * sizeof(int));
  cilk_for (int i = 0; i < n; ++i)
    x[i] = 0;

  // Race
  inc_loop(x, x+1);
  // Duplicate race
  inc_loop(x+2, frob(x+2));

  // Race
  cilk_spawn inc(x);
  inc(x+1);
  cilk_sync;

  // Duplicate race
  cilk_spawn inc(x+3);
  inc(frob(x+3));
  cilk_sync;

  for (int i = 0; i < n; ++i)
    printf("x[%d] = %d\n", i, x[i]);

  return 0;
}
