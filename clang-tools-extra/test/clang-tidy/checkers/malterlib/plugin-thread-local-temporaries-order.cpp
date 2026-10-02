// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -std=c++23 -fsyntax-only -fno-caret-diagnostics -fplugin-arg-malterlib-checks='-*,malterlib-coroutine-thread-local-reference' -I %S/Inputs %s 2>&1 | FileCheck --implicit-check-not=warning: --implicit-check-not=error: %s

// The plugin gives clang each warning as the check makes it, unsorted: the
// temporaries alive across a suspension are reported in the order of the
// source, the same in each compile, not in the order of a set of them.

#include "malterlib-actor-phase.h"

struct CCurrentActorScope {
  CCurrentActorScope();
  ~CCurrentActorScope();
};
TCFuture<int> fg_Work();
int fg_Use(CCurrentActorScope, CCurrentActorScope, CCurrentActorScope,
           CCurrentActorScope, CCurrentActorScope, int);
int fg_Use3(CCurrentActorScope, CCurrentActorScope, CCurrentActorScope);

TCFuture<int> fg_ScopeTemporaries() {
  co_return fg_Use(CCurrentActorScope(),
// CHECK: plugin-thread-local-temporaries-order.cpp:[[@LINE-1]]:20: warning: a temporary 'CCurrentActorScope' keeps thread-local data of the thread that made it and is alive across a suspension; the coroutine can resume on another thread; end its scope before the suspension [malterlib-coroutine-thread-local-reference]
                   CCurrentActorScope(),
// CHECK-NEXT: plugin-thread-local-temporaries-order.cpp:[[@LINE-1]]:20: warning: a temporary 'CCurrentActorScope'
                   CCurrentActorScope(),
// CHECK-NEXT: plugin-thread-local-temporaries-order.cpp:[[@LINE-1]]:20: warning: a temporary 'CCurrentActorScope'
                   CCurrentActorScope(),
// CHECK-NEXT: plugin-thread-local-temporaries-order.cpp:[[@LINE-1]]:20: warning: a temporary 'CCurrentActorScope'
                   CCurrentActorScope(), co_await fg_Work());
// CHECK-NEXT: plugin-thread-local-temporaries-order.cpp:[[@LINE-1]]:20: warning: a temporary 'CCurrentActorScope'
}

// Also in what is awaited and in what a capture is made of.
TCFuture<int> fg_ScopeTemporariesInAwaited() {
  co_return co_await (fg_Use3(CCurrentActorScope(),
// CHECK-NEXT: plugin-thread-local-temporaries-order.cpp:[[@LINE-1]]:31: warning: a temporary 'CCurrentActorScope'
                              CCurrentActorScope(),
// CHECK-NEXT: plugin-thread-local-temporaries-order.cpp:[[@LINE-1]]:31: warning: a temporary 'CCurrentActorScope'
                              CCurrentActorScope()),
// CHECK-NEXT: plugin-thread-local-temporaries-order.cpp:[[@LINE-1]]:31: warning: a temporary 'CCurrentActorScope'
                      fg_Work());
}
TCFuture<int> fg_ScopeTemporariesInCapture() {
  auto fRead = [Value = (fg_Use3(CCurrentActorScope(),
// CHECK-NEXT: plugin-thread-local-temporaries-order.cpp:[[@LINE-1]]:34: warning: a temporary 'CCurrentActorScope'
                                 CCurrentActorScope(),
// CHECK-NEXT: plugin-thread-local-temporaries-order.cpp:[[@LINE-1]]:34: warning: a temporary 'CCurrentActorScope'
                                 CCurrentActorScope()),
// CHECK-NEXT: plugin-thread-local-temporaries-order.cpp:[[@LINE-1]]:34: warning: a temporary 'CCurrentActorScope'
                         co_await fg_Work())] { return Value; };
  co_return fRead();
}
