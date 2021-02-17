#include <cilk/cilk.h>
#include <stdio.h>
#include <stdlib.h>

int globl = 0;
void bar() {
  // Location of write-write race
  globl++;
}

void foo(int n) {
  int sum = 0;
  cilk_spawn bar();
  // Write-write race
  cilk_for(int i = 0; i < n; ++i)
    sum += i;
  bar();
  cilk_sync;
}

int main(int argc, char *argv[]) {
  int n = 4096;
  if (argc > 1)
    n = atoi(argv[1]);
  /* cilk_spawn printf("hi\n");; */
  foo(n);
  /* cilk_sync; */
  printf("globl = %d\n", globl);
  return 0;
}
