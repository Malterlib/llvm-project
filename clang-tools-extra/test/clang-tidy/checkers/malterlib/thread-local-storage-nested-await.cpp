// RUN: %check_clang_tidy -std=c++23-or-later %s malterlib-thread-local-storage %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml', malterlib-thread-local-storage.ThreadLocalTemplates: 'TCNestedAwaitThreadLocal'}}" -- -I %S/Inputs

// What is reported in an instantiation is reported where code that isn't a
// template's made it, found through what uses it: each statement of a
// coroutine once, also those the nodes of each of 24 nested co_awaits share.

#include "malterlib-concurrency.h"

struct CNestedAwaitData {};
template <typename t_CType> struct TCNestedAwaitThreadLocal {
  t_CType *f_Get();
};
template <typename t_CType> void fg_NestedAwaitHold() { t_CType Local; }
struct CNestedAwaitable {
  bool await_ready();
  void await_suspend(std::coroutine_handle<>);
  CNestedAwaitable await_resume();
};
CNestedAwaitable fg_NestedAwaitGet();
template <typename t_CType> TCFuture<int> fg_NestedAwaitRun() {
  fg_NestedAwaitHold<TCNestedAwaitThreadLocal<CNestedAwaitData>>();
  co_await co_await co_await co_await co_await co_await co_await co_await co_await co_await co_await co_await co_await co_await co_await co_await co_await co_await co_await co_await co_await co_await co_await co_await fg_NestedAwaitGet();
  co_return 0;
}
TCFuture<int> fg_NestedAwaitGo() { return fg_NestedAwaitRun<int>(); }
// CHECK-MESSAGES: :[[@LINE-1]]:43: warning: 'Local' holds 'CNestedAwaitData' for each thread, which isn't one of ThreadLocalTypes, so it isn't taken as thread-local data; add it to ThreadLocalTypes in the options file, or hold a struct of its own for the thread instead [malterlib-thread-local-storage]
