# REQUIRES: x86
## Where the link keeps the copies of inline definitions compiled without
## -fmalterlib-sized-destructors, the marker of the weak alias of each copy
## compiled with it, which it drops, falls back on the marker of the definition,
## which the archive member being loaded defines in a section it drops too. A
## reference to such a marker loads nothing from a member that isn't an import,
## and each member is looked at once, not for each of its markers, in time
## linear in the number of them.

# RUN: rm -rf %t && mkdir %t
# RUN: llvm-mc -filetype=obj -triple=x86_64-windows-msvc %s -o %t/main.obj
# RUN: %python %S/Inputs/malterlib-sized-many-markers.py 65536 unflagged > %t/unflagged.s
# RUN: %python %S/Inputs/malterlib-sized-many-markers.py 65536 flagged > %t/flagged.s
# RUN: llvm-mc -filetype=obj -triple=x86_64-windows-msvc %t/unflagged.s -o %t/unflagged.obj
# RUN: llvm-mc -filetype=obj -triple=x86_64-windows-msvc %t/flagged.s -o %t/flagged.obj
# RUN: llvm-ar rcs %t/flagged.lib %t/flagged.obj
# RUN: lld-link %t/main.obj %t/unflagged.obj %t/flagged.lib /out:%t/main.exe /entry:main /subsystem:console

.globl main
main:
  callq flagged
  retq
