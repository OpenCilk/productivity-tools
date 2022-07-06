// RUN: %clangxx_cilksan -fopencilk -Og %s -o %t
// RUN: %run %t 2>&1 | FileCheck %s

#include <stdio.h>
#include <cilk/cilk.h>
#include <cilk/opadd_reducer.h>
#include <chrono>
#include <thread>
#include <pthread.h>

int main() {
  cilk::opadd_reducer<int> sum;
  int rsum = 0;
  int lsum = 0;
  pthread_mutex_t mtex;
  pthread_mutex_init(&mtex, NULL);
  cilk_for (int i = 0; i <= 10000; i++) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    sum += i;
    rsum += i;
    pthread_mutex_lock(&mtex);
    lsum += i;
    pthread_mutex_unlock(&mtex);
  }
  printf("%p\n%p\n%p\n",(void*)&(sum),(void*)&rsum,(void*)&lsum);
  printf("%d\n%d\n%d\n",sum,rsum,lsum);
}

// CHECK: Race detected on location [[RSUM:[0-9a-f]+]]
// CHECK-NEXT: * Write {{[0-9a-f]+}} main
// CHECK-NEXT: to variable rsum
// CHECK-NEXT: * Read {{[0-9a-f]+}} main
// CHECK-NEXT: to variable rsum
// CHECK-NEXT: Common calling context
// CHECK-NEXT: Parfor

// CHECK: Race detected on location [[RSUM]]
// CHECK-NEXT: * Write {{[0-9a-f]+}} main
// CHECK-NEXT: to variable rsum
// CHECK-NEXT: * Write {{[0-9a-f]+}} main
// CHECK-NEXT: to variable rsum
// CHECK-NEXT: Common calling context
// CHECK-NEXT: Parfor

// Verify that no other races are detected
// CHECK-NOT: Race detected on location

// CHECK: 0x{{[0-9a-f]+}}
// CHECK-NEXT: 0x[[RSUM]]
// CHECK-NEXT: 0x{{[0-9a-f]+}}
// CHECK-NEXT: 50005000
// CHECK-NEXT: 50005000
// CHECK-NEXT: 50005000

// CHECK: Cilksan detected 2 distinct races.
// CHECK-NEXT: Cilksan suppressed {{[0-9]+}} duplicate race reports.
