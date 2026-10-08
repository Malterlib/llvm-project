// RUN: %clang_cc1 -std=c++20 -fsyntax-only -Wno-integer-overflow -verify %s

// The check of an expression for overflow is made also where the warning is
// off, as a pragma can turn it on where a default argument it evaluates is.

constexpr int g();
#pragma clang diagnostic push
#pragma clang diagnostic warning "-Winteger-overflow"
constexpr int f(int n = g() + 1) { return n; } // expected-warning {{overflow in expression; result is -2'147'483'648 with type 'int'}}
#pragma clang diagnostic pop
constexpr int g() { return __INT_MAX__; }
int test() { return f() + 1; }
