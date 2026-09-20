; RUN: opt -module-summary %s -o %t1.bc
; RUN: opt -module-summary %p/Inputs/malterlib-sized-promote.ll -o %t2.bc
; RUN: llvm-lto -thinlto-action=thinlink -o %t.index.bc %t1.bc %t2.bc
; RUN: llvm-lto -thinlto-action=promote -thinlto-index %t.index.bc %t1.bc -o - | llvm-dis -o - | FileCheck %s

; The local marker of -fmalterlib-sized-destructors of a local definition that
; is promoted, for a construction site another module imports, is promoted
; with it, named as the marker of its promoted name, which is how the backend
; spells the references to it.

target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

; CHECK: @llvm.compiler.used = {{.*}}[ptr @ctor.llvm.[[HASH:[0-9]+]].mib_sized, ptr @vftable.llvm.[[HASH]].mib_sized]
; CHECK: @ctor.llvm.[[HASH]].mib_sized = hidden alias void (ptr), ptr @ctor.llvm.[[HASH]]
@llvm.compiler.used = appending global [2 x ptr] [ptr @ctor.mib_sized, ptr @vftable.mib_sized], section "llvm.metadata"
@ctor.mib_sized = internal alias void (ptr), ptr @ctor

; Also of an alias, like a vftable symbol inside its variable past its RTTI.
; CHECK: @vftable.llvm.[[HASH]] = hidden alias ptr, getelementptr inbounds (i8, ptr @vftable.data, i64 8)
; CHECK: @vftable.llvm.[[HASH]].mib_sized = hidden alias ptr, getelementptr inbounds (i8, ptr @vftable.data, i64 8)
@vftable.data = internal constant [2 x ptr] [ptr null, ptr null]
@vftable = internal alias ptr, getelementptr inbounds (i8, ptr @vftable.data, i64 8)
@vftable.mib_sized = internal alias ptr, getelementptr inbounds (i8, ptr @vftable.data, i64 8)

; CHECK: define hidden void @ctor.llvm.[[HASH]](ptr %this)
define internal void @ctor(ptr %this) "malterlib-sized" {
  ret void
}

define void @site(ptr %p) alwaysinline {
  call void @ctor(ptr %p)
  store ptr @vftable, ptr %p
  ret void
}
