# Writes a chain of distinct functions, each giving what it is given to the
# next, of one kind of interprocedural summary, which a check asks of the
# first, as deep as the chain is long, on new stack segments (withStack).
# Or, of `alias`, a
# chain of local pointers in one coroutine, each a copy of the one before,
# the last of which a coroutine lambda captures.
import sys

Kind = sys.argv[1]
Count = int(sys.argv[2])
print('#include "malterlib-concurrency.h"')
print('using namespace NMib::NConcurrency;')
print('TCFuture<int> fg_Suspend();')
if Kind == 'kept':
    # What a constructor keeps of what it is given (parameterKept).
    print('int const *g_pValue;')
    for Index in range(Count + 1):
        print('void fg_Save%d(int const &_Value);' % Index)
    for Index in range(Count):
        print('void fg_Save%d(int const &_Value) { fg_Save%d(_Value); }' % (Index, Index + 1))
    print('void fg_Save%d(int const &_Value) { g_pValue = &_Value; }' % Count)
    print('struct CResult { int const *m_pValue = nullptr; CResult(int const &_Value) { fg_Save0(_Value); } };')
    print('TCFuture<CResult> fg_Make(int const &_Value) { co_return CResult(_Value); }')
elif Kind == 'set':
    # What a setter sets of what it is given (setParameters).
    for Index in range(Count + 1):
        print('void fg_Set%d(int *&o_pValue, int *_pValue);' % Index)
    for Index in range(Count):
        print('void fg_Set%d(int *&o_pValue, int *_pValue) { fg_Set%d(o_pValue, _pValue); }' % (Index, Index + 1))
    print('void fg_Set%d(int *&o_pValue, int *_pValue) { o_pValue = _pValue; }' % Count)
    print('struct CRef { int *m_pValue; };')
    print('TCFuture<CRef> fg_Read(int &_Value) { int *pValue = nullptr; fg_Set0(pValue, &_Value); co_return CRef{pValue}; }')
    print('thread_local int g_ThreadLocal;')
    print('int *g_pStored;')
    print('void fg_Store() { int *pValue = nullptr; fg_Set0(pValue, &g_ThreadLocal); g_pStored = pValue; }')
elif Kind == 'returned':
    # What a function gives back of what it is given (returnedRoots).
    for Index in range(Count + 1):
        print('int *fg_Pass%d(int *_pValue);' % Index)
    for Index in range(Count):
        print('int *fg_Pass%d(int *_pValue) { return fg_Pass%d(_pValue); }' % (Index, Index + 1))
    print('int *fg_Pass%d(int *_pValue) { return _pValue; }' % Count)
    print('TCFuture<int *> fg_Get(int &_Value) { co_return fg_Pass0(&_Value); }')
elif Kind == 'callback':
    # Whether a function only calls what it is given (onlyCallsParameter,
    # closureUses).
    for Index in range(Count + 1):
        print('void fg_Run%d(TCFunction<void ()> const &_fFunction);' % Index)
    for Index in range(Count):
        print('void fg_Run%d(TCFunction<void ()> const &_fFunction) { fg_Run%d(_fFunction); }' % (Index, Index + 1))
    print('void fg_Run%d(TCFunction<void ()> const &_fFunction) { _fFunction(); }' % Count)
    print('thread_local int g_ThreadLocal;')
    print('void fg_Use() { fg_Run0([pValue = &g_ThreadLocal] { *pValue = 1; }); }')
elif Kind == 'reads':
    # Whether a member function reads only the storage of its object
    # (readsOnlyOwnStorage).
    print('struct CStrSpan {')
    print('  char const *m_pStr;')
    print('  unsigned m_Length;')
    for Index in range(Count):
        print('  unsigned f_Get%d() const { return f_Get%d(); }' % (Index, Index + 1))
    print('  unsigned f_Get%d() const { return m_Length; }' % Count)
    print('};')
    print('TCFuture<unsigned> fg_Read(CStrSpan _Text) { co_await fg_Suspend(); co_return _Text.f_Get0(); }')
elif Kind == 'relation':
    # What a member function gives back of its object (returnedRelation).
    print('struct CApp : CActor {')
    print('  int m_Value;')
    for Index in range(Count):
        print('  int &f_Get%d() { return f_Get%d(); }' % (Index, Index + 1))
    print('  int &f_Get%d() { return m_Value; }' % Count)
    print('  TCFuture<int> f_Run() { int &Value = f_Get0(); co_await fg_Suspend(); Value = 1; co_return 0; }')
    print('};')
elif Kind == 'alias':
    # What a local points to, through the locals it is a copy of
    # (localPointedBy, localSlotValues).
    print('TCFuture<int> fg_Run() {')
    print('  int Value = 0;')
    print('  int *p0 = &Value;')
    for Index in range(1, Count + 1):
        print('  int *p%d = p%d;' % (Index, Index - 1))
    print('  co_return co_await fg_CallSafe([p%d]() -> TCFuture<int> { co_await fg_Suspend(); co_return *p%d; });' % (Count, Count))
    print('}')
elif Kind == 'cycle':
    # Helpers in one recursion, each calling the one before or the next,
    # nested as deep as there are helpers, of whether a getter only gives
    # access (onlyGivesAccessOf): each is worked out again from where its own
    # passes ended, not from its initial value each time one around it is.
    print('using umint = unsigned long;')
    print('int fg_GetLowestBitSetNoZero(umint const &_Bits);')
    for Index in range(Count + 1):
        print('umint const &fg_At%d(umint const &_Bits, unsigned _Depth);' % Index)
    print('umint const &fg_At0(umint const &_Bits, unsigned _Depth) { return fg_At1(_Bits, _Depth); }')
    for Index in range(1, Count):
        print('umint const &fg_At%d(umint const &_Bits, unsigned _Depth) { if (_Depth) return fg_At%d(_Bits, _Depth - 1); return fg_At%d(_Bits, _Depth); }' % (Index, Index - 1, Index + 1))
    print('umint const &fg_At%d(umint const &_Bits, unsigned _Depth) { if (_Depth) return fg_At%d(_Bits, _Depth - 1); return _Bits; }' % (Count, Count - 1))
    print('struct CBits { umint m_Bits; umint const &operator()() { return fg_At0(m_Bits, 0); } };')
    print('int fg_Read(CBits &_Bits) { if (!_Bits.m_Bits) return -1; _Bits(); return fg_GetLowestBitSetNoZero(_Bits.m_Bits); }')
else:
    sys.exit('unknown kind ' + Kind)
