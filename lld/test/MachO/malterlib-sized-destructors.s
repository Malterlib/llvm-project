# REQUIRES: x86
## A definition compiled with -fmalterlib-sized-destructors carries a marker
## symbol at its address, which the construction sites that depend on it refer
## to. A definition compiled without the flag has no marker, and the reference
## stays undefined.

# RUN: rm -rf %t; split-file %s %t
# RUN: llvm-mc -filetype=obj -triple=x86_64-apple-darwin %t/main.s -o %t/main.o
# RUN: llvm-mc -filetype=obj -triple=x86_64-apple-darwin %t/flagged.s -o %t/flagged.o
# RUN: llvm-mc -filetype=obj -triple=x86_64-apple-darwin %t/unflagged.s -o %t/unflagged.o

# RUN: %lld -lSystem %t/main.o %t/flagged.o -o %t/flagged.out
# RUN: not %lld -demangle -lSystem %t/main.o %t/unflagged.o -o /dev/null 2>&1 | FileCheck %s
# CHECK:      error: Foreign::~Foreign() was not compiled with -fmalterlib-sized-destructors
# CHECK-NEXT: >>> its marker __ZN7ForeignD0Ev.mib_sized is undefined
# CHECK-NEXT: >>> an object a construction site makes is destroyed with the size its deleting destructor returns

## The references are in data that dead code stripping removes, and an
## unresolved marker is an error whatever the treatment of the other symbols.
# RUN: not %lld -demangle -lSystem -dead_strip -undefined dynamic_lookup %t/main.o %t/unflagged.o -o /dev/null 2>&1 \
# RUN:   | FileCheck %s

## A weak definition is checked in the copy the link keeps. The marker of a
## copy that lost is left behind, and then belongs to a copy the link did not
## keep.
# RUN: llvm-mc -filetype=obj -triple=x86_64-apple-darwin %t/weak-flagged.s -o %t/weak-flagged.o
# RUN: llvm-mc -filetype=obj -triple=x86_64-apple-darwin %t/weak-unflagged.s -o %t/weak-unflagged.o
# RUN: %lld -lSystem %t/main.o %t/weak-flagged.o %t/weak-unflagged.o -o %t/weak.out
# RUN: not %lld -demangle -lSystem %t/main.o %t/weak-unflagged.o %t/weak-flagged.o -o /dev/null 2>&1 \
# RUN:   | FileCheck %s --check-prefix=MIXED
# MIXED:      error: Foreign::~Foreign() was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
# MIXED-NEXT: >>> defined in {{.*}}weak-unflagged.o
# MIXED-NEXT: >>> marked in {{.*}}weak-flagged.o
## A marker nothing refers to is not an error: the class is not constructed
## through the builtin. The marker the copy that lost left behind does not
## leave the image.
# RUN: %lld -dylib -lSystem %t/weak-unflagged.o %t/weak-flagged.o -o %t/libdisplaced.dylib
# RUN: llvm-nm -g %t/libdisplaced.dylib | FileCheck %s --check-prefix=DISPLACED
# DISPLACED: __ZN7ForeignD0Ev
# DISPLACED-NOT: __ZN7ForeignD0Ev.mib_sized

## A site and an inline definition in one object refer to the marker the
## assembler resolved within the object, and the link may keep another copy.
# RUN: llvm-mc -filetype=obj -triple=x86_64-apple-darwin %t/site-weak.s -o %t/site-weak.o
# RUN: %lld -lSystem -dead_strip %t/site-weak.o %t/weak-unflagged.o -o /dev/null
# RUN: not %lld -demangle -lSystem -dead_strip %t/weak-unflagged.o %t/site-weak.o -o /dev/null 2>&1 \
# RUN:   | FileCheck %s --check-prefix=MIXED-SITE
# MIXED-SITE:      error: Foreign::~Foreign() was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
# MIXED-SITE-NEXT: >>> defined in {{.*}}weak-unflagged.o
# MIXED-SITE-NEXT: >>> marked in {{.*}}site-weak.o

## A definition from a dylib is checked against the marker the dylib exports.
# RUN: %lld -dylib -lSystem %t/flagged.o -o %t/libflagged.dylib
# RUN: %lld -dylib -lSystem %t/unflagged.o -o %t/libunflagged.dylib
# RUN: %lld -lSystem %t/main.o %t/libflagged.dylib -o %t/dylib.out
# RUN: not %lld -demangle -lSystem %t/main.o %t/libunflagged.dylib -o /dev/null 2>&1 | FileCheck %s
## A marker leaves the image with its definition, whatever the list of
## exported symbols says of it.
# RUN: %lld -dylib -lSystem -exported_symbol __ZN7ForeignD0Ev %t/flagged.o -o %t/libexported.dylib
# RUN: llvm-nm -g %t/libexported.dylib | FileCheck %s --check-prefix=EXPORTED-MARKER
# EXPORTED-MARKER: __ZN7ForeignD0Ev
# EXPORTED-MARKER: __ZN7ForeignD0Ev.mib_sized
# RUN: %lld -dylib -lSystem -dead_strip -exported_symbol __ZN7ForeignD0Ev %t/flagged.o -o %t/libexported-ds.dylib
# RUN: llvm-nm -g %t/libexported-ds.dylib | FileCheck %s --check-prefix=EXPORTED-MARKER
## The dylib's marker does not stand in for an object's copy.
# RUN: not %lld -demangle -lSystem %t/main.o %t/unflagged.o %t/libflagged.dylib -o /dev/null 2>&1 \
# RUN:   | FileCheck %s --check-prefix=MIXED-DYLIB
# MIXED-DYLIB:      error: Foreign::~Foreign() was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
# MIXED-DYLIB-NEXT: >>> defined in {{.*}}unflagged.o
# MIXED-DYLIB-NEXT: >>> marked in {{.*}}libflagged.dylib

## dyld coalesces weak definitions across images: a dylib's sites bind at
## runtime to the copy of the first image that defines it, this image's or
## another dylib's.
# RUN: llvm-mc -filetype=obj -triple=x86_64-apple-darwin %t/start.s -o %t/start.o
# RUN: %lld -dylib -lSystem %t/main.o %t/weak-flagged.o -o %t/libsite.dylib
# RUN: %lld -dylib -lSystem %t/weak-unflagged.o -o %t/libweak-unflagged.dylib
# RUN: %lld -lSystem %t/start.o %t/libsite.dylib -o /dev/null
# RUN: %lld -lSystem %t/start.o %t/weak-flagged.o %t/libsite.dylib -o /dev/null
# RUN: %lld -lSystem %t/start.o %t/libsite.dylib %t/libweak-unflagged.dylib -o /dev/null
# RUN: not %lld -demangle -lSystem %t/start.o %t/weak-unflagged.o %t/libsite.dylib -o /dev/null 2>&1 \
# RUN:   | FileCheck %s --check-prefix=COALESCE
# COALESCE:      error: Foreign::~Foreign() was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
# COALESCE-NEXT: >>> defined in {{.*}}weak-unflagged.o
# COALESCE-NEXT: >>> marked in {{.*}}libsite.dylib
# COALESCE-NEXT: >>> the construction sites of {{.*}}libsite.dylib bind to the copy the link keeps at runtime
## Dead stripping keeps what the dylib names.
# RUN: %lld -dylib -lSystem -dead_strip %t/main.o %t/weak-flagged.o -o %t/libsite-ds.dylib
# RUN: not %lld -demangle -lSystem %t/start.o %t/weak-unflagged.o %t/libsite-ds.dylib -o /dev/null 2>&1 \
# RUN:   | FileCheck %s --check-prefix=COALESCE-DS
# COALESCE-DS: error: Foreign::~Foreign() was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
## A dylib another re-exports is loaded with it.
# RUN: %lld -dylib -lSystem %t/start.o -reexport_library %t/libsite.dylib -o %t/libwrapper.dylib
# RUN: %lld -lSystem %t/start.o %t/libwrapper.dylib -o /dev/null
# RUN: not %lld -demangle -lSystem %t/start.o %t/weak-unflagged.o %t/libwrapper.dylib -o /dev/null 2>&1 \
# RUN:   | FileCheck %s --check-prefix=COALESCE-REEXPORT
# COALESCE-REEXPORT: error: Foreign::~Foreign() was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
## A dylib without sites names nothing its sites depend on.
# RUN: %lld -dylib -lSystem %t/weak-flagged.o -o %t/libweak-flagged.dylib
# RUN: %lld -lSystem %t/start.o %t/weak-unflagged.o %t/libweak-flagged.dylib -o /dev/null
## A strong import binds to the dylib its link found it in, whatever another
## dylib of the same names that a later link loads defines.
# RUN: %lld -dylib -lSystem %t/main.o %t/libflagged.dylib -o %t/libfactory-strong.dylib
# RUN: %lld -lSystem %t/start.o %t/libfactory-strong.dylib %t/libunflagged.dylib -o /dev/null
## A dylib the image does not load requires nothing.
# RUN: %lld -lSystem -dead_strip_dylibs %t/start.o %t/weak-unflagged.o %t/libsite.dylib -o /dev/null
# RUN: not %lld -demangle -lSystem %t/start.o %t/libweak-unflagged.dylib %t/libsite.dylib -o /dev/null 2>&1 \
# RUN:   | FileCheck %s --check-prefix=COALESCE-DYLIB
# COALESCE-DYLIB:      error: Foreign::~Foreign() was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
# COALESCE-DYLIB-NEXT: >>> defined in {{.*}}libweak-unflagged.dylib
# COALESCE-DYLIB-NEXT: >>> marked in {{.*}}libsite.dylib
## A copy the image does not export does not take the dylib's place.
# RUN: %lld -lSystem -exported_symbol _main %t/start.o %t/weak-unflagged.o %t/libsite.dylib -o /dev/null

## The marker of a constructor proves its claim only when the vtable it
## installs in its image was compiled with the flag. A dylib whose constructor
## installs its own vtable links, but does not export the marker it cannot
## prove; a site elsewhere then fails.
# RUN: llvm-mc -filetype=obj -triple=x86_64-apple-darwin %t/ctor.s -o %t/ctor.o
# RUN: llvm-mc -filetype=obj -triple=x86_64-apple-darwin %t/vtable-flagged.s -o %t/vtable-flagged.o
# RUN: llvm-mc -filetype=obj -triple=x86_64-apple-darwin %t/vtable-unflagged.s -o %t/vtable-unflagged.o
# RUN: llvm-mc -filetype=obj -triple=x86_64-apple-darwin %t/ctor-site.s -o %t/ctor-site.o
# RUN: %lld -dylib -lSystem %t/ctor.o %t/vtable-flagged.o %t/flagged.o -o %t/libctor-flagged.dylib
# RUN: %lld -dylib -lSystem %t/ctor.o %t/vtable-unflagged.o %t/flagged.o -o %t/libctor-unflagged.dylib
# RUN: %lld -lSystem %t/ctor-site.o %t/libctor-flagged.dylib -o /dev/null
# RUN: not %lld -demangle -lSystem %t/ctor-site.o %t/libctor-unflagged.dylib -o /dev/null 2>&1 | FileCheck %s --check-prefix=CTOR
# CTOR: error: Foreign::Foreign() was not compiled with -fmalterlib-sized-destructors
# RUN: not %lld -demangle -lSystem %t/ctor-site.o %t/ctor.o %t/vtable-unflagged.o %t/flagged.o -o /dev/null 2>&1 \
# RUN:   | FileCheck %s --check-prefix=CHAIN
# CHAIN:      error: Foreign::Foreign() depends on vtable for Foreign
# CHAIN-NEXT: >>> vtable for Foreign was not compiled with -fmalterlib-sized-destructors
## The dependencies of a copy of an owner the link did not keep are not those
## of the copy it kept.
# RUN: llvm-mc -filetype=obj -triple=x86_64-apple-darwin %t/ctor-weak.s -o %t/ctor-weak.o
# RUN: llvm-mc -filetype=obj -triple=x86_64-apple-darwin %t/ctor-strong.s -o %t/ctor-strong.o
# RUN: not %lld -demangle -lSystem %t/ctor-site.o %t/ctor-weak.o %t/vtable-unflagged.o %t/flagged.o -o /dev/null 2>&1 \
# RUN:   | FileCheck %s --check-prefix=CHAIN
# RUN: %lld -lSystem %t/ctor-site.o %t/ctor-weak.o %t/ctor-strong.o %t/vtable-unflagged.o %t/flagged.o -o /dev/null

## A dylib whose sites construct a class another dylib defines names that
## class's constructor, and that dylib names what the proof of its constructor
## depends on that dyld may coalesce. An image whose weak copy takes the place
## of one of those fails, though neither dylib has anything to check in its
## own link.
# RUN: llvm-mc -filetype=obj -triple=x86_64-apple-darwin %t/vtable-weak-flagged.s -o %t/vtable-weak-flagged.o
# RUN: llvm-mc -filetype=obj -triple=x86_64-apple-darwin %t/vtable-weak-unflagged.s -o %t/vtable-weak-unflagged.o
# RUN: %lld -dylib -lSystem %t/ctor.o %t/vtable-weak-flagged.o %t/flagged.o -o %t/libclass.dylib
# RUN: %lld -dylib -lSystem %t/ctor-site.o %t/libclass.dylib -o %t/libfactory.dylib
# RUN: %lld -lSystem %t/start.o %t/libfactory.dylib %t/libclass.dylib -o /dev/null
# RUN: %lld -lSystem %t/start.o %t/vtable-weak-unflagged.o %t/libclass.dylib -o /dev/null
# RUN: not %lld -demangle -lSystem %t/start.o %t/vtable-weak-unflagged.o %t/libfactory.dylib %t/libclass.dylib \
# RUN:   -o /dev/null 2>&1 | FileCheck %s --check-prefix=CHAIN-DYLIB
# CHAIN-DYLIB:      error: vtable for Foreign was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
# CHAIN-DYLIB-NEXT: >>> defined in {{.*}}vtable-weak-unflagged.o
# CHAIN-DYLIB-NEXT: >>> marked in {{.*}}libclass.dylib
# CHAIN-DYLIB-NEXT: >>> the construction sites of {{.*}}libfactory.dylib bind to the copy the link keeps at runtime
## A marker the optimizer folded into its definition still depends on what
## the definition binds.
# RUN: llvm-mc -filetype=obj -triple=x86_64-apple-darwin %t/ctor-folded.s -o %t/ctor-folded.o
# RUN: not %lld -demangle -lSystem %t/ctor-folded.o %t/ctor.o %t/vtable-unflagged.o %t/flagged.o -o /dev/null 2>&1 \
# RUN:   | FileCheck %s --check-prefix=CHAIN
## A strong definition of the image overrides a dylib's weak one at runtime.
# RUN: not %lld -demangle -lSystem %t/start.o %t/unflagged.o %t/libsite.dylib -o /dev/null 2>&1 \
# RUN:   | FileCheck %s --check-prefix=OVERRIDE
# OVERRIDE:      error: Foreign::~Foreign() was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
# OVERRIDE-NEXT: >>> defined in {{.*}}unflagged.o

## A marker belongs to the copy of a definition the link chose for the
## definition's own sake, so a reference to it loads no archive member, such
## as the record of a constructor the optimizer inlined everywhere.
# RUN: llvm-mc -filetype=obj -triple=x86_64-apple-darwin %t/owner-gone.s -o %t/owner-gone.o
# RUN: llvm-mc -filetype=obj -triple=x86_64-apple-darwin %t/ctor-member.s -o %t/ctor-member.o
# RUN: rm -f %t/ctor-member.a && llvm-ar rcs %t/ctor-member.a %t/ctor-member.o
# RUN: %lld -lSystem %t/owner-gone.o %t/ctor-member.a -o /dev/null

## A definition LTO compiles carries its marker into the object LTO produces,
## even when LTO makes it local.
# RUN: llvm-as %t/lto-main.ll -o %t/lto-main.o
# RUN: llvm-as %t/lto-flagged.ll -o %t/lto-flagged.o
# RUN: llvm-as %t/lto-unflagged.ll -o %t/lto-unflagged.o
# RUN: %lld -lSystem %t/main.o %t/lto-flagged.o -o %t/lto.out
# RUN: not %lld -demangle -lSystem %t/main.o %t/lto-unflagged.o -o /dev/null 2>&1 | FileCheck %s
# RUN: %lld -lSystem %t/lto-main.o %t/lto-flagged.o -o %t/lto-local.out
# RUN: not %lld -demangle -lSystem %t/lto-main.o %t/lto-unflagged.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=LTO
# LTO: error: Foreign::~Foreign() was not compiled with -fmalterlib-sized-destructors

#--- main.s
.globl _main
_main:
  retq

.section __DATA_CONST,__mib_sized_ref
.quad __ZN7ForeignD0Ev
.quad __ZN7ForeignD0Ev.mib_sized

.subsections_via_symbols

#--- ctor-weak.s
.globl __ZN7ForeignC1Ev
.weak_definition __ZN7ForeignC1Ev
.globl __ZN7ForeignC1Ev.mib_sized
.weak_definition __ZN7ForeignC1Ev.mib_sized
__ZN7ForeignC1Ev:
__ZN7ForeignC1Ev.mib_sized:
  retq

.section __DATA_CONST,__mib_sized_dep
.quad __ZN7ForeignC1Ev.mib_sized
.quad __ZTV7Foreign
.quad __ZTV7Foreign.mib_sized

.subsections_via_symbols

#--- ctor-strong.s
.globl __ZN7ForeignC1Ev
.globl __ZN7ForeignC1Ev.mib_sized
__ZN7ForeignC1Ev:
__ZN7ForeignC1Ev.mib_sized:
  retq

.subsections_via_symbols

#--- ctor-folded.s
.globl _main
_main:
  retq

.section __DATA_CONST,__mib_sized_ref
.quad __ZN7ForeignC1Ev
.quad __ZN7ForeignC1Ev

.subsections_via_symbols

#--- owner-gone.s
.globl _main
_main:
  retq

.data
.globl __ZTV7Foreign
__ZTV7Foreign:
  .quad 0

.section __DATA_CONST,__mib_sized_dep
.quad __ZN7ForeignC1Ev.mib_sized
.quad __ZTV7Foreign
.quad __ZTV7Foreign.mib_sized

.subsections_via_symbols

#--- ctor-member.s
.globl __ZN7ForeignC1Ev
.globl __ZN7ForeignC1Ev.mib_sized
__ZN7ForeignC1Ev:
__ZN7ForeignC1Ev.mib_sized:
  callq _unrelated
  retq

.subsections_via_symbols

#--- start.s
.globl _main
_main:
  retq

.subsections_via_symbols

#--- ctor.s
.globl __ZN7ForeignC1Ev
.globl __ZN7ForeignC1Ev.mib_sized
__ZN7ForeignC1Ev:
__ZN7ForeignC1Ev.mib_sized:
  retq

.section __DATA_CONST,__mib_sized_dep
.quad __ZN7ForeignC1Ev.mib_sized
.quad __ZTV7Foreign
.quad __ZTV7Foreign.mib_sized

.subsections_via_symbols

#--- vtable-flagged.s
.section __DATA,__data
.globl __ZTV7Foreign
.globl __ZTV7Foreign.mib_sized
__ZTV7Foreign:
__ZTV7Foreign.mib_sized:
  .quad __ZN7ForeignD0Ev

.section __DATA_CONST,__mib_sized_dep
.quad __ZTV7Foreign.mib_sized
.quad __ZN7ForeignD0Ev
.quad __ZN7ForeignD0Ev.mib_sized

.subsections_via_symbols

#--- vtable-weak-flagged.s
.section __DATA,__data
.globl __ZTV7Foreign
.weak_definition __ZTV7Foreign
.globl __ZTV7Foreign.mib_sized
.weak_definition __ZTV7Foreign.mib_sized
__ZTV7Foreign:
__ZTV7Foreign.mib_sized:
  .quad __ZN7ForeignD0Ev

.section __DATA_CONST,__mib_sized_dep
.quad __ZTV7Foreign.mib_sized
.quad __ZN7ForeignD0Ev
.quad __ZN7ForeignD0Ev.mib_sized

.subsections_via_symbols

#--- vtable-weak-unflagged.s
.section __DATA,__data
.globl __ZTV7Foreign
.weak_definition __ZTV7Foreign
__ZTV7Foreign:
  .quad __ZN7ForeignD0Ev

.subsections_via_symbols

#--- vtable-unflagged.s
.section __DATA,__data
.globl __ZTV7Foreign
__ZTV7Foreign:
  .quad __ZN7ForeignD0Ev

.subsections_via_symbols

#--- ctor-site.s
.globl _main
_main:
  retq

.section __DATA_CONST,__mib_sized_ref
.quad __ZN7ForeignC1Ev
.quad __ZN7ForeignC1Ev.mib_sized

.subsections_via_symbols

#--- flagged.s
.globl __ZN7ForeignD0Ev
.globl __ZN7ForeignD0Ev.mib_sized
__ZN7ForeignD0Ev:
__ZN7ForeignD0Ev.mib_sized:
  retq

.subsections_via_symbols

#--- unflagged.s
.globl __ZN7ForeignD0Ev
__ZN7ForeignD0Ev:
  retq

.subsections_via_symbols

#--- site-weak.s
.globl _main
_main:
  retq

.section __DATA_CONST,__mib_sized_ref
.quad __ZN7ForeignD0Ev
.quad __ZN7ForeignD0Ev.mib_sized

.text
.globl __ZN7ForeignD0Ev
.weak_definition __ZN7ForeignD0Ev
.globl __ZN7ForeignD0Ev.mib_sized
.weak_definition __ZN7ForeignD0Ev.mib_sized
__ZN7ForeignD0Ev:
__ZN7ForeignD0Ev.mib_sized:
  retq

.subsections_via_symbols

#--- weak-flagged.s
.globl __ZN7ForeignD0Ev
.weak_definition __ZN7ForeignD0Ev
.globl __ZN7ForeignD0Ev.mib_sized
.weak_definition __ZN7ForeignD0Ev.mib_sized
__ZN7ForeignD0Ev:
__ZN7ForeignD0Ev.mib_sized:
  retq

.subsections_via_symbols

#--- weak-unflagged.s
.globl __ZN7ForeignD0Ev
.weak_definition __ZN7ForeignD0Ev
__ZN7ForeignD0Ev:
  retq

.subsections_via_symbols

#--- lto-main.ll
target triple = "x86_64-apple-darwin"
target datalayout = "e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"

@_ZN7ForeignD0Ev.mib_sized = external constant i8
@__mib_sized_ref = private constant [2 x ptr] [ptr @_ZN7ForeignD0Ev, ptr @_ZN7ForeignD0Ev.mib_sized], section "__DATA_CONST,__mib_sized_ref"
@llvm.compiler.used = appending global [1 x ptr] [ptr @__mib_sized_ref], section "llvm.metadata"

declare void @_ZN7ForeignD0Ev()

define void @main() {
  ret void
}

#--- lto-flagged.ll
target triple = "x86_64-apple-darwin"
target datalayout = "e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"

define void @_ZN7ForeignD0Ev() #0 {
  ret void
}

attributes #0 = { "malterlib-sized" }

#--- lto-unflagged.ll
target triple = "x86_64-apple-darwin"
target datalayout = "e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"

define void @_ZN7ForeignD0Ev() {
  ret void
}
