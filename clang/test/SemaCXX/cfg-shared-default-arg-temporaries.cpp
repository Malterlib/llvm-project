// RUN: %clang_cc1 -fsyntax-only -std=c++20 -Wreturn-type -verify %s

// The default argument of `f0` makes a temporary, and each function in a
// chain uses the one before it through either value of a conditional, so it
// is reached through a number of paths that doubles with each link, each of
// which destroys the temporary. Up to 4096 of them, the destructors are all
// added to the CFG; past that it isn't built, rather than built without some
// of them, which would leave paths past a destructor that doesn't return.

struct NoReturn {
  [[noreturn]] ~NoReturn();
};
struct Returns {
  ~Returns();
};
extern bool b;

int f0(NoReturn const & = NoReturn{});
#define LINK(N, P) int f##N(int = b ? f##P() : f##P());
LINK(1, 0) LINK(2, 1) LINK(3, 2) LINK(4, 3) LINK(5, 4) LINK(6, 5) LINK(7, 6)
LINK(8, 7) LINK(9, 8) LINK(10, 9) LINK(11, 10) LINK(12, 11) LINK(13, 12)
LINK(14, 13) LINK(15, 14) LINK(16, 15) LINK(17, 16) LINK(18, 17) LINK(19, 18)
LINK(20, 19) LINK(21, 20) LINK(22, 21) LINK(23, 22) LINK(24, 23) LINK(25, 24)
LINK(26, 25) LINK(27, 26) LINK(28, 27) LINK(29, 28) LINK(30, 29)
#undef LINK

int built() { f12(); }
int beyond() { f13(); }
int far() { f30(); }

int g0(Returns const & = Returns{});
#define LINK(N, P) int g##N(int = b ? g##P() : g##P());
LINK(1, 0) LINK(2, 1) LINK(3, 2) LINK(4, 3) LINK(5, 4) LINK(6, 5) LINK(7, 6)
LINK(8, 7) LINK(9, 8) LINK(10, 9) LINK(11, 10) LINK(12, 11)
#undef LINK

int returns() { g12(); } // expected-warning {{non-void function does not return a value}}

// A temporary that isn't evaluated, `sizeof(Returns{})`, isn't destroyed:
// such a default argument isn't visited for it, however many paths lead to
// it, and the CFG is built.
int h0(int = sizeof(Returns{}));
#define LINK(N, P) int h##N(int = b ? h##P() : h##P());
LINK(1, 0) LINK(2, 1) LINK(3, 2) LINK(4, 3) LINK(5, 4) LINK(6, 5) LINK(7, 6)
LINK(8, 7) LINK(9, 8) LINK(10, 9) LINK(11, 10) LINK(12, 11) LINK(13, 12)
#undef LINK

Returns sink(int);
int unevaluated() { sink(h13()); } // expected-warning {{non-void function does not return a value}}
