// Verify that compiling and linking with Cilksan works in the presence of various library function calls.
//
// RUN: %clangxx_cilksan -fopencilk -Og %s -o %t -g

#include <cstdlib>
#include <exception>

void test1() { std::terminate(); }

void test2() { exit(1); }

void test3() { abort(); }

void test4() { atexit(test2); }

int main() { return 0; }
