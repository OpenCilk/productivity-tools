#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include <cilk/cilk.h>

int main(int argc, char *argv[]) {
  long n = 0;
  if (argc > 1)
    n = atol(argv[1]);

  // Check that instrumentation on this allocation is handled
  // correctly, even when n == 0.
  long x[n];

  // Ensure that we have racing accesses on x, so that x is
  // instrumented.
  cilk_spawn {
    cilk_for (long i = 0; i < n; ++i)
      x[i] = sin(i);
  }
  cilk_for (long i = 0; i < n; ++i)
    x[i] = cos(i);

  for (long i = 0; i < n; ++i)
    printf("sin(%ld) = %ld\n", i, x[i]);

  return 0;
}
