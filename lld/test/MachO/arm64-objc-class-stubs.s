# REQUIRES: aarch64

# Tests arm64 ObjC class stub emission for fast/small stubs, local/dylib/archive
# classes, autolinked archives, dynamic lookup, malformed names, and missing
# classes.

# RUN: rm -rf %t && split-file %s %t

# Check fast and small stubs for regular selector calls, local class calls, and
# dylib class calls.
# RUN: llvm-mc -filetype=obj -triple=arm64-apple-darwin %t/main.s -o %t/main.o
# RUN: llvm-mc -filetype=obj -triple=arm64-apple-darwin %t/external.s -o %t/external.o
# RUN: %lld -arch arm64 -dylib -install_name @executable_path/libexternal.dylib \
# RUN:   -o %t/libexternal.dylib %t/external.o
# RUN: %lld -arch arm64 -lSystem -o %t/fast.out %t/main.o \
# RUN:   %t/libexternal.dylib -objc_stubs_fast
# RUN: llvm-objdump --no-show-raw-insn --section=__TEXT,__objc_stubs --syms \
# RUN:   --macho %t/fast.out | FileCheck %s --check-prefix=FAST
# RUN: %lld -arch arm64 -lSystem -o %t/small.out %t/main.o \
# RUN:   %t/libexternal.dylib -objc_stubs_small
# RUN: llvm-objdump --no-show-raw-insn --section=__TEXT,__objc_stubs --syms \
# RUN:   --macho %t/small.out | FileCheck %s --check-prefix=SMALL
# RUN: llvm-mc -filetype=obj -triple=arm64e-apple-darwin %t/main.s \
# RUN:   -o %t/main-arm64e.o
# RUN: llvm-mc -filetype=obj -triple=arm64e-apple-darwin %t/external.s \
# RUN:   -o %t/external-arm64e.o
# RUN: %lld -arch arm64e -dylib -install_name @executable_path/libexternal.dylib \
# RUN:   -o %t/libexternal-arm64e.dylib %t/external-arm64e.o
# RUN: %lld -arch arm64e -lSystem -o %t/fast-arm64e.out %t/main-arm64e.o \
# RUN:   %t/libexternal-arm64e.dylib -objc_stubs_fast
# RUN: llvm-objdump --no-show-raw-insn --section=__TEXT,__objc_stubs --syms \
# RUN:   --macho %t/fast-arm64e.out | FileCheck %s --check-prefix=FAST

# Check archive extraction through class-stub references.
# RUN: llvm-mc -filetype=obj -triple=arm64-apple-darwin %t/archive-main.s \
# RUN:   -o %t/archive-main.o
# RUN: llvm-mc -filetype=obj -triple=arm64-apple-darwin %t/archive-class.s \
# RUN:   -o %t/archive-class.o
# RUN: llvm-ar rcs %t/libarchive.a %t/archive-class.o
# RUN: %lld -arch arm64 -lSystem -o %t/archive.out %t/archive-main.o \
# RUN:   %t/libarchive.a -objc_stubs_fast -U _objc_msgSend
# RUN: llvm-objdump --no-show-raw-insn --section=__TEXT,__objc_stubs \
# RUN:   --macho %t/archive.out | FileCheck %s --check-prefix=ARCHIVE
# RUN: llvm-nm %t/archive.out | FileCheck %s --check-prefix=ARCHIVE-SYMS
# RUN: %lld -arch arm64 -lSystem -o %t/start-lib.out %t/archive-main.o \
# RUN:   --start-lib %t/archive-class.o --end-lib -objc_stubs_fast \
# RUN:   -U _objc_msgSend
# RUN: llvm-objdump --no-show-raw-insn --section=__TEXT,__objc_stubs \
# RUN:   --macho %t/start-lib.out | FileCheck %s --check-prefix=ARCHIVE
# RUN: llvm-nm %t/start-lib.out | FileCheck %s --check-prefix=ARCHIVE-SYMS
## The class is an alias in the member, which the extraction, in place for a
## --start-lib object, has to resolve.
# RUN: llvm-mc -filetype=obj -triple=arm64-apple-darwin %t/archive-alias.s \
# RUN:   -o %t/archive-alias.o
# RUN: %lld -arch arm64 -lSystem -o %t/start-lib-alias.out %t/archive-main.o \
# RUN:   --start-lib %t/archive-alias.o --end-lib -objc_stubs_fast \
# RUN:   -U _objc_msgSend
# RUN: llvm-objdump --no-show-raw-insn --section=__TEXT,__objc_stubs \
# RUN:   --macho %t/start-lib-alias.out | FileCheck %s --check-prefix=ARCHIVE
# RUN: llvm-nm %t/start-lib-alias.out | FileCheck %s --check-prefix=ARCHIVE-SYMS

# Check autolink extraction when the archive member defines the class.
# RUN: llvm-mc -filetype=obj -triple=arm64-apple-darwin %t/autolink-main-a.s \
# RUN:   -o %t/autolink-main-a.o
# RUN: llvm-mc -filetype=obj -triple=arm64-apple-darwin %t/autolink-dep-a.s \
# RUN:   -o %t/autolink-dep-a.o
# RUN: llvm-ar rcs %t/libautolinka.a %t/autolink-dep-a.o
# RUN: %lld -arch arm64 -lSystem -o %t/autolink-a.out \
# RUN:   %t/autolink-main-a.o -L%t -objc_stubs_fast -U _objc_msgSend
# RUN: llvm-objdump --no-show-raw-insn --section=__TEXT,__objc_stubs \
# RUN:   --macho %t/autolink-a.out | FileCheck %s --check-prefix=AUTOLINK-A

# Check autolink extraction when the class is already known.
# RUN: llvm-mc -filetype=obj -triple=arm64-apple-darwin %t/autolink-main-b.s \
# RUN:   -o %t/autolink-main-b.o
# RUN: llvm-mc -filetype=obj -triple=arm64-apple-darwin %t/autolink-dep-b.s \
# RUN:   -o %t/autolink-dep-b.o
# RUN: llvm-ar rcs %t/libautolinkb.a %t/autolink-dep-b.o
# RUN: %lld -arch arm64 -lSystem -dead_strip -o %t/autolink-b.out \
# RUN:   %t/autolink-main-b.o -L%t -objc_stubs_fast -U _objc_msgSend
# RUN: llvm-objdump --no-show-raw-insn --section=__TEXT,__objc_stubs \
# RUN:   --macho %t/autolink-b.out | FileCheck %s --check-prefix=AUTOLINK-B
# RUN: llvm-nm %t/autolink-b.out | FileCheck %s --check-prefix=AUTOLINK-B-SYMS

# Check that a class referenced by a stub in an extracted archive member is
# extracted too.
# RUN: llvm-mc -filetype=obj -triple=arm64-apple-darwin %t/chain-main.s \
# RUN:   -o %t/chain-main.o
# RUN: llvm-mc -filetype=obj -triple=arm64-apple-darwin %t/chain-a.s \
# RUN:   -o %t/chain-a.o
# RUN: llvm-mc -filetype=obj -triple=arm64-apple-darwin %t/chain-b.s \
# RUN:   -o %t/chain-b.o
# RUN: llvm-ar rcs %t/libchain.a %t/chain-a.o %t/chain-b.o
# RUN: %lld -arch arm64 -lSystem -o %t/chain.out %t/chain-main.o \
# RUN:   %t/libchain.a -objc_stubs_fast -U _objc_msgSend
# RUN: llvm-nm %t/chain.out | FileCheck %s --check-prefix=CHAIN-SYMS

# Check that a class defined only in a bitcode archive member is compiled with
# the other bitcode instead of staying a placeholder.
# RUN: llvm-as %t/lto-class.ll -o %t/lto-class.o
# RUN: llvm-ar rcs %t/libltoclass.a %t/lto-class.o
# RUN: %lld -arch arm64 -lSystem -o %t/lto-archive.out %t/archive-main.o \
# RUN:   %t/libltoclass.a -objc_stubs_fast -U _objc_msgSend
# RUN: llvm-objdump --no-show-raw-insn --section=__TEXT,__objc_stubs \
# RUN:   --macho %t/lto-archive.out | FileCheck %s --check-prefix=ARCHIVE
# RUN: llvm-nm %t/lto-archive.out | FileCheck %s --check-prefix=ARCHIVE-SYMS

# Check that a class the first rounds found in a dylib, and that a bitcode
# member extracted for another class then defines, survives LTO.
# RUN: llvm-mc -filetype=obj -triple=arm64-apple-darwin %t/replaced-main.s \
# RUN:   -o %t/replaced-main.o
# RUN: llvm-mc -filetype=obj -triple=arm64-apple-darwin %t/replaced-dylib.s \
# RUN:   -o %t/replaced-dylib.o
# RUN: %lld -arch arm64 -dylib -install_name @executable_path/libreplaced.dylib \
# RUN:   -o %t/libreplaced.dylib %t/replaced-dylib.o
# RUN: llvm-as %t/lto-replaced.ll -o %t/lto-replaced.o
# RUN: llvm-ar rcs %t/libltoreplaced.a %t/lto-replaced.o
# RUN: %lld -arch arm64 -lSystem -o %t/replaced.out %t/replaced-main.o \
# RUN:   %t/libreplaced.dylib %t/libltoreplaced.a -objc_stubs_fast -U _objc_msgSend
# RUN: llvm-nm %t/replaced.out | FileCheck %s --check-prefix=REPLACED-SYMS
# REPLACED-SYMS-DAG: {{[DS]}} _OBJC_CLASS_$_OtherClass
# REPLACED-SYMS-DAG: {{[DS]}} _OBJC_CLASS_$_ReplacedClass

# Check that a member an autolinked archive supplies for one class can need,
# through a stub, a class from another member of it.
# RUN: llvm-mc -filetype=obj -triple=arm64-apple-darwin %t/autolink-main-c.s \
# RUN:   -o %t/autolink-main-c.o
# RUN: llvm-mc -filetype=obj -triple=arm64-apple-darwin %t/autolink-dep-c1.s \
# RUN:   -o %t/autolink-dep-c1.o
# RUN: llvm-mc -filetype=obj -triple=arm64-apple-darwin %t/autolink-dep-c2.s \
# RUN:   -o %t/autolink-dep-c2.o
# RUN: llvm-ar rcs %t/libautolinkc.a %t/autolink-dep-c1.o %t/autolink-dep-c2.o
# RUN: %lld -arch arm64 -lSystem -o %t/autolink-c.out \
# RUN:   %t/autolink-main-c.o -L%t -objc_stubs_fast -U _objc_msgSend
# RUN: llvm-nm %t/autolink-c.out | FileCheck %s --check-prefix=AUTOLINK-C-SYMS

# Check that a class a bitcode object defines survives LTO for the stub that
# needs it.
# RUN: %lld -arch arm64 -lSystem -o %t/lto-object.out %t/archive-main.o \
# RUN:   %t/lto-class.o -objc_stubs_fast -U _objc_msgSend
# RUN: llvm-objdump --no-show-raw-insn --section=__TEXT,__objc_stubs \
# RUN:   --macho %t/lto-object.out | FileCheck %s --check-prefix=ARCHIVE
# RUN: llvm-nm %t/lto-object.out | FileCheck %s --check-prefix=ARCHIVE-SYMS

# Check that a member an autolinked archive supplies can name another archive
# in its own linker options.
# RUN: llvm-mc -filetype=obj -triple=arm64-apple-darwin %t/autolink-main-d.s \
# RUN:   -o %t/autolink-main-d.o
# RUN: llvm-mc -filetype=obj -triple=arm64-apple-darwin %t/autolink-dep-d.s \
# RUN:   -o %t/autolink-dep-d.o
# RUN: llvm-mc -filetype=obj -triple=arm64-apple-darwin %t/autolink-dep-e.s \
# RUN:   -o %t/autolink-dep-e.o
# RUN: llvm-ar rcs %t/libautolinkd.a %t/autolink-dep-d.o
# RUN: llvm-ar rcs %t/libautolinke.a %t/autolink-dep-e.o
# RUN: %lld -arch arm64 -lSystem -o %t/autolink-d.out \
# RUN:   %t/autolink-main-d.o -L%t -objc_stubs_fast -U _objc_msgSend
# RUN: llvm-nm %t/autolink-d.out | FileCheck %s --check-prefix=AUTOLINK-D-SYMS

# Check that a weak class definition, which is bound at run time, is loaded
# through the GOT like a dylib class.
# RUN: llvm-mc -filetype=obj -triple=arm64-apple-darwin %t/weak.s -o %t/weak.o
# RUN: %lld -arch arm64 -lSystem -o %t/weak.out %t/weak.o \
# RUN:   -objc_stubs_fast -U _objc_msgSend
# RUN: llvm-objdump --no-show-raw-insn --section=__TEXT,__objc_stubs \
# RUN:   --macho %t/weak.out | FileCheck %s --check-prefix=WEAK

# Check that a stub an autolinked archive member introduces keeps the class a
# bitcode object defines through LTO.
# RUN: llvm-mc -filetype=obj -triple=arm64-apple-darwin %t/autolink-main-f.s \
# RUN:   -o %t/autolink-main-f.o
# RUN: llvm-mc -filetype=obj -triple=arm64-apple-darwin %t/autolink-dep-f.s \
# RUN:   -o %t/autolink-dep-f.o
# RUN: llvm-ar rcs %t/libautolinkf.a %t/autolink-dep-f.o
# RUN: %lld -arch arm64 -lSystem -o %t/autolink-f.out \
# RUN:   %t/autolink-main-f.o %t/lto-class.o -L%t -objc_stubs_fast -U _objc_msgSend
# RUN: llvm-nm %t/autolink-f.out | FileCheck %s --check-prefix=ARCHIVE-SYMS

# Check that a class stub's reference makes the dylib that defines the class a
# strong dependency, also when the dylib precedes the object and the object
# only references the dylib weakly otherwise.
# RUN: llvm-mc -filetype=obj -triple=arm64-apple-darwin %t/weak-dylib-ref.s \
# RUN:   -o %t/weak-dylib-ref.o
# RUN: %lld -arch arm64 -lSystem -o %t/weak-dylib-ref.out \
# RUN:   %t/libexternal.dylib %t/weak-dylib-ref.o -objc_stubs_fast -U _objc_msgSend
# RUN: llvm-otool -l %t/weak-dylib-ref.out | FileCheck %s --check-prefix=DYLIB-LOAD
# DYLIB-LOAD:     cmd LC_LOAD_DYLIB
# DYLIB-LOAD:     name @executable_path/libexternal.dylib

# Check that a bitcode input's own linker options name an archive whose
# bitcode member defines the class before LTO compiles them together.
# RUN: llvm-as %t/lto-autolink-main.ll -o %t/lto-autolink-main.o
# RUN: %lld -arch arm64 -lSystem -o %t/lto-autolink.out \
# RUN:   %t/lto-autolink-main.o -L%t -objc_stubs_fast -U _objc_msgSend
# RUN: llvm-objdump --no-show-raw-insn --section=__TEXT,__objc_stubs \
# RUN:   --macho %t/lto-autolink.out | FileCheck %s --check-prefix=ARCHIVE
# RUN: llvm-nm %t/lto-autolink.out | FileCheck %s --check-prefix=ARCHIVE-SYMS

# Check class stubs for dynamic class symbols.
# RUN: llvm-mc -filetype=obj -triple=arm64-apple-darwin %t/dynamic.s \
# RUN:   -o %t/dynamic.o
# RUN: %lld -arch arm64 -lSystem -o %t/dynamic.out %t/dynamic.o \
# RUN:   -objc_stubs_fast -U '_OBJC_CLASS_$_DynamicClass' -U _objc_msgSend
# RUN: llvm-objdump --no-show-raw-insn --section=__TEXT,__objc_stubs \
# RUN:   --macho %t/dynamic.out | FileCheck %s --check-prefix=DYNAMIC
# RUN: %lld -arch arm64 -lSystem -o %t/dynamic-lookup.out %t/dynamic.o \
# RUN:   -objc_stubs_fast -undefined dynamic_lookup
# RUN: llvm-objdump --no-show-raw-insn --section=__TEXT,__objc_stubs \
# RUN:   --macho %t/dynamic-lookup.out | FileCheck %s --check-prefix=DYNAMIC

# Check diagnostics for missing class symbols.
# RUN: llvm-mc -filetype=obj -triple=arm64-apple-darwin %t/missing.s \
# RUN:   -o %t/missing.o
# RUN: not %lld -arch arm64 -lSystem -o /dev/null %t/missing.o \
# RUN:   -objc_stubs_fast 2>&1 | FileCheck %s --check-prefix=MISSING

# Check diagnostics for malformed class-stub symbol names.
# RUN: llvm-mc -filetype=obj -triple=arm64-apple-darwin %t/malformed.s \
# RUN:   -o %t/malformed.o
# RUN: not %lld -arch arm64 -lSystem -o /dev/null %t/malformed.o \
# RUN:   -objc_stubs_fast 2>&1 | FileCheck %s --check-prefix=MALFORMED

# FAST:      Contents of (__TEXT,__objc_stubs) section
# FAST:      _objc_msgSend$instance:
# FAST-NEXT: adrp    x1,
# FAST-NEXT: ldr     x1, {{.*}} ; Objc selector ref: instance
# FAST-NEXT: adrp    x16,
# FAST-NEXT: ldr     x16,
# FAST-NEXT: br      x16
# FAST-NEXT: brk     #0x1
# FAST-NEXT: brk     #0x1
# FAST-NEXT: brk     #0x1
# FAST-NEXT: _objc_msgSendClass$external$_OBJC_CLASS_$_ExternalClass:
# FAST-NEXT: adrp    x0,
# FAST-NEXT: ldr     x0, {{.*}} ; literal pool symbol address: _OBJC_CLASS_$_ExternalClass
# FAST-NEXT: adrp    x1,
# FAST-NEXT: ldr     x1, {{.*}} ; Objc selector ref: external
# FAST-NEXT: adrp    x16,
# FAST-NEXT: ldr     x16,
# FAST-NEXT: br      x16
# FAST-NEXT: brk     #0x1
# FAST-NEXT: _objc_msgSendClass$local$_OBJC_CLASS_$_LocalClass:
# FAST-NEXT: adrp    x0, [[#]] ; 0x[[#%x,FAST_LOCAL_CLASS_PAGE:]]
# FAST-NEXT: add     x0, x0, #0x[[#%x,FAST_LOCAL_CLASS_OFF:]]
# FAST-NEXT: adrp    x1,
# FAST-NEXT: ldr     x1, {{.*}} ; Objc selector ref: local
# FAST-NEXT: adrp    x16,
# FAST-NEXT: ldr     x16,
# FAST-NEXT: br      x16
# FAST-NEXT: brk     #0x1
# FAST-LABEL: SYMBOL TABLE:
# FAST-DAG: {{0*}}[[#%x,FAST_LOCAL_CLASS_PAGE+FAST_LOCAL_CLASS_OFF]] g     O __DATA,__data _OBJC_CLASS_$_LocalClass

# SMALL:      Contents of (__TEXT,__objc_stubs) section
# SMALL:      _objc_msgSend$instance:
# SMALL-NEXT: adrp    x1,
# SMALL-NEXT: ldr     x1, {{.*}} ; Objc selector ref: instance
# SMALL-NEXT: b       _objc_msgSend
# SMALL-NEXT: _objc_msgSendClass$external$_OBJC_CLASS_$_ExternalClass:
# SMALL-NEXT: adrp    x0,
# SMALL-NEXT: ldr     x0, {{.*}} ; literal pool symbol address: _OBJC_CLASS_$_ExternalClass
# SMALL-NEXT: adrp    x1,
# SMALL-NEXT: ldr     x1, {{.*}} ; Objc selector ref: external
# SMALL-NEXT: b       _objc_msgSend
# SMALL-NEXT: _objc_msgSendClass$local$_OBJC_CLASS_$_LocalClass:
# SMALL-NEXT: adrp    x0, [[#]] ; 0x[[#%x,SMALL_LOCAL_CLASS_PAGE:]]
# SMALL-NEXT: add     x0, x0, #0x[[#%x,SMALL_LOCAL_CLASS_OFF:]]
# SMALL-NEXT: adrp    x1,
# SMALL-NEXT: ldr     x1, {{.*}} ; Objc selector ref: local
# SMALL-NEXT: b       _objc_msgSend
# SMALL-LABEL: SYMBOL TABLE:
# SMALL-DAG: {{0*}}[[#%x,SMALL_LOCAL_CLASS_PAGE+SMALL_LOCAL_CLASS_OFF]] g     O __DATA,__data _OBJC_CLASS_$_LocalClass

# DYNAMIC:      Contents of (__TEXT,__objc_stubs) section
# DYNAMIC-NEXT: _objc_msgSendClass$dynamic$_OBJC_CLASS_$_DynamicClass:
# DYNAMIC-NEXT: adrp    x0,
# DYNAMIC-NEXT: ldr     x0, {{.*}} ; literal pool symbol address: _OBJC_CLASS_$_DynamicClass
# DYNAMIC-NEXT: adrp    x1,
# DYNAMIC-NEXT: ldr     x1, {{.*}} ; Objc selector ref: dynamic
# DYNAMIC-NEXT: adrp    x16,
# DYNAMIC-NEXT: ldr     x16, {{.*}} ; literal pool symbol address: _objc_msgSend
# DYNAMIC-NEXT: br      x16
# DYNAMIC-NEXT: brk     #0x1

# ARCHIVE:      Contents of (__TEXT,__objc_stubs) section
# ARCHIVE-NEXT: _objc_msgSendClass$archive$_OBJC_CLASS_$_ArchiveClass:
# ARCHIVE-NEXT: adrp    x0,
# ARCHIVE-NEXT: add     x0, x0,
# ARCHIVE-NEXT: adrp    x1,
# ARCHIVE-NEXT: ldr     x1, {{.*}} ; Objc selector ref: archive
# ARCHIVE-NEXT: adrp    x16,
# ARCHIVE-NEXT: ldr     x16, {{.*}} ; literal pool symbol address: _objc_msgSend
# ARCHIVE-NEXT: br      x16
# ARCHIVE-NEXT: brk     #0x1
#
# ARCHIVE-SYMS: _OBJC_CLASS_$_ArchiveClass

# CHAIN-SYMS-DAG: D _OBJC_CLASS_$_ChainA
# CHAIN-SYMS-DAG: D _OBJC_CLASS_$_ChainB

# AUTOLINK-A:      Contents of (__TEXT,__objc_stubs) section
# AUTOLINK-A-NEXT: _objc_msgSendClass$auto$_OBJC_CLASS_$_AutoClass:
# AUTOLINK-A-NEXT: adrp    x0,
# AUTOLINK-A-NEXT: add     x0, x0,
# AUTOLINK-A-NEXT: adrp    x1,
# AUTOLINK-A-NEXT: ldr     x1, {{.*}} ; Objc selector ref: auto
# AUTOLINK-A-NEXT: adrp    x16,
# AUTOLINK-A-NEXT: ldr     x16, {{.*}} ; literal pool symbol address: _objc_msgSend
# AUTOLINK-A-NEXT: br      x16
# AUTOLINK-A-NEXT: brk     #0x1
#
# AUTOLINK-B:      Contents of (__TEXT,__objc_stubs) section
# AUTOLINK-B-NEXT: _objc_msgSendClass$late$_OBJC_CLASS_$_LateClass:
# AUTOLINK-B-NEXT: adrp    x0,
# AUTOLINK-B-NEXT: add     x0, x0,
# AUTOLINK-B-NEXT: adrp    x1,
# AUTOLINK-B-NEXT: ldr     x1, {{.*}} ; Objc selector ref: late
# AUTOLINK-B-NEXT: adrp    x16,
# AUTOLINK-B-NEXT: ldr     x16, {{.*}} ; literal pool symbol address: _objc_msgSend
# AUTOLINK-B-NEXT: br      x16
# AUTOLINK-B-NEXT: brk     #0x1
#
# AUTOLINK-B-SYMS: _OBJC_CLASS_$_LateClass

# AUTOLINK-C-SYMS-DAG: D _OBJC_CLASS_$_AutoChainA
# AUTOLINK-C-SYMS-DAG: D _OBJC_CLASS_$_AutoChainB

# AUTOLINK-D-SYMS-DAG: D _OBJC_CLASS_$_NestedClass
# AUTOLINK-D-SYMS-DAG: T _nested_helper

# WEAK:      _objc_msgSendClass$weak$_OBJC_CLASS_$_WeakClass:
# WEAK-NEXT: adrp    x0,
# WEAK-NEXT: ldr     x0, {{.*}} ; literal pool symbol address: _OBJC_CLASS_$_WeakClass

# MISSING: error: undefined symbol: _OBJC_CLASS_$_MissingClass
# MISSING-NEXT: >>> referenced by objc class stub

# MALFORMED: error: malformed objc class stub symbol _objc_msgSendClass$malformed; expected _objc_msgSendClass$<selector>$_OBJC_CLASS_$_<class>
# MALFORMED-NOT: Objc selector ref:
# MALFORMED-NOT: undefined symbol: _objc_msgSendClass$malformed

#--- main.s
# Main fast/small case: one regular selector stub plus local and dylib class
# stubs.
.section __TEXT,__objc_methname,cstring_literals
Linstance:
  .asciz "instance"
Llocal:
  .asciz "local"
Lexternal:
  .asciz "external"

.section __DATA,__objc_selrefs,literal_pointers,no_dead_strip
.p2align 3
  .quad Linstance
  .quad Llocal
  .quad Lexternal

.text
.globl _objc_msgSend
_objc_msgSend:
  ret

.globl _main
_main:
  bl _objc_msgSend$instance
  bl _objc_msgSendClass$local$_OBJC_CLASS_$_LocalClass
  bl _objc_msgSendClass$external$_OBJC_CLASS_$_ExternalClass
  ret

.data
.globl _OBJC_CLASS_$_LocalClass
_OBJC_CLASS_$_LocalClass:
  .quad 0

.subsections_via_symbols

#--- external.s
# Provides the dylib-defined class used by main.s.
.data
.globl _OBJC_CLASS_$_ExternalClass
_OBJC_CLASS_$_ExternalClass:
  .quad 0
.globl _external_weak
_external_weak:
  .quad 0

#--- weak-dylib-ref.s
.text
.globl _main
_main:
  bl _objc_msgSendClass$external$_OBJC_CLASS_$_ExternalClass
  ret

.weak_reference _external_weak
.data
.quad _external_weak

#--- dynamic.s
# Uses a class stub whose class symbol is resolved dynamically.
.section __TEXT,__objc_methname,cstring_literals
Ldynamic:
  .asciz "dynamic"

.section __DATA,__objc_selrefs,literal_pointers,no_dead_strip
.p2align 3
  .quad Ldynamic

.text
.globl _main
_main:
  bl _objc_msgSendClass$dynamic$_OBJC_CLASS_$_DynamicClass
  ret

#--- archive-main.s
# References a class stub whose class definition must be pulled from an archive.
.text
.globl _main
_main:
  bl _objc_msgSendClass$archive$_OBJC_CLASS_$_ArchiveClass
  ret

#--- archive-class.s
# Archive member defining the class used by archive-main.s.
.data
.globl _OBJC_CLASS_$_ArchiveClass
_OBJC_CLASS_$_ArchiveClass:
  .quad 0

#--- archive-alias.s
# Archive member defining the class used by archive-main.s as an alias.
.data
.globl _OBJC_CLASS_$_ArchiveClass
.globl _OBJC_CLASS_$_AliasedClass
_OBJC_CLASS_$_AliasedClass:
  .quad 0
_OBJC_CLASS_$_ArchiveClass = _OBJC_CLASS_$_AliasedClass

#--- chain-main.s
.text
.globl _main
_main:
  bl _objc_msgSendClass$chain$_OBJC_CLASS_$_ChainA
  ret

#--- chain-a.s
# Defines ChainA and, through a stub, needs ChainB from the same archive.
.text
.globl _chain_a
_chain_a:
  bl _objc_msgSendClass$chain$_OBJC_CLASS_$_ChainB
  ret

.data
.globl _OBJC_CLASS_$_ChainA
_OBJC_CLASS_$_ChainA:
  .quad _chain_a

#--- chain-b.s
.data
.globl _OBJC_CLASS_$_ChainB
_OBJC_CLASS_$_ChainB:
  .quad 0

#--- replaced-main.s
.text
.globl _main
_main:
  bl _objc_msgSendClass$a$_OBJC_CLASS_$_ReplacedClass
  bl _objc_msgSendClass$b$_OBJC_CLASS_$_OtherClass
  ret

#--- replaced-dylib.s
.data
.globl _OBJC_CLASS_$_ReplacedClass
_OBJC_CLASS_$_ReplacedClass:
  .quad 0

#--- lto-replaced.ll
target triple = "arm64-apple-darwin"
target datalayout = "e-m:o-i64:64-i128:128-n32:64-S128-Fn32"

@"OBJC_CLASS_$_OtherClass" = global i64 0
@"OBJC_CLASS_$_ReplacedClass" = global i64 0

#--- lto-class.ll
target triple = "arm64-apple-darwin"
target datalayout = "e-m:o-i64:64-i128:128-n32:64-S128-Fn32"

@"OBJC_CLASS_$_ArchiveClass" = global i64 0

#--- lto-autolink-main.ll
target triple = "arm64-apple-darwin"
target datalayout = "e-m:o-i64:64-i128:128-n32:64-S128-Fn32"

declare void @"objc_msgSendClass$archive$_OBJC_CLASS_$_ArchiveClass"()

define void @main() {
  call void @"objc_msgSendClass$archive$_OBJC_CLASS_$_ArchiveClass"()
  ret void
}

!llvm.linker.options = !{!0}
!0 = !{!"-lltoclass"}

#--- autolink-main-a.s
# Autolinks an archive member that contains both the stub user and class.
.linker_option "-lautolinka"
.text
.globl _main
_main:
  bl _dep_a
  ret

#--- autolink-dep-a.s
# Autolinked member containing the class-stub reference and class definition.
.text
.globl _dep_a
_dep_a:
  bl _objc_msgSendClass$auto$_OBJC_CLASS_$_AutoClass
  ret

.data
.globl _OBJC_CLASS_$_AutoClass
_OBJC_CLASS_$_AutoClass:
  .quad 0

#--- autolink-main-b.s
# Autolinks an archive member that references a class defined in this object.
.linker_option "-lautolinkb"
.text
.globl _main
_main:
  bl _dep_b
  ret

.data
.globl _OBJC_CLASS_$_LateClass
_OBJC_CLASS_$_LateClass:
  .quad 0

#--- autolink-dep-b.s
# Autolinked member that introduces a late class-stub reference.
.text
.globl _dep_b
_dep_b:
  bl _objc_msgSendClass$late$_OBJC_CLASS_$_LateClass
  ret

#--- autolink-main-c.s
.linker_option "-lautolinkc"
.text
.globl _main
_main:
  bl _objc_msgSendClass$chain$_OBJC_CLASS_$_AutoChainA
  ret

#--- autolink-dep-c1.s
# Defines AutoChainA and, through a stub, needs AutoChainB from the archive.
.text
.globl _auto_chain_a
_auto_chain_a:
  bl _objc_msgSendClass$chain$_OBJC_CLASS_$_AutoChainB
  ret

.data
.globl _OBJC_CLASS_$_AutoChainA
_OBJC_CLASS_$_AutoChainA:
  .quad _auto_chain_a

#--- autolink-dep-c2.s
.data
.globl _OBJC_CLASS_$_AutoChainB
_OBJC_CLASS_$_AutoChainB:
  .quad 0

#--- autolink-main-d.s
.linker_option "-lautolinkd"
.text
.globl _main
_main:
  bl _objc_msgSendClass$nested$_OBJC_CLASS_$_NestedClass
  ret

#--- autolink-dep-d.s
# Defines NestedClass and needs a helper from another autolinked archive.
.linker_option "-lautolinke"
.text
.globl _nested_init
_nested_init:
  bl _nested_helper
  ret

.data
.globl _OBJC_CLASS_$_NestedClass
_OBJC_CLASS_$_NestedClass:
  .quad _nested_init

#--- autolink-dep-e.s
.text
.globl _nested_helper
_nested_helper:
  ret

#--- weak.s
.text
.globl _main
_main:
  bl _objc_msgSendClass$weak$_OBJC_CLASS_$_WeakClass
  ret

.data
.globl _OBJC_CLASS_$_WeakClass
.weak_definition _OBJC_CLASS_$_WeakClass
_OBJC_CLASS_$_WeakClass:
  .quad 0

#--- autolink-main-f.s
.linker_option "-lautolinkf"
.text
.globl _main
_main:
  bl _dep_f
  ret

#--- autolink-dep-f.s
# Autolinked member whose stub needs the class the bitcode object defines.
.text
.globl _dep_f
_dep_f:
  bl _objc_msgSendClass$archive$_OBJC_CLASS_$_ArchiveClass
  ret

#--- missing.s
# References a live class stub whose class symbol is unresolved.
.section __TEXT,__objc_methname,cstring_literals
Lmissing:
  .asciz "missing"

.section __DATA,__objc_selrefs,literal_pointers,no_dead_strip
.p2align 3
  .quad Lmissing

.text
.globl _objc_msgSend
_objc_msgSend:
  ret

.globl _main
_main:
  bl _objc_msgSendClass$missing$_OBJC_CLASS_$_MissingClass
  ret

#--- malformed.s
# References a class stub symbol without the required class-symbol suffix.
.text
.globl _main
_main:
  bl _objc_msgSendClass$malformed
  ret
