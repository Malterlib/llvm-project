; RUN: opt -S -passes=globalsplit %s | FileCheck %s

target datalayout = "e-p:64:64"
target triple = "x86_64-unknown-linux-gnu"

; A global that carries a marker of -fmalterlib-sized-destructors, lists those
; of its aliases, or holds owners with dependencies, stays whole: the markers
; and the dependencies are at its address.

; CHECK: @marked = internal constant { [1 x ptr], [1 x ptr] }
; CHECK: @owner = internal constant { [1 x ptr], [1 x ptr] }
; CHECK-NOT: @marked.0
; CHECK-NOT: @owner.0

@marked = internal constant { [1 x ptr], [1 x ptr] } {
  [1 x ptr] [ptr @f],
  [1 x ptr] [ptr @f]
}, !type !0 #0

@owner = internal constant { [1 x ptr], [1 x ptr] } {
  [1 x ptr] [ptr @f],
  [1 x ptr] [ptr @f]
}, !type !0, !malterlib.sized.deps !1

define ptr @f() {
  ret ptr getelementptr inrange(0, 8) ({ [1 x ptr], [1 x ptr] }, ptr @marked, i32 0, i32 0, i32 0)
}

define ptr @g() {
  ret ptr getelementptr inrange(0, 8) ({ [1 x ptr], [1 x ptr] }, ptr @owner, i32 0, i32 0, i32 0)
}

define void @dep() {
  ret void
}

declare i1 @llvm.type.test(ptr, metadata)

define i1 @use(ptr %p) {
  %x = call i1 @llvm.type.test(ptr %p, metadata !"t")
  ret i1 %x
}

attributes #0 = { "malterlib-sized" }

!0 = !{i32 0, !"t"}
!1 = !{i64 0, ptr @dep, null}
