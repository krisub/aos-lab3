#include <stdio.h>
#define SIZE (1024 * 100)

int arr1[SIZE] = {1, [SIZE - 1] = 1};         // first and last are 1
char arr2[SIZE] = {'A', [SIZE - 1] = 'Z'};    // first is 'A', last is 'Z'
double arr3[SIZE] = {1.0, [SIZE - 1] = 3.14}; // first is 1.0, last is 3.14

long sum_arr1() {
  long sum = 0;
  for (int i = 0; i < SIZE; i++) {
    sum += arr1[i] * i;
  }
  return sum;
}

int count_arr2() {
  int count = 0;
  for (int i = 0; i < SIZE; i++) {
    if (arr2[i] >= 'A' && arr2[i] <= 'Z') {
      count++;
    }
  }
  return count;
}

double mult_arr3() {
  double product = 1.0;
  for (int i = SIZE - 1; i >= 0; i -= 100) {
    if (arr3[i] > 0.0)
      product *= arr3[i];
    if (product < 1e-10 || product > 1e10)
      product = 1.0;
  }
  return product;
}

int main() {
  printf("dense access test (statically linked) - starting\n");

  for (int i = 0; i < 10000; i++) {
    long r1 = sum_arr1();
    int r2 = count_arr2();
    double r3 = mult_arr3();
    printf("results: %ld, %d, %f\n", r1, r2, r3);
  }

  printf("dense access test (statically linked) - finished\n");
  return 0;
}