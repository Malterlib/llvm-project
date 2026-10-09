// RUN: %clang_cc1 -std=c++20 -fsyntax-only -fopenmp -fopenmp-targets=x86_64-unknown-linux-gnu -verify %s
// expected-no-diagnostics

// The emitter of the deferred diagnostics of OpenMP offloading visits the
// operand of each of nested awaits once, not also in the expression that
// makes its awaiter, which would be a number of times exponential in their
// depth.

#include "Inputs/std-coroutine.h"

struct Task {
  struct promise_type {
    Task get_return_object();
    std::suspend_never initial_suspend();
    std::suspend_never final_suspend() noexcept;
    void return_void();
    void unhandled_exception();
  };
};
struct Awaiter {
  bool await_ready();
  void await_suspend(std::coroutine_handle<>);
  Awaiter &await_resume();
};
#define A2(E) co_await co_await (E)
#define A8(E) A2(A2(A2(A2(E))))
#define A40(E) A8(A8(A8(A8(A8(E)))))
Task test(Awaiter &a) {
  (void)A40(a);
  co_return;
}
