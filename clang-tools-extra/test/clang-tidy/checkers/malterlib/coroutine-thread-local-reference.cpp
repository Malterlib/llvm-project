// RUN: %check_clang_tidy -std=c++23-or-later %s malterlib-coroutine-thread-local-reference %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml', malterlib-coroutine-thread-local-reference.ThreadLocalTypes: 'CData;NThreadLocals::CConcurrencyThreadLocal;NThreadLocals::CSystemThreadLocal;NThreadLocals::CThreadLocalObject'}}" -- -I %S/Inputs

// This file is generated.

#include "malterlib-actor-phase.h"

namespace NThreadLocals {
TCFuture<int> fg_Work();
TCFuture<int> fg_Index();

struct CItems {
  int m_Count;
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
};

template <typename t_CData> struct TCThreadLocal {
  t_CData *f_Get();
  t_CData *operator->();
  // Its own implementation, which gives what it holds as it is.
  int *f_FirstValue() { return &f_Get()->m_Value; }
};

struct CSystemThreadLocal {
  int m_Value;
  CData &m_Data;
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

// A thread-local type whose member functions use the object.
struct CThreadLocalObject {
  int m_Value;
  TCFuture<int> f_Read();
  TCFuture<int> f_ReadBefore();
  auto f_Lambda();
  int f_CalledInPlace();
  auto f_CopiedObject();
  auto f_Captured();
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
// promise suspends before its body runs, from the start.
struct CSuspendAlways {
  bool await_ready() const noexcept { return false; }
  void await_suspend(std::coroutine_handle<>) const noexcept {}
  void await_resume() const noexcept {}
};
struct CLazy {
  struct promise_type {
    CLazy get_return_object();
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
// nor values of a conditional apart, nor a loop that doesn't go on after a
// suspension in it, nor what a thread named gives through a reference member.
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
      co_await fg_Work();
      co_return Value;
    }
    co_return 0;
}
TCFuture<int> fg_LoopBreaks() {
    int Total = 0;
    for (int Value : t_Values) {
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

// Not a function that isn't a coroutine.
inline int fg_NotCoroutine() {
  auto &Data = t_Data;
  return Data.m_Value;
}
} // namespace NThreadLocals

