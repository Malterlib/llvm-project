# Writes integer wrappers nested sys.argv[1] levels, each holding the one
# before as its member and converting it to an integer at two returns, and
# scans of the outermost, one after a test of the innermost member and one
# without: the members a conversion gives are told once for each conversion,
# not once per return through each level below.
import sys

Levels = int(sys.argv[1])
print('unsigned fg_GetHighestBitSetNoZero(unsigned _Bits);')
print('bool fg_Condition();')
print('struct CInt0 { unsigned m_Lower; operator unsigned() const { return m_Lower; } };')
for iLevel in range(1, Levels + 1):
    print('struct CInt%d { CInt%d m_Lower; operator unsigned() const { if (fg_Condition()) return unsigned(m_Lower); return unsigned(m_Lower); } };'
          % (iLevel, iLevel - 1))
Members = '.m_Lower' * (Levels + 1)
print('unsigned fg_Tested(CInt%d const &_Bits) {' % Levels)
print('  if (_Bits%s == 0)' % Members)
print('    return 0;')
print('  return fg_GetHighestBitSetNoZero(unsigned(_Bits));')
print('}')
print('unsigned fg_Untested(CInt%d const &_Bits) {' % Levels)
print('  return fg_GetHighestBitSetNoZero(unsigned(_Bits));')
print('}')
