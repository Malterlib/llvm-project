// RUN: %clang_cc1 -std=c++20 -fsyntax-only -Wuninitialized -verify %s

// A default argument with a temporary that is destroyed, used through a chain
// of conditionals, is reached through a number of paths that doubles with
// each link: past the limit of the destructors the CFG keeps, those of the
// rest are left out, and the analyses of the function still run.

extern bool b;
struct D {
  ~D();
};
int f0(D const & = D{});

#define STEP(N, P) int f##N(int = b ? f##P() : f##P());
STEP(1, 0) STEP(2, 1) STEP(3, 2) STEP(4, 3) STEP(5, 4) STEP(6, 5) STEP(7, 6)
STEP(8, 7) STEP(9, 8) STEP(10, 9) STEP(11, 10) STEP(12, 11) STEP(13, 12)
STEP(14, 13) STEP(15, 14) STEP(16, 15) STEP(17, 16) STEP(18, 17) STEP(19, 18)
STEP(20, 19) STEP(21, 20) STEP(22, 21) STEP(23, 22) STEP(24, 23) STEP(25, 24)
STEP(26, 25) STEP(27, 26) STEP(28, 27) STEP(29, 28) STEP(30, 29) STEP(31, 30)
STEP(32, 31) STEP(33, 32) STEP(34, 33) STEP(35, 34) STEP(36, 35) STEP(37, 36)
STEP(38, 37) STEP(39, 38) STEP(40, 39)

int test() {
  int x; // expected-note {{initialize the variable 'x' to silence this warning}}
  f40();
  return x; // expected-warning {{variable 'x' is uninitialized when used here}}
}
