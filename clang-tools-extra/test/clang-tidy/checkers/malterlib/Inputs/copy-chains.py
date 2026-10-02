# Writes functions that dispatch a TCFuture coroutine lambda capturing
# `[pValue = pN]`, a pointer that reaches a local through Count copies: a chain
# of copied conditionals, `p1 = _bChoose ? p0 : p0`, a chain of diamonds, each
# a conditional of two copies of the one before, a conditional of Count copies
# of one pointer, and Count pointers assigned to each other in chained
# assignments round a loop, `p1 = p2 = p3;`; and, as a control, the chain from
# a global.
import sys

Count = int(sys.argv[1])


def function(Name, Late, Body, Captured):
    return ['void %s(bool _bChoose, int _iChoose)' % Name, '{',
            '\tint %s = 0;' % Late] + Body + [
            '\tfg_Dispatch([pValue = %s]() -> TCFuture<int> { co_return *pValue; });' % Captured, '}']


def chain(Start):
    Body = ['\tint *p0 = &%s;' % Start]
    for i in range(1, Count + 1):
        Body.append('\tint *p%d = _bChoose ? p%d : p%d;' % (i, i - 1, i - 1))
    return Body


Diamond = ['\tint *q0 = &LateDiamond;']
for i in range(1, Count + 1):
    Diamond += ['\tint *a%d = q%d;' % (i, i - 1), '\tint *b%d = q%d;' % (i, i - 1),
                '\tint *q%d = _bChoose ? a%d : b%d;' % (i, i, i)]

Wide = ['\tint *w%d = &LateWide;' % i for i in range(Count)]
Choice = 'w%d' % (Count - 1)
for i in reversed(range(Count - 1)):
    Choice = '_iChoose == %d ? w%d : %s' % (i, i, Choice)
Wide.append('\tint *pWide = %s;' % Choice)

Loop = ['\tint *l%d = &LateLoop;' % i for i in range(Count)]
Loop += ['\tfor (int i = 0; i < _iChoose; ++i)', '\t{']
for i in range(Count):
    Loop += ['\t\tif (i == %d)' % i,
             '\t\t\tl%d = l%d = l%d;' % (i, (i + 1) % Count, (i * 7 + 3) % Count),
             '\t\t++l%d;' % ((i + 2) % Count)]
Loop += ['\t}']

Lines = ['#include "malterlib-concurrency.h"', 'int g_Value = 0;']
Lines += function('fg_Chain', 'LateChain', chain('LateChain'), 'p%d' % Count)
Lines += function('fg_Diamonds', 'LateDiamond', Diamond, 'q%d' % Count)
Lines += function('fg_Wide', 'LateWide', Wide, 'pWide')
Lines += function('fg_Loop', 'LateLoop', Loop, 'l0')
Lines += function('fg_GlobalChain', 'Unused', chain('g_Value'), 'p%d' % Count)
print('\n'.join(Lines))
