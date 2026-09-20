; RUN: opt -passes=inline -S < %s | FileCheck %s

; A function a construction site of -fmalterlib-sized-destructors is inlined in
; is a site too, whose code uses what the site depends on wherever it is
; emitted, also through a function the site was inlined in first.

declare void @ctor(ptr)

define linkonce_odr void @site(ptr %p) #0 {
  call void @ctor(ptr %p)
  ret void
}

define linkonce_odr void @helper(ptr %p) {
  call void @site(ptr %p)
  ret void
}

; CHECK: define linkonce_odr void @destructor(ptr %this) [[DESTRUCTOR:#[0-9]+]]
define linkonce_odr void @destructor(ptr %this) #1 {
  call void @helper(ptr %this)
  ret void
}

; CHECK: define void @other(ptr %p) {
define void @other(ptr %p) {
  call void @ctor(ptr %p)
  ret void
}

; CHECK: attributes [[DESTRUCTOR]] = { "malterlib-sized-destructor" "malterlib-sized-site" }

attributes #0 = { "malterlib-sized-site" }
attributes #1 = { "malterlib-sized-destructor" }
