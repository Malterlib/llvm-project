; RUN: llc -mtriple=x86_64-linux-gnu < %s | FileCheck %s --check-prefixes=CHECK,ELF
; RUN: llc -mtriple=x86_64-windows-msvc < %s | FileCheck %s --check-prefixes=CHECK,COFF
; RUN: llc -mtriple=x86_64-apple-macosx < %s | FileCheck %s --check-prefixes=CHECK,MACHO

; The definitions a module only had the body of to inline that its
; construction sites depend on are referenced, with their markers, only
; while the module still uses them: a site that inlined one everywhere
; neither calls nor depends on another image's copy of it.

declare void @called()
declare void @inlined()
declare void @unused()
declare void @inlinedbody()
declare void @byname()
; A local definition the optimizer replaced with its marker is its own marker.
define internal void @local.mib_sized() {
  ret void
}

; Not a local definition without its marker, which can be one link-time
; optimization brought in from a module compiled without the flag and made
; local: its marker is left undefined, which the linker reports.
define internal void @unmarked() {
  ret void
}

; Also one link-time optimization renamed apart from another module's of the
; same name, which gets a marker named as markers are, at it.
define internal void @renamed.mib_sized.7() {
  ret void
}

; Nor is what a vftable of the module holds that only the anchor keeps, which
; no code installs, unlike what one installed holds.
declare void @destroyer()
declare void @installed()
@unusedvftable = linkonce_odr constant [1 x ptr] [ptr @destroyer]
@usedvftable = linkonce_odr constant [1 x ptr] [ptr @installed]

; What keeps a definition a dependency record names, which isn't emitted,
; isn't a use.
@__mib_sized_kept = internal constant [3 x ptr] [ptr @inlined, ptr @unusedvftable, ptr @usedvftable], section "llvm.metadata"
@llvm.compiler.used = appending global [1 x ptr] [ptr @__mib_sized_kept], section "llvm.metadata"

define void @site(ptr %object) {
  store ptr @usedvftable, ptr %object
  call void @called()
  call void @byname()
  call void @local.mib_sized()
  call void @unmarked()
  call void @renamed.mib_sized.7()
  ret void
}

; Nor are the records of an owner's dependencies on them, like a constructor's
; on the dllimport constructor it delegates to, which it inlined.
define void @owner() #0 !malterlib.sized.deps !3 {
  call void @called()
  ret void
}

; Nor of a function the module only declares, which the frontend doesn't keep:
; one nothing emitted calls, and one a pass removed, which leaves no symbol.
define void @owner2() #0 !malterlib.sized.deps !4 {
  call void @called()
  ret void
}

; Nor of one only something else of the module calls, an ordinary function
; calling the import an exported constructor inlined.
declare void @elsewhere()
define void @owner3() #0 !malterlib.sized.deps !12 {
  ret void
}
define void @ordinary() {
  call void @elsewhere()
  ret void
}

; Nor a call in the body of a definition of another image the module only has
; to inline, which isn't emitted, also where nothing inlined it.
define available_externally void @body() {
  call void @inlinedbody()
  ret void
}

attributes #0 = { "malterlib-sized" }

!malterlib.sized.inlinable = !{!0, !1, !2, !5, !6, !7, !8, !9, !10, !11}
!0 = !{ptr @called}
!1 = !{ptr @inlined}
!2 = !{null}
!3 = !{i64 0, ptr @called, null, ptr @inlined, null}
!4 = !{i64 0, ptr @unused, !"malterlib.sized.may-go", null, !"malterlib.sized.may-go", ptr @called, null}
!5 = !{ptr @inlinedbody}
; Also by its name, of a declaration that the optimizer removed before code
; imported with link-time optimization used it.
!6 = !{null, !"byname"}
!7 = !{ptr @local.mib_sized}
!8 = !{ptr @unmarked}
!9 = !{ptr @renamed.mib_sized.7}
!10 = !{ptr @destroyer}
!11 = !{ptr @installed}
!12 = !{i64 0, ptr @elsewhere, !"malterlib.sized.may-go", null, !"malterlib.sized.may-go"}

; ELF: .section .data.rel.ro.__mib_sized_dep,"aw",@progbits
; COFF: .section .rdata$mibszd,"dr"
; MACHO: .section __DATA_CONST,__mib_sized_dep
; CHECK-NEXT: .p2align 3
; CHECK-NEXT: .quad {{\.?}}Lmib_sized_owner0
; CHECK-NEXT: .quad {{_?}}called
; CHECK-NEXT: .quad {{_?}}called.mib_sized
; CHECK-NEXT: .p2align 3
; CHECK-NEXT: .quad {{\.?}}Lmib_sized_owner1
; CHECK-NEXT: .quad {{_?}}called
; CHECK-NEXT: .quad {{_?}}called.mib_sized
; CHECK-NOT: {{inlined|unused|lost|elsewhere}}
; ELF: .section .data.rel.ro.__mib_sized_ref,"aw",@progbits
; COFF: .section .rdata$mibszf,"dr"
; MACHO: .section __DATA_CONST,__mib_sized_ref
; CHECK-NEXT: .p2align 3
; CHECK-NEXT: .quad {{_?}}called
; CHECK-NEXT: .quad {{_?}}called.mib_sized
; CHECK-NEXT: .quad {{_?}}byname
; CHECK-NEXT: .quad {{_?}}byname.mib_sized
; CHECK-NEXT: .quad {{_?}}local.mib_sized
; CHECK-NEXT: .quad {{_?}}local.mib_sized{{$}}
; CHECK-NEXT: .quad {{_?}}unmarked{{$}}
; CHECK-NEXT: .quad {{_?}}unmarked.mib_sized{{$}}
; CHECK-NEXT: .quad {{_?}}renamed.mib_sized.7{{$}}
; CHECK-NEXT: {{_?}}renamed.mib_sized.7.mib_sized = {{_?}}renamed.mib_sized.7{{$}}
; CHECK-NEXT: .quad {{_?}}renamed.mib_sized.7.mib_sized{{$}}
; CHECK-NEXT: .quad {{_?}}installed{{$}}
; CHECK-NEXT: .quad {{_?}}installed.mib_sized{{$}}
; CHECK-NOT: {{inlined|unmarked.mib_sized =|destroyer}}
