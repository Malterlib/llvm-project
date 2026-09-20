; REQUIRES: x86
;; A constructor that link-time optimization compiles depends on a vtable that
;; MinGW imports automatically from a DLL. The code generator gives the
;; dependency the vtable's marker, which no object before the optimization
;; referred to: its import is loaded after the optimization, as the vtable's
;; was before it.

; RUN: rm -rf %t; split-file %s %t
; RUN: llvm-mc -filetype=obj -triple=x86_64-windows-gnu %t/dll.s -o %t/dll.obj
; RUN: lld-link -lldmingw /dll /noentry %t/dll.obj /out:%t/a.dll /implib:%t/a.lib
; RUN: opt -module-summary %t/site.ll -o %t/site-thin.obj
; RUN: llvm-as %t/site.ll -o %t/site-full.obj
; RUN: llc -filetype=obj %t/site.ll -o %t/site-native.obj
; RUN: lld-link -lldmingw /dll /noentry %t/site-native.obj %t/a.lib /out:%t/native.dll
; RUN: lld-link -lldmingw /dll /noentry %t/site-thin.obj %t/a.lib /out:%t/thin.dll
; RUN: lld-link -lldmingw /dll /noentry %t/site-full.obj %t/a.lib /out:%t/full.dll

;--- dll.s
.section .rdata,"dr"
.globl _ZTV7Foreign
.globl _ZTV7Foreign.mib_sized
_ZTV7Foreign:
_ZTV7Foreign.mib_sized:
  .quad 0

.section .drectve,"yni"
.ascii " -export:_ZTV7Foreign,data -export:_ZTV7Foreign.mib_sized,data"

;--- site.ll
target datalayout = "e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-w64-windows-gnu"

$_ZN7ForeignC1Ev = comdat any

@_ZTV7Foreign = external global ptr
@_ZN7ForeignC1Ev.mib_sized = external constant i8
@__mib_sized_ref = private constant [2 x ptr] [ptr @_ZN7ForeignC1Ev, ptr @_ZN7ForeignC1Ev.mib_sized], section ".rdata$mibszf"
@llvm.compiler.used = appending global [1 x ptr] [ptr @__mib_sized_ref], section "llvm.metadata"

define dllexport ptr @make(ptr %memory) {
  %object = call ptr @_ZN7ForeignC1Ev(ptr %memory)
  ret ptr %object
}

define linkonce_odr ptr @_ZN7ForeignC1Ev(ptr returned %this) #0 comdat !malterlib.sized.deps !0 {
  store ptr @_ZTV7Foreign, ptr %this
  ret ptr %this
}

attributes #0 = { noinline "malterlib-sized" }

!0 = !{i64 0, ptr @_ZTV7Foreign, null}
