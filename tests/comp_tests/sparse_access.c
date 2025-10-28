#include <stdio.h>

#define PAGE_SIZE 4096
#define PAGE_STEP 10    // access 1 page every 10 pages
#define NUM_PAGES 10240 // 10k pages
#define ARRAY_SIZE (NUM_PAGES * PAGE_STEP * (PAGE_SIZE / sizeof(int)))

// large uninitialized array = .bss
static int bss_array[ARRAY_SIZE];

int main() {
  printf("sparse access test - starting\n");

  long sum = 0;
  int page_int_step = PAGE_SIZE / sizeof(int);

  for (int i = 0; i < NUM_PAGES; i++) {
    // access one element on a page, then jump 10 pages (to punish the p1 and p2 loaders)
    int index = i * page_int_step * PAGE_STEP;
    bss_array[index] = i;
    sum += i;
  }

  printf("sparse access test - finished with sum %ld\n", sum);
  return 0;
}