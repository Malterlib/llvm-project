// RUN: %check_clang_tidy -std=c++20-or-later %s malterlib-async-destroy %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -I %S/Inputs

// A getter that returns what the one before returns, in more than one return,
// is looked at once for each way and level it is met at, not once for each
// path to it, which is exponential in the length of the chain.

#include "malterlib-concurrency.h"

bool fg_Choose();
TCFuture<int> fg_Suspend();
void fg_Use(int const *_pValue);

#define DGetter(N, P) \
  int const *f_Get##N() const { \
    if (fg_Choose()) \
      return f_Get##P(); \
    if (fg_Choose()) \
      return f_Get##P(); \
    return f_Get##P(); \
  }
#define DGetters \
  DGetter(1, 0) \
  DGetter(2, 1) \
  DGetter(3, 2) \
  DGetter(4, 3) \
  DGetter(5, 4) \
  DGetter(6, 5) \
  DGetter(7, 6) \
  DGetter(8, 7) \
  DGetter(9, 8) \
  DGetter(10, 9) \
  DGetter(11, 10) \
  DGetter(12, 11) \
  DGetter(13, 12) \
  DGetter(14, 13) \
  DGetter(15, 14) \
  DGetter(16, 15)

// Points elsewhere, which the cleanup may use after it suspends.
struct CValue {
  int const *f_Get0() const { return nullptr; }
  DGetters
};

TCFuture<int> fg_Test(CValue &_Value) {
  auto Guard = co_await fg_AsyncDestroy([&_Value]() -> TCFuture<int> {
    int const *pValue = _Value.f_Get16();
    co_await fg_Suspend();
    fg_Use(pValue);
    co_return {};
  });
  co_return {};
}

// Points into the object the cleanup captures by reference, through fewer
// getters than the depth the check follows them to.
struct CThisValue {
  int m_Value;
  int const *f_Get0() const { return &m_Value; }
  DGetter(1, 0) DGetter(2, 1) DGetter(3, 2) DGetter(4, 3) DGetter(5, 4) DGetter(6, 5)
  DGetter(7, 6) DGetter(8, 7) DGetter(9, 8) DGetter(10, 9) DGetter(11, 10) DGetter(12, 11)
};

TCFuture<int> fg_TestThis(CThisValue &_Value) {
  auto Guard = co_await fg_AsyncDestroy([&_Value]() -> TCFuture<int> {
    int const *pValue = _Value.f_Get12();
    co_await fg_Suspend();
    fg_Use(pValue);
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: the cleanup function of 'fg_AsyncDestroy' uses 'pValue', which it captures by reference, after its first suspension
    co_return {};
  });
  co_return {};
}
