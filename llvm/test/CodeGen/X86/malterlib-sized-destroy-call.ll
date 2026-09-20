; RUN: llc -mtriple=x86_64-windows-msvc < %s | FileCheck %s

; The deleting destructor a call of `__builtin_malterlib_destroy` calls
; directly, which devirtualization made direct, also in a module with no
; construction site, is referenced, but not one it still calls through a
; vtable, nor a direct call that isn't the builtin's.

%result = type { ptr, i64 }

declare %result @deleting(ptr, i32)
declare %result @other(ptr, i32)

define %result @destroy(ptr %p) {
  %r = call %result @deleting(ptr %p, i32 0), !malterlib.sized.destroy !0
  ret %result %r
}

define %result @virtual(ptr %p, ptr %f) {
  %r = call %result %f(ptr %p, i32 0), !malterlib.sized.destroy !0
  ret %result %r
}

define %result @ordinary(ptr %p) {
  %r = call %result @other(ptr %p, i32 1)
  ret %result %r
}

!0 = !{}

; CHECK: .section .rdata$mibszf,"dr"
; CHECK-NEXT: .p2align 3
; CHECK-NEXT: .quad deleting
; CHECK-NEXT: .quad deleting.mib_sized
; CHECK-NOT: .quad other
