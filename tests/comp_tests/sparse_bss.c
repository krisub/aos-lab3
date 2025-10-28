#include <stdio.h>
#include <stdlib.h>

// 40MB array, uninitialized, guarantees it's in .bss
// 40MB / 4KB (pagesize) = 10240 pages
#define PAGE_SIZE 4096
#define ARRAY_SIZE (1024 * 1024 * 10)
static int bss_array[ARRAY_SIZE];

int main() {
  printf("sparse bss test (statically linked) - starting\n");

  long sum = 0;
  int step = PAGE_SIZE / sizeof(int);     // = 1024
  int pages_to_touch = ARRAY_SIZE / step; // = 10240

  for (int i = 0; i < pages_to_touch; i++) {
    // access one element per page
    bss_array[i * step] = i;
    sum += i;
  }

  printf("sparse bss test (statically linked) - finished with sum %ld\n", sum);
  return 0;
}