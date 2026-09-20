; RUN: llc -mtriple=x86_64-linux-gnu < %s | FileCheck %s --check-prefix=ELF
; RUN: llc -mtriple=x86_64-windows-msvc < %s | FileCheck %s --check-prefix=COFF
; RUN: sed -e 's/ comdat !/ !/' -e '/= comdat any/d' %s | llc -mtriple=x86_64-apple-macosx | FileCheck %s --check-prefix=MACHO

; A definition that holds owners of -fmalterlib-sized-destructors gets a local
; label, which the dependency records of the owners name, so that a record
; names the copy of the definition it came with. The records are in the
; definition's section group or COMDAT, which the linker discards with the
; copy. An owner the optimizer removed leaves no record.

$_ZN1RC2Ev = comdat any

@_ZTV1R = external constant ptr
@_ZTV1S = external constant ptr

define linkonce_odr void @_ZN1RC2Ev() #0 comdat !malterlib.sized.deps !0 {
  ret void
}

define void @_ZN1SC2Ev() #0 !malterlib.sized.deps !1 {
  ret void
}

; A local definition's marker is carried in the record, and one the optimizer
; replaced with its marker is its own.
@_ZTV1L = internal constant ptr null
@_ZTV1L.mib_sized = internal alias ptr, ptr @_ZTV1L
@_ZTV1M = internal constant ptr null
@_ZTV1M.mib_sized = internal alias ptr, ptr @_ZTV1M
@llvm.compiler.used = appending global [2 x ptr] [ptr @_ZTV1L.mib_sized, ptr @_ZTV1M.mib_sized], section "llvm.metadata"

define void @_ZN1LC2Ev() #0 !malterlib.sized.deps !2 {
  ret void
}

define void @_ZN1MC2Ev() #0 !malterlib.sized.deps !3 {
  ret void
}

; The marker of a dependency whose symbol a calling convention decorates is
; named by that symbol.
declare x86_vectorcallcc void @_ZN1VD0Ev(ptr)

define void @_ZN1VC2Ev() #0 !malterlib.sized.deps !4 {
  ret void
}

attributes #0 = { "malterlib-sized" }

!0 = !{i64 0, ptr @_ZTV1R, null}
!1 = !{i64 0, ptr @_ZTV1S, null}
!2 = !{i64 0, ptr @_ZTV1L, ptr @_ZTV1L.mib_sized}
!3 = !{i64 0, ptr @_ZTV1M.mib_sized, null}
!4 = !{i64 0, ptr @_ZN1VD0Ev, null}

; ELF-LABEL: _ZN1RC2Ev:
; ELF:       [[R:\.Lmib_sized_owner[0-9]*]]:
; ELF-LABEL: _ZN1SC2Ev:
; ELF:       [[S:\.Lmib_sized_owner[0-9]*]]:
; ELF:       .section .data.rel.ro.__mib_sized_dep,"awG",@progbits,_ZN1RC2Ev,comdat
; ELF-NEXT:  .p2align 3
; ELF-NEXT:  .quad [[R]]
; ELF-NEXT:  .quad _ZTV1R
; ELF-NEXT:  .quad _ZTV1R.mib_sized
; ELF:       .section .data.rel.ro.__mib_sized_dep,"aw",@progbits
; ELF-NEXT:  .p2align 3
; ELF-NEXT:  .quad [[S]]
; ELF-NEXT:  .quad _ZTV1S
; ELF-NEXT:  .quad _ZTV1S.mib_sized
; ELF-NEXT:  .p2align 3
; ELF-NEXT:  .quad {{\.Lmib_sized_owner[0-9]*}}
; ELF-NEXT:  .quad _ZTV1L
; ELF-NEXT:  .quad _ZTV1L.mib_sized
; ELF-NEXT:  .p2align 3
; ELF-NEXT:  .quad {{\.Lmib_sized_owner[0-9]*}}
; ELF-NEXT:  .quad _ZTV1M.mib_sized
; ELF-NEXT:  .quad _ZTV1M.mib_sized

; COFF-LABEL: _ZN1RC2Ev:
; COFF:       [[R:\.Lmib_sized_owner[0-9]*]]:
; COFF:       .section .rdata$mibszd,"dr",associative,_ZN1RC2Ev
; COFF-NEXT:  .p2align 3
; COFF-NEXT:  .quad [[R]]
; COFF-NEXT:  .quad _ZTV1R
; COFF-NEXT:  .quad _ZTV1R.mib_sized

; MACHO-LABEL: __ZN1RC2Ev:
; MACHO:       [[R:[Ll]mib_sized_owner[0-9]*]]:
; MACHO:       .section __DATA_CONST,__mib_sized_dep
; MACHO-NEXT:  .p2align 3
; MACHO-NEXT:  .quad [[R]]
; MACHO-NEXT:  .quad __ZTV1R
; MACHO-NEXT:  .quad __ZTV1R.mib_sized
; MACHO:       .quad "_ZN1VD0Ev@@8"
; MACHO-NEXT:  .quad "_ZN1VD0Ev@@8.mib_sized"
