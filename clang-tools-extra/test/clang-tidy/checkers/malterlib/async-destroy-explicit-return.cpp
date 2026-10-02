// RUN: %check_clang_tidy -std=c++23-or-later %s malterlib-async-destroy %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -I %S/Inputs

// 24 nested coroutine lambdas whose promise's return_value has an explicit
// object parameter, in a function that uses fg_AsyncDestroy: each statement
// is indexed once, also what a co_return shares between its operand and its
// promise call.

#include "malterlib-concurrency.h"

using namespace NMib::NConcurrency;

struct CExplicitFuture {
  struct promise_type {
    CExplicitFuture get_return_object();
    std::suspend_never initial_suspend() noexcept;
    std::suspend_never final_suspend() noexcept;
    void return_value(this promise_type &_Self, int _Value);
    void unhandled_exception();
  };
  bool await_ready();
  void await_suspend(std::coroutine_handle<>);
  int await_resume();
};

TCFuture<int> fg_ExplicitReturnNest() {
  auto Cleanup = co_await fg_AsyncDestroy([]() -> TCFuture<int> { co_return 0; });
  co_return co_await [&]() -> CExplicitFuture { co_return co_await [&]() -> CExplicitFuture { co_return co_await [&]() -> CExplicitFuture { co_return co_await [&]() -> CExplicitFuture { co_return co_await [&]() -> CExplicitFuture { co_return co_await [&]() -> CExplicitFuture { co_return co_await [&]() -> CExplicitFuture { co_return co_await [&]() -> CExplicitFuture { co_return co_await [&]() -> CExplicitFuture { co_return co_await [&]() -> CExplicitFuture { co_return co_await [&]() -> CExplicitFuture { co_return co_await [&]() -> CExplicitFuture { co_return co_await [&]() -> CExplicitFuture { co_return co_await [&]() -> CExplicitFuture { co_return co_await [&]() -> CExplicitFuture { co_return co_await [&]() -> CExplicitFuture { co_return co_await [&]() -> CExplicitFuture { co_return co_await [&]() -> CExplicitFuture { co_return co_await [&]() -> CExplicitFuture { co_return co_await [&]() -> CExplicitFuture { co_return co_await [&]() -> CExplicitFuture { co_return co_await [&]() -> CExplicitFuture { co_return co_await [&]() -> CExplicitFuture { co_return co_await [&]() -> CExplicitFuture { co_return 0; }(); }(); }(); }(); }(); }(); }(); }(); }(); }(); }(); }(); }(); }(); }(); }(); }(); }(); }(); }(); }(); }(); }(); }();
}
