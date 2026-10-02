# Writes coroutine lambdas that await Count calls nested in each other's
# arguments, `fg_Id(co_await fg_Id(...))`, and then read what a captured
# local pointer, or a captured reference, refers to.
import sys

Count = int(sys.argv[1])
Nested = '0'
for _ in range(Count):
    Nested = 'co_await fg_Id(%s)' % Nested
Lines = ['#include "malterlib-concurrency.h"', 'using namespace NMib::NConcurrency;',
         'TCFuture<int> fg_Id(int _Value);',
         'TCFuture<int> fg_Pointer()', '{', '\tint Value = 0;', '\tint *pValue = &Value;',
         '\treturn fg_CallSafe([pValue]() -> TCFuture<int> { int Sum = %s; co_return Sum + *pValue; });' % Nested,
         '}',
         'TCFuture<int> fg_Reference()', '{', '\tint Value = 0;', '\tint &Alias = Value;',
         '\treturn fg_CallSafe([&Alias]() -> TCFuture<int> { int Sum = %s; co_return Sum + Alias; });' % Nested,
         '}']
print('\n'.join(Lines))
