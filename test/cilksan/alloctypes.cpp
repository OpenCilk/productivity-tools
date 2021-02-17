#include <iostream>
#include <cilk/cilk.h>

struct Foo_st {
  int a = 0;
  double b = 0.0;
};

class Foo {
  int val = 0;
public:
  Foo() {}
  ~Foo() {}
  int &getVal() { return val; }
  void incVal() { val++; }
};

class Bar {
  int val[4] = {0,0,0,0};
public:
  Bar() {}
  ~Bar() {}
  int &getVal(int i) { return val[i]; }
  void incVal(int i) { val[i]++; }
};

int global = 0;

__attribute__((noinline))
static void helper(int &x) {
  x++;
}

static void arr_helper(int *x, int n) {
  for (int i = 0; i < n; i++)
    x[i]++;
}

void global_test() {
  std::cout << "global_test\n";
  cilk_for (int i = 0; i < 1000; i++)
    helper(global);

  cilk_for (int i = 0; i < 1000; i++)
    global--;
  std::cout << global << '\n';
}

void local_test() {
  std::cout << "local_test\n";
  int local = 1;
  cilk_for (int i = 0; i < 1000; i++)
    helper(local);
  std::cout << local << '\n';
}

void param_test(int &param) {
  std::cout << "param_test\n";
  cilk_for (int i = 0; i < 1000; i++)
    helper(param);
  std::cout << param << '\n';
}

int *malloc_test(int size) {
  std::cout << "malloc_test\n";
  int *x = (int*)malloc(size * sizeof(int));
  x[0] = 0;
  cilk_for (int i = 0; i < 1000; i++)
    arr_helper(x, size);
  std::cout << x[0] << '\n';
  return x;
}

void calloc_test(int size) {
  std::cout << "calloc_test\n";
  int *y = (int*)calloc(size, sizeof(int));
  cilk_for (int i = 0; i < 1000; i++)
    arr_helper(y, size);
  std::cout << y[0] << '\n';
  free(y);
}

int *realloc_test(int *x, int size) {
  std::cout << "realloc_test\n";
  x = (int*)realloc(x, size * sizeof(int));
  cilk_for (int i = 0; i < 1000; i++)
    arr_helper(x, size);
  std::cout << x[0] << '\n';
  return x;
}

void new_test() {
  std::cout << "new_test\n";
  Foo *x = new Foo();
  cilk_for (int i = 0; i < 1000; i++)
    x->getVal()--;

  cilk_for (int i = 0; i < 1000; i++)
    x->incVal();
  std::cout << "x->getVal() = " << x->getVal() << '\n';
  delete x;

  Bar *y = new Bar();
  cilk_for (int i = 0; i < 1000; i++)
    y->getVal(i % 4)--;

  cilk_for (int i = 0; i < 1000; i++)
    y->incVal(i % 4);
  std::cout << "y->getVal(0) = " << y->getVal(0) << '\n';
  delete y;

  Foo_st *z = new Foo_st();
  cilk_for (int i = 0; i < 1000; i++) {
    z->a++;
    z->b += z->a * i;
  }
  std::cout << "z->b = " << z->b << '\n';
  delete z;
}

int main(int argc, char** argv) {
  int arrsize = 1;
  if (argc == 2)
    arrsize = atoi(argv[1]);

  // Race 1
  global_test();

  // Race 2
  local_test();

  int parent_local = 2;
  // Race 3
  param_test(parent_local);

  // Race 4
  int *x = malloc_test(arrsize);

  // Race 5
  calloc_test(arrsize);

  // Race 6
  x = realloc_test(x, 5 * arrsize);
  free(x);

  // Redundant with race 4
  x = malloc_test(arrsize);
  free(x);
  
  // Races 7-12
  new_test();

  return 0;
}
