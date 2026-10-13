// Only run this test where ulimit is known to work well.
// (There's nothing really platform-specific being tested, this is just ulimit).
//
// REQUIRES: system-linux
// UNSUPPORTED: msan
// UNSUPPORTED: asan
// UNSUPPORTED: hwasan
//
// RUN: ulimit -v 1048576
// RUN: %clang_cc1 -std=c++20 -fsyntax-only -verify %s
// expected-no-diagnostics

// The matches of types the deduction of template arguments keeps, to match
// a type that names another twice once, keep the deduced arguments of the
// template parameters the type of the parameter uses, not all of them, nor
// one of a parameter that is a template parameter itself: memory linear in
// the number of parameters, not quadratic.

#define TEN(X) X, X, X, X, X, X, X, X, X, X
#define THOUSAND(X) TEN(TEN(TEN(X)))
#define TWELVE_THOUSAND(X)                                                     \
  THOUSAND(X), THOUSAND(X), THOUSAND(X), THOUSAND(X), THOUSAND(X),             \
      THOUSAND(X), THOUSAND(X), THOUSAND(X), THOUSAND(X), THOUSAND(X),         \
      THOUSAND(X), THOUSAND(X)

void fg_Use(TWELVE_THOUSAND(auto));
void fg_Test() { fg_Use(TWELVE_THOUSAND(0)); }

void fg_UsePointers(TWELVE_THOUSAND(auto *));
void fg_TestPointers(int *_pValue) { fg_UsePointers(TWELVE_THOUSAND(_pValue)); }
