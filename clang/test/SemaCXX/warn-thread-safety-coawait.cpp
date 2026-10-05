// RUN: %clang_cc1 -std=c++20 -fsyntax-only -Wthread-safety -verify %s -I %S/Inputs

// The object a co_await makes, which its await_resume call returns, is the
// one the variable it initializes holds, and destroys at the end of its scope,
// also a scoped lockable, which holds what await_resume acquires until then.

#include "std-coroutine.h"

struct __attribute__((capability("mutex"))) CMutex {};
extern CMutex g_Mutex;
extern int g_Guarded __attribute__((guarded_by(g_Mutex)));

struct __attribute__((scoped_lockable)) CLock {
  ~CLock() __attribute__((release_capability()));
};

struct CLockAwaiter {
  bool await_ready();
  void await_suspend(std::coroutine_handle<>);
  CLock await_resume() __attribute__((acquire_capability(g_Mutex)));
};

struct CHolder {
  CHolder() __attribute__((acquire_capability(g_Mutex)));
  ~CHolder() __attribute__((release_capability(g_Mutex)));
};

struct CHolderAwaiter {
  bool await_ready();
  void await_suspend(std::coroutine_handle<>);
  CHolder await_resume() __attribute__((acquire_capability(g_Mutex)));
};

struct CTask {
  struct promise_type {
    CTask get_return_object();
    std::suspend_never initial_suspend() noexcept;
    std::suspend_never final_suspend() noexcept;
    void return_void();
    void unhandled_exception();
  };
};

CTask fg_Reference() {
  {
    CLock const &Lock = co_await CLockAwaiter{};
    g_Guarded = 1;
  }
  g_Guarded = 2; // expected-warning {{writing variable 'g_Guarded' requires holding mutex 'g_Mutex' exclusively}}
}

CTask fg_Value() {
  {
    CLock Lock = co_await CLockAwaiter{};
    g_Guarded = 1;
  }
  g_Guarded = 2; // expected-warning {{writing variable 'g_Guarded' requires holding mutex 'g_Mutex' exclusively}}
}

// One that nothing holds is gone at the end of the full-expression.
CTask fg_Discarded() {
  co_await CLockAwaiter{};
  g_Guarded = 3; // expected-warning {{writing variable 'g_Guarded' requires holding mutex 'g_Mutex' exclusively}}
}

// And one whose destructor releases what it holds itself.
CTask fg_Holder() {
  {
    CHolder const &Holder = co_await CHolderAwaiter{};
    g_Guarded = 1;
  }
  g_Guarded = 2; // expected-warning {{writing variable 'g_Guarded' requires holding mutex 'g_Mutex' exclusively}}
}
