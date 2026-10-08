// RUN: %clang_cc1 -std=c++20 -fsyntax-only -Wunreachable-code -verify %s

// The CFG leaves out the destructors of a use of a default argument it
// reaches more than 4096 times, and isn't built where one of them doesn't
// return. Separate uses in the source, each reached once, are all added,
// however many: the destructor that doesn't return ends the flow, and what
// follows is never executed.

struct Stop {
  [[noreturn]] ~Stop();
};
int consume(Stop = {});

#define R4(...) __VA_ARGS__, __VA_ARGS__, __VA_ARGS__, __VA_ARGS__
#define R16(...) R4(R4(__VA_ARGS__))
#define R256(...) R16(R16(__VA_ARGS__))
#define R4096(...) R256(R16(__VA_ARGS__))

void test() {
  int values[] = {R4096(consume()), consume()};
  int reached = 1; // expected-warning {{code will never be executed}}
}
