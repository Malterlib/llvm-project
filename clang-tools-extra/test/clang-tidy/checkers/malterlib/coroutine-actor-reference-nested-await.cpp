// RUN: %check_clang_tidy -std=c++20-or-later %s malterlib-coroutine-actor-reference %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -I %S/Inputs

#include "malterlib-actor-phase.h"

// A helper of deeply nested awaits, whose parts each share their operand, is
// gone through once for each node, in time that grows with how deep they
// are, not exponentially.

namespace NNestedAwait {
TCFuture<int> fg_Id(int _Value);
TCFuture<int> fg_Work();

struct CItems {
  int *f_FindEqual(int _Key);
  void f_Clear();
};

TCFuture<int> fg_Nested(CItems &_Items) {
  co_return co_await fg_Id(co_await fg_Id(co_await fg_Id(co_await fg_Id(co_await fg_Id(co_await fg_Id(co_await fg_Id(co_await fg_Id(co_await fg_Id(co_await fg_Id(co_await fg_Id(co_await fg_Id(co_await fg_Id(co_await fg_Id(co_await fg_Id(co_await fg_Id(co_await fg_Id(co_await fg_Id(co_await fg_Id(co_await fg_Id(co_await fg_Id(co_await fg_Id(co_await fg_Id(co_await fg_Id(co_await fg_Id(co_await fg_Id(co_await fg_Id(co_await fg_Id(co_await fg_Id(co_await fg_Id(co_await fg_Work()))))))))))))))))))))))))))))));
}

struct CApp : CActor {
  CItems mp_Items;

  TCFuture<int> f_Run() {
    int *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      (void)fg_Nested(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
  }
};
} // namespace NNestedAwait

// A guard given what it doesn't keep by a member function whose body,
// nested awaits in a lambda, never names it, is gone through once for each
// node too.
namespace NNestedAwaitSetter {
TCFuture<int> fg_Id(int _Value);
TCFuture<int> fg_Work();

struct CItems {
  int *f_FindEqual(int _Key);
  void f_Clear();
};

struct CGuard {
  CItems *m_pItems = nullptr;
  ~CGuard() {
    if (m_pItems)
      m_pItems->f_Clear();
  }
  void f_Set(CItems &_Items) {
    auto fNested = [&]() -> TCFuture<int> {
      co_return co_await fg_Id(co_await fg_Id(co_await fg_Id(co_await fg_Id(co_await fg_Id(co_await fg_Id(co_await fg_Id(co_await fg_Id(co_await fg_Id(co_await fg_Id(co_await fg_Id(co_await fg_Id(co_await fg_Id(co_await fg_Id(co_await fg_Id(co_await fg_Id(co_await fg_Id(co_await fg_Id(co_await fg_Id(co_await fg_Id(co_await fg_Id(co_await fg_Id(co_await fg_Id(co_await fg_Id(co_await fg_Id(co_await fg_Id(co_await fg_Id(co_await fg_Id(co_await fg_Id(co_await fg_Id(co_await fg_Work()))))))))))))))))))))))))))))));
    };
    (void)fNested;
  }
};

struct CApp : CActor {
  CItems mp_Items;

  TCFuture<int> f_Run() {
    int *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      CGuard Guard;
      Guard.f_Set(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
  }
};
} // namespace NNestedAwaitSetter
