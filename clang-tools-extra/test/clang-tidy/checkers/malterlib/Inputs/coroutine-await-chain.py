# Writes a chain of coroutines, each awaiting the next, which a coroutine
# with a reference parameter awaits: the non-suspension proof of each
# awaited coroutine is bounded, not one stack frame for each link.
import sys

Count = int(sys.argv[1])
print('#include "malterlib-concurrency.h"')
print('using namespace NMib::NConcurrency;')
print('TCFuture<int> fg_Link%d() { co_return 0; }' % Count)
for Index in range(Count - 1, -1, -1):
    print('TCFuture<int> fg_Link%d() { co_return co_await fg_Link%d(); }' % (Index, Index + 1))
print('struct CChainUser {')
print('  TCFuture<int> f_Run(int const &_Value) {')
print('    int Result = co_await fg_Link0();')
print('    co_return Result + _Value;')
print('  }')
print('};')
