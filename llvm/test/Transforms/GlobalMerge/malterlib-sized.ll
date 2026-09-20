; RUN: opt -passes='global-merge<max-offset=100;merge-const>' -S < %s | FileCheck %s

; A global that carries a marker of -fmalterlib-sized-destructors, or lists
; those of its aliases, keeps its own symbol, which the marker is at.

; CHECK: @c = internal constant i32 3 #0
; CHECK: @d = internal constant i32 4 #1
; CHECK: @_MergedGlobals = private constant <{ i32, i32 }> <{ i32 1, i32 2 }>, align 4

@a = internal constant i32 1
@b = internal constant i32 2
@c = internal constant i32 3 #0
@d = internal constant i32 4 #1

define void @use() {
  %a = load i32, ptr @a
  %b = load i32, ptr @b
  %c = load i32, ptr @c
  %d = load i32, ptr @d
  ret void
}

attributes #0 = { "malterlib-sized" }
attributes #1 = { "malterlib-sized-aliases"="e=0" }
