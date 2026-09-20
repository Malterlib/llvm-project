; RUN: opt -thinlto-bc -thinlto-split-lto-unit -o %t %s
; RUN: llvm-modextract -b -n 0 -o %t0 %t
; RUN: llvm-modextract -b -n 1 -o %t1 %t
; RUN: llvm-dis -o - %t0 | FileCheck --check-prefix=M0 %s
; RUN: llvm-dis -o - %t1 | FileCheck --check-prefix=M1 %s

; A local definition the split promotes takes its local marker of
; -fmalterlib-sized-destructors with it in both modules, which then is named
; as the marker of the definition's new name, as the records that name the
; definition refer to it, also where one module only declares the marker.

target triple = "x86_64-unknown-linux-gnu"

; M0: @vtable.[[ID:[0-9a-f]+]].mib_sized = external hidden global
; M1: @vtable.[[ID:[0-9a-f]+]] = hidden constant [1 x ptr] [ptr @deleting.[[ID]]], !type !0
; M1: @vtable.[[ID]].mib_sized = hidden alias [1 x ptr], ptr @vtable.[[ID]]
; M0: @deleting.[[ID]].mib_sized = hidden alias void (ptr), ptr @deleting.[[ID]]
@vtable = internal constant [1 x ptr] [ptr @deleting], !type !0
@vtable.mib_sized = internal alias [1 x ptr], ptr @vtable
@deleting.mib_sized = internal alias void (ptr), ptr @deleting
@llvm.compiler.used = appending global [2 x ptr] [ptr @vtable.mib_sized, ptr @deleting.mib_sized], section "llvm.metadata"

define internal void @deleting(ptr %this) {
  ret void
}

define ptr @construct(ptr %p) {
  store ptr @vtable, ptr %p
  ret ptr %p
}

!0 = !{i64 0, !"vtable"}
