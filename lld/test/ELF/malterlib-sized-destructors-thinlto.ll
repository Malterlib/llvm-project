; REQUIRES: x86
;; A definition link-time optimization compiles carries its marker into the
;; object the optimization makes, in its section group, and a copy compiled
;; without -fmalterlib-sized-destructors that the link keeps has none, whether
;; the copies are bitcode or objects compiled before the link.

; RUN: rm -rf %t; split-file %s %t
; RUN: opt -module-summary %t/flagged.ll -o %t/flagged.o
; RUN: opt -module-summary %t/unflagged.ll -o %t/unflagged.o
; RUN: llvm-as %t/flagged.ll -o %t/flagged-full.o
; RUN: llvm-as %t/unflagged.ll -o %t/unflagged-full.o
; RUN: llc -filetype=obj -relocation-model=pic %t/flagged.ll -o %t/flagged-native.o
; RUN: llc -filetype=obj -relocation-model=pic %t/unflagged.ll -o %t/unflagged-native.o

; RUN: ld.lld -shared %t/flagged.o -o %t/flagged.so
; RUN: ld.lld -shared %t/flagged.o %t/unflagged.o -o %t/flagged-first.so
; RUN: not ld.lld -shared %t/unflagged.o %t/flagged.o -o /dev/null 2>&1 | FileCheck %s
; RUN: ld.lld -shared %t/flagged-full.o -o %t/flagged-full.so
; RUN: ld.lld -shared %t/flagged-full.o %t/unflagged-full.o -o %t/flagged-first-full.so
; RUN: not ld.lld -shared %t/unflagged-full.o %t/flagged-full.o -o /dev/null 2>&1 | FileCheck %s

; RUN: ld.lld -shared %t/flagged.o %t/flagged-native.o -o %t/mixed.so
; RUN: ld.lld -shared %t/flagged-native.o %t/flagged.o -o %t/mixed.so
; RUN: ld.lld -shared %t/flagged.o %t/unflagged-native.o -o %t/mixed.so
; RUN: not ld.lld -shared %t/unflagged-native.o %t/flagged.o -o /dev/null 2>&1 | FileCheck %s
; RUN: not ld.lld -shared %t/unflagged.o %t/flagged-native.o -o /dev/null 2>&1 | FileCheck %s
;; A constructor the optimization makes local is referenced through its
;; section, and named by its symbol.
; RUN: sed -e 's/) #1 comdat !malterlib.sized.deps !1/) unnamed_addr #1 comdat !malterlib.sized.deps !1/' %t/flagged.ll > %t/flagged-local.ll
; RUN: opt -module-summary %t/flagged-local.ll -o %t/flagged-local.o
; RUN: llvm-as %t/flagged-local.ll -o %t/flagged-local-full.o
; RUN: not ld.lld -shared %t/unflagged.o %t/flagged-local.o -o /dev/null 2>&1 | FileCheck %s
; RUN: not ld.lld -shared %t/unflagged-full.o %t/flagged-local-full.o -o /dev/null 2>&1 | FileCheck %s

; CHECK:      error: CObject::CObject() depends on vtable for CObject
; CHECK-NEXT: >>> vtable for CObject depends on CObject::~CObject()
; CHECK-NEXT: >>> CObject::~CObject() was not compiled with -fmalterlib-sized-destructors

;--- flagged.ll
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

$_ZN7CObjectC1Ev = comdat any
$_ZN7CObjectD0Ev = comdat any
$_ZTV7CObject = comdat any

@_ZTV7CObject = linkonce_odr unnamed_addr constant { [3 x ptr] } { [3 x ptr] [ptr null, ptr null, ptr @_ZN7CObjectD0Ev] }, comdat, !malterlib.sized.deps !0 #0
@_ZN7CObjectC1Ev.mib_sized = external constant i8
@__mib_sized_ref = private constant [2 x ptr] [ptr @_ZN7CObjectC1Ev, ptr @_ZN7CObjectC1Ev.mib_sized], section ".data.rel.ro.__mib_sized_ref"
@__mib_sized_kept = internal constant [2 x ptr] [ptr @_ZTV7CObject, ptr @_ZN7CObjectD0Ev], section "llvm.metadata"
@llvm.compiler.used = appending global [2 x ptr] [ptr @__mib_sized_ref, ptr @__mib_sized_kept], section "llvm.metadata"

define weak_odr ptr @_Z7fg_MakePv(ptr %memory) {
  call void @_ZN7CObjectC1Ev(ptr %memory)
  ret ptr %memory
}

define linkonce_odr void @_ZN7CObjectC1Ev(ptr %this) #1 comdat !malterlib.sized.deps !1 {
  store ptr getelementptr inbounds ({ [3 x ptr] }, ptr @_ZTV7CObject, i32 0, i32 0, i32 2), ptr %this
  ret void
}

define linkonce_odr { ptr, i64 } @_ZN7CObjectD0Ev(ptr %this) #1 comdat {
  %memory = insertvalue { ptr, i64 } poison, ptr %this, 0
  %result = insertvalue { ptr, i64 } %memory, i64 8, 1
  ret { ptr, i64 } %result
}

attributes #0 = { "malterlib-sized" }
attributes #1 = { noinline "malterlib-sized" }

!0 = !{i64 0, ptr @_ZN7CObjectD0Ev, null}
!1 = !{i64 0, ptr @_ZTV7CObject, null}

;--- unflagged.ll
;; The deleting destructor of the class compiled without the flag.
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

$_ZN7CObjectD0Ev = comdat any

@__mib_sized_kept = internal constant [1 x ptr] [ptr @_ZN7CObjectD0Ev], section "llvm.metadata"
@llvm.compiler.used = appending global [1 x ptr] [ptr @__mib_sized_kept], section "llvm.metadata"

define linkonce_odr void @_ZN7CObjectD0Ev(ptr %this) #0 comdat {
  ret void
}

attributes #0 = { noinline }
