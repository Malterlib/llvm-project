// RUN: %clang_cc1 -std=c++20 -fsyntax-only -Wuninitialized -Wcomma -Wfor-loop-analysis -verify %s

// The operand of a co_await is evaluated as part of the expression that makes
// its awaiter, which the visitors of what is evaluated meet too. Each is
// visited once, so the check of a variable used in its own initializer goes
// through nested awaits in time linear in their depth, not exponential, and
// still sees a use in an operand.

#include "Inputs/std-coroutine.h"

struct Task {
  struct promise_type {
    Task get_return_object() { return {}; }
    std::suspend_never initial_suspend() { return {}; }
    std::suspend_never final_suspend() noexcept { return {}; }
    void return_value(int) {}
    void unhandled_exception() {}
  };
};

struct Awaiter {
  bool await_ready() noexcept { return true; }
  void await_suspend(std::coroutine_handle<>) noexcept {}
  int await_resume() noexcept { return 0; }
};

Awaiter work(int);

#define A(x) co_await work(x)
#define A2(x) A(A(x))
#define A8(x) A2(A2(A2(A2(x))))
#define A32(x) A8(A8(A8(A8(x))))

Task nested() {
  auto Read = [Value = A32(A8(0))] { return Value; };
  co_return Read();
}

// Also the checks of a for loop, of whether the variables of its condition
// change in its body or increment, and of a break or continue in its
// increment, which still see a change in an operand.
Task loop() {
  for (int i = 0; i < 1;) // expected-warning {{variable 'i' used in loop condition not modified in loop body}}
    A32(A8(0));
  for (int i = 0; i < 1;)
    A32(A8(i++));
  for (int i = 0; i < 1; A32(A8(0))) {} // expected-warning {{variable 'i' used in loop condition not modified in loop body}}
  for (int i = 0; i < 1; A32(A8(i++))) {}
  co_return 0;
}

// Also the search of the initializer of a variable for its own use, which
// still finds one after an operand.
Task own() {
  int Value = ((void)A32(A8(0)), Value); // expected-warning {{variable 'Value' is uninitialized when used within its own initialization}}
  co_return Value;
}

// Also the check of the commas in a condition.
Task condition() {
  if (A32(A8(0)))
    co_return 1;
  co_return 0;
}

// Also the search of a statement attribute for the calls it applies to,
// through an awaiter whose result is an awaiter, awaited in turn.
struct SelfAwaiter {
  bool await_ready() noexcept { return true; }
  void await_suspend(std::coroutine_handle<>) noexcept {}
  SelfAwaiter await_resume() noexcept { return {}; }
};
SelfAwaiter ready();
#define S(x) co_await x
#define S2(x) S(S(x))
#define S8(x) S2(S2(S2(S2(x))))
#define S32(x) S8(S8(S8(S8(x))))
Task attributed() {
  [[clang::nomerge]] S32(S8(ready()));
  co_return 0;
}

// Which still sees one in an operand, once.
int Count;
Task commaInOperand() {
  if (co_await work((Count++, 1))) // expected-warning {{possible misuse of comma operator here}} expected-note {{cast expression to void to silence warning}}
    co_return 1;
  co_return 0;
}

Task self() {
  int Value = co_await work(Value); // expected-warning {{variable 'Value' is uninitialized when used within its own initialization}}
  co_return Value;
}

// Also an operand that is the awaiter itself, an lvalue.
Awaiter First, Second;
struct State {
  bool Flag;
};

Task lvalue() {
  State Value = ((void)[Copy = co_await (Value.Flag ? First : Second)] {}, State{}); // expected-warning {{variable 'Value' is uninitialized when used within its own initialization}}
  co_return 0;
}

// A visitor that stops at calls still finds a call in the operand, which the
// expression that makes the awaiter has in a call to operator co_await.
struct Awaitable {
  Awaiter operator co_await();
};
[[gnu::noinline]] Awaitable notInlined(); // expected-note {{conflicting attribute is here}}

Task attribute() {
  [[clang::always_inline]] co_await notInlined(); // expected-warning {{statement attribute 'clang::always_inline' has higher precedence than function attribute 'noinline'}}
  co_return 0;
}

// Also an operand that is a member of what is made with it, which a promise
// that takes awaiters by value copies, so only the copy is visited.
struct CopiedAwaiter {
  CopiedAwaiter(CopiedAwaiter const &);
  bool await_ready() noexcept { return true; }
  void await_suspend(std::coroutine_handle<>) noexcept {}
  int const &await_resume() noexcept;
};
struct Holder {
  CopiedAwaiter Read;
};
Holder load(int const &);
struct CopyingTask {
  struct promise_type {
    CopyingTask get_return_object() { return {}; }
    std::suspend_never initial_suspend() { return {}; }
    std::suspend_never final_suspend() noexcept { return {}; }
    void return_void() {}
    void unhandled_exception() {}
    template <typename AwaiterType>
    AwaiterType await_transform(AwaiterType Value) { return Value; }
  };
};
CopyingTask member() {
  int const &Value = co_await load(Value).Read; // expected-warning {{reference 'Value' is not yet bound to a value when used within its own initialization}}
  co_return;
}

// Also the base of a static member, which is evaluated to name it.
struct StaticHolder {
  static CopiedAwaiter Read;
};
StaticHolder loadStatic(int const &);
CopyingTask staticMember() {
  int const &Value = co_await loadStatic(Value).Read; // expected-warning {{reference 'Value' is not yet bound to a value when used within its own initialization}}
  co_return;
}
