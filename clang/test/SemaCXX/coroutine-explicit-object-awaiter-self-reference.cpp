// RUN: %clang_cc1 -std=c++23 -fsyntax-only -Wuninitialized -verify %s

// An awaiter whose await_ready, await_suspend and await_resume take it by
// value, `bool await_ready(this Awaiter)`, copies it for each, from an opaque
// value of the same expression, whose awaits the check of a variable used in
// its own initializer handles once, not once for each copy, which would grow
// exponentially with each level of nested awaits. It still finds the use.

namespace std {
template <class R, class...> struct coroutine_traits {
  using promise_type = typename R::promise_type;
};
template <class P = void> struct coroutine_handle {
  static coroutine_handle from_address(void *);
};
} // namespace std

struct Awaiter {
  bool await_ready(this Awaiter) noexcept { return false; }
  template <class H> void await_suspend(this Awaiter, H) noexcept {}
  int await_resume(this Awaiter) noexcept { return 1; }
};
struct Task {
  struct promise_type {
    Task get_return_object();
    Awaiter initial_suspend();
    Awaiter final_suspend() noexcept;
    void return_void();
    void unhandled_exception();
  };
};
Awaiter work(int);

Task chain() { auto Lambda = [x = co_await work(co_await work(co_await work(co_await work(co_await work(co_await work(co_await work(co_await work(co_await work(co_await work(co_await work(co_await work(co_await work(co_await work(co_await work(co_await work(co_await work(co_await work(co_await work(co_await work(co_await work(co_await work(co_await work(co_await work(co_await work(co_await work(co_await work(co_await work(co_await work(co_await work(1))))))))))))))))))))))))))))))] {}; }

Task own() {
  int y = co_await work(co_await work(y)); // expected-warning {{variable 'y' is uninitialized when used within its own initialization}}
}
