#include <cilk/cilk.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

void fill(unsigned char *data, unsigned length, unsigned char seed)
{
  for (unsigned i = 0; i < length; ++i)
    data[i] = seed + i;
}

unsigned check(unsigned char *data, unsigned length, unsigned char seed)
{
  fprintf(stdout, "check %p seed %d\n", data, seed);
  for (unsigned i = 0; i < length; ++i)
    if (data[i] != (unsigned char)(seed + i))
      {
        fprintf(stdout, "check %p seed %d [%u] %d != %d\n", data, seed, i,
                data[i], (unsigned char)(seed + i));
        return 1;
      }
  return 0;
}

unsigned work(unsigned char *data, unsigned length, unsigned char seed)
{
  fprintf(stdout, "work %p seed %d\n", data, seed);
  fflush(stdout);
  fill(data, length, seed);
  usleep(100);
  return check(data, length, seed);
}

unsigned loop(unsigned length)
{
  unsigned errors[length];
  memset(errors, 0, sizeof errors);
  for (unsigned int i = 0; i < 100; ++i)
    {
      unsigned char vla[length];
      cilk_spawn errors[i] = work(vla, length, i);
    }
  cilk_sync;
  unsigned sum = 0;
  for (unsigned int i = 0; i < 100; ++i)
    sum += errors[i];
  return sum;
}

int main(int argc, char *argv[])
{
  unsigned length = 0;
  if (argc > 1)
    length = strtoul(argv[1], 0, 0);
  if (length == 0)
    length = 399;
  unsigned errors = loop(length);
  return errors != 0;
}
