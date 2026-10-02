// RUN: %check_clang_tidy -std=c++23-or-later %s malterlib-continue-running-on-actor %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -I %S/Inputs

// A borrowed member of a local holder given again before the use, after 26
// nested co_awaits: each statement of the function is looked at once, also
// those the nodes of each co_await share.

#include "malterlib-concurrency.h"

using namespace NMib::NConcurrency;

TCFuture<int> fg_NestedAwaitWork(int _Value);
struct CNestedAwaitView {
  int *m_pValue;
};
struct CNestedAwaitActor : CActor {
  int m_Value;
  TCFuture<int> f_Run() {
    int Local = 1;
    CNestedAwaitView View{&m_Value};
    int Value = co_await fg_NestedAwaitWork(co_await fg_NestedAwaitWork(co_await fg_NestedAwaitWork(co_await fg_NestedAwaitWork(co_await fg_NestedAwaitWork(co_await fg_NestedAwaitWork(co_await fg_NestedAwaitWork(co_await fg_NestedAwaitWork(co_await fg_NestedAwaitWork(co_await fg_NestedAwaitWork(co_await fg_NestedAwaitWork(co_await fg_NestedAwaitWork(co_await fg_NestedAwaitWork(co_await fg_NestedAwaitWork(co_await fg_NestedAwaitWork(co_await fg_NestedAwaitWork(co_await fg_NestedAwaitWork(co_await fg_NestedAwaitWork(co_await fg_NestedAwaitWork(co_await fg_NestedAwaitWork(co_await fg_NestedAwaitWork(co_await fg_NestedAwaitWork(co_await fg_NestedAwaitWork(co_await fg_NestedAwaitWork(co_await fg_NestedAwaitWork(co_await fg_NestedAwaitWork(0))))))))))))))))))))))))));
    View.m_pValue = &Local;
    auto Other = fg_BlockingActor();
    co_await fg_ContinueRunningOnActor(Other);
    co_return *View.m_pValue + Value;
  }
};
