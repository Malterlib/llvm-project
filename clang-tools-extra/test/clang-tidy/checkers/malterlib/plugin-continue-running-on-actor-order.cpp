// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -std=c++20 -fsyntax-only -fno-caret-diagnostics -fplugin-arg-malterlib-checks='-*,malterlib-continue-running-on-actor' -I %S/Inputs %s 2>&1 | FileCheck --implicit-check-not=warning: --implicit-check-not=error: %s

// The plugin gives clang each warning as the check makes it, unsorted: the
// registrations of one full expression alive at a switch to another actor are
// reported in the order of the source, the same in each compile, not in the
// order of a set of them.

#include "malterlib-actor-phase.h"

struct CApp : CActor {
  int m_Value;
  TCFuture<int> f_Temporaries(TCActor<> _Other) {
    auto Own = fg_ThisActor(this);
    ((void)(co_await fg_OnResume([this]() -> CExceptionPointer {
// CHECK: plugin-continue-running-on-actor-order.cpp:[[@LINE-1]]:34: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
       ++m_Value;
       return {};
     })),
     (void)(co_await fg_OnResume([this]() -> CExceptionPointer {
// CHECK-NEXT: plugin-continue-running-on-actor-order.cpp:[[@LINE-1]]:34: warning: the object of the actor is used after 'fg_ContinueRunningOnActor'
       ++m_Value;
       return {};
     })),
     (void)(co_await fg_OnResume([this]() -> CExceptionPointer {
// CHECK-NEXT: plugin-continue-running-on-actor-order.cpp:[[@LINE-1]]:34: warning: the object of the actor is used after 'fg_ContinueRunningOnActor'
       ++m_Value;
       return {};
     })),
     (void)(co_await fg_OnResume([this]() -> CExceptionPointer {
// CHECK-NEXT: plugin-continue-running-on-actor-order.cpp:[[@LINE-1]]:34: warning: the object of the actor is used after 'fg_ContinueRunningOnActor'
       ++m_Value;
       return {};
     })),
     co_await fg_ContinueRunningOnActor(_Other));
    co_await fg_ContinueRunningOnActor(Own);
    co_return 0;
  }
};
