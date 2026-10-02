// RUN: %check_clang_tidy -std=c++23-or-later %s malterlib-coroutine-thread-local-reference %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml', malterlib-coroutine-thread-local-reference.ThreadLocalTypes: 'CData;NThreadLocals::CConcurrencyThreadLocal;NThreadLocals::CSystemThreadLocal;NThreadLocals::CThreadLocalObject;NThreadLocals::CR109YBytes'}}" -- -I %S/Inputs

// This file is generated.

#include "malterlib-actor-phase.h"

namespace NThreadLocals {
TCFuture<int> fg_Work();
TCFuture<int> fg_Index();

struct CItems {
  int m_Count;
  int f_Count(int _Value);
};

// Thread-local data, the types of ThreadLocalTypes, also one held by a
// `TCThreadLocal`.
struct CData {
  int m_Value;
  int *m_pValue;
  int m_Fixed[4];
  CItems m_Items;
  int &f_Value() { return *m_pValue; }
  int *f_Pointer() { return m_pValue; }
  CData &f_Self() { return *this; }
  int &f_Member() { return m_Fixed[0]; }
  int &f_Unseen();
  void f_Set(int *_pValue) { m_pValue = _pValue; }
  int f_Add(int _Value) { return m_Value + _Value; }
  int f_AddExplicit(this CData &_Self, int _Value) {
    return _Self.m_Value + _Value;
  }
  int &f_ExplicitValue(this CData &_Self) { return _Self.m_Value; }
  virtual int &f_Virtual() { return *m_pValue; }
};

template <typename t_CData> struct TCThreadLocal {
  t_CData *f_Get();
  // Of the thread named, not the data of the thread that calls it.
  t_CData *f_GetForThread(int _ThreadID);
  t_CData *operator->();
  // Its own implementation, which gives what it holds as it is.
  int *f_FirstValue() { return &f_Get()->m_Value; }
};

struct CSystemThreadLocal {
  int m_Value;
  CData &m_Data;
  CSystemThreadLocal &f_Self() { return *this; }
};
struct CConcurrencyThreadLocal {
  CConcurrencyThreadLocal();
  CSystemThreadLocal &m_SystemThreadLocal;
  int m_Value;
};

CConcurrencyThreadLocal &fg_ConcurrencyThreadLocal();
CSystemThreadLocal &fg_SystemThreadLocal();
// Of the thread named, not of the thread that calls it.
CSystemThreadLocal &fg_SystemThreadLocal(int _ThreadID);

template <typename t_CType> t_CType &&fg_Move(t_CType &_Value) {
  return static_cast<t_CType &&>(_Value);
}
void fg_Throws();
int fg_ReadPointer(int *const &_pValue);
int fg_ReadAddress(int *_pValue);
int fg_Pair(int *_pValue, int _Value);
// Gives the data by its type only, not one of ThreadLocalFunctions.
CData &fg_GetData();

// A thread-local type whose member functions use the object.
struct CThreadLocalObject {
  int m_Value;
  TCFuture<int> f_Read();
  TCFuture<int> f_ReadBefore();
  auto f_Lambda();
  int f_CalledInPlace();
  auto f_CopiedObject();
  auto f_Captured();
  auto f_NestedCopy();
  auto f_CopyAlias();
  auto f_R119NestedBorrow();
  auto f_R119NestedBorrowThis();
};

TCThreadLocal<CData> g_ThreadLocal;
thread_local CData t_Data;
thread_local int t_Value;
thread_local int t_Values[4];
int g_Value;

// An accessor of a part of it, of ThreadLocalFunctions, which gives it by
// another type.
thread_local CItems t_Random;
inline CItems &fg_RandomThreadLocal() { return t_Random; }

// A scope the coroutine runtime suspends and resumes with the coroutine, and
// one it doesn't (ThreadLocalScopeTypes).
struct CCoroutineThreadLocalHandler {
  virtual void f_Suspend() noexcept = 0;
  virtual void f_Resume() noexcept;
};
struct CDataScope : CCoroutineThreadLocalHandler {
  CDataScope(CConcurrencyThreadLocal &_ThreadLocal)
      : m_pThreadLocal(&_ThreadLocal) {}
  void f_Suspend() noexcept override;
  void f_Resume() noexcept override;
  CConcurrencyThreadLocal *m_pThreadLocal;
};
struct CCurrentActorScope {
  CCurrentActorScope(CConcurrencyThreadLocal &_ThreadLocal)
      : mp_ThreadLocal(_ThreadLocal) {}
  CConcurrencyThreadLocal &mp_ThreadLocal;
};
// A scope made as an aggregate, which restores what it keeps when it ends.
struct CCurrentlyProcessingActorScope {
  CConcurrencyThreadLocal &m_ThreadLocal;
  ~CCurrentlyProcessingActorScope();
};
CCurrentActorScope fg_MakeScope();

// Of fg_ConcurrencyThreadLocal().
TCFuture<int> fg_FunctionAlias() {
    auto &Alias = fg_ConcurrencyThreadLocal();
    co_await fg_Work();
    co_return Alias.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'Alias' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_FunctionPointer() {
    CConcurrencyThreadLocal *pData = &fg_ConcurrencyThreadLocal();
    co_await fg_Work();
    co_return pData->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pData' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_FunctionMember() {
    int &Value = fg_ConcurrencyThreadLocal().m_Value;
    co_await fg_Work();
    co_return Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'Value' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_FunctionMemberPointer() {
    int *pValue = &fg_ConcurrencyThreadLocal().m_Value;
    co_await fg_Work();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_FunctionCopy() {
    auto Copy = fg_ConcurrencyThreadLocal();
    co_await fg_Work();
    co_return Copy.m_Value;
}
TCFuture<int> fg_FunctionGottenAgain() {
    CConcurrencyThreadLocal *pData = &fg_ConcurrencyThreadLocal();
    co_await fg_Work();
    pData = &fg_ConcurrencyThreadLocal();
    co_return pData->m_Value;
}
TCFuture<int> fg_FunctionBefore() {
    auto &Alias = fg_ConcurrencyThreadLocal();
    int Value = Alias.m_Value;
    co_await fg_Work();
    co_return Value;
}
// Of (*g_ThreadLocal.f_Get()).
TCFuture<int> fg_GetAlias() {
    auto &Alias = (*g_ThreadLocal.f_Get());
    co_await fg_Work();
    co_return Alias.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'Alias' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_GetPointer() {
    CData *pData = &(*g_ThreadLocal.f_Get());
    co_await fg_Work();
    co_return pData->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pData' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_GetMember() {
    int &Value = (*g_ThreadLocal.f_Get()).m_Value;
    co_await fg_Work();
    co_return Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'Value' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_GetMemberPointer() {
    int *pValue = &(*g_ThreadLocal.f_Get()).m_Value;
    co_await fg_Work();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_GetCopy() {
    auto Copy = (*g_ThreadLocal.f_Get());
    co_await fg_Work();
    co_return Copy.m_Value;
}
TCFuture<int> fg_GetGottenAgain() {
    CData *pData = &(*g_ThreadLocal.f_Get());
    co_await fg_Work();
    pData = &(*g_ThreadLocal.f_Get());
    co_return pData->m_Value;
}
TCFuture<int> fg_GetBefore() {
    auto &Alias = (*g_ThreadLocal.f_Get());
    int Value = Alias.m_Value;
    co_await fg_Work();
    co_return Value;
}
// Of (*g_ThreadLocal.operator->()).
TCFuture<int> fg_ArrowAlias() {
    auto &Alias = (*g_ThreadLocal.operator->());
    co_await fg_Work();
    co_return Alias.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'Alias' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_ArrowPointer() {
    CData *pData = &(*g_ThreadLocal.operator->());
    co_await fg_Work();
    co_return pData->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pData' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_ArrowMember() {
    int &Value = (*g_ThreadLocal.operator->()).m_Value;
    co_await fg_Work();
    co_return Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'Value' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_ArrowMemberPointer() {
    int *pValue = &(*g_ThreadLocal.operator->()).m_Value;
    co_await fg_Work();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_ArrowCopy() {
    auto Copy = (*g_ThreadLocal.operator->());
    co_await fg_Work();
    co_return Copy.m_Value;
}
TCFuture<int> fg_ArrowGottenAgain() {
    CData *pData = &(*g_ThreadLocal.operator->());
    co_await fg_Work();
    pData = &(*g_ThreadLocal.operator->());
    co_return pData->m_Value;
}
TCFuture<int> fg_ArrowBefore() {
    auto &Alias = (*g_ThreadLocal.operator->());
    int Value = Alias.m_Value;
    co_await fg_Work();
    co_return Value;
}
// Of t_Data.
TCFuture<int> fg_KeywordAlias() {
    auto &Alias = t_Data;
    co_await fg_Work();
    co_return Alias.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'Alias' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_KeywordPointer() {
    CData *pData = &t_Data;
    co_await fg_Work();
    co_return pData->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pData' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_KeywordMember() {
    int &Value = t_Data.m_Value;
    co_await fg_Work();
    co_return Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'Value' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_KeywordMemberPointer() {
    int *pValue = &t_Data.m_Value;
    co_await fg_Work();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_KeywordCopy() {
    auto Copy = t_Data;
    co_await fg_Work();
    co_return Copy.m_Value;
}
TCFuture<int> fg_KeywordGottenAgain() {
    CData *pData = &t_Data;
    co_await fg_Work();
    pData = &t_Data;
    co_return pData->m_Value;
}
TCFuture<int> fg_KeywordBefore() {
    auto &Alias = t_Data;
    int Value = Alias.m_Value;
    co_await fg_Work();
    co_return Value;
}

// A parameter of a reference or a pointer to a thread-local type refers to
// the data of the thread that called it.
TCFuture<int> fg_ReferenceParameter(CData &_Data) {
    co_await fg_Work();
    co_return _Data.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: '_Data' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_PointerParameter(CData *_pData) {
    co_await fg_Work();
    co_return _pData->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: '_pData' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_ParameterBefore(CData &_Data) {
    int Value = _Data.m_Value;
    co_await fg_Work();
    co_return Value;
}

// What a member function of it gives by reference or pointer is in it
// (objectRelation), also of another type, not one whose body gives what a
// pointer it holds points to.
TCFuture<int> fg_AccessorOfOtherType() {
    int &Value = t_Data.f_Value();
    co_await fg_Work();
    co_return Value;
}
TCFuture<int> fg_AccessorOfMember() {
    int &Value = t_Data.f_Member();
    co_await fg_Work();
    co_return Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'Value' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_AccessorUnseen() {
    int &Value = t_Data.f_Unseen();
    co_await fg_Work();
    co_return Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'Value' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_AccessorOfType() {
    CData &Self = t_Data.f_Self();
    co_await fg_Work();
    co_return Self.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'Self' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}

// Also what an accessor of ThreadLocalFunctions gives by another type.
TCFuture<int> fg_PartAccessor() {
    auto &Random = fg_RandomThreadLocal();
    co_await fg_Work();
    co_return Random.m_Count;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'Random' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}

// Through local aliases, also of a pointer to a part of it.
TCFuture<int> fg_AliasOfAlias() {
    auto &Data = t_Data;
    int *pFixed = Data.m_Fixed;
    co_await fg_Work();
    co_return pFixed[1];
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pFixed' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_AssignedLater() {
    int *pValue = &g_Value;
    pValue = &t_Value;
    co_await fg_Work();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_AssignedElsewhere() {
    int *pValue = &t_Value;
    co_await fg_Work();
    pValue = &g_Value;
    co_return *pValue;
}
TCFuture<int> fg_AssignedInBranch(bool _bOther) {
    int *pValue = &g_Value;
    if (_bOther)
      pValue = &t_Value;
    co_await fg_Work();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}

// What is gotten before a suspension in the same expression, also of what
// is awaited, is of the thread before, not what an assignment's right side
// suspends before its left side gets.
TCFuture<int> fg_GottenBeforeAwait() {
    int *pValue = (&t_Value, co_await fg_Work(), &t_Value);
    co_return *pValue;
}
TCFuture<int> fg_AssignedAfterAwait() {
    int &Value = (t_Value = co_await fg_Work());
    co_return Value;
}

// A loop over it goes on after a suspension in it.
TCFuture<int> fg_Loop() {
    int Total = 0;
    for (int &Value : t_Data.m_Fixed) {
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: the loop goes on over thread-local data of the thread that ran before a suspension; the coroutine can resume on another thread; loop over a copy, or get it again after the suspension [malterlib-coroutine-thread-local-reference]
      co_await fg_Work();
      Total += Value;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'Value' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
    }
    co_return Total;
}

// A handler that catches what the suspension throws.
TCFuture<int> fg_Handler() {
    int *pValue = &t_Value;
    try {
      co_await fg_Work();
    } catch (...) {
      co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: 'pValue' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
    }
    co_return 0;
}

// A lambda that is a coroutine, of what it captures, and of one whose
// promise suspends before its body runs, from the start: how a coroutine
// starts is told by the type of what `initial_suspend` returns,
// `std::suspend_always`, or by what its `await_ready()` gives where that is a
// constant expression.
struct CLazy {
  struct promise_type {
    CLazy get_return_object();
    std::suspend_always initial_suspend();
    std::suspend_always final_suspend() noexcept;
    void return_void();
    void unhandled_exception();
  };
};
struct CSuspendAlways {
  bool await_ready() const noexcept { return false; }
  void await_suspend(std::coroutine_handle<>) const noexcept {}
  void await_resume() const noexcept {}
};
struct CLazyByMember {
  struct promise_type {
    CLazyByMember get_return_object();
    CSuspendAlways initial_suspend();
    CSuspendAlways final_suspend() noexcept;
    void return_void();
    void unhandled_exception();
  };
};
inline auto fg_CapturedPointer() {
  int *pValue = &t_Value;
  return [pValue]() -> TCFuture<int> {
    co_await fg_Work();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
  };
}
inline auto fg_CapturedLazy() {
  int *pValue = &t_Value;
  return [pValue]() -> CLazy {
    ++*pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: 'pValue' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
    co_return;
  };
}
// An awaiter whose `await_ready()` gives a constant false suspends before the
// body runs.
inline auto fg_CapturedLazyByMember() {
  int *pValue = &t_Value;
  return [pValue]() -> CLazyByMember {
    ++*pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: 'pValue' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
    co_return;
  };
}
inline auto fg_CapturedCopy() {
  auto &Data = t_Data;
  return [Data]() -> TCFuture<int> {
    co_await fg_Work();
    co_return Data.m_Value;
  };
}

// A scope that keeps it, not one the runtime moves, alive across a
// suspension.
TCFuture<int> fg_ScopeAcross() {
    CCurrentActorScope Scope(fg_ConcurrencyThreadLocal());
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: 'Scope' keeps thread-local data of the thread that made it and is alive across a suspension; the coroutine can resume on another thread; end its scope before the suspension [malterlib-coroutine-thread-local-reference]
    co_await fg_Work();
    co_return 0;
}
TCFuture<int> fg_ScopeEndsBefore() {
    {
      CCurrentActorScope Scope(fg_ConcurrencyThreadLocal());
    }
    co_await fg_Work();
    co_return 0;
}
TCFuture<int> fg_HandlerScope() {
    CDataScope Scope(fg_ConcurrencyThreadLocal());
    co_await fg_Work();
    co_return 0;
}

// Round 95: a scope alive across a suspension in an initializer, declared in
// what a statement starts with, or a temporary a reference extends; not one
// whose suspension can't be reached.
TCFuture<int> fg_ScopeAwaitInDeclaration() {
    CCurrentActorScope Scope(fg_ConcurrencyThreadLocal());
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: 'Scope' keeps thread-local data of the thread that made it and is alive across a suspension; the coroutine can resume on another thread; end its scope before the suspension [malterlib-coroutine-thread-local-reference]
    int Value = co_await fg_Work();
    co_return Value;
}
TCFuture<int> fg_ScopeInIf(bool _bRun) {
    if (CCurrentActorScope Scope(fg_ConcurrencyThreadLocal()); _bRun) {
// CHECK-MESSAGES: :[[@LINE-1]]:28: warning: 'Scope' keeps thread-local data of the thread that made it and is alive across a suspension; the coroutine can resume on another thread; end its scope before the suspension [malterlib-coroutine-thread-local-reference]
      co_await fg_Work();
    }
    co_return 0;
}
TCFuture<int> fg_ScopeInFor() {
    for (CCurrentActorScope Scope(fg_ConcurrencyThreadLocal()); g_Value < 1;) {
// CHECK-MESSAGES: :[[@LINE-1]]:29: warning: 'Scope' keeps thread-local data of the thread that made it and is alive across a suspension; the coroutine can resume on another thread; end its scope before the suspension [malterlib-coroutine-thread-local-reference]
      co_await fg_Work();
    }
    co_return 0;
}
TCFuture<int> fg_ScopeInSwitch() {
    switch (CCurrentActorScope Scope(fg_ConcurrencyThreadLocal()); g_Value) {
// CHECK-MESSAGES: :[[@LINE-1]]:32: warning: 'Scope' keeps thread-local data of the thread that made it and is alive across a suspension; the coroutine can resume on another thread; end its scope before the suspension [malterlib-coroutine-thread-local-reference]
    case 0:
      co_await fg_Work();
      break;
    }
    co_return 0;
}
TCFuture<int> fg_ScopeExtended() {
    auto const &Scope = CCurrentActorScope(fg_ConcurrencyThreadLocal());
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: 'Scope' keeps thread-local data of the thread that made it and is alive across a suspension; the coroutine can resume on another thread; end its scope before the suspension [malterlib-coroutine-thread-local-reference]
    co_await fg_Work();
    co_return 0;
}
TCFuture<int> fg_ScopeBorrowed(CCurrentActorScope &_Scope) {
    auto &Scope = _Scope;
    co_await fg_Work();
    co_return 0;
}
TCFuture<int> fg_ScopeUnreachableAwait() {
    CCurrentActorScope Scope(fg_ConcurrencyThreadLocal());
    co_return 0;
    co_await fg_Work();
}
TCFuture<int> fg_ScopeInDiscarded() {
    if constexpr (false) {
      CCurrentActorScope Scope(fg_ConcurrencyThreadLocal());
      co_await fg_Work();
    }
    co_return 0;
}
TCFuture<int> fg_ScopeInFalse() {
    if (false) {
      CCurrentActorScope Scope(fg_ConcurrencyThreadLocal());
      co_await fg_Work();
    }
    co_return 0;
}

// The object of a member function of a thread-local type, also that a lambda
// captures.
TCFuture<int> CThreadLocalObject::f_Read() {
    co_await fg_Work();
    co_return m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the object refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> CThreadLocalObject::f_ReadBefore() {
    int Value = m_Value;
    co_await fg_Work();
    co_return Value;
}
inline auto CThreadLocalObject::f_Lambda() {
  return [this]() -> TCFuture<int> {
    co_await fg_Work();
    co_return m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the object refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
  };
}

// Through a local reference to a local pointer, a value-preserving cast, a
// structured binding, a move or an address-of helper, a braced initializer,
// and a local array of pointers.
TCFuture<int> fg_ReadThroughPointerReference() {
    int *pValue = &t_Value;
    int *&rpValue = pValue;
    co_await fg_Work();
    co_return *rpValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'rpValue' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_WriteThroughPointerReference() {
    int *pValue = nullptr;
    int *&rpValue = pValue;
    rpValue = &t_Value;
    co_await fg_Work();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_ReadThroughCast() {
    int *pValue = &t_Value;
    int *pCopy = static_cast<int *const &>(pValue);
    co_await fg_Work();
    co_return *pCopy;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pCopy' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_StructuredBinding() {
    auto &[Value, pValue, Fixed, Items] = t_Data;
    co_await fg_Work();
    co_return Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'Value' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_StructuredBindingCopy() {
    auto [Value, pValue, Fixed, Items] = t_Data;
    co_await fg_Work();
    co_return Value;
}
TCFuture<int> fg_MovedPointer() {
    int *pValue = &t_Value;
    int *pMoved = fg_Move(pValue);
    co_await fg_Work();
    co_return *pMoved;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pMoved' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_AddressOfHelper() {
    int *pValue = __builtin_addressof(t_Value);
    co_await fg_Work();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_BracedPointer() {
    int *pValue{&t_Value};
    co_await fg_Work();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_BracedReference() {
    int &Value{t_Value};
    co_await fg_Work();
    co_return Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'Value' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_PointerArray() {
    int *Values[1] = {&t_Value};
    co_await fg_Work();
    co_return *Values[0];
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'Values' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_IndexAwaited() {
    int &Value = t_Data.m_Fixed[co_await fg_Work()];
    co_return Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'Value' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_CommaAwaitedThenGotten() {
    int *pValue = (co_await fg_Work(), &t_Value);
    co_await fg_Work();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}

// A copy is what its source is where it is copied; an assignment through a
// reference doesn't rebind it; an alias cycle; not a branch a constant
// doesn't take; not what a thread named gives.
TCFuture<int> fg_CopyOfReassigned() {
    int *pValue = &t_Value;
    pValue = &g_Value;
    int *pCopy = pValue;
    co_await fg_Work();
    co_return *pCopy;
}
TCFuture<int> fg_AssignThroughReference() {
    int &Value = t_Value;
    Value = 1;
    co_await fg_Work();
    co_return Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'Value' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_AliasCycle() {
    int *pB = nullptr;
    int *pA = nullptr;
    pA = &t_Value;
    pB = pA;
    pA = pB;
    co_await fg_Work();
    co_return *pB;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pB' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_ConstantConditional() {
    int *pValue = true ? &g_Value : &t_Value;
    co_await fg_Work();
    co_return *pValue;
}
TCFuture<int> fg_ExplicitThread(int _ThreadID) {
    auto &Data = fg_SystemThreadLocal(_ThreadID);
    int *pValue = &fg_SystemThreadLocal(_ThreadID).m_Value;
    co_await fg_Work();
    co_return Data.m_Value + *pValue;
}
TCFuture<int> fg_AssignedParameter(int *_pValue) {
    _pValue = &t_Value;
    co_await fg_Work();
    co_return *_pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: '_pValue' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}

// A loop over it whose iterators are classes.
struct CRange {
  struct CIterator {
    int *m_pValue;
    int &operator*() const;
    CIterator &operator++();
    bool operator!=(CIterator const &) const;
  };
  CIterator begin();
  CIterator end();
};
thread_local CRange t_Range;
TCFuture<int> fg_ClassIteratorLoop() {
    int Total = 0;
    for (int Value : t_Range) {
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: the loop goes on over thread-local data of the thread that ran before a suspension; the coroutine can resume on another thread; loop over a copy, or get it again after the suspension [malterlib-coroutine-thread-local-reference]
      co_await fg_Work();
      Total += Value;
    }
    co_return Total;
}
TCFuture<int> fg_ClassIteratorLoopNoSuspension() {
    int Total = 0;
    for (int Value : t_Range)
      Total += Value;
    co_await fg_Work();
    co_return Total;
}

// What is assigned before a try doesn't reach its handler.
TCFuture<int> fg_AssignedBeforeTry() {
    int *pValue = &t_Value;
    co_await fg_Work();
    pValue = &g_Value;
    try {
      fg_Throws();
    } catch (...) {
      co_return *pValue;
    }
    co_return 0;
}

// Round 96: through a local reference to a local pointer, also of a
// thread-local type, captured by reference, copied or as an initializer, and
// a parameter of a reference to a pointer captured by copy; a structured
// binding captured.
TCFuture<int> fg_PointerReferenceOfType() {
    CData *pData = &t_Data;
    CData *&rpData = pData;
    co_await fg_Work();
    co_return rpData->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'rpData' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
inline auto fg_CapturedPointerReference() {
  int *pValue = &t_Value;
  int *&rpValue = pValue;
  return [&rpValue]() -> TCFuture<int> {
    co_await fg_Work();
    co_return *rpValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'rpValue' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
  };
}
inline auto fg_CopiedPointerReference() {
  int *pValue = &t_Value;
  int *&rpValue = pValue;
  return [rpValue]() -> TCFuture<int> {
    co_await fg_Work();
    co_return *rpValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'rpValue' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
  };
}
inline auto fg_InitPointerReference() {
  int *pValue = &t_Value;
  return [&rpValue = pValue]() -> TCFuture<int> {
    co_await fg_Work();
    co_return *rpValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'rpValue' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
  };
}
inline auto fg_CopiedReferenceParameter(CData *const &_pData) {
  return [_pData]() -> TCFuture<int> {
    co_await fg_Work();
    co_return _pData->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: '_pData' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
  };
}
inline auto fg_CapturedBinding() {
  auto &[Value, pValue, Fixed, Items] = t_Data;
  return [&Value]() -> TCFuture<int> {
    co_await fg_Work();
    co_return Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'Value' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
  };
}

// Writes through a reference to a local array, assignments as values, and
// pointers stepped, moved or given by reference, which are uses; through a
// local pointer to a local pointer; a parameter assigned and copied.
TCFuture<int> fg_ArrayReferenceWrite() {
    int *pValues[1] = {nullptr};
    auto &Values = pValues;
    Values[0] = &t_Value;
    co_await fg_Work();
    co_return *pValues[0];
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValues' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_AssignmentValue() {
    int *pValue = nullptr;
    int *pOther = (pValue = &t_Value);
    co_await fg_Work();
    co_return *pOther;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pOther' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_PostIncrementUse() {
    int *pValue = t_Values;
    co_await fg_Work();
    co_return *pValue++;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_PreIncrementUse() {
    int *pValue = t_Values;
    co_await fg_Work();
    co_return *++pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: 'pValue' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_CompoundUse() {
    int *pValue = t_Values;
    co_await fg_Work();
    co_return *(pValue += 1);
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: 'pValue' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_MovedUse() {
    int *pValue = &t_Value;
    co_await fg_Work();
    co_return *fg_Move(pValue);
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: 'pValue' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_ByReferenceUse() {
    int *pValue = &t_Value;
    co_await fg_Work();
    co_return fg_ReadPointer(pValue);
// CHECK-MESSAGES: :[[@LINE-1]]:30: warning: 'pValue' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_IncrementedCopy() {
    int *pValue = t_Values;
    int *pOld = pValue++;
    co_await fg_Work();
    co_return *pOld;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pOld' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_PointerToPointer() {
    int *pValue = &t_Value;
    int **ppValue = &pValue;
    co_await fg_Work();
    co_return **ppValue;
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: 'ppValue' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_AssignedParameterCopy(int *_pValue) {
    _pValue = &t_Value;
    int *pCopy = _pValue;
    co_await fg_Work();
    co_return *pCopy;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pCopy' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}

// Not a copy of what a conditional gives of what no longer points into it,
// nor what a thread named gives through a reference member. Each value of a
// conditional is followed on its own path, so one that suspends doesn't make
// the other stale; a loop over the data with a suspension in it goes on after
// it.
TCFuture<int> fg_ConditionalCopy(bool _bFlag) {
    int *pValue = &t_Value;
    pValue = &g_Value;
    int *pCopy = _bFlag ? pValue : &g_Value;
    co_await fg_Work();
    co_return *pCopy;
}
TCFuture<int> fg_ExclusiveValues(bool _bChoose) {
    int *pValue = _bChoose ? &t_Value : (co_await fg_Work(), nullptr);
    if (pValue)
      co_return *pValue;
    co_return 0;
}
TCFuture<int> fg_LoopReturns() {
    for (int Value : t_Values) {
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: the loop goes on over thread-local data of the thread that ran before a suspension; the coroutine can resume on another thread; loop over a copy, or get it again after the suspension [malterlib-coroutine-thread-local-reference]
      co_await fg_Work();
      co_return Value;
    }
    co_return 0;
}
TCFuture<int> fg_LoopBreaks() {
    int Total = 0;
    for (int Value : t_Values) {
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: the loop goes on over thread-local data of the thread that ran before a suspension; the coroutine can resume on another thread; loop over a copy, or get it again after the suspension [malterlib-coroutine-thread-local-reference]
      co_await fg_Work();
      Total += Value;
      break;
    }
    co_return Total;
}
TCFuture<int> fg_ExplicitThreadMember(int _ThreadID) {
    auto &Data = fg_SystemThreadLocal(_ThreadID).m_Data;
    co_await fg_Work();
    co_return Data.m_Value;
}

// A temporary scope alive across a suspension in its full expression, and a
// scope the coroutine is given.
TCFuture<int> fg_ScopeTemporary() {
    co_return (CCurrentActorScope(fg_ConcurrencyThreadLocal()), co_await fg_Work());
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: a temporary 'CCurrentActorScope' keeps thread-local data of the thread that made it and is alive across a suspension; the coroutine can resume on another thread; end its scope before the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_ScopeParameter(CCurrentActorScope _Scope) {
// CHECK-MESSAGES: :[[@LINE-1]]:52: warning: '_Scope' keeps thread-local data of the thread that made it and is alive across a suspension; the coroutine can resume on another thread; end its scope before the suspension [malterlib-coroutine-thread-local-reference]
    co_await fg_Work();
    co_return 0;
}

// Round 97: a reference to a pointer slot of the data, which is the data
// itself; a write through a local pointer to a local pointer; a pointer
// refreshed through a local reference to it, which is no use; the address of
// what a reference refers to given on; a copy captured by what it is given
// where the lambda is made; what a thread named gives; what a call gives by
// its type before a suspension in the same expression.
TCFuture<int> fg_SlotReference() {
    int *&rpSlot = t_Data.m_pValue;
    co_await fg_Work();
    rpSlot = &g_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: 'rpSlot' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
    co_return 0;
}
TCFuture<int> fg_WriteThroughPointerAlias() {
    int *pValue = nullptr;
    int **ppValue = &pValue;
    *ppValue = &t_Value;
    co_await fg_Work();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_RefreshThroughReference() {
    int *pValue = &t_Value;
    int *&rpValue = pValue;
    co_await fg_Work();
    rpValue = &t_Value;
    co_return *pValue;
}
TCFuture<int> fg_AddressGivenOn() {
    int &Value = t_Value;
    co_await fg_Work();
    co_return fg_ReadAddress(&Value);
// CHECK-MESSAGES: :[[@LINE-1]]:31: warning: 'Value' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
inline auto fg_CopyAtCapture() {
  int *pValue = &t_Value;
  pValue = &g_Value;
  return [pValue]() -> TCFuture<int> {
    co_await fg_Work();
    co_return *pValue;
  };
}
TCFuture<int> fg_ForThread(int _ThreadID) {
    CData *pData = g_ThreadLocal.f_GetForThread(_ThreadID);
    co_await fg_Work();
    co_return pData->m_Value;
}
TCFuture<int> fg_TypedBeforeAwait() {
    int *pValue = &fg_GetData().m_Fixed[co_await fg_Work()];
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}

// Scopes: a parameter of a coroutine that suspends before its body runs, one
// a lambda captures, a temporary in what a capture is made of or in what is
// awaited, also in the other value of a conditional, as a whole.
CLazy fg_LazyScopeParameter(CCurrentActorScope _Scope) {
// CHECK-MESSAGES: :[[@LINE-1]]:48: warning: '_Scope' keeps thread-local data of the thread that made it and is alive across a suspension; the coroutine can resume on another thread; end its scope before the suspension [malterlib-coroutine-thread-local-reference]
  co_return;
}
inline auto fg_CapturedScope() {
  return [Scope = CCurrentActorScope(fg_ConcurrencyThreadLocal())]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: 'Scope' keeps thread-local data of the thread that made it and is alive across a suspension; the coroutine can resume on another thread; end its scope before the suspension [malterlib-coroutine-thread-local-reference]
    co_await fg_Work();
    co_return 0;
  };
}
TCFuture<int> fg_ScopeInCapture() {
    auto fRead = [Value = (CCurrentActorScope(fg_ConcurrencyThreadLocal()), co_await fg_Work())] { return Value; };
// CHECK-MESSAGES: :[[@LINE-1]]:28: warning: a temporary 'CCurrentActorScope' keeps thread-local data of the thread that made it and is alive across a suspension; the coroutine can resume on another thread; end its scope before the suspension [malterlib-coroutine-thread-local-reference]
    co_return fRead();
}
TCFuture<int> fg_ScopeInAwaited() {
    co_return co_await (CCurrentActorScope(fg_ConcurrencyThreadLocal()), fg_Work());
// CHECK-MESSAGES: :[[@LINE-1]]:25: warning: a temporary 'CCurrentActorScope' keeps thread-local data of the thread that made it and is alive across a suspension; the coroutine can resume on another thread; end its scope before the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_ScopeOtherValue(bool _bReady) {
    co_return _bReady ? (CCurrentActorScope(fg_ConcurrencyThreadLocal()), 1) : co_await fg_Work();
}

// Round 98: temporary scopes only where they can be made, also aggregates,
// what a call gives, extended through a cast, and copied through a reference
// a lambda captures; not one on the left of an assignment whose right side
// suspends.
TCFuture<int> fg_ScopeTemporaryCantRun() {
    if (false)
      (CCurrentActorScope(fg_ConcurrencyThreadLocal()), co_await fg_Work());
    co_return 0;
    (CCurrentActorScope(fg_ConcurrencyThreadLocal()), co_await fg_Work());
}
TCFuture<int> fg_ScopeAggregateTemporary() {
    co_return co_await (CCurrentlyProcessingActorScope{fg_ConcurrencyThreadLocal()}, fg_Work());
// CHECK-MESSAGES: :[[@LINE-1]]:25: warning: a temporary 'CCurrentlyProcessingActorScope' keeps thread-local data of the thread that made it and is alive across a suspension; the coroutine can resume on another thread; end its scope before the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_ScopeGivenTemporary() {
    co_return (fg_MakeScope(), co_await fg_Work());
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: a temporary 'CCurrentActorScope' keeps thread-local data of the thread that made it and is alive across a suspension; the coroutine can resume on another thread; end its scope before the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_ScopeExtendedCast() {
    auto &&Scope = static_cast<CCurrentActorScope &&>(CCurrentActorScope(fg_ConcurrencyThreadLocal()));
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: 'Scope' keeps thread-local data of the thread that made it and is alive across a suspension; the coroutine can resume on another thread; end its scope before the suspension [malterlib-coroutine-thread-local-reference]
    co_await fg_Work();
    co_return 0;
}
inline auto fg_CapturedScopeReference(CCurrentActorScope &_Scope) {
  auto &Alias = _Scope;
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: 'Alias' keeps thread-local data of the thread that made it and is alive across a suspension; the coroutine can resume on another thread; end its scope before the suspension [malterlib-coroutine-thread-local-reference]
  return [Alias]() -> TCFuture<int> {
    co_await fg_Work();
    co_return 0;
  };
}
TCFuture<int> fg_ScopeAssignedAfter() {
    int Value = 0;
    (CCurrentActorScope(fg_ConcurrencyThreadLocal()), Value) = co_await fg_Work();
    co_return Value;
}

// What is reached through what was named before a suspension in the same
// expression; a refresh through a local pointer to a pointer, and of a
// parameter before its copy; what a thread named gives through a getter of
// itself; arrays of arrays of pointers; a getter an override can change.
TCFuture<int> fg_SubscriptAfterAwait() {
    int *pValue = t_Values;
    co_return pValue[co_await fg_Index()];
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pValue' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_ReceiverAfterAwait() {
    CData &Data = t_Data;
    co_return Data.f_Add(co_await fg_Work());
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'Data' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_PointerReceiverAfterAwait() {
    CData *pData = &t_Data;
    co_return pData->f_Add(co_await fg_Work());
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pData' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_RefreshThroughPointerAlias() {
    int *pValue = &t_Value;
    int **ppValue = &pValue;
    co_await fg_Work();
    *ppValue = &t_Value;
    co_return *pValue;
}
TCFuture<int> fg_ParameterRefreshedCopy(int *_pValue) {
    _pValue = &t_Value;
    co_await fg_Work();
    _pValue = &g_Value;
    int *pCopy = _pValue;
    co_await fg_Work();
    co_return *pCopy;
}
TCFuture<int> fg_ExplicitThreadSelf(int _ThreadID) {
    auto &Data = fg_SystemThreadLocal(_ThreadID).f_Self();
    co_await fg_Work();
    co_return Data.m_Value;
}
TCFuture<int> fg_ArrayOfArrays() {
    int *pValues[1][1]{{&t_Value}};
    co_await fg_Work();
    co_return *pValues[0][0];
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValues' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_VirtualGetter() {
    CData &Data = t_Data;
    int &Value = Data.f_Virtual();
    co_await fg_Work();
    co_return Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'Value' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}

// Each use the effect table tells of a pointer that refers to the data of the
// thread before a suspension (EUse): what reads, writes, calls on, decides by,
// computes with or gives on what it points to uses it; what only gives the
// pointer on, takes its address, drops it, or doesn't evaluate it doesn't,
// and what gives it to a tracked variable or binds an alias of it is followed.
TCFuture<int> fg_UseWrite() {
    int *pValue = &t_Value;
    co_await fg_Work();
    *pValue = 1;
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: 'pValue' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
    co_return 0;
}
TCFuture<int> fg_UseCondition() {
    int *pValue = &t_Value;
    co_await fg_Work();
    if (pValue)
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: 'pValue' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
      co_return 1;
    co_return 0;
}
TCFuture<int> fg_UseValue() {
    int *pValue = &t_Value;
    co_await fg_Work();
    co_return pValue != nullptr;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pValue' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_UseCopiedToParameter() {
    int *pValue = &t_Value;
    co_await fg_Work();
    co_return fg_ReadAddress(pValue);
// CHECK-MESSAGES: :[[@LINE-1]]:30: warning: 'pValue' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_UseBoundToParameter() {
    int *pValue = &t_Value;
    co_await fg_Work();
    co_return fg_ReadPointer(pValue);
// CHECK-MESSAGES: :[[@LINE-1]]:30: warning: 'pValue' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_UseCaptured() {
    int *pValue = &t_Value;
    co_await fg_Work();
    auto fRead = [pValue] { return *pValue; };
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: 'pValue' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
    co_return fRead();
}
TCFuture<int> fg_NoUseCopiedToTracked() {
    int *pValue = &t_Value;
    co_await fg_Work();
    int *pCopy = pValue;
    co_return *pCopy;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pCopy' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_NoUseAliasRefreshed() {
    int *pValue = &t_Value;
    co_await fg_Work();
    int *&rpValue = pValue;
    rpValue = &t_Value;
    co_return *pValue;
}
TCFuture<int> fg_NoUseAddressDiscardedUnevaluated() {
    int *pValue = &t_Value;
    co_await fg_Work();
    int **ppValue = &pValue;
    (void)ppValue;
    (void)pValue;
    co_return sizeof(*pValue);
}

// What gives the data, used after a suspension later in its own expression,
// as C++17 sequences it: a receiver before the arguments, the pointer of a
// subscript before the index, the arguments of a call in any order; not the
// left of an assignment, after its right side, nor what is gotten after the
// suspension.
TCFuture<int> fg_RootReceiver() {
    co_return fg_ConcurrencyThreadLocal().m_SystemThreadLocal.m_Data.f_Add(co_await fg_Work());
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: thread-local data gotten here is used after a suspension later in its expression; the coroutine can resume on another thread; get it after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_RootReceiverByType() {
    co_return fg_GetData().f_Add(co_await fg_Work());
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: thread-local data gotten here is used after a suspension later in its expression; the coroutine can resume on another thread; get it after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_RootSubscript() {
    co_return t_Values[co_await fg_Index()];
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: thread-local data gotten here is used after a suspension later in its expression; the coroutine can resume on another thread; get it after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_RootMemberSubscript() {
    co_return t_Data.m_Fixed[co_await fg_Index()];
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: thread-local data gotten here is used after a suspension later in its expression; the coroutine can resume on another thread; get it after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_RootUnsequencedArguments() {
    co_return fg_Pair(&t_Value, co_await fg_Work());
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: thread-local data gotten here is used after a suspension later in its expression; the coroutine can resume on another thread; get it after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_RootAfterSuspension() {
    co_return fg_Pair((co_await fg_Work(), &t_Value), 1);
}
TCFuture<int> fg_RootAssignedAfter() {
    t_Value = co_await fg_Work();
    co_return 0;
}

// Round 99: the receiver of a member function of a part of the data, of an
// explicit object parameter, a pointer read before the suspension of its own
// index whose variable is refreshed in it, a nested array of pointers given an
// element, arrays and closures of scopes, a copy of the object, and a local
// reference to a pointer that refreshes it.
struct CCounter {
  int &f_Counter() {
    static thread_local int s_Value;
    return s_Value;
  }
};
CCounter g_Counter;
TCFuture<int> fg_GetterOfThreadLocal() {
    int &Counter = g_Counter.f_Counter();
    co_await fg_Work();
    co_return Counter;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'Counter' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_PartReceiver() {
    auto &Data = fg_GetData();
    co_return Data.m_Items.f_Count(co_await fg_Work());
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'Data' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_ExplicitObjectReceiver() {
    auto &Data = fg_GetData();
    co_return Data.f_AddExplicit(co_await fg_Work());
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'Data' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_LoadedBeforeRefresh() {
    int *pValue = t_Data.m_Fixed;
    co_return pValue[(co_await fg_Work(), pValue = t_Data.m_Fixed, 0)];
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pValue' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_NestedArrayElement() {
    int *pValues[1][1]{};
    pValues[0][0] = &t_Value;
    co_await fg_Work();
    co_return *pValues[0][0];
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValues' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_ScopeArray() {
    CCurrentActorScope Scopes[1]{fg_ConcurrencyThreadLocal()};
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: 'Scopes' keeps thread-local data of the thread that made it and is alive across a suspension; the coroutine can resume on another thread; end its scope before the suspension [malterlib-coroutine-thread-local-reference]
    co_await fg_Work();
    co_return 0;
}
TCFuture<int> fg_ScopeInClosure() {
    auto fScope = [Scope = CCurrentActorScope(fg_ConcurrencyThreadLocal())] {};
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: 'fScope' keeps thread-local data of the thread that made it and is alive across a suspension; the coroutine can resume on another thread; end its scope before the suspension [malterlib-coroutine-thread-local-reference]
    co_await fg_Work();
    fScope();
    co_return 0;
}
auto CThreadLocalObject::f_CopiedObject() {
  return [*this]() -> TCFuture<int> {
    auto const &Value = m_Value;
    co_await fg_Work();
    co_return Value;
  };
}

// Round 100: what a default argument evaluates, also a scope; the arguments
// of a call in either order around a suspension, also a scope and a default;
// a pointer stepped after a suspension, also discarded, and what a step gives;
// not a thread-local pointer stepped, which points elsewhere, nor a pointer
// to a tracked pointer, which doesn't read it; a conditional's value fresh
// in each iteration; a value given to either of two pointers; a declaration
// of several; scopes that end with their expression; not a handler that
// holds a scope; an array captured through a reference; not a getter of a
// copy.
TCFuture<int> fg_DefaultScoped(CCurrentActorScope const &_Scope = CCurrentActorScope(fg_ConcurrencyThreadLocal()));
TCFuture<int> fg_DefaultScope() {
    co_return co_await fg_DefaultScoped();
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a temporary 'CCurrentActorScope' keeps thread-local data of the thread that made it and is alive across a suspension; the coroutine can resume on another thread; end its scope before the suspension [malterlib-coroutine-thread-local-reference]
}
int fg_PairReversed(int _Value, int *_pValue);
TCFuture<int> fg_RootUnsequencedReversed() {
    co_return fg_PairReversed(co_await fg_Work(), &t_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:52: warning: thread-local data gotten here is used after a suspension later in its expression; the coroutine can resume on another thread; get it after the suspension [malterlib-coroutine-thread-local-reference]
}
int fg_PairDefaulted(int _Value, int *_pValue = &t_Value);
TCFuture<int> fg_DefaultUnsequenced() {
    co_return fg_PairDefaulted(co_await fg_Work());
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: thread-local data gotten here is used after a suspension later in its expression; the coroutine can resume on another thread; get it after the suspension [malterlib-coroutine-thread-local-reference]
}
int fg_ValueAndScope(int _Value, CCurrentActorScope const &_Scope);
TCFuture<int> fg_ScopeSiblingArgument() {
    co_return fg_ValueAndScope(co_await fg_Work(),
                               CCurrentActorScope(
// CHECK-MESSAGES: :[[@LINE-1]]:32: warning: a temporary 'CCurrentActorScope' keeps thread-local data of the thread that made it and is alive across a suspension; the coroutine can resume on another thread; end its scope before the suspension [malterlib-coroutine-thread-local-reference]
                                   fg_ConcurrencyThreadLocal()));
}
TCFuture<int> fg_StepAfterSuspension() {
    int *pValue = t_Values;
    co_await fg_Work();
    ++pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: 'pValue' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
    co_return 0;
}
TCFuture<int> fg_PostStepAfterSuspension() {
    int *pValue = t_Values;
    co_await fg_Work();
    pValue++;
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: 'pValue' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
    co_return 0;
}
TCFuture<int> fg_CompoundStepAfterSuspension() {
    int *pValue = t_Values;
    co_await fg_Work();
    pValue += 1;
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: 'pValue' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
    co_return 0;
}
TCFuture<int> fg_PreIncrementCopy() {
    int *pValue = t_Values;
    int *pNext = ++pValue;
    co_await fg_Work();
    co_return *pNext;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pNext' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_CompoundCopy() {
    int *pValue = t_Values;
    int *pNext = (pValue += 1);
    co_await fg_Work();
    co_return *pNext;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pNext' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
int g_Values[4];
thread_local int *t_pCursor = g_Values;
TCFuture<int> fg_StepThreadLocalSlot() {
    int *pPost = t_pCursor++;
    int *pPre = ++t_pCursor;
    int *pCompound = (t_pCursor += 1);
    co_await fg_Work();
    co_return *pPost + *pPre + *pCompound;
}
TCFuture<bool> fg_AddressOfTracked() {
    int *pValue = &t_Value;
    int **ppValue = &pValue;
    co_await fg_Work();
    co_return ppValue == &pValue;
}
bool fg_Next();
bool fg_Choose();
int fg_Use(int _Value);
TCFuture<int> fg_ConditionalEachIteration() {
    while (fg_Next()) {
      int *pValue = fg_Choose() ? &t_Values[0] : &t_Values[1];
      fg_Use(*pValue);
      co_await fg_Work();
    }
    co_return 0;
}
TCFuture<int> fg_EitherDestination(bool _bChoose) {
    int *pFirst = &g_Value;
    int *pSecond = &g_Value;
    *(_bChoose ? &pFirst : &pSecond) = &t_Value;
    co_await fg_Work();
    co_return *pFirst;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pFirst' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_EitherRefreshed(bool _bChoose) {
    int *pFirst = &t_Value;
    int *pSecond = &t_Value;
    co_await fg_Work();
    *(_bChoose ? &pFirst : &pSecond) = &g_Value;
    co_return *pSecond;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pSecond' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_SeveralDeclared() {
    int *pValue = &t_Value, *pCopy = pValue;
    co_await fg_Work();
    co_return *pCopy;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pCopy' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_SeveralScopes() {
    CCurrentActorScope First(fg_ConcurrencyThreadLocal()),
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: 'First' keeps thread-local data of the thread that made it and is alive across a suspension; the coroutine can resume on another thread; end its scope before the suspension [malterlib-coroutine-thread-local-reference]
        Second(fg_ConcurrencyThreadLocal());
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: 'Second' keeps thread-local data of the thread that made it and is alive across a suspension; the coroutine can resume on another thread; end its scope before the suspension [malterlib-coroutine-thread-local-reference]
    co_await fg_Work();
    co_return 0;
}
int fg_ConsumeScope(CCurrentActorScope const &_Scope);
int fg_TakeScope(CCurrentActorScope _Scope);
TCFuture<int> fg_ScopeEndsAtItsExpression() {
    fg_ConsumeScope(CCurrentActorScope(fg_ConcurrencyThreadLocal()));
    fg_TakeScope(CCurrentActorScope(fg_ConcurrencyThreadLocal()));
    co_await fg_Work();
    co_return 0;
}
struct CScopeHandler : CCoroutineThreadLocalHandler {
  CScopeHandler(CConcurrencyThreadLocal &_ThreadLocal) : m_Scope(_ThreadLocal) {}
  void f_Suspend() noexcept override;
  void f_Resume() noexcept override;
  CCurrentActorScope m_Scope;
};
struct CDerivedScopeHandler : CScopeHandler {
  using CScopeHandler::CScopeHandler;
};
TCFuture<int> fg_HandlerHoldsScope() {
    CScopeHandler Handler(fg_ConcurrencyThreadLocal());
    CDerivedScopeHandler Derived(fg_ConcurrencyThreadLocal());
    co_await fg_Work();
    co_return 0;
}
inline auto fg_ArrayAliasCaptured() {
  int *pValues[1] = {&t_Value};
  auto &Values = pValues;
  return [Values]() -> TCFuture<int> {
    co_await fg_Work();
    co_return *Values[0];
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'Values' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
  };
}
TCFuture<int> fg_ReceiverGetter() {
    auto Copy = fg_GetData();
    int &Value = Copy.f_Member();
    co_await fg_Work();
    co_return Value;
}


// Round 101: a scope alive from its construction in the initializer of what
// holds it, also of a closure; what an accessor gives through a pointer to a
// member function; not what a builtin doesn't evaluate; what a default
// argument gives at each use; not what an explicit object of a copy gives;
// a use in an operand that can come after a suspension in another; not what
// thread-local data borrows, nor a copy of a pointer made null; arrays copied
// by their elements.
struct CScopePair {
  CCurrentActorScope m_Scope;
  int m_Value;
};
TCFuture<int> fg_ScopeInAggregate() {
    CScopePair Pair{CCurrentActorScope(fg_ConcurrencyThreadLocal()), co_await fg_Work()};
// CHECK-MESSAGES: :[[@LINE-1]]:21: warning: a temporary 'CCurrentActorScope' keeps thread-local data of the thread that made it and is alive across a suspension; the coroutine can resume on another thread; end its scope before the suspension [malterlib-coroutine-thread-local-reference]
    co_return Pair.m_Value;
}
TCFuture<int> fg_ScopeInCaptures() {
    auto fScope = [Scope = CCurrentActorScope(fg_ConcurrencyThreadLocal()), Value = co_await fg_Work()] { return Value; };
// CHECK-MESSAGES: :[[@LINE-1]]:28: warning: a temporary 'CCurrentActorScope' keeps thread-local data of the thread that made it and is alive across a suspension; the coroutine can resume on another thread; end its scope before the suspension [malterlib-coroutine-thread-local-reference]
    co_return fScope();
}
TCThreadLocal<int> g_IntThreadLocal;
TCFuture<int> fg_AccessorThroughMemberPointer() {
    auto fGet = &TCThreadLocal<int>::f_Get;
    int *pValue = (g_IntThreadLocal.*fGet)();
    co_await fg_Work();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_UnevaluatedBuiltin() {
    int &Value = t_Value;
    co_await fg_Work();
    co_return __builtin_constant_p(Value);
}
inline int *fg_GetDefault(int *_pValue = t_Values) { return _pValue; }
TCFuture<int> fg_DefaultTwice() {
    co_return fg_GetDefault()[(co_await fg_Work(), (void)fg_GetDefault(), 0)];
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: thread-local data gotten here is used after a suspension later in its expression; the coroutine can resume on another thread; get it after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_ExplicitObjectOfCopy() {
    auto Copy = fg_GetData();
    int &Value = Copy.f_ExplicitValue();
    co_await fg_Work();
    co_return Value;
}
int fg_Add(int _First, int _Second);
TCFuture<int> fg_UseInSiblingOperand() {
    int *pValue = &t_Value;
    co_return *pValue + co_await fg_Work();
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_UseInSiblingArgument() {
    int *pValue = &t_Value;
    co_return fg_Add(*pValue, co_await fg_Work());
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: 'pValue' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_BorrowedPointerIndexed() {
    co_return fg_GetData().m_pValue[co_await fg_Index()];
}
TCFuture<bool> fg_ClearedParameterCopy(CData *_pData) {
    _pData = nullptr;
    auto *pCopy = _pData;
    co_await fg_Work();
    co_return pCopy != nullptr;
}
TCFuture<int> fg_ArrayCopiedByElements() {
    int *pValues[2]{&t_Value, &t_Value};
    auto [pFirst, pSecond] = pValues;
    co_await fg_Work();
    co_return *pFirst;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pFirst' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
thread_local int *t_pPairOfOther[2] = {&g_Value, &g_Value};
TCFuture<int> fg_ThreadLocalArrayCopied() {
    auto [pFirst, pSecond] = t_pPairOfOther;
    co_await fg_Work();
    co_return *pFirst;
}


// Round 101: the slot of a pointer and what it points to, for each form of
// the slot, each operation and each origin (the matrix): thread-local
// storage, storage thread-local data points to, and a local that holds a
// pointer to thread-local data.
int g_MatrixValue;
int *g_pMatrix;
int **g_ppMatrix;
struct CMatrixHolder {
  int *m_pValue;
};
struct CMatrixRef {
  int *&m_rpValue;
};
thread_local int *t_pMatrixRoot = &g_MatrixValue;
thread_local int *t_pMatrixRoots[1] = {&g_MatrixValue};
thread_local CMatrixHolder t_MatrixRoot{&g_MatrixValue};
int *g_pMatrixSlot = &g_MatrixValue;
int *g_pMatrixSlots[1] = {&g_MatrixValue};
CMatrixHolder g_MatrixHolder{&g_MatrixValue};
thread_local int **t_ppMatrixBorrowed = &g_pMatrixSlot;
thread_local int **t_ppMatrixBorroweds = g_pMatrixSlots;
thread_local CMatrixHolder *t_pMatrixBorrowed = &g_MatrixHolder;
thread_local CMatrixRef t_MatrixBorrowedRef{g_pMatrixSlot};

TCFuture<int> fg_MatrixRootVariableLoad() {
    co_await fg_Work();
    co_return *t_pMatrixRoot;
}
TCFuture<int> fg_MatrixRootVariableCopy() {
    int *pCopy = t_pMatrixRoot;
    co_await fg_Work();
    co_return *pCopy;
}
TCFuture<int> fg_MatrixRootVariableAddress() {
    int **ppAddress = &t_pMatrixRoot;
    co_await fg_Work();
    co_return **ppAddress;
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: 'ppAddress' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_MatrixRootElementLoad() {
    co_await fg_Work();
    co_return *t_pMatrixRoots[0];
}
TCFuture<int> fg_MatrixRootElementCopy() {
    int *pCopy = t_pMatrixRoots[0];
    co_await fg_Work();
    co_return *pCopy;
}
TCFuture<int> fg_MatrixRootElementAddress() {
    int **ppAddress = &t_pMatrixRoots[0];
    co_await fg_Work();
    co_return **ppAddress;
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: 'ppAddress' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_MatrixRootMemberLoad() {
    co_await fg_Work();
    co_return *t_MatrixRoot.m_pValue;
}
TCFuture<int> fg_MatrixRootMemberCopy() {
    int *pCopy = t_MatrixRoot.m_pValue;
    co_await fg_Work();
    co_return *pCopy;
}
TCFuture<int> fg_MatrixRootMemberAddress() {
    int **ppAddress = &t_MatrixRoot.m_pValue;
    co_await fg_Work();
    co_return **ppAddress;
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: 'ppAddress' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_MatrixRootReferenceMemberLoad() {
    CMatrixRef Ref{t_pMatrixRoot};
    co_await fg_Work();
    co_return *Ref.m_rpValue;
}
TCFuture<int> fg_MatrixRootReferenceMemberCopy() {
    CMatrixRef Ref{t_pMatrixRoot};
    int *pCopy = Ref.m_rpValue;
    co_await fg_Work();
    co_return *pCopy;
}
TCFuture<int> fg_MatrixRootReferenceMemberAddress() {
    CMatrixRef Ref{t_pMatrixRoot};
    int **ppAddress = &Ref.m_rpValue;
    co_await fg_Work();
    co_return **ppAddress;
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: 'ppAddress' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_MatrixRootDereferenceLoad() {
    int **ppSlot = &t_pMatrixRoot;
    co_await fg_Work();
    co_return **ppSlot;
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: 'ppSlot' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_MatrixRootDereferenceCopy() {
    int **ppSlot = &t_pMatrixRoot;
    int *pCopy = *ppSlot;
    co_await fg_Work();
    co_return *pCopy;
}
TCFuture<int> fg_MatrixRootDereferenceAddress() {
    int **ppSlot = &t_pMatrixRoot;
    int **ppAddress = &*ppSlot;
    co_await fg_Work();
    co_return **ppAddress;
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: 'ppAddress' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_MatrixRootBitCastLoad() {
    co_await fg_Work();
    co_return *__builtin_bit_cast(int *, t_pMatrixRoot);
}
TCFuture<int> fg_MatrixRootBitCastCopy() {
    int *pCopy = __builtin_bit_cast(int *, t_pMatrixRoot);
    co_await fg_Work();
    co_return *pCopy;
}
TCFuture<int> fg_MatrixBorrowedVariableLoad() {
    co_await fg_Work();
    co_return **t_ppMatrixBorrowed;
}
TCFuture<int> fg_MatrixBorrowedVariableCopy() {
    int *pCopy = *t_ppMatrixBorrowed;
    co_await fg_Work();
    co_return *pCopy;
}
TCFuture<int> fg_MatrixBorrowedVariableAddress() {
    int **ppAddress = &*t_ppMatrixBorrowed;
    co_await fg_Work();
    co_return **ppAddress;
}
TCFuture<int> fg_MatrixBorrowedElementLoad() {
    co_await fg_Work();
    co_return *t_ppMatrixBorroweds[0];
}
TCFuture<int> fg_MatrixBorrowedElementCopy() {
    int *pCopy = t_ppMatrixBorroweds[0];
    co_await fg_Work();
    co_return *pCopy;
}
TCFuture<int> fg_MatrixBorrowedElementAddress() {
    int **ppAddress = &t_ppMatrixBorroweds[0];
    co_await fg_Work();
    co_return **ppAddress;
}
TCFuture<int> fg_MatrixBorrowedMemberLoad() {
    co_await fg_Work();
    co_return *t_pMatrixBorrowed->m_pValue;
}
TCFuture<int> fg_MatrixBorrowedMemberCopy() {
    int *pCopy = t_pMatrixBorrowed->m_pValue;
    co_await fg_Work();
    co_return *pCopy;
}
TCFuture<int> fg_MatrixBorrowedMemberAddress() {
    int **ppAddress = &t_pMatrixBorrowed->m_pValue;
    co_await fg_Work();
    co_return **ppAddress;
}
TCFuture<int> fg_MatrixBorrowedReferenceMemberLoad() {
    co_await fg_Work();
    co_return *t_MatrixBorrowedRef.m_rpValue;
}
TCFuture<int> fg_MatrixBorrowedReferenceMemberCopy() {
    int *pCopy = t_MatrixBorrowedRef.m_rpValue;
    co_await fg_Work();
    co_return *pCopy;
}
TCFuture<int> fg_MatrixBorrowedReferenceMemberAddress() {
    int **ppAddress = &t_MatrixBorrowedRef.m_rpValue;
    co_await fg_Work();
    co_return **ppAddress;
}
TCFuture<int> fg_MatrixBorrowedDereferenceLoad() {
    int **ppSlot = &*t_ppMatrixBorrowed;
    co_await fg_Work();
    co_return **ppSlot;
}
TCFuture<int> fg_MatrixBorrowedDereferenceCopy() {
    int **ppSlot = &*t_ppMatrixBorrowed;
    int *pCopy = *ppSlot;
    co_await fg_Work();
    co_return *pCopy;
}
TCFuture<int> fg_MatrixBorrowedDereferenceAddress() {
    int **ppSlot = &*t_ppMatrixBorrowed;
    int **ppAddress = &*ppSlot;
    co_await fg_Work();
    co_return **ppAddress;
}
TCFuture<int> fg_MatrixBorrowedBitCastLoad() {
    co_await fg_Work();
    co_return *__builtin_bit_cast(int *, *t_ppMatrixBorrowed);
}
TCFuture<int> fg_MatrixBorrowedBitCastCopy() {
    int *pCopy = __builtin_bit_cast(int *, *t_ppMatrixBorrowed);
    co_await fg_Work();
    co_return *pCopy;
}
TCFuture<int> fg_MatrixLocalVariableLoad() {
    int *pLocal = &t_Value;
    co_await fg_Work();
    co_return *pLocal;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pLocal' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_MatrixLocalVariableCopy() {
    int *pLocal = &t_Value;
    int *pCopy = pLocal;
    co_await fg_Work();
    co_return *pCopy;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pCopy' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_MatrixLocalVariableAddress() {
    int *pLocal = &t_Value;
    int **ppAddress = &pLocal;
    co_await fg_Work();
    co_return **ppAddress;
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: 'ppAddress' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_MatrixLocalElementLoad() {
    int *pLocal = &t_Value;
    int *pLocals[1] = {&t_Value};
    co_await fg_Work();
    co_return *pLocals[0];
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pLocals' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_MatrixLocalElementCopy() {
    int *pLocal = &t_Value;
    int *pLocals[1] = {&t_Value};
    int *pCopy = pLocals[0];
    co_await fg_Work();
    co_return *pCopy;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pCopy' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_MatrixLocalElementAddress() {
    int *pLocal = &t_Value;
    int *pLocals[1] = {&t_Value};
    int **ppAddress = &pLocals[0];
    co_await fg_Work();
    co_return **ppAddress;
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: 'ppAddress' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_MatrixLocalMemberLoad() {
    int *pLocal = &t_Value;
    CMatrixHolder Local{&t_Value};
    co_await fg_Work();
    co_return *Local.m_pValue;
}
TCFuture<int> fg_MatrixLocalMemberCopy() {
    int *pLocal = &t_Value;
    CMatrixHolder Local{&t_Value};
    int *pCopy = Local.m_pValue;
    co_await fg_Work();
    co_return *pCopy;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pCopy' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_MatrixLocalMemberAddress() {
    int *pLocal = &t_Value;
    CMatrixHolder Local{&t_Value};
    int **ppAddress = &Local.m_pValue;
    co_await fg_Work();
    co_return **ppAddress;
}
TCFuture<int> fg_MatrixLocalReferenceMemberLoad() {
    int *pLocal = &t_Value;
    CMatrixRef Ref{pLocal};
    co_await fg_Work();
    co_return *Ref.m_rpValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pLocal' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_MatrixLocalReferenceMemberCopy() {
    int *pLocal = &t_Value;
    CMatrixRef Ref{pLocal};
    int *pCopy = Ref.m_rpValue;
    co_await fg_Work();
    co_return *pCopy;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pCopy' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_MatrixLocalReferenceMemberAddress() {
    int *pLocal = &t_Value;
    CMatrixRef Ref{pLocal};
    int **ppAddress = &Ref.m_rpValue;
    co_await fg_Work();
    co_return **ppAddress;
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: 'ppAddress' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_MatrixLocalDereferenceLoad() {
    int *pLocal = &t_Value;
    int **ppSlot = &pLocal;
    co_await fg_Work();
    co_return **ppSlot;
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: 'ppSlot' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_MatrixLocalDereferenceCopy() {
    int *pLocal = &t_Value;
    int **ppSlot = &pLocal;
    int *pCopy = *ppSlot;
    co_await fg_Work();
    co_return *pCopy;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pCopy' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_MatrixLocalDereferenceAddress() {
    int *pLocal = &t_Value;
    int **ppSlot = &pLocal;
    int **ppAddress = &*ppSlot;
    co_await fg_Work();
    co_return **ppAddress;
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: 'ppAddress' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_MatrixLocalBitCastLoad() {
    int *pLocal = &t_Value;
    co_await fg_Work();
    co_return *__builtin_bit_cast(int *, pLocal);
// CHECK-MESSAGES: :[[@LINE-1]]:42: warning: 'pLocal' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_MatrixLocalBitCastCopy() {
    int *pLocal = &t_Value;
    int *pCopy = __builtin_bit_cast(int *, pLocal);
    co_await fg_Work();
    co_return *pCopy;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pCopy' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}

// Round 102: a pointer member read through a stale reference; a current
// thread's data beside another thread's; a write that can miss a tracked
// pointer; an alias that can be of thread-local data; a copy of the object
// that holds a scope; a variable an argument gives a value in either order;
// a default scope at each use; not the object of a member function counted
// apart from what is called; an element of a thread-local array of pointers
// to a thread-local type; the pointer a slot a call gives holds; a static
// call operator; not a buffer a thread-local object owns.
TCFuture<int> fg_StaleReceiverPointerLoad() {
    auto &Data = fg_GetData();
    co_await fg_Work();
    int *pValue = Data.m_pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: 'Data' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
    co_return pValue != nullptr;
}
TCFuture<int> fg_EitherThread(bool _bChoose) {
    auto &Data = (_bChoose ? fg_SystemThreadLocal(1) : fg_SystemThreadLocal()).f_Self();
    co_await fg_Work();
    co_return Data.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'Data' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
thread_local int *t_pAlternative;
TCFuture<int> fg_WeakWrite(bool _bChoose) {
    int *pValue = &t_Value;
    co_await fg_Work();
    (_bChoose ? pValue : t_pAlternative) = &t_Value;
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_AliasAlternative(bool _bChoose) {
    int Local = 0;
    int &Value = _bChoose ? t_Value : Local;
    co_await fg_Work();
    co_return Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'Value' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
struct CScopeOwner {
  CCurrentActorScope m_Scope;
  auto f_Copied() {
    return [*this]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: the copy of the object the lambda captures keeps
      co_await fg_Work();
      co_return 0;
    };
  }
};
TCFuture<int> fg_ArgumentAssigns() {
    int *pValue = nullptr;
    fg_PairReversed(co_await fg_Work(), (pValue = &t_Value, nullptr));
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
int fg_ConsumeDefaultScope(int _Value, CCurrentActorScope const &_Scope = CCurrentActorScope(fg_ConcurrencyThreadLocal()));
TCFuture<int> fg_DefaultScopeTwice() {
    fg_ConsumeDefaultScope(0);
    co_return fg_ConsumeDefaultScope(co_await fg_Work());
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: a temporary 'CCurrentActorScope' keeps thread-local data of the thread that made it and is alive across a suspension; the coroutine can resume on another thread; end its scope before the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_ReceiverAfterSuspension() {
    co_return (co_await fg_Work(), fg_GetData()).f_Add(0);
}
thread_local CData *t_pDataSlots[1] = {&t_Data};
TCFuture<int> fg_TypedElementLoaded() {
    CData *pData = t_pDataSlots[0];
    co_await fg_Work();
    co_return pData->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pData' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
CData *&fg_DataSlotRef();
TCFuture<int> fg_SlotResultLoaded() {
    auto *pData = fg_DataSlotRef();
    co_await fg_Work();
    co_return pData->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pData' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_StaticIdentity() {
    int *pValue = [](int *_pValue) static { return _pValue; }(&t_Value);
    co_await fg_Work();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
template <typename t_CValue> struct TCOwnedBuffer {
  ~TCOwnedBuffer();
  t_CValue *f_Data() { return m_pData; }
  t_CValue *m_pData;
};
thread_local TCOwnedBuffer<int> t_OwnedBuffer;
TCFuture<int> fg_OwnedBufferData() {
    int *pData = t_OwnedBuffer.f_Data();
    co_await fg_Work();
    co_return *pData;
}


// Round 103: the elements an array capture reads, and the scopes it copies;
// an element a decayed array designates; a getter called through a pointer
// to it; a local reference to a pointer slot, by what the slot holds; not a
// number read on another path of a conditional; a pointer to a thread-local
// type at a deeper level; a scope held in bytes; a captured structured
// binding by its own type; a pointer a conditional assignment may give a
// value; a default scope after an unevaluated use; not a copy of the object
// in a lambda nested in one that copies it.
TCFuture<int> fg_ArrayCaptureAfter() {
    int *pValues[1] = {&t_Value};
    co_await fg_Work();
    auto fRead = [pValues] { return *pValues[0]; };
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: 'pValues' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
    co_return fRead();
}
TCFuture<int> fg_ScopeArrayCaptured(
    CCurrentActorScope const (&_Scopes)[1]) {
    auto fScope = [_Scopes, Value = co_await fg_Work()] { return Value; };
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: a temporary 'CCurrentActorScope' keeps thread-local data of the thread that made it and is alive across a suspension; the coroutine can resume on another thread; end its scope before the suspension [malterlib-coroutine-thread-local-reference]
    co_return fScope();
}
TCFuture<int> fg_DecayPartial() {
    int *pValues[2]{&t_Value, &t_Value};
    co_await fg_Work();
    *pValues = &g_Value;
    co_return *pValues[1];
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValues' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
inline int (&fg_ThreadArray())[4] { return t_Values; }
TCFuture<int> fg_ResolvedGetter() {
    auto fGet = &fg_ThreadArray;
    co_return fGet()[co_await fg_Index()];
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: thread-local data gotten here is used after a suspension later in its expression; the coroutine can resume on another thread; get it after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_SlotReferenceLoaded() {
    auto &rpData = fg_DataSlotRef();
    auto *pData = rpData;
    co_await fg_Work();
    co_return pData->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pData' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_ScalarConditional(bool _bChoose) {
    int *pValue = &t_Value;
    int Value = _bChoose ? *pValue : co_await fg_Work();
    co_return Value;
}
TCFuture<int> fg_DeeperTyped(CData **_ppData) {
    CData *pData = *_ppData;
    co_await fg_Work();
    co_return pData->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pData' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
template <typename t_CValue> struct TCByteOptional {
  alignas(t_CValue) unsigned char m_Storage[sizeof(t_CValue)];
  bool m_bSet;
};
TCFuture<int> fg_ByteOptionalScope() {
    TCByteOptional<CCurrentActorScope> Optional;
// CHECK-MESSAGES: :[[@LINE-1]]:40: warning: 'Optional' keeps thread-local data of the thread that made it and is alive across a suspension; the coroutine can resume on another thread; end its scope before the suspension [malterlib-coroutine-thread-local-reference]
    co_await fg_Work();
    co_return Optional.m_bSet;
}
struct CDataPointerPair {
  CData *m_pData;
};
thread_local CDataPointerPair t_DataPair{&t_Data};
inline auto fg_BindingCaptured() {
  auto &[pData] = t_DataPair;
  return [pData]() -> TCFuture<int> {
    co_await fg_Work();
    co_return pData->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pData' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
  };
}
TCFuture<int> fg_WeakConditionalCopy(bool _bChoose) {
    int *pFirst = &g_Value;
    int *pSecond = &g_Value;
    (_bChoose ? pFirst : pSecond) = &t_Value;
    auto *pCopy = pFirst;
    co_await fg_Work();
    co_return *pCopy;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pCopy' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_DefaultAfterUnevaluated() {
    (void)sizeof(fg_ConsumeDefaultScope(0));
    co_return fg_ConsumeDefaultScope(co_await fg_Work());
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: a temporary 'CCurrentActorScope' keeps thread-local data of the thread that made it and is alive across a suspension; the coroutine can resume on another thread; end its scope before the suspension [malterlib-coroutine-thread-local-reference]
}
auto CThreadLocalObject::f_NestedCopy() {
  return [*this]() -> TCFuture<int> {
    auto fInner = [this]() -> TCFuture<int> {
      co_await fg_Work();
      co_return m_Value;
    };
    co_return co_await fInner();
  };
}


// Round 104: not what a copy's own member function gives of the copy; a
// static data member by its type; not the arm of a conditional that doesn't
// run; a capture given the data later; what a static call operator gives of
// its parameter; a forward function whose body isn't seen; not the address
// of a copy, nor of what names the thread; an element of a pointer-array
// member; an alias captured with a copy of the object.
TCFuture<int> fg_CopiedReceiver() {
    CData Copy = fg_GetData();
    CData *pData = &Copy.f_Self();
    co_await fg_Work();
    co_return pData->m_Value;
}
struct CStaticProvider {
  static CData *ms_pData;
  static CData &ms_Data;
};
TCFuture<int> fg_StaticMemberRoot(CStaticProvider &_Provider) {
    CData &Data = _Provider.ms_pData[0];
    CData &Other = _Provider.ms_Data;
    co_await fg_Work();
    int Value = Data.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: 'Data' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
    co_return Value + Other.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: 'Other' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_SelectedArm(bool _bChoose) {
    int &Old = t_Value;
    int &Value = _bChoose ? Old : (co_await fg_Work(), g_Value);
    co_return Value;
}
TCFuture<int> fg_CaptureGivenLater() {
    int *pValue = &g_Value;
    auto fRead = [pValue]() mutable -> TCFuture<int> {
      pValue = &t_Value;
      co_await fg_Work();
      co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: 'pValue' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
    };
    co_return co_await fRead();
}
struct CStaticCallGetter {
  int m_Value;
  int &f_Get() {
    return [](int &_Value) static -> int & { return _Value; }(m_Value);
  }
};
thread_local CStaticCallGetter t_StaticCallGetter;
TCFuture<int> fg_StaticCallReceiver() {
    int &Value = t_StaticCallGetter.f_Get();
    co_await fg_Work();
    co_return Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'Value' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
namespace NUnseen {
template <typename t_CType> t_CType &&fg_Forward(t_CType &_Value);
}
TCFuture<int> fg_UnseenForward() {
    int *pValue = &t_Value;
    int *pForwarded = NUnseen::fg_Forward(pValue);
    co_await fg_Work();
    co_return *pForwarded;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pForwarded' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
namespace NAddress {
template <typename t_CType> t_CType *addressof(t_CType &_Value) noexcept;
}
TCFuture<int> fg_AddressOfCopy() {
    auto Copy = fg_GetData();
    CData *pData = NAddress::addressof(Copy);
    co_await fg_Work();
    co_return pData->m_Value;
}
TCFuture<int> fg_AddressOfThread(int _ThreadID) {
    CSystemThreadLocal *pData =
        NAddress::addressof(fg_SystemThreadLocal(_ThreadID));
    co_await fg_Work();
    co_return pData->m_Value;
}
struct CDataSlots {
  CData *m_pData[1];
};
thread_local CDataSlots t_DataSlots{{&t_Data}};
TCFuture<int> fg_PointerArrayMember() {
    CData *pData = t_DataSlots.m_pData[0];
    co_await fg_Work();
    co_return pData->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pData' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
auto CThreadLocalObject::f_CopyAlias() {
  int &Value = m_Value;
  return [*this, &Value]() -> TCFuture<int> {
    co_await fg_Work();
    co_return Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'Value' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
  };
}


// Round 105: a read in an argument the other order can make before a
// suspension of another; a reference to a pointer temporary; the arms of a
// conditional of pointers and of addresses on their own paths; not a pointer
// whose address a helper takes; a getter by what reaches its return; an
// element of an array a pointer to it designates; what a reference to a
// pointer reads where it is read; a reference init capture as the pointer it
// is bound to; the elements of an array parameter; a copy a closure makes of
// a reference; a member pointer to a typed member; a call through a member
// pointer that can dispatch; a scope in a temporary a reference extends.
int fg_ReadTwo(int _Value, int *_pValue);
TCFuture<int> fg_UnorderedRead() {
    int *pValue = &t_Value;
    int *&rpAlias = pValue;
    co_return fg_ReadTwo((rpAlias = &g_Value, co_await fg_Work()), pValue);
// CHECK-MESSAGES: :[[@LINE-1]]:68: warning: 'pValue' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_PointerTemporaryAlias() {
    auto *const &pValue = &t_Value;
    auto &rpAlias = pValue;
    co_await fg_Work();
    co_return *rpAlias;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'rpAlias' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_PointerArms(bool _bChoose) {
    int *pFirst = &t_Value;
    int *pSecond = &g_Value;
    int *pValue = _bChoose ? pFirst : (co_await fg_Work(), pSecond);
    co_return *pValue;
}
TCFuture<int> fg_AddressArms(bool _bChoose) {
    int &rOld = t_Value;
    int Other = 0;
    int *pValue = _bChoose ? &rOld : (co_await fg_Work(), &Other);
    co_return *pValue;
}
TCFuture<int> fg_AddressHelperSlot() {
    int *pValue = &t_Value;
    co_await fg_Work();
    auto ppValue = __builtin_addressof(pValue);
    *ppValue = nullptr;
    co_return 0;
}
inline int *fg_CopiedBeforeReassigned() {
  int *pSource = &g_Value;
  int *pCopy = pSource;
  pSource = &t_Value;
  (void)pSource;
  return pCopy;
}
TCFuture<int> fg_GetterReachingValue() {
    int *pValue = fg_CopiedBeforeReassigned();
    co_await fg_Work();
    co_return *pValue;
}
TCFuture<int> fg_ElementThroughArrayPointer() {
    int *pValues[2] = {&t_Value, &t_Value};
    auto *pArray = &pValues;
    co_await fg_Work();
    (*pArray)[0] = &g_Value;
    co_return *pValues[1];
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValues' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_ReferenceReadWhereRead() {
    int *pValue = &g_Value;
    int *&rpValue = pValue;
    pValue = &t_Value;
    int *pCopy = rpValue;
    co_await fg_Work();
    co_return *pCopy;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pCopy' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_ReferenceInitCapture() {
    int *pValue = &t_Value;
    co_return co_await [&rpValue = pValue]() -> TCFuture<int> {
      co_await fg_Work();
      rpValue = &t_Value;
      co_return *rpValue;
    }();
}
TCFuture<int> fg_ArrayParameter(CData *const (&_pData)[2]) {
    co_await fg_Work();
    co_return _pData[0]->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: '_pData' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_ArrayReferenceParameter(CData (&_Data)[2]) {
    co_await fg_Work();
    co_return _Data[0].m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: '_Data' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
inline auto fg_CopiedReferent() {
  int &rValue = t_Value;
  return [rValue]() -> TCFuture<int> {
    int const *pValue = &rValue;
    co_await fg_Work();
    co_return *pValue;
  };
}
struct CDataLink {
  CData *m_pData;
};
thread_local CDataLink t_DataLink{&t_Data};
TCFuture<int> fg_MemberPointerSelection() {
    CDataLink Link = t_DataLink;
    auto *pData = Link.*&CDataLink::m_pData;
    co_await fg_Work();
    co_return pData->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pData' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
struct CVirtualGetter {
  virtual int &f_Get() { return g_Value; }
};
struct CVirtualOverride : CVirtualGetter {
  int m_Value;
  int &f_Get() override { return m_Value; }
};
thread_local CVirtualOverride t_VirtualOverride;
TCFuture<int> fg_MemberPointerDispatch() {
    CVirtualGetter &Base = t_VirtualOverride;
    auto pGet = &CVirtualGetter::f_Get;
    int &Value = (Base.*pGet)();
    co_await fg_Work();
    co_return Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'Value' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
struct CExtendedScopePair {
  CCurrentActorScope m_Scope;
  int m_Value;
};
TCFuture<int> fg_ExtendedScopePair() {
    auto const &Pair = CExtendedScopePair{CCurrentActorScope(fg_ConcurrencyThreadLocal()), co_await fg_Work()};
// CHECK-MESSAGES: :[[@LINE-1]]:43: warning: a temporary 'CCurrentActorScope' keeps thread-local data of the thread that made it and is alive across a suspension; the coroutine can resume on another thread; end its scope before the suspension [malterlib-coroutine-thread-local-reference]
    co_return Pair.m_Value;
}
struct CThreadAwaiter {
  int m_Value;
  bool await_ready();
  void await_suspend(std::coroutine_handle<>);
  int await_resume() { return m_Value; }
};
thread_local CThreadAwaiter t_Awaiter;
TCFuture<int> fg_ThreadAwaiter() {
    co_return co_await t_Awaiter;
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: thread-local data gotten here is used after a suspension later in its expression; the coroutine can resume on another thread; get it after the suspension [malterlib-coroutine-thread-local-reference]
}


// Round 106: not a getter's return that can't run; what a local closure a
// call runs assigns, also through a reference it captures, not what can't
// run in it; what a pointer to a pointer reads where it is read; a parameter
// at the level of its thread-local type only; each level of a pointer a
// coroutine closure copies; the object a dynamic cast checks; a default
// argument where it is used.
inline int *fg_R106GetFalse() {
  if (false)
    return &t_Value;
  return &g_Value;
}
inline int *fg_R106GetUnreachable() {
  return &g_Value;
  return &t_Value;
}
TCFuture<int> fg_R106Getter() {
    int *pValue = fg_R106GetFalse();
    int *pOther = fg_R106GetUnreachable();
    co_await fg_Work();
    co_return *pValue + *pOther;
}
TCFuture<int> fg_R106ClosureCalled() {
    int *pValue = &g_Value;
    [&] { pValue = &t_Value; }();
    co_await fg_Work();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_R106ClosureScoped() {
    int *pValue = nullptr;
    {
      auto fSet = [&] { pValue = &t_Value; };
      fSet();
    }
    co_await fg_Work();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_R106ClosureReference() {
    int *pValue = &g_Value;
    {
      auto fSet = [&rpSlot = pValue] { rpSlot = &t_Value; };
      fSet();
    }
    co_await fg_Work();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_R106ClosureUnreachable() {
    int *pValue = &g_Value;
    {
      auto fSet = [&] {
        if (false)
          pValue = &t_Value;
      };
      fSet();
    }
    co_await fg_Work();
    co_return *pValue;
}
TCFuture<int> fg_R106IndirectLoad() {
    int *pValue = nullptr;
    int **ppValue = &pValue;
    pValue = &t_Value;
    co_await fg_Work();
    co_return **ppValue;
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: 'ppValue' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_R106IndirectRefreshed() {
    int *pValue = &t_Value;
    int **ppValue = &pValue;
    co_await fg_Work();
    pValue = &g_Value;
    co_return **ppValue;
}
TCFuture<int> fg_R106PointerSlotParameter(CData **_ppData) {
    co_await fg_Work();
    *_ppData = nullptr;
    co_return 0;
}
TCFuture<int> fg_R106PointerSlotCompared(CData **_ppData) {
    co_await fg_Work();
    co_return _ppData != nullptr;
}
TCFuture<int> fg_R106ReferenceSlotParameter(CData *&_pData) {
    co_await fg_Work();
    _pData = nullptr;
    co_return 0;
}
TCFuture<int> fg_R106PointerSlotData(CData **_ppData) {
    co_await fg_Work();
    co_return (*_ppData)->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: '_ppData' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_R106CapturedLevels() {
    int *pValue = &t_Value;
    int **ppValue = &pValue;
    co_return co_await [ppValue]() -> TCFuture<int> {
      co_await fg_Work();
      co_return **ppValue;
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: 'ppValue' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
    }();
}
struct CR106DerivedData : CData {};
TCFuture<int> fg_R106DynamicCast() {
    CData &rData = t_Data;
    co_await fg_Work();
    (void)dynamic_cast<CR106DerivedData &>(rData);
// CHECK-MESSAGES: :[[@LINE-1]]:44: warning: 'rData' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
    co_return 0;
}
void fg_R106Consume(int _Value, int *_pValue = &t_Value);
TCFuture<int> fg_R106SharedDefault() {
    fg_R106Consume(0);
    fg_R106Consume(co_await fg_Work());
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: thread-local data gotten here is used after a suspension later in its expression; the coroutine can resume on another thread; get it after the suspension [malterlib-coroutine-thread-local-reference]
    co_return 0;
}


// Round 107: what a reference to an element or a pointer to an array reads
// where it is read; what a scope exit's closure assigns where its guard ends,
// not after; what a closure called in a local closure assigns.
TCFuture<int> fg_R107ElementReference() {
    int *pSlots[1] = {&g_Value};
    auto &rpSlot = pSlots[0];
    rpSlot = &t_Value;
    co_await fg_Work();
    co_return *rpSlot;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'rpSlot' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_R107ElementPointer() {
    int *pValues[2] = {};
    int **ppValues = pValues;
    pValues[0] = &t_Value;
    co_await fg_Work();
    co_return **ppValues;
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: 'ppValues' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
struct CR107ScopeExit {
  template <typename t_CFunction> struct CGuard {
    t_CFunction m_fFunction;
    ~CGuard() { m_fFunction(); }
  };
  template <typename t_CFunction>
  CGuard<t_CFunction> operator/(t_CFunction _fFunction) const {
    return {_fFunction};
  }
};
extern CR107ScopeExit g_R107OnScopeExit;
TCFuture<int> fg_R107GuardEnded() {
    int *pValue = nullptr;
    {
      auto Cleanup = g_R107OnScopeExit / [&] { pValue = &t_Value; };
    }
    pValue = &g_Value;
    co_await fg_Work();
    co_return *pValue;
}
TCFuture<int> fg_R107NestedClosure() {
    int *pValue = &g_Value;
    [&] { [&] { pValue = &t_Value; }(); }();
    co_await fg_Work();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}

// Round 107, second part: a name of a structured binding by what it binds
// where it is read; each level of a pointer init capture; not what a pointer
// that a listed type's accessor gives points to, but what a pointer to a
// listed type stored for each thread points to; a typed pointer member of a
// local aggregate by what it is made with.
struct CR107Pair {
    int *m_pFirst;
    int *m_pSecond;
};
TCFuture<int> fg_R107BindingCopy() {
    int *pValues[1] = {&t_Value};
    auto [pValue] = pValues;
    co_await fg_Work();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_R107BindingLater() {
    int *pValues[1] = {&g_Value};
    auto &[pValue] = pValues;
    pValues[0] = &t_Value;
    co_await fg_Work();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_R107BindingRefreshed() {
    CR107Pair Pair{&t_Value, &g_Value};
    auto &[pFirst, pSecond] = Pair;
    pFirst = &g_Value;
    co_await fg_Work();
    co_return *Pair.m_pFirst;
}
inline auto fg_R107BindingCaptured() {
  CR107Pair Pair{&g_Value, &g_Value};
  auto &[pFirst, pSecond] = Pair;
  Pair.m_pFirst = &t_Value;
  return [pFirst]() -> TCFuture<int> {
    co_await fg_Work();
    co_return *pFirst;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pFirst' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
  };
}
inline auto fg_R107BindingCapturedRefreshed() {
  CR107Pair Pair{&t_Value, &g_Value};
  auto &[pFirst, pSecond] = Pair;
  Pair.m_pFirst = &g_Value;
  return [pFirst]() -> TCFuture<int> {
    co_await fg_Work();
    co_return *pFirst;
  };
}
TCFuture<int> fg_R107InitCapture() {
    int *pValue = &t_Value;
    co_return co_await [ppValue = &pValue]() -> TCFuture<int> {
        co_await fg_Work();
        co_return **ppValue;
// CHECK-MESSAGES: :[[@LINE-1]]:21: warning: 'ppValue' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
    }();
}
TCFuture<int> fg_R107AccessorPointer() {
    int *pValue = g_ThreadLocal.f_Get()->f_Pointer();
    co_await fg_Work();
    co_return *pValue;
}
TCThreadLocal<CData *> g_R107PointerThreadLocal;
TCFuture<int> fg_R107AccessorStoredPointer() {
    CData *pData = *g_R107PointerThreadLocal.f_Get();
    co_await fg_Work();
    co_return pData->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pData' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
struct CR107Link {
    CData *m_pData;
};
TCFuture<int> fg_R107LinkToCopy() {
    CData Copy = t_Data;
    CR107Link Link{&Copy};
    CData *pData = Link.m_pData;
    co_await fg_Work();
    co_return pData->m_Value;
}


// Round 108: not what a closure in a closure that
// isn't called assigns; a generic lambda by the specialization called; not
// what an awaiter's resume gives, which is of the thread it resumes on; not
// what a helper gives back of the data of a thread named.
TCFuture<int> fg_R108NestedUnused() {
    int *pValue = &g_Value;
    [&] { auto fUnused = [&] { pValue = &t_Value; }; (void)fUnused; }();
    co_await fg_Work();
    co_return *pValue;
}
TCFuture<int> fg_R108NestedCalled() {
    int *pValue = &g_Value;
    [&] { auto fSet = [&] { pValue = &t_Value; }; fSet(); }();
    co_await fg_Work();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_R108GenericDiscarded() {
    int *pValue = &g_Value;
    auto fUpdate = [&]<bool t_bEnabled>() {
      if constexpr (t_bEnabled)
        pValue = &t_Value;
    };
    fUpdate.template operator()<false>();
    co_await fg_Work();
    co_return *pValue;
}
struct CR108Acquire {
  bool await_ready();
  void await_suspend(std::coroutine_handle<>);
  CData &await_resume();
};
TCFuture<int> fg_R108Acquired() {
    CData &Data = co_await CR108Acquire{};
    co_return Data.m_Value;
}
TCFuture<int> fg_R108AcquiredDirect() {
    co_return (co_await CR108Acquire{}).m_Value;
}
template <typename t_CType>
t_CType const &fg_R108Const(t_CType const &_Value) { return _Value; }
TCFuture<int> fg_R108ConstForThread(int _ThreadID) {
    CData const &Data = fg_R108Const(*g_ThreadLocal.f_GetForThread(_ThreadID));
    co_await fg_Work();
    co_return Data.m_Value;
}


// Round 109: what a helper gives on every return, not only of its
// parameters.
inline CData &fg_R109Select(bool _bCurrent, CData &_Other) {
  if (_bCurrent)
    return *g_ThreadLocal.f_Get();
  return _Other;
}
TCFuture<int> fg_R109SelectForThread(bool _bCurrent, int _ThreadID) {
    CData &Data = fg_R109Select(_bCurrent, *g_ThreadLocal.f_GetForThread(_ThreadID));
    co_await fg_Work();
    co_return Data.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'Data' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}

// Round 109, second part: a getter of a reference member; a temporary a local
// reference extends, as its own slot; an init capture by what it is made
// with; a copied capture by what it was given where the closure was made; a
// copy of a reference as a pointer of the closure; a member pointer of a
// pointer to a thread-local type; a byte of the object's own storage.
struct CR109YSlot {
  int *&m_pValue;
  int *&f_Get() { return m_pValue; }
};
TCFuture<int> fg_R109YReferenceMemberGetter() {
    int *pValue = nullptr;
    CR109YSlot Slot{pValue};
    Slot.f_Get() = &t_Value;
    co_await fg_Work();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_R109YExtended() {
    auto &&pValue = &g_Value;
    pValue = &t_Value;
    co_await fg_Work();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_R109YExtendedRefreshed() {
    auto &&pValue = &t_Value;
    co_await fg_Work();
    pValue = &g_Value;
    co_return *pValue;
}
TCFuture<int> fg_R109YInitCapture() {
    int *pValue = &g_Value;
    {
      auto fSet = [pSaved = &t_Value, &pValue] { pValue = pSaved; };
      fSet();
    }
    co_await fg_Work();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_R109YCapturedCopy() {
    int *pValue = &t_Value;
    int *pOut = &g_Value;
    {
      auto fCopy = [pValue, &pOut] { pOut = pValue; };
      pValue = &g_Value;
      fCopy();
    }
    co_await fg_Work();
    co_return *pOut;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pOut' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_R109YCapturedCopyOther() {
    int *pValue = &g_Value;
    int *pOut = &g_Value;
    {
      auto fCopy = [pValue, &pOut] { pOut = pValue; };
      pValue = &t_Value;
      fCopy();
    }
    co_await fg_Work();
    co_return *pOut;
}
inline auto fg_R109YCopiedReference() {
    int *pValue = &t_Value;
    int *&rpValue = pValue;
    return [rpValue]() mutable -> TCFuture<int> {
      co_await fg_Work();
      rpValue = &g_Value;
      co_return *rpValue;
    };
}
struct CR109YDataSlot {
  CData **m_ppData;
};
TCFuture<int> fg_R109YMemberPointerLevels(CR109YDataSlot &_Slot) {
    CData *pData = *_Slot.m_ppData;
    co_await fg_Work();
    co_return pData->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pData' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
struct CR109YBytes {
  int m_Value;
  unsigned char *f_Bytes() {
    return reinterpret_cast<unsigned char *>(this) + 1;
  }
};
TCFuture<int> fg_R109YBytes(CR109YBytes &_Bytes) {
    unsigned char *pByte = _Bytes.f_Bytes();
    co_await fg_Work();
    co_return *pByte;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pByte' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
// End of round 109, second part.

// Round 110: a capture by reference names the slot, not what it holds, also
// of a closure that refreshes it before it reads it.
TCFuture<int> fg_R110CapturedReset() {
    auto *pThreadLocal = &fg_ConcurrencyThreadLocal();
    co_await fg_Work();
    [&pThreadLocal] { pThreadLocal = nullptr; }();
    co_return 0;
}
TCFuture<int> fg_R110CapturedRefresh() {
    auto *pThreadLocal = &fg_ConcurrencyThreadLocal();
    co_await fg_Work();
    co_return [&] {
      pThreadLocal = &fg_ConcurrencyThreadLocal();
      return pThreadLocal->m_Value;
    }();
}


// Round 111: what a closure called there assigns each time it runs replaces
// what the variable held, not what it assigns only on one path; the scopes a
// `try` makes end before its handler.
TCFuture<int> fg_R111Refreshed() {
    auto *pThreadLocal = &fg_ConcurrencyThreadLocal();
    co_await fg_Work();
    [&] { pThreadLocal = &fg_ConcurrencyThreadLocal(); }();
    co_return pThreadLocal->m_Value;
}
TCFuture<int> fg_R111RefreshedNamed() {
    auto *pThreadLocal = &fg_ConcurrencyThreadLocal();
    co_await fg_Work();
    auto fRefresh = [&pThreadLocal] { pThreadLocal = &fg_ConcurrencyThreadLocal(); };
    fRefresh();
    co_return pThreadLocal->m_Value;
}
TCFuture<int> fg_R111RefreshedMaybe(bool _bRefresh) {
    auto *pThreadLocal = &fg_ConcurrencyThreadLocal();
    co_await fg_Work();
    [&] {
      if (_bRefresh)
        pThreadLocal = &fg_ConcurrencyThreadLocal();
    }();
    co_return pThreadLocal->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pThreadLocal' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_R111UnwoundScope() {
    try {
      CCurrentActorScope Scope(fg_ConcurrencyThreadLocal());
      fg_Throws();
    } catch (...) {
    }
    co_await fg_Work();
    co_return 0;
}


// Round 112: what a closure called there assigns on each way through it; not
// what a variable held before it was given another value before a closure
// that refers to it is made; a handler by what its own `try` leaves; what a
// helper gives back of a copy, not by its type.
TCFuture<int> fg_R112BranchRefresh(bool _bRead) {
    auto *pThreadLocal = &fg_ConcurrencyThreadLocal();
    co_await fg_Work();
    auto fRefresh = [&] {
      if (_bRead)
        pThreadLocal = &fg_ConcurrencyThreadLocal();
      else
        pThreadLocal = nullptr;
    };
    fRefresh();
    co_return pThreadLocal ? pThreadLocal->m_Value : 0;
}
TCFuture<int> fg_R112BranchRefreshMaybe(bool _bRead) {
    auto *pThreadLocal = &fg_ConcurrencyThreadLocal();
    co_await fg_Work();
    auto fRefresh = [&] {
      if (_bRead)
        pThreadLocal = &fg_ConcurrencyThreadLocal();
      else
        return;
    };
    fRefresh();
    co_return pThreadLocal->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pThreadLocal' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
inline TCFuture<int> fg_R112CaptureOverwritten() {
    int *pValue = &t_Value;
    ++*pValue;
    pValue = &g_Value;
    auto fRead = [&pValue]() -> TCFuture<int> {
      co_await fg_Work();
      co_return *pValue;
    };
    co_return co_await fRead();
}
TCFuture<int> fg_R112NestedTry() {
    auto *pThreadLocal = &fg_ConcurrencyThreadLocal();
    co_await fg_Work();
    try {
      pThreadLocal = &fg_ConcurrencyThreadLocal();
      try {
        fg_Throws();
      } catch (...) {
        co_return pThreadLocal->m_Value;
      }
    } catch (...) {
    }
    co_return 0;
}
template <typename t_CType>
t_CType const &fg_R112Const(t_CType const &_Value) { return _Value; }
TCFuture<int> fg_R112ConstSnapshot() {
    CData Snapshot = *g_ThreadLocal.f_Get();
    auto const &Value = fg_R112Const(Snapshot).m_Value;
    co_await fg_Work();
    co_return Value;
}


// Round 113: what a closure called there assigns on each way through it, also
// after the block it is assigned in; what a guard nothing names assigns on
// each way through its closure where it ends.
TCFuture<int> fg_R113RefreshThenRead(bool _bRead) {
    auto *pThreadLocal = &fg_ConcurrencyThreadLocal();
    co_await fg_Work();
    auto fRefresh = [&] {
      pThreadLocal = &fg_ConcurrencyThreadLocal();
      return _bRead ? pThreadLocal->m_Value : 0;
    };
    fRefresh();
    co_return pThreadLocal->m_Value;
}
TCFuture<int> fg_R113BranchThenCall(bool _bUse) {
    auto *pThreadLocal = &fg_ConcurrencyThreadLocal();
    co_await fg_Work();
    auto fRefresh = [&] {
      if (_bUse)
        pThreadLocal = &fg_ConcurrencyThreadLocal();
      else
        pThreadLocal = nullptr;
      fg_ReadAddress(nullptr);
    };
    fRefresh();
    co_return pThreadLocal ? pThreadLocal->m_Value : 0;
}
TCFuture<int> fg_R113GuardRestores() {
    int Value = 0;
    int *pValue = &Value;
    {
      auto Restore = g_OnScopeExit / [&] { pValue = &Value; };
      pValue = &t_Value;
      ++*pValue;
    }
    co_await fg_Work();
    co_return *pValue;
}


// Round 114: the left of a compound assignment after its right, which
// suspends; what a closure called by `std::invoke` assigns on each way
// through it; what a closure leaves in a slot where it returns, not what it
// assigns before.
TCFuture<int> fg_R114Read();
TCFuture<int> fg_R114CompoundAfter() {
    fg_ConcurrencyThreadLocal().m_Value += co_await fg_R114Read();
    co_return 0;
}
} // namespace NThreadLocals
namespace std {
template <typename t_FFunction>
auto invoke(t_FFunction &&_fFunction) -> decltype(static_cast<t_FFunction &&>(_fFunction)()) {
  return static_cast<t_FFunction &&>(_fFunction)();
}
} // namespace std
namespace NThreadLocals {
TCFuture<int> fg_R114Invoked() {
    auto *pThreadLocal = &fg_ConcurrencyThreadLocal();
    co_await fg_Work();
    auto fRefresh = [&] { pThreadLocal = &fg_ConcurrencyThreadLocal(); };
    std::invoke(fRefresh);
    co_return pThreadLocal->m_Value;
}
TCFuture<int> fg_R114ExitValue() {
    int Count = 0;
    int *pCounter = &Count;
    {
      auto fSample = [&] {
        pCounter = &fg_ConcurrencyThreadLocal().m_Value;
        Count = *pCounter;
        pCounter = &Count;
      };
      fSample();
    }
    co_await fg_Work();
    co_return *pCounter;
}


// Round 115: what a closure called in a closure called there assigns each
// time; an exchange as an assignment of what it is given; not what a guard
// cancelled runs.
TCFuture<int> fg_R115NestedRefresh() {
    auto *pData = &fg_ConcurrencyThreadLocal();
    co_await fg_Work();
    auto fRead = [&] {
      auto fRefresh = [&] { pData = &fg_ConcurrencyThreadLocal(); };
      fRefresh();
      return pData->m_Value;
    };
    fRead();
    co_return pData->m_Value;
}
TCFuture<int> fg_R115ExchangeReset(bool _bAsync) {
    auto *pThreadLocal = &fg_ConcurrencyThreadLocal();
    if (_bAsync) {
      fg_Exchange(pThreadLocal, nullptr)->m_Value = 0;
      co_await fg_Work();
    }
    if (pThreadLocal)
      pThreadLocal->m_Value = 0;
    co_return 0;
}
TCFuture<int> fg_R115CancelledGuard() {
    int nLocal = 0;
    int *pCounter = &nLocal;
    {
      auto Rollback = g_OnScopeExit / [&] { pCounter = &t_Value; };
      pCounter = &nLocal;
      Rollback.f_Clear();
    }
    co_await fg_Work();
    co_return *pCounter;
}
TCFuture<int> fg_R115GuardNotCancelled() {
    int nLocal = 0;
    int *pCounter = &nLocal;
    {
      auto Rollback = g_OnScopeExit / [&] { pCounter = &t_Value; };
      pCounter = &nLocal;
    }
    co_await fg_Work();
    co_return *pCounter;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pCounter' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}


// Round 116: a closure runs only where a use of it calls it, by the one
// classifier of its uses: not where a function object of
// SynchronousFunctionTypes made of it is only checked; where it is called.
TCFuture<int> fg_R116WrapperChecked() {
    int nLocal = 0;
    int *pValue = &nLocal;
    NMib::NFunction::TCFunction<void ()> fSet = [&] { pValue = &fg_ConcurrencyThreadLocal().m_Value; };
    if (!fSet)
      co_return 0;
    co_await fg_Work();
    co_return *pValue;
}
TCFuture<int> fg_R116WrapperCalled() {
    int nLocal = 0;
    int *pValue = &nLocal;
    NMib::NFunction::TCFunction<void ()> fSet = [&] { pValue = &fg_ConcurrencyThreadLocal().m_Value; };
    if (fSet)
      fSet();
    co_await fg_Work();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}


// Round 117: what a closure assigns each time it runs is assigned where the
// one classifier of its uses tells it is called: through a function object
// of SynchronousFunctionTypes made of it, and through a local closure that
// captures it and calls it; not through one given another callable.
TCFuture<int> fg_R117WrapperRefresh() {
    int *pValue = &fg_ConcurrencyThreadLocal().m_Value;
    co_await fg_Work();
    NMib::NFunction::TCFunction<void ()> fRefresh = [&] { pValue = &fg_ConcurrencyThreadLocal().m_Value; };
    fRefresh();
    co_return *pValue;
}
TCFuture<int> fg_R117CapturedRefresh() {
    auto *pThreadLocal = &fg_ConcurrencyThreadLocal();
    co_await fg_Work();
    auto fRefresh = [&] { pThreadLocal = &fg_ConcurrencyThreadLocal(); };
    auto fRead = [&] { fRefresh(); };
    fRead();
    co_return pThreadLocal->m_Value;
}
TCFuture<int> fg_R117CapturedRefreshMaybe(bool _bRefresh) {
    auto *pThreadLocal = &fg_ConcurrencyThreadLocal();
    co_await fg_Work();
    auto fRefresh = [&] { pThreadLocal = &fg_ConcurrencyThreadLocal(); };
    auto fRead = [&] {
      if (_bRefresh)
        fRefresh();
    };
    fRead();
    co_return pThreadLocal->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pThreadLocal' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_R117WrapperReplaced() {
    int *pValue = &fg_ConcurrencyThreadLocal().m_Value;
    co_await fg_Work();
    int nLocal = 0;
    NMib::NFunction::TCFunction<void ()> fRefresh = [&] { pValue = &fg_ConcurrencyThreadLocal().m_Value; };
    fRefresh = [&] { nLocal = 1; };
    fRefresh();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}


// Round 118, part A: a call of a local function object of
// SynchronousFunctionTypes runs the closure whose value reaches it, also
// before the object is given another; not one given another before it.
TCFuture<int> fg_R118ARefreshBeforeReplaced() {
    int *pValue = &fg_ConcurrencyThreadLocal().m_Value;
    co_await fg_Work();
    int nLocal = 0;
    NMib::NFunction::TCFunction<void ()> fRefresh = [&] { pValue = &fg_ConcurrencyThreadLocal().m_Value; };
    fRefresh();
    fRefresh = [&] { nLocal = 1; };
    fRefresh();
    co_return *pValue;
}
TCFuture<int> fg_R118ARefreshMaybeCleared(bool _bClear) {
    int *pValue = &fg_ConcurrencyThreadLocal().m_Value;
    co_await fg_Work();
    NMib::NFunction::TCFunction<void ()> fRefresh = [&] { pValue = &fg_ConcurrencyThreadLocal().m_Value; };
    if (_bClear)
      fRefresh.f_Clear();
    fRefresh();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
// End of round 118, part A.


// Round 119: a nested closure of a copy of the object, `[*this]`, that
// borrows a member of the copy points into the copy, not into thread-local
// data; not one of the object itself.
auto CThreadLocalObject::f_R119NestedBorrow() {
  return [*this]() -> TCFuture<int> {
    auto fRead = [pValue = &m_Value]() -> TCFuture<int> {
      co_await fg_Work();
      co_return *pValue;
    };
    co_return co_await fRead();
  };
}
auto CThreadLocalObject::f_R119NestedBorrowThis() {
  return [this]() -> TCFuture<int> {
    auto fRead = [pValue = &m_Value]() -> TCFuture<int> {
      co_await fg_Work();
      co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: 'pValue' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
    };
    co_return co_await fRead();
  };
}
// End of round 119.


// Round 120: a write through a pointer a closure copies, whose value is the
// address of a local, assigns that local, each time it runs when it is on
// each way through the closure; not on only one.
TCFuture<int> fg_R120PointerCaptureRefresh() {
    auto *pData = &fg_ConcurrencyThreadLocal();
    co_await fg_Work();
    auto fRefresh = [ppData = &pData] { *ppData = &fg_ConcurrencyThreadLocal(); };
    fRefresh();
    co_return pData->m_Value;
}
TCFuture<int> fg_R120PointerCaptureRefreshMaybe(bool _bRefresh) {
    auto *pData = &fg_ConcurrencyThreadLocal();
    co_await fg_Work();
    auto fRefresh = [ppData = &pData, _bRefresh] {
      if (_bRefresh)
        *ppData = &fg_ConcurrencyThreadLocal();
    };
    fRefresh();
    co_return pData->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pData' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
// End of round 120.


// Round 121: what an operand unsequenced to one that suspends uses of what
// it defines itself on each way before is on the same side of the
// suspension; not what leaves it, nor what it defines only on some ways, nor
// what it uses of what was defined before it.
int fg_R121Combine(int _First, int _Second);
TCFuture<int> fg_R121FreshInOperand() {
    CData *pData = nullptr;
    co_return fg_R121Combine((pData = g_ThreadLocal.f_Get()) ? pData->m_Value : 0, co_await fg_Work());
}
TCFuture<int> fg_R121LeavesOperand() {
    CData *pData = nullptr;
    int Value = fg_R121Combine((pData = g_ThreadLocal.f_Get()) ? 1 : 0, co_await fg_Work());
    co_return Value + pData->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: 'pData' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_R121DefinedOnSomeWays(bool _bGet) {
    CData *pData = g_ThreadLocal.f_Get();
    co_return fg_R121Combine((_bGet || (pData = g_ThreadLocal.f_Get())) ? pData->m_Value : 0, co_await fg_Work());
// CHECK-MESSAGES: :[[@LINE-1]]:75: warning: 'pData' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_R121DefinedBefore() {
    CData *pData = g_ThreadLocal.f_Get();
    co_return fg_R121Combine(pData->m_Value, co_await fg_Work());
// CHECK-MESSAGES: :[[@LINE-1]]:30: warning: 'pData' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
// End of round 121.


// Round 122: what an operand unsequenced to one that suspends uses of what
// it defines itself in an operand evaluated before the use, the left of a
// comma, is on the same side of the suspension; not an operand unsequenced
// to the use.
TCFuture<int> fg_R122CommaRefresh() {
    CConcurrencyThreadLocal *pTL = nullptr;
    co_return fg_R121Combine((pTL = &fg_ConcurrencyThreadLocal(), pTL->m_Value), co_await fg_Work());
}
TCFuture<int> fg_R122UnsequencedRefresh() {
    CConcurrencyThreadLocal *pTL = &fg_ConcurrencyThreadLocal();
    co_return fg_R121Combine(pTL->m_Value + (pTL = &fg_ConcurrencyThreadLocal(), 0), co_await fg_Work());
// CHECK-MESSAGES: :[[@LINE-1]]:30: warning: 'pTL' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
// End of round 122.

// Not a function that isn't a coroutine.
inline int fg_NotCoroutine() {
  auto &Data = t_Data;
  return Data.m_Value;
}

namespace NReview2Round192NullReturn {
// A return of nothing, `return nullptr;`, adds no way to what a helper gives:
// one that gives what a thread named gives on every other way is of that
// thread, like its conditional form; one that gives the current thread's
// data on another way is not.
CSystemThreadLocal *fg_TryThreadData(int _ThreadID) {
  if (!_ThreadID)
    return nullptr;
  return &fg_SystemThreadLocal(_ThreadID);
}
CSystemThreadLocal *fg_ChooseThreadData(int _ThreadID) {
  return _ThreadID ? &fg_SystemThreadLocal(_ThreadID) : nullptr;
}
CSystemThreadLocal *fg_TryCurrentData(int _ThreadID) {
  if (!_ThreadID)
    return nullptr;
  return &fg_SystemThreadLocal();
}
TCFuture<int> fg_Try(int _ThreadID) {
  auto *pData = fg_TryThreadData(_ThreadID);
  co_await fg_Work();
  co_return pData->m_Value;
}
TCFuture<int> fg_Choose(int _ThreadID) {
  auto *pData = fg_ChooseThreadData(_ThreadID);
  co_await fg_Work();
  co_return pData->m_Value;
}
TCFuture<int> fg_Current(int _ThreadID) {
  auto *pData = fg_TryCurrentData(_ThreadID);
  co_await fg_Work();
  co_return pData->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pData' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
} // namespace NReview2Round192NullReturn

// A TCFuture coroutine's body starts where it is called, whatever the
// promise's initial suspend does at run time (CInitialSuspend): a parameter
// read before the first suspension is read on the caller's thread, also in a
// coroutine lambda given to fg_CallSafe.
namespace NReview2Round195Start {
TCFuture<int> fg_Timeout(double _Seconds);
TCFuture<int> fg_Read(CConcurrencyThreadLocal &_ThreadLocal) {
  int nCount = int(_ThreadLocal.m_Value);
  co_await fg_Timeout(0.01);
  co_return nCount;
}
TCFuture<int> fg_CallRead() {
  co_return co_await fg_Read(fg_ConcurrencyThreadLocal());
}
TCFuture<int> fg_ReadAfter(CConcurrencyThreadLocal &_ThreadLocal) {
  co_await fg_Timeout(0.01);
  co_return int(_ThreadLocal.m_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: '_ThreadLocal' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}
TCFuture<int> fg_Inline() {
  auto &Data = fg_ConcurrencyThreadLocal();
  co_return co_await fg_CallSafe([&Data]() -> TCFuture<int> {
    co_return Data.m_Value;
  });
}
TCFuture<int> fg_InlineAfter() {
  auto &Data = fg_ConcurrencyThreadLocal();
  co_return co_await fg_CallSafe([&Data]() -> TCFuture<int> {
    co_await fg_Timeout(0.01);
    co_return Data.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'Data' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
  });
}
} // namespace NReview2Round195Start
} // namespace NThreadLocals
