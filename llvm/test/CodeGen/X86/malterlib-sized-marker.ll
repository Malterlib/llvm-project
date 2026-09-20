; RUN: llc -mtriple=x86_64-linux-gnu < %s | FileCheck %s --check-prefix=ELF
; RUN: sed 's/, comdat//; s/ comdat {/ {/' %s | llc -mtriple=x86_64-apple-macosx | FileCheck %s --check-prefix=MACHO
; RUN: llc -mtriple=x86_64-windows-msvc < %s | FileCheck %s --check-prefix=COFF
; RUN: llc -mtriple=x86_64-windows-msvc < %s | FileCheck %s --check-prefix=COFF-WEAK
; RUN: sed 's/, comdat//; s/ comdat {/ {/' %s | llc -mtriple=x86_64-apple-macosx | FileCheck %s --check-prefix=DECORATED

; A definition with the "malterlib-sized" attribute gets a marker symbol at its
; address, with its linkage and visibility, in its own section.

$_ZN1BC1Ev = comdat any
$_ZTV1B = comdat any

define void @_ZN1AC1Ev() #0 {
  ret void
}

define linkonce_odr hidden void @_ZN1BC1Ev() #0 comdat {
  ret void
}

define internal void @_ZN1CC1Ev() #0 {
  ret void
}

@_ZTV1A = constant [1 x ptr] [ptr @_ZN1AC1Ev] #1
@_ZTV1B = linkonce_odr constant [1 x ptr] [ptr @_ZN1BC1Ev], comdat #1

define void @_ZN1PC1Ev() #0 prefix i32 42 {
  ret void
}

define void @user() {
  call void @_ZN1CC1Ev()
  ret void
}

attributes #0 = { "malterlib-sized" }
attributes #1 = { "malterlib-sized" }

; ELF:      .globl _ZN1AC1Ev
; ELF:      .globl _ZN1AC1Ev.mib_sized
; ELF-NEXT: .type _ZN1AC1Ev.mib_sized,@function
; ELF-NEXT: _ZN1AC1Ev.mib_sized:
;
; ELF:      .section .text._ZN1BC1Ev,"axG",@progbits,_ZN1BC1Ev,comdat
; ELF:      .hidden _ZN1BC1Ev
; ELF:      .weak _ZN1BC1Ev
; ELF:      .weak _ZN1BC1Ev.mib_sized
; ELF-NEXT: .hidden _ZN1BC1Ev.mib_sized
; ELF-NEXT: .type _ZN1BC1Ev.mib_sized,@function
; ELF-NEXT: _ZN1BC1Ev.mib_sized:
;
; ELF-LABEL: _ZN1CC1Ev:
; ELF-NOT:  .globl _ZN1CC1Ev.mib_sized
; ELF:      .type _ZN1CC1Ev.mib_sized,@function
; ELF-NEXT: _ZN1CC1Ev.mib_sized:
;

; MACHO:      .globl __ZN1AC1Ev.mib_sized
; MACHO-NEXT: __ZN1AC1Ev.mib_sized:
;
; MACHO:      .private_extern __ZN1BC1Ev
; MACHO:      .globl __ZN1BC1Ev
; MACHO:      .weak_definition __ZN1BC1Ev
; MACHO:      .globl __ZN1BC1Ev.mib_sized
; MACHO-NEXT: .weak_definition __ZN1BC1Ev.mib_sized
; MACHO-NEXT: .private_extern __ZN1BC1Ev.mib_sized
; MACHO-NEXT: __ZN1BC1Ev.mib_sized:
;
; MACHO-LABEL: __ZN1CC1Ev:
; MACHO-NEXT: __ZN1CC1Ev.mib_sized:
;
; The marker of a function with prefix data is, like the function, an
; alternative entry of the atom the data starts.
; MACHO:      .alt_entry __ZN1PC1Ev
; MACHO:      .globl __ZN1PC1Ev.mib_sized
; MACHO-NEXT: .alt_entry __ZN1PC1Ev.mib_sized
; MACHO-NEXT: __ZN1PC1Ev.mib_sized:
;
; MACHO:      .globl __ZTV1A.mib_sized
; MACHO-NEXT: __ZTV1A.mib_sized:
; On COFF a marker is an external symbol of its definition's COMDAT section:
; the linker drops it with a copy of the definition it does not keep.
;
; COFF-LABEL: _ZN1AC1Ev:
; COFF-NEXT: .globl _ZN1AC1Ev.mib_sized
; COFF-NEXT: .def _ZN1AC1Ev.mib_sized;
; COFF-NEXT: .scl 2;
; COFF-NEXT: .type 32;
; COFF-NEXT: .endef
; COFF-NEXT: _ZN1AC1Ev.mib_sized:
;
; COFF:      .section .text,"xr",discard,_ZN1BC1Ev
; COFF-LABEL: _ZN1BC1Ev:
; COFF-NEXT: .globl _ZN1BC1Ev.mib_sized
; COFF-NEXT: .def _ZN1BC1Ev.mib_sized;
; COFF-NEXT: .scl 2;
; COFF:      _ZN1BC1Ev.mib_sized:
;
; COFF-LABEL: _ZN1CC1Ev:
; COFF-NEXT: .def _ZN1CC1Ev.mib_sized;
; COFF-NEXT: .scl 3;
;
; COFF-LABEL: _ZTV1A:
; COFF-NEXT: .globl _ZTV1A.mib_sized
; COFF-NEXT: .def _ZTV1A.mib_sized;
; COFF-NEXT: .scl 2;
; COFF-NEXT: .type 0;
; COFF-NEXT: .endef
; COFF-NEXT: _ZTV1A.mib_sized:

; An alias listed on its definition, with its offset into it, gets a marker of
; its own at that address, which may be inside the definition, whether or not
; the alias survived optimization; one that names another definition does
; not. A marker whose alias is gone, or local, is local.

@_ZN1DC1Ev = unnamed_addr alias void (), ptr @_ZN1DC2Ev
@_ZN1EC1Ev = unnamed_addr alias void (), ptr @_ZN1EC2Ev

define void @_ZN1DC2Ev() #2 {
  ret void
}

define void @_ZN1EC2Ev() #3 {
  ret void
}

define void @other() #3 {
  ret void
}

@0 = private constant { [2 x ptr] } { [2 x ptr] [ptr null, ptr @_ZN1AC1Ev] } #4

; The marker of an exported definition is exported with it.
define dllexport void @_ZN1XC1Ev() #6 {
  ret void
}
@"??_7G@@6B@" = alias ptr, ptr getelementptr inbounds ({ [2 x ptr] }, ptr @0, i32 0, i32 0, i32 1)

attributes #2 = { "malterlib-sized-aliases"="_ZN1DC1Ev=0" }
attributes #3 = { "malterlib-sized-aliases"="_ZN1EC1Ev=0,_ZN1DC1Ev=0,_ZN1FC1Ev=0" }
attributes #4 = { "malterlib-sized-aliases"="??_7G@@6B@=8" }
attributes #6 = { "malterlib-sized" }

; The functions come first in the output, then the variables, then the aliases.
;
; ELF-LABEL: _ZN1DC2Ev:
; ELF-NEXT:  .globl _ZN1DC1Ev.mib_sized
; ELF-NEXT:  .type _ZN1DC1Ev.mib_sized,@function
; ELF-NEXT:  _ZN1DC1Ev.mib_sized = _ZN1DC2Ev
;
; ELF-LABEL: _ZN1EC2Ev:
; ELF-NEXT:  .globl _ZN1EC1Ev.mib_sized
; ELF-NEXT:  .type _ZN1EC1Ev.mib_sized,@function
; ELF-NEXT:  _ZN1EC1Ev.mib_sized = _ZN1EC2Ev
; ELF-NEXT:  .type _ZN1FC1Ev.mib_sized,@function
; ELF-NEXT:  _ZN1FC1Ev.mib_sized = _ZN1EC2Ev
; ELF-NOT:   _ZN1DC1Ev.mib_sized
;
; ELF-LABEL: other:
; ELF-NOT:   _ZN1DC1Ev.mib_sized
; ELF-NOT:   _ZN1EC1Ev.mib_sized
;
; ELF:      .globl _ZTV1A
; ELF:      _ZTV1A:
; ELF:      .globl _ZTV1A.mib_sized
; ELF-NEXT: .type _ZTV1A.mib_sized,@object
; ELF-NEXT: _ZTV1A.mib_sized:
;
; ELF:      .section .rodata._ZTV1B,"aG",@progbits,_ZTV1B,comdat
; ELF:      .weak _ZTV1B.mib_sized
; ELF-NEXT: .type _ZTV1B.mib_sized,@object
; ELF-NEXT: _ZTV1B.mib_sized:
;
; On COFF, the marker of a weak alias is a weak alias too, which the marker of
; a copy that overrides the alias overrides, and the marker of a weak
; definition outside a COMDAT is weak as the definition is.

$_ZN1WC2Ev = comdat any
@_ZN1WC1Ev = weak_odr unnamed_addr alias void (), ptr @_ZN1WC2Ev

define weak_odr void @_ZN1WC2Ev() #7 comdat {
  ret void
}

define weak void @_ZN1VC1Ev() #0 {
  ret void
}

attributes #7 = { "malterlib-sized" "malterlib-sized-aliases"="_ZN1WC1Ev=0" }

; COFF-WEAK-LABEL: _ZN1WC2Ev:
; COFF-WEAK-NEXT: .globl _ZN1WC2Ev.mib_sized
; COFF-WEAK:      .weak _ZN1WC1Ev.mib_sized
; COFF-WEAK:      _ZN1WC1Ev.mib_sized = _ZN1WC2Ev
;
; COFF-WEAK-LABEL: _ZN1VC1Ev:
; COFF-WEAK-NEXT: .weak _ZN1VC1Ev.mib_sized
;
; COFF:      .ascii " /EXPORT:_ZN1XC1Ev /EXPORT:\"_ZN1XC1Ev.mib_sized\""
;
; ELF:      .globl "??_7G@@6B@.mib_sized"
; ELF-NEXT: .type "??_7G@@6B@.mib_sized",@object
; ELF-NEXT: "??_7G@@6B@.mib_sized" = .L__unnamed_1+8
;
; ELF:      _ZN1DC1Ev = _ZN1DC2Ev

; The marker of a definition whose symbol a calling convention decorates is
; the decorated symbol with the suffix, and so is that of an alias of it.

define x86_vectorcallcc void @_ZN1QD2Ev(ptr %this) #8 {
  ret void
}

attributes #8 = { "malterlib-sized" "malterlib-sized-aliases"="_ZN1QD1Ev=0" }

; DECORATED-LABEL: {{"?}}_ZN1QD2Ev@@8{{"?}}:
; DECORATED:       {{"?}}_ZN1QD2Ev@@8.mib_sized{{"?}}:
; DECORATED:       {{"?}}_ZN1QD1Ev@@8.mib_sized{{"?}} = {{"?}}_ZN1QD2Ev@@8{{"?}}
