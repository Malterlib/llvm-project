# Writes a coroutine with a chain of the given number of pointers to the
# guard of fg_AsyncDestroy, each given the one before, declared in the
# opposite order, and the guard moved through the last.
import sys

Count = int(sys.argv[1])
print('#include "malterlib-concurrency.h"')
print("template <typename tf_CValue>")
print("void fg_StoreGuard(tf_CValue &&_Value);")
print("TCFuture<int> fg_Chain(TCActor<> _Target)")
print("{")
print("\tauto Cleanup = co_await fg_AsyncDestroy(_Target);")
for Index in range(Count, 0, -1):
    print(f"\tdecltype(Cleanup) *p{Index} = nullptr;")
print("\tauto *p0 = &Cleanup;")
for Index in range(1, Count + 1):
    print(f"\tp{Index} = p{Index - 1};")
print(f"\tfg_StoreGuard(fg_Move(*p{Count}));")
print("\tco_return 0;")
print("}")
