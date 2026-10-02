# Writes code as deep as Count of one kind a walk of the checks follows, in
# one context: a chain of local aliases, each made of the one before
# (`ptr`, `ref`, `ptrref`, `assign`, `cond`, `record`), or an expression
# nested Count deep (`call`). The context is a constructor a coroutine
# result is made with (`constructor`), a coroutine lambda that captures the
# last alias (`lambda`), or an actor coroutine (`actor`).
import sys

Shape = sys.argv[1]
Context = sys.argv[2]
Count = int(sys.argv[3])


def body(Root):
    # The statements, the last alias as a pointer, and what it points to.
    Lines = []
    if Shape == 'ptr':
        Lines.append('int const *p0 = %s;' % Root)
        Lines += ['int const *p%d = p%d;' % (i, i - 1) for i in range(1, Count + 1)]
        return Lines, 'p%d' % Count, '*p%d' % Count
    if Shape == 'ref':
        Lines.append('int const &r0 = *%s;' % Root)
        Lines += ['int const &r%d = r%d;' % (i, i - 1) for i in range(1, Count + 1)]
        return Lines, '&r%d' % Count, 'r%d' % Count
    if Shape == 'ptrref':
        Lines.append('int const *p = %s;' % Root)
        Lines.append('int const *&rp0 = p;')
        Lines += ['int const *&rp%d = rp%d;' % (i, i - 1) for i in range(1, Count + 1)]
        return Lines, 'rp%d' % Count, '*rp%d' % Count
    if Shape == 'assign':
        Lines += ['int const *p%d = nullptr;' % i for i in range(Count + 1)]
        Lines.append('p0 = %s;' % Root)
        Lines += ['p%d = p%d;' % (i, i - 1) for i in range(1, Count + 1)]
        return Lines, 'p%d' % Count, '*p%d' % Count
    if Shape == 'cond':
        Lines.append('int const *p0 = %s;' % Root)
        Lines += ['int const *p%d = fg_Choose() ? p%d : p%d;' % (i, i - 1, i - 1)
                  for i in range(1, Count + 1)]
        return Lines, 'p%d' % Count, '*p%d' % Count
    if Shape == 'record':
        Lines.append('CPair v0{%s, nullptr};' % Root)
        Lines += ['CPair v%d = v%d;' % (i, i - 1) for i in range(1, Count + 1)]
        return Lines, 'v%d.m_pFirst' % Count, '*v%d.m_pFirst' % Count
    if Shape == 'call':
        Lines = ['CText Text{"a"};', 'CText Joined = ' + ' + '.join(['Text'] * Count) + ';']
        return Lines, Root, '*%s' % Root
    sys.exit('Unknown shape: ' + Shape)


Out = ['#include "malterlib-concurrency.h"', 'using namespace NMib::NConcurrency;',
       'TCFuture<int> fg_Work();', 'bool fg_Choose();',
       'struct CText { char const *m_pData; CText operator+(CText const &) const; };',
       'struct CPair { int const *m_pFirst; int const *m_pSecond; };']
if Context == 'constructor':
    Statements, Pointer, Value = body('&_Value')
    Out += ['struct CView { int const *m_pValue; CView(int const &_Value) {'] + Statements
    Out += ['m_pValue = %s;' % Pointer, '} };',
            'TCFuture<CView> fg_Get(int const &_Value) { co_return CView(_Value); }']
elif Context == 'lambda':
    Statements, Pointer, Value = body('&Value')
    Out += ['TCFuture<int> fg_Run() {', 'int Value = 0;'] + Statements
    Out += ['co_return co_await fg_CallSafe([pLast = %s]() -> TCFuture<int> {' % Pointer,
            'co_await fg_Work();', 'co_return *pLast;', '});', '}']
elif Context == 'actor':
    Statements, Pointer, Value = body('&m_Value')
    Out += ['struct CApp : CActor {', 'int m_Value = 0;', 'TCFuture<int> f_Run() {'] + Statements
    Out += ['co_await fg_ContinueRunningOnActor(fg_BlockingActor());', 'co_return %s;' % Value,
            '}', '};']
else:
    sys.exit('Unknown context: ' + Context)
print('\n'.join(Out))
