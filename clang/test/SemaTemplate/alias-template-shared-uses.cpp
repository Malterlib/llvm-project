// RUN: %clang_cc1 -std=c++20 -fsyntax-only -verify %s
// RUN: not %clang_cc1 -std=c++20 -fsyntax-only -DUSES %s 2>&1 | FileCheck %s --check-prefix=USES
// expected-no-diagnostics

// A specialization of an alias template that an alias names twice,
// `TCConditional<true, TC##P<T>, TC##P<T>>`, is substituted once in the
// substitution of the outermost one, not once for each path to it, which is
// exponential in the depth of such aliases.

template <bool, class A, class B> struct TCSelect {
  using CType = A;
};
template <bool B, class X, class Y>
using TCConditional = typename TCSelect<B, X, Y>::CType;

template <class T> using TC0 = T;
#define LEVEL(N, P)                                                            \
  template <class T> using TC##N = TCConditional<true, TC##P<T>, TC##P<T>>;
LEVEL(1, 0) LEVEL(2, 1) LEVEL(3, 2) LEVEL(4, 3) LEVEL(5, 4) LEVEL(6, 5)
LEVEL(7, 6) LEVEL(8, 7) LEVEL(9, 8) LEVEL(10, 9) LEVEL(11, 10) LEVEL(12, 11)
LEVEL(13, 12) LEVEL(14, 13) LEVEL(15, 14) LEVEL(16, 15) LEVEL(17, 16)
LEVEL(18, 17) LEVEL(19, 18) LEVEL(20, 19) LEVEL(21, 20) LEVEL(22, 21)
LEVEL(23, 22) LEVEL(24, 23) LEVEL(25, 24) LEVEL(26, 25) LEVEL(27, 26)
LEVEL(28, 27) LEVEL(29, 28) LEVEL(30, 29) LEVEL(31, 30) LEVEL(32, 31)
LEVEL(33, 32) LEVEL(34, 33) LEVEL(35, 34) LEVEL(36, 35) LEVEL(37, 36)
LEVEL(38, 37) LEVEL(39, 38) LEVEL(40, 39)

static_assert(__is_same(TC40<int>, int));

// So is one that the default arguments of another name twice, `class A =
// TCDefault##P<T>, class B = TCDefault##P<T>`, which are checked before it is
// substituted.
template <class T> using TCDefault0 = T;
#define DEFAULT_LEVEL(N, P)                                                     \
  template <class T, class A = TCDefault##P<T>, class B = TCDefault##P<T>>     \
  using TCDefault##N = T;
DEFAULT_LEVEL(1, 0) DEFAULT_LEVEL(2, 1) DEFAULT_LEVEL(3, 2)
DEFAULT_LEVEL(4, 3) DEFAULT_LEVEL(5, 4) DEFAULT_LEVEL(6, 5)
DEFAULT_LEVEL(7, 6) DEFAULT_LEVEL(8, 7) DEFAULT_LEVEL(9, 8)
DEFAULT_LEVEL(10, 9) DEFAULT_LEVEL(11, 10) DEFAULT_LEVEL(12, 11)
DEFAULT_LEVEL(13, 12) DEFAULT_LEVEL(14, 13) DEFAULT_LEVEL(15, 14)
DEFAULT_LEVEL(16, 15) DEFAULT_LEVEL(17, 16) DEFAULT_LEVEL(18, 17)
DEFAULT_LEVEL(19, 18) DEFAULT_LEVEL(20, 19) DEFAULT_LEVEL(21, 20)
DEFAULT_LEVEL(22, 21) DEFAULT_LEVEL(23, 22) DEFAULT_LEVEL(24, 23)
DEFAULT_LEVEL(25, 24) DEFAULT_LEVEL(26, 25) DEFAULT_LEVEL(27, 26)
DEFAULT_LEVEL(28, 27) DEFAULT_LEVEL(29, 28) DEFAULT_LEVEL(30, 29)
DEFAULT_LEVEL(31, 30) DEFAULT_LEVEL(32, 31) DEFAULT_LEVEL(33, 32)
DEFAULT_LEVEL(34, 33) DEFAULT_LEVEL(35, 34) DEFAULT_LEVEL(36, 35)
DEFAULT_LEVEL(37, 36) DEFAULT_LEVEL(38, 37) DEFAULT_LEVEL(39, 38)
DEFAULT_LEVEL(40, 39)

static_assert(__is_same(TCDefault40<int>, int));

// But each substitution of one that makes the closure type of a lambda makes
// its own, also through another alias.
template <class A, class B> struct TCPair {
  using CFirst = A;
  using CSecond = B;
};
template <class T> using TCLambda = decltype([] {});
template <class T> using TCLambdas = TCPair<TCLambda<T>, TCLambda<T>>;
template <class T> using TCOuter = TCPair<TCLambdas<T>, TCLambdas<T>>;
static_assert(!__is_same(TCLambda<int>, TCLambda<int>));
static_assert(!__is_same(TCLambdas<int>::CFirst, TCLambdas<int>::CSecond));
static_assert(!__is_same(TCOuter<int>::CFirst, TCOuter<int>::CSecond));

// Nor one met again after a definition was instantiated, which can change what
// it substitutes to, like whether a type is complete, also of one whose
// arguments aren't all types.
template <bool> struct TCBool {};
template <class T> using TCComplete = TCBool<__is_complete_type(T)>;
template <class T, int N> using TCCompleteN = TCBool<__is_complete_type(T)>;
template <class T> struct TCIncomplete { int m_Value; };
template <class, class, class> struct TCTriple {};
template <class T>
using TCCompleted =
    TCTriple<TCComplete<T>, decltype(sizeof(T)), TCComplete<T>>;
template <class T>
using TCCompletedN =
    TCTriple<TCCompleteN<T, 0>, decltype(sizeof(T)), TCCompleteN<T, 0>>;
static_assert(__is_same(TCCompleted<TCIncomplete<int>>,
                        TCTriple<TCBool<false>, decltype(sizeof(int)),
                                 TCBool<true>>));
static_assert(__is_same(TCCompletedN<TCIncomplete<long>>,
                        TCTriple<TCBool<false>, decltype(sizeof(int)),
                                 TCBool<true>>));

// Also one met again after an instantiation finished, which completed what
// the substitution in it saw incomplete.
template <int> struct TCTarget;
template <int N> using TCCompleteTarget = TCBool<__is_complete_type(TCTarget<N>)>;
struct CHelper {
  template <int N> using CInspect = TCCompleteTarget<N>;
  template <int N>
  using CFinish = TCPair<decltype(sizeof(TCTarget<N>)), CInspect<N>>;
};
template <int N> struct TCTarget {
  using CObserved = CHelper::CInspect<N>;
};
static_assert(__is_same(CHelper::CFinish<0>::CSecond, TCBool<true>));

// Also where its own substitution completed it, after it saw it incomplete,
// also of one whose arguments aren't all types.
template <bool V, class> struct TCResult { static constexpr bool mc_bValue = V; };
template <class T> struct TCBox { int m_Value; };
template <class T>
using TCProbe = TCResult<__is_complete_type(T), decltype(sizeof(T))>;
template <class T, int N>
using TCProbeN = TCResult<__is_complete_type(T), decltype(sizeof(T))>;
template <class T> using TCTwice = TCPair<TCProbe<T>, TCProbe<T>>;
template <class T> using TCTwiceN = TCPair<TCProbeN<T, 0>, TCProbeN<T, 0>>;
static_assert(!TCTwice<TCBox<int>>::CFirst::mc_bValue);
static_assert(TCTwice<TCBox<int>>::CSecond::mc_bValue);
static_assert(TCTwiceN<TCBox<long>>::CSecond::mc_bValue);

// And not where a definition is only asked for that is already instantiated,
// like the variable template a bound uses again at each level.
template <class T> constexpr int g_Extent = 1;
template <class T> using TCExtent0 = T[g_Extent<T>];
#define EXTENT_LEVEL(N, P)                                                     \
  template <class T> using TCExtent##N = TCPair<TCExtent##P<T>, TCExtent##P<T>>;
EXTENT_LEVEL(1, 0) EXTENT_LEVEL(2, 1) EXTENT_LEVEL(3, 2) EXTENT_LEVEL(4, 3)
EXTENT_LEVEL(5, 4) EXTENT_LEVEL(6, 5) EXTENT_LEVEL(7, 6) EXTENT_LEVEL(8, 7)
EXTENT_LEVEL(9, 8) EXTENT_LEVEL(10, 9) EXTENT_LEVEL(11, 10) EXTENT_LEVEL(12, 11)
EXTENT_LEVEL(13, 12) EXTENT_LEVEL(14, 13) EXTENT_LEVEL(15, 14)
EXTENT_LEVEL(16, 15) EXTENT_LEVEL(17, 16) EXTENT_LEVEL(18, 17)
EXTENT_LEVEL(19, 18) EXTENT_LEVEL(20, 19) EXTENT_LEVEL(21, 20)
EXTENT_LEVEL(22, 21) EXTENT_LEVEL(23, 22) EXTENT_LEVEL(24, 23)
EXTENT_LEVEL(25, 24) EXTENT_LEVEL(26, 25) EXTENT_LEVEL(27, 26)
EXTENT_LEVEL(28, 27) EXTENT_LEVEL(29, 28) EXTENT_LEVEL(30, 29)
EXTENT_LEVEL(31, 30) EXTENT_LEVEL(32, 31) EXTENT_LEVEL(33, 32)
EXTENT_LEVEL(34, 33) EXTENT_LEVEL(35, 34) EXTENT_LEVEL(36, 35)
EXTENT_LEVEL(37, 36) EXTENT_LEVEL(38, 37) EXTENT_LEVEL(39, 38)
EXTENT_LEVEL(40, 39)
static_assert(g_Extent<int> == 1);
TCExtent40<int> *g_pExtent;

#ifdef USES
// A use met again is still diagnosed where it is, also where a pragma
// suppresses the diagnostic at the one met first, and one written with a tag
// keyword is an error at each use.
template <class T> using TCOld [[deprecated("old")]] = T;
template <class T,
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
          class X = TCOld<T>,
#pragma clang diagnostic pop
          class Y = TCOld<T>>
using TCUses = T;
TCUses<int> g_Uses;
template <class T> using TCPlain = T;
template <class T>
using TCTagged = TCPair<struct TCPlain<T>, struct TCPlain<T>>;
TCTagged<int> g_Tagged;
// USES: :[[@LINE-7]]:{{[0-9]+}}: warning: 'TCOld' is deprecated: old
// USES-COUNT-4: error: alias template 'TCPlain' cannot be referenced with the 'struct' specifier
// USES: 1 warning and 4 errors generated.
#endif
