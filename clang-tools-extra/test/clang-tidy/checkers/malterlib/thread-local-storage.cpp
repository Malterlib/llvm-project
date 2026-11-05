// RUN: %check_clang_tidy -std=c++23-or-later %s malterlib-thread-local-storage %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml', malterlib-thread-local-storage.ThreadLocalTypes: 'CData;NThreadLocals::CConcurrencyThreadLocal;NThreadLocals::CSystemThreadLocal;CListedData;NThreadLocals::CPrivate;NThreadLocals::CThreadLocalObject;NThreadLocals::CInheritingData'}}" -- -I %S/Inputs

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


struct CHolder {
  int *m_pValue;
  int m_Count;
};

// Members, by assignment, a member initializer, a default one, a
// constructor's body and an aggregate.
inline void fg_AssignMember(CHolder &_Holder) {
  _Holder.m_pValue = &t_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: a pointer to thread-local data is stored in the member 'm_pValue'
}
inline void fg_AssignMemberOther(CHolder &_Holder) {
  _Holder.m_pValue = &g_Value;
}
inline void fg_AssignMemberCopy(CHolder &_Holder) {
  _Holder.m_Count = t_Value;
}
struct CView {
  CView(CData &_Data) : m_pData(&_Data) {}
// CHECK-MESSAGES: :[[@LINE-1]]:33: warning: a pointer to thread-local data is stored in the member 'm_pData'
  CData *m_pData;
};
struct CReference {
  CReference(CConcurrencyThreadLocal &_Data) : m_Data(_Data) {}
// CHECK-MESSAGES: :[[@LINE-1]]:55: warning: a reference to thread-local data is stored in the member 'm_Data'
  CConcurrencyThreadLocal &m_Data;
};
struct CDefault {
  int *m_pValue = &t_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: a pointer to thread-local data is stored in the member 'm_pValue'
};
struct CBody {
  CBody() { m_pValue = &fg_ConcurrencyThreadLocal().m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a pointer to thread-local data is stored in the member 'm_pValue'
  int *m_pValue;
};
inline int fg_Aggregate() {
  CHolder Holder{&t_Value, 1};
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: a pointer to thread-local data is stored in the member 'm_pValue'
  return *Holder.m_pValue;
}

// Globals, statics, objects on the heap and what an output parameter refers
// to.
int *g_pValue;
inline void fg_AssignGlobal() {
  g_pValue = &t_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
inline int fg_StaticLocal() {
  static CConcurrencyThreadLocal *s_pData = &fg_ConcurrencyThreadLocal();
// CHECK-MESSAGES: :[[@LINE-1]]:45: warning: a pointer to thread-local data is stored in the static 's_pData'
  return s_pData->m_Value;
}
inline int **fg_OnHeap() {
  return new int *(&t_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: a pointer to thread-local data is stored in an object on the heap
}
inline void fg_Output(CConcurrencyThreadLocal *&o_pData) {
  o_pData = &fg_ConcurrencyThreadLocal();
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: a pointer to thread-local data is stored in what the parameter 'o_pData' refers to
}

// Captures: a reference by reference, a pointer by reference or copied, and
// an initializer; not a copy of what a reference refers to.
inline auto fg_CaptureReference() {
  auto &Data = t_Data;
  return [&Data] { return Data.m_Value; };
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: a reference to thread-local data is stored in the capture of 'Data'
}
inline auto fg_CapturePointer() {
  CData *pData = &t_Data;
  return [pData] { return pData->m_Value; };
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: a pointer to thread-local data is stored in the capture of 'pData'
}
inline auto fg_CaptureInit() {
  return [pData = &t_Data] { return pData->m_Value; };
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: a pointer to thread-local data is stored in the capture of 'pData'
}
inline auto fg_CaptureCopy() {
  auto &Data = t_Data;
  return [Data] { return Data.m_Value; };
}

// Not a closure that ends with the function, before it suspends: called
// where it is made, a local only called, or the cleanup of a local scope
// exit; one that is given elsewhere, or alive across a suspension, is.
inline int fg_CalledInPlace() {
  auto &Data = t_Data;
  return [&] { return Data.m_Value; }();
}
inline int fg_LocalCalled() {
  auto &Data = t_Data;
  auto fRead = [&] { return Data.m_Value; };
  return fRead() + fRead();
}
inline void fg_ScopeExit() {
  auto &Data = t_Data;
  int Old = Data.m_Value;
  Data.m_Value = 1;
  auto Cleanup = g_OnScopeExit / [&] { Data.m_Value = Old; };
}
template <typename t_FFunction> void fg_Keep(t_FFunction &&_fFunction);
inline void fg_LocalGiven() {
  auto &Data = t_Data;
  auto fRead = [&] { return Data.m_Value; };
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: a reference to thread-local data is stored in the capture of 'Data'
  fg_Keep(fRead);
}
inline TCFuture<int> fg_CoroutineLocalBefore() {
  {
    auto &Data = t_Data;
    int Old = Data.m_Value;
    auto Cleanup = g_OnScopeExit / [&] { Data.m_Value = Old; };
  }
  co_await fg_Work();
  co_return 0;
}
inline TCFuture<int> fg_CoroutineLocal() {
  auto &Data = t_Data;
  auto fRead = [&] { return Data.m_Value; };
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: a reference to thread-local data is stored in the capture of 'Data'
  co_await fg_Work();
  co_return fRead();
}

// Returns, of the thread-local type itself only.
inline int &fg_ReturnMember() {
  return t_Data.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: a reference to thread-local data is returned as a type that isn't a reference to a thread-local type
}
inline int *fg_ReturnPointer() {
  return &t_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: a pointer to thread-local data is returned as a type that isn't a pointer to a thread-local type
}
inline CConcurrencyThreadLocal &fg_ReturnData() {
  return fg_ConcurrencyThreadLocal();
}
inline CData *fg_ReturnDataPointer() {
  return g_ThreadLocal.f_Get();
}
inline int fg_ReturnCopy() {
  return t_Value;
}

// Locals refer to it freely.
inline int fg_Locals() {
  CData *pData = &t_Data;
  auto &Data = fg_ConcurrencyThreadLocal();
  int *Values[1];
  Values[0] = &t_Value;
  return pData->m_Value + Data.m_Value + *Values[0];
}

// What is held for each thread, a class that isn't one of ThreadLocalTypes,
// also of a header, or in another template; not a template's, a value that
// isn't a class, nor a class listed, also qualified, past an anonymous
// namespace.
} // namespace NThreadLocals
#include "thread-local-storage-types.h"
namespace NThreadLocals {
struct CMainData {
  int m_Value;
};
TCThreadLocal<CMainData> g_MainThreadLocal;
// CHECK-MESSAGES: :[[@LINE-1]]:26: warning: 'g_MainThreadLocal' holds 'NThreadLocals::CMainData' for each thread
TCThreadLocal<CHeaderData> g_HeaderThreadLocal;
// CHECK-MESSAGES: :[[@LINE-1]]:28: warning: 'g_HeaderThreadLocal' holds 'CHeaderData' for each thread
TCWrapper<TCThreadLocal<CNestedHeaderData>> g_NestedThreadLocal;
// CHECK-MESSAGES: :[[@LINE-1]]:45: warning: 'g_NestedThreadLocal' holds 'CNestedHeaderData' for each thread
TCThreadLocal<TCHeaderTemplate<int>> g_TemplateThreadLocal;
TCThreadLocal<int> g_IntThreadLocal;
TCThreadLocal<CListedData> g_ListedThreadLocal;
namespace {
struct CPrivate {
  int m_Value;
};
}
TCThreadLocal<CPrivate> g_PrivateThreadLocal;
struct CHolderOfHeaderData {
  TCThreadLocal<CHeaderData> m_ThreadLocal;
// CHECK-MESSAGES: :[[@LINE-1]]:30: warning: 'm_ThreadLocal' holds 'CHeaderData' for each thread
};

// Round 95: what a constructor keeps of what it is given, where the object
// is made, of a local in a coroutine, which can resume on another thread: one
// that can be seen that stores it, one that can't of a pointer to what isn't
// const; not one that copies, a temporary, nor a local of a function that
// isn't a coroutine.
struct CBorrowingView {
  CBorrowingView(int *_pValue) : m_pValue(_pValue) {}
  int *m_pValue;
};
struct CBorrowingReference {
  CBorrowingReference(int &_Value) : m_Value(_Value) {}
  int &m_Value;
};
struct CInvisibleView {
  CInvisibleView(int *_pValue);
  CInvisibleView(int const *_pValue, int _Size);
  int *m_pValue;
};
struct CCopyingView {
  CCopyingView(int *_pValue) : m_Value(*_pValue) {}
  int m_Value;
};
inline TCFuture<int> fg_ConstructedView() {
  CBorrowingView View(&t_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a pointer to thread-local data is stored in the 'CBorrowingView' made of it
  co_await fg_Work();
  co_return *View.m_pValue;
}
inline TCFuture<int> fg_ConstructedReference() {
  CBorrowingReference View(t_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:28: warning: a reference to thread-local data is stored in the 'CBorrowingReference' made of it
  co_await fg_Work();
  co_return View.m_Value;
}
inline TCFuture<int> fg_ConstructedInvisible() {
  CInvisibleView View(&t_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a pointer to thread-local data is stored in the 'CInvisibleView' made of it
  co_await fg_Work();
  co_return *View.m_pValue;
}
inline TCFuture<int> fg_ConstructedConst() {
  CInvisibleView View(&t_Value, 1);
  co_await fg_Work();
  co_return *View.m_pValue;
}
inline TCFuture<int> fg_ConstructedCopy() {
  CCopyingView View(&t_Value);
  co_await fg_Work();
  co_return View.m_Value;
}
inline int fg_ConstructedNotCoroutine() {
  CBorrowingView View(&t_Value);
  CBorrowingReference Lock(t_Data.m_Value);
  return *View.m_pValue + Lock.m_Value;
}
inline int fg_ConstructedTemporary() {
  return *CBorrowingView(&t_Value).m_pValue;
}
inline CBorrowingView *fg_ConstructedOnHeap() {
  return new CBorrowingView(&t_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:29: warning: a pointer to thread-local data is stored in the 'CBorrowingView' made of it
}

// Arrays of pointers in members and on the heap, and braced scalars.
struct CPointers {
  int *m_pValues[1];
};
inline int fg_ArrayMember() {
  CPointers Pointers{{&t_Value}};
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a pointer to thread-local data is stored in the member 'm_pValues'
  return *Pointers.m_pValues[0];
}
inline int **fg_HeapArray() {
  return new int *[1]{&t_Value};
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a pointer to thread-local data is stored in an object on the heap
}
inline int **fg_HeapBraced() {
  return new int *{&t_Value};
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: a pointer to thread-local data is stored in an object on the heap
}

// Initializers outside functions: of globals, also their members and
// captures, and default member initializers, also an aggregate's and a
// template's as instantiated.
CHolder g_Holder{&t_Value, 1};
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: a pointer to thread-local data is stored in the member 'm_pValue'
auto g_fRead = [pValue = &t_Value] { return *pValue; };
// CHECK-MESSAGES: :[[@LINE-1]]:26: warning: a pointer to thread-local data is stored in the capture of 'pValue'
int *g_pBraced{&t_Value};
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: a pointer to thread-local data is stored in the variable 'g_pBraced'
CPointers g_Pointers{{&t_Value}};
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a pointer to thread-local data is stored in the member 'm_pValues'
struct CAggregateDefault {
  CHolder m_Holder = {&t_Value, 1};
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a pointer to thread-local data is stored in the member 'm_pValue'
};
template <typename t_CType> struct TCDefault {
  int *m_pValue = &t_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: a pointer to thread-local data is stored in the member 'm_pValue'
};
TCDefault<int> g_TemplateDefault;

// The object captured, not where the closure ends with the function, nor a
// copy of it.
inline int CThreadLocalObject::f_CalledInPlace() {
  return [this] { return m_Value; }();
}
inline auto CThreadLocalObject::f_CopiedObject() {
  return [*this] { return m_Value; };
}
inline auto CThreadLocalObject::f_Captured() {
  return [this] { return m_Value; };
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: a pointer to thread-local data is stored in the capture of 'this'
}

// Not what can't run.
inline void fg_Unreachable() {
  if (false)
    g_pValue = &t_Value;
  if constexpr (false)
    g_pValue = &t_Value;
  return;
  g_pValue = &t_Value;
}
inline void fg_DiscardedAlias() {
  int *pValue = &g_Value;
  if constexpr (false)
    pValue = &t_Value;
  g_pValue = pValue;
}
inline void fg_ConstantConditionalStore() {
  g_pValue = false ? &t_Value : &g_Value;
}

// Through local references to pointers, casts, moves, address-of helpers,
// structured bindings, alias cycles and the implicit move of a return.
inline void fg_StoreThroughAlias() {
  int *pValue = &t_Value;
  int *&rpValue = pValue;
  g_pValue = rpValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
inline void fg_StoreWrittenThroughAlias() {
  int *pValue = nullptr;
  int *&rpValue = pValue;
  rpValue = &t_Value;
  g_pValue = pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
inline void fg_StoreCast() {
  int *pValue = &t_Value;
  g_pValue = static_cast<int *const &>(pValue);
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
inline void fg_StoreMoved() {
  int *pValue = &t_Value;
  g_pValue = fg_Move(pValue);
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
inline void fg_StoreAddressOf() {
  g_pValue = __builtin_addressof(t_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
inline void fg_StoreBinding(CHolder &_Holder) {
  auto &[Value, pValue, Fixed, Items] = t_Data;
  _Holder.m_pValue = &Value;
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: a pointer to thread-local data is stored in the member 'm_pValue'
}
inline void fg_StoreAliasCycle() {
  int *pValue = &g_Value;
  int *pOther = pValue;
  pOther = &t_Value;
  pValue = pOther;
  g_pValue = pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
inline int *fg_ReturnLocalPointer() {
  int *pValue = &t_Value;
  return pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: a pointer to thread-local data is returned as a type that isn't a pointer to a thread-local type
}
inline void fg_StoreExplicitThread(int _ThreadID) {
  CSystemThreadLocal *pData = &fg_SystemThreadLocal(_ThreadID);
  (void)pData;
}

// What is held for each thread: each held class of a template, also in an
// argument pack, and a member of a template as instantiated.
template <typename t_CFirst, typename t_CSecond> struct TCPair {
  t_CFirst m_First;
  t_CSecond m_Second;
};
TCPair<TCThreadLocal<CHeaderData>, TCThreadLocal<CListedData>> g_PairFirst;
// CHECK-MESSAGES: :[[@LINE-1]]:64: warning: 'g_PairFirst' holds 'CHeaderData' for each thread
TCPair<TCThreadLocal<CListedData>, TCThreadLocal<CHeaderData>> g_PairSecond;
// CHECK-MESSAGES: :[[@LINE-1]]:64: warning: 'g_PairSecond' holds 'CHeaderData' for each thread
template <typename... t_CTypes> struct TCTuple {};
TCTuple<TCThreadLocal<CNestedHeaderData>> g_Tuple;
// CHECK-MESSAGES: :[[@LINE-1]]:43: warning: 'g_Tuple' holds 'CNestedHeaderData' for each thread
template <typename t_CType> struct TCHolderOf {
  TCThreadLocal<t_CType> m_Data;
// CHECK-MESSAGES: :[[@LINE-1]]:26: warning: 'm_Data' holds 'NThreadLocals::CMainData' for each thread
};
TCHolderOf<CMainData> g_HolderOf;

// A template that stores what it is given, of a type of its argument, like a
// generic pointer that clears itself: reported where it is given the data,
// where a NOLINT can tell why, not in the template. Also a function that
// stores what it can't tell is the data, of another type; not one that
// reports it itself, of a thread-local type, nor one that only reads it.
template <typename t_CType> struct TCAutoClearPtr {
  TCAutoClearPtr() = default;
  TCAutoClearPtr(t_CType *_pPtr) { mp_pPointTo = _pPtr; }
  TCAutoClearPtr &operator=(t_CType *_pPtr) {
    mp_pPointTo = _pPtr;
    return *this;
  }
  void f_Set(t_CType *_pPtr) { *this = _pPtr; }
  t_CType *mp_pPointTo = nullptr;
};
struct CInheriting {
  TCAutoClearPtr<CData> m_pInherit;
  void f_Inherit(CData &_Inherit) {
    m_pInherit = &_Inherit;
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: a pointer to thread-local data is given to 'operator=', which stores it where it can outlive the expression
  }
  void f_InheritSet(CData &_Inherit) {
    m_pInherit.f_Set(&_Inherit);
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: a pointer to thread-local data is given to 'f_Set', which stores it where it can outlive the expression
  }
  void f_InheritAllowed(CData &_Inherit) {
    m_pInherit = &_Inherit; // NOLINT(malterlib-thread-local-storage)
  }
};
// A thread-local type whose copy for a new thread keeps the one it is made
// from, of another thread, like Malterlib's test manager's.
struct CInheritingData {
  CInheritingData() = default;
  CInheritingData(CInheritingData const &_Inherit);
  TCAutoClearPtr<CInheritingData const> m_pInherit;
  int m_Value;
};
inline CInheritingData::CInheritingData(CInheritingData const &_Inherit) {
  m_Value = _Inherit.m_Value;
  m_pInherit = &_Inherit;
}
inline TCFuture<int> fg_AutoClearConstructed() {
  TCAutoClearPtr<CData> Pointer(&t_Data);
// CHECK-MESSAGES: :[[@LINE-1]]:33: warning: a pointer to thread-local data is stored in the 'TCAutoClearPtr' made of it
  co_await fg_Work();
  co_return Pointer.mp_pPointTo->m_Value;
}
inline TCFuture<int> fg_AutoClearAssignedLocal() {
  TCAutoClearPtr<CData> Pointer;
  Pointer = &t_Data;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: a pointer to thread-local data is given to 'operator=', which stores it where it can outlive the expression
  co_await fg_Work();
  co_return Pointer.mp_pPointTo->m_Value;
}
inline int fg_AutoClearAssignedLocalNotCoroutine() {
  TCAutoClearPtr<CData> Pointer;
  Pointer = &t_Data;
  return Pointer.mp_pPointTo->m_Value;
}
// Not what a temporary keeps, which ends with the expression.
struct CFormat {
  CFormat &&operator<<(int const &_Value) && {
    m_pValue = &_Value;
    return static_cast<CFormat &&>(*this);
  }
  int const *m_pValue = nullptr;
};
void fg_Report(CFormat &&_Format);
inline void fg_ReportThreadLocal() { fg_Report(CFormat() << t_Value); }
inline void fg_AutoClearOther(TCAutoClearPtr<int> &_Pointer) {
  _Pointer = &g_Value;
}
template <typename t_CType> struct TCKeepsThreadLocal {
  void f_Keep() { m_pValue = &t_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:30: warning: a pointer to thread-local data is stored in the member 'm_pValue'
  int *m_pValue;
};
inline void fg_KeepsThreadLocal(TCKeepsThreadLocal<int> &_Keeps) {
  _Keeps.f_Keep();
}
struct CDataSetter {
  void f_Set(CData *_pData) { m_pData = _pData; }
// CHECK-MESSAGES: :[[@LINE-1]]:41: warning: a pointer to thread-local data is stored in the member 'm_pData'
  CData *m_pData;
};
inline void fg_SetData(CDataSetter &_Setter) { _Setter.f_Set(&t_Data); }
struct CIntSetter {
  void f_Set(int *_pValue) { m_pValue = _pValue; }
  int *m_pValue;
};
inline void fg_SetInt(CIntSetter &_Setter) {
  _Setter.f_Set(&t_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: a pointer to thread-local data is given to 'f_Set', which stores it where it can outlive the expression
}
// Not what a function stores in a local of its own, also through a helper
// given it, like a formatter, nor what an object a pointer member points to
// gives.
struct CFormatter {
  int const *m_pArg = nullptr;
};
inline void fg_AddArgument(CFormatter &o_Formatter, int const &_Value) {
  o_Formatter.m_pArg = &_Value;
}
inline int fg_FormatValue(int const &_Value) {
  CFormatter Formatter;
  fg_AddArgument(Formatter, _Value);
  return *Formatter.m_pArg;
}
inline int fg_FormatThreadLocal() { return fg_FormatValue(t_Value); }
inline void fg_AddThreadLocal(CFormatter &o_Formatter) {
  fg_AddArgument(o_Formatter, t_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:31: warning: a reference to thread-local data is given to 'fg_AddArgument', which stores it where it can outlive the expression
}
struct CSharedArena {
  void *f_Alloc(int _Size);
};
struct CLocalArena {
  CSharedArena *m_pArena;
};
thread_local CLocalArena t_LocalArena;
inline void *fg_AllocFromArena() { return t_LocalArena.m_pArena->f_Alloc(1); }
inline int fg_ReadInt(int *_pValue) { return *_pValue; }
inline int fg_ReadThreadLocal() { return fg_ReadInt(&t_Value); }
// Not a copy, also of what a part of it points to, nor a function that
// stores in it what isn't made of it.
struct CHostInfo {
  CHostInfo() = default;
  CHostInfo(CHostInfo const &_Other) : m_pName(_Other.m_pName) {}
  CHostInfo &operator=(CHostInfo const &_Other) {
    m_pName = _Other.m_pName;
    return *this;
  }
  char const *m_pName = nullptr;
};
struct CHostInfoData {
  CHostInfo m_Info;
};
thread_local CHostInfoData t_HostInfo;
struct CHostInfoCopy {
  CHostInfoCopy(CHostInfo const &_Info) : m_Info(_Info) {}
  CHostInfo m_Info;
};
inline void fg_CopyHostInfo(CHostInfo &o_Info, CHostInfoCopy *&o_pCopy) {
  o_Info = t_HostInfo.m_Info;
  o_pCopy = new CHostInfoCopy(t_HostInfo.m_Info);
}
struct CArena {
  int *f_Checkout(CHostInfoData &_Data) {
    _Data.m_Info.m_pName = m_pName;
    return m_pValue;
  }
  char const *m_pName;
  int *m_pValue;
};
inline int *fg_Checkout(CArena &_Arena) { return _Arena.f_Checkout(t_HostInfo); }
inline void fg_CopyToHeap(int const &_Value) { new int(_Value); }
inline void fg_CopyThreadLocal() { fg_CopyToHeap(t_Value); }

// Round 96: an operator with an explicit object parameter, and a member
// function with one, of an object that stays; not of a local outside a
// coroutine.
struct CExplicitHolder {
  void operator<<(this CExplicitHolder &_Self, int *_pValue) {
    _Self.m_pValue = _pValue;
  }
  void f_Set(this CExplicitHolder &_Self, int *_pValue) {
    _Self.m_pValue = _pValue;
  }
  int *m_pValue;
};
inline void fg_ExplicitOperator(CExplicitHolder &_Holder) {
  _Holder << &t_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is given to 'operator<<', which stores it where it can outlive the expression
}
inline void fg_ExplicitSetter(CExplicitHolder &_Holder) {
  _Holder.f_Set(&t_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: a pointer to thread-local data is given to 'f_Set', which stores it where it can outlive the expression
}
inline int fg_ExplicitLocal() {
  CExplicitHolder Holder;
  Holder.f_Set(&t_Value);
  return *Holder.m_pValue;
}

// Not where a function stores it in a member that can refer to it: of a
// handler or a thread-local type; nor a store that can't run.
struct CHandlerSetter : CCoroutineThreadLocalHandler {
  void f_Suspend() noexcept override;
  void f_Set(int *_pValue) { m_pValue = _pValue; }
  int *m_pValue;
};
inline void fg_HandlerSet(CHandlerSetter &_Handler) {
  _Handler.f_Set(&t_Value);
}
inline void fg_ThreadLocalSet() { t_Data.f_Set(&t_Data.m_Value); }
template <bool t_bKeep> void fg_MaybeStore(int *_pValue) {
  if constexpr (!t_bKeep)
    return;
  g_pValue = _pValue;
}
inline void fg_MaybeStored() {
  fg_MaybeStore<false>(&t_Value);
  fg_MaybeStore<true>(&t_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a pointer to thread-local data is given to 'fg_MaybeStore<true>', which stores it where it can outlive the expression
}
struct CReader {
  CReader(int *_pValue) {
    if (true)
      return;
    m_pValue = _pValue;
  }
  int *m_pValue = nullptr;
};
inline CReader *fg_Reader() { return new CReader(&t_Value); }

// Where a constructor stores it through a local reference to a member, in a
// reference member of an aggregate, or a pointer given by reference.
struct CSlotView {
  CSlotView(int *_pValue) {
    auto &Slot = m_pValue;
    Slot = _pValue;
  }
  int *m_pValue;
};
inline CSlotView *fg_SlotView() {
  return new CSlotView(&t_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a pointer to thread-local data is stored in the 'CSlotView' made of it
}
struct CRefPart {
  int &m_Value;
};
struct CRefView {
  CRefView(int &_Value) : m_Ref{_Value} {}
  CRefPart m_Ref;
};
inline CRefView *fg_RefView() {
  return new CRefView(t_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a reference to thread-local data is stored in the 'CRefView' made of it
}
struct CPointerRefHolder {
  CPointerRefHolder(int *const &_pValue) : m_pValue(_pValue) {}
  int *m_pValue;
};
inline CPointerRefHolder *fg_PointerRef() {
  return new CPointerRefHolder(&t_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:32: warning: a pointer to thread-local data is stored in the 'CPointerRefHolder' made of it
}

// A local holder that leaves its function.
inline CBorrowingView fg_ViewEscapes() {
  CBorrowingView View(&t_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a pointer to thread-local data is stored in the 'CBorrowingView' made of it
  return View;
}

// An array decayed, assignments as values, pointers stepped, a parameter
// assigned; not an assignment that can't run, nor a copy a lambda changes,
// nor what a local pointer to a local points to.
int *g_pArray = t_Values;
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: a pointer to thread-local data is stored in the variable 'g_pArray'
inline int **fg_HeapDecayed() {
  return new int *(t_Values);
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: a pointer to thread-local data is stored in an object on the heap
}
inline void fg_AssignmentStored() {
  int *pValue = nullptr;
  g_pValue = (pValue = &t_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
inline void fg_PostIncrementStored() {
  int *pValue = t_Values;
  g_pValue = pValue++;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
inline int *fg_ReturnAssignedParameter(int *_pValue) {
  _pValue = &t_Value;
  return _pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: a pointer to thread-local data is returned as a type that isn't a pointer to a thread-local type
}
inline int *fg_UnreachableAssignment() {
  int *pValue = &g_Value;
  if (true)
    return pValue;
  pValue = &t_Value;
  return pValue;
}
inline void fg_CaptureWrite() {
  int *pValue = nullptr;
  auto fSet = [pValue]() mutable { pValue = &t_Value; };
  fSet();
  g_pValue = pValue;
}
inline void fg_PointerToLocalSlot() {
  int *pValue = nullptr;
  int **ppValue = &pValue;
  *ppValue = &t_Value;
}

// Captures: a structured binding, a copy of an array of pointers, a copy of
// a reference to a pointer.
inline auto fg_CaptureBinding() {
  auto &[Value, pValue, Fixed, Items] = t_Data;
  return [&Value] { return Value; };
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: a reference to thread-local data is stored in the capture of 'Value'
}
inline auto fg_CaptureArray() {
  int *pValues[1] = {&t_Value};
  return [pValues] { return *pValues[0]; };
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: a pointer to thread-local data is stored in the capture of 'pValues'
}
inline auto fg_CapturePointerReference() {
  int *pValue = &t_Value;
  int *&rpValue = pValue;
  return [rpValue] { return *rpValue; };
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: a pointer to thread-local data is stored in the capture of 'rpValue'
}

// Generic lambdas as instantiated; a lambda's own control flow; closures
// local but for a suspension that can run, also in their captures.
inline void fg_GenericLambdas() {
  auto fMaybe = []<bool t_bStore>() {
    if constexpr (t_bStore)
      g_pValue = &t_Value;
  };
  fMaybe.template operator()<false>();
  auto fStore = [](auto &_Holder) {
    _Holder.m_pValue = &t_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a pointer to thread-local data is stored in the member 'm_pValue'
  };
  CHolder Holder;
  fStore(Holder);
}
inline void fg_LambdaUnreachable() {
  auto fStore = [] {
    if (true)
      return;
    g_pValue = &t_Value;
  };
  fStore();
}
inline TCFuture<int> fg_ClosureUnreachableSuspension() {
  int *pValue = &t_Value;
  auto fRead = [pValue] { return *pValue; };
  if (false)
    co_await fg_Work();
  co_return fRead();
}
inline TCFuture<int> fg_ClosureCaptureSuspension() {
  auto fUse = [pValue = &t_Value, Result = co_await fg_Work()] { return *pValue + Result; };
// CHECK-MESSAGES: :[[@LINE-1]]:25: warning: a pointer to thread-local data is stored in the capture of 'pValue'
  co_return fUse();
}

// What is held for each thread on the heap.
inline void *fg_HeapThreadLocal() {
  return new TCThreadLocal<CHeaderData>;
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the object made on the heap holds 'CHeaderData' for each thread
}

// A template's store of what is the data by itself, also as a value of a
// conditional with what it is given; not what a thread named gives, also
// through a reference member or a local.
template <typename t_CType> struct TCChoosingHolder {
  void f_Set(t_CType *_pValue, bool _bChoose) {
    m_pValue = _bChoose ? _pValue : &t_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: a pointer to thread-local data is stored in the member 'm_pValue'
  }
  int *m_pValue;
};
inline void fg_Choose(TCChoosingHolder<int> &_Holder, int *_pOther) {
  _Holder.f_Set(_pOther, true);
}
inline void fg_StoreExplicitMember(int _ThreadID) {
  g_pValue = &fg_SystemThreadLocal(_ThreadID).m_Data.m_Value;
}
inline int fg_StoreExplicitLocal(int _ThreadID) {
  CSystemThreadLocal *pData = &fg_SystemThreadLocal(_ThreadID);
  static auto *s_pSaved = pData;
  return s_pSaved->m_Value;
}

// Not a member of a thread-local type, of a scope the runtime moves, nor of
// ThreadLocalScopeTypes.
CConcurrencyThreadLocal::CConcurrencyThreadLocal()
    : m_SystemThreadLocal(fg_SystemThreadLocal()) {}
inline CDataScope fg_HandlerMember() {
  return CDataScope(fg_ConcurrencyThreadLocal());
}
inline CCurrentActorScope fg_ScopeMember() {
  return CCurrentActorScope(fg_ConcurrencyThreadLocal());
}
} // namespace NThreadLocals

