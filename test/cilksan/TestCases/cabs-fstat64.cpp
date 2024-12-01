// RUN: %clangxx_cilksan -fopencilk -Og %s -o %t -g
// RUN: %run %t 2>&1 | FileCheck %s
// UNSUPPORTED: darwin

#include <complex>
#include <iostream>
#include <sys/stat.h>

int main() {
  std::cout << std::abs(std::complex<double>(1.0, 2.0)) << std::endl;
  struct stat64 buf;
  ::fstat64(0, &buf);
  std::cout << buf.st_dev << std::endl;
  return 0;
}

// CHECK: Cilksan detected 0 distinct races.
// CHECK-NEXT: Cilksan suppressed 0 duplicate race reports.
