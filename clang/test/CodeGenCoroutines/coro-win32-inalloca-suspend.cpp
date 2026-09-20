// A call that passes an argument in memory builds its argument block on the
// stack of the function that starts evaluating the arguments. A coroutine that
// suspends among them resumes in another function, so that is rejected on the
// one ABI that has such calls.
//
// Code generation stops at the first error, so every rejected call gets a run
// of its own.
//
// RUN: %clang_cc1 -std=c++20 -triple=i686-pc-windows-msvc -emit-llvm-only -verify=case1 -DCASE=1 %s
// RUN: %clang_cc1 -std=c++20 -triple=i686-pc-windows-msvc -emit-llvm-only -verify=case2 -DCASE=2 %s
// RUN: %clang_cc1 -std=c++20 -triple=i686-pc-windows-msvc -emit-llvm-only -verify=case3 -DCASE=3 %s
// RUN: %clang_cc1 -std=c++20 -triple=i686-pc-windows-msvc -emit-llvm-only -verify=case4 -DCASE=4 %s
// RUN: %clang_cc1 -std=c++20 -triple=i686-pc-windows-msvc -emit-llvm-only -verify=case5 -DCASE=5 %s
// RUN: %clang_cc1 -std=c++20 -triple=i686-pc-windows-msvc -emit-llvm-only -verify=case6 -DCASE=6 %s
// RUN: %clang_cc1 -std=c++20 -triple=i686-pc-windows-msvc -emit-llvm-only -verify=case7 -DCASE=7 %s
// RUN: %clang_cc1 -std=c++20 -triple=i686-pc-windows-msvc -emit-llvm-only -verify=case8 -DCASE=8 %s
// RUN: %clang_cc1 -std=c++23 -triple=i686-pc-windows-msvc -emit-llvm-only -verify=other -DCASE=0 %s
// RUN: %clang_cc1 -std=c++20 -triple=x86_64-pc-windows-msvc -emit-llvm-only -verify=other -DALL %s
// RUN: %clang_cc1 -std=c++20 -triple=i686-unknown-linux-gnu -emit-llvm-only -verify=other -DALL %s

// other-no-diagnostics

namespace std {
template <typename R, typename... Args> struct coroutine_traits {
  using promise_type = typename R::promise_type;
};

template <class Promise = void> struct coroutine_handle {
  coroutine_handle() = default;
  static coroutine_handle from_address(void *) noexcept;
};
template <> struct coroutine_handle<void> {
  static coroutine_handle from_address(void *) noexcept;
  coroutine_handle() = default;
  template <class PromiseType>
  coroutine_handle(coroutine_handle<PromiseType>) noexcept;
};
} // namespace std

struct suspend_never {
  bool await_ready() noexcept { return true; }
  void await_suspend(std::coroutine_handle<>) noexcept {}
  void await_resume() noexcept {}
};

struct task {
  struct promise_type {
    task get_return_object() { return {}; }
    suspend_never initial_suspend() { return {}; }
    suspend_never final_suspend() noexcept { return {}; }
    void return_void() {}
    void unhandled_exception() {}
  };
};

struct Noisy {
  int val;
  Noisy(int v);
  Noisy(Noisy &&o) noexcept;
  ~Noisy();
};

struct NoisyAwaiter {
  bool await_ready() noexcept { return false; }
  void await_suspend(std::coroutine_handle<>) noexcept {}
  Noisy await_resume() noexcept;
};

struct IntAwaiter {
  bool await_ready() noexcept { return false; }
  void await_suspend(std::coroutine_handle<>) noexcept {}
  int await_resume() noexcept;
};

struct Callable {
  void operator()(Noisy x, int y);
};

struct Receiver {
  Receiver &operator=(Noisy x);
};

struct ReceiverAwaiter {
  bool await_ready() noexcept { return false; }
  void await_suspend(std::coroutine_handle<>) noexcept {}
  Receiver &await_resume() noexcept;
};

struct Constructed {
  Constructed(Noisy x, int y);
};

void consume_two(Noisy x, Noisy y);
void consume_int(Noisy x, int y);
void consume_ints(int x, int y);
void consume_task(Noisy x, task y);
int twice(int v);
IntAwaiter make_awaiter(Noisy x);

#if defined(ALL) || CASE == 1
task suspend_in_argument() {
  consume_two(co_await NoisyAwaiter{}, Noisy(42)); // case1-error {{a coroutine cannot suspend while evaluating the arguments of a call that passes an argument in memory}}
}
#endif

#if defined(ALL) || CASE == 2
task suspend_in_nested_argument() {
  consume_int(Noisy(1), twice(co_await IntAwaiter{})); // case2-error {{a coroutine cannot suspend while evaluating the arguments}}
}
#endif

#if defined(ALL) || CASE == 3
task suspend_in_call_operator_argument() {
  Callable f;
  f(Noisy(1), co_await IntAwaiter{}); // case3-error {{a coroutine cannot suspend while evaluating the arguments}}
}
#endif

#if defined(ALL) || CASE == 4
task suspend_in_constructor_argument() {
  Constructed c(Noisy(1), co_await IntAwaiter{}); // case4-error {{a coroutine cannot suspend while evaluating the arguments}}
}
#endif

#if defined(ALL) || CASE == 6
task suspend_in_constexpr_if_initializer() {
  consume_int(Noisy(1), ({
    if constexpr (int n = co_await IntAwaiter{}; true) {} // case6-error {{a coroutine cannot suspend while evaluating the arguments}}
    0;
  }));
}
#endif

#if defined(ALL) || CASE == 7
task suspend_in_assignment_object() {
  // The right operand is evaluated first, into the argument block.
  (co_await ReceiverAwaiter{}) = Noisy(42); // case7-error {{a coroutine cannot suspend while evaluating the arguments}}
}
#endif

#if defined(ALL) || CASE == 8
struct Layout {
  int a[4];
};
task suspend_in_offsetof_index() {
  consume_int(Noisy(1), __builtin_offsetof(Layout, a[co_await IntAwaiter{}])); // case8-error {{a coroutine cannot suspend while evaluating the arguments}}
}
#endif

#if defined(ALL) || CASE == 5
template <typename T> task suspend_in_template(T) {
  consume_int(Noisy(1), co_await IntAwaiter{}); // case5-error {{a coroutine cannot suspend while evaluating the arguments}}
}
template task suspend_in_template(int);
#endif

// The cases below keep the suspend point out of the argument evaluation of the
// call that needs the argument block.

task suspend_before_call() {
  Noisy x = co_await NoisyAwaiter{};
  consume_two(static_cast<Noisy &&>(x), Noisy(42));
}

#if __cplusplus >= 202302L
task suspend_in_consteval_branch() {
  consume_int(Noisy(1), ({
    if consteval {
      co_await IntAwaiter{};
    }
    0;
  }));
}
#endif

template <bool Suspend> task suspend_in_discarded_branch() {
  consume_int(Noisy(1), ({
    if constexpr (Suspend)
      co_await IntAwaiter{};
    0;
  }));
}
template task suspend_in_discarded_branch<false>();

task suspend_after_call() {
  co_await make_awaiter(Noisy(1));
}

task suspend_without_memory_argument() {
  consume_ints(co_await IntAwaiter{}, 2);
}

task suspend_in_another_coroutine() {
  consume_task(Noisy(1), []() -> task { co_await IntAwaiter{}; }());
  co_return;
}
