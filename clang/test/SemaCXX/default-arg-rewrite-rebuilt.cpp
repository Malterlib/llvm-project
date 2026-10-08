// RUN: %clang_cc1 -std=c++20 -fsyntax-only -fcxx-exceptions -Wno-vla-cxx-extension -verify %s

// A call with a default argument rewritten for where it is used is rewritten
// again where Sema builds the call again: with the location of its own use,
// where an immediate invocation in it is not a constant expression, and as
// potentially evaluated where it is built again so, like in the size of an
// array of variable length in sizeof.

consteval int ce(int v) {
  if (v == 23)
    throw 0; // expected-note {{subexpression not valid in a constant expression}}
  return v;
}
consteval int id(int x) { return x; }
constexpr int pf(int v = ce(__builtin_LINE())) { return v; } // expected-note {{in call to 'ce(23)'}}
void use(int);

void first() {
  use(id(pf()));
  use(id(1));
}
void second() {
  use(id(pf())); // expected-error {{call to consteval function 'id' is not a constant expression}}
  use(id(1));
}

consteval int one(int v) { return v; }
int f0(int x = one(1)) { return x; }
int u() { return sizeof(char[f0()]); }
