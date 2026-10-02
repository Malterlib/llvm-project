# Writes layers of two classes each, each of which derives, not virtually,
# from both classes of the layer before, so that a class of the last layer
# reaches each base of the first through every path; and a coroutine that
# uses a reference to an unrelated class after a suspension, beside a keeper
# of the last layer.
import sys

Layers = int(sys.argv[1])
print('#include "malterlib-concurrency.h"')
print("using namespace NMib::NConcurrency;")
for Layer in range(Layers):
    for Index in range(2):
        Bases = f" : CLayer{Layer - 1}_0, CLayer{Layer - 1}_1" if Layer else ""
        print(f"struct CLayer{Layer}_{Index}{Bases} {{ int m_Value{Layer}_{Index}; }};")
print("struct CUnrelated { int m_Value; };")
print("TCFuture<int> fg_Work();")
print(f"TCFuture<int> fg_Use(CUnrelated &_Other, TCSharedPointer<CLayer{Layers - 1}_0> _pKeep) {{")
print("  co_await fg_Work();")
print("  co_return _Other.m_Value;")
print("}")
