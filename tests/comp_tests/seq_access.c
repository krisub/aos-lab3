#include <stdio.h>

#define PAGE_SIZE 4096
#define INT_PER_PAGE (PAGE_SIZE / sizeof(int))
#define NUM_PAGES (1024 * 32) // 32K pages
#define ARRAY_SIZE (NUM_PAGES * INT_PER_PAGE)

// large uninitialized array = .bss
static int bss_array[ARRAY_SIZE];

int main() {
  printf("p2_win test (seq access) - starting\n");

  long sum = 0;

  // access every single element sequentially
  for (int i = 0; i < ARRAY_SIZE; i++) {
    bss_array[i] = i;
    if (i % 100000 == 0)
      sum += bss_array[i]; // just to use the value
  }

  printf("p2_win test - finished with sum %ld\n", sum);
  return 0;
}