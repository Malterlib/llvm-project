; RUN: llc -mtriple=x86_64-windows-msvc < %s | FileCheck %s

; An owner uses what a function of the module's own that it calls uses, like
; the vftable a specialization of a constructor installs, which a pass made in
; place of the one the owner called: the owner depends on it while it calls
; that function, not where only something else does.

@vftable = external constant ptr
@other = external constant ptr

define internal void @ctor.specialized.1(ptr %this) {
  store ptr @vftable, ptr %this
  ret void
}

define internal void @unrelated(ptr %this) {
  store ptr @other, ptr %this
  ret void
}

define void @owner(ptr %this) #0 !malterlib.sized.deps !0 {
  call void @ctor.specialized.1(ptr %this)
  ret void
}

define void @ordinary(ptr %this) {
  call void @unrelated(ptr %this)
  ret void
}

attributes #0 = { "malterlib-sized" }

!0 = !{i64 0, ptr @vftable, !"malterlib.sized.may-go", ptr @other, !"malterlib.sized.may-go"}

; CHECK: .section .rdata$mibszd,"dr"
; CHECK-NEXT: .p2align 3
; CHECK-NEXT: .quad .Lmib_sized_owner0
; CHECK-NEXT: .quad vftable
; CHECK-NEXT: .quad vftable.mib_sized
; CHECK-NOT: other
