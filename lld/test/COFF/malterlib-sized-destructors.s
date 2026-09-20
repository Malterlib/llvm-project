# REQUIRES: x86
## A definition compiled with -fmalterlib-sized-destructors carries a marker
## symbol at its address, which the construction sites that depend on it refer
## to. A definition compiled without the flag has no marker, and the reference
## stays undefined.

# RUN: rm -rf %t; split-file %s %t
# RUN: llvm-mc -filetype=obj -triple=x86_64-windows-msvc %t/main.s -o %t/main.obj
# RUN: llvm-mc -filetype=obj -triple=x86_64-windows-msvc %t/flagged.s -o %t/flagged.obj
# RUN: llvm-mc -filetype=obj -triple=x86_64-windows-msvc %t/unflagged.s -o %t/unflagged.obj

# RUN: lld-link %t/main.obj %t/flagged.obj /out:%t/flagged.exe /entry:main /subsystem:console
# RUN: env LLD_IN_TEST=1 not lld-link %t/main.obj %t/unflagged.obj /out:%t/unflagged.exe \
# RUN:   /entry:main /subsystem:console 2>&1 | FileCheck %s
# CHECK:      error: public: virtual void * __cdecl Foreign::`vector deleting dtor'(unsigned int) was not compiled with -fmalterlib-sized-destructors
# CHECK-NEXT: >>> its marker ??_EForeign@@UEAAPEAXI@Z.mib_sized is undefined
# CHECK-NEXT: >>> an object a construction site makes is destroyed with the size its deleting destructor returns

## The references are in data that dead code stripping removes, and an
## unresolved marker is an error whatever /force says.
# RUN: env LLD_IN_TEST=1 not lld-link /opt:ref /force:unresolved %t/main.obj %t/unflagged.obj \
# RUN:   /out:%t/forced.exe /entry:main /subsystem:console 2>&1 | FileCheck %s

## An inline definition is checked in the copy the link keeps. The marker is
## an external symbol of the definition's COMDAT section, which is dropped with
## a copy that loses, and a copy from code without the flag has none.
# RUN: llvm-mc -filetype=obj -triple=x86_64-windows-msvc %t/inline-flagged.s -o %t/inline-flagged.obj
# RUN: llvm-mc -filetype=obj -triple=x86_64-windows-msvc %t/inline-unflagged.s -o %t/inline-unflagged.obj
# RUN: lld-link %t/main.obj %t/inline-flagged.obj %t/inline-unflagged.obj /out:%t/inline.exe \
# RUN:   /entry:main /subsystem:console
# RUN: env LLD_IN_TEST=1 not lld-link %t/main.obj %t/inline-unflagged.obj %t/inline-flagged.obj \
# RUN:   /out:%t/inline.exe /entry:main /subsystem:console 2>&1 | FileCheck %s
## A marker nothing refers to is left alone: the class is not constructed
## through the builtin.
# RUN: llvm-mc -filetype=obj -triple=x86_64-windows-msvc %t/plain.s -o %t/plain.obj
# RUN: lld-link %t/plain.obj %t/inline-unflagged.obj %t/inline-flagged.obj /out:%t/plain.exe \
# RUN:   /entry:main /subsystem:console

## A site and an inline definition in one object refer to the marker the
## assembler resolved within the object, and the link may keep another copy.
# RUN: llvm-mc -filetype=obj -triple=x86_64-windows-msvc %t/site-inline.s -o %t/site-inline.obj
# RUN: lld-link /opt:ref %t/site-inline.obj %t/inline-unflagged.obj /out:%t/site.exe /entry:main /subsystem:console
# RUN: env LLD_IN_TEST=1 not lld-link /opt:ref %t/inline-unflagged.obj %t/site-inline.obj /out:%t/site.exe \
# RUN:   /entry:main /subsystem:console 2>&1 | FileCheck %s

## A vector deleting destructor is a weak alias of the scalar one, which the
## link resolves after the check.
# RUN: llvm-mc -filetype=obj -triple=x86_64-windows-msvc %t/weak-alias.s -o %t/weak-alias.obj
# RUN: lld-link %t/main.obj %t/weak-alias.obj /out:%t/weak-alias.exe /entry:main /subsystem:console
## The marker of the alias is a weak alias too: a copy that overrides the alias
## overrides the marker with its own, or leaves the marker elsewhere.
# RUN: lld-link %t/main.obj %t/weak-alias.obj %t/flagged.obj /out:%t/weak-alias.exe /entry:main /subsystem:console
# RUN: env LLD_IN_TEST=1 not lld-link %t/main.obj %t/weak-alias.obj %t/unflagged.obj /out:%t/weak-alias.exe \
# RUN:   /entry:main /subsystem:console 2>&1 | FileCheck %s --check-prefix=WEAK-MIXED
# WEAK-MIXED: error: public: virtual void * __cdecl Foreign::`vector deleting dtor'(unsigned int) was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
## A DLL does not export the marker such a copy left behind.
# RUN: lld-link /dll /noentry %t/weak-alias.obj %t/unflagged.obj /out:%t/displaced.dll \
# RUN:   "/export:??_EForeign@@UEAAPEAXI@Z" "/export:??_EForeign@@UEAAPEAXI@Z.mib_sized"
# RUN: llvm-readobj --coff-exports %t/displaced.dll | FileCheck %s --check-prefix=DISPLACED
# DISPLACED: Name: ??_EForeign@@UEAAPEAXI@Z
# DISPLACED-NOT: Name: ??_EForeign@@UEAAPEAXI@Z.mib_sized

## A definition is exported with its marker, which needs no directive of its
## own.
# RUN: lld-link /dll /noentry %t/flagged.obj /out:%t/flagged-plain.dll "/export:??_EForeign@@UEAAPEAXI@Z"
# RUN: llvm-readobj --coff-exports %t/flagged-plain.dll | FileCheck %s --check-prefix=WITH-MARKER
# WITH-MARKER: Name: ??_EForeign@@UEAAPEAXI@Z
# WITH-MARKER: Name: ??_EForeign@@UEAAPEAXI@Z.mib_sized
# RUN: lld-link /dll /noentry %t/weak-alias.obj /out:%t/weak-alias.dll "/export:??_EForeign@@UEAAPEAXI@Z"
# RUN: llvm-readobj --coff-exports %t/weak-alias.dll | FileCheck %s --check-prefix=WITH-MARKER

## MinGW imports a variable another image exports when a reference names it
## directly, after the check.
# RUN: llvm-mc -filetype=obj -triple=x86_64-windows-gnu %t/mingw-export.s -o %t/mingw-export.obj
# RUN: llvm-mc -filetype=obj -triple=x86_64-windows-gnu %t/mingw-site.s -o %t/mingw-site.obj
# RUN: lld-link -lldmingw /dll /noentry %t/mingw-export.obj /out:%t/mingw.dll /implib:%t/mingw.lib
# RUN: lld-link -lldmingw %t/mingw-site.obj %t/mingw.lib /out:%t/mingw.exe /entry:main /subsystem:console

## The marker of a definition another image exports is exported with it, and
## a site refers to it through its import thunk.
# RUN: llvm-mc -filetype=obj -triple=x86_64-windows-msvc %t/main-import.s -o %t/main-import.obj
# RUN: llvm-mc -filetype=obj -triple=x86_64-windows-msvc %t/flagged-export.s -o %t/flagged-export.obj
# RUN: lld-link /dll /noentry %t/flagged-export.obj /out:%t/flagged.dll /implib:%t/flagged.lib
# RUN: lld-link /dll /noentry %t/unflagged.obj /out:%t/unflagged.dll /implib:%t/unflagged.lib "/export:??_EForeign@@UEAAPEAXI@Z"
# RUN: lld-link %t/main-import.obj %t/flagged.lib /out:%t/import.exe /entry:main /subsystem:console
# RUN: env LLD_IN_TEST=1 not lld-link %t/main-import.obj %t/unflagged.lib /out:%t/import.exe /entry:main /subsystem:console 2>&1 \
# RUN:   | FileCheck %s --check-prefix=IMPORT
# IMPORT: error: public: virtual void * __cdecl Foreign::`vector deleting dtor'(unsigned int) was not compiled with -fmalterlib-sized-destructors
# IMPORT-NEXT: >>> its marker ??_EForeign@@UEAAPEAXI@Z.mib_sized is undefined
## The image's marker does not stand in for an object's copy.
# RUN: env LLD_IN_TEST=1 not lld-link %t/main-import.obj %t/unflagged.obj %t/flagged.lib /out:%t/import.exe /entry:main /subsystem:console 2>&1 \
# RUN:   | FileCheck %s --check-prefix=MIXED-DLL
# MIXED-DLL: error: public: virtual void * __cdecl Foreign::`vector deleting dtor'(unsigned int) was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without

## The marker of a constructor proves its claim only when the vftable it
## installs in its image was compiled with the flag. A DLL whose constructor
## installs its own vftable links, but does not export the marker it cannot
## prove; a site elsewhere then fails.
# RUN: llvm-mc -filetype=obj -triple=x86_64-windows-msvc %t/ctor.s -o %t/ctor.obj
# RUN: llvm-mc -filetype=obj -triple=x86_64-windows-msvc %t/vtable-flagged.s -o %t/vtable-flagged.obj
# RUN: llvm-mc -filetype=obj -triple=x86_64-windows-msvc %t/vtable-unflagged.s -o %t/vtable-unflagged.obj
# RUN: llvm-mc -filetype=obj -triple=x86_64-windows-msvc %t/ctor-site.s -o %t/ctor-site.obj
# RUN: lld-link /dll /noentry %t/ctor.obj %t/vtable-flagged.obj %t/flagged.obj /out:%t/ctor-flagged.dll /implib:%t/ctor-flagged.lib
# RUN: lld-link /dll /noentry %t/ctor.obj %t/vtable-unflagged.obj %t/flagged.obj /out:%t/ctor-unflagged.dll /implib:%t/ctor-unflagged.lib
# RUN: lld-link %t/ctor-site.obj %t/ctor-flagged.lib /out:%t/ctor.exe /entry:main /subsystem:console
# RUN: env LLD_IN_TEST=1 not lld-link %t/ctor-site.obj %t/ctor-unflagged.lib /out:%t/ctor.exe /entry:main /subsystem:console 2>&1 \
# RUN:   | FileCheck %s --check-prefix=CTOR
# CTOR: error: public: __cdecl Foreign::Foreign(void) was not compiled with -fmalterlib-sized-destructors
# RUN: env LLD_IN_TEST=1 not lld-link %t/ctor-site.obj %t/ctor.obj %t/vtable-unflagged.obj %t/flagged.obj /out:%t/ctor.exe \
# RUN:   /entry:main /subsystem:console 2>&1 | FileCheck %s --check-prefix=CHAIN
# CHAIN:      error: public: __cdecl Foreign::Foreign(void) depends on const Foreign::`vftable'
# CHAIN-NEXT: >>> const Foreign::`vftable' was not compiled with -fmalterlib-sized-destructors

## The records of a COMDAT owner are associative with it, and garbage
## collection does not bring them back with the owner: an unmarked vtable
## nothing requires the proof of is not an error.
# RUN: llvm-mc -filetype=obj -triple=x86_64-windows-msvc %t/ctor-comdat.s -o %t/ctor-comdat.obj
# RUN: llvm-mc -filetype=obj -triple=x86_64-windows-msvc %t/ctor-comdat-user.s -o %t/ctor-comdat-user.obj
# RUN: lld-link /opt:ref %t/ctor-comdat-user.obj %t/ctor-comdat.obj %t/vtable-unflagged.obj %t/unflagged.obj \
# RUN:   /out:%t/ctor-comdat.exe /entry:main /subsystem:console

## A GNU import library holds an ordinary object for each import, which a
## marker may load, and whose library stands for the DLL.
# RUN: llvm-mc -triple=x86_64-windows-gnu %p/Inputs/gnu-implib-head.s -filetype=obj -o %t/gnu-dabcdh.o
# RUN: llvm-mc -triple=x86_64-windows-gnu %p/Inputs/gnu-implib-func.s -filetype=obj -o %t/gnu-dabcds00000.o
# RUN: llvm-mc -triple=x86_64-windows-gnu %t/gnu-func-marker.s -filetype=obj -o %t/gnu-dabcds00001.o
# RUN: llvm-mc -triple=x86_64-windows-gnu %p/Inputs/gnu-implib-tail.s -filetype=obj -o %t/gnu-dabcdt.o
# RUN: rm -f %t/gnu-implib.a
# RUN: llvm-ar rcs %t/gnu-implib.a %t/gnu-dabcdh.o %t/gnu-dabcds00000.o %t/gnu-dabcds00001.o %t/gnu-dabcdt.o
# RUN: llvm-mc -triple=x86_64-windows-gnu %t/gnu-site.s -filetype=obj -o %t/gnu-site.o
# RUN: lld-link %t/gnu-site.o %t/gnu-implib.a /out:%t/gnu.exe /entry:main /subsystem:console

## A marker belongs to the copy of a definition the link chose for the
## definition's own sake, so a reference to it loads no archive member, such
## as the record of a constructor the optimizer inlined everywhere.
# RUN: llvm-mc -filetype=obj -triple=x86_64-windows-msvc %t/owner-gone.s -o %t/owner-gone.obj
# RUN: llvm-mc -filetype=obj -triple=x86_64-windows-msvc %t/ctor-member.s -o %t/ctor-member.obj
# RUN: rm -f %t/ctor-member.lib && llvm-lib /out:%t/ctor-member.lib %t/ctor-member.obj
# RUN: lld-link %t/owner-gone.obj %t/ctor-member.lib /out:%t/owner-gone.exe /entry:main /subsystem:console

## A definition LTO compiles carries its marker into the object LTO produces,
## even when LTO makes it local.
# RUN: llvm-as %t/lto-main.ll -o %t/lto-main.obj
# RUN: llvm-as %t/lto-flagged.ll -o %t/lto-flagged.obj
# RUN: llvm-as %t/lto-unflagged.ll -o %t/lto-unflagged.obj
# RUN: lld-link %t/main.obj %t/lto-flagged.obj /out:%t/lto.exe /entry:main /subsystem:console
# RUN: env LLD_IN_TEST=1 not lld-link %t/main.obj %t/lto-unflagged.obj /out:%t/lto.exe \
# RUN:   /entry:main /subsystem:console 2>&1 | FileCheck %s
# RUN: lld-link %t/lto-main.obj %t/lto-flagged.obj /out:%t/lto-local.exe /entry:main /subsystem:console
# RUN: env LLD_IN_TEST=1 not lld-link %t/lto-main.obj %t/lto-unflagged.obj /out:%t/lto-local.exe \
# RUN:   /entry:main /subsystem:console 2>&1 | FileCheck %s --check-prefix=LTO
# LTO: error: public: virtual void * __cdecl Foreign::`vector deleting dtor'(unsigned int) was not compiled with -fmalterlib-sized-destructors

#--- ctor-comdat.s
.section .text,"xr",discard,"??0Foreign@@QEAA@XZ"
.globl "??0Foreign@@QEAA@XZ"
"??0Foreign@@QEAA@XZ":
.Lowner:
  leaq "??_7Foreign@@6B@"(%rip), %rax
  retq

.section .rdata$mibszd,"dr",associative,"??0Foreign@@QEAA@XZ"
.quad .Lowner
.quad "??_7Foreign@@6B@"
.quad "??_7Foreign@@6B@.mib_sized"

#--- ctor-comdat-user.s
.text
.globl main
main:
  callq "??0Foreign@@QEAA@XZ"
  retq

#--- gnu-site.s
.text
.globl main
main:
  retq

.section .rdata$mibszf,"dr"
.quad func
.quad func.mib_sized

#--- gnu-func-marker.s
        .text
        .global         func.mib_sized
        .global         __imp_func.mib_sized
func.mib_sized:
        jmp             *__imp_func.mib_sized

        .section        .idata$7
        .rva            _head_test_lib

        .section        .idata$5
__imp_func.mib_sized:
        .rva            .Lhint_name
        .long           0

        .section        .idata$4
        .rva            .Lhint_name
        .long           0

        .section        .idata$6
.Lhint_name:
        .short          0
        .asciz          "func.mib_sized"

#--- owner-gone.s
.text
.globl main
main:
  retq

.section .rdata,"dr"
.globl "??_7Foreign@@6B@"
"??_7Foreign@@6B@":
  .quad 0

.section .rdata$mibszd,"dr"
.quad "??0Foreign@@QEAA@XZ.mib_sized"
.quad "??_7Foreign@@6B@"
.quad "??_7Foreign@@6B@.mib_sized"

#--- ctor-member.s
.text
.globl "??0Foreign@@QEAA@XZ"
.globl "??0Foreign@@QEAA@XZ.mib_sized"
"??0Foreign@@QEAA@XZ":
"??0Foreign@@QEAA@XZ.mib_sized":
  callq unrelated
  retq

#--- main.s
.text
.globl main
main:
  retq

.section .rdata$mibszf,"dr"
.quad "??_EForeign@@UEAAPEAXI@Z"
.quad "??_EForeign@@UEAAPEAXI@Z.mib_sized"

#--- main-import.s
.text
.globl main
main:
  retq

.section .rdata$mibszf,"dr"
.quad "??_EForeign@@UEAAPEAXI@Z"
.quad "??_EForeign@@UEAAPEAXI@Z.mib_sized"

#--- flagged-export.s
.text
.globl "??_EForeign@@UEAAPEAXI@Z"
.globl "??_EForeign@@UEAAPEAXI@Z.mib_sized"
"??_EForeign@@UEAAPEAXI@Z":
"??_EForeign@@UEAAPEAXI@Z.mib_sized":
  retq

.section .drectve,"yni"
.ascii " /EXPORT:\"??_EForeign@@UEAAPEAXI@Z\" /EXPORT:\"??_EForeign@@UEAAPEAXI@Z.mib_sized\""

#--- ctor.s
.text
.globl "??0Foreign@@QEAA@XZ"
.globl "??0Foreign@@QEAA@XZ.mib_sized"
"??0Foreign@@QEAA@XZ":
"??0Foreign@@QEAA@XZ.mib_sized":
  retq

.section .rdata$mibszd,"dr"
.quad "??0Foreign@@QEAA@XZ.mib_sized"
.quad "??_7Foreign@@6B@"
.quad "??_7Foreign@@6B@.mib_sized"

.section .drectve,"yni"
.ascii " /EXPORT:\"??0Foreign@@QEAA@XZ\" /EXPORT:\"??0Foreign@@QEAA@XZ.mib_sized\""

#--- vtable-flagged.s
.section .rdata,"dr"
.globl "??_7Foreign@@6B@"
.globl "??_7Foreign@@6B@.mib_sized"
"??_7Foreign@@6B@":
"??_7Foreign@@6B@.mib_sized":
  .quad "??_EForeign@@UEAAPEAXI@Z"

.section .rdata$mibszd,"dr"
.quad "??_7Foreign@@6B@.mib_sized"
.quad "??_EForeign@@UEAAPEAXI@Z"
.quad "??_EForeign@@UEAAPEAXI@Z.mib_sized"

#--- vtable-unflagged.s
.section .rdata,"dr"
.globl "??_7Foreign@@6B@"
"??_7Foreign@@6B@":
  .quad "??_EForeign@@UEAAPEAXI@Z"

#--- ctor-site.s
.text
.globl main
main:
  retq

.section .rdata$mibszf,"dr"
.quad "??0Foreign@@QEAA@XZ"
.quad "??0Foreign@@QEAA@XZ.mib_sized"

#--- weak-alias.s
.section .text,"xr",discard,"??_GForeign@@UEAAPEAXI@Z"
.globl "??_GForeign@@UEAAPEAXI@Z"
"??_GForeign@@UEAAPEAXI@Z":
  retq
.weak "??_EForeign@@UEAAPEAXI@Z"
"??_EForeign@@UEAAPEAXI@Z" = "??_GForeign@@UEAAPEAXI@Z"
.weak "??_EForeign@@UEAAPEAXI@Z.mib_sized"
"??_EForeign@@UEAAPEAXI@Z.mib_sized" = "??_GForeign@@UEAAPEAXI@Z"

#--- mingw-export.s
.section .rdata,"dr"
.globl _ZTV7Foreign
.globl _ZTV7Foreign.mib_sized
_ZTV7Foreign:
_ZTV7Foreign.mib_sized:
  .quad 0

.section .drectve,"yni"
.ascii " -export:_ZTV7Foreign,data -export:_ZTV7Foreign.mib_sized,data"

#--- mingw-site.s
.text
.globl main
main:
  retq

.section .rdata$mibszf,"dr"
.quad _ZTV7Foreign
.quad _ZTV7Foreign.mib_sized

#--- site-inline.s
.text
.globl main
main:
  retq

.section .rdata$mibszf,"dr"
.quad "??_EForeign@@UEAAPEAXI@Z"
.quad "??_EForeign@@UEAAPEAXI@Z.mib_sized"

.section .text,"xr",discard,"??_EForeign@@UEAAPEAXI@Z"
.globl "??_EForeign@@UEAAPEAXI@Z"
.globl "??_EForeign@@UEAAPEAXI@Z.mib_sized"
"??_EForeign@@UEAAPEAXI@Z":
"??_EForeign@@UEAAPEAXI@Z.mib_sized":
  retq

#--- plain.s
.text
.globl main
main:
  callq "??_EForeign@@UEAAPEAXI@Z"
  retq

#--- flagged.s
.text
.globl "??_EForeign@@UEAAPEAXI@Z"
.globl "??_EForeign@@UEAAPEAXI@Z.mib_sized"
"??_EForeign@@UEAAPEAXI@Z":
"??_EForeign@@UEAAPEAXI@Z.mib_sized":
  retq

#--- unflagged.s
.text
.globl "??_EForeign@@UEAAPEAXI@Z"
"??_EForeign@@UEAAPEAXI@Z":
  retq

#--- inline-flagged.s
.section .text,"xr",discard,"??_EForeign@@UEAAPEAXI@Z"
.globl "??_EForeign@@UEAAPEAXI@Z"
.globl "??_EForeign@@UEAAPEAXI@Z.mib_sized"
"??_EForeign@@UEAAPEAXI@Z":
"??_EForeign@@UEAAPEAXI@Z.mib_sized":
  retq

#--- inline-unflagged.s
.section .text,"xr",discard,"??_EForeign@@UEAAPEAXI@Z"
.globl "??_EForeign@@UEAAPEAXI@Z"
"??_EForeign@@UEAAPEAXI@Z":
  retq

#--- lto-main.ll
target triple = "x86_64-pc-windows-msvc"
target datalayout = "e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"

@"??_EForeign@@UEAAPEAXI@Z.mib_sized" = external constant i8
@__mib_sized_ref = private constant [2 x ptr] [ptr @"??_EForeign@@UEAAPEAXI@Z", ptr @"??_EForeign@@UEAAPEAXI@Z.mib_sized"], section ".rdata$mibszf"
@llvm.compiler.used = appending global [1 x ptr] [ptr @__mib_sized_ref], section "llvm.metadata"

declare void @"??_EForeign@@UEAAPEAXI@Z"()

define void @main() {
  ret void
}

#--- lto-flagged.ll
target triple = "x86_64-pc-windows-msvc"
target datalayout = "e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"

define void @"??_EForeign@@UEAAPEAXI@Z"() #0 {
  ret void
}

attributes #0 = { "malterlib-sized" }

#--- lto-unflagged.ll
target triple = "x86_64-pc-windows-msvc"
target datalayout = "e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"

define void @"??_EForeign@@UEAAPEAXI@Z"() {
  ret void
}
