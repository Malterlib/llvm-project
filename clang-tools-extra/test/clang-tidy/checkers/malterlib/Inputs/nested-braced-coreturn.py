# Writes a coroutine that awaits the given number of nested coroutine lambdas,
# each returning a braced list of what it awaits, which the class it returns
# is constructed with.
import sys

Count = int(sys.argv[1])
Expression = "co_await fg_Work()"
for Index in range(Count):
    Expression = (
        "(co_await []() -> TCFuture<CResult> {\n"
        f"  co_return {{{Expression}}};\n"
        "}()).m_Value"
    )
print('#include "malterlib-concurrency.h"')
print("struct CResult { CResult(); CResult(int _Value); int m_Value; };")
print("TCFuture<int> fg_Work();")
print("TCFuture<int> fg_Test() {")
print(f"  co_return {Expression};")
print("}")
