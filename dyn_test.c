#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define SIZE 64

#define MAGIC 0xCC // (can be any value you want)

int main() {
  printf("(dynamic linking) this is my test executable! :)\n");
  unsigned char *mem = malloc(SIZE);
  for (int i = 0; i < SIZE; i++)
    assert(mem[i] == MAGIC);
}