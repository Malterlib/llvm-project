# Writes a coroutine with Count blocks of `if (true)` nested in each other,
# the innermost using a reference parameter after a suspension.
import sys

Count = int(sys.argv[1])
Lines = ['#include "malterlib-concurrency.h"', 'using namespace NMib::NConcurrency;',
         'TCFuture<int> fg_Work();', 'TCFuture<int> fg_Deep(int &_Value)', '{',
         '\tco_await fg_Work();']
Lines += ['\tif (true) {'] * Count
Lines += ['\tco_return _Value;']
Lines += ['\t}'] * Count
Lines += ['\tco_return 0;', '}']
print('\n'.join(Lines))
