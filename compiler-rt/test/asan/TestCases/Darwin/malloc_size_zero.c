// RUN: %clang_asan %s -o %t
// RUN: %run %t 2>&1 | FileCheck %s

#include <malloc/malloc.h>
#include <stdio.h>
#include <stdlib.h>

int main(void) {
  void *p = malloc(0);
  size_t size = malloc_size(p);
  fprintf(stderr, "size = %zu\n", size);
  // CHECK: size = 0
  free(p);
  return 0;
}
