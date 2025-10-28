#include <stdio.h>

// 400MB array, initialized, to force it into .data
// 400MB / 4KB (pagesize) = 102400 pages
#define ARRAY_SIZE (1024 * 1024 * 100)
static int large_data_array[ARRAY_SIZE] = {1};

int main() {
  printf("large data test (statically linked) - starting and exiting\n");

  // dont access the array just exit

  // compiler will optimize the array away if we don't use it at all
  if (large_data_array[0] == 99) {
    printf("this should not happen");
  }

  printf("large data test - finished\n");
  return 0;
}