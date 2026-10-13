// RUN: %clang_cc1 -std=c++20 -fsyntax-only -verify %s
// expected-no-diagnostics

// The classes and namespaces associated with the type of an argument of an
// unqualified call are found from each type once, also one that a type names
// twice, like the arguments of a specialization `TCTypeList<CPrevious,
// CPrevious>`, not once for each path to it, which is exponential in the depth
// of such types.

namespace NMeta {
template <class... tp_CTypes> struct TCTypeList {};
template <class t_CType> struct TCMember {};
} // namespace NMeta

template <int t_N> struct TCLevel {
  using CPrevious = typename TCLevel<t_N - 1>::CType;
  using CType = NMeta::TCTypeList<CPrevious, CPrevious>;
};
template <> struct TCLevel<0> {
  using CType = NMeta::TCTypeList<>;
};

void fg_Consume(TCLevel<64>::CType const &);
void fg_Test(TCLevel<64>::CType const &_Value) { fg_Consume(_Value); }

// Also of the parameters and result of a function type.
template <int t_N> struct TCFunction {
  using CPrevious = typename TCFunction<t_N - 1>::CType;
  using CType = CPrevious (*)(CPrevious, CPrevious);
};
template <> struct TCFunction<0> {
  using CType = NMeta::TCMember<int>;
};

void fg_ConsumeFunction(TCFunction<64>::CType);
void fg_TestFunction(TCFunction<64>::CType _pFunction) {
  fg_ConsumeFunction(_pFunction);
}

// The associated namespace of the innermost argument is still found.
namespace NMeta {
void fg_Found(TCLevel<64>::CType const &);
void fg_FoundFunction(TCFunction<64>::CType);
} // namespace NMeta
void fg_TestFound(TCLevel<64>::CType const &_Value,
                  TCFunction<64>::CType _pFunction) {
  fg_Found(_Value);
  fg_FoundFunction(_pFunction);
}
