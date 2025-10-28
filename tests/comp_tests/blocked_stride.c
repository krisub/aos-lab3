#include <stdio.h>

#define PAGE_SIZE 4096
#define INT_PER_PAGE (PAGE_SIZE / sizeof(int)) // = 1024

// we will access 2 pages, then skip 1
#define PAGES_IN_CHUNK 2
#define PAGES_TO_SKIP 1
#define CHUNK_PAGE_COUNT (PAGES_IN_CHUNK + PAGES_TO_SKIP) // = 3 pages
#define CHUNK_INT_COUNT (CHUNK_PAGE_COUNT * INT_PER_PAGE) // = 3072 ints

#define NUM_CHUNKS (1024 * 32)
#define ARRAY_SIZE (NUM_CHUNKS * CHUNK_INT_COUNT)

// large uninitialized array = .bss
static int bss_array[ARRAY_SIZE];

int main() {
  printf("p1_win test - starting\n");

  long sum = 0;

  for (int i = 0; i < NUM_CHUNKS; i++) {
    int base_index = i * CHUNK_INT_COUNT;

    // access page 0 of the chunk
    bss_array[base_index] = i;
    sum += i;

    // access page 1 of the chunk
    bss_array[base_index + INT_PER_PAGE] = i;
    sum += i;

    // skip page 2 (index: base_index + 2 * INT_PER_PAGE)
  }

  printf("p1_win test - finished with sum %ld\n", sum);
  return 0;
}