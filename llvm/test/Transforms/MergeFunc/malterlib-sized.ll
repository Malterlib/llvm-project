; RUN: opt -passes=mergefunc -S < %s | FileCheck %s

; A definition that carries the marker of -fmalterlib-sized-destructors is
; referenced together with its marker, which the linker expects at the
; definition's address, so it is not merged into another.

define i64 @a(ptr %p) unnamed_addr #0 {
  %v = load i64, ptr %p
  ret i64 %v
}

define i64 @b(ptr %p) unnamed_addr #0 {
  %v = load i64, ptr %p
  ret i64 %v
}

define i64 @c(ptr %p) unnamed_addr {
  %v = load i64, ptr %p
  ret i64 %v
}

define i64 @d(ptr %p) unnamed_addr {
  %v = load i64, ptr %p
  ret i64 %v
}

attributes #0 = { "malterlib-sized" }

; CHECK:      define i64 @a(ptr %p) unnamed_addr #0 {
; CHECK-NEXT:   %v = load i64, ptr %p
; CHECK:      define i64 @b(ptr %p) unnamed_addr #0 {
; CHECK-NEXT:   %v = load i64, ptr %p
; CHECK:      define i64 @d(ptr %0) unnamed_addr {
; CHECK-NEXT:   {{.*}}call i64 @c(
