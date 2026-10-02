# Writes Count macros each defined as the one before, the first a block of
# `__if_exists`, and a class whose body uses the last; and a class with a
# repeated label that uses none.
import sys

Count = int(sys.argv[1])
Lines = ['struct CConfig', '{', '};', '#define DIfExists0 __if_exists(CConfig)']
Lines += ['#define DIfExists%d DIfExists%d' % (i, i - 1) for i in range(1, Count + 1)]
Lines += ['struct CExists', '{', 'private:', '\tint mp_First;',
          '\tDIfExists%d' % Count, '\t{', '\tprivate:', '\t\tint mp_Inner;', '\t}',
          'private:', '\tint mp_Last;', '};',
          'struct CPlain', '{', 'private:', '\tint mp_First;', 'private:',
          '\tint mp_Last;', '};']
print('\n'.join(Lines))
