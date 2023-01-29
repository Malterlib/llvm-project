; RUN: opt -passes="ee-instrument" -S < %s | FileCheck %s

; A function named by the instrument-function-entry attribute other than the
; ones of the options is called like __cyg_profile_func_enter, with the address
; of the function and the address it returns to.

target triple = "x86_64-unknown-linux-gnu"

define void @f() #0 {
; CHECK-LABEL: define void @f()
; CHECK: [[RETURN:%.*]] = call ptr @llvm.returnaddress{{.*}}(i32 0)
; CHECK-NEXT: call void @fg_Inspect(ptr @f, ptr [[RETURN]])
  ret void
}

; CHECK: declare void @fg_Inspect(ptr, ptr)

attributes #0 = { "instrument-function-entry"="fg_Inspect" }
