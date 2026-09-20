; RUN: opt -passes=wholeprogramdevirt -whole-program-visibility -wholeprogramdevirt-summary-action=export -wholeprogramdevirt-read-summary=%S/Inputs/export.yaml -wholeprogramdevirt-write-summary=%t -S -o - %s | FileCheck %s

; A single implementation the pass renames for export keeps the marker of
; -fmalterlib-sized-destructors the references to it name.

; CHECK: @vf3.llvm.merged.mib_sized = external constant i8
; CHECK: @__mib_sized_ref = private constant [2 x ptr] [ptr @vf3.llvm.merged, ptr @vf3.llvm.merged.mib_sized]
; CHECK: define hidden void @vf3.llvm.merged(ptr %0) #

@vt3 = constant ptr @vf3, !type !0

@vf3.mib_sized = external constant i8
@__mib_sized_ref = private constant [2 x ptr] [ptr @vf3, ptr @vf3.mib_sized]
@llvm.compiler.used = appending global [1 x ptr] [ptr @__mib_sized_ref], section "llvm.metadata"

define internal void @vf3(ptr) #0 {
  ret void
}

attributes #0 = { "malterlib-sized" }

!0 = !{i32 0, !"typeid3"}
