; RUN: opt -passes=dfsan -S < %s | FileCheck %s
target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

; The marker of -fmalterlib-sized-destructors of a definition the pass renames
; is renamed with it: the references to it, and the entry of an alias its
; definition lists.

; CHECK: @_ZN1AC1Ev.dfsan.mib_sized = external constant i8
; CHECK: @"\01_ZN1VD0Ev.dfsan@@8.mib_sized" = external constant i8
; CHECK: define void @_ZN1AC2Ev.dfsan() #[[CTOR:[0-9]+]]
; CHECK: attributes #[[CTOR]] = { {{.*}}"malterlib-sized" "malterlib-sized-aliases"="_ZN1AC1Ev.dfsan=0"

@_ZN1AC1Ev.mib_sized = external constant i8
@"\01_ZN1VD0Ev@@8.mib_sized" = external constant i8
@__mib_sized_ref = private constant [4 x ptr] [ptr @_ZN1AC1Ev, ptr @_ZN1AC1Ev.mib_sized, ptr @_ZN1VD0Ev, ptr @"\01_ZN1VD0Ev@@8.mib_sized"]
@llvm.compiler.used = appending global [1 x ptr] [ptr @__mib_sized_ref], section "llvm.metadata"

@_ZN1AC1Ev = alias void (), ptr @_ZN1AC2Ev

define void @_ZN1AC2Ev() #0 {
  ret void
}

; A marker the calling convention of its definition decorates is renamed
; decorated.
define x86_vectorcallcc void @_ZN1VD0Ev(ptr %this) #1 {
  ret void
}

attributes #0 = { "malterlib-sized" "malterlib-sized-aliases"="_ZN1AC1Ev=0" }
attributes #1 = { "malterlib-sized" }
