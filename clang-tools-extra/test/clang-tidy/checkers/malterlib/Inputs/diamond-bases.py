# Writes layers of classes, each of which derives virtually from every class of
# the layer before, and a variable of a class of the last layer, or with
# "parameter" a function taking one by value, or with "pointer" a variable of a
# smart pointer derived from the last layer, whose bases share each base of
# the layers before through every path.
import sys

Layers = int(sys.argv[1])
Width = int(sys.argv[2])
for Layer in range(Layers):
    for Index in range(Width):
        Bases = (
            ", ".join(f"virtual CLayer{Layer - 1}_{Base}" for Base in range(Width))
            if Layer
            else ""
        )
        print(f"struct CLayer{Layer}_{Index}{' : ' + Bases if Bases else ''} {{}};")
if len(sys.argv) > 3 and sys.argv[3] == "parameter":
    print(f"void fg_Use(CLayer{Layers - 1}_0 _Value);")
elif len(sys.argv) > 3 and sys.argv[3] == "pointer":
    # A smart pointer, named as one, derived from the last layer, whose bases
    # are gone through to tell whether it is a handle.
    Bases = ", ".join(f"virtual CLayer{Layers - 1}_{Base}" for Base in range(Width))
    print(f"struct CLeafPointer : {Bases} {{ int *operator->() const; }};")
    print("CLeafPointer g_pValue;")
else:
    print(f"CLayer{Layers - 1}_0 g_Value;")
