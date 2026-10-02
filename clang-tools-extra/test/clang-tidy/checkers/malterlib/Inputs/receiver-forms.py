# Writes the same code with the member functions the checks recognize as
# getters or as what acts on their object in one of the forms a receiver
# takes: `implicit`, an implicit object, `f_Get() const`; `explicit`, an
# explicit object parameter of the class, `f_Get(this CType const &_This)`;
# `forwarding`, a deduced one, `f_Get(this tf_CThis &&_This)`. Each form
# keeps every line where it is, so that what the checks report of each is
# the same, line for line.
import sys

Form = sys.argv[1]
if Form not in ('implicit', 'explicit', 'forwarding'):
    sys.exit('Unknown form: ' + Form)


def method(Class, Result, Name, Body, Const=True, Parameters=''):
    # One line: the declaration and the body, where `@` is the object's
    # member access, `m_Value` or `_This.m_Value`, and `$` the object,
    # `*this` or `_This`.
    if Form == 'implicit':
        return '%s %s(%s)%s { %s }' % (Result, Name, Parameters, ' const' if Const else '',
                                        Body.replace('@', '').replace('$', '*this'))
    Rest = (', ' + Parameters) if Parameters else ''
    Body = Body.replace('@', '_This.').replace('$', '_This')
    if Form == 'explicit':
        Object = 'this %s%s &_This' % (Class, ' const' if Const else '')
        return '%s %s(%s%s) { %s }' % (Result, Name, Object, Rest, Body)
    return 'template <typename tf_CThis> %s %s(this tf_CThis &&_This%s) { %s }' % (
        Result, Name, Rest, Body)


Lines = '''#include "malterlib-runs-to-completion.h"
using namespace NMib::NConcurrency;
using umint = unsigned long;
TCFuture<int> fg_Work();
template <typename t_CType> int fg_GetLowestBitSetNoZero(t_CType const &_Value);
// no-zero-bit-scan: a getter of the bits, one of their value, and what
// clears them.
struct CBits {
  umint m_Bits;
  %s
  %s
  %s
};
int fg_Guarded(CBits &_Bits) {
  if (_Bits.f_Get())
    return fg_GetLowestBitSetNoZero(_Bits.f_Get());
  return -1;
}
int fg_Cleared(CBits &_Bits) {
  if (!_Bits.f_Get())
    return -1;
  _Bits.f_Clear();
  return fg_GetLowestBitSetNoZero(_Bits.f_Get());
}
// Read through a helper given it by a reference that isn't const, which
// only calls the getter of the value, whose object isn't const either.
void fg_Observe(CBits &_Bits) { _Bits.f_Read(); }
int fg_Observed(CBits &_Bits) { if (!_Bits.m_Bits) return -1; fg_Observe(_Bits); return fg_GetLowestBitSetNoZero(_Bits.m_Bits); }
// coroutine-reference-parameter: what a view holds itself, and what it
// refers to.
struct CFormStrSpan {
  char const *m_pStr;
  umint m_Length;
  %s
  %s
};
TCFuture<umint> fg_Length(CFormStrSpan _Text) { co_await fg_Work(); co_return _Text.f_GetLen(); }
TCFuture<char> fg_First(CFormStrSpan _Text) { co_await fg_Work(); co_return *_Text.f_GetStr(); }
// coroutine-reference-capture: a pointer into a local, and one elsewhere.
int g_Values[4];
struct CBuffer {
  int m_Values[4];
  %s
  %s
};
TCFuture<int> fg_Local() {
  CBuffer Buffer;
  return fg_CallSafe([pData = Buffer.f_GetArray()]() -> TCFuture<int> { co_await fg_Work(); co_return *pData; });
}
TCFuture<int> fg_Global() {
  CBuffer Buffer;
  return fg_CallSafe([pData = Buffer.f_GetGlobal()]() -> TCFuture<int> { co_await fg_Work(); co_return *pData; });
}
// coroutine-unsafe-this: a pointer into the object, and a copy of a value.
struct CState {
  int m_Value = 0;
  %s
  %s
  TCFuture<int> f_Pointer() {
    return fg_CallSafe([pValue = f_Get()]() -> TCFuture<int> { co_await fg_Work(); co_return *pValue; });
  }
  TCFuture<int> f_Copied() {
    return fg_CallSafe([Value = f_Copy()]() -> TCFuture<int> { co_await fg_Work(); co_return Value; });
  }
};
// runs-to-completion: what lives with the object, and what doesn't.
struct CEntry {
  int m_Value;
};
struct COptional {
  CEntry m_Value;
  CEntry *m_pOther;
  %s
  %s
};
DMibRunsToCompletion TCFuture<int> fg_Read(CEntry const &_Value);
DMibRunsToCompletion TCFuture<int> fg_ReadAll(COptional &_State) {
  TCFutureVector<int> Results;
  auto Join = co_await fg_AsyncDestroy([&]() -> TCFuture<int> { co_await fg_AllDone(Results); co_return 0; });
  fg_Read(_State.f_Get()) > Results;
  fg_Read(_State.f_GetOther()) > Results;
  co_await fg_Move(Join);
  co_return 0;
}
DMibRunsToCompletion TCFuture<int> fg_ReadLocal() {
  TCFutureVector<int> Results;
  auto Join = co_await fg_AsyncDestroy([&]() -> TCFuture<int> { co_await fg_AllDone(Results); co_return 0; });
  {
    COptional State{};
    fg_Read(State.f_Get()) > Results;
  }
  co_await fg_Move(Join);
  co_return 0;
}
// coroutine-actor-reference: a member of an entry, and of a member.
struct CValue {
  int m_Value;
  %s
};
template <typename t_CKey, typename t_CValue>
struct TCMap {
  t_CValue &operator[](t_CKey _Key);
};
struct CApp : CActor {
  TCMap<int, CValue> mp_Items;
  CValue m_Fixed;
  TCFuture<int> f_Entry() {
    int &Value = mp_Items[1].f_Value();
    co_await fg_Work();
    co_return Value;
  }
  TCFuture<int> f_Fixed() {
    int &Value = m_Fixed.f_Value();
    co_await fg_Work();
    co_return Value;
  }
};
// coroutine-reference-capture: what the summaries of member functions tell
// of an owner, a setter a constructor calls, a getter by value and by
// reference, a factory of a holder, a fluent setter that gives its object
// back and one that writes the owner, and a setter that resets it.
struct CJob { int m_Result = 0; };
DMibReturnsNew TCSharedPointer<CJob> fg_MakeJob();
struct CKeepSet {
  TCSharedPointer<CJob> m_pJob;
  CKeepSet(TCSharedPointer<CJob> const &_pJob) { f_Set(_pJob); }
  %s
};
struct CKeepReset {
  TCSharedPointer<CJob> m_pJob;
  CKeepReset(TCSharedPointer<CJob> const &_pJob) { f_Set(_pJob); }
  %s
};
struct CJobContext {
  TCSharedPointer<CJob> m_pJob;
  int m_ID = 0;
  %s
  %s
  %s
  %s
  %s
  %s
};
TCFuture<int> fg_Setter(TCSharedPointer<CJob> const &_pJob) {
  int &Result = _pJob->m_Result;
  return fg_CallSafe([&Result, Keep = CKeepSet(_pJob)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_SetterResets(TCSharedPointer<CJob> const &_pJob) {
  int &Result = _pJob->m_Result;
  return fg_CallSafe([&Result, Keep = CKeepReset(_pJob)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_ValueGetter() {
  CJobContext Context{fg_MakeJob(), 1};
  int &Result = Context.m_pJob->m_Result;
  return fg_CallSafe([&Result, pKeep = Context.f_Job()]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_ReferenceGetter() {
  CJobContext Context{fg_MakeJob(), 1};
  int &Result = Context.f_JobRef()->m_Result;
  return fg_CallSafe([&Result, pKeep = Context.f_JobRef()]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_Factory() {
  CJobContext Context{fg_MakeJob(), 1};
  int &Result = Context.m_pJob->m_Result;
  return fg_CallSafe([&Result, Keep = Context.f_Copy()]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_Fluent() {
  CJobContext Context{fg_MakeJob(), 1};
  int &Result = Context.m_pJob->m_Result;
  Context.f_SetID(2).f_SetID(3);
  return fg_CallSafe([&Result, Context]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_FluentWrites() {
  CJobContext Context{fg_MakeJob(), 1};
  int &Result = Context.m_pJob->m_Result;
  Context.f_Reset();
  return fg_CallSafe([&Result, Context]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_FluentChainWrites() {
  CJobContext Context{fg_MakeJob(), 1};
  int &Result = Context.m_pJob->m_Result;
  Context.f_SetID(2).m_pJob = {};
  return fg_CallSafe([&Result, Context]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCFuture<int> fg_ReadOnly() {
  CJobContext Context{fg_MakeJob(), 1};
  int &Result = Context.m_pJob->m_Result;
  Context.f_ID();
  return fg_CallSafe([&Result, Context]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
// coroutine-unsafe-this: a copy of a member of the object, whose pointer
// member borrows the object: reported (receiver-forms.cpp).
struct CCopiedRequest { int m_ID = 0; int *m_pData = nullptr; };
struct CCopiedState {
  int m_Value = 0;
  CCopiedRequest m_Request{1, &m_Value};
  %s
  %s
};
TCFuture<int> fg_Copied(CCopiedState &_State) {
  _State.f_Data();
  return _State.f_ID();
}
// runs-to-completion: a pointer member that states that only its class
// points it (DMibSetByOwnClass), which it never does, refers to nothing.
struct CKnownOptional {
  CEntry m_Value;
  DMibSetByOwnClass CEntry *m_pOther = nullptr;
  CEntry &f_GetOther() { return *m_pOther; }
};
DMibRunsToCompletion TCFuture<int> fg_ReadKnown(CKnownOptional &_State) {
  TCFutureVector<int> Results;
  auto Join = co_await fg_AsyncDestroy([&]() -> TCFuture<int> { co_await fg_AllDone(Results); co_return 0; });
  fg_Read(_State.f_GetOther()) > Results;
  co_await fg_Move(Join);
  co_return 0;
}
''' % (
    method('CBits', 'umint &', 'f_Get', 'return @m_Bits;', Const=False),
    method('CBits', 'umint', 'f_Read', 'return @m_Bits;', Const=False),
    method('CBits', 'void', 'f_Clear', '@m_Bits = 0;', Const=False),
    method('CFormStrSpan', 'umint', 'f_GetLen', 'return @m_Length;'),
    method('CFormStrSpan', 'char const *', 'f_GetStr', 'return @m_pStr;'),
    method('CBuffer', 'int *', 'f_GetArray', 'return @m_Values;', Const=False),
    method('CBuffer', 'int *', 'f_GetGlobal', 'return g_Values;', Const=False),
    method('CState', 'int *', 'f_Get', 'return &@m_Value;', Const=False),
    method('CState', 'int', 'f_Copy', 'return @m_Value;'),
    method('COptional', 'CEntry &', 'f_Get', 'return @m_Value;', Const=False),
    method('COptional', 'CEntry &', 'f_GetOther', 'return *@m_pOther;', Const=False),
    method('CValue', 'int &', 'f_Value', 'return @m_Value;', Const=False),
    method('CKeepSet', 'void', 'f_Set', '@m_pJob = _pJob;', Const=False,
           Parameters='TCSharedPointer<CJob> const &_pJob'),
    method('CKeepReset', 'void', 'f_Set', '@m_pJob = _pJob; @m_pJob = {};', Const=False,
           Parameters='TCSharedPointer<CJob> const &_pJob'),
    method('CJobContext', 'TCSharedPointer<CJob>', 'f_Job', 'return @m_pJob;'),
    method('CJobContext', 'TCSharedPointer<CJob> const &', 'f_JobRef', 'return @m_pJob;'),
    method('CJobContext', 'CJobContext', 'f_Copy', 'return {@m_pJob, @m_ID};'),
    method('CJobContext', 'CJobContext &', 'f_SetID', '@m_ID = _ID; return $;', Const=False,
           Parameters='int _ID'),
    method('CJobContext', 'CJobContext &', 'f_Reset', '@m_pJob = {}; return $;', Const=False),
    method('CJobContext', 'int', 'f_ID', 'return @m_ID;', Const=False),
    method('CCopiedState', 'TCFuture<int>', 'f_Data', 'auto const &Request = @m_Request;\n    '
           'return fg_CallSafe([Request]() -> TCFuture<int> { co_await fg_Work(); co_return *Request.m_pData; });',
           Const=False),
    method('CCopiedState', 'TCFuture<int>', 'f_ID', 'auto const &Request = @m_Request;\n    '
           'return fg_CallSafe([Request]() -> TCFuture<int> { co_await fg_Work(); co_return Request.m_ID; });',
           Const=False),
)
print(Lines, end='')
