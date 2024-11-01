#include "debug_util.h"
#include "driver.h"
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <signal.h>

/*
  static void print_bt(FILE *f) {
  const int N=10;
  void *buf[N];
  int n = backtrace(buf, N);
  if (1) {
  for (int i = 0; i < n; i++) {
  print_addr(f, buf[i]);
  }
  } else {
  assert(n>=2);
  print_addr(f, buf[2]);
  }
  }
*/

void debug_printf(const char *fmt, ...) {
  std::va_list l;
  va_start(l, fmt);
  std::vfprintf(stderr, fmt, l);
  va_end(l);
}

// Print out the error message and exit
__attribute__((noreturn))
void die(const char *fmt, ...) {
  std::va_list l;
  std::fprintf(err_io, "=================================================\n");
  std::fprintf(err_io, "Cilksan: fatal error\n");

  va_start(l, fmt);
  std::vfprintf(err_io, fmt, l);
  std::fprintf(err_io, "=================================================\n");
  fflush(err_io);
  va_end(l);
  raise(SIGTRAP);
  std::exit(1);
}

