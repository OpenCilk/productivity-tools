#include <stdio.h>
#include <cilk/cilk.h>
#include <cilk/reducer_opadd.h>
#include <chrono>
#include <thread>
#include <pthread.h>

int main() {
  cilk::reducer_opadd<int> sum;
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
    // &nbs p;
  }
  printf("%d\n%d\n%d\n",sum.get_value(),rsum,lsum);
}
