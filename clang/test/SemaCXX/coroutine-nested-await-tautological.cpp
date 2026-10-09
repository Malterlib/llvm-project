// RUN: %clang_cc1 -std=c++20 -fsyntax-only -Wtautological-bitwise-compare -verify %s

// The check of a comparison that is always true or false for a macro in it
// visits the operand of a co_await once, which the expressions that make its
// awaiter share, so it goes through nested awaits, written out rather than by
// a macro, in time linear in their depth, not exponential.

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
    void return_value(bool) {}
    void unhandled_exception() {}
  };
};

Awaiter ready(int);

Task nested() {
  co_return ((co_await ready(
      co_await ready(
      co_await ready(
      co_await ready(
      co_await ready(
      co_await ready(
      co_await ready(
      co_await ready(
      co_await ready(
      co_await ready(
      co_await ready(
      co_await ready(
      co_await ready(
      co_await ready(
      co_await ready(
      co_await ready(
      co_await ready(
      co_await ready(
      co_await ready(
      co_await ready(
      co_await ready(
      co_await ready(
      co_await ready(
      co_await ready(
      co_await ready(
      co_await ready(
      co_await ready(
      co_await ready(
      co_await ready(
      co_await ready(
      co_await ready(
      co_await ready(
      co_await ready(
      co_await ready(
      co_await ready(
      co_await ready(
      co_await ready(
      co_await ready(
      co_await ready(
      co_await ready(0))))))))))))))))))))))))))))))))))))))))) & 4) == 2; // expected-warning {{bitwise comparison always evaluates to false}}
}
