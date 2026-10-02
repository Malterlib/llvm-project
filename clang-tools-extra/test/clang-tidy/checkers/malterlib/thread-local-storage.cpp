// RUN: %check_clang_tidy -std=c++23-or-later %s malterlib-thread-local-storage %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml', malterlib-thread-local-storage.ThreadLocalTypes: 'CData;NThreadLocals::CConcurrencyThreadLocal;NThreadLocals::CSystemThreadLocal;CListedData;NThreadLocals::CPrivate;NThreadLocals::CThreadLocalObject;NThreadLocals::CInheritingData;NThreadLocals::CCopiedObject;NThreadLocals::CRefData;NThreadLocals::CBorrowView;NThreadLocals::CCopiedAlias;NThreadLocals::CR107DerivedData;NThreadLocals::CR109YBytes'}}" -- -I %S/Inputs

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
// A member whose coroutines read it (round 187): one that can't suspend,
// one that can.
struct CStatistics {
  int m_Count;
  TCFuture<int> f_Read() const { co_return m_Count; }
  TCFuture<int> f_ReadLater() const {
    co_await fg_Work();
    co_return m_Count;
  }
};
struct CConcurrencyThreadLocal {
  CConcurrencyThreadLocal();
  CSystemThreadLocal &m_SystemThreadLocal;
  int m_Value;
  CStatistics m_Statistics;
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
};
// Where the constructor the compiler defines runs it, reported there.
inline CDefault *fg_MakeDefault() { return new CDefault; }
// CHECK-MESSAGES: :[[@LINE-1]]:48: warning: a pointer to thread-local data is stored in the member 'm_pValue'
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
// that can be seen that stores it, one that can't of a class that can hold
// it, also const (round 102); not one that copies, a temporary, nor a local
// of a function that isn't a coroutine.
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
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a pointer to thread-local data is stored in the 'CInvisibleView' made of it
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
// template's as instantiated, reported where what has them is made.
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
};
inline CAggregateDefault *fg_MakeAggregateDefault() {
  return new CAggregateDefault;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the member 'm_pValue'
}
template <typename t_CType> struct TCDefault {
  int *m_pValue = &t_Value;
};
TCDefault<int> g_TemplateDefault;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: a pointer to thread-local data is stored in the member 'm_pValue'


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
};
TCHolderOf<CMainData> g_HolderOf;
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: 'm_Data' holds 'NThreadLocals::CMainData' for each thread

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

// Round 97: a reference to a pointer slot of the data returned; a pointer
// stored where a local pointer to the heap points, by a function given it; a
// pointer receiver by what it points to; not a value a constant condition
// leaves out; each value of a conditional made into an object; an aggregate
// in a handler; a base given it by a constructor; a copy captured by what it
// is given where the lambda is made.
inline int *&fg_ReturnSlot() {
  return t_Data.m_pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: a reference to thread-local data is returned as a type that isn't a reference to a thread-local type
}
int **g_ppSlot;
inline void fg_StoreThroughHeapSlot(int *_pValue) {
  int **ppSlot = new int *;
  *ppSlot = _pValue;
  g_ppSlot = ppSlot;
}
inline void fg_HeapSlotGiven() {
  fg_StoreThroughHeapSlot(&t_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:27: warning: a pointer to thread-local data is given to 'fg_StoreThroughHeapSlot', which stores it where it can outlive the expression
}
struct CStore {
  void f_Set(int *_pValue) { m_pValue = _pValue; }
  int *m_pValue;
};
CStore g_Store;
inline void fg_PointerReceiver() {
  CStore *pStore = &g_Store;
  pStore->f_Set(&t_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: a pointer to thread-local data is given to 'f_Set', which stores it where it can outlive the expression
}
struct CMaybeView {
  CMaybeView(int *_pValue) : m_pValue(false ? _pValue : nullptr) {}
  int *m_pValue;
};
inline CMaybeView *fg_MaybeView() { return new CMaybeView(&t_Value); }
inline CHolder fg_ConditionalObject(bool _bChoose) {
  return _bChoose ? CHolder{&t_Value, 1} : CHolder{nullptr, 0};
// CHECK-MESSAGES: :[[@LINE-1]]:29: warning: a pointer to thread-local data is stored in the member 'm_pValue'
}
struct CHandlerView : CCoroutineThreadLocalHandler {
  void f_Suspend() noexcept override;
  CHolder m_View{&t_Value, 1};
};
struct CKeepingBase {
  CKeepingBase(int *_pValue) : m_pValue(_pValue) {}
  int *m_pValue;
};
struct CDerivedKeeper : CKeepingBase {
  CDerivedKeeper(CData &_Data) : CKeepingBase(&_Data.m_Value) {}
// CHECK-MESSAGES: :[[@LINE-1]]:47: warning: a pointer to thread-local data is stored in the 'CKeepingBase' made of it
};
// Not what a temporary keeps that a builder gives back of itself, also given
// on in the same expression, like a format of a string.
struct CFormatBuilder {
  CFormatBuilder &&operator<<(int const &_Value) && {
    m_pArgs[m_nArgs++] = &_Value;
    return static_cast<CFormatBuilder &&>(*this);
  }
  int const *m_pArgs[4];
  int m_nArgs = 0;
};
struct CObserved {
  void f_Insert(CFormatBuilder &&_Format);
};
inline void fg_ObserveThreadLocal(CObserved &_Observed) {
  auto &Data = t_Data;
  _Observed.f_Insert(CFormatBuilder() << Data.m_Value << Data.m_Fixed[0]);
}

// What is held for each thread by a template that makes it on the heap, where
// it is declared, not in the template.
template <typename t_CType> struct TCHeapHolder {
  TCHeapHolder() : m_pValue(new t_CType()) {}
  t_CType *m_pValue;
};
TCHeapHolder<TCThreadLocal<CNestedHeaderData>> g_HeapHolder;
// CHECK-MESSAGES: :[[@LINE-1]]:48: warning: 'g_HeapHolder' holds 'CNestedHeaderData' for each thread

// A local holder that leaves its function as a copy, through a reference to
// it or captured; not by a return that can't run. Thread-local variables of
// the same thread; a scope exit's guard run where it is.
CBorrowingView g_View{nullptr};
inline CBorrowingView fg_ViewCopyEscapes() {
  CBorrowingView View(&t_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a pointer to thread-local data is stored in the 'CBorrowingView' made of it
  CBorrowingView Copy = View;
  return Copy;
}
inline void fg_ViewAliasEscapes() {
  CBorrowingView View(&t_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a pointer to thread-local data is stored in the 'CBorrowingView' made of it
  CBorrowingView &Alias = View;
  g_View = Alias;
}
inline auto fg_ViewCaptured() {
  CBorrowingView View(&t_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a pointer to thread-local data is stored in the 'CBorrowingView' made of it
  return [View] { return *View.m_pValue; };
}
inline CBorrowingView fg_ViewReturnCantRun() {
  CBorrowingView View(&t_Value);
  if (false)
    return View;
  return CBorrowingView(&g_Value);
}
thread_local int *t_pSaved = &t_Value;
inline void fg_SaveThreadLocal() { t_pSaved = &t_Value; }
inline void fg_ScopeExitRun() {
  auto &Data = t_Data;
  int Old = Data.m_Value;
  auto Cleanup = g_OnScopeExit / [&] { Data.m_Value = Old; };
  Cleanup();
}

inline auto fg_CaptureCopyAtCapture() {
  int *pValue = &t_Value;
  pValue = &g_Value;
  return [pValue] { return *pValue; };
}

// Round 98: a local holder that leaves its function by an assignment, a
// static, a cast to a reference, a call given it or a pointer of it; a
// constructor of a local that stores elsewhere; a local a setter stores in
// that leaves; not a use of a closure that can't run; a copied pointer of a
// structured binding; a typed pointer in thread-local storage.
struct CViewCache {
  void f_Set(CBorrowingView const &_View) { m_View = _View; }
  CBorrowingView m_View{nullptr};
};
CViewCache g_Cache;
inline CBorrowingView fg_ViewAssignedEscapes() {
  CBorrowingView View(&t_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a pointer to thread-local data is stored in the 'CBorrowingView' made of it
  CBorrowingView Copy(nullptr);
  Copy = View;
  return Copy;
}
inline void fg_ViewStaticEscapes() {
  CBorrowingView View(&t_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a pointer to thread-local data is stored in the 'CBorrowingView' made of it
  static CBorrowingView s_Saved = View;
}
inline CBorrowingView fg_ViewCastEscapes() {
  CBorrowingView View(&t_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a pointer to thread-local data is stored in the 'CBorrowingView' made of it
  return static_cast<CBorrowingView &&>(View);
}
inline void fg_ViewGivenEscapes() {
  CBorrowingView View(&t_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a pointer to thread-local data is stored in the 'CBorrowingView' made of it
  g_Cache.f_Set(View);
}
inline int *fg_ViewPartEscapes() {
  CBorrowingView View(&t_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a pointer to thread-local data is stored in the 'CBorrowingView' made of it
  return View.m_pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: a pointer to thread-local data is returned as a type that isn't a pointer to a thread-local type
}
struct CRegistration {
  CRegistration(int *_pValue) { g_pValue = _pValue; }
};
inline void fg_Registration() {
  CRegistration Registration(&t_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:30: warning: a pointer to thread-local data is stored in the 'CRegistration' made of it
}
inline TCAutoClearPtr<int> fg_SetterLocalEscapes() {
  TCAutoClearPtr<int> Pointer;
  Pointer = &t_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: a pointer to thread-local data is given to 'operator=', which stores it where it can outlive the expression
  return Pointer;
}
inline int fg_ClosureUseCantRun() {
  auto &Data = t_Data;
  auto fRead = [&] { return Data.m_Value; };
  if (false)
    fg_Keep(fRead);
  return fRead();
}
inline auto fg_CaptureBindingPointer() {
  int *pValues[1] = {&t_Value};
  auto &[pValue] = pValues;
  return [pValue] { return *pValue; };
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: a pointer to thread-local data is stored in the capture of 'pValue'
}
struct CTypedHolder {
  CData *m_pData;
};
thread_local CTypedHolder t_TypedHolder;
inline void fg_TypedSlot() { t_TypedHolder.m_pData = &t_Data; }

// Each destination the effect table tells of (EDestination), by how long it
// lives: a static, a global, the heap, a member and what an output parameter
// refers to outlive the expression; a local, thread-local storage and a
// temporary don't; a local holder only when it escapes, also by a getter, a
// pointer to it, or a closure, an array of them too.
inline int &fg_HolderEscapesByReference() {
  CBorrowingView View(&t_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a pointer to thread-local data is stored in the 'CBorrowingView' made of it
  return *View.m_pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: a reference to thread-local data is returned as a type that isn't a reference to a thread-local type
}
struct CGetterView {
  CGetterView(int *_pValue) : m_pValue(_pValue) {}
  int *f_Get() const { return m_pValue; }
  int *m_pValue;
};
inline int *fg_HolderEscapesByGetter() {
  CGetterView View(&t_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: a pointer to thread-local data is stored in the 'CGetterView' made of it
  return View.f_Get();
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: a pointer to thread-local data is returned as a type that isn't a pointer to a thread-local type
}
inline int fg_HolderReadByGetter() {
  CGetterView View(&t_Value);
  return *View.f_Get();
}
struct CSettable {
  void f_Set(int *_pValue) { m_pValue = _pValue; }
  int *m_pValue = nullptr;
};
inline CSettable fg_HolderEscapesThroughPointer() {
  CSettable Settable;
  auto *pSettable = &Settable;
  pSettable->f_Set(&t_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: a pointer to thread-local data is given to 'f_Set', which stores it where it can outlive the expression
  return Settable;
}
inline auto fg_HolderArrayCaptured() {
  CBorrowingView Views[1]{&t_Value};
// CHECK-MESSAGES: :[[@LINE-1]]:27: warning: a pointer to thread-local data is stored in the 'CBorrowingView' made of it
  return [Views] { return *Views[0].m_pValue; };
}
inline int fg_HolderUnevaluated() {
  CBorrowingView View(&t_Value);
  (void)sizeof(View);
  return *View.m_pValue;
}
inline int fg_ClosureUnevaluated() {
  auto fRead = [pValue = &t_Value] { return *pValue; };
  (void)sizeof(fRead);
  return fRead();
}
struct CSettableOuter {
  CSettable m_Settable;
};
inline TCFuture<int> fg_CoroutineLocalPart() {
  CSettableOuter Outer;
  Outer.m_Settable.f_Set(&t_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:26: warning: a pointer to thread-local data is given to 'f_Set', which stores it where it can outlive the expression
  co_await fg_Work();
  co_return 0;
}

// Round 99: thread-local destinations through setters, references, captures
// and assignments of objects; what an exempt constructor stores elsewhere;
// closures called in place or unreachable in initializers. Not a constructor
// without a body given a thread-local type, which reports itself where it is
// defined, nor what a delegated constructor keeps in the object, which is the
// object's own (decided by locality).
thread_local CSettable t_Settable;
inline void fg_ThreadLocalSetter() { t_Settable.f_Set(&t_Value); }
thread_local int *t_pSlot;
inline void fg_ThreadLocalThroughReference() {
  int *&rSlot = t_pSlot;
  rSlot = &t_Value;
}
thread_local CHolder t_AssignedHolder;
inline void fg_ThreadLocalAssignedObject() {
  t_AssignedHolder = CHolder{&t_Value, 1};
}
thread_local auto t_fReadValue = [pValue = &t_Value] { return *pValue; };
struct CCounter {
  int &f_Counter() {
    static thread_local int s_Value;
    return s_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: a reference to thread-local data is returned as a type that isn't a reference to a thread-local type
  }
};
CCounter g_Counter;
inline void fg_StoreGetterOfThreadLocal() {
  g_pValue = &g_Counter.f_Counter();
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
template <typename t_CType>
void fg_StoreThroughPatternAlias(bool _bChoose, t_CType *_pValue) {
  int *pLocal = _bChoose ? _pValue : &t_Value;
  g_pValue = pLocal;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
inline void fg_InstantiatePatternAlias() { fg_StoreThroughPatternAlias(true, &g_Value); }
inline int fg_StaticThreadLocalClosure() {
  static thread_local auto s_fRead = [pValue = &t_Value] { return *pValue; };
  return s_fRead();
}
struct CRegisteringHandler : CCoroutineThreadLocalHandler {
  CRegisteringHandler(int *_pValue) { g_pValue = _pValue; }
  void f_Suspend() noexcept override;
};
inline void fg_ExemptConstructorStoresElsewhere() {
  CRegisteringHandler Handler(&t_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:31: warning: a pointer to thread-local data is stored in the 'CRegisteringHandler' made of it
}
struct CUnseenView {
  CUnseenView(CData &_Data);
  CData &m_Data;
};
CUnseenView g_UnseenView(t_Data);
struct CDelegating {
  CDelegating(int *_pValue, int) : m_pValue(_pValue) {}
  CDelegating() : CDelegating(&t_Value, 0) {}
  int *m_pValue;
};
int g_CalledInPlace = [pValue = &t_Value] { return *pValue; }();
struct CCalledInPlaceMember {
  int m_Value = [pValue = &t_Value] { return *pValue; }();
};
auto g_fUnreachableStore = [] {
  if (true)
    return;
  g_pValue = &t_Value;
};

// Round 100: what a default argument gives, also a default member
// initializer and what a default argument evaluates; what a constructor a
// derived class inherits keeps; temporaries a reference extends, the array of
// a list, what is assigned or copied from a temporary; a pointer stepped by a
// prefix or a compound step, not a thread-local pointer stepped; what a
// pointer a thread-local variable holds points to; not what is kept in a
// thread-local variable or reached through an accessor; the argument a helper
// gives back, also `__builtin_addressof`; calls through local pointers to
// functions and member functions; a constructor that can't be seen, by what
// its class can hold.
inline void fg_StoreParameter(int *_pValue = &t_Value) { g_pValue = _pValue; }
inline void fg_StoreDefaulted() {
  fg_StoreParameter();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: a pointer to thread-local data is given to 'fg_StoreParameter', which stores it
}
struct CDefaultHolder {
  CDefaultHolder(int *_pValue = &t_Value) : m_pValue(_pValue) {}
  int *m_pValue;
};
inline CDefaultHolder fg_ReturnDefaultHolder() {
  return CDefaultHolder();
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: a pointer to thread-local data is stored in the 'CDefaultHolder' made of it
}
inline int fg_SaveGlobal(int *_pValue) {
  g_pValue = _pValue;
  return 0;
}
void fg_RunDefault(int _Value = fg_SaveGlobal(&t_Value));
inline void fg_RunDefaulted() {
  fg_RunDefault();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: a pointer to thread-local data is given to 'fg_SaveGlobal', which stores it
}
struct CStoringBase {
  CStoringBase(int *_pValue) : m_pValue(_pValue) {}
  int *m_pValue;
};
struct CStoringDerived : CStoringBase {
  using CStoringBase::CStoringBase;
};
CStoringDerived g_StoringDerived(&t_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:34: warning: a pointer to thread-local data is stored in the 'CStoringDerived' made of it
inline CStoringDerived *fg_NewStoringDerived() {
  return new CStoringDerived(&t_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:30: warning: a pointer to thread-local data is stored in the 'CStoringDerived' made of it
}
struct CTemporaryView {
  int *m_pValue;
};
static const CTemporaryView &g_ExtendedView = CTemporaryView{&t_Value};
// CHECK-MESSAGES: :[[@LINE-1]]:62: warning: a pointer to thread-local data is stored in the member 'm_pValue'
inline int fg_ExtendedLocal() {
  const CTemporaryView &View = CTemporaryView{&t_Value};
// CHECK-MESSAGES: :[[@LINE-1]]:47: warning: a pointer to thread-local data is stored in the member 'm_pValue'
  return *View.m_pValue;
}
CTemporaryView g_AssignedView;
inline void fg_AssignTemporaryView() {
  g_AssignedView = CTemporaryView{&t_Value};
// CHECK-MESSAGES: :[[@LINE-1]]:35: warning: a pointer to thread-local data is stored in the member 'm_pValue'
}
CTemporaryView g_CopiedView(static_cast<const CTemporaryView &>(CTemporaryView{&t_Value}));
// CHECK-MESSAGES: :[[@LINE-1]]:80: warning: a pointer to thread-local data is stored in the member 'm_pValue'
} // namespace NThreadLocals
namespace std {
template <typename t_CElement> class initializer_list {
  const t_CElement *m_pBegin;
  decltype(sizeof(0)) m_Size;

public:
  bool f_IsEmpty() const { return m_Size == 0; }
};
} // namespace std
namespace NThreadLocals {
std::initializer_list<int *> g_ListOfPointers{&t_Value};
// CHECK-MESSAGES: :[[@LINE-1]]:47: warning: a pointer to thread-local data is stored in the variable 'g_ListOfPointers'
inline int fg_LocalListOfPointers() {
  std::initializer_list<int *> List{&t_Value};
  return List.f_IsEmpty();
}
inline void fg_StoreStepped() {
  int *pValue = t_Values;
  g_pValue = ++pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
inline void fg_StoreCompoundStepped() {
  int *pValue = t_Values;
  g_pValue = (pValue += 1);
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
int g_Values[4];
thread_local int *t_pCursor = g_Values;
inline void fg_StoreSteppedThreadLocalSlot() {
  g_pValue = t_pCursor++;
  g_pValue = ++t_pCursor;
  g_pValue = (t_pCursor += 1);
}
thread_local CSettable *t_pSettable;
inline void fg_SetThroughThreadLocalPointer() {
  t_pSettable->f_Set(&t_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: a pointer to thread-local data is given to 'f_Set', which stores it
}
inline void fg_KeepInThreadLocal(int *_pValue) {
  static thread_local int *s_pValue = _pValue;
  (void)s_pValue;
}
inline void fg_KeepInThreadLocalCaller() { fg_KeepInThreadLocal(&t_Value); }
template <typename t_CType> struct TCPointerView {
  t_CType *m_pValue;
  void f_Set(t_CType *_pValue) { m_pValue = _pValue; }
};
TCThreadLocal<TCPointerView<int>> g_PointerView;
inline void fg_SetThroughAccessor() { g_PointerView.f_Get()->f_Set(&t_Value); }
inline int *fg_Second(int *, int *_pSecond) { return _pSecond; }
struct CSecondView {
  CSecondView(int *_pFirst, int *_pSecond)
      : m_pValue(fg_Second(_pFirst, _pSecond)) {}
  int *m_pValue;
};
inline CSecondView *fg_NewSecondView() {
  return new CSecondView(&g_Value, &t_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:36: warning: a pointer to thread-local data is stored in the 'CSecondView' made of it
}
inline CSecondView *fg_NewFirstView() {
  return new CSecondView(&t_Value, &g_Value);
}
struct CAddressView {
  CAddressView(int &_Value) : m_pValue(__builtin_addressof(_Value)) {}
  int *m_pValue;
};
inline CAddressView fg_ReturnAddressView() {
  return CAddressView(t_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a reference to thread-local data is stored in the 'CAddressView' made of it
}
inline void fg_StorePointer(int *_pValue) { g_pValue = _pValue; }
inline void fg_StoreThroughLocalPointer() {
  auto fStore = &fg_StorePointer;
  fStore(&t_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: a pointer to thread-local data is given to 'fg_StorePointer', which stores it
}
inline void fg_StoreThroughMemberPointer(CSettable &_Settable) {
  auto pmSet = &CSettable::f_Set;
  (_Settable.*pmSet)(&t_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: a pointer to thread-local data is given to 'f_Set', which stores it
}
struct CCountOnly {
  CCountOnly(int &_Value);
  int m_Count;
};
inline CCountOnly fg_ReturnCountOnly() { return CCountOnly(t_Value); }
struct CErasedOnly {
  CErasedOnly(int &_Value);
  void *m_pErased;
};
inline CErasedOnly fg_ReturnErasedOnly() { return CErasedOnly(t_Value); }
struct CPointerOnly {
  CPointerOnly(int &_Value);
  int *m_pValue;
};
inline CPointerOnly fg_ReturnPointerOnly() {
  return CPointerOnly(t_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a reference to thread-local data is stored in the 'CPointerOnly' made of it
}


// Round 101: not what can't run in a member initializer, also of a summary;
// not what a copy of the object holds in a closure; a setter of an explicit
// object called through a pointer; not what a builtin doesn't evaluate; what
// a handler stores through a pointer it holds, not in its own parts, also
// inherited; a binding of what a reference member refers to; what a class
// holds for each thread through a base; a closure a parameter is captured in
// that ends up on the heap; a local list in a coroutine; a copy on the heap;
// not what a getter gives through a pointer, nor what a reference member
// refers to; the address of the slot an assignment gives back; not the value
// loaded through a reference parameter.
struct CUnreachableInit {
  int m_Value;
  CUnreachableInit() : m_Value(false ? (g_pValue = &t_Value, 1) : 0) {}
};
struct CUnreachableStore {
  CUnreachableStore(int *_pValue) : m_Value(false ? fg_SaveGlobal(_pValue) : 0) {}
  int m_Value;
};
CUnreachableStore g_UnreachableStore(&t_Value);
struct CCopiedObject {
  int m_Value;
  auto f_Copied() {
    return [*this] { return &m_Value; };
  }
  auto f_Nested() {
    return [*this] { return [this] { return &m_Value; }; };
  }
};
struct CExplicitSetHolder {
  int *m_pValue;
  void f_Set(this CExplicitSetHolder &_Self, int *_pValue) { _Self.m_pValue = _pValue; }
};
CExplicitSetHolder g_ExplicitHolder;
inline void fg_SetExplicitThroughPointer() {
  auto fSet = &CExplicitSetHolder::f_Set;
  fSet(g_ExplicitHolder, &t_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:26: warning: a pointer to thread-local data is given to 'f_Set', which stores it
}
inline int fg_UnevaluatedStore() { return __builtin_constant_p(g_pValue = &t_Value); }
struct CExternalHandler : CCoroutineThreadLocalHandler {
  CSettable *m_pExternal;
  void f_Suspend() noexcept override {
    m_pExternal->f_Set(&t_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a pointer to thread-local data is given to 'f_Set', which stores it
  }
};
struct CInnerState {
  int *m_pValue;
};
struct CHandlerBase {
  int *m_pBaseValue;
};
struct CNestedHandler : CCoroutineThreadLocalHandler, CHandlerBase {
  CInnerState m_Inner;
  void f_Suspend() noexcept override {
    m_Inner.m_pValue = &t_Value;
    m_pBaseValue = &t_Value;
  }
};
struct CSlots {
  int *&m_rpSlot;
};
int *g_pGlobalSlot;
thread_local auto [t_rpSlot] = CSlots{g_pGlobalSlot};
inline void fg_StoreThroughBinding() {
  t_rpSlot = &t_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in
}
struct CUnlistedPayload {
  int m_Value;
};
struct CMyThreadLocal : TCThreadLocal<CUnlistedPayload> {};
CMyThreadLocal g_MyThreadLocal;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'g_MyThreadLocal' holds
inline void fg_KeepInClosure(int *_pValue) {
  auto *pClosure = new auto([_pValue] { return *_pValue; });
  (void)pClosure;
}
inline void fg_KeepInClosureCaller() {
  fg_KeepInClosure(&t_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: a pointer to thread-local data is given to 'fg_KeepInClosure', which stores it
}
inline TCFuture<int> fg_ListInCoroutine() {
  std::initializer_list<int *> List{&t_Value};
// CHECK-MESSAGES: :[[@LINE-1]]:37: warning: a pointer to thread-local data is stored in
  co_await fg_Work();
  co_return List.f_IsEmpty();
}
struct CCopiedData {
  int m_Count;
  int *m_pValue;
};
thread_local CCopiedData t_CopiedData;
inline void fg_HeapCopy() {
  auto *pCopy = new CCopiedData(t_CopiedData);
  pCopy->m_pValue = &t_CopiedData.m_Count;
// CHECK-MESSAGES: :[[@LINE-1]]:21: warning: a pointer to thread-local data is stored in the member 'm_pValue'
}
struct CBorrowView {
  int *m_pValue;
  int &f_Get() { return *m_pValue; }
};
thread_local CBorrowView t_BorrowView{&g_Value};
inline void fg_StoreBorrowed() { g_pValue = &t_BorrowView.f_Get(); }
struct CRefData {
  int &m_rValue;
};
template <typename t_CData> void fg_KeepRefMember(t_CData &_Data) {
  g_pValue = &_Data.m_rValue;
}
CRefData &fg_GetRefData();
inline void fg_KeepRefMemberCaller() { fg_KeepRefMember(fg_GetRefData()); }
thread_local int *t_pAssignSlot;
int **g_ppSaved;
inline void fg_SaveAssignedSlot() {
  g_ppSaved = &(t_pAssignSlot = &g_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: a pointer to thread-local data is stored in the variable 'g_ppSaved'
}
inline void fg_SaveLoaded(int *const &_pValue) { g_pValue = _pValue; }
thread_local int *t_pLoadedSlot = &g_Value;
inline void fg_SaveLoadedCaller() { fg_SaveLoaded(t_pLoadedSlot); }


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

inline void fg_MatrixRootVariableLoad() {
  g_pMatrix = t_pMatrixRoot;
}
inline void fg_MatrixRootVariableCopy() {
  int *pCopy = t_pMatrixRoot;
  g_pMatrix = pCopy;
}
inline void fg_MatrixRootVariableStore() {
  t_pMatrixRoot = &t_Value;
}
inline void fg_MatrixRootVariableAddress() {
  g_ppMatrix = &t_pMatrixRoot;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: a pointer to thread-local data is stored in the variable 'g_ppMatrix'
}
inline void fg_MatrixRootElementLoad() {
  g_pMatrix = t_pMatrixRoots[0];
}
inline void fg_MatrixRootElementCopy() {
  int *pCopy = t_pMatrixRoots[0];
  g_pMatrix = pCopy;
}
inline void fg_MatrixRootElementStore() {
  t_pMatrixRoots[0] = &t_Value;
}
inline void fg_MatrixRootElementAddress() {
  g_ppMatrix = &t_pMatrixRoots[0];
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: a pointer to thread-local data is stored in the variable 'g_ppMatrix'
}
inline void fg_MatrixRootMemberLoad() {
  g_pMatrix = t_MatrixRoot.m_pValue;
}
inline void fg_MatrixRootMemberCopy() {
  int *pCopy = t_MatrixRoot.m_pValue;
  g_pMatrix = pCopy;
}
inline void fg_MatrixRootMemberStore() {
  t_MatrixRoot.m_pValue = &t_Value;
}
inline void fg_MatrixRootMemberAddress() {
  g_ppMatrix = &t_MatrixRoot.m_pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: a pointer to thread-local data is stored in the variable 'g_ppMatrix'
}
inline void fg_MatrixRootReferenceMemberLoad() {
  CMatrixRef Ref{t_pMatrixRoot};
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: a pointer to thread-local data is stored in the member 'm_rpValue'
  g_pMatrix = Ref.m_rpValue;
}
inline void fg_MatrixRootReferenceMemberCopy() {
  CMatrixRef Ref{t_pMatrixRoot};
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: a pointer to thread-local data is stored in the member 'm_rpValue'
  int *pCopy = Ref.m_rpValue;
  g_pMatrix = pCopy;
}
inline void fg_MatrixRootReferenceMemberStore() {
  CMatrixRef Ref{t_pMatrixRoot};
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: a pointer to thread-local data is stored in the member 'm_rpValue'
  Ref.m_rpValue = &t_Value;
}
inline void fg_MatrixRootReferenceMemberAddress() {
  CMatrixRef Ref{t_pMatrixRoot};
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: a pointer to thread-local data is stored in the member 'm_rpValue'
  g_ppMatrix = &Ref.m_rpValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: a pointer to thread-local data is stored in the variable 'g_ppMatrix'
}
inline void fg_MatrixRootDereferenceLoad() {
  int **ppSlot = &t_pMatrixRoot;
  g_pMatrix = *ppSlot;
}
inline void fg_MatrixRootDereferenceCopy() {
  int **ppSlot = &t_pMatrixRoot;
  int *pCopy = *ppSlot;
  g_pMatrix = pCopy;
}
inline void fg_MatrixRootDereferenceStore() {
  int **ppSlot = &t_pMatrixRoot;
  *ppSlot = &t_Value;
}
inline void fg_MatrixRootDereferenceAddress() {
  int **ppSlot = &t_pMatrixRoot;
  g_ppMatrix = &*ppSlot;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: a pointer to thread-local data is stored in the variable 'g_ppMatrix'
}
inline void fg_MatrixRootBitCastLoad() {
  g_pMatrix = __builtin_bit_cast(int *, t_pMatrixRoot);
}
inline void fg_MatrixRootBitCastCopy() {
  int *pCopy = __builtin_bit_cast(int *, t_pMatrixRoot);
  g_pMatrix = pCopy;
}
inline void fg_MatrixBorrowedVariableLoad() {
  g_pMatrix = *t_ppMatrixBorrowed;
}
inline void fg_MatrixBorrowedVariableCopy() {
  int *pCopy = *t_ppMatrixBorrowed;
  g_pMatrix = pCopy;
}
inline void fg_MatrixBorrowedVariableStore() {
  *t_ppMatrixBorrowed = &t_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:25: warning: a pointer to thread-local data is stored in
}
inline void fg_MatrixBorrowedVariableAddress() {
  g_ppMatrix = &*t_ppMatrixBorrowed;
}
inline void fg_MatrixBorrowedElementLoad() {
  g_pMatrix = t_ppMatrixBorroweds[0];
}
inline void fg_MatrixBorrowedElementCopy() {
  int *pCopy = t_ppMatrixBorroweds[0];
  g_pMatrix = pCopy;
}
inline void fg_MatrixBorrowedElementStore() {
  t_ppMatrixBorroweds[0] = &t_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:28: warning: a pointer to thread-local data is stored in
}
inline void fg_MatrixBorrowedElementAddress() {
  g_ppMatrix = &t_ppMatrixBorroweds[0];
}
inline void fg_MatrixBorrowedMemberLoad() {
  g_pMatrix = t_pMatrixBorrowed->m_pValue;
}
inline void fg_MatrixBorrowedMemberCopy() {
  int *pCopy = t_pMatrixBorrowed->m_pValue;
  g_pMatrix = pCopy;
}
inline void fg_MatrixBorrowedMemberStore() {
  t_pMatrixBorrowed->m_pValue = &t_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:33: warning: a pointer to thread-local data is stored in
}
inline void fg_MatrixBorrowedMemberAddress() {
  g_ppMatrix = &t_pMatrixBorrowed->m_pValue;
}
inline void fg_MatrixBorrowedReferenceMemberLoad() {
  g_pMatrix = t_MatrixBorrowedRef.m_rpValue;
}
inline void fg_MatrixBorrowedReferenceMemberCopy() {
  int *pCopy = t_MatrixBorrowedRef.m_rpValue;
  g_pMatrix = pCopy;
}
inline void fg_MatrixBorrowedReferenceMemberStore() {
  t_MatrixBorrowedRef.m_rpValue = &t_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:35: warning: a pointer to thread-local data is stored in
}
inline void fg_MatrixBorrowedReferenceMemberAddress() {
  g_ppMatrix = &t_MatrixBorrowedRef.m_rpValue;
}
inline void fg_MatrixBorrowedDereferenceLoad() {
  int **ppSlot = &*t_ppMatrixBorrowed;
  g_pMatrix = *ppSlot;
}
inline void fg_MatrixBorrowedDereferenceCopy() {
  int **ppSlot = &*t_ppMatrixBorrowed;
  int *pCopy = *ppSlot;
  g_pMatrix = pCopy;
}
inline void fg_MatrixBorrowedDereferenceStore() {
  int **ppSlot = &*t_ppMatrixBorrowed;
  *ppSlot = &t_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: a pointer to thread-local data is stored in
}
inline void fg_MatrixBorrowedDereferenceAddress() {
  int **ppSlot = &*t_ppMatrixBorrowed;
  g_ppMatrix = &*ppSlot;
}
inline void fg_MatrixBorrowedBitCastLoad() {
  g_pMatrix = __builtin_bit_cast(int *, *t_ppMatrixBorrowed);
}
inline void fg_MatrixBorrowedBitCastCopy() {
  int *pCopy = __builtin_bit_cast(int *, *t_ppMatrixBorrowed);
  g_pMatrix = pCopy;
}
inline void fg_MatrixLocalVariableLoad() {
  int *pLocal = &t_Value;
  g_pMatrix = pLocal;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: a pointer to thread-local data is stored in the variable 'g_pMatrix'
}
inline void fg_MatrixLocalVariableCopy() {
  int *pLocal = &t_Value;
  int *pCopy = pLocal;
  g_pMatrix = pCopy;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: a pointer to thread-local data is stored in the variable 'g_pMatrix'
}
inline void fg_MatrixLocalVariableStore() {
  int *pLocal = &t_Value;
  pLocal = &t_Value;
}
inline void fg_MatrixLocalVariableAddress() {
  int *pLocal = &t_Value;
  g_ppMatrix = &pLocal;
}
inline void fg_MatrixLocalElementLoad() {
  int *pLocal = &t_Value;
  int *pLocals[1] = {&t_Value};
  g_pMatrix = pLocals[0];
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: a pointer to thread-local data is stored in the variable 'g_pMatrix'
}
inline void fg_MatrixLocalElementCopy() {
  int *pLocal = &t_Value;
  int *pLocals[1] = {&t_Value};
  int *pCopy = pLocals[0];
  g_pMatrix = pCopy;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: a pointer to thread-local data is stored in the variable 'g_pMatrix'
}
inline void fg_MatrixLocalElementStore() {
  int *pLocal = &t_Value;
  int *pLocals[1] = {&t_Value};
  pLocals[0] = &t_Value;
}
inline void fg_MatrixLocalElementAddress() {
  int *pLocal = &t_Value;
  int *pLocals[1] = {&t_Value};
  g_ppMatrix = &pLocals[0];
}
inline void fg_MatrixLocalMemberLoad() {
  int *pLocal = &t_Value;
  CMatrixHolder Local{&t_Value};
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a pointer to thread-local data is stored in the member 'm_pValue'
  g_pMatrix = Local.m_pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: a pointer to thread-local data is stored in the variable 'g_pMatrix'
}
inline void fg_MatrixLocalMemberCopy() {
  int *pLocal = &t_Value;
  CMatrixHolder Local{&t_Value};
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a pointer to thread-local data is stored in the member 'm_pValue'
  int *pCopy = Local.m_pValue;
  g_pMatrix = pCopy;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: a pointer to thread-local data is stored in the variable 'g_pMatrix'
}
inline void fg_MatrixLocalMemberStore() {
  int *pLocal = &t_Value;
  CMatrixHolder Local{&t_Value};
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a pointer to thread-local data is stored in the member 'm_pValue'
  Local.m_pValue = &t_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: a pointer to thread-local data is stored in
}
inline void fg_MatrixLocalMemberAddress() {
  int *pLocal = &t_Value;
  CMatrixHolder Local{&t_Value};
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a pointer to thread-local data is stored in the member 'm_pValue'
  g_ppMatrix = &Local.m_pValue;
}
inline void fg_MatrixLocalReferenceMemberLoad() {
  int *pLocal = &t_Value;
  CMatrixRef Ref{pLocal};
  g_pMatrix = Ref.m_rpValue;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: a pointer to thread-local data is stored in the variable 'g_pMatrix'
}
inline void fg_MatrixLocalReferenceMemberCopy() {
  int *pLocal = &t_Value;
  CMatrixRef Ref{pLocal};
  int *pCopy = Ref.m_rpValue;
  g_pMatrix = pCopy;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: a pointer to thread-local data is stored in the variable 'g_pMatrix'
}
inline void fg_MatrixLocalReferenceMemberStore() {
  int *pLocal = &t_Value;
  CMatrixRef Ref{pLocal};
  Ref.m_rpValue = &t_Value;
}
inline void fg_MatrixLocalReferenceMemberAddress() {
  int *pLocal = &t_Value;
  CMatrixRef Ref{pLocal};
  g_ppMatrix = &Ref.m_rpValue;
}
inline void fg_MatrixLocalDereferenceLoad() {
  int *pLocal = &t_Value;
  int **ppSlot = &pLocal;
  g_pMatrix = *ppSlot;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: a pointer to thread-local data is stored in the variable 'g_pMatrix'
}
inline void fg_MatrixLocalDereferenceCopy() {
  int *pLocal = &t_Value;
  int **ppSlot = &pLocal;
  int *pCopy = *ppSlot;
  g_pMatrix = pCopy;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: a pointer to thread-local data is stored in the variable 'g_pMatrix'
}
inline void fg_MatrixLocalDereferenceStore() {
  int *pLocal = &t_Value;
  int **ppSlot = &pLocal;
  *ppSlot = &t_Value;
}
inline void fg_MatrixLocalDereferenceAddress() {
  int *pLocal = &t_Value;
  int **ppSlot = &pLocal;
  g_ppMatrix = &*ppSlot;
}
inline void fg_MatrixLocalBitCastLoad() {
  int *pLocal = &t_Value;
  g_pMatrix = __builtin_bit_cast(int *, pLocal);
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: a pointer to thread-local data is stored in the variable 'g_pMatrix'
}
inline void fg_MatrixLocalBitCastCopy() {
  int *pLocal = &t_Value;
  int *pCopy = __builtin_bit_cast(int *, pLocal);
  g_pMatrix = pCopy;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: a pointer to thread-local data is stored in the variable 'g_pMatrix'
}

// Round 102: each alternative of a destination, not one of them; what a
// reference member of an exempt object refers to; each store of a summary,
// also after one that is kept anywhere, each at the level it keeps, a load
// through a pointer parameter, a parameter assigned before it is stored or
// given back; the value a reference parameter loads, also as reported; not
// what an assignment stored before; an element of a thread-local array of
// pointers to a thread-local type; the pointer a slot a call gives holds, not
// the address of a slot that isn't thread-local; a reference to an element
// of a local array captured; a static call operator; an alias of a member of
// a local holder; not a buffer a thread-local object owns.
thread_local int *t_pMixed;
int *g_pMixed;
inline void fg_MixedDestination(bool _bChoose) {
  (_bChoose ? t_pMixed : g_pMixed) = &t_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:38: warning: a pointer to thread-local data is stored in
}
inline void fg_MixedAlias(bool _bChoose) {
  int *&rpSlot = _bChoose ? t_pMixed : g_pMixed;
  rpSlot = &t_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: a pointer to thread-local data is stored in
}
inline void fg_MixedSlotPointer(bool _bShared) {
  int **ppSlot = &t_pMixed;
  if (_bShared)
    ppSlot = &g_pMixed;
  *ppSlot = &t_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: a pointer to thread-local data is stored in
}
struct CMixedHandler : CCoroutineThreadLocalHandler {
  int *m_pValue;
  void f_Suspend() noexcept override;
};
struct CMixedOut {
  int *m_pValue;
};
inline void fg_MixedHandler(bool _bChoose, CMixedHandler &_Handler, CMixedOut &_Out) {
  (_bChoose ? _Handler.m_pValue : _Out.m_pValue) = &t_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:52: warning: a pointer to thread-local data is stored in
}
struct CRefHandler : CCoroutineThreadLocalHandler {
  CSettable &m_Owner;
  int *&m_rpOutput;
  void f_Suspend() noexcept override {
    m_Owner.m_pValue = &t_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a pointer to thread-local data is stored in
    m_Owner.f_Set(&t_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: a pointer to thread-local data is given to 'f_Set'
    m_rpOutput = &t_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: a pointer to thread-local data is stored in
  }
};
int **g_ppBothSlot;
inline void fg_SaveBoth(int *&_pValue) {
  g_pValue = _pValue;
  g_ppBothSlot = &_pValue;
}
thread_local int *t_pBoth = &g_Value;
inline void fg_SaveBothCaller() {
  fg_SaveBoth(t_pBoth);
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: a pointer to thread-local data is given to 'fg_SaveBoth'
}
struct CSlotHolder {
  int *const *m_ppSlot;
  void f_Save(int *const &_pValue) {
    m_ppSlot = &_pValue;
    g_pValue = _pValue;
  }
};
inline void fg_SaveToLocalHolder() {
  CSlotHolder Holder;
  Holder.f_Save(t_pBoth);
}
inline void fg_SaveThrough(int **_ppValue) { g_pValue = *_ppValue; }
inline void fg_SaveThroughCaller() { fg_SaveThrough(&t_pBoth); }
inline int *fg_Pick(int *_pFirst, int *_pSecond) {
  _pFirst = _pSecond;
  return _pFirst;
}
inline void fg_PickStore() {
  g_pValue = fg_Pick(&g_Value, &t_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
inline void fg_PickReversed() { g_pValue = fg_Pick(&t_Value, &g_Value); }
inline void fg_SaveReassigned(int *_pValue) {
  _pValue = &g_Value;
  g_pValue = _pValue;
}
inline void fg_SaveReassignedCaller() { fg_SaveReassigned(&t_Value); }
inline void fg_SaveLoadedValue(int *const &_pValue) { g_pValue = _pValue; }
inline void fg_SaveLoadedValueCaller() {
  int *pValue = &t_Value;
  fg_SaveLoadedValue(pValue);
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: a pointer to thread-local data is given to 'fg_SaveLoadedValue'
}
inline void fg_StoreAssignedValue() {
  int *pValue = &t_Value;
  g_pValue = (pValue = &g_Value);
}
thread_local CData *t_pDataSlots[1] = {&t_Data};
CData *g_pTypedData;
inline void fg_StoreTypedElement() {
  g_pTypedData = t_pDataSlots[0];
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: a pointer to thread-local data is stored in the variable 'g_pTypedData'
}
CData *&fg_DataSlotRef();
inline void fg_StoreSlotResultValue() {
  g_pTypedData = fg_DataSlotRef();
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: a pointer to thread-local data is stored in the variable 'g_pTypedData'
}
int *g_pPlainSlot;
inline int *&fg_PlainSlot() { return g_pPlainSlot; }
int **g_ppPlain;
inline void fg_StorePlainSlotAddress() { g_ppPlain = &fg_PlainSlot(); }
inline TCFuture<int> fg_CaptureElementReference() {
  int *pValues[1] = {&t_Value};
  int *&rpValue = pValues[0];
  auto fRead = [&rpValue] { return *rpValue; };
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: a pointer to thread-local data is stored in the capture of 'rpValue'
  co_await fg_Work();
  co_return fRead();
}
inline void fg_StaticCallOperator() {
  [](int *_pValue) static { g_pValue = _pValue; }(&t_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:51: warning: a pointer to thread-local data is given to 'operator()'
}
struct CAliasHolder {
  int *m_pValue;
};
inline int fg_HolderAlias() {
  CAliasHolder Holder{nullptr};
  int *&rpSlot = Holder.m_pValue;
  rpSlot = &t_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: a pointer to thread-local data is stored in
  return *Holder.m_pValue;
}
template <typename t_CValue> struct TCOwnedBuffer {
  ~TCOwnedBuffer();
  t_CValue *f_Data() { return m_pData; }
  t_CValue *m_pData;
};
thread_local TCOwnedBuffer<int> t_OwnedBuffer;
inline void fg_StoreOwnedBufferData() { g_pValue = t_OwnedBuffer.f_Data(); }


// Round 103: a default argument of a member initializer; not an operand a
// constant left skips; each use of a default argument, also after one that
// isn't evaluated; not a getter's load as bits, nor what may be a pointer it
// holds; the slot of a pointer assignment a getter gives the address of; a
// pointer an alignment builtin gives back; not a member initializer that
// can't run; the own storage of a handler through an alias, and assigned an
// object; a reference to a character; not a lambda with an explicit object
// called where it is made; an object a coroutine takes by value; a temporary
// a reference extends; not a typed parameter assigned before it is stored.
inline int fg_UseDefault(int _Value = ((g_pValue = &t_Value), 1));
struct CInitDefault {
  int m_Value;
  CInitDefault() : m_Value(fg_UseDefault()) {}
// CHECK-MESSAGES: :[[@LINE-1]]:28: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
};
void fg_ShortDefault(bool _bValue = false && (g_pValue = &t_Value));
void fg_ShortOrDefault(bool _bValue = true || (g_pValue = &t_Value));
inline void fg_ShortDefaultCaller() {
  fg_ShortDefault();
  fg_ShortOrDefault();
}
int fg_StoreDefault(int _Value = ((g_pValue = &t_Value), 1));
inline void fg_StoreDefaultTwice() {
  (void)sizeof(fg_StoreDefault());
  fg_StoreDefault();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
  fg_StoreDefault();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
struct CBitCastGetter {
  int *m_pValue;
  int *f_Get() { return __builtin_bit_cast(int *, m_pValue); }
};
thread_local CBitCastGetter t_BitCastGetter{&g_Value};
inline void fg_StoreBitCastGetter() { g_pValue = t_BitCastGetter.f_Get(); }
struct CEitherGetter {
  int *m_pValue;
  int *f_Get(bool _bChoose) { return _bChoose ? m_pValue : &g_Value; }
};
thread_local CEitherGetter t_EitherGetter{&g_Value};
inline void fg_StoreEitherGetter(bool _bChoose) {
  g_pValue = t_EitherGetter.f_Get(_bChoose);
}
struct CResetGetter {
  int *m_pValue;
  int **f_Reset() { return &(m_pValue = &g_Value); }
};
thread_local CResetGetter t_ResetGetter;
int **g_ppReset;
inline void fg_StoreResetSlot() {
  g_ppReset = t_ResetGetter.f_Reset();
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: a pointer to thread-local data is stored in the variable 'g_ppReset'
}
inline void fg_StoreAligned() {
  g_pValue = static_cast<int *>(__builtin_assume_aligned(&t_Value, 4));
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
[[noreturn]] void fg_Stop();
struct CUnreachableMember {
  int m_Dummy;
  int *m_pValue;
  CUnreachableMember() : m_Dummy((fg_Stop(), 0)), m_pValue(&t_Value) {}
};
struct CPartState {
  int *m_pValue;
};
struct CAliasHandler : CCoroutineThreadLocalHandler {
  CPartState m_Part;
  void f_Suspend() noexcept override {
    auto &Part = m_Part;
    Part.m_pValue = &t_Value;
    m_Part = CPartState{&t_Value};
  }
};
struct CCharRef {
  CCharRef(char &_Value);
  char &m_Value;
};
thread_local char t_Char;
CCharRef g_CharRef(t_Char);
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: a reference to thread-local data is stored in the 'CCharRef' made of it
inline int fg_ExplicitLambda() {
  int *pValue = &t_Value;
  auto fRead = [pValue](this auto const &_Self) { return *pValue; };
  return fRead() + [pValue](this auto const &) { return *pValue; }();
}
struct CSavingView {
  CSavingView(int *_pValue) : m_pValue(_pValue) {}
  int *m_pValue;
};
inline TCFuture<int> fg_ConsumeView(CSavingView _View) {
  co_await fg_Work();
  co_return *_View.m_pValue;
}
inline void fg_PassViewToCoroutine() {
  fg_ConsumeView(CSavingView(&t_Value));
// CHECK-MESSAGES: :[[@LINE-1]]:30: warning: a pointer to thread-local data is stored in the 'CSavingView' made of it
}
inline TCFuture<int> fg_ExtendedHolder() {
  auto &&View = CSettable{};
  View.f_Set(&t_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is given to 'f_Set'
  co_await fg_Work();
  co_return *View.m_pValue;
}
CData *g_pResetData;
inline void fg_ResetTyped(CData *_pData) {
  _pData = nullptr;
  g_pResetData = _pData;
}


// Round 104: a member of a temporary aggregate, also a reference member; a
// temporary forwarded into a global; a forward function whose body isn't
// seen; a mutable closure's own copy of a capture, by what it assigns it;
// not the elements of an array a closure that stays local captures; not a
// copy of the object through an alias of its member; an incoming parameter
// in a handler, thrown before it is assigned; an array a constructor that
// can't be seen takes by reference; a structured binding of a holder; a
// static data member by its type; a pointer-array member by its type; what a
// static call operator gives of its parameter; not a default member
// initializer every constructor overrides, nor one of a thread-local object;
// not the address of a copy, nor a copy's own member function.
struct CTemporaryPair {
  int *m_pValue;
  int &m_Value;
};
inline void fg_StoreTemporaryMember() {
  g_pValue = CTemporaryPair{&t_Value, g_Value}.m_pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
  g_pValue = &CTemporaryPair{&g_Value, t_Value}.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
struct CPlainView {
  int *m_pValue;
};
CPlainView g_PlainView;
namespace NTemporary {
template <typename t_CType> t_CType &&fg_Forward(t_CType &&_Value) {
  return static_cast<t_CType &&>(_Value);
}
}
inline void fg_StoreForwardedTemporary() {
  g_PlainView = NTemporary::fg_Forward(CPlainView{&t_Value});
// CHECK-MESSAGES: :[[@LINE-1]]:51: warning: a pointer to thread-local data is stored in the member 'm_pValue'
}
namespace NUnseen {
template <typename t_CType> t_CType &&fg_Forward(t_CType &_Value);
}
inline void fg_StoreUnseenForward() {
  int *pValue = &t_Value;
  g_pValue = NUnseen::fg_Forward(pValue);
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
inline void fg_StoreClosureCopy() {
  int *pValue = &g_Value;
  auto fGet = [pValue]() mutable {
    pValue = &t_Value;
    return pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: a pointer to thread-local data is returned as a type that isn't a pointer to a thread-local type
  };
  g_pValue = fGet();
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
inline void fg_StoreClosureReset() {
  int *pValue = &t_Value;
  auto fGet = [pValue]() mutable {
    pValue = &g_Value;
    return pValue;
  };
  g_pValue = fGet();
}
inline int fg_ArrayCaptureCalled() {
  int *pValues[] = {&t_Value};
  auto fRead = [pValues] { return *pValues[0]; };
  return [pValues] { return *pValues[0]; }() + fRead();
}
struct CCopiedAlias {
  int m_Value;
  void f_StoreAlias();
};
inline void CCopiedAlias::f_StoreAlias() {
  int &Value = m_Value;
  [*this, &Value] { g_pValue = &Value; }();
// CHECK-MESSAGES: :[[@LINE-1]]:32: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
inline void fg_ThrownBeforeAssigned(CData *_pData) {
  CData Local;
  try {
    fg_Throws();
    _pData = &Local;
  } catch (...) {
    g_pValue = &_pData->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
  }
}
struct CArrayView {
  CArrayView(int const (&_Values)[4]);
  int const *m_pValues;
};
CArrayView g_ArrayView(t_Values);
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a reference to thread-local data is stored in the 'CArrayView' made of it
inline int *fg_BindingEscape() {
  CSavingView View(&t_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: a pointer to thread-local data is stored in the 'CSavingView' made of it
  auto &[pValue] = View;
  return pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: a pointer to thread-local data is returned as a type that isn't a pointer to a thread-local type
}
struct CStaticDataProvider {
  static CData *ms_pData;
};
inline void fg_StoreStaticMember(CStaticDataProvider &_Provider) {
  g_pValue = &_Provider.ms_pData[0].m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
struct CDataSlotArray {
  CData *m_pData[1];
};
thread_local CDataSlotArray t_DataSlotArray{{&t_Data}};
inline void fg_StorePointerArrayMember() {
  g_pValue = &t_DataSlotArray.m_pData[0]->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
struct CStaticCallData {
  int m_Value;
  int &f_Get() {
    return [](int &_Value) static -> int & { return _Value; }(m_Value);
  }
};
thread_local CStaticCallData t_StaticCallData;
inline void fg_StoreStaticCall() {
  g_pValue = &t_StaticCallData.f_Get();
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
struct CDefaultInThread {
  int *m_pValue = &t_Value;
};
thread_local CDefaultInThread t_DefaultInThread;
struct CDefaultOverridden {
  int *m_pValue = &t_Value;
  CDefaultOverridden() : m_pValue(nullptr) {}
};
namespace NAddress {
template <typename t_CType> t_CType *addressof(t_CType &_Value) noexcept;
}
CData *g_pCopiedData;
inline void fg_StoreAddressOfCopy() {
  static CData s_Copy = fg_GetData();
  g_pCopiedData = NAddress::addressof(s_Copy);
  g_pCopiedData = &s_Copy.f_Self();
}


// Round 105: what a holder of a whole array keeps; a mutable closure's own
// capture; a thread-local reference by what it refers to; a call through a
// member pointer that can dispatch; not a local closure called as a template
// or extended by a reference; not a pointer whose address a helper takes; not
// a pointer overwritten after a weak write, nor one written weakly after it
// is read; a pointer parameter by what is assigned to it; a getter by what
// reaches its return; a member of an element of a local array through an
// alias; not a base member of an exempt object; not a holder moved and used
// as a value; each holder a conditional can choose; a typed array reference
// captured; a member pointer to a typed member; what a reference to a
// pointer reads where it is read; not a copy a closure makes of a reference.
struct CWholeArrayRef {
  CWholeArrayRef(int (&_Values)[4]);
  int (&m_Values)[4];
};
struct CWholeArrayPointer {
  CWholeArrayPointer(int (*_pValues)[4]);
  int (*m_pValues)[4];
};
CWholeArrayRef g_WholeArrayRef(t_Values);
// CHECK-MESSAGES: :[[@LINE-1]]:32: warning: a reference to thread-local data is stored in the 'CWholeArrayRef' made of it
CWholeArrayPointer g_WholeArrayPointer(&t_Values);
// CHECK-MESSAGES: :[[@LINE-1]]:40: warning: a pointer to thread-local data is stored in the 'CWholeArrayPointer' made of it
inline auto fg_MutableCapture() {
  return [pCounter = (int *)nullptr]() mutable {
    pCounter = &t_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: a pointer to thread-local data is stored in the capture 'pCounter'
    return *pCounter;
  };
}
int *g_pReferred;
thread_local int *&t_rpReferred = g_pReferred;
inline void fg_StoreThroughThreadReference() {
  t_rpReferred = &t_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: a pointer to thread-local data is stored in the variable 't_rpReferred'
}
struct CVirtualPointer {
  virtual int *f_Get() { return &g_Value; }
};
struct CVirtualPointerOverride : CVirtualPointer {
  int m_Value;
  int *f_Get() override { return &m_Value; }
};
thread_local CVirtualPointerOverride t_VirtualPointerOverride;
inline void fg_StoreMemberPointerDispatch() {
  CVirtualPointer &Base = t_VirtualPointerOverride;
  auto pGet = &CVirtualPointer::f_Get;
  g_pValue = (Base.*pGet)();
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
inline int fg_TemplateClosureCalls() {
  auto fRead = [pValue = &t_Value]<typename t_CType>() { return t_CType(*pValue); };
  return fRead.operator()<int>() +
         [pValue = &t_Value]<typename t_CType>() { return t_CType(*pValue); }.operator()<int>();
}
inline int fg_ExtendedClosures() {
  int &rValue = t_Value;
  auto const &fRead = [&rValue] { return rValue; };
  auto &&fOther = [&rValue] { return rValue; };
  return fRead() + fOther();
}
inline void fg_AddressHelperOverwrite() {
  int *pValue = &t_Value;
  auto ppValue = __builtin_addressof(pValue);
  *ppValue = nullptr;
}
inline void fg_WeakThenOverwritten(bool _bChoose) {
  int *pFirst = &g_Value, *pSecond = &g_Value;
  *(_bChoose ? &pFirst : &pSecond) = &t_Value;
  pFirst = &g_Value;
  g_pValue = pFirst;
}
inline void fg_WeakAfterRead(bool _bChoose) {
  int *pFirst = &g_Value, *pSecond = &g_Value;
  g_pValue = pFirst;
  *(_bChoose ? &pFirst : &pSecond) = &t_Value;
}
CPlainView g_PlainViewAssigned;
thread_local CPlainView t_PlainView;
inline void fg_StoreReassignedDestination(CPlainView *_pView, int *_pValue) {
  _pView = &g_PlainViewAssigned;
  _pView->m_pValue = _pValue;
}
inline void fg_CallReassignedDestination() {
  fg_StoreReassignedDestination(&t_PlainView, &t_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:47: warning: a pointer to thread-local data is given to 'fg_StoreReassignedDestination'
}
inline int *fg_CopiedBeforeReassigned() {
  int *pSource = &g_Value;
  int *pCopy = pSource;
  pSource = &t_Value;
  (void)pSource;
  return pCopy;
}
inline void fg_StoreGetterReachingValue() { g_pValue = fg_CopiedBeforeReassigned(); }
struct CArrayHolder {
  int *m_pValue;
};
inline void fg_ElementMemberAlias() {
  CArrayHolder Holders[1]{};
  int *&rpSlot = Holders[0].m_pValue;
  rpSlot = &t_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: a pointer to thread-local data is stored in what the expression refers to
}
struct CPointerBase {
  int *m_pBaseValue;
};
struct CHandlerWithBase : CPointerBase, CCoroutineThreadLocalHandler {
  void f_Suspend() noexcept override;
};
inline void fg_SetInheritedExempt(CHandlerWithBase &_Handler) {
  _Handler.m_pBaseValue = &t_Value;
}
struct CConsumingView {
  CConsumingView(int *_pValue) : m_pValue(_pValue) {}
  int f_Consume();
  int *m_pValue;
};
inline int fg_MovedHolderValue() {
  CConsumingView View(&t_Value);
  return fg_Move(View).f_Consume();
}
inline CSettable fg_ChosenHolder(bool _bChoose) {
  CSettable First{}, Second{};
  (_bChoose ? First : Second).f_Set(&t_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:37: warning: a pointer to thread-local data is given to 'f_Set'
  return First;
}
inline auto fg_ArrayReferenceCapture(CData (&_Data)[2]) {
  return [&_Data] { return _Data[0].m_Value; };
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: a reference to thread-local data is stored in the capture of '_Data'
}
struct CDataLink {
  CData *m_pData;
};
thread_local CDataLink t_DataLink{&t_Data};
inline void fg_StoreMemberPointerSelection() {
  CDataLink Link = t_DataLink;
  g_pValue = &(Link.*&CDataLink::m_pData)->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
inline void fg_StoreReferenceReadWhereRead() {
  int *pValue = &g_Value;
  int *&rpValue = pValue;
  pValue = &t_Value;
  g_pValue = rpValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
int const *g_pcValue;
inline auto fg_StoreCopiedReferent() {
  int &rValue = t_Value;
  return [rValue] { g_pcValue = &rValue; };
}


// Round 106: not a getter's return that can't run; what a local closure a
// call runs assigns, also through a reference, an alias, an element or a
// pointer, not what can't run in it, nor at calls before it is made or that
// can't run it; not a local whose address is taken where that can't run;
// not an assignment that isn't evaluated; the arm a constant condition takes;
// a destination that can be what isn't a local; an indirect write and an
// indirect read where each is; what can't be told of a receiver and a
// borrowed temporary as alternatives of what may hold the data.
int **g_ppR106Value;
int *g_pR106Other;
void fg_R106Use(int *_pValue);
void fg_R106SideEffect();
template <typename t_CFunction> void fg_R106Run(t_CFunction &&_fFunction);
inline int *fg_R106GetFalse() {
  if (false)
    return &t_Value;
  return &g_Value;
}
inline int *fg_R106GetUnreachable() {
  return &g_Value;
  return &t_Value;
}
inline int *fg_R106GetConstexpr() {
  if constexpr (false)
    return &t_Value;
  return &g_Value;
}
inline void fg_R106StoreGetters() {
  g_pValue = fg_R106GetFalse();
  g_pValue = fg_R106GetUnreachable();
  g_pValue = fg_R106GetConstexpr();
}
inline void fg_R106ClosureUnreachable() {
  int *pValue = &g_Value;
  auto fSet = [&] {
    if (false)
      pValue = &t_Value;
  };
  fSet();
  g_pValue = pValue;
}
inline void fg_R106ClosureAfterReturn() {
  int *pValue = &g_Value;
  auto fSet = [&] {
    return;
    pValue = &t_Value;
  };
  fSet();
  g_pValue = pValue;
}
inline void fg_R106ClosureUnevaluated() {
  int *pValue = &g_Value;
  auto fSet = [&pValue] { (void)sizeof(pValue = &t_Value); };
  fSet();
  g_pValue = pValue;
}
inline void fg_R106ClosureInitReference() {
  int *pValue = &g_Value;
  auto fSet = [&rpSlot = pValue] { rpSlot = &t_Value; };
  fSet();
  g_pValue = pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
inline void fg_R106ClosureCapturedAlias() {
  int *pValue = &g_Value;
  int *&rpAlias = pValue;
  [&rpAlias] { rpAlias = &t_Value; }();
  g_pValue = pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
inline void fg_R106ClosureLocalAlias() {
  int *pValue = nullptr;
  auto fSet = [&] {
    int *&rpSlot = pValue;
    rpSlot = &t_Value;
  };
  fSet();
  g_pValue = pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
inline void fg_R106ClosureElement() {
  int *pValues[1] = {nullptr};
  auto fSet = [&] { pValues[0] = &t_Value; };
  fSet();
  g_pValue = pValues[0];
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
inline void fg_R106ClosureThroughPointer() {
  int *pValue = &g_Value;
  int **ppValue = &pValue;
  [ppValue] { *ppValue = &t_Value; }();
  g_pValue = pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
inline void fg_R106ClosureBeforeMade() {
  int *pValue = nullptr;
  fg_R106Use(pValue);
  g_pValue = pValue;
  auto fSet = [&] { pValue = &t_Value; };
  fSet();
}
inline void fg_R106ClosureUnrelatedCall() {
  int *pValue = nullptr;
  auto fChange = [&] { pValue = &t_Value; };
  fChange();
  pValue = &g_Value;
  fg_R106SideEffect();
  g_pValue = pValue;
}
inline void fg_R106ClosureReadsLater() {
  int *pValue = &g_Value;
  auto fStore = [&] { g_pValue = pValue; };
// CHECK-MESSAGES: :[[@LINE-1]]:34: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
  pValue = &t_Value;
  fStore();
}
inline void fg_R106ClosureGivenAway() {
  int *pValue = &g_Value;
  fg_R106Run([&] { pValue = &t_Value; });
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: a pointer to thread-local data is stored in the capture of 'pValue'
  g_pValue = pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
inline void fg_R106EscapeUnevaluated() {
  int *pValue = &t_Value;
  (void)sizeof(g_ppR106Value = &pValue);
  pValue = &g_Value;
  g_pValue = pValue;
}
inline void fg_R106EscapeUnreachable() {
  int *pValue = &t_Value;
  pValue = &g_Value;
  if (false)
    g_ppR106Value = &pValue;
  g_pValue = pValue;
}
inline void fg_R106ConstantP() {
  int *pValue = &t_Value;
  (void)__builtin_constant_p(pValue = &g_Value);
  g_pValue = pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
inline void fg_R106ConstantArm() {
  int *pValue = &t_Value, *pOther = nullptr;
  *(true ? &pValue : &pOther) = &g_Value;
  g_pValue = pValue;
}
inline void fg_R106WeakGiven(bool _bChoose, int **_ppOther) {
  int *pValue = &t_Value;
  *(_bChoose ? &pValue : _ppOther) = &g_Value;
  g_pValue = pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
inline void fg_R106WeakGlobal(bool _bChoose) {
  int *pValue = &t_Value;
  *(_bChoose ? &pValue : &g_pR106Other) = &g_Value;
  g_pValue = pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
inline void fg_R106WeakDestination(bool _bChoose, int **_ppDestination) {
  int *pValue = &t_Value;
  if (_bChoose)
    _ppDestination = &pValue;
  *_ppDestination = &g_Value;
  g_pValue = pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
struct CR106View {
  int *m_pValue;
  void f_Set(int *_pValue) { m_pValue = _pValue; }
};
CR106View &fg_R106External();
inline void fg_R106ChooseExternal(bool _bChoose) {
  CR106View Local;
  (_bChoose ? Local : fg_R106External()).f_Set(&t_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:48: warning: a pointer to thread-local data is given to 'f_Set'
}
inline void fg_R106ChooseHeap(bool _bChoose) {
  CR106View Local;
  (_bChoose ? &Local : new CR106View)->f_Set(&t_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:46: warning: a pointer to thread-local data is given to 'f_Set'
}
struct CR106Holder {
  int *m_pValue;
};
thread_local CR106Holder t_R106Holder;
CR106Holder g_R106Holder;
struct CR106Ref {
  CR106Holder *m_pHolder;
  CR106Holder &operator*() const { return *m_pHolder; }
};
inline void fg_R106BorrowedTemporary(bool _bChoose) {
  (_bChoose ? t_R106Holder : *CR106Ref{&g_R106Holder}).m_pValue = &t_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:67: warning: a pointer to thread-local data is stored in the member 'm_pValue'
}
inline void fg_R106IndirectStore() {
  int *pValue = nullptr;
  int **ppValue = &pValue;
  *ppValue = &t_Value;
  g_pValue = pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
inline void fg_R106IndirectLoad() {
  int *pValue = nullptr;
  auto ppValue = &pValue;
  pValue = &t_Value;
  g_pValue = *ppValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
inline void fg_R106IndirectRefreshed() {
  int *pValue = &t_Value;
  auto ppValue = &pValue;
  pValue = &g_Value;
  g_pValue = *ppValue;
}


// Round 107: a stepped pointer by what it held before the step; each read of
// what a pointer designates where it is read, also through a copy of the
// pointer; what a scope exit's closure assigns where its guard ends; what a
// closure called in a local closure assigns.
inline void fg_R107StepPrefix() {
  int *pValue = t_Values;
  ++pValue;
  g_pValue = pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
inline void fg_R107StepPostfix() {
  int *pValue = t_Values;
  pValue++;
  g_pValue = pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
inline void fg_R107StepCompound() {
  int *pValue = t_Values;
  pValue += 1;
  g_pValue = pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
inline int *fg_R107StepReturn() {
  int *pValue = t_Values;
  ++pValue;
  return pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: a pointer to thread-local data is returned as a type that isn't a pointer to a thread-local type
}
inline void fg_R107StoreStepped() {
  g_pValue = fg_R107StepReturn();
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
inline void fg_R107ReadTwice(bool _bChoose) {
  int *pValue = &g_Value;
  int **ppValue = &pValue;
  int *pOld = *ppValue;
  pValue = &t_Value;
  g_pValue = _bChoose ? pOld : *ppValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
inline void fg_R107CopiedAlias() {
  int *pValue = &g_Value;
  int **ppValue = &pValue;
  int **ppCopy = ppValue;
  pValue = &t_Value;
  g_pValue = *ppCopy;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
inline void fg_R107CopiedAliasRefreshed() {
  int *pValue = &t_Value;
  int **ppValue = &pValue;
  int **ppCopy = ppValue;
  pValue = &g_Value;
  g_pValue = *ppCopy;
}
inline void fg_R107GuardWrites() {
  int *pValue = &g_Value;
  {
    auto Cleanup = g_OnScopeExit / [&] { pValue = &t_Value; };
    pValue = &g_Value;
  }
  g_pValue = pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
inline void fg_R107NestedClosure() {
  int *pValue = &g_Value;
  [&] { [&] { pValue = &t_Value; }(); }();
  g_pValue = pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}

// Round 107, second part: a name of a structured binding by what it binds
// where it is read, and an assignment to it by what it binds; what a helper
// or a getter gives a reference or a pointer to; a pointer a coroutine keeps
// in its frame; what a parameter can still designate; a handler of a
// constructor's try for its initializers; either order of the arguments of a
// call; not a base of a listed type made as an aggregate; not what a pointer
// that a listed type's accessor gives points to.
inline void fg_R107BindingCopy() {
  int *pValues[1] = {&t_Value};
  auto [pValue] = pValues;
  g_pValue = pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
inline int *fg_R107BindingReturn() {
  int *pItems[2] = {&t_Value, nullptr};
  auto [pFirst, pSecond] = pItems;
  return pFirst;
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: a pointer to thread-local data is returned as a type that isn't a pointer to a thread-local type
}
inline void fg_R107BindingWrite() {
  int *pValues[1] = {nullptr};
  auto &[pValue] = pValues;
  pValue = &t_Value;
  g_pValue = pValues[0];
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
inline void fg_R107BindingLater() {
  int *pValues[1] = {&g_Value};
  auto &[pValue] = pValues;
  pValues[0] = &t_Value;
  g_pValue = pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
struct CR107Pair {
  int *m_pFirst;
  int *m_pSecond;
};
inline void fg_R107BindingRefreshed() {
  CR107Pair Pair{&t_Value, &g_Value};
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: a pointer to thread-local data is stored in the member 'm_pFirst'
  auto &[pFirst, pSecond] = Pair;
  pFirst = &g_Value;
  g_pValue = Pair.m_pFirst;
}
inline void fg_R107BindingMember() {
  CR107Pair Pair{&g_Value, &g_Value};
  auto &[pFirst, pSecond] = Pair;
  pFirst = &t_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: a pointer to thread-local data is stored in the member 'm_pFirst'
  g_pValue = Pair.m_pFirst;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
inline void fg_R107KeepPointer(int *const &_rpValue) { g_pValue = _rpValue; }
inline void fg_R107BindingPointerReference() {
  CR107Pair Pair{&g_Value, &g_Value};
  auto &[pFirst, pSecond] = Pair;
  Pair.m_pFirst = &t_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: a pointer to thread-local data is stored in the member 'm_pFirst'
  fg_R107KeepPointer(pFirst);
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: a pointer to thread-local data is given to 'fg_R107KeepPointer', which stores it
}
inline void fg_R107BindingPointerReferenceRefreshed() {
  CR107Pair Pair{&t_Value, &g_Value};
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: a pointer to thread-local data is stored in the member 'm_pFirst'
  auto &[pFirst, pSecond] = Pair;
  Pair.m_pFirst = &g_Value;
  fg_R107KeepPointer(pFirst);
}
inline int *&fg_R107Slot(int *&_pValue) { return _pValue; }
inline int **fg_R107SlotAddress(int **_ppValue) { return _ppValue; }
struct CR107SlotReference {
  int *&m_pValue;
  int *&f_Get() { return m_pValue; }
};
inline void fg_R107HelperSlot() {
  int *pValue = &g_Value;
  fg_R107Slot(pValue) = &t_Value;
  g_pValue = pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
inline void fg_R107HelperSlotNull() {
  int *pValue = nullptr;
  fg_R107Slot(pValue) = &t_Value;
  g_pValue = pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
inline void fg_R107HelperSlotAddress() {
  int *pValue = &g_Value;
  *fg_R107SlotAddress(&pValue) = &t_Value;
  g_pValue = pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
inline void fg_R107GetterSlot() {
  int *pValue = &g_Value;
  CR107SlotReference Reference{pValue};
  Reference.f_Get() = &t_Value;
  g_pValue = pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
inline TCFuture<int> fg_R107ReadLater(int *_pValue) {
  co_await fg_Work();
  co_return *_pValue;
}
inline TCFuture<int> fg_R107PassToCoroutine() {
  co_return co_await fg_R107ReadLater(&fg_ConcurrencyThreadLocal().m_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:39: warning: a pointer to thread-local data is given to 'fg_R107ReadLater', which stores it
}
inline TCFuture<int> fg_R107PassOtherToCoroutine() {
  co_return co_await fg_R107ReadLater(&g_Value);
}
thread_local int *t_pR107Slot;
inline void fg_R107MaybeThreadSlot(int **_ppSlot, bool _bChoose) {
  if (_bChoose)
    _ppSlot = &t_pR107Slot;
  *_ppSlot = &t_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in what the parameter '_ppSlot' refers to
}
inline void fg_R107ThreadSlot(int **_ppSlot) {
  _ppSlot = &t_pR107Slot;
  *_ppSlot = &t_Value;
}
struct CR107Fail {
  CR107Fail();
};
struct CR107TryView {
  CR107TryView() try : m_Fail() {
  } catch (...) {
    g_pValue = &t_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
    throw;
  }
  CR107Fail m_Fail;
};
void fg_R107Both(int *_pFirst, int *_pSecond);
inline void fg_R107EitherOrder() {
  int *pValue = &g_Value;
  fg_R107Both((g_pValue = pValue), (pValue = &t_Value));
// CHECK-MESSAGES: :[[@LINE-1]]:27: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
inline void fg_R107EitherOrderBefore() {
  int *pValue = &t_Value;
  fg_R107Both((pValue = &g_Value), (g_pValue = pValue));
// CHECK-MESSAGES: :[[@LINE-1]]:48: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
inline void fg_R107Sequenced() {
  int *pValue = &t_Value;
  (pValue = &g_Value, g_pValue = pValue);
}
struct CR107Base {
  int *m_pValue;
};
struct CR107DerivedData : CR107Base {};
struct CR107Derived : CR107Base {};
inline int fg_R107ExemptBase() {
  CR107DerivedData Data{{&t_Value}};
  return *Data.m_pValue;
}
inline CR107Derived *fg_R107HeapBase() {
  return new CR107Derived{{&t_Value}};
// CHECK-MESSAGES: :[[@LINE-1]]:28: warning: a pointer to thread-local data is stored in the member 'm_pValue'
}
inline void fg_R107StoreAccessorPointer() {
  g_pValue = g_ThreadLocal.f_Get()->f_Pointer();
}


// Round 108: either order of arguments a conditional splits; not what a
// closure in a closure that isn't called assigns; a generic lambda by the
// specialization called; what a helper gives through a local alias; what an
// explicit-object or a final getter gives; a reference a coroutine keeps,
// also of its object; not what a helper gives back of the data of a thread
// named; the own storage of an exempt object through an alias of its base;
// the elements of an array made for a reference; a write through an array of
// pointers to locals; a parameter after a pack.
void fg_R108Both(int *_pFirst, int *_pSecond);
inline void fg_R108Keep(int, int *_pValue) { g_pValue = _pValue; }
inline void fg_R108EitherArm(bool _bChoose) {
  int *pValue = &g_Value;
  fg_R108Both((g_pValue = pValue), _bChoose ? (pValue = &t_Value) : (pValue = &g_Value));
// CHECK-MESSAGES: :[[@LINE-1]]:27: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
inline void fg_R108EitherFirstArm(bool _bChoose) {
  int *pValue = &g_Value;
  fg_R108Both(_bChoose ? (g_pValue = pValue) : nullptr, pValue = &t_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:38: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
inline void fg_R108EitherKilled(bool _bChoose) {
  int *pValue = &t_Value;
  fg_R108Keep(_bChoose ? (pValue = &g_Value, 0) : (pValue = &g_Value, 1), pValue);
// CHECK-MESSAGES: :[[@LINE-1]]:75: warning: a pointer to thread-local data is given to 'fg_R108Keep', which stores it
}
inline void fg_R108NestedUnused() {
  int *pValue = &g_Value;
  [&] { auto fUnused = [&] { pValue = &t_Value; }; (void)fUnused; }();
  g_pValue = pValue;
}
inline void fg_R108NestedNotCalled() {
  int *pValue = &g_Value;
  [&] {
    auto fSet = [&] { pValue = &t_Value; };
    if (false)
      fSet();
  }();
  g_pValue = pValue;
}
inline void fg_R108NestedCalled() {
  int *pValue = &g_Value;
  [&] { auto fSet = [&] { pValue = &t_Value; }; fSet(); }();
  g_pValue = pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
struct CR108Holder {
  static thread_local int ms_Value;
};
inline void fg_R108Generic() {
  int *pValue = &g_Value;
  [&]<typename t_CHolder>() { pValue = &t_CHolder::ms_Value; }.template operator()<CR108Holder>();
  g_pValue = pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
inline void fg_R108GenericDiscarded() {
  int *pValue = &g_Value;
  auto fUpdate = [&]<bool t_bEnabled>() {
    if constexpr (t_bEnabled)
      pValue = &t_Value;
  };
  fUpdate.template operator()<false>();
  g_pValue = pValue;
}
inline int *&fg_R108SlotAlias(int *&_pValue) {
  int *&rpAlias = _pValue;
  return rpAlias;
}
inline int **fg_R108SlotPointerAlias(int **_ppValue) {
  int **ppAlias = _ppValue;
  return ppAlias;
}
inline void fg_R108HelperAlias() {
  int *pValue = &g_Value;
  fg_R108SlotAlias(pValue) = &t_Value;
  g_pValue = pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
inline void fg_R108HelperPointerAlias() {
  int *pValue = &g_Value;
  *fg_R108SlotPointerAlias(&pValue) = &t_Value;
  g_pValue = pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
struct CR108Getter {
  int *&f_Get(this CR108Getter &, int *&_pValue) { return _pValue; }
};
inline void fg_R108ExplicitGetter() {
  CR108Getter Getter;
  int *pValue = &g_Value;
  Getter.f_Get(pValue) = &t_Value;
  g_pValue = pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
struct CR108FinalReference final {
  CR108FinalReference(int *&_pValue) : m_pValue(_pValue) {}
  virtual int *&f_Get() { return m_pValue; }
  int *&m_pValue;
};
inline void fg_R108FinalGetter() {
  int *pValue = nullptr;
  CR108FinalReference Reference(pValue);
  Reference.f_Get() = &t_Value;
  g_pValue = pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
struct CR108Overridden {
  virtual int *&f_Get(int *&_pValue) { return _pValue; }
};
inline void fg_R108QualifiedGetter(CR108Overridden &_Overridden) {
  int *pValue = &g_Value;
  _Overridden.CR108Overridden::f_Get(pValue) = &t_Value;
  g_pValue = pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
inline TCFuture<int> fg_R108ReadReference(int &_Value) {
  co_await fg_Work();
  co_return _Value;
}
inline TCFuture<int> fg_R108PassReference() {
  co_return co_await fg_R108ReadReference(t_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:43: warning: a reference to thread-local data is given to 'fg_R108ReadReference', which stores it
}
inline TCFuture<int> fg_R108PassOtherReference() {
  co_return co_await fg_R108ReadReference(g_Value);
}
struct CR108Reader {
  int m_Value;
  TCFuture<int> f_Read();
  int f_Get();
};
inline TCFuture<int> CR108Reader::f_Read() {
  co_await fg_Work();
  co_return m_Value;
}
thread_local CR108Reader t_R108Reader;
CR108Reader g_R108Reader;
inline TCFuture<int> fg_R108ReadReceiver() {
  co_return co_await t_R108Reader.f_Read();
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: a reference to thread-local data is given to 'f_Read', which stores it
}
inline TCFuture<int> fg_R108ReadOtherReceiver() {
  co_return co_await g_R108Reader.f_Read() + t_R108Reader.f_Get();
}
template <typename t_CType>
t_CType const &fg_R108Const(t_CType const &_Value) { return _Value; }
struct CR108ConstView {
  CData const *m_pData;
};
inline CR108ConstView fg_R108ForThread(int _ThreadID) {
  return {&fg_R108Const(*g_ThreadLocal.f_GetForThread(_ThreadID))};
}
struct CR108Scope : CCoroutineThreadLocalHandler, CHolder {
  void f_Suspend() noexcept override;
  void f_Resume() noexcept override;
};
inline void fg_R108ScopeBase() {
  CR108Scope Scope;
  CHolder &Base = Scope;
  Base.m_pValue = &t_Value;
  CHolder *pBase = &Scope;
  pBase->m_pValue = &t_Value;
  Scope.m_pValue = &t_Value;
}
inline void fg_R108KeepArray(int *const (&_pValues)[2]) { g_pValue = _pValues[0]; }
inline void fg_R108PassNamedArray() {
  int *pValues[2] = {&t_Value, &g_Value};
  fg_R108KeepArray(pValues);
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: a reference to thread-local data is given to 'fg_R108KeepArray', which stores it
}
inline void fg_R108PassArray() {
  fg_R108KeepArray({&t_Value, &g_Value});
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: a reference to thread-local data is given to 'fg_R108KeepArray', which stores it
}
inline void fg_R108ThroughSlots() {
  int *pValue = &g_Value;
  int **ppSlots[] = {&pValue};
  *ppSlots[0] = &t_Value;
  g_pValue = pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
CData *g_pR108Data;
template <typename... t_CArgs> void fg_R108StoreAfterPack(t_CArgs..., CData *_pData) {
  g_pR108Data = _pData;
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: a pointer to thread-local data is stored in the variable 'g_pR108Data'
}
inline auto fg_R108StoreAfterPackUser() { return &fg_R108StoreAfterPack<int>; }


// Round 109: what a helper gives on every return, not only of its
// parameters; not what a closure assigns where a copy of it ends; what a
// copy of a closure called assigns.
inline CData &fg_R109Select(bool _bCurrent, CData &_Other) {
  if (_bCurrent)
    return *g_ThreadLocal.f_Get();
  return _Other;
}
struct CR109View {
  CData *m_pData;
};
inline CR109View fg_R109SelectForThread(bool _bCurrent, int _ThreadID) {
  return {&fg_R109Select(_bCurrent, *g_ThreadLocal.f_GetForThread(_ThreadID))};
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: a pointer to thread-local data is stored in the member 'm_pData'
}
inline void fg_R109ClosureCopy() {
  int *pValue = &g_Value;
  {
    auto fSet = [&] { pValue = &t_Value; };
    auto fCopy = fSet;
    (void)fCopy;
  }
  g_pValue = pValue;
}
inline void fg_R109ClosureCopyCalled() {
  int *pValue = &g_Value;
  {
    auto fSet = [&] { pValue = &t_Value; };
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: a pointer to thread-local data is stored in the capture of 'pValue'
    auto fCopy = fSet;
    fCopy();
  }
  g_pValue = pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}

// Round 109, second part: a write through a bit cast of a pointer to a slot
// and through what only the compiler looks at; a local alias a closure gives
// back; a getter of a reference member; a captured slot a local getter gives;
// a dereferenced alias a helper gives back; what a getter of a handler gives;
// a generic lambda by the specialization called; a temporary a local
// reference extends; an init capture; an address a closure keeps to itself;
// an array member of a temporary; a member of a temporary aggregate through
// its getter; a member pointer of a pointer to a thread-local type; a member
// selected by a pointer to a data member; a byte of the object's own storage.
inline void fg_R109YBitCastLoad() {
  int *pValue = nullptr;
  int **ppValue = &pValue;
  *__builtin_bit_cast(int **, ppValue) = &t_Value;
  g_pValue = pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
inline void fg_R109YLaunder() {
  int *pValue = nullptr;
  int **ppValue = &pValue;
  *__builtin_launder(ppValue) = &t_Value;
  g_pValue = pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
inline void fg_R109YLambdaResult() {
  CData *pData = nullptr;
  auto &rpData = [&]() -> CData *& { return pData; }();
  rpData = &fg_GetData();
  (void)rpData;
}
struct CR109YSlot {
  int *&m_pValue;
  int *&f_Get() { return m_pValue; }
};
inline void fg_R109YReferenceMemberGetter() {
  int *pValue = nullptr;
  CR109YSlot Slot{pValue};
  Slot.f_Get() = &t_Value;
  g_pValue = pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
inline void fg_R109YCapturedGetter() {
  int *pValue = nullptr;
  auto fSlot = [&]() -> int *& { return pValue; };
  fSlot() = &t_Value;
  (void)pValue;
}
inline void fg_R109YCapturedGetterStored() {
  int *pValue = nullptr;
  auto fSlot = [&]() -> int *& { return pValue; };
  fSlot() = &t_Value;
  g_pValue = pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
inline int *&fg_R109YSlot(int *&_pValue) {
  auto ppValue = &_pValue;
  return *ppValue;
}
inline void fg_R109YDereferencedAlias() {
  int *pValue = &g_Value;
  fg_R109YSlot(pValue) = &t_Value;
  g_pValue = pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
struct CR109YHandler : CCoroutineThreadLocalHandler {
  CR109YHandler(int *_pValue) : m_pValue(_pValue) {}
  void f_Suspend() noexcept override;
  int *f_Get() const { return m_pValue; }
  int *m_pValue;
};
inline void fg_R109YHandlerGetter() {
  CR109YHandler Handler(&t_Value);
  g_pValue = Handler.f_Get();
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
inline void fg_R109YHandlerSelected() {
  CR109YHandler Handler(&t_Value);
  g_pValue = Handler.*&CR109YHandler::m_pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
  CR109YHandler *pHandler = &Handler;
  g_pValue = pHandler->*&CR109YHandler::m_pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
inline void fg_R109YGenericCalled() {
  int *pValue = &g_Value;
  auto fUpdate = [&]<bool t_bEnabled>() {
    if constexpr (t_bEnabled)
      pValue = &t_Value;
  };
  if (false)
    fUpdate.template operator()<true>();
  fUpdate.template operator()<false>();
  g_pValue = pValue;
}
inline void fg_R109YExtended() {
  auto &&pValue = &g_Value;
  pValue = &t_Value;
  g_pValue = pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
inline void fg_R109YInitCapture() {
  int *pValue = nullptr;
  {
    auto fSet = [pSaved = &t_Value, &pValue] { pValue = pSaved; };
    fSet();
  }
  g_pValue = pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
inline void fg_R109YClosureAddress() {
  int *pValue = &t_Value;
  [&] {
    auto ppValue = &pValue;
    return **ppValue;
  }();
  pValue = &g_Value;
  g_pValue = pValue;
}
struct CR109YBatch {
  int *m_pValues[2];
};
inline void fg_R109YArrayMember() {
  g_pValue = CR109YBatch{{&t_Value, nullptr}}.m_pValues[0];
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
struct CR109YView {
  int *m_pValue;
  int *f_Get() const { return m_pValue; }
};
inline void fg_R109YTemporaryGetter() {
  g_pValue = CR109YView{&t_Value}.f_Get();
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
struct CR109YDataSlot {
  CData **m_ppData;
};
CData *g_pR109YData;
inline void fg_R109YMemberPointerLevels(CR109YDataSlot &_Slot) {
  g_pR109YData = *_Slot.m_ppData;
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: a pointer to thread-local data is stored in the variable 'g_pR109YData'
}
struct CR109YBytes {
  int m_Value;
  unsigned char *f_Bytes() {
    return reinterpret_cast<unsigned char *>(this) + 1;
  }
};
unsigned char *g_pR109YByte;
inline void fg_R109YBytes(CR109YBytes &_Bytes) {
  g_pR109YByte = _Bytes.f_Bytes();
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: a pointer to thread-local data is stored in the variable 'g_pR109YByte'
}
// End of round 109, second part.


// Round 111: aliases of a closure chosen many times, each once; closures in a
// closure that stay local, also one that only calls another; a reference
// member of a local by what it is bound to; what a closure called there
// assigns each time it runs.
inline void fg_R111ClosureAliases(bool _bChoose) {
  int *pValue = &g_Value;
  auto fSet = [&] { pValue = &t_Value; };
  auto &fAlias0 = fSet;
  auto &fAlias1 = _bChoose ? fAlias0 : _bChoose ? fAlias0 : _bChoose ? fAlias0 : _bChoose ? fAlias0 : _bChoose ? fAlias0 : _bChoose ? fAlias0 : _bChoose ? fAlias0 : _bChoose ? fAlias0 : _bChoose ? fAlias0 : _bChoose ? fAlias0 : _bChoose ? fAlias0 : fAlias0;
  auto &fAlias2 = _bChoose ? fAlias1 : _bChoose ? fAlias1 : _bChoose ? fAlias1 : _bChoose ? fAlias1 : _bChoose ? fAlias1 : _bChoose ? fAlias1 : _bChoose ? fAlias1 : _bChoose ? fAlias1 : _bChoose ? fAlias1 : _bChoose ? fAlias1 : _bChoose ? fAlias1 : fAlias1;
  auto &fAlias3 = _bChoose ? fAlias2 : _bChoose ? fAlias2 : _bChoose ? fAlias2 : _bChoose ? fAlias2 : _bChoose ? fAlias2 : _bChoose ? fAlias2 : _bChoose ? fAlias2 : _bChoose ? fAlias2 : _bChoose ? fAlias2 : _bChoose ? fAlias2 : _bChoose ? fAlias2 : fAlias2;
  auto &fAlias4 = _bChoose ? fAlias3 : _bChoose ? fAlias3 : _bChoose ? fAlias3 : _bChoose ? fAlias3 : _bChoose ? fAlias3 : _bChoose ? fAlias3 : _bChoose ? fAlias3 : _bChoose ? fAlias3 : _bChoose ? fAlias3 : _bChoose ? fAlias3 : _bChoose ? fAlias3 : fAlias3;
  auto &fAlias5 = _bChoose ? fAlias4 : _bChoose ? fAlias4 : _bChoose ? fAlias4 : _bChoose ? fAlias4 : _bChoose ? fAlias4 : _bChoose ? fAlias4 : _bChoose ? fAlias4 : _bChoose ? fAlias4 : _bChoose ? fAlias4 : _bChoose ? fAlias4 : _bChoose ? fAlias4 : fAlias4;
  auto &fAlias6 = _bChoose ? fAlias5 : _bChoose ? fAlias5 : _bChoose ? fAlias5 : _bChoose ? fAlias5 : _bChoose ? fAlias5 : _bChoose ? fAlias5 : _bChoose ? fAlias5 : _bChoose ? fAlias5 : _bChoose ? fAlias5 : _bChoose ? fAlias5 : _bChoose ? fAlias5 : fAlias5;
  auto &fAlias7 = _bChoose ? fAlias6 : _bChoose ? fAlias6 : _bChoose ? fAlias6 : _bChoose ? fAlias6 : _bChoose ? fAlias6 : _bChoose ? fAlias6 : _bChoose ? fAlias6 : _bChoose ? fAlias6 : _bChoose ? fAlias6 : _bChoose ? fAlias6 : _bChoose ? fAlias6 : fAlias6;
  fAlias7();
  g_pValue = pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}
inline int fg_R111NestedLocal() {
  CBorrowingView View(&t_Value);
  auto fRead = [&] { return [&] { return *View.m_pValue; }(); };
  return fRead();
}
inline int fg_R111WrapperLocal() {
  auto &Data = *g_ThreadLocal.f_Get();
  auto fRead = [&Data] { return Data.m_Value; };
  return [&fRead] { return fRead(); }();
}
struct CR111Context {
  CData &m_Data;
};
inline TCFuture<int> fg_R111Snapshot() {
  CData Snapshot = *g_ThreadLocal.f_Get();
  CR111Context Context{Snapshot};
  CBorrowingView View(&Context.m_Data.m_Value);
  co_await fg_Work();
  co_return *View.m_pValue;
}
inline void fg_R111Refreshed() {
  int *pValue = &t_Value;
  [&] { pValue = &g_Value; }();
  g_pValue = pValue;
}
inline void fg_R111RefreshedMaybe(bool _bRefresh) {
  int *pValue = &t_Value;
  [&] {
    if (_bRefresh)
      pValue = &g_Value;
  }();
  g_pValue = pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'
}


// Round 112: what a closure called there assigns on each way through it; not
// what a variable held before it was given another value before a closure
// that refers to it is made; a guard a `try` makes, which unwinding ends
// before its handler.
inline void fg_R112BranchRefresh(bool _bRead) {
  int *pValue = &t_Value;
  [&] {
    if (_bRead)
      pValue = &g_Value;
    else
      pValue = nullptr;
  }();
  g_pValue = pValue;
}
inline TCFuture<int> fg_R112CleanupOverwritten() {
  int *pValue = &t_Value;
  ++*pValue;
  pValue = &g_Value;
  auto Cleanup = g_OnScopeExit / [&pValue] { --*pValue; };
  co_await fg_Work();
  co_return 0;
}
inline TCFuture<int> fg_R112GuardInTry(bool _bRun) {
  try {
    auto &Data = *g_ThreadLocal.f_Get();
    auto Cleanup = g_OnScopeExit / [&Data] { Data.m_Value = 0; };
    if (_bRun)
      fg_Throws();
  } catch (...) {
  }
  co_await fg_Work();
  co_return 0;
}


// Round 113: a closure given to what only calls it, also through what only
// gives it back; not what is loaded of what a holder refers to; what a local
// holder is assigned, as what it is made with.
template <typename t_CType, typename t_CArgument>
t_CType &&fg_Forward(t_CArgument &_Value) {
  return static_cast<t_CType &&>(_Value);
}
template <typename t_CValue, unsigned t_Size, typename t_FFunction>
void fg_R113ForEach(t_CValue (&_Items)[t_Size], t_FFunction &&_fFunction) {
  for (t_CValue &Item : _Items)
    fg_Forward<t_FFunction>(_fFunction)(Item);
}
inline void fg_R113SynchronousCallback() {
  auto &Data = *g_ThreadLocal.f_Get();
  int Items[2] = {};
  fg_R113ForEach(Items, [&Data](int &) { ++Data.m_Value; });
}
inline int fg_R113MovedCall() {
  auto &Data = *g_ThreadLocal.f_Get();
  auto fRead = [&Data] { return Data.m_Value; };
  return fg_Move(fRead)();
}
template <typename t_CType> struct TCR113Reference {
  TCR113Reference(t_CType &_Value) : m_pValue(&_Value) {}
  t_CType &f_Get() const { return *m_pValue; }
  t_CType *m_pValue;
};
inline int *fg_R113Extracted() {
  TCR113Reference<CData> Reference(*g_ThreadLocal.f_Get());
  return Reference.f_Get().m_pValue;
}
inline int fg_R113AssignedView() {
  CBorrowingView View(&g_Value);
  View = CBorrowingView(&t_Value);
  return *View.m_pValue;
}


// Round 114: a closure given to what only calls it, also recursively, also
// through a closure of its own that it calls, not where that can't run;
// what a closure leaves in a slot where it returns.
template <typename t_FFunction>
void fg_R114Recurse(int _Depth, t_FFunction &&_fFunction) {
  if (_Depth > 0)
    fg_R114Recurse(_Depth - 1, _fFunction);
  _fFunction(_Depth);
}
inline void fg_R114RecursiveCallback() {
  auto &Data = *g_ThreadLocal.f_Get();
  fg_R114Recurse(2, [&Data](int) { ++Data.m_Value; });
}
template <typename t_FFunction> void fg_R114Visit(t_FFunction &&_fVisit) {
  [&] { _fVisit(1); }();
}
inline void fg_R114VisitCallback() {
  auto &Data = *g_ThreadLocal.f_Get();
  fg_R114Visit([&Data](int) { ++Data.m_Value; });
}
template <typename t_FFunction> void fg_R114Keep(t_FFunction const &_fCallback);
constexpr bool gc_bR114Defer = false;
template <typename t_FFunction> void fg_R114Run(t_FFunction const &_fCallback) {
  if (gc_bR114Defer)
    fg_R114Keep(_fCallback);
  else
    _fCallback();
}
inline void fg_R114NotDeferred() {
  auto &Data = *g_ThreadLocal.f_Get();
  fg_R114Run([&Data] { ++Data.m_Value; });
}
inline void fg_R114ExitValue() {
  int Count = 0;
  int *pCounter = &Count;
  [&] {
    pCounter = &t_Value;
    Count = *pCounter;
    pCounter = &Count;
  }();
  g_pValue = pCounter;
}


// Round 115: a local function object of SynchronousFunctionTypes made of a
// closure, only called; an exchange as an assignment; not what a guard
// cancelled runs.
inline bool fg_R115FunctionObject() {
  auto &Data = *g_ThreadLocal.f_Get();
  NMib::NFunction::TCFunction<bool ()> fRead = [&Data] { return Data.m_Value != 0; };
  return fRead();
}
inline void fg_R115Exchanged() {
  int *pValue = &t_Value;
  fg_Exchange(pValue, nullptr);
  g_pValue = pValue;
}
inline void fg_R115CancelledGuard() {
  int *pValue = &g_Value;
  {
    auto Rollback = g_OnScopeExit / [&] { pValue = &t_Value; };
    Rollback.f_Clear();
  }
  g_pValue = pValue;
}


// Round 116: one classifier of the uses of a closure: a temporary function
// object of SynchronousFunctionTypes given to what only calls it, a presence
// check, a local reference to it, a helper that only calls it in another
// local closure; not one that any of them gives away.
inline void fg_R116Run(NMib::NFunction::TCFunction<void ()> const &_fRun) { _fRun(); }
void fg_R116Keep(NMib::NFunction::TCFunction<void ()> const &_fRun);
inline void fg_R116TemporaryWrapper() {
  auto &Data = *g_ThreadLocal.f_Get();
  fg_R116Run([&Data] { ++Data.m_Value; });
}
inline void fg_R116TemporaryWrapperKept() {
  auto &Data = *g_ThreadLocal.f_Get();
  fg_R116Keep([&Data] { ++Data.m_Value; });
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: a reference to thread-local data is stored in the capture of 'Data'
}
inline bool fg_R116PresenceCheck() {
  auto &Data = *g_ThreadLocal.f_Get();
  NMib::NFunction::TCFunction<bool ()> fRead = [&Data] { return Data.m_Value != 0; };
  if (fRead && !fRead.f_IsEmpty())
    return fRead();
  return false;
}
inline bool fg_R116ReferenceAlias() {
  auto &Data = *g_ThreadLocal.f_Get();
  NMib::NFunction::TCFunction<bool ()> fRead = [&] { return Data.m_Value != 0; };
  auto const &fBorrowed = fRead;
  return fBorrowed();
}
inline void fg_R116ReferenceAliasKept() {
  auto &Data = *g_ThreadLocal.f_Get();
  NMib::NFunction::TCFunction<void ()> fRun = [&] { ++Data.m_Value; };
// CHECK-MESSAGES: :[[@LINE-1]]:48: warning: a reference to thread-local data is stored in the capture of 'Data'
  auto const &fBorrowed = fRun;
  fg_R116Keep(fBorrowed);
}
template <typename t_CContainer, typename t_FFunctor>
void fg_R116ForEach(t_CContainer &_Container, t_FFunctor &&_fFunctor) {
  for (auto &Item : _Container)
    _fFunctor(Item);
}
inline void fg_R116HelperInWrapper() {
  int Items[2] = {};
  auto &Data = *g_ThreadLocal.f_Get();
  auto fVisit = [&Data](int) { ++Data.m_Value; };
  auto fBatch = [&] { fg_R116ForEach(Items, fVisit); };
  fBatch();
}


// Round 117: a closure assigned to a local function object of
// SynchronousFunctionTypes is in it, as when it is made with it; not one
// assigned to a member.
inline int fg_R117AssignedWrapper() {
  auto &Count = g_ThreadLocal.f_Get()->m_Value;
  NMib::NFunction::TCFunction<int ()> fRead;
  fRead = [&Count] { return Count; };
  return fRead();
}
struct CR117Holder {
  NMib::NFunction::TCFunction<int ()> m_fRead;
};
inline int fg_R117AssignedMember(CR117Holder &o_Holder) {
  auto &Count = g_ThreadLocal.f_Get()->m_Value;
  o_Holder.m_fRead = [&Count] { return Count; };
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a reference to thread-local data is stored in the capture of 'Count'
  return o_Holder.m_fRead();
}


// Round 118: a default member initializer in a member of a handler, also
// deeper, is the handler's own storage; not of a class that isn't one.
struct CR118State {
  CConcurrencyThreadLocal *m_pData = &fg_ConcurrencyThreadLocal();
};
struct CR118Nested {
  CR118State m_State;
};
struct CR118Scope : CCoroutineThreadLocalHandler {
  CR118Nested m_Nested;
  void f_Suspend() noexcept override;
};
inline TCFuture<int> fg_R118HandlerState() {
  CR118Scope Scope;
  co_return co_await fg_Work();
}
struct CR118Plain {
  CR118Nested m_Nested;
};
inline CR118Plain *fg_R118PlainState() {
  return new CR118Plain;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the member 'm_pData'
}
// Round 118, part A: a local function object of SynchronousFunctionTypes is
// a slot of closure values. A closure is in it from where it is given it to
// where it is given another or cleared, on the ways it reaches; only the uses
// it reaches are its own, and only a suspension while it is in it counts. A
// closure that captures the object it is in, only to call it, is called
// through it.
void fg_R118ASave(NMib::NFunction::TCFunction<int ()> const &_fRead);
void fg_R118AKeep(NMib::NFunction::TCFunction<void (int _Depth)> const &_fVisit);
inline TCFuture<int> fg_R118AInstalledAfterSuspension() {
  NMib::NFunction::TCFunction<int ()> fRead;
  co_await fg_Work();
  auto &Data = fg_ConcurrencyThreadLocal();
  fRead = [&Data] { return Data.m_Value; };
  co_return fRead();
}
inline TCFuture<int> fg_R118AInstalledPointerAfterSuspension() {
  NMib::NFunction::TCFunction<int ()> fRead;
  co_await fg_Work();
  fRead = [pData = &fg_ConcurrencyThreadLocal()] { return pData->m_Value; };
  co_return fRead();
}
inline TCFuture<int> fg_R118AInstalledBeforeSuspension() {
  NMib::NFunction::TCFunction<int ()> fRead;
  auto &Data = fg_ConcurrencyThreadLocal();
  fRead = [&Data] { return Data.m_Value; };
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: a reference to thread-local data is stored in the capture of 'Data'
  co_await fg_Work();
  co_return fRead();
}
inline TCFuture<int> fg_R118AReplacedBeforeSuspension() {
  auto &Data = fg_ConcurrencyThreadLocal();
  NMib::NFunction::TCFunction<int ()> fRead = [&Data] { return Data.m_Value; };
  fRead = [] { return 0; };
  co_await fg_Work();
  co_return fRead();
}
inline TCFuture<int> fg_R118AReplacedButSaved() {
  auto &Data = fg_ConcurrencyThreadLocal();
  NMib::NFunction::TCFunction<int ()> fRead = [&Data] { return Data.m_Value; };
// CHECK-MESSAGES: :[[@LINE-1]]:49: warning: a reference to thread-local data is stored in the capture of 'Data'
  NMib::NFunction::TCFunction<int ()> fSaved = fRead;
  fRead = [] { return 0; };
  co_await fg_Work();
  co_return fSaved();
}
inline int fg_R118ASuccessiveValues() {
  NMib::NFunction::TCFunction<int ()> fRead = [pData = &fg_ConcurrencyThreadLocal()] { return pData->m_Value; };
  auto Count = fRead();
  fRead = [] { return 0; };
  fg_R118ASave(fRead);
  return Count;
}
inline int fg_R118ASavedBeforeReplaced() {
  NMib::NFunction::TCFunction<int ()> fRead = [pData = &fg_ConcurrencyThreadLocal()] { return pData->m_Value; };
// CHECK-MESSAGES: :[[@LINE-1]]:56: warning: a pointer to thread-local data is stored in the capture of 'pData'
  fg_R118ASave(fRead);
  fRead = [] { return 0; };
  return fRead();
}
inline int fg_R118AReplacedOnOnePath(bool _bReplace) {
  NMib::NFunction::TCFunction<int ()> fRead = [pData = &fg_ConcurrencyThreadLocal()] { return pData->m_Value; };
// CHECK-MESSAGES: :[[@LINE-1]]:56: warning: a pointer to thread-local data is stored in the capture of 'pData'
  if (_bReplace)
    fRead = nullptr;
  fg_R118ASave(fRead);
  return 0;
}
inline int fg_R118ACleared() {
  auto &Data = fg_ConcurrencyThreadLocal();
  NMib::NFunction::TCFunction<int ()> fRead = [&Data] { return Data.m_Value; };
  int Value = fRead();
  fRead.f_Clear();
  return Value;
}
inline bool fg_R118AClearedSystem() {
  auto &ThreadLocal = fg_SystemThreadLocal();
  NMib::NFunction::TCFunction<bool ()> fRead = [&ThreadLocal] { return ThreadLocal.m_Value != 0; };
  bool bValue = fRead();
  fRead.f_Clear();
  return bValue;
}
inline TCFuture<int> fg_R118AClearedBeforeSuspension() {
  auto &Data = fg_ConcurrencyThreadLocal();
  NMib::NFunction::TCFunction<int ()> fRead = [&Data] { return Data.m_Value; };
  int Value = fRead();
  fRead.f_Clear();
  co_await fg_Work();
  co_return Value;
}
inline TCFuture<int> fg_R118AClearedOnOnePath(bool _bClear) {
  auto &Data = fg_ConcurrencyThreadLocal();
  NMib::NFunction::TCFunction<int ()> fRead = [&Data] { return Data.m_Value; };
// CHECK-MESSAGES: :[[@LINE-1]]:49: warning: a reference to thread-local data is stored in the capture of 'Data'
  if (_bClear)
    fRead.f_Clear();
  co_await fg_Work();
  co_return fRead ? fRead() : 0;
}
inline void fg_R118AClearedOnOnePathSaved(bool _bClear) {
  auto &Data = fg_ConcurrencyThreadLocal();
  NMib::NFunction::TCFunction<int ()> fRead = [&Data] { return Data.m_Value; };
// CHECK-MESSAGES: :[[@LINE-1]]:49: warning: a reference to thread-local data is stored in the capture of 'Data'
  if (_bClear)
    fRead.f_Clear();
  fg_R118ASave(fRead);
}
inline void fg_R118ASelfRecursive() {
  int &Visited = t_Value;
  NMib::NFunction::TCFunction<void (int _Depth)> fVisit;
  fVisit = [&](int _Depth) {
    ++Visited;
    if (_Depth)
      fVisit(_Depth - 1);
  };
  fVisit(3);
}
inline void fg_R118ASelfRecursiveInitialized() {
  int &Visited = t_Value;
  NMib::NFunction::TCFunction<void (int _Depth)> fVisit = [&](int _Depth) {
    ++Visited;
    if (_Depth)
      fVisit(_Depth - 1);
  };
  fVisit(3);
}
inline void fg_R118ASelfRecursiveInLoop(int _nRounds) {
  int &Visited = t_Value;
  NMib::NFunction::TCFunction<void (int _Depth)> fVisit;
  for (int iRound = 0; iRound < _nRounds; ++iRound) {
    fVisit = [&](int _Depth) {
      ++Visited;
      if (_Depth)
        fVisit(_Depth - 1);
    };
    fVisit(iRound);
  }
}
inline void fg_R118ASelfRecursiveKept() {
  int &Visited = t_Value;
  NMib::NFunction::TCFunction<void (int _Depth)> fVisit;
  fVisit = [&](int _Depth) {
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: a reference to thread-local data is stored in the capture of 'Visited'
    ++Visited;
    if (_Depth)
      fVisit(_Depth - 1);
  };
  fg_R118AKeep(fVisit);
}
// End of round 118, part A.


// Round 119: a local reference to a local function object of
// SynchronousFunctionTypes is a second name of its slot: what is done
// through either is done to it; a function that makes the guard of a scope
// exit, like the helper's `operator /`; what a helper gives of a thread it
// is told, `fg_SystemThreadLocal(_ThreadID)`.
void fg_R119Save(NMib::NFunction::TCFunction<int ()> const &_fRead);
inline TCFuture<int> fg_R119AliasClearedBeforeSuspension() {
  NMib::NFunction::TCFunction<int ()> fRead = [pData = &fg_ConcurrencyThreadLocal()] { return pData->m_Value; };
  auto &fAlias = fRead;
  auto Count = fAlias();
  fRead.f_Clear();
  co_await fg_Work();
  co_return Count;
}
inline TCFuture<int> fg_R119ClearedThroughAlias() {
  auto &Data = fg_ConcurrencyThreadLocal();
  NMib::NFunction::TCFunction<int ()> fRead = [&Data] { return Data.m_Value; };
  auto &fAlias = fRead;
  int Value = fRead();
  fAlias.f_Clear();
  co_await fg_Work();
  co_return Value;
}
inline void fg_R119AliasReplaced() {
  auto &Data = fg_ConcurrencyThreadLocal();
  NMib::NFunction::TCFunction<int ()> fRead = [&Data] { return Data.m_Value; };
  auto &fAlias = fRead;
  fRead = [] { return 0; };
  fg_R119Save(fAlias);
}
inline void fg_R119ReplacedThroughAlias() {
  auto &Data = fg_ConcurrencyThreadLocal();
  NMib::NFunction::TCFunction<int ()> fRead = [&Data] { return Data.m_Value; };
  auto &fAlias = fRead;
  fAlias = [] { return 0; };
  fg_R119Save(fRead);
}
inline void fg_R119AliasSavedBeforeReplaced() {
  auto &Data = fg_ConcurrencyThreadLocal();
  NMib::NFunction::TCFunction<int ()> fRead = [&Data] { return Data.m_Value; };
// CHECK-MESSAGES: :[[@LINE-1]]:49: warning: a reference to thread-local data is stored in the capture of 'Data'
  auto &fAlias = fRead;
  fg_R119Save(fAlias);
  fRead = [] { return 0; };
}
inline TCFuture<int> fg_R119AliasAcrossSuspension() {
  auto &Data = fg_ConcurrencyThreadLocal();
  NMib::NFunction::TCFunction<int ()> fRead = [&Data] { return Data.m_Value; };
// CHECK-MESSAGES: :[[@LINE-1]]:49: warning: a reference to thread-local data is stored in the capture of 'Data'
  auto &fAlias = fRead;
  co_await fg_Work();
  co_return fAlias();
}
inline TCFuture<int> fg_R119ChosenReference(bool _bChoose) {
  auto &Data = fg_ConcurrencyThreadLocal();
  NMib::NFunction::TCFunction<int ()> fRead = [&Data] { return Data.m_Value; };
// CHECK-MESSAGES: :[[@LINE-1]]:49: warning: a reference to thread-local data is stored in the capture of 'Data'
  NMib::NFunction::TCFunction<int ()> fOther;
  auto &fChosen = _bChoose ? fRead : fOther;
  fRead.f_Clear();
  co_await fg_Work();
  co_return fChosen ? 1 : 0;
}
inline void fg_R119ScopeExitFunction() {
  auto &Data = fg_ConcurrencyThreadLocal();
  auto Cleanup = NMib::fg_OnScopeExit([&Data] { Data.m_Value = 0; });
}
inline TCFuture<int> fg_R119ScopeExitFunctionAcrossSuspension() {
  auto &Data = fg_ConcurrencyThreadLocal();
  auto Cleanup = NMib::fg_OnScopeExit([&Data] { Data.m_Value = 0; });
// CHECK-MESSAGES: :[[@LINE-1]]:41: warning: a reference to thread-local data is stored in the capture of 'Data'
  co_return co_await fg_Work();
}
inline CSystemThreadLocal &fg_R119WorkerData(int _ThreadID) { return fg_SystemThreadLocal(_ThreadID); }
inline CSystemThreadLocal &fg_R119WorkerDataNested(int _ThreadID) { return fg_R119WorkerData(_ThreadID); }
inline CSystemThreadLocal &fg_R119CallerData() { return fg_SystemThreadLocal(); }
CSystemThreadLocal *g_pR119WorkerData;
inline void fg_R119StoreWorkerData(int _ThreadID) {
  g_pR119WorkerData = &fg_R119WorkerData(_ThreadID);
  g_pR119WorkerData = &fg_R119WorkerDataNested(_ThreadID);
  g_pR119WorkerData = &fg_R119CallerData();
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a pointer to thread-local data is stored in the variable 'g_pR119WorkerData'
}
// End of round 119.


// Round 120: a copy of a member of a local holder that can't hold a pointer
// or a reference carries nothing of what the holder borrows; not one that
// can.
struct CR120Stats {
  int m_nProcessed = 0;
};
struct CR120Link {
  int *m_pValue = nullptr;
};
struct CR120Report {
  CR120Report(int *_pCount) : m_pCount(_pCount), m_Link{_pCount} {}
  int *m_pCount;
  CR120Stats m_Stats;
  CR120Link m_Link;
};
inline CR120Stats fg_R120StatsCopy() {
  CR120Report Report(&fg_ConcurrencyThreadLocal().m_Value);
  return Report.m_Stats;
}
inline CR120Stats fg_R120StatsAssigned(CR120Stats &o_Stats) {
  CR120Report Report(&fg_ConcurrencyThreadLocal().m_Value);
  o_Stats = Report.m_Stats;
  return o_Stats;
}
inline CR120Link fg_R120LinkCopy() {
  CR120Report Report(&fg_ConcurrencyThreadLocal().m_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: a pointer to thread-local data is stored in the 'CR120Report' made of it
  return Report.m_Link;
}
// Also a copy of what a getter of the holder gives a reference to, not one
// that can hold a pointer.
struct CR150Profiler {
  CR150Profiler(int *_pCalls) : m_pCalls(_pCalls), m_Link{_pCalls} {}
  CR120Stats const &f_GetStatistics() const { return m_Stats; }
  CR120Link const &f_GetLink() const { return m_Link; }
  int *m_pCalls;
  CR120Stats m_Stats;
  CR120Link m_Link;
};
inline CR120Stats fg_R150Snapshot() {
  CR150Profiler Profiler(&fg_ConcurrencyThreadLocal().m_Value);
  return Profiler.f_GetStatistics();
}
inline CR120Link fg_R150LinkSnapshot() {
  CR150Profiler Profiler(&fg_ConcurrencyThreadLocal().m_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:26: warning: a pointer to thread-local data is stored in the 'CR150Profiler' made of it
  return Profiler.f_GetLink();
}
// End of round 120.


// Round 121: a reference init capture of a local, by what the local holds
// where the closure is made and after, as a capture of it by reference; not
// what it held only before.
inline TCFuture<int> fg_R121ReferenceInitCapture() {
  int *pValue = &t_Value;
  int Owned = 0;
  pValue = &Owned;
  auto fRead = [&pCurrent = pValue] { return *pCurrent; };
  co_await fg_Work();
  co_return fRead();
}
inline TCFuture<int> fg_R121ReferenceInitCaptureData() {
  int Owned = 0;
  int *pValue = &Owned;
  pValue = &t_Value;
  auto fRead = [&pCurrent = pValue] { return *pCurrent; };
// CHECK-MESSAGES: :[[@LINE-1]]:29: warning: a pointer to thread-local data is stored in the capture of 'pCurrent'
  co_await fg_Work();
  co_return fRead();
}
// End of round 121.


// Round 122: a capture by reference of what the lambda it is made in copies
// refers to that copy, which that closure keeps; not of what it captures by
// reference.
struct CR122Stats {
  int m_nBytes = 0;
};
thread_local CR122Stats t_R122Stats;
void fg_R122Log(int _nBytes);
inline void fg_R122NestedCopyCapture() {
  auto const &Stats = t_R122Stats;
  auto fMakeCleanup = [Stats] { return NMib::g_OnScopeExit / [&Stats] { fg_R122Log(Stats.m_nBytes); }; };
  auto Cleanup = fMakeCleanup();
}
inline void fg_R122NestedReferenceCapture() {
  auto const &Stats = t_R122Stats;
  auto fMakeCleanup = [&] { return NMib::g_OnScopeExit / [&Stats] { fg_R122Log(Stats.m_nBytes); }; };
// CHECK-MESSAGES: :[[@LINE-1]]:60: warning: a reference to thread-local data is stored in the capture of 'Stats'
  auto Cleanup = fMakeCleanup();
}
// End of round 122.

// What is held for each thread, where code that isn't a template's made the
// instantiation: by a factory or a local of a function template.
template <typename t_CType> t_CType *fg_MakeOnHeap() { return new t_CType(); }
template <typename t_CType> void fg_HoldLocally() { t_CType Local; }
inline void *fg_MakeThreadLocal() {
  fg_HoldLocally<TCThreadLocal<CHeaderData>>();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'Local' holds 'CHeaderData' for each thread
  return fg_MakeOnHeap<TCThreadLocal<CHeaderData>>();
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the object made on the heap holds 'CHeaderData' for each thread
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
// A coroutine that can't suspend runs to its end in the call: what it is
// given isn't kept past it. One that suspends keeps it in its frame.
inline TCFuture<int> fg_R184ReadCounter(int const *_pCounter) { co_return *_pCounter; }
inline TCFuture<int> fg_R184ReadNow() {
  co_return co_await fg_R184ReadCounter(&fg_ConcurrencyThreadLocal().m_Value);
}
inline TCFuture<int> fg_R184ReadLater(int const *_pCounter) {
  co_await fg_Work();
  co_return *_pCounter;
}
inline TCFuture<int> fg_R184ReadLaterNow() {
  co_return co_await fg_R184ReadLater(&fg_ConcurrencyThreadLocal().m_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:39: warning: a pointer to thread-local data is given to 'fg_R184ReadLater', which stores it
}
// So is the object of a member coroutine (round 187).
inline TCFuture<int> fg_R187ReadStatistics() {
  co_return co_await fg_ConcurrencyThreadLocal().m_Statistics.f_Read();
}
inline TCFuture<int> fg_R187ReadStatisticsLater() {
  co_return co_await fg_ConcurrencyThreadLocal().m_Statistics.f_ReadLater();
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning:
}
} // namespace NThreadLocals

