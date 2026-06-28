// RUN: %clangxx_asan -O0 %s -o %t
// RUN: %run %t | FileCheck %s

#include <cstdio>

int main() {
  int caught = 0;

  try {
    try {
      throw int(30);
    } catch (int) {
      ++caught;
      throw;
    }
  } catch (int) {
    ++caught;
  }

  std::printf("caught=%d\n", caught);
// CHECK: caught=2
  return caught == 2 ? 0 : 1;
}
