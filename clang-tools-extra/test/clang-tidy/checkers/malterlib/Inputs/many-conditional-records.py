# Writes the given number of structs that each start with a 'public:' section
# after a conditional directive, which another configuration can need.
import sys

Count = int(sys.argv[1])
for Index in range(Count):
    print(f"struct SThing{Index}")
    print("{")
    print("#if 1")
    print("#endif")
    print("public:")
    print("\tint m_Value;")
    print("};")
