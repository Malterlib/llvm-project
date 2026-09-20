# Writes an object with the given number of inline definitions, each with a
# weak alias, like a scalar deleting destructor and the vector deleting
# destructor that is an alias of it: with "flagged", compiled with
# -fmalterlib-sized-destructors, each with a marker, and a function `flagged`
# that loads it from an archive.
import sys

Count = int(sys.argv[1])
Flagged = sys.argv[2] == "flagged"
for Index in range(Count):
    print(f'.section .text,"xr",discard,g{Index}')
    print(f".globl g{Index}")
    if Flagged:
        print(f".globl g{Index}.mib_sized")
    print(f"g{Index}:")
    if Flagged:
        print(f"g{Index}.mib_sized:")
    print("  retq")
    print(f".weak e{Index}")
    print(f"e{Index} = g{Index}")
    if Flagged:
        print(f".weak e{Index}.mib_sized")
        print(f"e{Index}.mib_sized = g{Index}.mib_sized")
if Flagged:
    print(".text")
    print(".globl flagged")
    print("flagged:")
    print("  retq")
