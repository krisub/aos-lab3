#include <stdio.h>
#include <stdlib.h>

int main() {
  printf("simple test is running!\\n");
  void *mem = malloc(64);
  if (mem != NULL) {
    printf("malloc(64) succeeded!\\n");
    free(mem);
  } else {
    printf("malloc(64) failed...\\n");
  }
  return 0;
}