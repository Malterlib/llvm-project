# Writes Count function-like macros, each naming the one before twice, the
# first naming itself, which none of them invokes, and a class whose body
# uses the last through a macro that discards its argument; and a class with
# a repeated label.
import sys

Count = int(sys.argv[1])
Lines = ['#define DCycle0() DCycle0']
Lines += ['#define DCycle%d() DCycle%d DCycle%d' % (i, i - 1, i - 1) for i in range(1, Count + 1)]
Lines += ['#define DDiscard(d_Value)', '#define DStart DDiscard(DCycle%d)' % Count,
          'struct CUses', '{', 'private:', '\tint mp_First;', '\tDStart', 'private:', '\tint mp_Last;', '};',
          'struct CPlain', '{', 'private:', '\tint mp_First;', 'private:', '\tint mp_Last;', '};']
print('\n'.join(Lines))
