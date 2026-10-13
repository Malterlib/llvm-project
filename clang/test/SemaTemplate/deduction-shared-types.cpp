// RUN: %clang_cc1 -std=c++20 -fsyntax-only -verify %s

// The deduction of template arguments from a type that names another twice,
// like the arguments of `TCTypeList<CPrevious, CPrevious>` or the parameters
// and result of `CPrevious (*)(CPrevious, CPrevious)`, matches each pair of
// types once from what it deduced, and marks the template parameters each
// type uses once, not once for each path to it, which is exponential in the
// depth of such types.

template <class... tp_CTypes> struct TCTypeList {};
template <class T> using TCList0 = T;

template <class T> using TCList1 = TCTypeList<TCList0<T>, TCList0<T>>;
template <class T> using TCList2 = TCTypeList<TCList1<T>, TCList1<T>>;
template <class T> using TCList3 = TCTypeList<TCList2<T>, TCList2<T>>;
template <class T> using TCList4 = TCTypeList<TCList3<T>, TCList3<T>>;
template <class T> using TCList5 = TCTypeList<TCList4<T>, TCList4<T>>;
template <class T> using TCList6 = TCTypeList<TCList5<T>, TCList5<T>>;
template <class T> using TCList7 = TCTypeList<TCList6<T>, TCList6<T>>;
template <class T> using TCList8 = TCTypeList<TCList7<T>, TCList7<T>>;
template <class T> using TCList9 = TCTypeList<TCList8<T>, TCList8<T>>;
template <class T> using TCList10 = TCTypeList<TCList9<T>, TCList9<T>>;
template <class T> using TCList11 = TCTypeList<TCList10<T>, TCList10<T>>;
template <class T> using TCList12 = TCTypeList<TCList11<T>, TCList11<T>>;
template <class T> using TCList13 = TCTypeList<TCList12<T>, TCList12<T>>;
template <class T> using TCList14 = TCTypeList<TCList13<T>, TCList13<T>>;
template <class T> using TCList15 = TCTypeList<TCList14<T>, TCList14<T>>;
template <class T> using TCList16 = TCTypeList<TCList15<T>, TCList15<T>>;
template <class T> using TCList17 = TCTypeList<TCList16<T>, TCList16<T>>;
template <class T> using TCList18 = TCTypeList<TCList17<T>, TCList17<T>>;
template <class T> using TCList19 = TCTypeList<TCList18<T>, TCList18<T>>;
template <class T> using TCList20 = TCTypeList<TCList19<T>, TCList19<T>>;
template <class T> using TCList21 = TCTypeList<TCList20<T>, TCList20<T>>;
template <class T> using TCList22 = TCTypeList<TCList21<T>, TCList21<T>>;
template <class T> using TCList23 = TCTypeList<TCList22<T>, TCList22<T>>;
template <class T> using TCList24 = TCTypeList<TCList23<T>, TCList23<T>>;
template <class T> using TCList25 = TCTypeList<TCList24<T>, TCList24<T>>;
template <class T> using TCList26 = TCTypeList<TCList25<T>, TCList25<T>>;
template <class T> using TCList27 = TCTypeList<TCList26<T>, TCList26<T>>;
template <class T> using TCList28 = TCTypeList<TCList27<T>, TCList27<T>>;
template <class T> using TCList29 = TCTypeList<TCList28<T>, TCList28<T>>;
template <class T> using TCList30 = TCTypeList<TCList29<T>, TCList29<T>>;
template <class T> using TCList31 = TCTypeList<TCList30<T>, TCList30<T>>;
template <class T> using TCList32 = TCTypeList<TCList31<T>, TCList31<T>>;
template <class T> using TCList33 = TCTypeList<TCList32<T>, TCList32<T>>;
template <class T> using TCList34 = TCTypeList<TCList33<T>, TCList33<T>>;
template <class T> using TCList35 = TCTypeList<TCList34<T>, TCList34<T>>;
template <class T> using TCList36 = TCTypeList<TCList35<T>, TCList35<T>>;
template <class T> using TCList37 = TCTypeList<TCList36<T>, TCList36<T>>;
template <class T> using TCList38 = TCTypeList<TCList37<T>, TCList37<T>>;
template <class T> using TCList39 = TCTypeList<TCList38<T>, TCList38<T>>;
template <class T> using TCList40 = TCTypeList<TCList39<T>, TCList39<T>>;
template <class T> void fg_UseList(TCList40<T> *);
void fg_TestList(TCList40<int> *_pValue) { fg_UseList(_pValue); }

template <class T> using TCFunction0 = T;
template <class T> using TCFunction1 = TCFunction0<T> (*)(TCFunction0<T>, TCFunction0<T>);
template <class T> using TCFunction2 = TCFunction1<T> (*)(TCFunction1<T>, TCFunction1<T>);
template <class T> using TCFunction3 = TCFunction2<T> (*)(TCFunction2<T>, TCFunction2<T>);
template <class T> using TCFunction4 = TCFunction3<T> (*)(TCFunction3<T>, TCFunction3<T>);
template <class T> using TCFunction5 = TCFunction4<T> (*)(TCFunction4<T>, TCFunction4<T>);
template <class T> using TCFunction6 = TCFunction5<T> (*)(TCFunction5<T>, TCFunction5<T>);
template <class T> using TCFunction7 = TCFunction6<T> (*)(TCFunction6<T>, TCFunction6<T>);
template <class T> using TCFunction8 = TCFunction7<T> (*)(TCFunction7<T>, TCFunction7<T>);
template <class T> using TCFunction9 = TCFunction8<T> (*)(TCFunction8<T>, TCFunction8<T>);
template <class T> using TCFunction10 = TCFunction9<T> (*)(TCFunction9<T>, TCFunction9<T>);
template <class T> using TCFunction11 = TCFunction10<T> (*)(TCFunction10<T>, TCFunction10<T>);
template <class T> using TCFunction12 = TCFunction11<T> (*)(TCFunction11<T>, TCFunction11<T>);
template <class T> using TCFunction13 = TCFunction12<T> (*)(TCFunction12<T>, TCFunction12<T>);
template <class T> using TCFunction14 = TCFunction13<T> (*)(TCFunction13<T>, TCFunction13<T>);
template <class T> using TCFunction15 = TCFunction14<T> (*)(TCFunction14<T>, TCFunction14<T>);
template <class T> using TCFunction16 = TCFunction15<T> (*)(TCFunction15<T>, TCFunction15<T>);
template <class T> using TCFunction17 = TCFunction16<T> (*)(TCFunction16<T>, TCFunction16<T>);
template <class T> using TCFunction18 = TCFunction17<T> (*)(TCFunction17<T>, TCFunction17<T>);
template <class T> using TCFunction19 = TCFunction18<T> (*)(TCFunction18<T>, TCFunction18<T>);
template <class T> using TCFunction20 = TCFunction19<T> (*)(TCFunction19<T>, TCFunction19<T>);
template <class T> using TCFunction21 = TCFunction20<T> (*)(TCFunction20<T>, TCFunction20<T>);
template <class T> using TCFunction22 = TCFunction21<T> (*)(TCFunction21<T>, TCFunction21<T>);
template <class T> using TCFunction23 = TCFunction22<T> (*)(TCFunction22<T>, TCFunction22<T>);
template <class T> using TCFunction24 = TCFunction23<T> (*)(TCFunction23<T>, TCFunction23<T>);
template <class T> using TCFunction25 = TCFunction24<T> (*)(TCFunction24<T>, TCFunction24<T>);
template <class T> using TCFunction26 = TCFunction25<T> (*)(TCFunction25<T>, TCFunction25<T>);
template <class T> using TCFunction27 = TCFunction26<T> (*)(TCFunction26<T>, TCFunction26<T>);
template <class T> using TCFunction28 = TCFunction27<T> (*)(TCFunction27<T>, TCFunction27<T>);
template <class T> using TCFunction29 = TCFunction28<T> (*)(TCFunction28<T>, TCFunction28<T>);
template <class T> using TCFunction30 = TCFunction29<T> (*)(TCFunction29<T>, TCFunction29<T>);
template <class T> using TCFunction31 = TCFunction30<T> (*)(TCFunction30<T>, TCFunction30<T>);
template <class T> using TCFunction32 = TCFunction31<T> (*)(TCFunction31<T>, TCFunction31<T>);
template <class T> using TCFunction33 = TCFunction32<T> (*)(TCFunction32<T>, TCFunction32<T>);
template <class T> using TCFunction34 = TCFunction33<T> (*)(TCFunction33<T>, TCFunction33<T>);
template <class T> using TCFunction35 = TCFunction34<T> (*)(TCFunction34<T>, TCFunction34<T>);
template <class T> using TCFunction36 = TCFunction35<T> (*)(TCFunction35<T>, TCFunction35<T>);
template <class T> using TCFunction37 = TCFunction36<T> (*)(TCFunction36<T>, TCFunction36<T>);
template <class T> using TCFunction38 = TCFunction37<T> (*)(TCFunction37<T>, TCFunction37<T>);
template <class T> using TCFunction39 = TCFunction38<T> (*)(TCFunction38<T>, TCFunction38<T>);
template <class T> using TCFunction40 = TCFunction39<T> (*)(TCFunction39<T>, TCFunction39<T>);
template <class T> void fg_UseFunction(TCFunction40<T>);
void fg_TestFunction(TCFunction40<int> _pFunction) { fg_UseFunction(_pFunction); }

// The elements of a pack are each deduced, also the same type twice.
template <class... tp_CTypes>
constexpr unsigned fg_Count(TCTypeList<tp_CTypes...>) { return sizeof...(tp_CTypes); }
static_assert(fg_Count(TCTypeList<int, int>{}) == 2);
static_assert(fg_Count(TCTypeList<TCList2<int>, TCList2<int>, TCList2<int>>{}) == 3);

// And a parameter that two types deduce differently still fails.
template <class T> void fg_Same(TCTypeList<T, T>); // expected-note {{candidate template ignored: deduced conflicting types for parameter 'T' ('int' vs. 'long')}}
void fg_TestSame() { fg_Same(TCTypeList<int, long>{}); } // expected-error {{no matching function for call to 'fg_Same'}}

// A match that deduced a pack is matched again in an expansion of the same
// pack, which checks what it deduces against that expansion's: one kept from
// a match of the arguments of a class that failed, before its bases were
// tried, would deduce the conflicting packs of a base its arguments don't
// match, and choose a template that an ambiguous deduction rejects.
namespace NestedPacks {
template <class... X> struct TCTypeList {};
template <class A, class B> struct TCPair {};
template <class... X> struct W {};
using IL = TCTypeList<int, long>;
using LL = TCTypeList<long, long>;
using Good = TCTypeList<TCPair<int, IL>, TCPair<long, IL>>;
using Bad = TCTypeList<TCPair<int, IL>, TCPair<long, LL>>;
template <>
struct W<TCPair<long, LL>, TCPair<long, LL>, int>
    : W<TCPair<int, IL>, TCPair<long, IL>> {};
template <> struct TCPair<int, Bad> : TCPair<int, Good>, TCPair<char, Good> {};
template <class U, class... T>
char fg_Nested(W<TCPair<T, TCTypeList<T...>>...>,
               TCPair<U, TCTypeList<TCPair<T, TCTypeList<T...>>...>>);
long fg_Nested(...);
static_assert(sizeof(fg_Nested(W<TCPair<long, LL>, TCPair<long, LL>, int>{},
                               TCPair<int, Bad>{})) == sizeof(long));
}

// A match that deduces a pack in an expansion of another pack, which isn't
// what it is checked against, is matched once too.
namespace UnrelatedPack {
template <class... T> struct TCTypeList {};
template <class A, class B> struct TCPair {};
template <class... T> using TCList0 = TCTypeList<T...>;
template <class... T> using TCList1 = TCTypeList<TCList0<T...>, TCList0<T...>, TCList0<T...>, TCList0<T...>>;
template <class... T> using TCList2 = TCTypeList<TCList1<T...>, TCList1<T...>, TCList1<T...>, TCList1<T...>>;
template <class... T> using TCList3 = TCTypeList<TCList2<T...>, TCList2<T...>, TCList2<T...>, TCList2<T...>>;
template <class... T> using TCList4 = TCTypeList<TCList3<T...>, TCList3<T...>, TCList3<T...>, TCList3<T...>>;
template <class... T> using TCList5 = TCTypeList<TCList4<T...>, TCList4<T...>, TCList4<T...>, TCList4<T...>>;
template <class... T> using TCList6 = TCTypeList<TCList5<T...>, TCList5<T...>, TCList5<T...>, TCList5<T...>>;
template <class... T> using TCList7 = TCTypeList<TCList6<T...>, TCList6<T...>, TCList6<T...>, TCList6<T...>>;
template <class... T> using TCList8 = TCTypeList<TCList7<T...>, TCList7<T...>, TCList7<T...>, TCList7<T...>>;
template <class... T> using TCList9 = TCTypeList<TCList8<T...>, TCList8<T...>, TCList8<T...>, TCList8<T...>>;
template <class... T> using TCList10 = TCTypeList<TCList9<T...>, TCList9<T...>, TCList9<T...>, TCList9<T...>>;
template <class... T> using TCList11 = TCTypeList<TCList10<T...>, TCList10<T...>, TCList10<T...>, TCList10<T...>>;
template <class... T> using TCList12 = TCTypeList<TCList11<T...>, TCList11<T...>, TCList11<T...>, TCList11<T...>>;
template <class... T> using TCList13 = TCTypeList<TCList12<T...>, TCList12<T...>, TCList12<T...>, TCList12<T...>>;
template <class... T> using TCList14 = TCTypeList<TCList13<T...>, TCList13<T...>, TCList13<T...>, TCList13<T...>>;
template <class... T, class... U>
char fg_Unrelated(TCPair<U, TCList14<T...>> *...);
static_assert(sizeof(fg_Unrelated((TCPair<char, TCList14<int>> *)nullptr)) == 1);
}

// A match that deduces a pack in an expansion of the same pack, which checks
// what it deduces against what the other deductions in the expansion deduced
// and adds it, is matched once for each of those it is checked against, and
// still fails when they differ.
namespace SamePackChain {
template <class... T> struct TCTypeList {};
template <class A, class B> struct TCPair {};
template <class... T> using TCList0 = TCTypeList<T...>;
template <class... T> using TCList1 = TCPair<TCList0<T...>, TCList0<T...>>;
template <class... T> using TCList2 = TCPair<TCList1<T...>, TCList1<T...>>;
template <class... T> using TCList3 = TCPair<TCList2<T...>, TCList2<T...>>;
template <class... T> using TCList4 = TCPair<TCList3<T...>, TCList3<T...>>;
template <class... T> using TCList5 = TCPair<TCList4<T...>, TCList4<T...>>;
template <class... T> using TCList6 = TCPair<TCList5<T...>, TCList5<T...>>;
template <class... T> using TCList7 = TCPair<TCList6<T...>, TCList6<T...>>;
template <class... T> using TCList8 = TCPair<TCList7<T...>, TCList7<T...>>;
template <class... T> using TCList9 = TCPair<TCList8<T...>, TCList8<T...>>;
template <class... T> using TCList10 = TCPair<TCList9<T...>, TCList9<T...>>;
template <class... T> using TCList11 = TCPair<TCList10<T...>, TCList10<T...>>;
template <class... T> using TCList12 = TCPair<TCList11<T...>, TCList11<T...>>;
template <class... T> using TCList13 = TCPair<TCList12<T...>, TCList12<T...>>;
template <class... T> using TCList14 = TCPair<TCList13<T...>, TCList13<T...>>;
template <class... T> using TCList15 = TCPair<TCList14<T...>, TCList14<T...>>;
template <class... T> using TCList16 = TCPair<TCList15<T...>, TCList15<T...>>;
template <class... T> using TCList17 = TCPair<TCList16<T...>, TCList16<T...>>;
template <class... T> using TCList18 = TCPair<TCList17<T...>, TCList17<T...>>;
template <class... T> using TCList19 = TCPair<TCList18<T...>, TCList18<T...>>;
template <class... T> using TCList20 = TCPair<TCList19<T...>, TCList19<T...>>;
template <class... T> using TCList21 = TCPair<TCList20<T...>, TCList20<T...>>;
template <class... T> using TCList22 = TCPair<TCList21<T...>, TCList21<T...>>;
template <class... T> using TCList23 = TCPair<TCList22<T...>, TCList22<T...>>;
template <class... T> using TCList24 = TCPair<TCList23<T...>, TCList23<T...>>;
template <class... T> using TCList25 = TCPair<TCList24<T...>, TCList24<T...>>;
template <class... T> using TCList26 = TCPair<TCList25<T...>, TCList25<T...>>;
template <class... T> using TCList27 = TCPair<TCList26<T...>, TCList26<T...>>;
template <class... T> using TCList28 = TCPair<TCList27<T...>, TCList27<T...>>;
template <class... T> using TCList29 = TCPair<TCList28<T...>, TCList28<T...>>;
template <class... T> using TCList30 = TCPair<TCList29<T...>, TCList29<T...>>;
template <class... T>
char fg_Same(TCTypeList<TCPair<T, TCList30<T...>>...>);
long fg_Same(...);
static_assert(sizeof(fg_Same(TCTypeList<TCPair<int, TCList30<int>>>{})) == 1);
static_assert(sizeof(fg_Same(TCTypeList<TCPair<int, TCList30<int, long>>,
                                        TCPair<long, TCList30<int, long>>>{})) ==
              1);
static_assert(sizeof(fg_Same(TCTypeList<TCPair<int, TCList30<long>>>{})) ==
              sizeof(long));
static_assert(sizeof(fg_Same(TCTypeList<TCPair<int, TCList30<int, long>>,
                                        TCPair<long, TCList30<int, int>>>{})) ==
              sizeof(long));
template <class... T>
char fg_Twice(TCTypeList<TCPair<T, TCList30<T...>>...>,
              TCTypeList<TCPair<T, TCList30<T...>>...>);
long fg_Twice(...);
static_assert(sizeof(fg_Twice(TCTypeList<TCPair<int, TCList30<int>>>{},
                              TCTypeList<TCPair<int, TCList30<int>>>{})) == 1);
static_assert(sizeof(fg_Twice(TCTypeList<TCPair<int, TCList30<int>>>{},
                              TCTypeList<TCPair<long, TCList30<int>>>{})) ==
              sizeof(long));
}
