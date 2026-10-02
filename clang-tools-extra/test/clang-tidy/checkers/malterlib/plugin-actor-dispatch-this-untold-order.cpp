// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -std=c++20 -fsyntax-only -fno-caret-diagnostics -fplugin-arg-malterlib-checks='-*,malterlib-actor-dispatch-this' -I %S/Inputs %s 2>&1 | FileCheck --implicit-check-not=warning: --implicit-check-not=error: %s

// The plugin gives clang each warning as the check makes it, unsorted: the
// lambdas a local function object can be when a closure that captures it
// changes it, so its definitions aren't told, are reported in the order of the
// source, the same in each compile, not in the order of a map of its changes.

#include "malterlib-concurrency.h"

void fg_Keep(TCFunction<void()> const &_fKeep);

struct CApp : CActor {
  int m_Value;
  void f_Local(bool _bA, bool _bB, bool _bC, bool _bD, TCActor<> _Other) {
    TCFunction<int()> fCallback{[this] { return m_Value; }};
// CHECK: plugin-actor-dispatch-this-untold-order.cpp:[[@LINE-1]]:34: warning: a lambda that captures 'this' of actor 'CApp' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
    if (_bA)
      fCallback = [this] { return m_Value + 1; };
// CHECK-NEXT: plugin-actor-dispatch-this-untold-order.cpp:[[@LINE-1]]:20: warning: a lambda that captures 'this' of actor 'CApp'
    if (_bB)
      fCallback = [this] { return m_Value + 2; };
// CHECK-NEXT: plugin-actor-dispatch-this-untold-order.cpp:[[@LINE-1]]:20: warning: a lambda that captures 'this' of actor 'CApp'
    if (_bC)
      fCallback = [this] { return m_Value + 3; };
// CHECK-NEXT: plugin-actor-dispatch-this-untold-order.cpp:[[@LINE-1]]:20: warning: a lambda that captures 'this' of actor 'CApp'
    if (_bD)
      fCallback = [this] { return m_Value + 4; };
// CHECK-NEXT: plugin-actor-dispatch-this-untold-order.cpp:[[@LINE-1]]:20: warning: a lambda that captures 'this' of actor 'CApp'
    auto fChange = [&] { fCallback = [] { return 0; }; };
    fChange();
    fg_Dispatch(_Other, fCallback);
  }

  // The same with the closure given to a function that keeps it.
  void f_Kept(bool _bA, bool _bB, bool _bC, TCActor<> _Other) {
    TCFunction<int()> fCallback{[] { return 0; }};
    if (_bA)
      fCallback = [this] { return m_Value + 1; };
// CHECK-NEXT: plugin-actor-dispatch-this-untold-order.cpp:[[@LINE-1]]:20: warning: a lambda that captures 'this' of actor 'CApp'
    if (_bB)
      fCallback = [this] { return m_Value + 2; };
// CHECK-NEXT: plugin-actor-dispatch-this-untold-order.cpp:[[@LINE-1]]:20: warning: a lambda that captures 'this' of actor 'CApp'
    if (_bC)
      fCallback = [this] { return m_Value + 3; };
// CHECK-NEXT: plugin-actor-dispatch-this-untold-order.cpp:[[@LINE-1]]:20: warning: a lambda that captures 'this' of actor 'CApp'
    fg_Keep([&] { fCallback.f_Clear(); });
    fg_Dispatch(_Other, fCallback);
  }
};
