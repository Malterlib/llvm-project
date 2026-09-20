; REQUIRES: x86
;; With -lldmingw, a weak definition that an object compiled before the link and
;; an object link-time optimization compiles both define is a weak alias in
;; each, which keeps the fallback of the first: the marker of the constructor
;; falls back on that of the copy the link keeps, also when the bitcode one
;; prevails, which comes with no marker of its own until it is compiled.

; RUN: rm -rf %t; split-file %s %t
; RUN: llc -filetype=obj %t/site.ll -o %t/site.obj
; RUN: opt -module-summary %t/class.ll -o %t/class.obj
; RUN: llvm-as %t/class.ll -o %t/class-full.obj
; RUN: opt -module-summary %t/class-unflagged.ll -o %t/class-unflagged.obj
; RUN: llc -filetype=obj %t/class-unflagged.ll -o %t/class-unflagged-native.obj

; RUN: lld-link -lldmingw /dll /noentry %t/site.obj %t/class.obj /out:%t/a.dll
; RUN: lld-link -lldmingw /dll /noentry %t/class.obj %t/site.obj /out:%t/b.dll
; RUN: lld-link -lldmingw /dll /noentry %t/site.obj %t/class-full.obj /out:%t/c.dll
; RUN: lld-link -lldmingw /dll /noentry %t/class-full.obj %t/site.obj /out:%t/d.dll

;; The copy the link keeps is the bitcode's, compiled without the flag, or the
;; native one, which comes first.
; RUN: env LLD_IN_TEST=1 not lld-link -lldmingw /dll /noentry %t/site.obj %t/class-unflagged.obj \
; RUN:   /out:%t/e.dll 2>&1 | FileCheck %s
; RUN: env LLD_IN_TEST=1 not lld-link -lldmingw /dll /noentry %t/class-unflagged.obj %t/site.obj \
; RUN:   /out:%t/f.dll 2>&1 | FileCheck %s
; RUN: lld-link -lldmingw /dll /noentry %t/site.obj %t/class-unflagged-native.obj /out:%t/g.dll
; RUN: env LLD_IN_TEST=1 not lld-link -lldmingw /dll /noentry %t/class-unflagged-native.obj %t/site.obj \
; RUN:   /out:%t/h.dll 2>&1 | FileCheck %s

; CHECK: error: A::A() was {{.*}}compiled {{.*}}without -fmalterlib-sized-destructors

;--- site.ll
target datalayout = "e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-windows-gnu"

$_ZTV1A = comdat any
$_ZN1AD0Ev = comdat any

@_ZTV1A = linkonce_odr dso_local unnamed_addr constant { [3 x ptr] } { [3 x ptr] [ptr null, ptr null, ptr @_ZN1AD0Ev] }, comdat, !malterlib.sized.deps !0 #0
@_ZN1AC1Ev.mib_sized = external constant i8
@__mib_sized_ref = private constant [2 x ptr] [ptr @_ZN1AC1Ev, ptr @_ZN1AC1Ev.mib_sized], section ".rdata$mibszf"
@__mib_sized_kept = internal constant [3 x ptr] [ptr @_ZTV1A, ptr @_ZN1AC1Ev, ptr @_ZN1AD0Ev], section "llvm.metadata"
@llvm.compiler.used = appending global [2 x ptr] [ptr @__mib_sized_kept, ptr @__mib_sized_ref], section "llvm.metadata"

define weak dso_local void @_ZN1AC1Ev(ptr %this) #1 !malterlib.sized.deps !1 {
  store ptr getelementptr (i8, ptr @_ZTV1A, i64 16), ptr %this
  ret void
}

define dso_local dllexport ptr @make(ptr %memory) {
  call void @_ZN1AC1Ev(ptr %memory)
  ret ptr %memory
}

define linkonce_odr dso_local { ptr, i64 } @_ZN1AD0Ev(ptr %this) #1 comdat {
  %memory = insertvalue { ptr, i64 } poison, ptr %this, 0
  %result = insertvalue { ptr, i64 } %memory, i64 16, 1
  ret { ptr, i64 } %result
}

attributes #0 = { "malterlib-sized" }
attributes #1 = { noinline "malterlib-sized" }

!0 = !{i64 0, ptr @_ZN1AD0Ev, null}
!1 = !{i64 0, ptr @_ZTV1A, null}

;--- class.ll
target datalayout = "e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-windows-gnu"

$_ZTV1A = comdat any
$_ZN1AD0Ev = comdat any

@_ZTV1A = linkonce_odr dso_local unnamed_addr constant { [3 x ptr] } { [3 x ptr] [ptr null, ptr null, ptr @_ZN1AD0Ev] }, comdat, !malterlib.sized.deps !0 #0
@__mib_sized_kept = internal constant [3 x ptr] [ptr @_ZTV1A, ptr @_ZN1AC1Ev, ptr @_ZN1AD0Ev], section "llvm.metadata"
@llvm.compiler.used = appending global [1 x ptr] [ptr @__mib_sized_kept], section "llvm.metadata"

define weak dso_local void @_ZN1AC1Ev(ptr %this) #1 !malterlib.sized.deps !1 {
  store ptr getelementptr (i8, ptr @_ZTV1A, i64 16), ptr %this
  ret void
}

define linkonce_odr dso_local { ptr, i64 } @_ZN1AD0Ev(ptr %this) #1 comdat {
  %memory = insertvalue { ptr, i64 } poison, ptr %this, 0
  %result = insertvalue { ptr, i64 } %memory, i64 16, 1
  ret { ptr, i64 } %result
}

attributes #0 = { "malterlib-sized" }
attributes #1 = { noinline "malterlib-sized" }

!0 = !{i64 0, ptr @_ZN1AD0Ev, null}
!1 = !{i64 0, ptr @_ZTV1A, null}

;--- class-unflagged.ll
target datalayout = "e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-windows-gnu"

$_ZTV1A = comdat any
$_ZN1AD0Ev = comdat any

@_ZTV1A = linkonce_odr dso_local unnamed_addr constant { [3 x ptr] } { [3 x ptr] [ptr null, ptr null, ptr @_ZN1AD0Ev] }, comdat

define weak dso_local void @_ZN1AC1Ev(ptr %this) #0 {
  store ptr getelementptr (i8, ptr @_ZTV1A, i64 16), ptr %this
  ret void
}

define linkonce_odr dso_local ptr @_ZN1AD0Ev(ptr %this) #0 comdat {
  ret ptr %this
}

attributes #0 = { noinline }
