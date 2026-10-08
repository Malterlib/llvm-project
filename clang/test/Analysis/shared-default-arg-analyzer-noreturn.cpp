// RUN: %clang_analyze_cc1 -std=c++20 -analyzer-checker=core -verify %s
// expected-no-diagnostics

// A temporary of a shared default argument whose destructor is
// `analyzer_noreturn` ends the path, also where the chain of defaults is long
// enough that the CFG leaves out temporaries that don't matter.
struct Stop {
  __attribute__((analyzer_noreturn)) ~Stop();
};
struct Holder {
  Stop m_Stop;
};
extern bool b;
int f0(Stop const & = Stop{});
int g0(Holder const & = Holder{});

#define LINK(F, N, P) int F##N(int = b ? F##P() : F##P());
#define CHAIN(F)                                                              \
  LINK(F, 1, 0) LINK(F, 2, 1) LINK(F, 3, 2) LINK(F, 4, 3) LINK(F, 5, 4)       \
  LINK(F, 6, 5) LINK(F, 7, 6) LINK(F, 8, 7) LINK(F, 9, 8) LINK(F, 10, 9)      \
  LINK(F, 11, 10) LINK(F, 12, 11) LINK(F, 13, 12) LINK(F, 14, 13)
CHAIN(f)
CHAIN(g)

void test() {
  f14();
  int *p = nullptr;
  *p = 1;
}

void testMember() {
  g14();
  int *p = nullptr;
  *p = 1;
}

// Also one whose destructor only does something the analyzer tracks: past the
// limit, the CFG isn't built for it, rather than built without it.
extern const bool choose;
int value;
struct SetOnExit {
  ~SetOnExit() { value = 1; }
};
int e0(const SetOnExit & = {}) { return 0; }
#define DEFINED(N, P) int e##N(int n = choose ? e##P() : e##P()) { return n; }
DEFINED(1, 0) DEFINED(2, 1) DEFINED(3, 2) DEFINED(4, 3) DEFINED(5, 4)
DEFINED(6, 5) DEFINED(7, 6) DEFINED(8, 7) DEFINED(9, 8) DEFINED(10, 9)
DEFINED(11, 10) DEFINED(12, 11) DEFINED(13, 12) DEFINED(14, 13)

void testEffect() {
  if (choose)
    return;
  value = 0;
  e14();
  (void)(1 / value);
}
