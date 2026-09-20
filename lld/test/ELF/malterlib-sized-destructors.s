# REQUIRES: x86
## A definition compiled with -fmalterlib-sized-destructors carries a marker
## symbol at its address, which the construction sites that depend on it refer
## to. A definition compiled without the flag has no marker, and the reference
## stays undefined.

# RUN: rm -rf %t; split-file %s %t
# RUN: llvm-mc -filetype=obj -triple=x86_64 %t/main.s -o %t/main.o
# RUN: llvm-mc -filetype=obj -triple=x86_64 %t/flagged.s -o %t/flagged.o
# RUN: llvm-mc -filetype=obj -triple=x86_64 %t/unflagged.s -o %t/unflagged.o

# RUN: ld.lld %t/main.o %t/flagged.o -o %t/flagged.out
# RUN: not ld.lld %t/main.o %t/unflagged.o -o /dev/null 2>&1 | FileCheck %s
# CHECK:      error: Foreign::~Foreign() was not compiled with -fmalterlib-sized-destructors
# CHECK-NEXT: >>> its marker _ZN7ForeignD0Ev.mib_sized is undefined
# CHECK-NEXT: >>> an object a construction site makes is destroyed with the size its deleting destructor returns

## A 32-bit target keeps the offset of a reference in the section contents.
# RUN: llvm-mc -filetype=obj -triple=i386 %t/main32.s -o %t/main32.o
# RUN: llvm-mc -filetype=obj -triple=i386 %t/flagged32.s -o %t/flagged32.o
# RUN: llvm-mc -filetype=obj -triple=i386 %t/unflagged32.s -o %t/unflagged32.o
# RUN: ld.lld %t/main32.o %t/flagged32.o -o /dev/null
# RUN: not ld.lld %t/main32.o %t/unflagged32.o -o /dev/null 2>&1 | FileCheck %s

## The references are in data that dead code stripping removes, and an
## unresolved marker is an error whatever the policy for the other symbols.
# RUN: not ld.lld --gc-sections --unresolved-symbols=ignore-all %t/main.o %t/unflagged.o -o /dev/null 2>&1 \
# RUN:   | FileCheck %s

## An inline definition is checked in the copy the link keeps. The marker is in
## the definition's section group, so it is discarded with the copies that
## lose, and a copy from code without the flag has none.
# RUN: llvm-mc -filetype=obj -triple=x86_64 %t/inline-flagged.s -o %t/inline-flagged.o
# RUN: llvm-mc -filetype=obj -triple=x86_64 %t/inline-unflagged.s -o %t/inline-unflagged.o
# RUN: ld.lld %t/main.o %t/inline-flagged.o %t/inline-unflagged.o -o %t/inline.out
# RUN: llvm-nm %t/inline.out | FileCheck %s --check-prefix=INLINE-SYMS
# INLINE-SYMS: _ZN7ForeignD0Ev.mib_sized
# RUN: not ld.lld %t/main.o %t/inline-unflagged.o %t/inline-flagged.o -o /dev/null 2>&1 | FileCheck %s

## A site and an inline definition in one object refer to the marker the
## assembler resolved within the object, and the link may keep another copy.
# RUN: llvm-mc -filetype=obj -triple=x86_64 %t/site-inline.s -o %t/site-inline.o
# RUN: ld.lld --gc-sections %t/site-inline.o %t/inline-unflagged.o -o /dev/null
# RUN: not ld.lld --gc-sections %t/inline-unflagged.o %t/site-inline.o -o /dev/null 2>&1 | FileCheck %s

## A weak definition outside a section group leaves its marker behind when
## another copy displaces it; the marker then belongs to a copy the link did
## not keep.
# RUN: llvm-mc -filetype=obj -triple=x86_64 %t/weak-flagged.s -o %t/weak-flagged.o
# RUN: llvm-mc -filetype=obj -triple=x86_64 %t/weak-unflagged.s -o %t/weak-unflagged.o
# RUN: ld.lld %t/main.o %t/weak-flagged.o %t/weak-unflagged.o -o /dev/null
# RUN: not ld.lld %t/main.o %t/weak-unflagged.o %t/weak-flagged.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=MIXED
# MIXED:      error: Foreign::~Foreign() was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
# MIXED-NEXT: >>> defined in {{.*}}weak-unflagged.o
# MIXED-NEXT: >>> marked in {{.*}}weak-flagged.o
## A marker nothing refers to is not an error: the class is not constructed
## through the builtin. The marker the copy that lost left behind does not
## leave the image.
# RUN: ld.lld -shared %t/weak-unflagged.o %t/weak-flagged.o -o %t/displaced.so
# RUN: llvm-nm -D %t/displaced.so | FileCheck %s --check-prefix=DISPLACED
# DISPLACED: _ZN7ForeignD0Ev
# DISPLACED-NOT: _ZN7ForeignD0Ev.mib_sized
# RUN: ld.lld -shared %t/inline-unflagged.o %t/inline-flagged.o -o /dev/null

## A definition from a shared library is checked against the marker the
## library exports.
# RUN: ld.lld -shared %t/flagged.o -o %t/libflagged.so
# RUN: ld.lld -shared %t/unflagged.o -o %t/libunflagged.so
# RUN: ld.lld %t/main.o %t/libflagged.so -o %t/shared.out
# RUN: not ld.lld %t/main.o %t/libunflagged.so -o /dev/null 2>&1 | FileCheck %s
## The library's marker does not stand in for an object's copy.
# RUN: not ld.lld %t/main.o %t/unflagged.o %t/libflagged.so -o /dev/null 2>&1 | FileCheck %s --check-prefix=MIXED-SO
# MIXED-SO:      error: Foreign::~Foreign() was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
# MIXED-SO-NEXT: >>> defined in {{.*}}unflagged.o
# MIXED-SO-NEXT: >>> marked in {{.*}}libflagged.so
## A shared library's sites bind at runtime to the copy the dynamic loader
## finds first: a copy this image exports, or another library's. The library
## names the definitions its sites depend on that another image may preempt;
## a library without sites, or one that binds its references to its own
## definitions, names none.
# RUN: llvm-mc -filetype=obj -triple=x86_64 %t/start.s -o %t/start.o
# RUN: ld.lld -shared %t/main.o %t/flagged.o -o %t/libsite.so
# RUN: ld.lld %t/start.o %t/libsite.so -o /dev/null
# RUN: ld.lld %t/start.o %t/flagged.o %t/libsite.so -o /dev/null
# RUN: ld.lld %t/start.o %t/libsite.so %t/libunflagged.so -o /dev/null
# RUN: not ld.lld %t/start.o %t/unflagged.o %t/libsite.so -o /dev/null 2>&1 | FileCheck %s --check-prefix=PREEMPT
# PREEMPT:      error: Foreign::~Foreign() was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
# PREEMPT-NEXT: >>> defined in {{.*}}unflagged.o
# PREEMPT-NEXT: >>> marked in {{.*}}libsite.so
# PREEMPT-NEXT: >>> the construction sites of {{.*}}libsite.so bind to the copy the link keeps at runtime
# RUN: ld.lld %t/start.o %t/unflagged.o %t/libflagged.so -o /dev/null
## A library --as-needed drops is not loaded, and requires nothing.
# RUN: ld.lld %t/start.o %t/unflagged.o --as-needed %t/libsite.so -o /dev/null
## A library passes what the libraries it loads require on to the links of the
## images that load it, which may not load those libraries.
# RUN: ld.lld -shared %t/start.o %t/libsite.so -o %t/libwrap.so
# RUN: not ld.lld --export-dynamic --allow-shlib-undefined %t/start.o %t/unflagged.o %t/libwrap.so -o /dev/null 2>&1 \
# RUN:   | FileCheck %s --check-prefix=INDIRECT
# INDIRECT:      error: Foreign::~Foreign() was not compiled with -fmalterlib-sized-destructors
# INDIRECT-NEXT: >>> the construction sites of {{.*}}libwrap.so bind to the copy the link keeps at runtime
# RUN: ld.lld -shared -Bsymbolic %t/main.o %t/flagged.o -o %t/libsite-symbolic.so
# RUN: ld.lld %t/start.o %t/unflagged.o %t/libsite-symbolic.so -o /dev/null
# RUN: not ld.lld %t/start.o %t/libunflagged.so %t/libsite.so -o /dev/null 2>&1 | FileCheck %s --check-prefix=PREEMPT-SO
# PREEMPT-SO:      error: Foreign::~Foreign() was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
# PREEMPT-SO-NEXT: >>> defined in {{.*}}libunflagged.so
# PREEMPT-SO-NEXT: >>> marked in {{.*}}libsite.so
## A copy the image does not export does not take the library's place.
# RUN: ld.lld %t/start.o %t/unflagged.o %t/libsite.so --version-script=%t/local.ver -o /dev/null

## A marker leaves the image with its definition, whatever the version script
## says of it.
# RUN: ld.lld -shared --version-script=%t/export-dtor.ver %t/flagged.o -o %t/libexported.so
# RUN: llvm-nm -D %t/libexported.so | FileCheck %s --check-prefix=EXPORTED-MARKER
# EXPORTED-MARKER: _ZN7ForeignD0Ev
# EXPORTED-MARKER: _ZN7ForeignD0Ev.mib_sized

## A marker belongs to the copy of a definition the link chose for the
## definition's own sake, so a reference to it loads no archive member, such
## as the record of a constructor the optimizer inlined everywhere.
# RUN: llvm-mc -filetype=obj -triple=x86_64 %t/owner-gone.s -o %t/owner-gone.o
# RUN: llvm-mc -filetype=obj -triple=x86_64 %t/ctor-member.s -o %t/ctor-member.o
# RUN: rm -f %t/ctor-member.a && llvm-ar rcs %t/ctor-member.a %t/ctor-member.o
# RUN: ld.lld %t/owner-gone.o %t/ctor-member.a -o /dev/null
## Nor is an unresolved marker an import of a shared library.
# RUN: ld.lld -shared %t/owner-gone.o -o %t/libowner-gone.so
# RUN: llvm-nm -D %t/libowner-gone.so | FileCheck %s --check-prefix=NO-MARKER-IMPORT
# NO-MARKER-IMPORT-NOT: mib_sized
# RUN: ld.lld %t/start.o %t/libowner-gone.so -o /dev/null

## A relocatable output keeps the references for the final link.
# RUN: ld.lld -r %t/main.o -o %t/main-r.o
# RUN: ld.lld %t/main-r.o %t/flagged.o -o /dev/null
# RUN: not ld.lld %t/main-r.o %t/unflagged.o -o /dev/null 2>&1 | FileCheck %s

## The marker of a constructor proves its claim only when the vtable it
## installs in its image was compiled with the flag. A shared library whose
## constructor installs its own vtable, bound to its own destructor, links, but
## does not export the marker it cannot prove; a site elsewhere then fails.
# RUN: llvm-mc -filetype=obj -triple=x86_64 %t/ctor.s -o %t/ctor.o
# RUN: llvm-mc -filetype=obj -triple=x86_64 %t/vtable-flagged.s -o %t/vtable-flagged.o
# RUN: llvm-mc -filetype=obj -triple=x86_64 %t/vtable-unflagged.s -o %t/vtable-unflagged.o
# RUN: llvm-mc -filetype=obj -triple=x86_64 %t/ctor-site.s -o %t/ctor-site.o
# RUN: ld.lld -shared -Bsymbolic %t/ctor.o %t/vtable-flagged.o %t/flagged.o -o %t/libctor-flagged.so
# RUN: ld.lld -shared -Bsymbolic %t/ctor.o %t/vtable-unflagged.o %t/flagged.o -o %t/libctor-unflagged.so
# RUN: llvm-nm -D %t/libctor-flagged.so | FileCheck %s --check-prefix=EXPORTED
# RUN: llvm-nm -D %t/libctor-unflagged.so | FileCheck %s --check-prefix=HIDDEN
# EXPORTED: _ZN7ForeignC1Ev.mib_sized
# HIDDEN-NOT: _ZN7ForeignC1Ev.mib_sized
# RUN: ld.lld %t/ctor-site.o %t/libctor-flagged.so -o /dev/null
# RUN: not ld.lld %t/ctor-site.o %t/libctor-unflagged.so -o /dev/null 2>&1 | FileCheck %s --check-prefix=CTOR
# CTOR: error: Foreign::Foreign() was not compiled with -fmalterlib-sized-destructors
## In one link, the site reports the dependency that fails.
# RUN: not ld.lld %t/ctor-site.o %t/ctor.o %t/vtable-unflagged.o %t/flagged.o -o /dev/null 2>&1 \
# RUN:   | FileCheck %s --check-prefix=CHAIN
# CHAIN:      error: Foreign::Foreign() depends on vtable for Foreign
# CHAIN-NEXT: >>> vtable for Foreign was not compiled with -fmalterlib-sized-destructors
# RUN: ld.lld %t/ctor-site.o %t/ctor.o %t/vtable-flagged.o %t/flagged.o -o /dev/null
## The dependencies of a copy of an owner the link did not keep are not those
## of the copy it kept.
# RUN: llvm-mc -filetype=obj -triple=x86_64 %t/ctor-weak.s -o %t/ctor-weak.o
# RUN: llvm-mc -filetype=obj -triple=x86_64 %t/ctor-strong.s -o %t/ctor-strong.o
# RUN: not ld.lld %t/ctor-site.o %t/ctor-weak.o %t/vtable-unflagged.o %t/flagged.o -o /dev/null 2>&1 \
# RUN:   | FileCheck %s --check-prefix=CHAIN
# RUN: ld.lld %t/ctor-site.o %t/ctor-weak.o %t/ctor-strong.o %t/vtable-unflagged.o %t/flagged.o -o /dev/null
## A record the compiler emits names its owner by a local label, so it names
## the copy it came with, even once a relocatable link merged the copies.
# RUN: llvm-mc -filetype=obj -triple=x86_64 %t/ctor-weak-local.s -o %t/ctor-weak-local.o
# RUN: ld.lld -r %t/ctor-weak-local.o %t/ctor-strong.o -o %t/ctor-merged.o
# RUN: ld.lld %t/ctor-site.o %t/ctor-merged.o %t/vtable-unflagged.o %t/flagged.o -o /dev/null
# RUN: not ld.lld %t/ctor-site.o %t/ctor-weak-local.o %t/vtable-unflagged.o %t/flagged.o -o /dev/null 2>&1 \
# RUN:   | FileCheck %s --check-prefix=CHAIN
## A library whose sites construct a class another library defines names that
## class's constructor, and that library names what the proof of its
## constructor and vtable depends on. An image that takes the place of one of
## those fails, though neither library has anything to check in its own link.
# RUN: ld.lld -shared %t/ctor.o %t/vtable-flagged.o %t/flagged.o -soname libclass.so -o %t/libclass.so
# RUN: ld.lld -shared %t/ctor-site.o %t/libclass.so -o %t/libfactory.so
# RUN: ld.lld %t/start.o %t/libfactory.so %t/libclass.so -o /dev/null
# RUN: ld.lld %t/start.o %t/vtable-unflagged.o %t/libclass.so -o /dev/null
# RUN: not ld.lld %t/start.o %t/vtable-unflagged.o %t/libfactory.so %t/libclass.so -o /dev/null 2>&1 \
# RUN:   | FileCheck %s --check-prefix=CHAIN-SO
# CHAIN-SO:      error: vtable for Foreign was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
# CHAIN-SO-NEXT: >>> defined in {{.*}}vtable-unflagged.o
# CHAIN-SO-NEXT: >>> marked in {{.*}}libclass.so
# CHAIN-SO-NEXT: >>> the construction sites of {{.*}}libfactory.so bind to the copy the link keeps at runtime
# RUN: not ld.lld %t/start.o %t/unflagged.o %t/libfactory.so %t/libclass.so -o /dev/null 2>&1 \
# RUN:   | FileCheck %s --check-prefix=CHAIN-SO-DTOR
# CHAIN-SO-DTOR:      error: Foreign::~Foreign() was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
# CHAIN-SO-DTOR-NEXT: >>> defined in {{.*}}unflagged.o

## The records of an owner in a section group are members of the group, and
## garbage collection does not bring them back with the owner.
# RUN: llvm-mc -filetype=obj -triple=x86_64 %t/ctor-group.s -o %t/ctor-group.o
# RUN: llvm-mc -filetype=obj -triple=x86_64 %t/ctor-group-user.s -o %t/ctor-group-user.o
# RUN: ld.lld -pie --gc-sections %t/ctor-group-user.o %t/ctor-group.o %t/vtable-unflagged.o %t/flagged.o -o %t/ctor-group.out
# RUN: llvm-readelf -r %t/ctor-group.out | FileCheck %s --check-prefix=GROUP-RELOCS
# GROUP-RELOCS-NOT: {{^0+ }}

## The references leave the link, even where a linker script keeps their
## sections: an undefined marker nothing needs is not an error.
# RUN: ld.lld --gc-sections -T %t/keep.lds %t/start.o %t/ctor.o %t/vtable-unflagged.o %t/flagged.o -o /dev/null

## A marker the optimizer folded into its definition still depends on what
## the definition binds.
# RUN: llvm-mc -filetype=obj -triple=x86_64 %t/ctor-folded.s -o %t/ctor-folded.o
# RUN: not ld.lld %t/ctor-folded.o %t/ctor.o %t/vtable-unflagged.o %t/flagged.o -o /dev/null 2>&1 \
# RUN:   | FileCheck %s --check-prefix=CHAIN

## A definition LTO compiles carries its marker into the object LTO produces,
## even when LTO makes it local.
# RUN: llvm-as %t/lto-main.ll -o %t/lto-main.bc
# RUN: llvm-as %t/lto-flagged.ll -o %t/lto-flagged.bc
# RUN: llvm-as %t/lto-unflagged.ll -o %t/lto-unflagged.bc
# RUN: ld.lld %t/main.o %t/lto-flagged.bc -o %t/lto.out
# RUN: not ld.lld %t/main.o %t/lto-unflagged.bc -o /dev/null 2>&1 | FileCheck %s
# RUN: ld.lld %t/lto-main.bc %t/lto-flagged.bc -o %t/lto-local.out
# RUN: not ld.lld %t/lto-main.bc %t/lto-unflagged.bc -o /dev/null 2>&1 | FileCheck %s --check-prefix=LTO
# LTO: error: Foreign::~Foreign() was not compiled with -fmalterlib-sized-destructors
## LTO keeps one copy of an inline definition, and the marker of a copy it does
## not keep goes with it.
# RUN: llvm-as %t/lto-inline-flagged.ll -o %t/lto-inline-flagged.bc
# RUN: llvm-as %t/lto-inline-unflagged.ll -o %t/lto-inline-unflagged.bc
# RUN: ld.lld %t/lto-main.bc %t/lto-inline-flagged.bc %t/lto-inline-unflagged.bc -o /dev/null
# RUN: not ld.lld %t/lto-main.bc %t/lto-inline-unflagged.bc %t/lto-inline-flagged.bc -o /dev/null 2>&1 \
# RUN:   | FileCheck %s --check-prefix=LTO

#--- main.s
.globl _start
_start:
  ret

.section .data.rel.ro.__mib_sized_ref,"aw",@progbits
.quad _ZN7ForeignD0Ev
.quad _ZN7ForeignD0Ev.mib_sized

#--- ctor-weak.s
.weak _ZN7ForeignC1Ev
.weak _ZN7ForeignC1Ev.mib_sized
_ZN7ForeignC1Ev:
_ZN7ForeignC1Ev.mib_sized:
  ret

.section .data.rel.ro.__mib_sized_dep,"aw",@progbits
.quad _ZN7ForeignC1Ev.mib_sized
.quad _ZTV7Foreign
.quad _ZTV7Foreign.mib_sized

#--- ctor-weak-local.s
.weak _ZN7ForeignC1Ev
.weak _ZN7ForeignC1Ev.mib_sized
_ZN7ForeignC1Ev:
_ZN7ForeignC1Ev.mib_sized:
.Lowner:
  ret

.section .data.rel.ro.__mib_sized_dep,"aw",@progbits
.quad .Lowner
.quad _ZTV7Foreign
.quad _ZTV7Foreign.mib_sized

#--- ctor-strong.s
.globl _ZN7ForeignC1Ev
.globl _ZN7ForeignC1Ev.mib_sized
_ZN7ForeignC1Ev:
_ZN7ForeignC1Ev.mib_sized:
  ret

#--- ctor-folded.s
.globl _start
_start:
  ret

.section .data.rel.ro.__mib_sized_ref,"aw",@progbits
.quad _ZN7ForeignC1Ev
.quad _ZN7ForeignC1Ev

#--- ctor-group.s
.section .text._ZN7ForeignC1Ev,"axG",@progbits,_ZN7ForeignC1Ev,comdat
.globl _ZN7ForeignC1Ev
_ZN7ForeignC1Ev:
.Lowner:
  leaq _ZTV7Foreign(%rip), %rax
  ret

.section .data.rel.ro.__mib_sized_dep,"awG",@progbits,_ZN7ForeignC1Ev,comdat
.quad .Lowner
.quad _ZTV7Foreign
.quad _ZTV7Foreign.mib_sized

#--- ctor-group-user.s
.globl _start
_start:
  call _ZN7ForeignC1Ev
  ret

#--- owner-gone.s
.globl _start
_start:
  ret

.data
.globl _ZTV7Foreign
_ZTV7Foreign:
  .quad 0

.section .data.rel.ro.__mib_sized_dep,"aw",@progbits
.quad _ZN7ForeignC1Ev.mib_sized
.quad _ZTV7Foreign
.quad _ZTV7Foreign.mib_sized

#--- ctor-member.s
.globl _ZN7ForeignC1Ev
.globl _ZN7ForeignC1Ev.mib_sized
_ZN7ForeignC1Ev:
_ZN7ForeignC1Ev.mib_sized:
  call unrelated
  ret

#--- start.s
.globl _start
_start:
  ret

#--- keep.lds
SECTIONS {
  .text : { *(.text*) }
  .data.rel.ro : { KEEP(*(.data.rel.ro.*)) }
}

#--- export-dtor.ver
{ global: _ZN7ForeignD0Ev; local: *; };

#--- local.ver
{ local: _ZN7Foreign*; };

#--- ctor.s
.globl _ZN7ForeignC1Ev
.globl _ZN7ForeignC1Ev.mib_sized
_ZN7ForeignC1Ev:
_ZN7ForeignC1Ev.mib_sized:
  ret

.section .data.rel.ro.__mib_sized_dep,"aw",@progbits
.quad _ZN7ForeignC1Ev.mib_sized
.quad _ZTV7Foreign
.quad _ZTV7Foreign.mib_sized

#--- vtable-flagged.s
.data
.globl _ZTV7Foreign
.globl _ZTV7Foreign.mib_sized
_ZTV7Foreign:
_ZTV7Foreign.mib_sized:
  .quad _ZN7ForeignD0Ev

.section .data.rel.ro.__mib_sized_dep,"aw",@progbits
.quad _ZTV7Foreign.mib_sized
.quad _ZN7ForeignD0Ev
.quad _ZN7ForeignD0Ev.mib_sized

#--- vtable-unflagged.s
.data
.globl _ZTV7Foreign
_ZTV7Foreign:
  .quad _ZN7ForeignD0Ev

#--- ctor-site.s
.globl _start
_start:
  ret

.section .data.rel.ro.__mib_sized_ref,"aw",@progbits
.quad _ZN7ForeignC1Ev
.quad _ZN7ForeignC1Ev.mib_sized

#--- main32.s
.globl _start
_start:
  ret

.section .data.rel.ro.__mib_sized_ref,"aw",@progbits
.long _ZN7ForeignD0Ev
.long _ZN7ForeignD0Ev.mib_sized

#--- flagged32.s
## The definition is not at the start of its section.
.globl other
other:
  ret
.globl _ZN7ForeignD0Ev
.globl _ZN7ForeignD0Ev.mib_sized
_ZN7ForeignD0Ev:
_ZN7ForeignD0Ev.mib_sized:
  ret

#--- unflagged32.s
.globl _ZN7ForeignD0Ev
_ZN7ForeignD0Ev:
  ret

#--- flagged.s
.globl _ZN7ForeignD0Ev
.globl _ZN7ForeignD0Ev.mib_sized
_ZN7ForeignD0Ev:
_ZN7ForeignD0Ev.mib_sized:
  ret

#--- unflagged.s
.globl _ZN7ForeignD0Ev
_ZN7ForeignD0Ev:
  ret

#--- site-inline.s
.globl _start
_start:
  ret

.section .data.rel.ro.__mib_sized_ref,"aw",@progbits
.quad _ZN7ForeignD0Ev
.quad _ZN7ForeignD0Ev.mib_sized

.section .text._ZN7ForeignD0Ev,"axG",@progbits,_ZN7ForeignD0Ev,comdat
.weak _ZN7ForeignD0Ev
.weak _ZN7ForeignD0Ev.mib_sized
_ZN7ForeignD0Ev:
_ZN7ForeignD0Ev.mib_sized:
  ret

#--- inline-flagged.s
.section .text._ZN7ForeignD0Ev,"axG",@progbits,_ZN7ForeignD0Ev,comdat
.weak _ZN7ForeignD0Ev
.weak _ZN7ForeignD0Ev.mib_sized
_ZN7ForeignD0Ev:
_ZN7ForeignD0Ev.mib_sized:
  ret

#--- inline-unflagged.s
.section .text._ZN7ForeignD0Ev,"axG",@progbits,_ZN7ForeignD0Ev,comdat
.weak _ZN7ForeignD0Ev
_ZN7ForeignD0Ev:
  ret

#--- weak-flagged.s
.weak _ZN7ForeignD0Ev
.weak _ZN7ForeignD0Ev.mib_sized
_ZN7ForeignD0Ev:
_ZN7ForeignD0Ev.mib_sized:
  ret

#--- weak-unflagged.s
.weak _ZN7ForeignD0Ev
_ZN7ForeignD0Ev:
  ret

#--- lto-main.ll
target triple = "x86_64-unknown-linux-gnu"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"

@_ZN7ForeignD0Ev.mib_sized = external constant i8
@__mib_sized_ref = private constant [2 x ptr] [ptr @_ZN7ForeignD0Ev, ptr @_ZN7ForeignD0Ev.mib_sized], section ".data.rel.ro.__mib_sized_ref"
@llvm.compiler.used = appending global [1 x ptr] [ptr @__mib_sized_ref], section "llvm.metadata"

declare void @_ZN7ForeignD0Ev()

define void @_start() {
  ret void
}

#--- lto-flagged.ll
target triple = "x86_64-unknown-linux-gnu"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"

define void @_ZN7ForeignD0Ev() #0 {
  ret void
}

attributes #0 = { "malterlib-sized" }

#--- lto-unflagged.ll
target triple = "x86_64-unknown-linux-gnu"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"

define void @_ZN7ForeignD0Ev() {
  ret void
}

#--- lto-inline-flagged.ll
target triple = "x86_64-unknown-linux-gnu"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"

$_ZN7ForeignD0Ev = comdat any

define linkonce_odr void @_ZN7ForeignD0Ev() #0 comdat {
  ret void
}

attributes #0 = { "malterlib-sized" }

#--- lto-inline-unflagged.ll
target triple = "x86_64-unknown-linux-gnu"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"

$_ZN7ForeignD0Ev = comdat any

define linkonce_odr void @_ZN7ForeignD0Ev() comdat {
  ret void
}
