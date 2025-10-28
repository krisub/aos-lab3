#include <math.h>   // Library 2 (libm)
#include <stdio.h>  // Library 1 (libc)
#include <stdlib.h> // Library 1 (libc)
#include <string.h> // Library 1 (libc)
#include <time.h>   // Library 1 (libc)

#define BUFFER_SIZE 50

int main() {
  // 1. printf (libc)
  printf("Starting lib test.\n");

  // 2. time (libc)
  // 3. srand (libc)
  srand(time(NULL));

  // 4. rand (libc)
  int r = rand() % 100;

  // 5. malloc (libc)
  char *buffer = (char *)malloc(BUFFER_SIZE * sizeof(char));
  if (buffer == NULL) {
    // 6. fprintf (libc) - (to stderr)
    fprintf(stderr, "Test failed: malloc returned NULL\n");
    return 1;
  }
  printf("malloc() succeeded.\n");

  // 7. strcpy (libc)
  strcpy(buffer, "Hello from the test program!");

  // 8. strlen (libc)
  size_t len = strlen(buffer);
  printf("String length: %zu\n", len);

  // Perform math operations (libm)
  double val = (double)r;
  // 9. sin (libm)
  double s = sin(val);
  // 10. cos (libm)
  double c = cos(val);
  // 11. sqrt (libm)
  double sq = sqrt(val);
  // 12. pow (libm)
  double p = pow(val, 2.0);

  printf("Math results: sin(r)=%f, cos(r)=%f, sqrt(r)=%f, pow(r,2)=%f\n", s, c,
         sq, p);

  // Perform file operations (libc)
  // 13. fopen (libc)
  FILE *f = fopen("/tmp/test_output.txt", "w");
  if (f) {
    fprintf(f, "Test succeeded with value %d\n", r);
    printf("Successfully wrote to /tmp/test_output.txt\n");
    // 14. fclose (libc)
    fclose(f);
  } else {
    fprintf(stderr, "Could not open /tmp/test_output.txt\n");
  }

  // 15. free (libc)
  free(buffer);

  return 0;
}