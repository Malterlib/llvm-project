// RUN: %clang_cc1 -std=c++20 -fsyntax-only -x c++ -fbracket-depth 2048 -ftemplate-depth=2048 -verify %s
// expected-no-diagnostics

// A match of a chain of types that each name the next once, matched once, is
// kept for a match of the same types only when it succeeds again: finding
// the template parameters each type of the chain uses would go through the
// rest of the chain, a time quadratic in its length.

template <class A, class B> struct Pair {};
#define R2(...) __VA_ARGS__ __VA_ARGS__
#define R4(...) R2(R2(__VA_ARGS__))
#define R16(...) R4(R4(__VA_ARGS__))
#define R256(...) R16(R16(__VA_ARGS__))
#define R1024(...) R256(R4(__VA_ARGS__))
#define OPEN(T) Pair<T,
#define CLOSE >

template <class T> void f(R1024(OPEN(T)) int R1024(CLOSE) *);
using Arg = R1024(OPEN(int)) int R1024(CLOSE);
#define CALL f((Arg *)nullptr);
void g() { R256(CALL) }
