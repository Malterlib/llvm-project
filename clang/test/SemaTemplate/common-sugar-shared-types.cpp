// RUN: %clang_cc1 -std=c++20 -fsyntax-only -verify %s
// expected-no-diagnostics

// The common sugar of two types spelled differently, like the deduction of a
// template argument from both, or the result of a conditional of both, is
// found once for each pair of types they name, also one that a type names
// twice, like the parameters and result of `CPrevious (*)(CPrevious,
// CPrevious)`, not once for each path to it, which is exponential in the
// depth of such types.

template <int t_N, int t_Tag> struct TCType {
  using CPrevious = typename TCType<t_N - 1, t_Tag>::CType;
  using CType = CPrevious (*)(CPrevious, CPrevious);
};
template <int t_Tag> struct TCType<0, t_Tag> {
  using CType = int;
};

template <class T> void fg_Consume(T, T);

void fg_Test(TCType<40, 0>::CType _pFirst, TCType<40, 1>::CType _pSecond,
             bool _bFirst) {
  fg_Consume(_pFirst, _pSecond);
  auto pEither = _bFirst ? _pFirst : _pSecond;
  static_assert(__is_same(decltype(pEither), TCType<40, 0>::CType));
}
