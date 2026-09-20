; RUN: opt -passes=ipsccp -force-specialization -S < %s | FileCheck %s

; The marker of a definition, and those of the aliases it lists, belong to the
; definition alone, not to its specializations.

; CHECK-NOT: malterlib-sized

define internal i32 @_ZN1AC2Ev(ptr %f) #0 {
  %r = call i32 %f()
  ret i32 %r
}

define i32 @one() {
  ret i32 1
}

define i32 @two() {
  ret i32 2
}

define i32 @main() {
  %a = call i32 @_ZN1AC2Ev(ptr @one)
  %b = call i32 @_ZN1AC2Ev(ptr @two)
  %r = add i32 %a, %b
  ret i32 %r
}

attributes #0 = { "malterlib-sized" "malterlib-sized-aliases"="_ZN1AC1Ev=0" }
