# Writes records each of which holds ten members of the one before, the
# first a shared owner of an object, sys.argv[1] levels, and a lambda that
# captures `this` with the outermost record: what it holds of owners is told
# by a bounded walk, not once per path through the records.
import sys

Levels = int(sys.argv[1])
print('#include "malterlib-concurrency.h"')
print('using namespace NMib::NConcurrency;')
print('using namespace NMib::NStorage;')
print('TCFuture<int> fg_Work();')
print('struct CState;')
print('struct CLevel0 { TCSharedPointer<CState> m_pState; };')
for iLevel in range(1, Levels + 1):
    Members = ' '.join('CLevel%d m_Member%d;' % (iLevel - 1, iMember) for iMember in range(10))
    print('struct CLevel%d { %s };' % (iLevel, Members))
print('struct CState {')
print('  int m_Value = 0;')
print('  TCFuture<int> f_Wide(CLevel%d const &_Keep) {' % Levels)
# The copy holds owners a parameter that doesn't state that it owns this
# object gives (DMibKeepsThis): the capture of `this` is reported.
print('    return fg_CallSafe([this, Keep = _Keep]() -> TCFuture<int> { co_await fg_Work(); co_return m_Value; });')
print('  }')
print('};')
