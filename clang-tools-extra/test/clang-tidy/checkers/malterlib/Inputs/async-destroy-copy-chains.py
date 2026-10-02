# Writes coroutines whose async-destroy cleanup reads a pointer that is then
# pointed at a local declared after the guard through Count copies: a chain of
# copied conditionals, `p1 = _bChoose ? p0 : p0`, a chain of diamonds, each a
# conditional of two copies of the one before, and a conditional of Count
# copies of one pointer; and, as a control, the chain from a local declared
# before the guard.
import sys

Count = int(sys.argv[1])


def coroutine(Name, Late, Body):
    return ['TCFuture<int> %s(bool _bChoose, int _iChoose)' % Name, '{',
            '\tint Early = 0;', '\tint *pValue = nullptr;',
            '\tauto Cleanup = co_await fg_AsyncDestroy([&]() -> TCFuture<int> { co_return *pValue; });',
            '\tint %s = 0;' % Late] + Body + ['\tco_return _iChoose;', '}']


def chain(Start):
    Body = ['\tint *p0 = &%s;' % Start]
    for i in range(1, Count + 1):
        Body.append('\tint *p%d = _bChoose ? p%d : p%d;' % (i, i - 1, i - 1))
    return Body + ['\tpValue = p%d;' % Count]


Diamond = ['\tint *q0 = &LateDiamond;']
for i in range(1, Count + 1):
    Diamond += ['\tint *a%d = q%d;' % (i, i - 1), '\tint *b%d = q%d;' % (i, i - 1),
                '\tint *q%d = _bChoose ? a%d : b%d;' % (i, i, i)]
Diamond.append('\tpValue = q%d;' % Count)

Wide = ['\tint *w%d = &LateWide;' % i for i in range(Count)]
Choice = 'w%d' % (Count - 1)
for i in reversed(range(Count - 1)):
    Choice = '_iChoose == %d ? w%d : %s' % (i, i, Choice)
Wide.append('\tpValue = %s;' % Choice)

Lines = ['#include "malterlib-concurrency.h"']
Lines += coroutine('fg_Chain', 'LateChain', chain('LateChain'))
Lines += coroutine('fg_Diamonds', 'LateDiamond', Diamond)
Lines += coroutine('fg_Wide', 'LateWide', Wide)
Lines += coroutine('fg_EarlyChain', 'Unused', chain('Early'))
print('\n'.join(Lines))
