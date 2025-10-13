#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAGIC 0xCC

// og malloc
static void *(*real_malloc)(size_t) = NULL;

// my malloc
void *malloc(size_t size) {
  if (!real_malloc) { // get og malloc
    real_malloc = dlsym(RTLD_NEXT, "malloc");
    if (!real_malloc) {
      fprintf(stderr, "error in dlsym: %s\n", dlerror());
      exit(1);
    }
  }

  fprintf(stderr, "malloc called with size: %zu\n", size);
  void *p = real_malloc(size);
  fprintf(stderr, "malloc returned: %p\n", p);
  memset(p, MAGIC, size);
  return p;
}