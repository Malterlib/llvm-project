// RUN: %clang_cc1 -std=c++20 -fsyntax-only -verify %s
// RUN: %clang_cc1 -std=c++20 -fsyntax-only -Wc++98-compat-local-type-template-args -verify=compat %s

// The check of a template argument for a type that is local, unnamed or has
// no linkage goes through each type it is compounded from once, also one that
// a type names twice, like the parameters of `void(CPrevious, CPrevious)`,
// not once for each path to it, which is exponential in the depth of such
// types.

template <unsigned t_N> struct TCTypes {
  using CPrevious = typename TCTypes<t_N - 1>::CType;
  using CType = void(CPrevious, CPrevious);
};
template <> struct TCTypes<0> {
  using CType = int;
};

template <class t_CSignature> struct TCFunction {};
// expected-no-diagnostics
using CCallback = TCFunction<TCTypes<64>::CType>;

// Each such type is still found, also after the many paths to those that
// aren't.
void fg_Local() {
  struct CLocal {};
  TCFunction<void(TCTypes<64>::CType *, CLocal)> Function; // compat-warning {{local type 'CLocal' as template argument is incompatible with C++98}}
  (void)Function;
}
