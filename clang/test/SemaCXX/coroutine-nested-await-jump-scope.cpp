// RUN: %clang_cc1 -std=c++20 -fsyntax-only -verify %s
// expected-no-diagnostics

// The check of the scopes jumps cross, which a switch makes, walks the operand
// of a co_await once, which the expression that makes its awaiter has too, so
// it goes through nested awaits in time linear in their depth, not
// exponential.

#include "Inputs/std-coroutine.h"

struct Awaiter {
  bool await_ready() noexcept { return true; }
  void await_suspend(std::coroutine_handle<>) noexcept {}
  int await_resume() noexcept { return 0; }
};

struct Task {
  struct promise_type {
    Task get_return_object() { return {}; }
    std::suspend_never initial_suspend() { return {}; }
    std::suspend_never final_suspend() noexcept { return {}; }
    void return_void() {}
    void unhandled_exception() {}
  };
};

Awaiter ready(int);

Task nested(int Selector) {
  switch (Selector) {
  case 0:
    (void)(co_await ready(co_await ready(co_await ready(co_await ready(co_await ready(co_await ready(co_await ready(co_await ready(co_await ready(co_await ready(co_await ready(co_await ready(co_await ready(co_await ready(co_await ready(co_await ready(co_await ready(co_await ready(co_await ready(co_await ready(co_await ready(co_await ready(co_await ready(co_await ready(co_await ready(co_await ready(co_await ready(co_await ready(0)))))))))))))))))))))))))))));
    break;
  }
  co_return;
}
