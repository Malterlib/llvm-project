# Writes the same cases of what an owner keeps alive with the owner taken in
# one of the ways a capture can take it: `direct`, `pKeep = pOwner` or a move;
# `alias`, through an unchanged local reference, `auto &&rOwner = pOwner`;
# `copy`, by a copy function, `fg_TempCopy(pOwner)`; `forward`,
# `fg_Forward<decltype(pOwner)>(pOwner)`; `convert`, by a converting copy to
# an owner of a base; and with the owner a parameter, `value`, by value, and
# `rvalue`, by `&&`. Each form keeps every line where it is, so that what
# the checks report of the forms of one place, the locals or the parameters,
# is the same, line for line. The shared owner is a copy and a move, the
# unique and the buffer one a move, the sharedbuffer one a copy and a move,
# not converted, by their kinds (OwnerTypes). Owner slots
# that the parameter forms don't replace, members of `this`, of a local, of a
# member and of the explicit object, and structured bindings, are taken the
# same ways (takeOf), directly in the parameter forms; a copy of an owner the
# reference is made through after it is a copy in each form (copyTake).
import sys

Form = sys.argv[1]
Forms = ('direct', 'alias', 'copy', 'forward', 'convert', 'value', 'rvalue')
if Form not in Forms:
    sys.exit('Unknown form: ' + Form)
Parameter = Form in ('value', 'rvalue')


def owner(Type, Name, Made):
    # The declaration of a local owner, or of a parameter, which replaces it.
    if Parameter:
        return '(void)0;'
    return '%s %s = %s;' % (Type, Name, Made)


def signature(Type, Name):
    if not Parameter:
        return ''
    return '%s %s%s' % (Type, '&&' if Form == 'rvalue' else '', Name)


def alias(Name):
    # The line an alias takes, a statement of nothing in the other forms.
    if Form == 'alias':
        return 'auto &&r%s = %s;' % (Name, Name)
    return '(void)0;'


def take(Kind, Name, Base):
    # How the capture takes the owner: a shared one copied, the others moved.
    Source = ('r' + Name) if Form == 'alias' else Name
    if Form == 'forward':
        return 'fg_Forward<decltype(%s)>(%s)' % (Name, Source)
    if Kind in ('shared', 'sharedbuffer'):
        if Form == 'copy':
            return 'fg_TempCopy(%s)' % Source
        if Form == 'convert' and Kind == 'shared':
            return 'TCSharedPointer<%s>(%s)' % (Base, Source)
        return Source
    return 'fg_Move(%s)' % Source


def copyTake(Name):
    # A copy of a shared owner in each form: a forward of an lvalue
    # reference, which copies, as one of the owner's own type moves it.
    if Form == 'forward':
        Source = ('r' + Name) if Form == 'alias' else Name
        return 'fg_Forward<decltype(%s) &>(%s)' % (Name, Source)
    return take('shared', Name, 'CJobBase')


def aliasOf(Alias, Expr):
    # The line an alias of an owner slot takes, nothing in the other forms.
    if Form == 'alias':
        return 'auto &&%s = %s;' % (Alias, Expr)
    return '(void)0;'


def takeOf(Kind, Expr, Alias, Base='CJobBase'):
    # How the capture takes an owner slot that isn't a local the parameter
    # forms replace: those take it directly.
    Source = Alias if Form == 'alias' else Expr
    if Form == 'forward':
        return 'fg_Forward<decltype(%s)>(%s)' % (Expr, Source)
    if Kind == 'shared':
        if Form == 'copy':
            return 'fg_TempCopy(%s)' % Source
        if Form == 'convert':
            return 'TCSharedPointer<%s>(%s)' % (Base, Source)
        return Source
    return 'fg_Move(%s)' % Source


def takeArray(Expr, Alias):
    # How the capture takes an array: copied, by its name or an alias.
    return Alias if Form == 'alias' else Expr


def sourceOf(Name):
    # The owner as a form names it where it isn't taken: its alias or itself.
    return ('r' + Name) if Form == 'alias' else Name


def takeCast(Name):
    # A copy of the owner through a cast to a const reference to it.
    return 'static_cast<TCSharedPointer<CJob> const &>(%s)' % sourceOf(Name)


def takeSame(Name):
    # A copy of a shared owner of its own type in each form, of the owner's
    # own class in the converting form.
    if Form == 'convert':
        return 'TCSharedPointer<CJob>(%s)' % sourceOf(Name)
    return take('shared', Name, 'CJobBase')


def takeRecord(Expr, Alias):
    # How the capture takes a record that holds an owner: copied, or moved
    # by a forward.
    Source = Alias if Form == 'alias' else Expr
    if Form == 'forward':
        return 'fg_Forward<decltype(%s)>(%s)' % (Expr, Source)
    if Form == 'copy':
        return 'fg_TempCopy(%s)' % Source
    return Source


Lines = '''#include "malterlib-concurrency.h"
namespace std {
template <typename t_CType> struct tuple_size;
template <decltype(sizeof(0)) t_Index, typename t_CType> struct tuple_element;
}
using namespace NMib::NConcurrency;
TCFuture<int> fg_Work();
template <typename t_CType> t_CType fg_TempCopy(t_CType const &_Value);
template <typename t_CType>
struct TCUniquePointer {
  t_CType *m_pPointTo;
  explicit TCUniquePointer(t_CType *_pPointTo) : m_pPointTo(_pPointTo) {}
  TCUniquePointer(TCUniquePointer &&_Other) : m_pPointTo(_Other.m_pPointTo) { _Other.m_pPointTo = nullptr; }
  ~TCUniquePointer() { delete m_pPointTo; }
  t_CType *operator->() const { return m_pPointTo; }
  t_CType &operator*() const { return *m_pPointTo; }
};
template <typename t_CData>
struct TCVector {
  TCVector();
  TCVector(TCVector const &_Other);
  TCVector(TCVector &&_Other);
  ~TCVector();
  void f_InsertLast(t_CData const &_Value);
  void f_Clear();
  t_CData *f_GetArray() { return mp_pData; }
  t_CData &operator[](int _Index) { return mp_pData[_Index]; }
  t_CData const &operator[](int _Index) const { return mp_pData[_Index]; }
  t_CData &f_GetFirst() { return (*this)[0]; }
  t_CData &f_GetLast() { return (*this)[mp_nLen - 1]; }
  t_CData *begin() { return mp_pData; }
  t_CData *end() { return mp_pData + 1; }
  t_CData *mp_pData;
  int mp_nLen = 0;
};
template <typename t_CKey, typename t_CValue>
struct TCMap {
  t_CValue &operator[](t_CKey const &_Key) { return *mp_pValue; }
  t_CValue *f_FindEqual(t_CKey const &_Key) { return mp_pValue; }
  void f_Remove(t_CKey const &_Key);
  t_CValue *mp_pValue;
};
template <typename t_CType> void fg_Swap(t_CType &_Left, t_CType &_Right);
struct CJobBase { int m_Result = 0; };
struct CJob : CJobBase {};
DMibReturnsNew TCSharedPointer<CJob> fg_MakeJob();
// A shared owner keeps what a reference into what it owns refers to.
TCFuture<int> fg_Shared(%s) {
  %s
  %s
  int &Result = pOwner->m_Result;
  return fg_CallSafe([&Result, pKeep = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// A unique one, moved, what a pointer into what it owns points to.
TCFuture<int> fg_Unique(%s) {
  %s
  %s
  auto *pResult = &pOwner->m_Result;
  return fg_CallSafe([pResult, pKeep = %s]() -> TCFuture<int> { co_await fg_Work(); co_return *pResult; });
}
// A buffer one, moved, what a pointer into its buffer points to.
TCFuture<int> fg_Buffer(%s) {
  %s
  %s
  int *pValue = pOwner.f_GetArray();
  return fg_CallSafe([pValue, pKeep = %s]() -> TCFuture<int> { co_await fg_Work(); co_return *pValue; });
}
// A copy of a shared owner taken before the reference, and one after it,
// keep what both own, the same allocation.
TCFuture<int> fg_CopyBefore(%s) {
  %s
  %s
  auto pCopy = %s;
  int &Result = pOwner->m_Result;
  return fg_CallSafe([&Result, pCopy]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_CopyAfter(%s) {
  %s
  %s
  int &Result = pOwner->m_Result;
  auto pCopy = %s;
  return fg_CallSafe([&Result, pCopy]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// A view of what a buffer owner owns, captured by value with the owner.
struct CIntStrSpan {
  CIntStrSpan(int *_pData, int _Length);
  int *m_pData;
  int m_Length;
};
TCFuture<int> fg_View(%s) {
  %s
  %s
  CIntStrSpan Span(pOwner.f_GetArray(), 8);
  return fg_CallSafe([Span, pKeep = %s]() -> TCFuture<int> { co_await fg_Work(); co_return *Span.m_pData; });
}
// The object of a class of CAllowUnsafeThis, through `this`, a reference and
// a pointer parameter.
struct CUnsafe : CAllowUnsafeThis {
  int m_Value = 0;
  TCFuture<int> f_Member() { return fg_CallSafe([&Value = m_Value]() -> TCFuture<int> { co_await fg_Work(); co_return Value; }); }
};
TCFuture<int> fg_UnsafeReference(CUnsafe &_State) { return fg_CallSafe([&_State]() -> TCFuture<int> { co_await fg_Work(); co_return _State.m_Value; }); }
TCFuture<int> fg_UnsafePointer(CUnsafe *_pState) { return fg_CallSafe([pValue = &_pState->m_Value]() -> TCFuture<int> { co_await fg_Work(); co_return *pValue; }); }
// Reported: a raw pointer into what an owner the closure doesn't take owns.
TCFuture<int> fg_Unrelated(%s) {
  %s
  TCUniquePointer<CJob> pOther(new CJob);
  auto *pResult = &pOwner->m_Result;
  return fg_CallSafe([pResult, pKeep = fg_Move(pOther)]() -> TCFuture<int> { co_await fg_Work(); co_return *pResult; });
}
// Reported: the owner captured by reference, which the closure doesn't own.
TCFuture<int> fg_ByReference(%s) {
  %s
  %s
  auto *pResult = &pOwner->m_Result;
  return fg_CallSafe([pResult, &pOwner]() -> TCFuture<int> { co_await fg_Work(); co_return *pResult; });
}
// A shared owner made of `this` keeps the object, however it is taken.
struct CState : CJobBase {
  int m_Value = 0;
  TCFuture<int> f_Run() {
    TCSharedPointer<CState> pThis{fg_Explicit(this)};
    %s
    return fg_CallSafe([this, pKeep = %s]() -> TCFuture<int> { co_await fg_Work(); co_return m_Value; });
  }
  // A reference to a member of the object, with a shared owner of it.
  TCFuture<int> f_Member() {
    TCSharedPointer<CState> pThis{fg_Explicit(this)};
    %s
    int &Value = m_Value;
    return fg_CallSafe([&Value, pKeep = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Value; });
  }
};
// Reported: what a reference parameter refers to.
TCFuture<int> fg_Referent(CJob &_Job) {
  return fg_CallSafe([&Job = _Job]() -> TCFuture<int> { co_await fg_Work(); co_return Job.m_Result; });
}
// Owner slots: a member of `this`, of a local, of a member, of the explicit
// object, and a structured binding.
struct CHolder { TCSharedPointer<CJob> m_pOwner; int m_Other = 0; };
struct CMemberSlot {
  TCSharedPointer<CJob> mp_pOwner;
  CHolder mp_Holder;
  TCFuture<int> f_Member() {
    %s
    int &Result = mp_pOwner->m_Result;
    return fg_CallSafe([&Result, pKeep = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
  }
  TCFuture<int> f_MemberOfMember() {
    %s
    int &Result = mp_Holder.m_pOwner->m_Result;
    return fg_CallSafe([&Result, pKeep = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
  }
  TCFuture<int> f_Explicit(this CMemberSlot &_Self) {
    %s
    int &Result = _Self.mp_pOwner->m_Result;
    return fg_CallSafe([&Result, pKeep = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
  }
};
TCFuture<int> fg_MemberOfLocal() {
  CHolder Holder{fg_MakeJob()};
  %s
  int &Result = Holder.m_pOwner->m_Result;
  return fg_CallSafe([&Result, pKeep = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_Binding() {
  auto [pBound, Other] = CHolder{fg_MakeJob()};
  %s
  int &Result = pBound->m_Result;
  return fg_CallSafe([&Result, pKeep = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// The explicit object, with a shared owner of it, converted to one of its
// base.
struct CExplicitState : CJobBase {
  TCFuture<int> f_Run(this CExplicitState &_Self) {
    TCSharedPointer<CExplicitState> pThis{fg_Explicit(&_Self)};
    %s
    int &Result = _Self.m_Result;
    return fg_CallSafe([&Result, pKeep = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
  }
};
// Timings: assigned before the reference, a buffer changed before the
// pointer into it, a copy the reference is made through released after it,
// a copy moved into the closure, the owner released after the closure takes
// it.
TCFuture<int> fg_AssignedBefore(%s) {
  %s
  pOwner = fg_MakeJob();
  %s
  int &Result = pOwner->m_Result;
  return fg_CallSafe([&Result, pKeep = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_ChangedBefore(%s) {
  %s
  pOwner.f_InsertLast(42);
  %s
  int *pValue = pOwner.f_GetArray();
  return fg_CallSafe([pValue, pKeep = %s]() -> TCFuture<int> { co_await fg_Work(); co_return *pValue; });
}
TCFuture<int> fg_CopyReleased(%s) {
  %s
  %s
  auto pCopy = %s;
  int &Result = pCopy->m_Result;
  pCopy.f_Clear();
  return fg_CallSafe([&Result, pKeep = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_CopyMoved(%s) {
  %s
  %s
  auto pCopy = %s;
  int &Result = pOwner->m_Result;
  return fg_CallSafe([&Result, pKeep = fg_Move(pCopy)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_ReleasedAfter(%s) {
  %s
  %s
  int &Result = pOwner->m_Result;
  auto Future = fg_CallSafe([&Result, pKeep = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
  pOwner.f_Clear();
  return Future;
}
// What f_ShareAsConst gives shares what its object owns.
TCFuture<int> fg_ShareAsConst(%s) {
  %s
  %s
  int const &Result = pOwner->m_Result;
  return fg_CallSafe([&Result, pKeep = %s.f_ShareAsConst()]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// An object holding a pointer into what the owner owns, captured with it.
struct CResultRef { int *m_pResult; };
TCFuture<int> fg_Holder(%s) {
  %s
  %s
  CResultRef Ref{&pOwner->m_Result};
  return fg_CallSafe([Ref, pKeep = %s]() -> TCFuture<int> { co_await fg_Work(); co_return *Ref.m_pResult; });
}
// An actor helper given the actor's own object, and a structured binding of
// a member of the actor.
struct CApp : CActor {
  int m_Value = 0;
  struct CCounters { int m_nStarted = 0; int m_nFinished = 0; } m_Counters;
  static TCFuture<int> fs_Read(CApp *_pThis) { return fg_CallSafe([&Value = _pThis->m_Value]() -> TCFuture<int> { co_await fg_Work(); co_return Value; }); }
  TCFuture<int> f_Read() { return fs_Read(this); }
  TCFuture<int> f_Binding() {
    auto &[nStarted, nFinished] = m_Counters;
    return self / [&nStarted]() -> TCFuture<int> { co_await fg_Work(); co_return nStarted; };
  }
};
// Reported: a release reaching the capture.
TCFuture<int> fg_ReleasedBefore(%s) {
  %s
  %s
  int &Result = pOwner->m_Result;
  pOwner.f_Clear();
  return fg_CallSafe([&Result, pKeep = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// Reported: another allocation assigned between the reference and the
// capture.
TCFuture<int> fg_Reassigned(%s) {
  %s
  %s
  int &Result = pOwner->m_Result;
  pOwner = fg_MakeJob();
  return fg_CallSafe([&Result, pKeep = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// Reported: a member of `this` changed by a call between.
struct CMemberChanged {
  TCSharedPointer<CJob> mp_pOwner;
  void f_Reset();
  TCFuture<int> f_Run() {
    %s
    int &Result = mp_pOwner->m_Result;
    f_Reset();
    return fg_CallSafe([&Result, pKeep = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
  }
};
// Reported: a new allocation each time round a loop, a pointer into the
// first held.
TCFuture<int> fg_Loop() {
  TCSharedPointer<CJob> pLoop;
  CResultRef Ref{nullptr};
  for (int i = 0; i < 2; ++i) {
    pLoop = fg_MakeJob();
    if (i == 0)
      Ref.m_pResult = &pLoop->m_Result;
  }
  return fg_CallSafe([Ref, pLoop]() -> TCFuture<int> { co_await fg_Work(); co_return *Ref.m_pResult; });
}
// Round 163. What reaches the reference doesn't matter, only that the slot
// isn't changed between it and the capture: assigned on both or one branch
// before the reference.
extern bool g_bChoose;
TCFuture<int> fg_BothBranches(%s) {
  %s
  if (g_bChoose) pOwner = fg_MakeJob(); else pOwner = fg_MakeJob();
  %s
  int &Result = pOwner->m_Result;
  return fg_CallSafe([&Result, pKeep = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_OneBranch(%s) {
  %s
  if (g_bChoose) pOwner = fg_MakeJob();
  %s
  int &Result = pOwner->m_Result;
  return fg_CallSafe([&Result, pKeep = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// An owner in a record captured whole.
struct CJobContext { TCSharedPointer<CJob> m_pJob; int m_Other = 0; };
TCFuture<int> fg_RecordCaptured() {
  CJobContext Context{fg_MakeJob()};
  %s
  int &Result = Context.m_pJob->m_Result;
  return fg_CallSafe([&Result, Kept = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// A capture of the closure around, an owner slot of it.
TCFuture<int> fg_Enclosing(%s) {
  %s
  %s
  return fg_CallSafe([pOuter = %s]() -> TCFuture<int> {
    int &Result = pOuter->m_Result;
    co_return co_await fg_CallSafe([&Result, pOuter]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
  });
}
// A structured binding of a tuple-like class, an element of a container, an
// owner a getter gives, and one a forward gives.
struct CPair { TCSharedPointer<CJob> m_pFirst; template <decltype(sizeof(0)) t_Index> TCSharedPointer<CJob> &get() { return m_pFirst; } };
template <> struct std::tuple_size<CPair> { static constexpr decltype(sizeof(0)) value = 1; };
template <> struct std::tuple_element<0, CPair> { using type = TCSharedPointer<CJob>; };
TCFuture<int> fg_TupleBinding() {
  auto [pTupleBound] = CPair{fg_MakeJob()};
  %s
  int &Result = pTupleBound->m_Result;
  return fg_CallSafe([&Result, pKeep = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_Element(TCVector<TCSharedPointer<CJob>> &_Jobs) {
  %s
  int &Result = _Jobs[0]->m_Result;
  return fg_CallSafe([&Result, pKeep = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
struct CJobs { TCSharedPointer<CJob> m_pJob; TCSharedPointer<CJob> &f_Job() { return m_pJob; } };
TCFuture<int> fg_Getter(CJobs &_Jobs) {
  %s
  int &Result = _Jobs.f_Job()->m_Result;
  return fg_CallSafe([&Result, pKeep = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
template <typename t_CType> t_CType const &fg_Const(t_CType const &_Value) { return _Value; }
TCFuture<int> fg_Forwarded(%s) {
  %s
  %s
  int const &Result = fg_Const(pOwner)->m_Result;
  return fg_CallSafe([&Result, pKeep = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// Reported: a change on one path between the reference and the capture, a
// release on one branch, a change of the container, and a suspension
// between them of a member of `this`.
TCFuture<int> fg_ChangedOnOnePath(%s) {
  %s
  %s
  int &Result = pOwner->m_Result;
  if (g_bChoose) pOwner = fg_MakeJob();
  return fg_CallSafe([&Result, pKeep = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_ReleasedOnOneBranch(%s) {
  %s
  %s
  int &Result = pOwner->m_Result;
  if (g_bChoose) pOwner.f_Clear();
  return fg_CallSafe([&Result, pKeep = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_ContainerChanged(TCVector<TCSharedPointer<CJob>> &_Jobs) {
  %s
  int &Result = _Jobs[0]->m_Result;
  _Jobs.f_Clear();
  return fg_CallSafe([&Result, pKeep = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
struct CSuspended : CAllowUnsafeThis {
  TCSharedPointer<CJob> mp_pOwner;
  TCFuture<int> f_Run() {
    %s
    int &Result = mp_pOwner->m_Result;
    co_await fg_Work();
    co_return co_await fg_CallSafe([&Result, pKeep = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
  }
};
// Round 165: a loop body, each pass its own owner and closure; owner paths
// through a local pointer to a record, to `this`, and one a function gives;
// copies of a record holding the owner, before and after the reference; a
// sibling member written and a member function of the record that doesn't
// change the owner; a final override; an identity comparison of `this`; a
// closure made in a closure.
struct CHeld { TCSharedPointer<CJob> m_pJob; int m_nAccess = 0; void f_CountAccess() { ++m_nAccess; } void f_Reset() { m_pJob = {}; } };
CHeld *fg_FindHeld();
void fg_LoopBody(%s) {
  for (int iPass = 0; iPass < 2; ++iPass) {
    %s
    %s
    int &Result = pOwner->m_Result;
    fg_CallSafe([&Result, pKeep = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
  }
}
TCFuture<int> fg_PointerToRecord() {
  CHeld Held{fg_MakeJob()};
  auto *pHeld = &Held;
  %s
  int &Result = pHeld->m_pJob->m_Result;
  return fg_CallSafe([&Result, pKeep = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_PointerFound() {
  auto *pFound = fg_FindHeld();
  %s
  int &Result = pFound->m_pJob->m_Result;
  return fg_CallSafe([&Result, pKeep = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
struct CThisAlias {
  TCSharedPointer<CJob> mp_pOwner;
  CHeld mp_Held;
  virtual void f_Virtual();
  TCFuture<int> f_Run() {
    auto *pSelf = this;
    %s
    int &Result = pSelf->mp_pOwner->m_Result;
    return fg_CallSafe([&Result, pKeep = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
  }
  TCFuture<int> f_Same(CThisAlias *_pOther) {
    %s
    int &Result = mp_pOwner->m_Result;
    bool bSame = this == _pOther;
    return fg_CallSafe([&Result, pKeep = %s, bSame]() -> TCFuture<int> { co_await fg_Work(); co_return Result + bSame; });
  }
  TCFuture<int> f_Sibling() {
    %s
    int &Result = mp_Held.m_pJob->m_Result;
    ++mp_Held.m_nAccess;
    mp_Held.f_CountAccess();
    return fg_CallSafe([&Result, pKeep = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
  }
  // Reported: a call that can run an override that changes it.
  TCFuture<int> f_CallsVirtual() {
    %s
    int &Result = mp_pOwner->m_Result;
    f_Virtual();
    return fg_CallSafe([&Result, pKeep = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
  }
};
struct CFinalCounter final : CThisAlias {
  int m_nCount = 0;
  void f_Virtual() override { ++m_nCount; }
  TCFuture<int> f_Final() {
    %s
    int &Result = mp_pOwner->m_Result;
    f_Virtual();
    return fg_CallSafe([&Result, pKeep = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
  }
};
TCFuture<int> fg_LocalRecordCall() {
  CHeld Held{fg_MakeJob()};
  %s
  int &Result = Held.m_pJob->m_Result;
  Held.f_CountAccess();
  return fg_CallSafe([&Result, pKeep = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_RecordCopyBefore() {
  CHeld Held{fg_MakeJob()};
  CHeld Copy = Held;
  %s
  int &Result = Held.m_pJob->m_Result;
  return fg_CallSafe([&Result, Kept = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_RecordCopyAfter() {
  CHeld Held{fg_MakeJob()};
  int &Result = Held.m_pJob->m_Result;
  CHeld Copy = Held;
  %s
  return fg_CallSafe([&Result, Kept = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_ClosureInClosure(%s) {
  %s
  %s
  int &Result = pOwner->m_Result;
  auto fRun = [&Result, pOuter = %s] { return fg_CallSafe([&Result, pOuter]() -> TCFuture<int> { co_await fg_Work(); co_return Result; }); };
  return fRun();
}
// Reported: a release in the same pass, a write through the pointer to the
// record, and the owner of the copy reassigned before the capture.
void fg_LoopRelease(%s) {
  for (int iPass = 0; iPass < 2; ++iPass) {
    %s
    %s
    int &Result = pOwner->m_Result;
    pOwner.f_Clear();
    fg_CallSafe([&Result, pKeep = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
  }
}
TCFuture<int> fg_WriteThroughPointer() {
  CHeld Held{fg_MakeJob()};
  auto *pHeld = &Held;
  %s
  int &Result = Held.m_pJob->m_Result;
  pHeld->m_pJob = fg_MakeJob();
  return fg_CallSafe([&Result, pKeep = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_CopyReassigned() {
  CHeld Held{fg_MakeJob()};
  int &Result = Held.m_pJob->m_Result;
  CHeld Copy = Held;
  Copy.m_pJob = fg_MakeJob();
  %s
  return fg_CallSafe([&Result, Kept = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// Round 166: an exchange gives the value it moves out, a const alias, a
// record moved by a cast, a temporary a local reference extends, a borrow in
// the closure's own captures, and a record holding a shared owner of `this`
// whose other member the closure writes.
template <typename t_CType> t_CType fg_Exchange(t_CType &_Value, decltype(nullptr));
TCFuture<int> fg_Exchanged(%s) {
  %s
  %s
  int &Result = pOwner->m_Result;
  auto pOut = fg_Exchange(pOwner, nullptr);
  return fg_CallSafe([&Result, pOut]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_ConstAlias(%s) {
  %s
  %s
  int &Result = pOwner->m_Result;
  auto const &pAlias = fg_Const(pOwner);
  return fg_CallSafe([&Result, pAlias]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_RecordCast() {
  CHeld Held{fg_MakeJob()};
  int &Result = Held.m_pJob->m_Result;
  return fg_CallSafe([&Result, Kept = static_cast<CHeld &&>(Held)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_Extended() {
  auto const &pTemp = fg_MakeJob();
  %s
  int const &Result = pTemp->m_Result;
  return fg_CallSafe([&Result, pKeep = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_SameCaptures(%s) {
  %s
  %s
  return fg_CallSafe([&Result = pOwner->m_Result, pKeep = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
struct CKeptState {
  int m_Value = 0;
  struct CKeep { TCSharedPointer<CKeptState> m_pThis; int m_nRequestID = 0; };
  TCFuture<int> f_Run() {
    CKeep Keep{TCSharedPointer<CKeptState>{fg_Explicit(this)}};
    return fg_CallSafe([this, Keep]() mutable -> TCFuture<int> { ++Keep.m_nRequestID; co_await fg_Work(); co_return m_Value; });
  }
};
// Reported: what is borrowed from the slot after its value is moved out.
TCFuture<int> fg_MovedOutBefore(%s) {
  %s
  %s
  auto pOut = fg_Exchange(pOwner, nullptr);
  int &Result = pOwner->m_Result;
  return fg_CallSafe([&Result, pOut]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// Round 166: owners that values hold. A container of shared owners copied
// and moved whole, an array copied, an array member, an element of an
// array, a reference bound to the element at an index or of a key, the
// variable of a loop over a range, a copy and a reference, and a record made
// in place with the owner, captured and assigned, keep the owner; so does a
// record holding a shared owner of `this` made in place.
TCFuture<int> fg_VectorCopied() {
  TCVector<TCSharedPointer<CJob>> Jobs;
  %s
  int &Result = Jobs[0]->m_Result;
  return fg_CallSafe([&Result, Kept = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_VectorMoved() {
  TCVector<TCSharedPointer<CJob>> Jobs;
  %s
  int &Result = Jobs[0]->m_Result;
  return fg_CallSafe([&Result, Kept = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_ArrayCopied() {
  TCSharedPointer<CJob> pJobs[2]{fg_MakeJob(), fg_MakeJob()};
  %s
  int &Result = pJobs[0]->m_Result;
  return fg_CallSafe([&Result, %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
struct CJobBatch { TCSharedPointer<CJob> m_pJobs[2]; };
TCFuture<int> fg_ArrayMember() {
  CJobBatch Batch{{fg_MakeJob(), fg_MakeJob()}};
  %s
  int &Result = Batch.m_pJobs[0]->m_Result;
  return fg_CallSafe([&Result, Kept = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_ArrayElement() {
  TCSharedPointer<CJob> pJobs[2]{fg_MakeJob(), fg_MakeJob()};
  %s
  int &Result = pJobs[0]->m_Result;
  return fg_CallSafe([&Result, pKeep = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_IndexBound(TCVector<TCSharedPointer<CJob>> const &_Jobs, int _iJob) {
  auto const &pJob = _Jobs[_iJob];
  %s
  int &Result = pJob->m_Result;
  return fg_CallSafe([&Result, pKeep = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
struct CJobMap : CActor {
  TCMap<int, TCSharedPointer<CJob>> mp_Jobs;
  TCFuture<int> f_KeyBound(int _JobID) {
    auto &pJob = mp_Jobs[_JobID];
    %s
    int &Result = pJob->m_Result;
    return fg_CallSafe([&Result, pKeep = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
  }
};
void fg_RangeCopy(TCVector<TCSharedPointer<CJob>> &_Jobs) {
  for (auto pJob : _Jobs) {
    %s
    int &Result = pJob->m_Result;
    fg_CallSafe([&Result, pKeep = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
  }
}
void fg_RangeReference(TCVector<TCSharedPointer<CJob>> &_Jobs) {
  for (auto &pJob : _Jobs) {
    %s
    int &Result = pJob->m_Result;
    fg_CallSafe([&Result, pKeep = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
  }
}
struct CBaseContext { TCSharedPointer<CJobBase> m_pJob; int m_nRequestID = 0; };
TCFuture<int> fg_InPlace(%s) {
  %s
  %s
  int &Result = pOwner->m_Result;
  return fg_CallSafe([&Result, Context = CBaseContext{%s, 1}]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_InPlaceAssigned(%s) {
  %s
  %s
  CBaseContext Context;
  Context = CBaseContext{%s, 1};
  int &Result = Context.m_pJob->m_Result;
  return fg_CallSafe([&Result, Context]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
struct CInPlaceState {
  int m_Value = 0;
  struct CKeep { TCSharedPointer<CInPlaceState> m_pThis; int m_nRequestID = 0; };
  TCFuture<int> f_Run() {
    return fg_CallSafe([this, Keep = CKeep{TCSharedPointer<CInPlaceState>{fg_Explicit(this)}, 1}]() -> TCFuture<int> { co_await fg_Work(); co_return m_Value; });
  }
};
// Reported: an element of the container changed between the reference and
// the capture, the container cleared, an element at another index assigned
// where the reference is bound at an index, the member of the record
// reassigned, and what is borrowed from the container after it is moved
// out.
TCFuture<int> fg_ElementChanged() {
  TCVector<TCSharedPointer<CJob>> Jobs;
  %s
  int &Result = Jobs[0]->m_Result;
  Jobs[0] = fg_MakeJob();
  return fg_CallSafe([&Result, Kept = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_ContainerCleared() {
  TCVector<TCSharedPointer<CJob>> Jobs;
  %s
  int &Result = Jobs[0]->m_Result;
  Jobs.f_Clear();
  return fg_CallSafe([&Result, Kept = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_IndexAssigned(TCVector<TCSharedPointer<CJob>> &_Jobs, int _iJob, int _iOther) {
  auto const &pJob = _Jobs[_iJob];
  %s
  int &Result = pJob->m_Result;
  _Jobs[_iOther] = fg_MakeJob();
  return fg_CallSafe([&Result, pKeep = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_MemberReassigned(%s) {
  %s
  %s
  CBaseContext Context{%s, 1};
  int &Result = Context.m_pJob->m_Result;
  Context.m_pJob = fg_MakeJob();
  return fg_CallSafe([&Result, Context]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_ContainerMovedOut() {
  TCVector<TCSharedPointer<CJob>> Jobs;
  auto Moved = fg_Move(Jobs);
  %s
  int &Result = Jobs[0]->m_Result;
  return fg_CallSafe([&Result, Kept = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// Round 167: a holder whose constructor copies the owner, a base holding it,
// a cast to a const reference, an element an accessor gives and one a
// lookup gives bound to a reference, a getter's owner with its holder
// captured, an owner swapped into the captured slot, and containers of
// records that hold each other, told in either order, keep the owner.
struct CBaseHolder { CBaseHolder(TCSharedPointer<CJobBase> const &_pJob) : m_pJob(_pJob) {} TCSharedPointer<CJobBase> m_pJob; };
struct CDerivedContext : CBaseContext { int m_nExtra = 0; };
TCFuture<int> fg_ConstructedHolder(%s) {
  %s
  %s
  int &Result = pOwner->m_Result;
  return fg_CallSafe([&Result, Context = CBaseHolder(%s)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_BaseHeld(%s) {
  %s
  %s
  int &Result = pOwner->m_Result;
  return fg_CallSafe([&Result, Context = CDerivedContext{{%s, 1}, 2}]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_CastTaken(%s) {
  %s
  %s
  int &Result = pOwner->m_Result;
  return fg_CallSafe([&Result, pKeep = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_Accessor() {
  TCVector<TCSharedPointer<CJob>> Jobs;
  %s
  int &Result = Jobs.f_GetFirst()->m_Result;
  return fg_CallSafe([&Result, Kept = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_LookupBound(TCMap<int, TCSharedPointer<CJob>> &_Jobs, int _Key) {
  auto const &pJob = *_Jobs.f_FindEqual(_Key);
  %s
  int &Result = pJob->m_Result;
  return fg_CallSafe([&Result, pKeep = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_GetterBorrow() {
  CJobs Jobs{fg_MakeJob()};
  %s
  int &Result = Jobs.f_Job()->m_Result;
  return fg_CallSafe([&Result, Kept = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_SwappedIn(%s) {
  %s
  %s
  int &Result = pOwner->m_Result;
  TCSharedPointer<CJob> pKeep;
  fg_Swap(pKeep, %s);
  return fg_CallSafe([&Result, pKeep]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
struct CRecChild;
struct CRecNode { TCMap<int, CRecChild> m_Children; TCSharedPointer<CJob> m_pJob; };
struct CRecChild { TCMap<int, CRecNode> m_Children; };
TCFuture<int> fg_RecursiveNodes(TCMap<int, CRecNode> _Nodes) {
  int &Result = _Nodes[0].m_pJob->m_Result;
  return fg_CallSafe([&Result, Nodes = _Nodes]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_RecursiveChildren(TCMap<int, CRecChild> _Nodes) {
  int &Result = _Nodes[0].m_Children[0].m_pJob->m_Result;
  return fg_CallSafe([&Result, Nodes = _Nodes]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
struct CRecChildB;
struct CRecNodeB { TCMap<int, CRecChildB> m_Children; TCSharedPointer<CJob> m_pJob; };
struct CRecChildB { TCMap<int, CRecNodeB> m_Children; };
TCFuture<int> fg_RecursiveChildrenFirst(TCMap<int, CRecChildB> _Nodes) {
  int &Result = _Nodes[0].m_Children[0].m_pJob->m_Result;
  return fg_CallSafe([&Result, Nodes = _Nodes]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_RecursiveNodesSecond(TCMap<int, CRecNodeB> _Nodes) {
  int &Result = _Nodes[0].m_pJob->m_Result;
  return fg_CallSafe([&Result, Nodes = _Nodes]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// Reported: a constructor that doesn't keep the owner, a base member reset
// between the reference and the capture, a swap that moves the owner away
// from the captured slot, and the element erased between them.
struct CNoStore { CNoStore(TCSharedPointer<CJobBase> const &_pJob) {} TCSharedPointer<CJobBase> m_pJob; };
TCFuture<int> fg_NotStored(%s) {
  %s
  %s
  int &Result = pOwner->m_Result;
  return fg_CallSafe([&Result, Context = CNoStore(%s)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_BaseReset() {
  CDerivedContext Context{{fg_MakeJob(), 1}, 2};
  int &Result = Context.m_pJob->m_Result;
  Context.m_pJob = {};
  %s
  return fg_CallSafe([&Result, Kept = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_SwappedAway(%s) {
  %s
  %s
  int &Result = pOwner->m_Result;
  TCSharedPointer<CJob> pOther;
  fg_Swap(pOther, %s);
  return fg_CallSafe([&Result, pKeep = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_Erased(TCMap<int, TCSharedPointer<CJob>> &_Jobs, int _Key) {
  auto const &pJob = *_Jobs.f_FindEqual(_Key);
  %s
  int &Result = pJob->m_Result;
  _Jobs.f_Remove(_Key);
  return fg_CallSafe([&Result, pKeep = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// Reported: a record holding a pointer into a local.
TCFuture<int> fg_HolderOfLocal() {
  int Value = 0;
  CResultRef Ref{&Value};
  return fg_CallSafe([Ref]() -> TCFuture<int> { co_await fg_Work(); co_return *Ref.m_pResult; });
}
// Round 168: holders whose constructor assigns the owner in its body, takes
// it by a converting copy, from a member of a parameter, through another
// constructor it delegates to or a base's constructor, a factory that gives
// a holder made of it, records swapped and exchanged whole, an array made
// of owners, a lookup through a pointer, through an identity wrapper and
// through a local pointer, and the last element of a container moved whole,
// keep the owner; so does a sharing accessor of an explicit object.
struct CBodyKeep { CBodyKeep(TCSharedPointer<CJobBase> const &_pJob) { m_pJob = _pJob; } TCSharedPointer<CJobBase> m_pJob; };
struct CConvertKeep { CConvertKeep(TCSharedPointer<CJob> const &_pJob) : m_pJob(_pJob) {} TCSharedPointer<CJobBase> m_pJob; };
struct CFromContextKeep { CFromContextKeep(CBaseContext const &_Context) : m_pJob(_Context.m_pJob) {} TCSharedPointer<CJobBase> m_pJob; };
struct CDelegatingKeep {
  CDelegatingKeep(TCSharedPointer<CJobBase> const &_pJob) : CDelegatingKeep(_pJob, 0) {}
  CDelegatingKeep(TCSharedPointer<CJobBase> const &_pJob, int _ID) : m_pJob(_pJob), m_ID(_ID) {}
  TCSharedPointer<CJobBase> m_pJob;
  int m_ID;
};
struct CKeepBase { CKeepBase(TCSharedPointer<CJobBase> const &_pJob) : m_pJob(_pJob) {} TCSharedPointer<CJobBase> m_pJob; };
struct CKeepDerived : CKeepBase { CKeepDerived(TCSharedPointer<CJobBase> const &_pJob) : CKeepBase(_pJob) {} };
struct CMadeContext { TCSharedPointer<CJobBase> m_pJob; };
CMadeContext fg_MakeContext(TCSharedPointer<CJobBase> const &_pJob) { return CMadeContext{_pJob}; }
TCFuture<int> fg_BodyAssigned(%s) {
  %s
  %s
  int &Result = pOwner->m_Result;
  return fg_CallSafe([&Result, Keep = CBodyKeep(%s)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_ConvertedMember(%s) {
  %s
  %s
  int &Result = pOwner->m_Result;
  return fg_CallSafe([&Result, Keep = CConvertKeep(%s)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_FromParameterMember(%s) {
  %s
  %s
  CBaseContext Context{%s, 1};
  int &Result = pOwner->m_Result;
  return fg_CallSafe([&Result, Keep = CFromContextKeep(Context)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_Delegated(%s) {
  %s
  %s
  int &Result = pOwner->m_Result;
  return fg_CallSafe([&Result, Keep = CDelegatingKeep(%s)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_BaseConstructed(%s) {
  %s
  %s
  int &Result = pOwner->m_Result;
  return fg_CallSafe([&Result, Keep = CKeepDerived(%s)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_BaseConstructedLocal(%s) {
  %s
  %s
  CKeepDerived Keep{%s};
  int &Result = pOwner->m_Result;
  return fg_CallSafe([&Result, Keep]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_Factory(%s) {
  %s
  %s
  int &Result = pOwner->m_Result;
  return fg_CallSafe([&Result, Context = fg_MakeContext(%s)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_RecordsSwapped(%s) {
  %s
  %s
  CBaseContext Context{%s, 1};
  CBaseContext Other;
  int &Result = pOwner->m_Result;
  fg_Swap(Context, Other);
  return fg_CallSafe([&Result, Other]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_RecordExchanged(%s) {
  %s
  %s
  CBaseContext Context{%s, 1};
  int &Result = pOwner->m_Result;
  return fg_CallSafe([&Result, Kept = NMib::fg_Exchange(Context, CBaseContext{})]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_ArrayOfOwners(%s) {
  %s
  %s
  int &Result = pOwner->m_Result;
  TCSharedPointer<CJobBase> pJobs[2]{%s, fg_MakeJob()};
  return fg_CallSafe([&Result, pJobs]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_PointerLookup(TCMap<int, TCSharedPointer<CJob>> *_pJobs, int _Key) {
  auto const &pJob = *_pJobs->f_FindEqual(_Key);
  %s
  int &Result = pJob->m_Result;
  return fg_CallSafe([&Result, pKeep = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_WrappedLookup(TCMap<int, TCSharedPointer<CJob>> &_Jobs, int _Key) {
  auto const &pJob = fg_Const(*_Jobs.f_FindEqual(_Key));
  %s
  int &Result = pJob->m_Result;
  return fg_CallSafe([&Result, pKeep = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_LookupPointer(int _Key) {
  TCMap<int, TCSharedPointer<CJob>> Jobs;
  auto *ppJob = Jobs.f_FindEqual(_Key);
  %s
  int &Result = (*ppJob)->m_Result;
  return fg_CallSafe([&Result, Kept = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_LastElement(TCVector<TCSharedPointer<CJob>> &_Jobs) {
  %s
  int &Result = _Jobs.f_GetLast()->m_Result;
  return fg_CallSafe([&Result, Kept = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
namespace NExplicitShare {
template <typename t_CType>
struct TCSharedPointer {
  TCSharedPointer();
  TCSharedPointer(TCSharedPointer const &_Other);
  ~TCSharedPointer();
  t_CType *operator->() const;
  TCSharedPointer<t_CType const> f_ShareAsConst(this TCSharedPointer const &_This);
};
TCSharedPointer<CJob> fg_MakeShared();
}
TCFuture<int> fg_ExplicitShare(%s) {
  %s
  %s
  int const &Result = pOwner->m_Result;
  return fg_CallSafe([&Result, pKeep = %s.f_ShareAsConst()]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// Reported: a constructor that assigns the owner only on one branch, a base
// constructor that doesn't keep it, a record swapped into the captured one
// whose owner is then reset, and an element a local pointer points to whose
// container changes between.
struct CConditionalKeep { CConditionalKeep(TCSharedPointer<CJobBase> const &_pJob, bool _bKeep) { if (_bKeep) m_pJob = _pJob; } TCSharedPointer<CJobBase> m_pJob; };
struct CNoStoreBase { CNoStoreBase(TCSharedPointer<CJobBase> const &_pJob) {} TCSharedPointer<CJobBase> m_pJob; };
struct CNoStoreDerived : CNoStoreBase { CNoStoreDerived(TCSharedPointer<CJobBase> const &_pJob) : CNoStoreBase(_pJob) {} };
extern bool g_bKeep;
TCFuture<int> fg_ConditionallyAssigned(%s) {
  %s
  %s
  int &Result = pOwner->m_Result;
  return fg_CallSafe([&Result, Keep = CConditionalKeep(%s, g_bKeep)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_BaseNotStored(%s) {
  %s
  %s
  int &Result = pOwner->m_Result;
  return fg_CallSafe([&Result, Keep = CNoStoreDerived(%s)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_SwappedThenReset(%s) {
  %s
  %s
  CBaseContext Context{%s, 1};
  CBaseContext Other;
  int &Result = pOwner->m_Result;
  fg_Swap(Context, Other);
  Other.m_pJob = {};
  return fg_CallSafe([&Result, Other]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_PointedContainerChanged(int _Key) {
  TCMap<int, TCSharedPointer<CJob>> Jobs;
  auto *ppJob = Jobs.f_FindEqual(_Key);
  int &Result = (*ppJob)->m_Result;
  Jobs.f_Remove(_Key);
  %s
  return fg_CallSafe([&Result, Kept = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// Round 169: a constructor that stores a local copy of the owner, also
// moved, an inherited constructor, a constructor that throws on another
// path, a factory that returns a named local, and a helper that gives the
// owner back keep it.
void fg_PrepareJob(CJobBase &_Job);
struct CLocalCopyKeep { CLocalCopyKeep(TCSharedPointer<CJobBase> const &_pJob) { auto pKeep = _pJob; m_pJob = pKeep; } TCSharedPointer<CJobBase> m_pJob; };
struct CLocalMovedKeep { CLocalMovedKeep(TCSharedPointer<CJobBase> const &_pJob) { auto pPrepared = _pJob; fg_PrepareJob(*pPrepared); m_pJob = fg_Move(pPrepared); } TCSharedPointer<CJobBase> m_pJob; };
struct CInheritedKeep : CKeepBase { using CKeepBase::CKeepBase; };
struct CThrowingKeep { CThrowingKeep(TCSharedPointer<CJobBase> const &_pJob) { if (!g_bKeep) throw 1; m_pJob = _pJob; } TCSharedPointer<CJobBase> m_pJob; };
CMadeContext fg_MakeNamed(TCSharedPointer<CJobBase> const &_pJob) { CMadeContext Context{_pJob}; return Context; }
TCSharedPointer<CJobBase> fg_KeepJob(TCSharedPointer<CJobBase> const &_pJob) { return _pJob; }
TCFuture<int> fg_LocalCopyStored(%s) {
  %s
  %s
  int &Result = pOwner->m_Result;
  return fg_CallSafe([&Result, Keep = CLocalCopyKeep(%s)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_LocalMovedStored(%s) {
  %s
  %s
  int &Result = pOwner->m_Result;
  return fg_CallSafe([&Result, Keep = CLocalMovedKeep(%s)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_InheritedConstructor(%s) {
  %s
  %s
  int &Result = pOwner->m_Result;
  return fg_CallSafe([&Result, Keep = CInheritedKeep(%s)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_ThrowsOtherwise(%s) {
  %s
  %s
  int &Result = pOwner->m_Result;
  return fg_CallSafe([&Result, Keep = CThrowingKeep(%s)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_NamedFactory(%s) {
  %s
  %s
  int &Result = pOwner->m_Result;
  return fg_CallSafe([&Result, Context = fg_MakeNamed(%s)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_HelperGivesBack(%s) {
  %s
  %s
  int &Result = pOwner->m_Result;
  return fg_CallSafe([&Result, pKeep = fg_KeepJob(%s)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// Reported: a constructor that throws on one path and doesn't store it on
// the other, a local copy reset before it is stored, and an inherited
// constructor of a base that doesn't keep it.
struct CThrowNoStore { CThrowNoStore(TCSharedPointer<CJobBase> const &_pJob) { if (!g_bKeep) throw 1; } TCSharedPointer<CJobBase> m_pJob; };
struct CLocalResetKeep { CLocalResetKeep(TCSharedPointer<CJobBase> const &_pJob) { auto pKeep = _pJob; pKeep = {}; m_pJob = pKeep; } TCSharedPointer<CJobBase> m_pJob; };
struct CInheritedNoStore : CNoStoreBase { using CNoStoreBase::CNoStoreBase; };
TCFuture<int> fg_ThrowNoStore(%s) {
  %s
  %s
  int &Result = pOwner->m_Result;
  return fg_CallSafe([&Result, Keep = CThrowNoStore(%s)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_LocalReset(%s) {
  %s
  %s
  int &Result = pOwner->m_Result;
  return fg_CallSafe([&Result, Keep = CLocalResetKeep(%s)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_InheritedNoStore(%s) {
  %s
  %s
  int &Result = pOwner->m_Result;
  return fg_CallSafe([&Result, Keep = CInheritedNoStore(%s)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// Round 169, rest: a constructor keeping the owner in each element of an
// array member, sharing through a pointer to the owner, a getter given what
// holds the owner by a reference that isn't const that doesn't change it,
// and a string whose copies share its buffer (`sharedbuffer`) keep it.
struct CPairKeep { CPairKeep(TCSharedPointer<CJobBase> const &_pJob) : m_pJobs{_pJob, _pJob} {} TCSharedPointer<CJobBase> m_pJobs[2]; };
TCSharedPointer<CJobBase> &fg_KeptJob(CMadeContext &_Context) { return _Context.m_pJob; }
TCSharedPointer<CJobBase> &fg_ResetJob(CMadeContext &_Context) { _Context.m_pJob = {}; return _Context.m_pJob; }
struct CStrTraits_CStr {};
template <typename t_CTraits>
struct TCStr {
  TCStr();
  TCStr(TCStr const &_Other);
  TCStr(TCStr &&_Other);
  ~TCStr();
  char const *f_GetStr() const { return mp_pStr; }
  void f_AddStr(char const *_pText);
  char const *mp_pStr;
};
using CStr = TCStr<CStrTraits_CStr>;
CStr fg_ReadText();
TCFuture<int> fg_ArrayMemberMade(%s) {
  %s
  %s
  int &Result = pOwner->m_Result;
  return fg_CallSafe([&Result, Keep = CPairKeep(%s)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_SharedThroughPointer(%s) {
  %s
  %s
  auto *ppOwner = &%s;
  int &Result = (*ppOwner)->m_Result;
  return fg_CallSafe([&Result, pKeep = ppOwner->f_ShareAsConst()]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_FreeGetter(%s) {
  %s
  %s
  CMadeContext Context{%s};
  int &Result = fg_KeptJob(Context)->m_Result;
  return fg_CallSafe([&Result, Context]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_SharedBuffer(%s) {
  %s
  %s
  char const &First = *Text.f_GetStr();
  return fg_CallSafe([&First, Kept = %s]() -> TCFuture<int> { co_await fg_Work(); co_return First; });
}
// Reported: a getter that resets the owner between the reference and the
// capture, and the string changed between them, which can give it another
// buffer.
TCFuture<int> fg_ResettingGetter(%s) {
  %s
  %s
  CMadeContext Context{%s};
  int &Result = Context.m_pJob->m_Result;
  fg_ResetJob(Context);
  return fg_CallSafe([&Result, Context]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_SharedBufferChanged(%s) {
  %s
  %s
  char const &First = *Text.f_GetStr();
  Text.f_AddStr("x");
  return fg_CallSafe([&First, Kept = %s]() -> TCFuture<int> { co_await fg_Work(); co_return First; });
}
// Round 170: a holder's address given to a function that only reads
// through it, a getter that checks before it gives the owner back, and a
// member function that changes another member than the array holding the
// owner keep it.
int fg_ReadContext(CMadeContext const *_pContext) { return _pContext->m_pJob == nullptr ? 0 : 1; }
void fg_ClearContext(CMadeContext *_pContext) { _pContext->m_pJob = {}; }
struct CCheckedContext {
  TCSharedPointer<CJobBase> m_pJob;
  TCSharedPointer<CJobBase> const &f_Job() const { if (m_pJob == nullptr) throw 1; return m_pJob; }
  TCSharedPointer<CJobBase> &f_Reset() { if (m_pJob == nullptr) throw 1; m_pJob = {}; return m_pJob; }
};
struct CCountedBatch { TCSharedPointer<CJobBase> m_pJobs[2]; int m_nAccess = 0; void f_CountAccess() { ++m_nAccess; } };
TCFuture<int> fg_ReadOnlyAddress(%s) {
  %s
  %s
  CMadeContext Context{%s};
  int &Result = Context.m_pJob->m_Result;
  fg_ReadContext(&Context);
  return fg_CallSafe([&Result, Context]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_CheckedGetter(%s) {
  %s
  %s
  CCheckedContext Context{%s};
  int &Result = Context.f_Job()->m_Result;
  return fg_CallSafe([&Result, Context]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_CountedBatch(%s) {
  %s
  %s
  CCountedBatch Batch{{%s, fg_MakeJob()}};
  int &Result = Batch.m_pJobs[0]->m_Result;
  Batch.f_CountAccess();
  return fg_CallSafe([&Result, Batch]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// Reported: a function that clears the owner through the address, and a
// checked getter that resets it, between the reference and the capture.
TCFuture<int> fg_ClearedThroughAddress(%s) {
  %s
  %s
  CMadeContext Context{%s};
  int &Result = Context.m_pJob->m_Result;
  fg_ClearContext(&Context);
  return fg_CallSafe([&Result, Context]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_CheckedReset(%s) {
  %s
  %s
  CCheckedContext Context{%s};
  int &Result = Context.m_pJob->m_Result;
  Context.f_Reset();
  return fg_CallSafe([&Result, Context]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// Round 170: functions that give the owner a member of what they are given
// is, by reference through a pointer and by value through a reference or a
// pointer, a setter a constructor calls, and a tuple whose elements are in
// bases, copied whole, keep it.
TCSharedPointer<CJobBase> const &fg_JobThrough(CMadeContext const *_pContext) { return _pContext->m_pJob; }
TCSharedPointer<CJobBase> fg_JobOf(CMadeContext const &_Context) { return _Context.m_pJob; }
TCSharedPointer<CJobBase> fg_JobOfPointer(CMadeContext const *_pContext) { return _pContext->m_pJob; }
TCSharedPointer<CJobBase> const &fg_ClearAndGet(CMadeContext *_pContext) { _pContext->m_pJob = {}; return _pContext->m_pJob; }
struct CSetKeep {
  TCSharedPointer<CJobBase> m_pJob;
  CSetKeep(TCSharedPointer<CJobBase> const &_pJob) { f_Set(_pJob); }
  void f_Set(TCSharedPointer<CJobBase> const &_pJob) { m_pJob = _pJob; }
};
struct CMaybeSetKeep {
  TCSharedPointer<CJobBase> m_pJob;
  CMaybeSetKeep(TCSharedPointer<CJobBase> const &_pJob, bool _bKeep) { f_Set(_pJob, _bKeep); }
  void f_Set(TCSharedPointer<CJobBase> const &_pJob, bool _bKeep) { if (_bKeep) m_pJob = _pJob; }
};
template <int t_Index, typename t_CType> struct TCTupleLeaf { t_CType m_Value; t_CType &f_Get() { return m_Value; } };
template <typename t_CFirst, typename t_CSecond> struct TCTupleImp : TCTupleLeaf<0, t_CFirst>, TCTupleLeaf<1, t_CSecond> {};
template <typename t_CFirst, typename t_CSecond> struct TCLeafTuple { TCTupleImp<t_CFirst, t_CSecond> m_Base; };
template <typename t_CFirst, typename t_CSecond>
t_CFirst &fg_GetFirst(TCLeafTuple<t_CFirst, t_CSecond> &_Tuple) { return static_cast<TCTupleLeaf<0, t_CFirst> &>(_Tuple.m_Base).f_Get(); }
TCFuture<int> fg_JobThroughPointer(%s) {
  %s
  %s
  CMadeContext Context{%s};
  int &Result = Context.m_pJob->m_Result;
  auto pKeep = fg_JobThrough(&Context);
  return fg_CallSafe([&Result, pKeep]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_JobThroughLocalPointer(%s) {
  %s
  %s
  CMadeContext Context{%s};
  CMadeContext const *pContext = &Context;
  int &Result = pContext->m_pJob->m_Result;
  return fg_CallSafe([&Result, pJob = fg_JobThrough(pContext)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_JobByValue(%s) {
  %s
  %s
  CMadeContext Context{%s};
  int &Result = Context.m_pJob->m_Result;
  auto pKeep = fg_JobOf(Context);
  return fg_CallSafe([&Result, pKeep]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_JobByValuePointer(%s) {
  %s
  %s
  CMadeContext Context{%s};
  int &Result = Context.m_pJob->m_Result;
  auto pKeep = fg_JobOfPointer(&Context);
  return fg_CallSafe([&Result, pKeep]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_ConstructorSetter(%s) {
  %s
  %s
  int &Result = pOwner->m_Result;
  return fg_CallSafe([&Result, Keep = CSetKeep(%s)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_LeafTuple(%s) {
  %s
  %s
  TCLeafTuple<TCSharedPointer<CJobBase>, int> Jobs{{{%s}, {0}}};
  int &Result = fg_GetFirst(Jobs)->m_Result;
  return fg_CallSafe([&Result, Jobs]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// Reported: a function that clears the owner through the pointer before it
// gives it back, a setter that keeps it only on one branch, and the tuple's
// element reset between the reference and the capture.
TCFuture<int> fg_ClearedAndGiven(%s) {
  %s
  %s
  CMadeContext Context{%s};
  int &Result = Context.m_pJob->m_Result;
  auto pKeep = fg_ClearAndGet(&Context);
  return fg_CallSafe([&Result, pKeep]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_SetterOnBranch(%s) {
  %s
  %s
  int &Result = pOwner->m_Result;
  return fg_CallSafe([&Result, Keep = CMaybeSetKeep(%s, true)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_LeafTupleReset(%s) {
  %s
  %s
  TCLeafTuple<TCSharedPointer<CJobBase>, int> Jobs{{{%s}, {0}}};
  int &Result = fg_GetFirst(Jobs)->m_Result;
  fg_GetFirst(Jobs) = {};
  return fg_CallSafe([&Result, Jobs]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// Round 172: an explicit-object setter a constructor calls, a getter by
// value of the receiver's member, a fluent setter, a read-only helper given
// a pointer alias, an owner reached through what an owner owns, an array
// record and a container made of an initializer list keep it.
namespace std {
template <typename t_CElement> class initializer_list {
  t_CElement const *mp_pBegin;
  unsigned long mp_Size;
};
} // namespace std
struct CR172Context {
  TCSharedPointer<CJobBase> m_pJob;
  int m_ID = 0;
  TCSharedPointer<CJobBase> f_Job() const { return m_pJob; }
  CR172Context &f_SetID(int _ID) { m_ID = _ID; return *this; }
  CR172Context &f_Reset() { m_pJob = {}; return *this; }
};
TCSharedPointer<CR172Context> fg_R172Share(CR172Context &&_Context);
int fg_R172ID(CR172Context *_pContext) { return _pContext->m_ID; }
TCSharedPointer<CJobBase> const &fg_R172Job(CR172Context *_pContext) { return _pContext->m_pJob; }
struct CR172Set {
  TCSharedPointer<CJobBase> m_pJob;
  CR172Set(TCSharedPointer<CJobBase> const &_pJob) { f_Set(_pJob); }
  void f_Set(this CR172Set &_This, TCSharedPointer<CJobBase> const &_pJob) { _This.m_pJob = _pJob; }
};
struct CR172Reset {
  TCSharedPointer<CJobBase> m_pJob;
  CR172Reset(TCSharedPointer<CJobBase> const &_pJob) { f_Set(_pJob); }
  void f_Set(this CR172Reset &_This, TCSharedPointer<CJobBase> const &_pJob) { _This.m_pJob = _pJob; _This.m_pJob = {}; }
};
template <typename t_CType, int t_Size> struct TCR172Array {
  t_CType m_Elements[t_Size];
  t_CType &operator[](int _Index) { return m_Elements[_Index]; }
};
template <typename t_CType> struct TCR172List {
  TCR172List(std::initializer_list<t_CType> _Values);
  TCR172List(TCR172List const &_Other);
  t_CType *mp_pData;
};
TCFuture<int> fg_R172ExplicitSetter(%s) {
  %s
  %s
  int &Result = pOwner->m_Result;
  return fg_CallSafe([&Result, Keep = CR172Set(%s)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_R172ValueGetter(%s) {
  %s
  %s
  CR172Context Context{%s, 1};
  int &Result = Context.m_pJob->m_Result;
  return fg_CallSafe([&Result, pKeep = Context.f_Job()]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_R172Fluent(%s) {
  %s
  %s
  CR172Context Context{%s, 1};
  int &Result = Context.m_pJob->m_Result;
  Context.f_SetID(2).f_SetID(3);
  return fg_CallSafe([&Result, Context]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_R172PointerAlias(%s) {
  %s
  %s
  CR172Context Context{%s, 1};
  auto *pContext = &Context;
  int &Result = pContext->m_pJob->m_Result;
  fg_R172ID(pContext);
  return fg_CallSafe([&Result, pKeep = fg_R172Job(pContext)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_R172Owned(%s) {
  %s
  %s
  TCSharedPointer<CR172Context> pContext = fg_R172Share(CR172Context{%s, 1});
  int &Result = pContext->m_pJob->m_Result;
  return fg_CallSafe([&Result, pJob = pContext->m_pJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_R172ArrayRecord(%s) {
  %s
  %s
  TCR172Array<TCSharedPointer<CJobBase>, 2> Jobs{{%s, fg_MakeJob()}};
  int &Result = Jobs[0]->m_Result;
  return fg_CallSafe([&Result, Jobs]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_R172List(%s) {
  %s
  %s
  int &Result = pOwner->m_Result;
  TCR172List<TCSharedPointer<CJobBase>> Jobs{%s};
  return fg_CallSafe([&Result, Jobs]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// Reported: a pointer alias pointed at a context of a new job between the
// reference and the capture, an explicit-object setter that resets the owner,
// a fluent setter that writes it, and an element of the array record reset.
TCFuture<int> fg_R172AliasReassigned(%s) {
  %s
  %s
  CR172Context Context{%s, 1};
  CR172Context Other{TCSharedPointer<CJob>(new CJob), 2};
  auto *pContext = &Context;
  int &Result = Context.m_pJob->m_Result;
  pContext = &Other;
  return fg_CallSafe([&Result, pKeep = fg_R172Job(pContext)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_R172ExplicitSetterResets(%s) {
  %s
  %s
  int &Result = pOwner->m_Result;
  return fg_CallSafe([&Result, Keep = CR172Reset(%s)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_R172FluentWrites(%s) {
  %s
  %s
  CR172Context Context{%s, 1};
  int &Result = Context.m_pJob->m_Result;
  Context.f_SetID(2).f_Reset();
  return fg_CallSafe([&Result, Context]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_R172ArrayRecordReset(%s) {
  %s
  %s
  TCR172Array<TCSharedPointer<CJobBase>, 2> Jobs{{%s, fg_MakeJob()}};
  int &Result = Jobs[0]->m_Result;
  Jobs[0] = {};
  return fg_CallSafe([&Result, Jobs]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// Round 173: a qualified call of a virtual member function, which no
// override can take, that doesn't change the owner; owners nested in
// owners, a member owner read through an owner and taken itself, and one a
// const member of what a captured owner owns keeps; an owner a scope exit
// releases where its scope ends, after the capture. Reported: an unqualified
// call an override that resets the owner can take, the nested owner reset
// between the reference and the capture, and a scope exit that ends before
// the capture.
struct CR173Virtual {
  TCSharedPointer<CJobBase> m_pJob;
  int m_ID = 0;
  CR173Virtual(TCSharedPointer<CJobBase> _pJob) : m_pJob(_pJob) {}
  virtual void f_Observe() { ++m_ID; }
};
struct CR173VirtualReset : CR173Virtual {
  using CR173Virtual::CR173Virtual;
  void f_Observe() override { m_pJob = {}; }
};
struct CR173Request { TCSharedPointer<CJobBase> m_pJob; };
struct CR173ConstRequest { TCSharedPointer<CJobBase> const m_pJob; };
struct CR173Batch { TCSharedPointer<CR173Request> m_pRequest; };
TCSharedPointer<CR173Request> fg_R173Share(CR173Request &&_Request);
TCSharedPointer<CR173ConstRequest> fg_R173ShareConst(CR173ConstRequest &&_Request);
TCFuture<int> fg_R173Qualified(%s) {
  %s
  %s
  CR173Virtual Context{%s};
  CR173Virtual &rContext = Context;
  int &Result = Context.m_pJob->m_Result;
  rContext.CR173Virtual::f_Observe();
  return fg_CallSafe([&Result, Context]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_R173NestedTaken(%s) {
  %s
  %s
  CR173Batch Batch{fg_R173Share(CR173Request{%s})};
  int &Result = Batch.m_pRequest->m_pJob->m_Result;
  return fg_CallSafe([&Result, pJob = Batch.m_pRequest->m_pJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_R173ConstNested(%s) {
  %s
  %s
  TCSharedPointer<CR173ConstRequest> pRequest = fg_R173ShareConst(CR173ConstRequest{%s});
  int &Result = pRequest->m_pJob->m_Result;
  return fg_CallSafe([&Result, pRequest]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_R173DeferredRelease(%s) {
  %s
  %s
  TCSharedPointer<CJobBase> pKeep = %s;
  int &Result = pKeep->m_Result;
  auto Cleanup = g_OnScopeExit / [&] { pKeep.f_Clear(); };
  return fg_CallSafe([&Result, pKeep]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_R173Unqualified(%s) {
  %s
  %s
  CR173Virtual Context{%s};
  CR173Virtual &rContext = Context;
  int &Result = Context.m_pJob->m_Result;
  rContext.f_Observe();
  return fg_CallSafe([&Result, Context]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_R173NestedReset(%s) {
  %s
  %s
  CR173Batch Batch{fg_R173Share(CR173Request{%s})};
  int &Result = Batch.m_pRequest->m_pJob->m_Result;
  Batch.m_pRequest->m_pJob = fg_MakeJob();
  return fg_CallSafe([&Result, pJob = Batch.m_pRequest->m_pJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_R173EarlyRelease(%s) {
  %s
  %s
  TCSharedPointer<CJobBase> pKeep = %s;
  int &Result = pKeep->m_Result;
  {
    auto Cleanup = g_OnScopeExit / [&] { pKeep.f_Clear(); };
  }
  return fg_CallSafe([&Result, pKeep]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// Round 200: an owner reached only through a unique owner that the closure
// takes is changed only by the code of the function and of the closure, so
// the unique owner keeps it. Reported: the same through a shared owner,
// which others can change, and the nested owner reset by the closure or
// between the reference and the capture.
struct CR200Context { TCUniquePointer<CJob> m_pJob{new CJob}; };
TCSharedPointer<CR200Context> fg_R200Share();
void fg_R200Reset(CR200Context &_Context);
void fg_R200Read(CR200Context const &_Context);
TCFuture<int> fg_R200UniqueNested(%s) {
  %s
  %s
  int &Result = pContext->m_pJob->m_Result;
  fg_R200Read(*pContext);
  return fg_CallSafe([&Result, pKeep = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result + pKeep->m_pJob->m_Result; });
}
TCFuture<int> fg_R200SharedNested(%s) {
  %s
  %s
  int &Result = pContext->m_pJob->m_Result;
  return fg_CallSafe([&Result, pKeep = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_R200UniqueNestedResetInClosure(%s) {
  %s
  %s
  int &Result = pContext->m_pJob->m_Result;
  return fg_CallSafe([&Result, pKeep = %s]() -> TCFuture<int> { fg_R200Reset(*pKeep); co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_R200UniqueNestedResetBefore(%s) {
  %s
  %s
  int &Result = pContext->m_pJob->m_Result;
  fg_R200Reset(*pContext);
  return fg_CallSafe([&Result, pKeep = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
''' % (
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    signature('TCUniquePointer<CJob>', 'pOwner'),
    owner('TCUniquePointer<CJob>', 'pOwner', 'TCUniquePointer<CJob>(new CJob)'),
    alias('pOwner'),
    take('unique', 'pOwner', 'CJobBase'),
    signature('TCVector<int>', 'pOwner'),
    owner('TCVector<int>', 'pOwner', 'TCVector<int>()'),
    alias('pOwner'),
    take('buffer', 'pOwner', 'int'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    copyTake('pOwner'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    signature('TCVector<int>', 'pOwner'),
    owner('TCVector<int>', 'pOwner', 'TCVector<int>()'),
    alias('pOwner'),
    take('buffer', 'pOwner', 'int'),
    signature('TCUniquePointer<CJob>', 'pOwner'),
    owner('TCUniquePointer<CJob>', 'pOwner', 'TCUniquePointer<CJob>(new CJob)'),
    signature('TCUniquePointer<CJob>', 'pOwner'),
    owner('TCUniquePointer<CJob>', 'pOwner', 'TCUniquePointer<CJob>(new CJob)'),
    alias('pOwner'),
    alias('pThis'),
    take('shared', 'pThis', 'CJobBase'),
    alias('pThis'),
    take('shared', 'pThis', 'CJobBase'),
    aliasOf('rMember', 'mp_pOwner'),
    takeOf('shared', 'mp_pOwner', 'rMember'),
    aliasOf('rMember', 'mp_Holder.m_pOwner'),
    takeOf('shared', 'mp_Holder.m_pOwner', 'rMember'),
    aliasOf('rMember', '_Self.mp_pOwner'),
    takeOf('shared', '_Self.mp_pOwner', 'rMember'),
    aliasOf('rMember', 'Holder.m_pOwner'),
    takeOf('shared', 'Holder.m_pOwner', 'rMember'),
    aliasOf('rBound', 'pBound'),
    takeOf('shared', 'pBound', 'rBound'),
    aliasOf('rThis', 'pThis'),
    takeOf('shared', 'pThis', 'rThis'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'TCSharedPointer<CJob>()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    signature('TCVector<int>', 'pOwner'),
    owner('TCVector<int>', 'pOwner', 'TCVector<int>()'),
    alias('pOwner'),
    take('buffer', 'pOwner', 'int'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    copyTake('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    copyTake('pOwner'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    aliasOf('rMember', 'mp_pOwner'),
    takeOf('shared', 'mp_pOwner', 'rMember'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'TCSharedPointer<CJob>()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'TCSharedPointer<CJob>()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    aliasOf('rContext', 'Context'),
    takeRecord('Context', 'rContext'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    aliasOf('rBound', 'pTupleBound'),
    takeOf('shared', 'pTupleBound', 'rBound'),
    aliasOf('rElement', '_Jobs[0]'),
    takeOf('shared', '_Jobs[0]', 'rElement'),
    aliasOf('rGot', '_Jobs.f_Job()'),
    takeOf('shared', '_Jobs.f_Job()', 'rGot'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    aliasOf('rElement', '_Jobs[0]'),
    takeOf('shared', '_Jobs[0]', 'rElement'),
    aliasOf('rMember', 'mp_pOwner'),
    takeOf('shared', 'mp_pOwner', 'rMember'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    aliasOf('rHeld', 'pHeld->m_pJob'),
    takeOf('shared', 'pHeld->m_pJob', 'rHeld'),
    aliasOf('rFound', 'pFound->m_pJob'),
    takeOf('shared', 'pFound->m_pJob', 'rFound'),
    aliasOf('rSelf', 'pSelf->mp_pOwner'),
    takeOf('shared', 'pSelf->mp_pOwner', 'rSelf'),
    aliasOf('rMember', 'mp_pOwner'),
    takeOf('shared', 'mp_pOwner', 'rMember'),
    aliasOf('rMember', 'mp_Held.m_pJob'),
    takeOf('shared', 'mp_Held.m_pJob', 'rMember'),
    aliasOf('rMember', 'mp_pOwner'),
    takeOf('shared', 'mp_pOwner', 'rMember'),
    aliasOf('rMember', 'mp_pOwner'),
    takeOf('shared', 'mp_pOwner', 'rMember'),
    aliasOf('rHeld', 'Held.m_pJob'),
    takeOf('shared', 'Held.m_pJob', 'rHeld'),
    aliasOf('rCopy', 'Copy'),
    takeRecord('Copy', 'rCopy'),
    aliasOf('rCopy', 'Copy'),
    takeRecord('Copy', 'rCopy'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    aliasOf('rHeld', 'Held.m_pJob'),
    takeOf('shared', 'Held.m_pJob', 'rHeld'),
    aliasOf('rCopy', 'Copy'),
    takeRecord('Copy', 'rCopy'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    aliasOf('rTemp', 'pTemp'),
    takeOf('shared', 'pTemp', 'rTemp'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    aliasOf('rJobs', 'Jobs'),
    takeRecord('Jobs', 'rJobs'),
    aliasOf('rJobs', 'Jobs'),
    takeOf('unique', 'Jobs', 'rJobs'),
    aliasOf('rJobs', 'pJobs'),
    takeArray('pJobs', 'rJobs'),
    aliasOf('rBatch', 'Batch'),
    takeRecord('Batch', 'rBatch'),
    aliasOf('rElement', 'pJobs[0]'),
    takeOf('shared', 'pJobs[0]', 'rElement'),
    aliasOf('rJob', 'pJob'),
    takeOf('shared', 'pJob', 'rJob'),
    aliasOf('rJob', 'pJob'),
    takeOf('shared', 'pJob', 'rJob'),
    aliasOf('rJob', 'pJob'),
    takeOf('shared', 'pJob', 'rJob'),
    aliasOf('rJob', 'pJob'),
    takeOf('shared', 'pJob', 'rJob'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    aliasOf('rJobs', 'Jobs'),
    takeRecord('Jobs', 'rJobs'),
    aliasOf('rJobs', 'Jobs'),
    takeRecord('Jobs', 'rJobs'),
    aliasOf('rJob', 'pJob'),
    takeOf('shared', 'pJob', 'rJob'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    aliasOf('rMoved', 'Moved'),
    takeRecord('Moved', 'rMoved'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    takeCast('pOwner'),
    aliasOf('rJobs', 'Jobs'),
    takeRecord('Jobs', 'rJobs'),
    aliasOf('rJob', 'pJob'),
    takeOf('shared', 'pJob', 'rJob'),
    aliasOf('rJobs', 'Jobs'),
    takeRecord('Jobs', 'rJobs'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    sourceOf('pOwner'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    aliasOf('rContext', 'Context'),
    takeRecord('Context', 'rContext'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    sourceOf('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    aliasOf('rJob', 'pJob'),
    takeOf('shared', 'pJob', 'rJob'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    takeSame('pOwner'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    copyTake('pOwner'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    copyTake('pOwner'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    copyTake('pOwner'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    copyTake('pOwner'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    aliasOf('rJob', 'pJob'),
    takeOf('shared', 'pJob', 'rJob'),
    aliasOf('rJob', 'pJob'),
    takeOf('shared', 'pJob', 'rJob'),
    aliasOf('rJobs', 'Jobs'),
    takeRecord('Jobs', 'rJobs'),
    aliasOf('rJobs', '_Jobs'),
    takeOf('unique', '_Jobs', 'rJobs'),
    signature('NExplicitShare::TCSharedPointer<CJob>', 'pOwner'),
    owner('NExplicitShare::TCSharedPointer<CJob>', 'pOwner', 'NExplicitShare::fg_MakeShared()'),
    alias('pOwner'),
    sourceOf('pOwner'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    copyTake('pOwner'),
    aliasOf('rJobs', 'Jobs'),
    takeRecord('Jobs', 'rJobs'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    sourceOf('pOwner'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    signature('CStr', 'Text'),
    owner('CStr', 'Text', 'fg_ReadText()'),
    alias('Text'),
    take('sharedbuffer', 'Text', None),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    signature('CStr', 'Text'),
    owner('CStr', 'Text', 'fg_ReadText()'),
    alias('Text'),
    take('sharedbuffer', 'Text', None),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    signature('TCSharedPointer<CJob>', 'pOwner'),
    owner('TCSharedPointer<CJob>', 'pOwner', 'fg_MakeJob()'),
    alias('pOwner'),
    take('shared', 'pOwner', 'CJobBase'),
    signature('TCUniquePointer<CR200Context>', 'pContext'),
    owner('TCUniquePointer<CR200Context>', 'pContext', 'TCUniquePointer<CR200Context>(new CR200Context)'),
    alias('pContext'),
    take('unique', 'pContext', 'CR200Context'),
    signature('TCSharedPointer<CR200Context>', 'pContext'),
    owner('TCSharedPointer<CR200Context>', 'pContext', 'fg_R200Share()'),
    alias('pContext'),
    take('shared', 'pContext', 'CR200Context'),
    signature('TCUniquePointer<CR200Context>', 'pContext'),
    owner('TCUniquePointer<CR200Context>', 'pContext', 'TCUniquePointer<CR200Context>(new CR200Context)'),
    alias('pContext'),
    take('unique', 'pContext', 'CR200Context'),
    signature('TCUniquePointer<CR200Context>', 'pContext'),
    owner('TCUniquePointer<CR200Context>', 'pContext', 'TCUniquePointer<CR200Context>(new CR200Context)'),
    alias('pContext'),
    take('unique', 'pContext', 'CR200Context'),
)
print(Lines, end='')
