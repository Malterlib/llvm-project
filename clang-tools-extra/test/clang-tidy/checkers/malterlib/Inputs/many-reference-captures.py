# Writes a coroutine whose fg_AsyncDestroy cleanup captures the given number of
# locals by reference, reads each before its suspension, and one after it.
import sys

Count = int(sys.argv[1])
print('#include "malterlib-concurrency.h"')
print("TCFuture<int> fg_Suspend();")
print("TCFuture<int> fg_Test() {")
for Index in range(Count):
    print(f"  int Value{Index} = {Index};")
print("  auto Cleanup = co_await fg_AsyncDestroy([&]() -> TCFuture<int> {")
for Index in range(Count):
    print(f"    (void)(Value{Index} + 1);")
print("    co_await fg_Suspend();")
print("    co_return Value7;")
print("  });")
print("  co_await fg_Suspend();")
print("  co_return 0;")
print("}")
