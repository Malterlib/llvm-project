// RUN: %clang_cc1 -std=c++20 -fsyntax-only -Wuninitialized -verify %s

// Whether a record is empty is decided once for each record, not for each
// field of its type: a record of many fields of the record before, which is
// empty in the end, is decided in time linear in the length of the chain, not
// exponential.

template <int N> struct TCEmpty {
  TCEmpty<N - 1> m_A, m_B, m_C, m_D, m_E, m_F, m_G, m_H, m_I, m_J, m_K, m_L,
      m_M, m_N, m_O, m_P;
};
template <> struct TCEmpty<0> {};

TCEmpty<12> fg_Empty() {
  TCEmpty<12> Value;
  return Value;
}

int fg_Full() {
  int Value; // expected-note {{initialize the variable 'Value' to silence this warning}}
  return Value; // expected-warning {{variable 'Value' is uninitialized when used here}}
}
