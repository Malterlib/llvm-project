// RUN: %check_clang_tidy -std=c++23-or-later %s malterlib-coroutine-reference-capture %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -I %S/Inputs

#include "malterlib-concurrency.h"
using namespace NMib::NConcurrency;

TCFuture<int> fg_Work();

// What a getter with an explicit object gives of a local is of that local, as
// of one with an implicit object, also through a pointer to it and as an
// operator; not what it gives of elsewhere.
int g_Values[4];
struct CBuffer
{
	int m_Values[4];
	int *f_GetArray(this CBuffer &_This) { return _This.m_Values; }
	int *f_GetGlobal(this CBuffer &_This) { return g_Values; }
	int &operator[](this CBuffer &_This, int _iIndex) { return _This.m_Values[_iIndex]; }
};

TCFuture<int> fg_Explicit()
{
	CBuffer Buffer;
	return fg_CallSafe([pData = Buffer.f_GetArray()]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: a TCFuture coroutine lambda uses 'pData', which points to 'Buffer', after a suspension, when 'Buffer' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
		co_await fg_Work();
		co_return pData[0];
	});
}

TCFuture<int> fg_ThroughPointer()
{
	CBuffer Buffer;
	CBuffer *pBuffer = &Buffer;
	return fg_CallSafe([pData = pBuffer->f_GetArray()]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: a TCFuture coroutine lambda uses 'pData', which points to 'Buffer', after a suspension, when 'Buffer' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
		co_await fg_Work();
		co_return pData[0];
	});
}

TCFuture<int> fg_Operator()
{
	CBuffer Buffer;
	return fg_CallSafe([pData = &Buffer[1]]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: a TCFuture coroutine lambda uses 'pData', which points to 'Buffer', after a suspension, when 'Buffer' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
		co_await fg_Work();
		co_return *pData;
	});
}

TCFuture<int> fg_Global()
{
	CBuffer Buffer;
	return fg_CallSafe([pData = Buffer.f_GetGlobal()]() -> TCFuture<int> {
		co_await fg_Work();
		co_return pData[0];
	});
}


// Round 157: what a closure keeps alive, by the one model of it
// (storageRootsOf, isAlive): a shared owner copied, whatever its copy
// constructor does, a unique owner moved from a parameter by value or by
// `&&`, static storage a getter gives, the object of a class of
// CAllowUnsafeThis, and the explicit object of an actor.
namespace NReview2Round157
{
	TCFuture<int> fg_Work();
	struct CJob
	{
		int m_Result = 0;
	};
	template <typename t_CType>
	struct TCUniquePointer
	{
		t_CType *m_pPointTo;
		explicit TCUniquePointer(t_CType *_pPointTo) : m_pPointTo(_pPointTo) {}
		TCUniquePointer(TCUniquePointer &&_Other) : m_pPointTo(_Other.m_pPointTo) { _Other.m_pPointTo = nullptr; }
		~TCUniquePointer() { delete m_pPointTo; }
		t_CType *operator->() const { return m_pPointTo; }
		t_CType &operator*() const { return *m_pPointTo; }
	};
	DMibReturnsNew TCSharedPointer<CJob> fg_MakeJob();
	TCFuture<int> fg_SharedCopy()
	{
		TCSharedPointer<CJob> pJob = fg_MakeJob();
		int &Result = pJob->m_Result;
		return fg_CallSafe([&Result, pJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
	}
	TCFuture<int> fg_SharedCleared()
	{
		TCSharedPointer<CJob> pJob = fg_MakeJob();
		int &Result = pJob->m_Result;
		pJob.f_Clear();
		return fg_CallSafe([&Result, pJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
	}
	TCFuture<int> fg_RValueParameter(TCUniquePointer<CJob> &&_pJob)
	{
		auto *pResult = &_pJob->m_Result;
		return fg_CallSafe([pResult, pKeep = fg_Move(_pJob)]() -> TCFuture<int> { co_await fg_Work(); co_return *pResult; });
	}
	TCFuture<int> fg_ValueParameter(TCUniquePointer<CJob> _pJob)
	{
		CJob &Job = *_pJob;
		co_return co_await fg_CallSafe([&Job, pKeep = fg_Move(_pJob)]() -> TCFuture<int> { co_await fg_Work(); co_return Job.m_Result; });
	}
	TCFuture<int> fg_NotMovedParameter(TCUniquePointer<CJob> const &_pJob)
	{
		CJob &Job = *_pJob;
		return fg_CallSafe([&Job]() -> TCFuture<int> { co_await fg_Work(); co_return Job.m_Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Job', which it captures by reference, after a suspension, when 'Job' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
	}
	struct CConfig
	{
		int m_Timeout;
	};
	CConfig const &fg_Config()
	{
		static CConfig const s_Config{5};
		return s_Config;
	}
	void fg_StaticGetter()
	{
		(void)(g_Dispatch / [&Config = fg_Config()]() -> TCFuture<int> { co_await fg_Work(); co_return Config.m_Timeout; });
	}
	struct CUnsafe : CAllowUnsafeThis
	{
		int m_Value = 0;
		TCFuture<int> f_Init()
		{
			return fg_CallSafe([&Value = m_Value]() -> TCFuture<int> { co_await fg_Work(); co_return Value; });
		}
		TCFuture<int> f_Alias()
		{
			auto &Value = m_Value;
			return fg_CallSafe([&Value]() -> TCFuture<int> { co_await fg_Work(); co_return Value; });
		}
	};
	struct CApp : CActor
	{
		int m_Value = 0;
		void f_Run(this CApp &_Self)
		{
			_Self.self / [&Value = _Self.m_Value]() -> TCFuture<int> { co_await fg_Work(); co_return Value; };
		}
		TCFuture<int> f_Read(this CApp &_This)
		{
			auto &Value = _This.m_Value;
			return fg_CallSafe([&Value]() -> TCFuture<int> { co_await fg_Work(); co_return Value; });
		}
	};
}


// Round 159: the allocation an owner holds is kept by any copy or move of it
// (allocationOf), also taken before or after the reference into it or through
// an alias; a view captured by value is checked like a pointer; a member of
// the object is kept by a shared owner of it; and the object of a class of
// CAllowUnsafeThis is alive also through a reference or pointer parameter.
// What is released before the copy, a view whose owner isn't taken, and what
// a reference parameter of another class refers to are reported.
namespace NReview2Round159 {
TCFuture<int> fg_Work();
struct CJob { int m_Result = 0; };
DMibReturnsNew TCSharedPointer<CJob> fg_MakeJob();
template <typename t_CType>
struct TCUniquePointer {
  t_CType *m_pPointTo;
  explicit TCUniquePointer(t_CType *_pPointTo) : m_pPointTo(_pPointTo) {}
  TCUniquePointer(TCUniquePointer &&_Other) : m_pPointTo(_Other.m_pPointTo) { _Other.m_pPointTo = nullptr; }
  ~TCUniquePointer() { delete m_pPointTo; }
  t_CType *operator->() const { return m_pPointTo; }
};
template <typename t_CData>
struct TCVector { TCVector(); TCVector(TCVector &&_Other); ~TCVector(); t_CData *f_GetArray() { return mp_pData; } t_CData *mp_pData; };
struct CStrSpan { CStrSpan(char const *_pData, int _Length); char const *m_pData; int m_Length; char const *f_GetStr() const { return m_pData; } };
// 1: copies of a shared owner, before and after the reference.
TCFuture<int> fg_CopyBefore() {
  TCSharedPointer<CJob> pJob = fg_MakeJob();
  auto pKeep = pJob;
  int &Result = pJob->m_Result;
  return fg_CallSafe([&Result, pKeep]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_CopyAfter() {
  TCSharedPointer<CJob> pJob = fg_MakeJob();
  int &Result = pJob->m_Result;
  auto pKeep = pJob;
  return fg_CallSafe([&Result, pKeep]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_AliasUnique() {
  TCUniquePointer<CJob> pOwner(new CJob);
  auto const &pAlias = pOwner;
  int &Result = pAlias->m_Result;
  return fg_CallSafe([&Result, pKeep = fg_Move(pOwner)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// 2: a view captured by value.
TCFuture<int> fg_View() {
  TCVector<char> Data;
  CStrSpan Span(Data.f_GetArray(), 8);
  return fg_CallSafe([Span, Data = fg_Move(Data)]() -> TCFuture<int> { co_await fg_Work(); co_return *Span.f_GetStr(); });
}
// 3: a member of the object with a shared owner of it.
struct CState {
  int m_Result = 0;
  TCFuture<int> f_Run() {
    TCSharedPointer<CState> pThis{fg_Explicit(this)};
    int &Result = m_Result;
    return fg_CallSafe([&Result, pThis]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
  }
};
// 5: CAllowUnsafeThis through parameters.
struct CUnsafe : CAllowUnsafeThis { int m_Value = 0; };
TCFuture<int> fg_ReadReference(CUnsafe &_State) { return fg_CallSafe([&_State]() -> TCFuture<int> { co_await fg_Work(); co_return _State.m_Value; }); }
TCFuture<int> fg_ReadPointer(CUnsafe *_pState) { return fg_CallSafe([pValue = &_pState->m_Value]() -> TCFuture<int> { co_await fg_Work(); co_return *pValue; }); }
// controls
TCFuture<int> fg_ClearedCopy() {
  TCSharedPointer<CJob> pJob = fg_MakeJob();
  int &Result = pJob->m_Result;
  pJob.f_Clear();
  auto pKeep = pJob;
  return fg_CallSafe([&Result, pKeep]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
TCFuture<int> fg_ViewNotMoved() {
  TCVector<char> Data;
  CStrSpan Span(Data.f_GetArray(), 8);
  return fg_CallSafe([Span]() -> TCFuture<int> { co_await fg_Work(); co_return *Span.f_GetStr(); });
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a TCFuture coroutine lambda uses 'Span', which points to 'Data', after a suspension, when 'Data' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
struct CPlainState { int m_Value = 0; };
TCFuture<int> fg_ReadPlain(CPlainState &_State) { return fg_CallSafe([&_State]() -> TCFuture<int> { co_await fg_Work(); co_return _State.m_Value; }); }
// CHECK-MESSAGES: :[[@LINE-1]]:72: warning: a TCFuture coroutine lambda uses '_State', which it captures by reference, after a suspension, when '_State' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
// Round 161: what an owner slot holds is what reaches the reference and the
// capture (flowEventsReaching): an assignment or a change of a buffer before
// the reference, a copy cleared after it or moved into the closure, the
// capture's own copy cleared after the closure is made, are clean, and so are
// a member of `this`, of a local or of the explicit object, f_ShareAsConst,
// a tag argument of a view, an actor helper given its own object, and a
// structured binding of an actor's member; a release or another allocation
// between the reference and the capture, a buffer changed between them, an
// object holding a pointer into what an owner the closure doesn't take owns,
// or into an earlier pass of a loop, and a member of `this` a member function
// can change between them are reported.
namespace NReview2Round161 {
using namespace NMib::NStorage;
TCFuture<int> fg_Work();
struct CJob { int m_Result = 0; };
DMibReturnsNew TCSharedPointer<CJob> fg_MakeJob();
template <typename t_CData>
struct TCVector { TCVector(); TCVector(TCVector &&_Other); ~TCVector(); void f_InsertLast(t_CData const &_Value); t_CData *f_GetArray() { return mp_pData; } t_CData *mp_pData; };
struct CInitByRange {};
struct CStrPtr {
  CStrPtr(CInitByRange, char const *_pStart, char const *_pEnd);
  CStrPtr(char const *_pData, int _Length);
  char const *f_GetStr() const { return m_pData; }
  char const *m_pData;
  int m_Length;
};
// Clean: populated before the borrow.
TCFuture<int> fg_InsertBefore() {
  TCVector<int> Values;
  Values.f_InsertLast(42);
  auto *pValue = Values.f_GetArray();
  return fg_CallSafe([pValue, Values = fg_Move(Values)]() -> TCFuture<int> { co_await fg_Work(); co_return *pValue; });
}
// Clean: an intermediate copy moved into the capture.
TCFuture<int> fg_MoveCopy() {
  TCSharedPointer<CJob> pJob = fg_MakeJob();
  auto pCopy = pJob;
  int &Result = pJob->m_Result;
  return fg_CallSafe([&Result, pKeep = fg_Move(pCopy)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// Clean: the capture's copy cleared after the closure is made.
TCFuture<int> fg_ClearAfter() {
  TCSharedPointer<CJob> pJob = fg_MakeJob();
  auto pKeep = pJob;
  int &Result = pJob->m_Result;
  auto Future = fg_CallSafe([&Result, pKeep]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
  pKeep = {};
  return Future;
}
// Clean: the borrowed copy cleared after the borrow.
TCFuture<int> fg_ClearBorrowed() {
  TCSharedPointer<CJob> pOwner = fg_MakeJob();
  auto pCopy = pOwner;
  int &Result = pCopy->m_Result;
  pCopy.f_Clear();
  return fg_CallSafe([&Result, pOwner]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// Clean: assigned before the borrow.
TCFuture<int> fg_AssignBefore() {
  TCSharedPointer<CJob> pJob;
  pJob = fg_MakeJob();
  int &Result = pJob->m_Result;
  return fg_CallSafe([&Result, pJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// Clean: f_ShareAsConst.
TCFuture<int> fg_ShareAsConst() {
  TCSharedPointer<CJob> pJob = fg_MakeJob();
  int const &Result = pJob->m_Result;
  return fg_CallSafe([&Result, pKeep = pJob.f_ShareAsConst()]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// Clean: a member of a local holder.
struct CHolder { TCSharedPointer<CJob> m_pJob; };
TCFuture<int> fg_HolderMember() {
  CHolder Holder{fg_MakeJob()};
  int &Result = Holder.m_pJob->m_Result;
  return fg_CallSafe([&Result, pKeep = Holder.m_pJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// Clean: a member of `this`.
struct CMemberOwner {
  TCSharedPointer<CJob> mp_pJob;
  TCFuture<int> f_Run() {
    int &Result = mp_pJob->m_Result;
    return fg_CallSafe([&Result, pJob = mp_pJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
  }
};
// Clean: the explicit object kept by a shared owner of it.
struct CState {
  int m_Result = 0;
  TCFuture<int> f_Run(this CState &_Self) {
    TCSharedPointer<CState> pThis{fg_Explicit(&_Self)};
    int &Result = _Self.m_Result;
    return fg_CallSafe([&Result, pThis]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
  }
};
// Clean: a tag argument of a view.
TCFuture<int> fg_RangeView() {
  TCVector<char> Data;
  CStrPtr View(CInitByRange{}, Data.f_GetArray(), Data.f_GetArray() + 8);
  return fg_CallSafe([View, Data = fg_Move(Data)]() -> TCFuture<int> { co_await fg_Work(); co_return View.f_GetStr()[0]; });
}
// Clean: an actor helper given the actor's own `this`.
struct CApp : public CActor {
  int m_Value = 0;
  struct CCounters { int m_nStarted = 0; int m_nFinished = 0; } m_Counters;
  static TCFuture<int> fs_Read(CApp *_pThis) {
    return fg_CallSafe([&Value = _pThis->m_Value]() -> TCFuture<int> { co_await fg_Work(); co_return Value; });
  }
  TCFuture<int> f_Read() { return fs_Read(this); }
  // Clean: a structured binding of the actor's member.
  TCFuture<int> f_Binding() {
    auto &[nStarted, nFinished] = m_Counters;
    return self / [&nStarted]() -> TCFuture<int> { co_await fg_Work(); co_return nStarted; };
  }
};
// Reported: a release reaching the capture.
TCFuture<int> fg_ReleaseBefore() {
  TCSharedPointer<CJob> pJob = fg_MakeJob();
  int &Result = pJob->m_Result;
  pJob.f_Clear();
  return fg_CallSafe([&Result, pJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
// Reported: a reassignment between borrow and capture.
TCFuture<int> fg_Reassigned() {
  TCSharedPointer<CJob> pJob = fg_MakeJob();
  int &Result = pJob->m_Result;
  pJob = fg_MakeJob();
  return fg_CallSafe([&Result, pJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
// Reported: buffer changed between borrow and capture.
TCFuture<int> fg_InsertAfter() {
  TCVector<int> Values;
  auto *pValue = Values.f_GetArray();
  Values.f_InsertLast(42);
  return fg_CallSafe([pValue, Values = fg_Move(Values)]() -> TCFuture<int> { co_await fg_Work(); co_return *pValue; });
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a TCFuture coroutine lambda uses 'pValue', which points to 'Values', after a suspension, when 'Values' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
// Reported: an object holding a pointer into what an owner the closure
// doesn't take owns.
struct CResultRef { int *m_pResult; };
TCFuture<int> fg_Unrelated() {
  TCSharedPointer<CJob> pJob = fg_MakeJob();
  TCSharedPointer<CJob> pOther = fg_MakeJob();
  CResultRef Ref{&pJob->m_Result};
  return fg_CallSafe([Ref, pOther]() -> TCFuture<int> { co_await fg_Work(); co_return *Ref.m_pResult; });
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a TCFuture coroutine lambda uses 'Ref', which points to 'pJob', after a suspension, when 'pJob' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
// Reported: in a loop, a new job each time with the borrow from the first.
TCFuture<int> fg_Loop() {
  TCSharedPointer<CJob> pJob;
  CResultRef Ref{nullptr};
  for (int i = 0; i < 2; ++i) {
    pJob = fg_MakeJob();
    if (i == 0)
      Ref.m_pResult = &pJob->m_Result;
  }
  return fg_CallSafe([Ref, pJob]() -> TCFuture<int> { co_await fg_Work(); co_return *Ref.m_pResult; });
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a TCFuture coroutine lambda uses 'Ref', which points to 'pJob', after a suspension, when 'pJob' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
// Reported: a member of `this` changed by a call between.
struct CMemberChanged {
  TCSharedPointer<CJob> mp_pJob;
  void f_Reset();
  TCFuture<int> f_Run() {
    int &Result = mp_pJob->m_Result;
    f_Reset();
    return fg_CallSafe([&Result, pJob = mp_pJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:26: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
  }
};
}
// Round 163: an owner slot keeps what a reference into what it owns refers
// to when it isn't changed between the reference and the capture, whatever
// reaches it; an owner in a record captured whole, a capture of the closure
// around, a tuple-like binding, an element, a getter and a forward are slots;
// a non-suspending await and a member function that doesn't change the slot
// aren't changes of it.
namespace std {
template <typename t_CType> struct tuple_size;
template <decltype(sizeof(0)) t_Index, typename t_CType> struct tuple_element;
}
namespace NReview2Round163 {
using namespace NMib::NStorage;
TCFuture<int> fg_Work();
struct CJob { int m_Result = 0; };
DMibReturnsNew TCSharedPointer<CJob> fg_MakeJob();
template <typename t_CType> t_CType const &fg_Const(t_CType const &_Value) { return _Value; }
template <typename t_CData>
struct TCVector {
  TCVector(); TCVector(TCVector &&_Other); ~TCVector();
  void f_SetLen(int _Length);
  void f_Clear();
  t_CData *f_GetArray() { return mp_pData; }
  t_CData *f_GetArray(int _Length) { f_SetLen(_Length); return mp_pData; }
  t_CData &operator[](int _Index) { return mp_pData[_Index]; }
  t_CData *mp_pData;
};
struct CPair { TCSharedPointer<CJob> m_pFirst; int m_Second = 0; template <decltype(sizeof(0)) t_Index> TCSharedPointer<CJob> &get() { return m_pFirst; } };
}
template <> struct std::tuple_size<NReview2Round163::CPair> { static constexpr decltype(sizeof(0)) value = 1; };
template <> struct std::tuple_element<0, NReview2Round163::CPair> { using type = NMib::NStorage::TCSharedPointer<NReview2Round163::CJob>; };
namespace NReview2Round163 {
// Clean: conditional initialization before the reference.
TCFuture<int> fg_Conditional(bool _b) {
  auto pJob = fg_MakeJob();
  if (_b)
    pJob = fg_MakeJob();
  int &Result = pJob->m_Result;
  return fg_CallSafe([&Result, pJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// Clean: an owner inside a captured aggregate.
struct CJobContext { TCSharedPointer<CJob> m_pJob; int m_Other = 0; };
TCFuture<int> fg_Aggregate() {
  CJobContext Context{fg_MakeJob()};
  int &Result = Context.m_pJob->m_Result;
  return fg_CallSafe([&Result, Context]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// Clean: a non-suspending await and an unrelated member change.
struct CMember {
  TCSharedPointer<CJob> mp_pJob;
  int m_nAccess = 0;
  void f_CountAccess() { ++m_nAccess; }
  void f_Reset() { mp_pJob = {}; }
  TCFuture<int> f_Flag() {
    int &Result = mp_pJob->m_Result;
    co_await ECoroutineFlag_CaptureExceptions;
    co_return co_await fg_CallSafe([&Result, pJob = mp_pJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
  }
  TCFuture<int> f_Count() {
    int &Result = mp_pJob->m_Result;
    f_CountAccess();
    return fg_CallSafe([&Result, pJob = mp_pJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
  }
  // Reported: a member function that resets it, and a suspension.
  TCFuture<int> f_Reset2() {
    int &Result = mp_pJob->m_Result;
    f_Reset();
    return fg_CallSafe([&Result, pJob = mp_pJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:26: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
  }
  TCFuture<int> f_Suspended() {
    int &Result = mp_pJob->m_Result;
    co_await fg_Work();
    co_return co_await fg_CallSafe([&Result, pJob = mp_pJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:38: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
  }
};
// Clean: an enclosing closure's capture, and its init capture.
TCFuture<int> fg_Enclosing() {
  TCSharedPointer<CJob> pJob = fg_MakeJob();
  return fg_CallSafe([pJob]() -> TCFuture<int> {
    int &Result = pJob->m_Result;
    co_return co_await fg_CallSafe([&Result, pJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
  });
}
TCFuture<int> fg_EnclosingInit() {
  return fg_CallSafe([pJob = fg_MakeJob()]() -> TCFuture<int> {
    int &Result = pJob->m_Result;
    co_return co_await fg_CallSafe([&Result, pJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
  });
}
// Clean: a tuple binding.
TCFuture<int> fg_Tuple() {
  auto [pBound] = CPair{fg_MakeJob()};
  int &Result = pBound->m_Result;
  return fg_CallSafe([&Result, pBound]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// Clean: a container element, a getter, a forward.
TCFuture<int> fg_Element(TCVector<TCSharedPointer<CJob>> &_Jobs) {
  int &Result = _Jobs[0]->m_Result;
  return fg_CallSafe([&Result, pJob = _Jobs[0]]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
struct CJobs { TCSharedPointer<CJob> m_pJob; TCSharedPointer<CJob> &f_Job() { return m_pJob; } };
TCFuture<int> fg_Getter(CJobs &_Jobs) {
  int &Result = _Jobs.f_Job()->m_Result;
  return fg_CallSafe([&Result, pJob = _Jobs.f_Job()]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_Forward() {
  TCSharedPointer<CJob> pJob = fg_MakeJob();
  int const &Result = fg_Const(pJob)->m_Result;
  return fg_CallSafe([&Result, pJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// Clean: a buffer changed by the call that gives the pointer.
TCFuture<int> fg_GetArrayLength() {
  TCVector<int> Values;
  auto *pValue = Values.f_GetArray(8);
  return fg_CallSafe([pValue, Values = fg_Move(Values)]() -> TCFuture<int> { co_await fg_Work(); co_return *pValue; });
}
// Reported: a change on one path between.
TCFuture<int> fg_OnePath(bool _b) {
  auto pJob = fg_MakeJob();
  int &Result = pJob->m_Result;
  if (_b)
    pJob = fg_MakeJob();
  return fg_CallSafe([&Result, pJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
TCFuture<int> fg_ReleaseOneBranch(bool _b) {
  auto pJob = fg_MakeJob();
  int &Result = pJob->m_Result;
  if (_b)
    pJob.f_Clear();
  return fg_CallSafe([&Result, pJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
// Reported: a container change between.
TCFuture<int> fg_ContainerChanged(TCVector<TCSharedPointer<CJob>> &_Jobs) {
  int &Result = _Jobs[0]->m_Result;
  _Jobs.f_Clear();
  return fg_CallSafe([&Result, pJob = _Jobs[0]]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
// Reported: another element.
TCFuture<int> fg_OtherElement(TCVector<TCSharedPointer<CJob>> &_Jobs) {
  int &Result = _Jobs[0]->m_Result;
  return fg_CallSafe([&Result, pJob = _Jobs[1]]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
// Reported: the aggregate's owner reset between.
TCFuture<int> fg_AggregateReset() {
  CJobContext Context{fg_MakeJob()};
  int &Result = Context.m_pJob->m_Result;
  Context.m_pJob = fg_MakeJob();
  return fg_CallSafe([&Result, Context]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
}
// Round 165: the slot of each pass of a loop is its own; owner paths through
// local pointers; record copies carry their owner members; member functions
// change only what they can, also final overrides; identity uses of `this`
// change nothing; closures made in closures, and records holding a shared
// owner of `this`, keep what they hold.
namespace NReview2Round165Capture {
using namespace NMib::NStorage;
TCFuture<int> fg_Work();
struct CJob { int m_Result = 0; };
DMibReturnsNew TCSharedPointer<CJob> fg_MakeJob();
struct CContext { TCSharedPointer<CJob> m_pJob; int m_nAccess = 0; void f_CountAccess() { ++m_nAccess; } void f_Reset() { m_pJob = {}; } };
CContext *fg_FindJob(int _Key);
extern bool g_bChoose;
// Clean: a loop body, each pass its own owner.
void fg_Loop() {
  for (int i = 0; i < 2; ++i) {
    auto pJob = fg_MakeJob();
    int &Result = pJob->m_Result;
    fg_CallSafe([&Result, pJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
  }
}
// Clean: owner paths through local pointers.
TCFuture<int> fg_PointerToLocal() {
  CContext Context{fg_MakeJob()};
  auto *pContext = &Context;
  int &Result = pContext->m_pJob->m_Result;
  return fg_CallSafe([&Result, pKeep = pContext->m_pJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_PointerFound(int _Key) {
  auto *pEntry = fg_FindJob(_Key);
  int &Result = pEntry->m_pJob->m_Result;
  return fg_CallSafe([&Result, pKeep = pEntry->m_pJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
struct CSelf {
  TCSharedPointer<CJob> mp_pJob;
  int m_nAccess = 0;
  virtual void f_Virtual();
  TCFuture<int> f_Alias() {
    auto *pSelf = this;
    int &Result = pSelf->mp_pJob->m_Result;
    return fg_CallSafe([&Result, pKeep = mp_pJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
  }
  // Clean: an identity comparison.
  TCFuture<int> f_Same(CSelf *_pOther) {
    int &Result = mp_pJob->m_Result;
    bool bSame = this == _pOther;
    return fg_CallSafe([&Result, pJob = mp_pJob, bSame]() -> TCFuture<int> { co_await fg_Work(); co_return Result + bSame; });
  }
  // Reported: a non-final virtual call between.
  TCFuture<int> f_CallVirtual() {
    int &Result = mp_pJob->m_Result;
    f_Virtual();
    return fg_CallSafe([&Result, pJob = mp_pJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:26: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
  }
};
// Clean: a final method that doesn't change it.
struct CFinal {
  TCSharedPointer<CJob> mp_pJob;
  int m_nAccess = 0;
  virtual void f_Count();
};
struct CFinalDerived final : CFinal {
  void f_Count() override { ++m_nAccess; }
  TCFuture<int> f_Run() {
    int &Result = mp_pJob->m_Result;
    f_Count();
    return fg_CallSafe([&Result, pJob = mp_pJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
  }
};
// Clean: a sibling member written, and a local record's member call.
struct CHolderMember {
  CContext mp_Context;
  TCFuture<int> f_Sibling() {
    int &Result = mp_Context.m_pJob->m_Result;
    ++mp_Context.m_nAccess;
    mp_Context.f_CountAccess();
    return fg_CallSafe([&Result, pJob = mp_Context.m_pJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
  }
};
TCFuture<int> fg_LocalCall() {
  CContext Context{fg_MakeJob()};
  int &Result = Context.m_pJob->m_Result;
  Context.f_CountAccess();
  return fg_CallSafe([&Result, pJob = Context.m_pJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// Clean: a copy of the record, before and after the reference.
TCFuture<int> fg_RecordCopyBefore() {
  CContext Context{fg_MakeJob()};
  CContext Copy = Context;
  int &Result = Context.m_pJob->m_Result;
  return fg_CallSafe([&Result, Copy]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_RecordCopyAfter() {
  CContext Context{fg_MakeJob()};
  int &Result = Context.m_pJob->m_Result;
  CContext Copy = Context;
  return fg_CallSafe([&Result, Copy]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// Clean: a factory closure.
TCFuture<int> fg_Factory() {
  auto pJob = fg_MakeJob();
  int &Result = pJob->m_Result;
  auto fRun = [&Result, pJob] { return fg_CallSafe([&Result, pJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; }); };
  return fRun();
}
// Reported: a release in the same pass between.
void fg_LoopRelease() {
  for (int i = 0; i < 2; ++i) {
    auto pJob = fg_MakeJob();
    int &Result = pJob->m_Result;
    pJob.f_Clear();
    fg_CallSafe([&Result, pJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
  }
}
// Reported: a write through the alias pointer.
TCFuture<int> fg_WriteThroughAlias() {
  CContext Context{fg_MakeJob()};
  auto *pContext = &Context;
  int &Result = Context.m_pJob->m_Result;
  pContext->m_pJob = fg_MakeJob();
  return fg_CallSafe([&Result, pKeep = Context.m_pJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
TCFuture<int> fg_ResetThroughAlias() {
  CContext Context{fg_MakeJob()};
  auto *pContext = &Context;
  int &Result = Context.m_pJob->m_Result;
  pContext->f_Reset();
  return fg_CallSafe([&Result, pKeep = Context.m_pJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
// Reported: the owner reassigned through the copy before the capture.
TCFuture<int> fg_CopyReassigned() {
  CContext Context{fg_MakeJob()};
  int &Result = Context.m_pJob->m_Result;
  CContext Copy = Context;
  Copy.m_pJob = fg_MakeJob();
  return fg_CallSafe([&Result, Copy]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
}
// Round 166: an exchange gives the value it moves out; a const alias, a record
// moved by a cast and an extended temporary are slots; a borrow in the
// closure's own captures is made with them; an explicit-object operator maps
// its arguments by argumentShift; a borrow after the value is moved out isn't
// kept.
namespace NReview2Round166Capture {
using namespace NMib::NStorage;
TCFuture<int> fg_Work();
struct CJob { int m_Result = 0; };
DMibReturnsNew TCSharedPointer<CJob> fg_MakeJob();
template <typename t_CType> t_CType fg_Exchange(t_CType &_Value, decltype(nullptr));
template <typename t_CType> t_CType const &fg_Const(t_CType const &_Value) { return _Value; }
struct CContext { TCSharedPointer<CJob> m_pJob; int m_nRequestID = 0; };
struct CLogger { CLogger &operator<<(this CLogger &_This, void const *_pObject); };
extern CLogger g_Log;
// Clean: exchange.
TCFuture<int> fg_Exchanged() {
  TCSharedPointer<CJob> pJob = fg_MakeJob();
  int &Result = pJob->m_Result;
  auto pKeep = fg_Exchange(pJob, nullptr);
  return fg_CallSafe([&Result, pKeep]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// Clean: a const alias.
TCFuture<int> fg_ConstAlias() {
  TCSharedPointer<CJob> pJob = fg_MakeJob();
  int &Result = pJob->m_Result;
  auto const &pAlias = fg_Const(pJob);
  return fg_CallSafe([&Result, pAlias]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// Clean: an xvalue cast of a record.
TCFuture<int> fg_XValue() {
  CContext Context{fg_MakeJob()};
  int &Result = Context.m_pJob->m_Result;
  return fg_CallSafe([&Result, Kept = static_cast<CContext &&>(Context)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// Clean: a lifetime-extended temporary.
TCFuture<int> fg_Temporary() {
  auto const &pJob = fg_MakeJob();
  int &Result = pJob->m_Result;
  return fg_CallSafe([&Result, pJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// Clean: the borrow in the same closure's captures, after the owner's copy.
TCFuture<int> fg_SameClosure() {
  TCSharedPointer<CJob> pJob = fg_MakeJob();
  return fg_CallSafe([&Result = pJob->m_Result, pJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// Clean: an explicit-object operator given `this` by a pointer to const.
struct CMember {
  TCSharedPointer<CJob> mp_pJob;
  TCFuture<int> f_Log() {
    int &Result = mp_pJob->m_Result;
    g_Log << this;
    return fg_CallSafe([&Result, pJob = mp_pJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
  }
};
// Reported: the moved-from source used again.
TCFuture<int> fg_MovedFromUsed() {
  TCSharedPointer<CJob> pJob = fg_MakeJob();
  auto pKeep = fg_Exchange(pJob, nullptr);
  int &Result = pJob->m_Result;
  return fg_CallSafe([&Result, pKeep]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
}
namespace NReview2Round166Holders {
using namespace NMib::NStorage;
TCFuture<int> fg_Work();
struct CJob { int m_Result = 0; char m_Text[64]; };
DMibReturnsNew TCSharedPointer<CJob> fg_MakeJob();
template <typename t_CData>
struct TCVector {
  TCVector();
  TCVector(TCVector const &_Other);
  TCVector(TCVector &&_Other);
  ~TCVector();
  void f_Clear();
  t_CData &operator[](int _Index) { return mp_pData[_Index]; }
  t_CData const &operator[](int _Index) const { return mp_pData[_Index]; }
  t_CData *begin() { return mp_pData; }
  t_CData *end() { return mp_pData + 1; }
  t_CData *mp_pData;
};
template <typename t_CKey, typename t_CValue>
struct TCMap {
  t_CValue &operator[](t_CKey const &_Key) { return *mp_pValue; }
  void f_Clear();
  t_CValue *mp_pValue;
};
struct CContext { TCSharedPointer<CJob> m_pJob; int m_nRequestID = 0; };
struct CJobBatch { TCSharedPointer<CJob> m_pJobs[2]; };
// 1. Clean: a copy of a vector of shared owners.
TCFuture<int> fg_VectorCopy() {
  TCVector<TCSharedPointer<CJob>> Jobs;
  int &Result = Jobs[0]->m_Result;
  return fg_CallSafe([&Result, Jobs]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// 2. Clean: a move of it.
TCFuture<int> fg_VectorMove() {
  TCVector<TCSharedPointer<CJob>> Jobs;
  int &Result = Jobs[0]->m_Result;
  return fg_CallSafe([&Result, Jobs = fg_Move(Jobs)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// 3. Clean: a copied array.
TCFuture<int> fg_ArrayCopy() {
  TCSharedPointer<CJob> pJobs[2]{fg_MakeJob(), fg_MakeJob()};
  int &Result = pJobs[0]->m_Result;
  return fg_CallSafe([&Result, pJobs]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// 4. Clean: a fixed-array field.
TCFuture<int> fg_Batch() {
  CJobBatch Batch{{fg_MakeJob(), fg_MakeJob()}};
  int &Result = Batch.m_pJobs[0]->m_Result;
  return fg_CallSafe([&Result, Batch]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// 5. Clean: a built-in subscript.
TCFuture<int> fg_Subscript() {
  TCSharedPointer<CJob> pJobs[2]{fg_MakeJob(), fg_MakeJob()};
  int &Result = pJobs[0]->m_Result;
  return fg_CallSafe([&Result, pJob = pJobs[0]]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// 6. Clean: a runtime-indexed reference.
TCFuture<int> fg_Indexed(TCVector<TCSharedPointer<CJob>> const &_Jobs, int _iJob) {
  auto const &pJob = _Jobs[_iJob];
  auto &Result = pJob->m_Result;
  return fg_CallSafe([&Result, pJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// 7. Clean: a keyed reference of an actor member.
struct CJobs : CActor {
  TCMap<int, TCSharedPointer<CJob>> mp_Jobs;
  TCFuture<int> f_Keyed(int _JobID) {
    auto &pJob = mp_Jobs[_JobID];
    int &Result = pJob->m_Result;
    return fg_CallSafe([&Result, pJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
  }
};
// 8. Clean: range-for copies and references.
void fg_RangeCopy(TCVector<TCSharedPointer<CJob>> &_Jobs) {
  for (auto pJob : _Jobs) {
    auto &Result = pJob->m_Result;
    fg_CallSafe([&Result, pJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
  }
}
void fg_RangeReference(TCVector<TCSharedPointer<CJob>> &_Jobs) {
  for (auto &pJob : _Jobs) {
    auto &Result = pJob->m_Result;
    fg_CallSafe([&Result, pJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
  }
}
// 9. Clean: records built in place.
TCFuture<int> fg_InPlace(int _RequestID) {
  auto pJob = fg_MakeJob();
  int &Result = pJob->m_Result;
  return fg_CallSafe([&Result, Context = CContext{pJob, _RequestID}]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_Assigned() {
  auto pJob = fg_MakeJob();
  CContext Context;
  Context = CContext{pJob, 1};
  int &Result = Context.m_pJob->m_Result;
  return fg_CallSafe([&Result, Context]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// 10. Clean: a vector reference bound at a runtime index, the vector copied.
TCFuture<int> fg_IndexedVector(TCVector<TCSharedPointer<CJob>> &_Jobs, int _iJob) {
  auto &pJob = _Jobs[_iJob];
  int &Result = pJob->m_Result;
  return fg_CallSafe([&Result, Jobs = _Jobs]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// Reported: an element changed between.
TCFuture<int> fg_ElementChanged() {
  TCVector<TCSharedPointer<CJob>> Jobs;
  int &Result = Jobs[0]->m_Result;
  Jobs[0] = fg_MakeJob();
  return fg_CallSafe([&Result, Jobs]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
// Reported: the container cleared.
TCFuture<int> fg_Cleared() {
  TCVector<TCSharedPointer<CJob>> Jobs;
  int &Result = Jobs[0]->m_Result;
  Jobs.f_Clear();
  return fg_CallSafe([&Result, Jobs]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
// Reported: _Jobs[i] reassigned.
TCFuture<int> fg_IndexReassigned(TCVector<TCSharedPointer<CJob>> &_Jobs, int _iJob, int _iOther) {
  auto const &pJob = _Jobs[_iJob];
  auto &Result = pJob->m_Result;
  _Jobs[_iOther] = fg_MakeJob();
  return fg_CallSafe([&Result, pJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
TCFuture<int> fg_ConstantReassigned(TCVector<TCSharedPointer<CJob>> &_Jobs, int _iJob) {
  auto const &pJob = _Jobs[_iJob];
  auto &Result = pJob->m_Result;
  _Jobs[1] = fg_MakeJob();
  return fg_CallSafe([&Result, pJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
TCFuture<int> fg_ThroughReference(TCVector<TCSharedPointer<CJob>> &_Jobs, int _iJob) {
  auto &pJob = _Jobs[_iJob];
  auto &Result = _Jobs[0]->m_Result;
  pJob = fg_MakeJob();
  return fg_CallSafe([&Result, Jobs = _Jobs]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
// Reported: a record field reassigned.
TCFuture<int> fg_FieldReassigned() {
  auto pJob = fg_MakeJob();
  CContext Context{pJob, 1};
  int &Result = Context.m_pJob->m_Result;
  Context.m_pJob = fg_MakeJob();
  return fg_CallSafe([&Result, Context]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
// Reported: the moved-from source used again.
TCFuture<int> fg_MovedFrom() {
  TCVector<TCSharedPointer<CJob>> Jobs;
  auto Moved = fg_Move(Jobs);
  int &Result = Jobs[0]->m_Result;
  return fg_CallSafe([&Result, Moved]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
// Reported: a suspension between, of a member container.
struct CSuspendedJobs : CAllowUnsafeThis {
  TCMap<int, TCSharedPointer<CJob>> mp_Jobs;
  TCFuture<int> f_Keyed(int _JobID) {
    auto &pJob = mp_Jobs[_JobID];
    int &Result = pJob->m_Result;
    co_await fg_Work();
    co_return co_await fg_CallSafe([&Result, pJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:38: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
  }
};
// Reported: a copy of a vector of unique-like buffers.
TCFuture<int> fg_BufferElements() {
  TCVector<TCVector<int>> Values;
  int *pValue = Values[0].begin();
  return fg_CallSafe([pValue, Values]() -> TCFuture<int> { co_await fg_Work(); co_return *pValue; });
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a TCFuture coroutine lambda uses 'pValue', which points to 'Values', after a suspension, when 'Values' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
TCFuture<int> fg_BufferElementsMoved() {
  TCVector<TCVector<int>> Values;
  int *pValue = Values[0].begin();
  return fg_CallSafe([pValue, Values = fg_Move(Values)]() -> TCFuture<int> { co_await fg_Work(); co_return *pValue; });
}
}
namespace NReview2Round167 {
using namespace NMib::NStorage;
TCFuture<int> fg_Work();
struct CJob { int m_Result = 0; };
DMibReturnsNew TCSharedPointer<CJob> fg_MakeJob();
template <typename t_CType> void fg_Swap(t_CType &_Left, t_CType &_Right);
template <typename t_CData>
struct TCVector {
  TCVector(); TCVector(TCVector const &); TCVector(TCVector &&); ~TCVector();
  t_CData &operator[](int _Index) { return mp_pData[_Index]; }
  t_CData &f_GetFirst() { return (*this)[0]; }
  t_CData *mp_pData;
};
template <typename t_CKey, typename t_CValue>
struct TCMap {
  t_CValue &operator[](t_CKey const &_Key) { return *mp_pValue; }
  t_CValue *f_FindEqual(t_CKey const &_Key) { return mp_pValue; }
  t_CValue *mp_pValue;
};
struct CContext { CContext(TCSharedPointer<CJob> const &_pJob, int _ID) : m_pJob(_pJob), m_ID(_ID) {} TCSharedPointer<CJob> m_pJob; int m_ID; };
struct CMoveContext { CMoveContext(TCSharedPointer<CJob> &&_pJob) : m_pJob(fg_Move(_pJob)) {} TCSharedPointer<CJob> m_pJob; };
struct CNoStore { CNoStore(TCSharedPointer<CJob> const &_pJob) {} TCSharedPointer<CJob> m_pJob; };
struct CContextBase { TCSharedPointer<CJob> m_pJob; };
struct CDerived : CContextBase { int m_ID; };
struct CJobs { TCSharedPointer<CJob> m_pJob; TCSharedPointer<CJob> const &f_Job() const { return m_pJob; } };
// 1. Clean: a constructor that copies the owner.
TCFuture<int> fg_Constructed() {
  auto pJob = fg_MakeJob();
  int &Result = pJob->m_Result;
  return fg_CallSafe([&Result, Context = CContext(pJob, 1)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_ConstructedLocal() {
  auto pJob = fg_MakeJob();
  int &Result = pJob->m_Result;
  CContext Context(pJob, 1);
  return fg_CallSafe([&Result, Context]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_ConstructedMove() {
  auto pJob = fg_MakeJob();
  int &Result = pJob->m_Result;
  return fg_CallSafe([&Result, Context = CMoveContext(fg_Move(pJob))]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// 2. Clean: a base.
TCFuture<int> fg_Base() {
  auto pJob = fg_MakeJob();
  int &Result = pJob->m_Result;
  return fg_CallSafe([&Result, Context = CDerived{{pJob}, 1}]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_BaseLocal() {
  CDerived Context{{fg_MakeJob()}, 1};
  int &Result = Context.m_pJob->m_Result;
  return fg_CallSafe([&Result, Context]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// 3. Clean: a const reference cast.
TCFuture<int> fg_Cast() {
  auto pJob = fg_MakeJob();
  int &Result = pJob->m_Result;
  return fg_CallSafe([&Result, pKeep = static_cast<TCSharedPointer<CJob> const &>(pJob)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_CastAlias() {
  auto pJob = fg_MakeJob();
  int &Result = pJob->m_Result;
  auto const &pKeep = static_cast<TCSharedPointer<CJob> const &>(pJob);
  return fg_CallSafe([&Result, pKeep]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// 4. Clean: accessors.
TCFuture<int> fg_First() {
  TCVector<TCSharedPointer<CJob>> Jobs;
  int &Result = Jobs.f_GetFirst()->m_Result;
  return fg_CallSafe([&Result, Jobs]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_Found(TCMap<int, TCSharedPointer<CJob>> &_Jobs, int _Key) {
  auto const &pJob = *_Jobs.f_FindEqual(_Key);
  int &Result = pJob->m_Result;
  return fg_CallSafe([&Result, pJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// 5. Clean: a getter's owner, the holder captured.
TCFuture<int> fg_Getter() {
  CJobs Jobs{fg_MakeJob()};
  int &Result = Jobs.f_Job()->m_Result;
  return fg_CallSafe([&Result, Jobs]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// 6. Clean: a swap into the captured owner.
TCFuture<int> fg_Swapped() {
  auto pJob = fg_MakeJob();
  int &Result = pJob->m_Result;
  TCSharedPointer<CJob> pKeep;
  fg_Swap(pKeep, pJob);
  return fg_CallSafe([&Result, pKeep]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// 7. Clean: mutually recursive containers.
struct CChildNode;
struct CNode { TCMap<int, CChildNode> *m_pUnused; TCSharedPointer<CJob> m_pJob; };
// Reported: a constructor that doesn't store the owner.
TCFuture<int> fg_NoStore() {
  auto pJob = fg_MakeJob();
  int &Result = pJob->m_Result;
  return fg_CallSafe([&Result, Context = CNoStore(pJob)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
// Reported: a base member reset between.
TCFuture<int> fg_BaseReset() {
  CDerived Context{{fg_MakeJob()}, 1};
  int &Result = Context.m_pJob->m_Result;
  Context.m_pJob = {};
  return fg_CallSafe([&Result, Context]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
// Reported: a swap that moves the owner away from the captured slot.
TCFuture<int> fg_SwappedAway() {
  auto pJob = fg_MakeJob();
  int &Result = pJob->m_Result;
  TCSharedPointer<CJob> pOther;
  fg_Swap(pOther, pJob);
  return fg_CallSafe([&Result, pJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
}
namespace NReview2Round167RealShape {
// A shared owner whose copies the user provides, like Malterlib's.
template <typename t_CType>
struct TCSharedPointer {
  TCSharedPointer();
  TCSharedPointer(TCSharedPointer const &_pOther);
  TCSharedPointer(TCSharedPointer &&_pOther);
  TCSharedPointer &operator=(TCSharedPointer const &_pOther);
  TCSharedPointer &operator=(TCSharedPointer &&_pOther);
  ~TCSharedPointer();
  t_CType *operator->() const;
};
struct CPage { int m_Value = 0; };
struct CValueResult { TCSharedPointer<CPage> m_pPage; int const *m_pValue = nullptr; };
TCFuture<int> fg_Work();
DMibReturnsNew TCSharedPointer<CPage> fg_ReadPage();
// Clean: the owner member assigned, then a pointer into it.
TCFuture<int> fg_Assigned() {
  auto pPage = fg_ReadPage();
  CValueResult Result;
  Result.m_pPage = pPage;
  Result.m_pValue = &pPage->m_Value;
  return fg_CallSafe([Result]() -> TCFuture<int> { co_await fg_Work(); co_return *Result.m_pValue; });
}
// Reported: another page's owner.
TCFuture<int> fg_AssignedOther() {
  auto pPage = fg_ReadPage();
  CValueResult Result;
  Result.m_pPage = fg_ReadPage();
  Result.m_pValue = &pPage->m_Value;
  return fg_CallSafe([Result]() -> TCFuture<int> { co_await fg_Work(); co_return *Result.m_pValue; });
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a TCFuture coroutine lambda uses 'Result', which points to 'pPage', after a suspension, when 'pPage' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
}
namespace NReview2Round168Hang {
using namespace NMib::NStorage;
TCFuture<int> fg_Work();
struct CJob { int m_Result = 0; };
struct CContext {
  TCSharedPointer<CJob> m_pJob;
  TCSharedPointer<CJob> &f_Get() { return f_Get(); }
  static CContext fs_Make(TCSharedPointer<CJob> const &_pJob) { return CContext{_pJob}; }
};
// A getter that gives itself.
TCFuture<int> fg_Recursive(CContext &_Context) {
  int &Result = _Context.f_Get()->m_Result;
  return fg_CallSafe([&Result, pKeep = _Context.f_Get()]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// Clean: f_Get() returns f_Get(), which never returns, so the capture is never made.
}
// A static factory's copy, which isn't a member copy.
TCFuture<int> fg_Factory(TCSharedPointer<CJob> _pJob) {
  CContext Context = CContext::fs_Make(_pJob);
  int &Result = Context.m_pJob->m_Result;
  return fg_CallSafe([&Result, Context]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
}
namespace NReview2Round168Capture {
using namespace NMib::NStorage;
TCFuture<int> fg_Work();
struct CJob { int m_Result = 0; };
struct CJobBase { int m_Result = 0; };
struct CDerivedJob : CJobBase {};
DMibReturnsNew TCSharedPointer<CJob> fg_MakeJob();
TCSharedPointer<CDerivedJob> fg_MakeDerivedJob();
template <typename t_CType> void fg_Swap(t_CType &_Left, t_CType &_Right);
template <typename t_CType> t_CType const &fg_Const(t_CType const &_Value) { return _Value; }
template <typename t_CData>
struct TCVector {
  TCVector(); TCVector(TCVector const &); TCVector(TCVector &&); ~TCVector();
  t_CData &operator[](int _Index) { return mp_pData[_Index]; }
  t_CData &f_GetLast() { return (*this)[mp_nLen - 1]; }
  t_CData *mp_pData;
  int mp_nLen;
};
template <typename t_CKey, typename t_CValue>
struct TCMap {
  TCMap(); TCMap(TCMap const &); TCMap(TCMap &&); TCMap &operator=(TCMap &&); ~TCMap();
  t_CValue *f_FindEqual(t_CKey const &_Key) { return mp_pValue; }
  t_CValue *mp_pValue;
};
struct CContext { CContext(TCSharedPointer<CJob> const &_pJob, int _ID) : m_pJob(_pJob), m_ID(_ID) {} TCSharedPointer<CJob> m_pJob; int m_ID; };
struct CAggContext { TCSharedPointer<CJob> m_pJob; };
// 1. Constructors.
struct CKeepBody { CKeepBody(TCSharedPointer<CJob> const &_pJob) { m_pJob = _pJob; } TCSharedPointer<CJob> m_pJob; };
struct CKeepConvert { CKeepConvert(TCSharedPointer<CDerivedJob> const &_pJob) : m_pJob(_pJob) {} TCSharedPointer<CJobBase> m_pJob; };
struct CKeepFromContext { CKeepFromContext(CContext const &_Context) : m_pJob(_Context.m_pJob) {} TCSharedPointer<CJob> m_pJob; };
struct CDelegating {
  CDelegating(TCSharedPointer<CJob> const &_pJob) : CDelegating(_pJob, 0) {}
  CDelegating(TCSharedPointer<CJob> const &_pJob, int _ID) : m_pJob(_pJob), m_ID(_ID) {}
  TCSharedPointer<CJob> m_pJob; int m_ID;
};
struct CKeepBase { CKeepBase(TCSharedPointer<CJob> const &_pJob) : m_pJob(_pJob) {} TCSharedPointer<CJob> m_pJob; };
struct CKeepDerived : CKeepBase { CKeepDerived(TCSharedPointer<CJob> const &_pJob) : CKeepBase(_pJob) {} };
struct CAggDerived : CAggContext { CAggDerived(TCSharedPointer<CJob> const &_pJob) : CAggContext{_pJob} {} };
struct CJobContext { TCSharedPointer<CJob> m_pJob; };
CJobContext fg_MakeContext(TCSharedPointer<CJob> const &_pJob) { return CJobContext{_pJob}; }
struct CKeepCond { CKeepCond(TCSharedPointer<CJob> const &_pJob, bool _bKeep) { if (_bKeep) m_pJob = _pJob; } TCSharedPointer<CJob> m_pJob; };
struct CKeepCleared { CKeepCleared(TCSharedPointer<CJob> const &_pJob) { m_pJob = _pJob; m_pJob.f_Clear(); } TCSharedPointer<CJob> m_pJob; };
struct CNoStoreBase { CNoStoreBase(TCSharedPointer<CJob> const &_pJob) {} TCSharedPointer<CJob> m_pJob; };
struct CNoStoreDerived : CNoStoreBase { CNoStoreDerived(TCSharedPointer<CJob> const &_pJob) : CNoStoreBase(_pJob) {} };
TCFuture<int> fg_Body() {
  auto pJob = fg_MakeJob();
  int &Result = pJob->m_Result;
  return fg_CallSafe([&Result, Keep = CKeepBody(pJob)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_Convert() {
  auto pJob = fg_MakeDerivedJob();
  int &Result = pJob->m_Result;
  return fg_CallSafe([&Result, Keep = CKeepConvert(pJob)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_FromContext() {
  CContext Context(fg_MakeJob(), 1);
  int &Result = Context.m_pJob->m_Result;
  return fg_CallSafe([&Result, Keep = CKeepFromContext{Context}]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_Delegating() {
  auto pJob = fg_MakeJob();
  int &Result = pJob->m_Result;
  return fg_CallSafe([&Result, Keep = CDelegating(pJob)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_BaseConstructor() {
  auto pJob = fg_MakeJob();
  int &Result = pJob->m_Result;
  return fg_CallSafe([&Result, Keep = CKeepDerived(pJob)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_BaseConstructorLocal() {
  auto pJob = fg_MakeJob();
  CKeepDerived Context(pJob);
  int &Result = pJob->m_Result;
  return fg_CallSafe([&Result, Context]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_AggregateBase() {
  auto pJob = fg_MakeJob();
  int &Result = pJob->m_Result;
  return fg_CallSafe([&Result, Keep = CAggDerived{pJob}]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_Factory() {
  auto pJob = fg_MakeJob();
  int &Result = pJob->m_Result;
  return fg_CallSafe([&Result, Context = fg_MakeContext(pJob)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// Reported: conditionally assigned, cleared after, a base that doesn't store.
TCFuture<int> fg_Conditional(bool _bKeep) {
  auto pJob = fg_MakeJob();
  int &Result = pJob->m_Result;
  return fg_CallSafe([&Result, Keep = CKeepCond(pJob, _bKeep)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
TCFuture<int> fg_Cleared() {
  auto pJob = fg_MakeJob();
  int &Result = pJob->m_Result;
  return fg_CallSafe([&Result, Keep = CKeepCleared(pJob)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
TCFuture<int> fg_NoStoreBase() {
  auto pJob = fg_MakeJob();
  int &Result = pJob->m_Result;
  return fg_CallSafe([&Result, Keep = CNoStoreDerived(pJob)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
// 2. Swaps and exchanges of records.
TCFuture<int> fg_SwapRecords() {
  auto pJob = fg_MakeJob();
  CAggContext Context{pJob}, Other;
  int &Result = pJob->m_Result;
  fg_Swap(Context, Other);
  return fg_CallSafe([&Result, Other]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_ExchangeRecord(CAggContext &_Context) {
  int &Value = _Context.m_pJob->m_Result;
  return fg_CallSafe([&Value, Context = fg_Exchange(_Context, CAggContext{})]() -> TCFuture<int> { co_await fg_Work(); co_return Value; });
}
// Reported: swapped, then reset; swapped away.
TCFuture<int> fg_SwapReset() {
  auto pJob = fg_MakeJob();
  CAggContext Context{pJob}, Other;
  int &Result = pJob->m_Result;
  fg_Swap(Context, Other);
  Other.m_pJob.f_Clear();
  return fg_CallSafe([&Result, Other]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
TCFuture<int> fg_SwapAway() {
  auto pJob = fg_MakeJob();
  CAggContext Context{pJob}, Other;
  int &Result = Context.m_pJob->m_Result;
  fg_Swap(Context, Other);
  return fg_CallSafe([&Result, Context]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
// 3. Forms.
TCFuture<int> fg_Array() {
  auto pFirst = fg_MakeJob();
  auto pSecond = fg_MakeJob();
  int &Result = pFirst->m_Result;
  TCSharedPointer<CJob> pJobs[2]{pFirst, pSecond};
  return fg_CallSafe([&Result, pJobs]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_PointerReceiver(TCMap<int, TCSharedPointer<CJob>> *_pJobs, int _Key) {
  auto &pJob = *_pJobs->f_FindEqual(_Key);
  int &Result = pJob->m_Result;
  return fg_CallSafe([&Result, pJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_Wrapper(TCMap<int, TCSharedPointer<CJob>> &_Jobs, int _Key) {
  auto &pJob = fg_Const(*_Jobs.f_FindEqual(_Key));
  int &Result = pJob->m_Result;
  return fg_CallSafe([&Result, pJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_CastWrapper(TCMap<int, TCSharedPointer<CJob>> &_Jobs, int _Key) {
  auto &pJob = static_cast<TCSharedPointer<CJob> const &>(*_Jobs.f_FindEqual(_Key));
  int &Result = pJob->m_Result;
  return fg_CallSafe([&Result, pJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_LookupPointer(int _Key) {
  TCMap<int, TCSharedPointer<CJob>> Jobs;
  auto *ppJob = Jobs.f_FindEqual(_Key);
  int &Result = (*ppJob)->m_Result;
  return fg_CallSafe([&Result, Jobs]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_Last(TCVector<TCSharedPointer<CJob>> &_Jobs) {
  int &Value = _Jobs.f_GetLast()->m_Result;
  return fg_CallSafe([&Value, Jobs = fg_Move(_Jobs)]() -> TCFuture<int> { co_await fg_Work(); co_return Value; });
}
// Reported: the container changed after the lookup; the pointer pointed elsewhere.
TCFuture<int> fg_LookupChanged(int _Key) {
  TCMap<int, TCSharedPointer<CJob>> Jobs;
  auto *ppJob = Jobs.f_FindEqual(_Key);
  int &Result = (*ppJob)->m_Result;
  Jobs = TCMap<int, TCSharedPointer<CJob>>();
  return fg_CallSafe([&Result, Jobs]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
TCFuture<int> fg_LookupRepointed(int _Key, int _Other) {
  TCMap<int, TCSharedPointer<CJob>> Jobs;
  auto *ppJob = Jobs.f_FindEqual(_Key);
  int &Result = (*ppJob)->m_Result;
  ppJob = Jobs.f_FindEqual(_Other);
  return fg_CallSafe([&Result, pKeep = *ppJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
}
namespace NReview2Round168ExplicitShare {
template <typename t_CType>
struct TCSharedPointer {
  TCSharedPointer(); TCSharedPointer(TCSharedPointer const &); ~TCSharedPointer();
  t_CType *operator->() const;
  TCSharedPointer<t_CType const> f_ShareAsConst(this TCSharedPointer const &_Self);
};
struct CJob { int m_Result = 0; };
DMibReturnsNew TCSharedPointer<CJob> fg_MakeJob();
TCFuture<int> fg_Work();
TCFuture<int> fg_ShareExplicit() {
  auto pJob = fg_MakeJob();
  int &Result = pJob->m_Result;
  return fg_CallSafe([&Result, pKeep = pJob.f_ShareAsConst()]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
}
namespace NReview2Round169Capture {
using namespace NMib::NStorage;
TCFuture<int> fg_Work();
struct CJob { int m_Result = 0; };
DMibReturnsNew TCSharedPointer<CJob> fg_MakeJob();
void fg_PrepareJob(CJob &_Job);
[[noreturn]] void fg_Fail();
// Clean: a constructor-local copy, also moved; an inherited constructor; a
// throwing path; a named-return factory; a helper giving the owner back.
struct CLocalCopy { CLocalCopy(TCSharedPointer<CJob> const &_pJob) { auto pKeep = _pJob; m_pJob = pKeep; } TCSharedPointer<CJob> m_pJob; };
struct CLocalMoved { CLocalMoved(TCSharedPointer<CJob> const &_pJob) { auto pPrepared = _pJob; fg_PrepareJob(*pPrepared); m_pJob = fg_Move(pPrepared); } TCSharedPointer<CJob> m_pJob; };
struct CKeepBase { CKeepBase(TCSharedPointer<CJob> const &_pJob) : m_pJob(_pJob) {} TCSharedPointer<CJob> m_pJob; };
struct CInherited : CKeepBase { using CKeepBase::CKeepBase; };
struct CThrowing { CThrowing(TCSharedPointer<CJob> const &_pJob, bool _bFail) { if (_bFail) throw 1; m_pJob = _pJob; } TCSharedPointer<CJob> m_pJob; };
struct CFailing { CFailing(TCSharedPointer<CJob> const &_pJob, bool _bFail) { if (_bFail) fg_Fail(); m_pJob = _pJob; } TCSharedPointer<CJob> m_pJob; };
struct CContext { TCSharedPointer<CJob> m_pJob; };
CContext fg_MakeNamed(TCSharedPointer<CJob> const &_pJob) { CContext Context{_pJob}; return Context; }
TCSharedPointer<CJob> fg_KeepJob(TCSharedPointer<CJob> const &_pJob) { return _pJob; }
TCFuture<int> fg_LocalCopy() {
  auto pJob = fg_MakeJob();
  int &Result = pJob->m_Result;
  return fg_CallSafe([&Result, Keep = CLocalCopy(pJob)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_LocalMoved() {
  auto pJob = fg_MakeJob();
  int &Result = pJob->m_Result;
  return fg_CallSafe([&Result, Keep = CLocalMoved(pJob)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_Inherited() {
  auto pJob = fg_MakeJob();
  int &Result = pJob->m_Result;
  return fg_CallSafe([&Result, Keep = CInherited(pJob)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_Throwing(bool _bFail) {
  auto pJob = fg_MakeJob();
  int &Result = pJob->m_Result;
  return fg_CallSafe([&Result, Keep = CThrowing(pJob, _bFail)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_Failing(bool _bFail) {
  auto pJob = fg_MakeJob();
  int &Result = pJob->m_Result;
  return fg_CallSafe([&Result, Keep = CFailing(pJob, _bFail)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_NamedFactory() {
  auto pJob = fg_MakeJob();
  int &Result = pJob->m_Result;
  return fg_CallSafe([&Result, Context = fg_MakeNamed(pJob)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_Helper() {
  auto pJob = fg_MakeJob();
  int &Result = pJob->m_Result;
  return fg_CallSafe([&Result, pKeep = fg_KeepJob(pJob)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// Reported: a throwing path that doesn't store, a local copy reset before it
// is stored, an inherited constructor of a base that doesn't store.
struct CThrowNoStore { CThrowNoStore(TCSharedPointer<CJob> const &_pJob, bool _bFail) { if (_bFail) throw 1; } TCSharedPointer<CJob> m_pJob; };
struct CLocalReset { CLocalReset(TCSharedPointer<CJob> const &_pJob) { auto pKeep = _pJob; pKeep.f_Clear(); m_pJob = pKeep; } TCSharedPointer<CJob> m_pJob; };
struct CNoStoreBase { CNoStoreBase(TCSharedPointer<CJob> const &_pJob) {} TCSharedPointer<CJob> m_pJob; };
struct CInheritedNoStore : CNoStoreBase { using CNoStoreBase::CNoStoreBase; };
TCFuture<int> fg_ThrowNoStore(bool _bFail) {
  auto pJob = fg_MakeJob();
  int &Result = pJob->m_Result;
  return fg_CallSafe([&Result, Keep = CThrowNoStore(pJob, _bFail)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
TCFuture<int> fg_LocalReset() {
  auto pJob = fg_MakeJob();
  int &Result = pJob->m_Result;
  return fg_CallSafe([&Result, Keep = CLocalReset(pJob)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
TCFuture<int> fg_InheritedNoStore() {
  auto pJob = fg_MakeJob();
  int &Result = pJob->m_Result;
  return fg_CallSafe([&Result, Keep = CInheritedNoStore(pJob)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
}
namespace NReview2Round169Explicit {
template <typename t_CType>
struct TCSharedPointer {
  TCSharedPointer();
  TCSharedPointer(TCSharedPointer const &_Other);
  TCSharedPointer &operator=(this TCSharedPointer &_This, TCSharedPointer const &_Other);
  ~TCSharedPointer();
  t_CType *operator->() const;
};
struct CJob { int m_Result = 0; };
DMibReturnsNew TCSharedPointer<CJob> fg_MakeJob();
TCFuture<int> fg_Work();
template <typename t_CData>
struct TCVector {
  TCVector(); TCVector(TCVector const &); TCVector(TCVector &&); ~TCVector();
  t_CData &f_First(this TCVector &_This) { return _This.mp_First; }
  t_CData mp_First;
};
// Clean: assigned through an explicit-object assignment; an explicit-object getter of a holder captured whole.
TCFuture<int> fg_ExplicitAssign() {
  auto pJob = fg_MakeJob();
  TCSharedPointer<CJob> pKeep;
  pKeep = pJob;
  int &Result = pJob->m_Result;
  return fg_CallSafe([&Result, pKeep]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_ExplicitGetter(TCVector<TCSharedPointer<CJob>> &_Jobs) {
  int &Result = _Jobs.f_First()->m_Result;
  return fg_CallSafe([&Result, Jobs = fg_Move(_Jobs)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
}
// Round 169, rest: a constructor keeping the owner in each element of an
// array member, sharing through a pointer to the owner, also a local pointer
// to an element, a tuple's element and a free getter given what holds the
// owner by a reference that isn't const that doesn't change it, also called
// between the reference and the capture, and a reference parameter kept by a
// shared owner made of its address keep it. Reported: an array member made
// of other owners, a getter that writes the owner called between, an owner
// made of another parameter's address, and the pointer pointed elsewhere
// between.
namespace std {
template <typename... tp_CTypes> struct tuple;
template <typename t_CType> struct tuple<t_CType> { t_CType m_First; };
template <decltype(sizeof(0)) t_Index, typename t_CType> t_CType &get(tuple<t_CType> &_Tuple) { return _Tuple.m_First; }
}
namespace NReview2Round169Rest {
using namespace NMib::NStorage;
TCFuture<int> fg_Work();
struct CJob { int m_Result = 0; };
DMibReturnsNew TCSharedPointer<CJob> fg_MakeJob();
template <typename t_CKey, typename t_CValue>
struct TCMap {
  t_CValue *f_FindEqual(t_CKey const &_Key) { return mp_pValue; }
  t_CValue *mp_pValue;
};
template <typename... tp_CTypes> using TCTuple = std::tuple<tp_CTypes...>;
// Array members made by a constructor.
struct CJobPair { CJobPair(TCSharedPointer<CJob> const &_pJob) : m_pJobs{_pJob, _pJob} {} TCSharedPointer<CJob> m_pJobs[2]; };
struct CJobBatch { CJobBatch(TCSharedPointer<CJob> const &_pFirst, TCSharedPointer<CJob> const &_pSecond) : m_pJobs{_pFirst, _pSecond} {} TCSharedPointer<CJob> m_pJobs[2]; };
TCFuture<int> fg_Pair() {
  auto pJob = fg_MakeJob();
  int &Result = pJob->m_Result;
  return fg_CallSafe([&Result, Context = CJobPair(pJob)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_Batch() {
  auto pJob = fg_MakeJob();
  auto pOther = fg_MakeJob();
  int &Result = pJob->m_Result;
  return fg_CallSafe([&Result, Batch = CJobBatch(pJob, pOther)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// Sharing through a pointer receiver.
TCFuture<int> fg_ShareArrow(int _Key) {
  TCMap<int, TCSharedPointer<CJob>> Jobs;
  auto *ppJob = Jobs.f_FindEqual(_Key);
  int &Result = (*ppJob)->m_Result;
  return fg_CallSafe([&Result, pKeep = ppJob->f_ShareAsConst()]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// Getters given a reference that isn't const that don't write.
struct CContext { TCSharedPointer<CJob> m_pJob; };
TCSharedPointer<CJob> &fg_GetJob(CContext &_Context) { return _Context.m_pJob; }
TCFuture<int> fg_Tuple() {
  TCTuple<TCSharedPointer<CJob>> Jobs{fg_MakeJob()};
  int &Result = std::get<0>(Jobs)->m_Result;
  return fg_CallSafe([&Result, Jobs]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_FreeGetter() {
  CContext Context{fg_MakeJob()};
  auto &pJob = fg_GetJob(Context);
  int &Result = pJob->m_Result;
  return fg_CallSafe([&Result, Context]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_GetterBetween() {
  CContext Context{fg_MakeJob()};
  int &Result = Context.m_pJob->m_Result;
  fg_GetJob(Context);
  return fg_CallSafe([&Result, Context]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// A reference parameter kept by an owner made of its address.
TCFuture<int> fg_KeptReference(CJob &_Job) {
  auto pKeep = TCSharedPointer<CJob>{fg_Explicit(&_Job)};
  return fg_CallSafe([&_Job, pKeep]() -> TCFuture<int> { co_await fg_Work(); co_return _Job.m_Result; });
}
// Reported controls.
struct CJobPairOther { CJobPairOther(TCSharedPointer<CJob> const &_pJob) : m_pJobs{fg_MakeJob(), fg_MakeJob()} {} TCSharedPointer<CJob> m_pJobs[2]; };
TCFuture<int> fg_PairOther() {
  auto pJob = fg_MakeJob();
  int &Result = pJob->m_Result;
  return fg_CallSafe([&Result, Context = CJobPairOther(pJob)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
TCSharedPointer<CJob> &fg_ResetJob(CContext &_Context) { _Context.m_pJob = fg_MakeJob(); return _Context.m_pJob; }
TCFuture<int> fg_WritingGetterBetween() {
  CContext Context{fg_MakeJob()};
  int &Result = Context.m_pJob->m_Result;
  fg_ResetJob(Context);
  return fg_CallSafe([&Result, Context]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
// The parameter the keeper is made of states that it is no object another
// parameter is (DMibDistinct): reported.
TCFuture<int> fg_UnrelatedAddress(CJob &_Job, DMibDistinct CJob &_Other) {
  auto pKeep = TCSharedPointer<CJob>{fg_Explicit(&_Other)};
  return fg_CallSafe([&_Job, pKeep]() -> TCFuture<int> { co_await fg_Work(); co_return _Job.m_Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses '_Job', which it captures by reference, after a suspension, when '_Job' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
// Clean: unstated, the two parameters may be the same object.
TCFuture<int> fg_UnrelatedAddressUnstated(CJob &_Job, CJob &_Other) {
  auto pKeep = TCSharedPointer<CJob>{fg_Explicit(&_Other)};
  return fg_CallSafe([&_Job, pKeep]() -> TCFuture<int> { co_await fg_Work(); co_return _Job.m_Result; });
}
TCFuture<int> fg_PointerRepointed(int _Key, int _Other) {
  TCMap<int, TCSharedPointer<CJob>> Jobs;
  auto *ppJob = Jobs.f_FindEqual(_Key);
  int &Result = (*ppJob)->m_Result;
  ppJob = Jobs.f_FindEqual(_Other);
  return fg_CallSafe([&Result, pKeep = ppJob->f_ShareAsConst()]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
}
// Round 169, rest: a copy of a CStr, whose copies share the buffer
// (`sharedbuffer`), keeps it, also taken before the reference and with the
// source changed after it, and so does a move. Reported: the string changed
// between the reference and the capture, or between the copy and the
// reference, and a string made of the text, which copies it.
namespace NReview2Round169Str {
TCFuture<int> fg_Work();
struct CStrTraits_CStr {};
template <typename t_CTraits>
struct TCStr {
  TCStr();
  TCStr(char const *_pText);
  TCStr(TCStr const &_Other);
  TCStr(TCStr &&_Other);
  ~TCStr();
  TCStr &operator=(TCStr const &_Other);
  char const *f_GetStr() const { return mp_pStr; }
  void f_AddStr(char const *_pText);
  int f_GetLen() const;
  char const *mp_pStr;
};
using CStr = TCStr<CStrTraits_CStr>;
CStr fg_ReadText();
// A copy of a CStr shares its buffer.
TCFuture<int> fg_Copied() {
  CStr Text = fg_ReadText();
  char const &First = *Text.f_GetStr();
  return fg_CallSafe([&First, Text]() -> TCFuture<int> { co_await fg_Work(); co_return First; });
}
TCFuture<int> fg_CopiedBefore() {
  CStr Text = fg_ReadText();
  CStr Copy = Text;
  char const *pFirst = Text.f_GetStr();
  return fg_CallSafe([pFirst, Copy]() -> TCFuture<int> { co_await fg_Work(); co_return *pFirst; });
}
TCFuture<int> fg_CopiedThenChanged() {
  CStr Text = fg_ReadText();
  char const *pFirst = Text.f_GetStr();
  CStr Copy = Text;
  Text.f_AddStr("x");
  return fg_CallSafe([pFirst, Copy]() -> TCFuture<int> { co_await fg_Work(); co_return *pFirst; });
}
TCFuture<int> fg_Moved() {
  CStr Text = fg_ReadText();
  char const *pFirst = Text.f_GetStr();
  return fg_CallSafe([pFirst, Text = fg_Move(Text)]() -> TCFuture<int> { co_await fg_Work(); co_return *pFirst; });
}
// Reported: changed between the borrow and the capture; changed between the
// copy and the borrow; a CStr made of other text.
TCFuture<int> fg_ChangedBetween() {
  CStr Text = fg_ReadText();
  char const &First = *Text.f_GetStr();
  Text.f_AddStr("x");
  return fg_CallSafe([&First, Text]() -> TCFuture<int> { co_await fg_Work(); co_return First; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'First', which it captures by reference, after a suspension, when 'First' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
TCFuture<int> fg_ChangedBeforeBorrow() {
  CStr Text = fg_ReadText();
  CStr Copy = Text;
  Text.f_AddStr("x");
  char const *pFirst = Text.f_GetStr();
  return fg_CallSafe([pFirst, Copy]() -> TCFuture<int> { co_await fg_Work(); co_return *pFirst; });
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a TCFuture coroutine lambda uses 'pFirst', which points to 'Text', after a suspension, when 'Text' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
TCFuture<int> fg_MadeOfText() {
  CStr Text = fg_ReadText();
  char const *pFirst = Text.f_GetStr();
  CStr Other(Text.f_GetStr());
  return fg_CallSafe([pFirst, Other]() -> TCFuture<int> { co_await fg_Work(); co_return *pFirst; });
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a TCFuture coroutine lambda uses 'pFirst', which points to 'Text', after a suspension, when 'Text' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
}
// Round 170: a holder's address given to a function that only reads
// through it, a getter that checks before it gives the owner back, a member
// function that changes another member than the array holding the owner, an
// implicit move of a parameter taken by `&&` on return, and an address taken
// only in what `if constexpr (false)` discards keep it. Reported: a function
// that clears the owner through the address, and a checked getter that
// resets it, between the reference and the capture.
namespace NReview2Round170Capture {
using namespace NMib::NStorage;
TCFuture<int> fg_Work();
struct CJob { int m_Result = 0; };
DMibReturnsNew TCSharedPointer<CJob> fg_MakeJob();
template <typename t_CType>
struct TCUniquePointer {
  t_CType *m_pPointTo;
  explicit TCUniquePointer(t_CType *_pPointTo) : m_pPointTo(_pPointTo) {}
  TCUniquePointer(TCUniquePointer &&_Other) : m_pPointTo(_Other.m_pPointTo) { _Other.m_pPointTo = nullptr; }
  ~TCUniquePointer() { delete m_pPointTo; }
  t_CType *operator->() const { return m_pPointTo; }
};
struct CJobContext { TCSharedPointer<CJob> m_pJob; int m_RequestID = 0; };
int fg_GetJobID(CJobContext const *_pContext) { return _pContext->m_RequestID; }
void fg_ResetThrough(CJobContext *_pContext) { _pContext->m_pJob = {}; }
void fg_Inspect(TCSharedPointer<CJob> *_pJob);
struct CBatch { TCSharedPointer<CJob> m_pJobs[2]; int m_nAccess = 0; void f_CountAccess() { ++m_nAccess; } };
struct CChecked {
  TCSharedPointer<CJob> m_pJob;
  TCSharedPointer<CJob> const &f_Job() const { if (m_pJob == nullptr) throw 1; return m_pJob; }
  TCSharedPointer<CJob> &f_Reset() { if (m_pJob == nullptr) throw 1; m_pJob = {}; return m_pJob; }
};
TCUniquePointer<CJob> fg_Take(TCUniquePointer<CJob> &&_pJob) { return _pJob; }
// Read-only address arguments.
TCFuture<int> fg_ReadOnlyAddress() {
  CJobContext Context{fg_MakeJob(), 1};
  int &Result = Context.m_pJob->m_Result;
  fg_GetJobID(&Context);
  return fg_CallSafe([&Result, Context]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// Checked getter.
TCFuture<int> fg_CheckedGetter() {
  CChecked Context{fg_MakeJob()};
  int &Result = Context.f_Job()->m_Result;
  return fg_CallSafe([&Result, Context]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// Array-held owner and a method that writes another member.
TCFuture<int> fg_ArrayCounted() {
  CBatch Batch{{fg_MakeJob(), fg_MakeJob()}};
  int &Result = Batch.m_pJobs[0]->m_Result;
  Batch.f_CountAccess();
  return fg_CallSafe([&Result, Batch]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// Implicit move on return.
TCFuture<int> fg_ImplicitMove() {
  TCUniquePointer<CJob> pJob(new CJob);
  int &Result = pJob->m_Result;
  auto pKeep = fg_Take(fg_Move(pJob));
  return fg_CallSafe([&Result, pKeep = fg_Move(pKeep)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
struct CActorState {
  TCSharedPointer<CJob> mp_pJob;
  TCFuture<int> f_Discarded() {
    if constexpr (false) fg_Inspect(&mp_pJob);
    int &Result = mp_pJob->m_Result;
    return fg_CallSafe([&Result, pJob = mp_pJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
  }
};
// Reported: a helper writing through the pointer, a getter resetting.
TCFuture<int> fg_WritesThrough() {
  CJobContext Context{fg_MakeJob(), 1};
  int &Result = Context.m_pJob->m_Result;
  fg_ResetThrough(&Context);
  return fg_CallSafe([&Result, Context]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
TCFuture<int> fg_ResetGetter() {
  CChecked Context{fg_MakeJob()};
  int &Result = Context.m_pJob->m_Result;
  Context.f_Reset();
  return fg_CallSafe([&Result, Context]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
}
// Round 170, rest: helpers giving the owner a member of what they are given
// is, by reference through a pointer parameter and by value through a
// reference or a pointer, and a setter a constructor calls, keep it.
// Reported: a helper that clears the owner through the pointer before it
// gives it back, by reference or by value, and a setter that keeps it on
// one branch only.
namespace NReview2Round170Rest {
using namespace NMib::NStorage;
TCFuture<int> fg_Work();
struct CJob { int m_Result = 0; };
DMibReturnsNew TCSharedPointer<CJob> fg_MakeJob();
struct CContext { TCSharedPointer<CJob> m_pJob; int m_RequestID = 0; };
TCSharedPointer<CJob> const &fg_GetJobPointer(CContext const *_pContext) { return _pContext->m_pJob; }
TCSharedPointer<CJob> fg_GetJobValue(CContext const &_Context) { return _Context.m_pJob; }
TCSharedPointer<CJob> fg_GetJobValuePointer(CContext const *_pContext) { return _pContext->m_pJob; }
TCSharedPointer<CJob> const &fg_ResetAndGet(CContext *_pContext) { _pContext->m_pJob = {}; return _pContext->m_pJob; }
TCSharedPointer<CJob> fg_ResetAndGetValue(CContext *_pContext) { _pContext->m_pJob = {}; return _pContext->m_pJob; }
// Helper returning an owner by reference through a pointer parameter.
TCFuture<int> fg_PointerGetter(CContext const *_pContext) {
  auto &Result = _pContext->m_pJob->m_Result;
  return fg_CallSafe([&Result, pJob = fg_GetJobPointer(_pContext)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_PointerGetterLocal() {
  CContext Context{fg_MakeJob(), 1};
  int &Result = Context.m_pJob->m_Result;
  auto pKeep = fg_GetJobPointer(&Context);
  return fg_CallSafe([&Result, pKeep]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// Helper returning an owner by value from a member.
TCFuture<int> fg_ValueGetter(CContext const &_Context) {
  auto &Result = _Context.m_pJob->m_Result;
  return fg_CallSafe([&Result, pJob = fg_GetJobValue(_Context)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_ValueGetterLocal() {
  CContext Context{fg_MakeJob(), 1};
  int &Result = Context.m_pJob->m_Result;
  auto pKeep = fg_GetJobValue(Context);
  return fg_CallSafe([&Result, pKeep]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_ValuePointerGetterLocal() {
  CContext Context{fg_MakeJob(), 1};
  int &Result = Context.m_pJob->m_Result;
  auto pKeep = fg_GetJobValuePointer(&Context);
  return fg_CallSafe([&Result, pKeep]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// Setters called from a constructor.
struct CKeep {
  TCSharedPointer<CJob> m_pJob;
  CKeep(TCSharedPointer<CJob> const &_pJob) { f_Set(_pJob); }
  void f_Set(TCSharedPointer<CJob> const &_pJob) { m_pJob = _pJob; }
};
TCFuture<int> fg_Setter(TCSharedPointer<CJob> const &_pJob) {
  int &Result = _pJob->m_Result;
  return fg_CallSafe([&Result, Keep = CKeep(_pJob)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// Reported: a helper writing through the pointer, a setter assigning on one branch only.
TCFuture<int> fg_PointerGetterWrites() {
  CContext Context{fg_MakeJob(), 1};
  int &Result = Context.m_pJob->m_Result;
  auto pKeep = fg_ResetAndGet(&Context);
  return fg_CallSafe([&Result, pKeep]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
TCFuture<int> fg_PointerGetterValueWrites() {
  CContext Context{fg_MakeJob(), 1};
  int &Result = Context.m_pJob->m_Result;
  auto pKeep = fg_ResetAndGetValue(&Context);
  return fg_CallSafe([&Result, pKeep]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
struct CMaybeKeep {
  TCSharedPointer<CJob> m_pJob;
  CMaybeKeep(TCSharedPointer<CJob> const &_pJob, bool _bKeep) { f_Set(_pJob, _bKeep); }
  void f_Set(TCSharedPointer<CJob> const &_pJob, bool _bKeep) { if (_bKeep) m_pJob = _pJob; }
};
TCFuture<int> fg_ConditionalSetter(TCSharedPointer<CJob> const &_pJob) {
  int &Result = _pJob->m_Result;
  return fg_CallSafe([&Result, Keep = CMaybeKeep(_pJob, true)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// Clean: CMaybeKeep(_pJob, true) stores _pJob, so Keep keeps the job.
}
}
// Round 172: an owner reached through what an owner owns, `_pContext->m_pJob`;
// a getter by value of the receiver's member; an explicit-object setter a
// constructor calls; a helper returning an element at a constant index; a
// factory given a pointer, also an unchanged local one; a fluent setter that
// gives its object back; read-only helpers given a pointer alias, an array
// element's holder, or a constructor given the holder's address; all keep the
// job. Reported: the owner reassigned, written through or reset between, a
// fluent write through the result, constructors that clear the owner in the
// body or an initializer, and helpers that clear the element.
namespace NReview2Round172Rest {
using namespace NMib::NStorage;
TCFuture<int> fg_Work();
struct CJob { int m_Result = 0; };
DMibReturnsNew TCSharedPointer<CJob> fg_MakeJob();
struct CContext {
  TCSharedPointer<CJob> m_pJob;
  int m_ID = 0;
  TCSharedPointer<CJob> f_Job() const { return m_pJob; }
  CContext &f_SetID(int _ID) { m_ID = _ID; return *this; }
  CContext &f_ResetFluent() { m_pJob = {}; return *this; }
};
struct CBatch { TCSharedPointer<CJob> m_pJobs[2]; int m_Count = 0; };
int fg_GetID(CContext *_pContext) { return _pContext->m_ID; }
int fg_GetCount(CBatch const *_pBatch) { return _pBatch->m_Count; }
TCSharedPointer<CJob> const &fg_GetJob(CContext *_pContext) { return _pContext->m_pJob; }
CContext fg_Keep(CContext const *_pContext) { return {_pContext->m_pJob}; }
TCSharedPointer<CJob> fg_First(CBatch const &_Batch) { return _Batch.m_pJobs[0]; }
void fg_ClearFirst(CBatch *_pBatch) { _pBatch->m_pJobs[0] = {}; }
void fg_ClearSecond(CBatch *_pBatch) { _pBatch->m_pJobs[1] = {}; }
struct CJobInfo { int m_ID; CJobInfo(CContext const *_pContext) : m_ID(_pContext->m_ID) {} };
struct CKeep {
  TCSharedPointer<CJob> m_pJob;
  CKeep(TCSharedPointer<CJob> const &_pJob) { f_SetJob(_pJob); }
  void f_SetJob(this CKeep &_Self, TCSharedPointer<CJob> const &_pJob) { _Self.m_pJob = _pJob; }
};
struct CKeepReset {
  TCSharedPointer<CJob> m_pJob;
  CKeepReset(TCSharedPointer<CJob> const &_pJob) { f_SetJob(_pJob); }
  void f_SetJob(this CKeepReset &_Self, TCSharedPointer<CJob> const &_pJob) { _Self.m_pJob = _pJob; _Self.m_pJob = {}; }
};
// 1. smart-pointer receivers (coroutine-1, coroutine-5)
TCFuture<int> fg_SmartReceiver(TCSharedPointer<CContext> _pContext) {
  int &Result = _pContext->m_pJob->m_Result;
  return fg_CallSafe([&Result, pJob = _pContext->m_pJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_SmartReceiverLocal(TCSharedPointer<CContext> _pContext) {
  int &Result = _pContext->m_pJob->m_Result;
  auto pJob = _pContext->m_pJob;
  return fg_CallSafe([&Result, pJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// 2. by-value getter of the receiver (coroutine-2)
TCFuture<int> fg_ValueGetter() {
  CContext Context{fg_MakeJob(), 1};
  int &Result = Context.m_pJob->m_Result;
  return fg_CallSafe([&Result, pKeep = Context.f_Job()]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// 3. explicit-object setter (coroutine-2, 3, 4, 9)
TCFuture<int> fg_ExplicitSetter(TCSharedPointer<CJob> const &_pJob) {
  int &Result = _pJob->m_Result;
  return fg_CallSafe([&Result, Keep = CKeep(_pJob)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// 4. constant array index in a return summary (coroutine-2)
TCFuture<int> fg_ArrayReturn() {
  CBatch Batch{{fg_MakeJob(), fg_MakeJob()}, 2};
  int &Result = Batch.m_pJobs[0]->m_Result;
  return fg_CallSafe([&Result, pKeep = fg_First(Batch)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// 5. pointer-taking factory with a pointer argument (coroutine-3, coroutine-8)
TCFuture<int> fg_PointerFactory(CContext const *_pContext) {
  int &Result = _pContext->m_pJob->m_Result;
  return fg_CallSafe([&Result, Keep = fg_Keep(_pContext)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_PointerFactoryAlias() {
  CContext Context{fg_MakeJob(), 1};
  auto const *pContext = &Context;
  int &Result = Context.m_pJob->m_Result;
  return fg_CallSafe([&Result, Keep = fg_Keep(pContext)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// 6. fluent return *this (coroutine-3)
TCFuture<int> fg_Fluent() {
  CContext Context{fg_MakeJob(), 1};
  int &Result = Context.m_pJob->m_Result;
  Context.f_SetID(1);
  return fg_CallSafe([&Result, Context]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// 7. read-only helper through a pointer alias (coroutine-6, coroutine-7)
TCFuture<int> fg_AliasReadOnly() {
  CContext Context{fg_MakeJob(), 1};
  auto *pContext = &Context;
  int &Result = pContext->m_pJob->m_Result;
  fg_GetID(pContext);
  return fg_CallSafe([&Result, Context]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_AliasGetter() {
  CContext Context{fg_MakeJob(), 1};
  int &Result = Context.m_pJob->m_Result;
  auto *pContext = &Context; auto pKeep = fg_GetJob(pContext);
  return fg_CallSafe([&Result, pKeep]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// 8. array element indices given to a helper's change summary (coroutine-6)
TCFuture<int> fg_ArrayReadOnly() {
  CBatch Batch{{fg_MakeJob(), fg_MakeJob()}, 2};
  int &Result = Batch.m_pJobs[0]->m_Result;
  fg_GetCount(&Batch);
  return fg_CallSafe([&Result, Batch]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// 9. read-only constructor argument (coroutine-8)
TCFuture<int> fg_ConstructorReadOnly() {
  CContext Context{fg_MakeJob(), 1};
  int &Result = Context.m_pJob->m_Result;
  CJobInfo Info(&Context);
  return fg_CallSafe([&Result, pKeep = Context.m_pJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// Controls, reported.
TCFuture<int> fg_AliasReassigned() {
  CContext Context{fg_MakeJob(), 1};
  CContext Other{fg_MakeJob(), 2};
  auto *pContext = &Context;
  int &Result = Context.m_pJob->m_Result;
  pContext = &Other;
  auto pKeep = fg_GetJob(pContext);
  return fg_CallSafe([&Result, pKeep]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
TCFuture<int> fg_ExplicitSetterResets(TCSharedPointer<CJob> const &_pJob) {
  int &Result = _pJob->m_Result;
  return fg_CallSafe([&Result, Keep = CKeepReset(_pJob)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
TCFuture<int> fg_FluentWrites() {
  CContext Context{fg_MakeJob(), 1};
  int &Result = Context.m_pJob->m_Result;
  Context.f_ResetFluent();
  return fg_CallSafe([&Result, Context]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
TCSharedPointer<CContext> fg_OtherContext();
TCFuture<int> fg_SmartReceiverReassigned(TCSharedPointer<CContext> _pContext) {
  int &Result = _pContext->m_pJob->m_Result;
  _pContext = fg_OtherContext();
  return fg_CallSafe([&Result, pJob = _pContext->m_pJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
TCFuture<int> fg_SmartReceiverWritten(TCSharedPointer<CContext> _pContext) {
  int &Result = _pContext->m_pJob->m_Result;
  _pContext->m_pJob = fg_MakeJob();
  return fg_CallSafe([&Result, pJob = _pContext->m_pJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
TCFuture<int> fg_SmartReceiverStar(TCSharedPointer<CContext> _pContext) {
  int &Result = _pContext->m_pJob->m_Result;
  *_pContext = CContext{};
  return fg_CallSafe([&Result, pJob = _pContext->m_pJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
TCFuture<int> fg_SmartReceiverReset(TCSharedPointer<CContext> _pContext) {
  int &Result = _pContext->m_pJob->m_Result;
  _pContext->f_ResetFluent();
  return fg_CallSafe([&Result, pJob = _pContext->m_pJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
TCFuture<int> fg_FluentChainWrites() {
  CContext Context{fg_MakeJob(), 1};
  int &Result = Context.m_pJob->m_Result;
  Context.f_SetID(1).m_pJob = {};
  return fg_CallSafe([&Result, Context]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
TCFuture<int> fg_FluentReferenceWrites() {
  CContext Context{fg_MakeJob(), 1};
  int &Result = Context.m_pJob->m_Result;
  auto &Same = Context.f_SetID(1);
  Same.m_pJob = {};
  return fg_CallSafe([&Result, Context]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
TCFuture<int> fg_ConstructorWrites() {
  struct CClear { CClear(CContext *_pContext) { _pContext->m_pJob = {}; } };
  CContext Context{fg_MakeJob(), 1};
  int &Result = Context.m_pJob->m_Result;
  CClear Clear(&Context);
  return fg_CallSafe([&Result, Context]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
TCFuture<int> fg_ConstructorInitWrites() {
  struct CClear { int m_Value; CClear(CContext *_pContext) : m_Value((_pContext->m_pJob = {}, 1)) {} };
  CContext Context{fg_MakeJob(), 1};
  int &Result = Context.m_pJob->m_Result;
  CClear Clear(&Context);
  return fg_CallSafe([&Result, Context]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
TCFuture<int> fg_ArrayWrites() {
  CBatch Batch{{fg_MakeJob(), fg_MakeJob()}, 2};
  int &Result = Batch.m_pJobs[0]->m_Result;
  fg_ClearFirst(&Batch);
  return fg_CallSafe([&Result, Batch]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
TCFuture<int> fg_ArrayOtherWrites() {
  CBatch Batch{{fg_MakeJob(), fg_MakeJob()}, 2};
  int &Result = Batch.m_pJobs[0]->m_Result;
  fg_ClearSecond(&Batch);
  return fg_CallSafe([&Result, Batch]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
}
// Round 172: holders of owners whatever their implementation holds, by the
// arguments of their templates: an optional deriving from its storage, a map
// with implementation members, an array record, a container made of an
// initializer list, a tuple copied by a converting constructor, and an owner
// reached through `->` of a pointer to it. Reported: an element reset, a
// container made of another owner, an optional cleared.
namespace std {
template <typename t_CElement> class initializer_list {
  t_CElement const *mp_pBegin;
  decltype(sizeof(0)) mp_Size;
};
} // namespace std
namespace NReview2Round172Holders {
using namespace NMib::NStorage;
TCFuture<int> fg_Work();
struct CJob { int m_Result = 0; };
DMibReturnsNew TCSharedPointer<CJob> fg_MakeJob();
template <typename t_CType> struct TCStorage { alignas(t_CType) unsigned char m_Data[sizeof(t_CType)]; };
template <typename t_CType> struct TCOptional : TCStorage<t_CType> {
  TCOptional(t_CType const &_Value);
  TCOptional(TCOptional const &_Other);
  t_CType &operator*();
  void f_Clear();
};
template <typename t_CKey, typename t_CValue> struct TCNode { t_CKey m_Key; t_CValue m_Value; TCNode *m_pLeft; };
template <typename t_CNode> struct TCTree { t_CNode *m_pRoot; };
template <typename t_CKey, typename t_CValue> struct TCMap {
  TCTree<TCNode<t_CKey, t_CValue>> mp_Tree;
  TCMap();
  TCMap(TCMap const &_Other);
  t_CValue *f_FindEqual(t_CKey const &_Key);
};
template <typename t_CType, int t_Size> struct TCArray {
  t_CType m_Elements[t_Size];
  t_CType &operator[](int _Index) { return m_Elements[_Index]; }
};
template <typename t_CType> struct TCList {
  TCList(std::initializer_list<t_CType> _Values);
  TCList(TCList const &_Other);
  t_CType &operator[](int _Index);
  t_CType *mp_pData;
};
template <typename t_CType> struct TCOne {
  t_CType m_Value;
  TCOne(t_CType const &_Value) : m_Value(_Value) {}
  TCOne(TCOne const &_Other) = default;
  template <typename t_COther> TCOne(TCOne<t_COther> &_Other) : m_Value(_Other.m_Value) {}
};
TCFuture<int> fg_Optional(TCSharedPointer<CJob> const &_pJob) {
  TCOptional<TCSharedPointer<CJob>> Job{_pJob};
  int &Result = (*Job)->m_Result;
  return fg_CallSafe([&Result, Job]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_Map(TCMap<int, TCSharedPointer<CJob>> const &_Jobs) {
  TCMap<int, TCSharedPointer<CJob>> Jobs = _Jobs;
  CJob &Job = **Jobs.f_FindEqual(0);
  return fg_CallSafe([&Job, Jobs]() -> TCFuture<int> { co_await fg_Work(); co_return Job.m_Result; });
}
TCFuture<int> fg_Array(TCSharedPointer<CJob> const &_pFirst, TCSharedPointer<CJob> const &_pSecond) {
  TCArray<TCSharedPointer<CJob>, 2> Jobs{{_pFirst, _pSecond}};
  int &Result = Jobs[0]->m_Result;
  return fg_CallSafe([&Result, Jobs]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_List(TCSharedPointer<CJob> const &_pJob) {
  int &Result = _pJob->m_Result;
  TCList<TCSharedPointer<CJob>> Jobs{_pJob};
  return fg_CallSafe([&Result, Jobs]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_Converting(TCSharedPointer<CJob> const &_pJob) {
  TCOne<TCSharedPointer<CJob>> Jobs{_pJob};
  int &Result = Jobs.m_Value->m_Result;
  return fg_CallSafe([&Result, Jobs]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_Arrow(TCMap<int, TCSharedPointer<CJob>> &_Jobs) {
  auto *ppJob = _Jobs.f_FindEqual(0);
  int &Result = ppJob->operator->()->m_Result;
  return fg_CallSafe([&Result, pKeep = *ppJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_ArrayReset(TCSharedPointer<CJob> const &_pFirst, TCSharedPointer<CJob> const &_pSecond) {
  TCArray<TCSharedPointer<CJob>, 2> Jobs{{_pFirst, _pSecond}};
  int &Result = Jobs[0]->m_Result;
  Jobs[0] = nullptr;
  return fg_CallSafe([&Result, Jobs]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
// The parameter the list is made of states that it owns no object another
// parameter owns (DMibDistinct): reported.
TCFuture<int> fg_ListOther(TCSharedPointer<CJob> const &_pJob, DMibDistinct TCSharedPointer<CJob> const &_pOther) {
  int &Result = _pJob->m_Result;
  TCList<TCSharedPointer<CJob>> Jobs{_pOther};
  return fg_CallSafe([&Result, Jobs]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
// Clean: unstated, the two parameters may be the same object.
TCFuture<int> fg_ListOtherUnstated(TCSharedPointer<CJob> const &_pJob, TCSharedPointer<CJob> const &_pOther) {
  int &Result = _pJob->m_Result;
  TCList<TCSharedPointer<CJob>> Jobs{_pOther};
  return fg_CallSafe([&Result, Jobs]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_OptionalCleared(TCSharedPointer<CJob> const &_pJob) {
  TCOptional<TCSharedPointer<CJob>> Job{_pJob};
  int &Result = (*Job)->m_Result;
  Job.f_Clear();
  return fg_CallSafe([&Result, Job]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
} // namespace NReview2Round172Holders
// Round 173 (model): a qualified call of a virtual member function, which no
// override can take; owners nested in owners, a member owner read through an
// owner and taken itself, a const one a captured owner keeps, one a helper
// given a smart pointer gives; elements assigned before a container is
// copied; a scope exit that releases where its scope ends, after the capture;
// a local closure that returns its copy; a pointer initialized through a
// cast; a lookup in a map whose key and value hold owners. Reported: an
// unqualified call an override can take, a nested owner reset or not const,
// one a helper resets, an element cleared, a scope exit that ends first, an
// owner reset before the closure copies it, a lookup cleared.
namespace NReview2Round173Model {
using namespace NMib::NStorage;
TCFuture<int> fg_Work();
struct CJob { int m_Result = 0; int m_Value = 0; };
DMibReturnsNew TCSharedPointer<CJob> fg_MakeJob();
// qualified virtual calls
struct CContextV {
  TCSharedPointer<CJob> m_pJob;
  int m_ID = 0;
  virtual void f_Observe() { ++m_ID; }
};
struct CContextVReset : CContextV {
  void f_Observe() override { m_pJob = {}; }
};
TCFuture<int> fg_Qualified(CContextV &_Context) {
  CContextV Context = _Context;
  int &Result = Context.m_pJob->m_Result;
  Context.CContextV::f_Observe();
  return fg_CallSafe([&Result, Context]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_QualifiedRef(CContextV &_Context) {
  int &Result = _Context.m_pJob->m_Result;
  _Context.CContextV::f_Observe();
  return fg_CallSafe([&Result, pJob = _Context.m_pJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_UnqualifiedRef(CContextV &_Context) {
  int &Result = _Context.m_pJob->m_Result;
  _Context.f_Observe();
  return fg_CallSafe([&Result, pJob = _Context.m_pJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
// owners nested in owners
struct CJobContext { TCSharedPointer<CJob> m_pJob; };
struct CBatchContext { TCSharedPointer<CJobContext> m_pContext; };
TCFuture<int> fg_NestedOwner(TCSharedPointer<CBatchContext> _pBatch) {
  int &Result = _pBatch->m_pContext->m_pJob->m_Result;
  return fg_CallSafe([&Result, pJob = _pBatch->m_pContext->m_pJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_MemberOwner(CBatchContext &_Batch) {
  int &Result = _Batch.m_pContext->m_pJob->m_Result;
  return fg_CallSafe([&Result, pJob = _Batch.m_pContext->m_pJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_MemberOwnerReset(CBatchContext &_Batch) {
  int &Result = _Batch.m_pContext->m_pJob->m_Result;
  _Batch.m_pContext->m_pJob = fg_MakeJob();
  return fg_CallSafe([&Result, pJob = _Batch.m_pContext->m_pJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
struct CRequest { TCSharedPointer<CJob> const m_pJob; };
struct CRequestMutable { TCSharedPointer<CJob> m_pJob; };
TCFuture<int> fg_ConstNested(TCSharedPointer<CRequest> _pRequest) {
  int &Result = _pRequest->m_pJob->m_Result;
  return fg_CallSafe([&Result, pRequest = _pRequest]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_MutableNested(TCSharedPointer<CRequestMutable> _pRequest) {
  int &Result = _pRequest->m_pJob->m_Result;
  return fg_CallSafe([&Result, pRequest = _pRequest]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
TCSharedPointer<CJob> fg_GetJob(TCSharedPointer<CJobContext> const &_pContext) { return _pContext->m_pJob; }
TCFuture<int> fg_HelperGetter(TCSharedPointer<CJobContext> _pContext) {
  int &Result = _pContext->m_pJob->m_Result;
  return fg_CallSafe([&Result, pKeep = fg_GetJob(_pContext)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
void fg_ResetJob(TCSharedPointer<CJobContext> const &_pContext) { _pContext->m_pJob = {}; }
TCFuture<int> fg_NestedResetByHelper(TCSharedPointer<CJobContext> _pContext) {
  int &Result = _pContext->m_pJob->m_Result;
  fg_ResetJob(_pContext);
  return fg_CallSafe([&Result, pJob = _pContext->m_pJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
// assigned container elements
template <typename t_CType> struct TCVec {
  TCVec();
  TCVec(TCVec const &_Other);
  void f_SetLen(int _Len);
  void f_Insert(t_CType const &_Value);
  t_CType &operator[](int _Index) { return mp_pData[_Index]; }
  t_CType *mp_pData;
};
TCFuture<int> fg_AssignedElement(TCSharedPointer<CJob> _pJob) {
  TCVec<TCSharedPointer<CJob>> Jobs;
  Jobs.f_SetLen(1);
  Jobs[0] = _pJob;
  int &Value = _pJob->m_Value;
  return fg_CallSafe([&Value, Jobs]() -> TCFuture<int> { co_await fg_Work(); co_return Value; });
}
// A resize to the constant 0 of RemovingMethods, `f_SetLen(0)`, removes every
// element; to another length what is left can't be told, and the copy keeps
// by type.
TCFuture<int> fg_AssignedElementResized(TCSharedPointer<CJob> _pJob) {
  TCVec<TCSharedPointer<CJob>> Jobs;
  Jobs.f_SetLen(1);
  Jobs[0] = _pJob;
  int &Value = _pJob->m_Value;
  Jobs.f_SetLen(1);
  return fg_CallSafe([&Value, Jobs]() -> TCFuture<int> { co_await fg_Work(); co_return Value; });
}
TCFuture<int> fg_AssignedElementCleared(TCSharedPointer<CJob> _pJob) {
  TCVec<TCSharedPointer<CJob>> Jobs;
  Jobs.f_SetLen(1);
  Jobs[0] = _pJob;
  int &Value = _pJob->m_Value;
  Jobs.f_SetLen(0);
  return fg_CallSafe([&Value, Jobs]() -> TCFuture<int> { co_await fg_Work(); co_return Value; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Value', which it captures by reference, after a suspension, when 'Value' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
// deferred releases
TCFuture<int> fg_DeferredRelease() {
  TCSharedPointer<CJob> pJob = fg_MakeJob();
  auto &Result = pJob->m_Result;
  auto Cleanup = g_OnScopeExit / [&] { pJob.f_Clear(); };
  return fg_CallSafe([&Result, pJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_DeferredReleaseInner() {
  TCSharedPointer<CJob> pJob = fg_MakeJob();
  auto &Result = pJob->m_Result;
  {
    auto Cleanup = g_OnScopeExit / [&] { pJob.f_Clear(); };
  }
  return fg_CallSafe([&Result, pJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
// synchronous local lambda
TCFuture<int> fg_LambdaGetter() {
  TCSharedPointer<CJob> pJob = fg_MakeJob();
  auto &Value = pJob->m_Value;
  auto fGet = [pJob] { return pJob; };
  auto pKeep = fGet();
  return fg_CallSafe([&Value, pKeep]() -> TCFuture<int> { co_await fg_Work(); co_return Value; });
}
TCFuture<int> fg_LambdaGetterReset() {
  TCSharedPointer<CJob> pJob = fg_MakeJob();
  auto &Value = pJob->m_Value;
  pJob = fg_MakeJob();
  auto fGet = [pJob] { return pJob; };
  auto pKeep = fGet();
  return fg_CallSafe([&Value, pKeep]() -> TCFuture<int> { co_await fg_Work(); co_return Value; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Value', which it captures by reference, after a suspension, when 'Value' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
// pointer initialiser casts
struct CBase { virtual ~CBase(); };
struct CCastContext : CBase { TCSharedPointer<CJob> m_pJob; };
TCFuture<int> fg_PointerCast(CCastContext Context) {
  CBase &Base = Context;
  auto *pContext = static_cast<CCastContext *>(&Base);
  int &Result = pContext->m_pJob->m_Result;
  return fg_CallSafe([&Result, Context]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// a lookup in a map whose key, a buffer owner, and value hold owners
template <typename t_CType> struct TCVector {
  t_CType *mp_pData;
  TCVector(TCVector const &_Other);
};
using CStr = TCVector<char>;
template <typename t_CKey, typename t_CValue> struct TCMap2 {
  struct CNode { t_CKey m_Key; t_CValue m_Value; CNode *m_pNext; };
  CNode *mp_pRoot;
  TCMap2(TCMap2 &&_Other);
  t_CValue *f_FindEqual(t_CKey const &_Key);
  void f_Clear();
};
TCFuture<int> fg_StringKeyLookup(TCMap2<CStr, TCSharedPointer<CJob>> _Jobs, CStr const &_JobID) {
  auto *ppJob = _Jobs.f_FindEqual(_JobID);
  int &Result = (*ppJob)->m_Result;
  return fg_CallSafe([&Result, Jobs = fg_Move(_Jobs)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_StringKeyLookupCleared(TCMap2<CStr, TCSharedPointer<CJob>> _Jobs, CStr const &_JobID) {
  auto *ppJob = _Jobs.f_FindEqual(_JobID);
  int &Result = (*ppJob)->m_Result;
  _Jobs.f_Clear();
  return fg_CallSafe([&Result, Jobs = fg_Move(_Jobs)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
} // namespace NReview2Round173Model
// Round 173 (model): a member function of a container whose body can be seen
// that clears it changes its elements, which are in storage its members
// manage. Reported.
namespace NReview2Round173ModelClear {
using namespace NMib::NStorage;
TCFuture<int> fg_Work();
struct CJob { int m_Value = 0; };
template <typename t_CType> struct TCVec {
  TCVec(TCVec const &_Other);
  t_CType &operator[](int _Index) { return mp_pData[_Index]; }
  void f_Clear() { mp_pData = nullptr; }
  t_CType *mp_pData;
};
TCFuture<int> fg_ElementCleared(TCVec<TCSharedPointer<CJob>> _Jobs) {
  TCVec<TCSharedPointer<CJob>> Jobs = _Jobs;
  int &Value = Jobs[0]->m_Value;
  Jobs.f_Clear();
  return fg_CallSafe([&Value, Jobs]() -> TCFuture<int> { co_await fg_Work(); co_return Value; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Value', which it captures by reference, after a suspension, when 'Value' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
} // namespace NReview2Round173ModelClear
