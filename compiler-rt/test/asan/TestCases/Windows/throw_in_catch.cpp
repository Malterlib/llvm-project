// RUN: %clangxx_asan -O2 %s -o %t
// RUN: %run %t | FileCheck %s

// A new exception thrown by a callee while control is inside a catch handler
// makes the MSVC C++ EH runtime recover the frame's EH state through the
// funclet's parent-frame link, which the ASan fake stack must not break.

#include <cstdio>

char *g_p;

__declspec(noinline) void thrower(int v) {
  if (v >= 0)
    throw 42;
}

__declspec(noinline) int outer(int v) {
  // Interesting alloca so ASan considers this frame for the fake stack.
  char buf[128];
  std::snprintf(buf, sizeof buf, "value %d", v);
  g_p = buf;

  int caught = 0;
  try {
    thrower(v);
  } catch (int) {
    for (int i = 0; i < 3; ++i) {
      try {
        thrower(v + i);
      } catch (int) {
        ++caught;
      }
    }
  }
  return caught;
}

int main() {
  std::printf("caught=%d\n", outer(1));
// CHECK: caught=3
  return 0;
}
