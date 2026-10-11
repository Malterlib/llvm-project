// RUN: %clang_cc1 -std=c++20 -fsyntax-only -verify %s

// A conversion function of a virtual base is hidden by one of a class above it
// on a path that reaches that base, but not by one above another subobject of
// the same class, which isn't the virtual one.
struct A { operator int() const; }; // expected-note 2 {{candidate function}}
struct B : A { operator int() const; }; // expected-note 2 {{candidate function}}
struct C : virtual A {};
struct D : B, C {};
int f(D const &d) { return d; } // expected-error {{conversion from 'const D' to 'int' is ambiguous}}

struct V : A {};
struct E : B, virtual V {};
int g(E const &e) { return e; } // expected-error {{conversion from 'const E' to 'int' is ambiguous}}

// One hidden on any path that reaches the virtual base is hidden.
struct F : virtual A { operator int() const; };
struct G : F, C {};
int h(G const &g) { return g; }
