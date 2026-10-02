# Writes a coroutine that gives back a sum of Count terms, the first read
# through a pointer parameter, after an await a constant condition leaves
# out. The sum initializes a local that is given back, so that the promise's
# forwarding return_value binds no temporary of the sum, whose temporaries
# clang's own analysis would follow on the thread's stack.
import sys

Count = int(sys.argv[1])
print('\n'.join(['#include "malterlib-concurrency.h"', 'using namespace NMib::NConcurrency;',
                 'TCFuture<int> fg_Work();', 'TCFuture<int> fg_Deep(int *_pValue)', '{',
                 '\tif (false)', '\t\tco_await fg_Work();',
                 '\tint Sum = *_pValue' + ' + 1' * Count + ';', '\tco_return Sum;', '}']))
