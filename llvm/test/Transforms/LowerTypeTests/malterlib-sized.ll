; RUN: opt -S -passes=lowertypetests %s | FileCheck %s

target datalayout = "e-p:64:64"

; A global the pass rebuilds hands its markers of -fmalterlib-sized-destructors
; and the dependencies of their owners to the global that holds it, at its
; offset there.

; CHECK-DAG: = private constant {{.*}}!malterlib.sized.deps ![[A:[0-9]+]]
; CHECK-DAG: ![[A]] = !{i64 {{[0-9]+}}, ptr @dep, null}

@a = constant [1 x ptr] [ptr null], !type !0, !malterlib.sized.deps !1 #0
@b = constant [1 x ptr] [ptr null], !type !0

declare void @dep()

define i1 @f(ptr %p) {
  %x = call i1 @llvm.type.test(ptr %p, metadata !"typeid1")
  ret i1 %x
}

declare i1 @llvm.type.test(ptr, metadata)

attributes #0 = { "malterlib-sized" }

!0 = !{i32 0, !"typeid1"}
!1 = !{i64 0, ptr @dep, null}
