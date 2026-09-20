; RUN: opt -passes=ipsccp -force-specialization -S < %s | FileCheck %s --implicit-check-not=_ZN1AC2Ev.specialized --implicit-check-not=listed.specialized

; A definition the construction sites of -fmalterlib-sized-destructors depend
; on isn't specialized: one with its marker, and those of the aliases it lists,
; which their references name, and one they list, which they reference only
; while it is used. Copies in its place, which don't have the marker, would
; leave the sites without a requirement of what the copies install.

; CHECK: define internal i32 @_ZN1AC2Ev(ptr %f) [[MARKED:#[0-9]+]]
define internal i32 @_ZN1AC2Ev(ptr %f) #0 {
  %r = call i32 %f()
  ret i32 %r
}

; CHECK: define internal i32 @listed(ptr %f) {
define internal i32 @listed(ptr %f) {
  %r = call i32 %f()
  ret i32 %r
}

; But another function is.
; CHECK: define internal i32 @other.specialized.1(
define internal i32 @other(ptr %f) {
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
  %c = call i32 @listed(ptr @one)
  %d = call i32 @listed(ptr @two)
  %e = call i32 @other(ptr @one)
  %f = call i32 @other(ptr @two)
  %ab = add i32 %a, %b
  %cd = add i32 %c, %d
  %ef = add i32 %e, %f
  %abcd = add i32 %ab, %cd
  %r = add i32 %abcd, %ef
  ret i32 %r
}

!malterlib.sized.inlinable = !{!0}
!0 = !{ptr @listed}

; CHECK: attributes [[MARKED]] = { "malterlib-sized" "malterlib-sized-aliases"="_ZN1AC1Ev=0" }
attributes #0 = { "malterlib-sized" "malterlib-sized-aliases"="_ZN1AC1Ev=0" }
