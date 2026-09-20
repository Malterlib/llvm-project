; REQUIRES: x86
;; Objects that link-time optimization compiles and objects compiled before the
;; link mix: the marker of the vector deleting destructor, a weak alias, is
;; that of the copy of the scalar destructor the link keeps, whichever kind of
;; object it comes from.

; RUN: rm -rf %t; split-file %s %t
; RUN: opt -module-summary %t/flagged.ll -o %t/flagged.obj
; RUN: opt -module-summary %t/unflagged.ll -o %t/unflagged.obj
; RUN: llc -filetype=obj %t/flagged.ll -o %t/flagged-native.obj
; RUN: llc -filetype=obj %t/unflagged.ll -o %t/unflagged-native.obj

; RUN: lld-link /dll /noentry %t/flagged.obj %t/flagged-native.obj /out:%t/a.dll
; RUN: lld-link /dll /noentry %t/flagged-native.obj %t/flagged.obj /out:%t/b.dll
; RUN: lld-link /dll /noentry %t/flagged.obj %t/unflagged-native.obj /out:%t/c.dll
; RUN: lld-link /dll /noentry %t/flagged-native.obj %t/unflagged.obj /out:%t/d.dll
; RUN: env LLD_IN_TEST=1 not lld-link /dll /noentry %t/unflagged-native.obj %t/flagged.obj \
; RUN:   /out:%t/e.dll 2>&1 | FileCheck %s
; RUN: env LLD_IN_TEST=1 not lld-link /dll /noentry %t/unflagged.obj %t/flagged-native.obj \
; RUN:   /out:%t/f.dll 2>&1 | FileCheck %s

;; So do objects compiled before the link alone.
; RUN: lld-link /dll /noentry %t/flagged-native.obj %t/unflagged-native.obj /out:%t/g.dll
; RUN: env LLD_IN_TEST=1 not lld-link /dll /noentry %t/unflagged-native.obj %t/flagged-native.obj \
; RUN:   /out:%t/h.dll 2>&1 | FileCheck %s

; CHECK:      error: public: __cdecl CObject::CObject(void) depends on const CObject::`vftable'
; CHECK-NEXT: >>> const CObject::`vftable' depends on public: virtual void * __cdecl CObject::`vector deleting dtor'(unsigned int)
; CHECK-NEXT: >>> public: virtual void * __cdecl CObject::`vector deleting dtor'(unsigned int) was not compiled with -fmalterlib-sized-destructors
; CHECK-NEXT: >>> its marker ??_ECObject@@UEAAPEAXI@Z.mib_sized is undefined

;--- flagged.ll
target datalayout = "e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-windows-msvc"

$"?fg_Make@@YAPEAUCObject@@PEAX@Z" = comdat any
$"??0CObject@@QEAA@XZ" = comdat any
$"??_GCObject@@UEAAPEAXI@Z" = comdat any
$"??_7CObject@@6B@" = comdat any

@"??_7CObject@@6B@" = linkonce_odr unnamed_addr constant { [1 x ptr] } { [1 x ptr] [ptr @"??_ECObject@@UEAAPEAXI@Z"] }, comdat, !malterlib.sized.deps !0 #0
@"??0CObject@@QEAA@XZ.mib_sized" = external constant i8
@__mib_sized_ref = private constant [2 x ptr] [ptr @"??0CObject@@QEAA@XZ", ptr @"??0CObject@@QEAA@XZ.mib_sized"], section ".rdata$mibszf"
@__mib_sized_kept = internal constant [2 x ptr] [ptr @"??_7CObject@@6B@", ptr @"??_ECObject@@UEAAPEAXI@Z"], section "llvm.metadata"
@llvm.compiler.used = appending global [2 x ptr] [ptr @__mib_sized_ref, ptr @__mib_sized_kept], section "llvm.metadata"

@"??_ECObject@@UEAAPEAXI@Z" = weak dso_local unnamed_addr alias { ptr, i64 } (ptr, i32), ptr @"??_GCObject@@UEAAPEAXI@Z"

define weak_odr dllexport ptr @"?fg_Make@@YAPEAUCObject@@PEAX@Z"(ptr %memory) comdat {
  %object = call ptr @"??0CObject@@QEAA@XZ"(ptr %memory)
  ret ptr %object
}

define linkonce_odr ptr @"??0CObject@@QEAA@XZ"(ptr returned %this) #1 comdat !malterlib.sized.deps !1 {
  store ptr @"??_7CObject@@6B@", ptr %this
  ret ptr %this
}

define linkonce_odr { ptr, i64 } @"??_GCObject@@UEAAPEAXI@Z"(ptr %this, i32 %flags) #2 comdat {
  %memory = insertvalue { ptr, i64 } poison, ptr %this, 0
  %result = insertvalue { ptr, i64 } %memory, i64 8, 1
  ret { ptr, i64 } %result
}

attributes #0 = { "malterlib-sized" }
attributes #1 = { noinline "malterlib-sized" }
attributes #2 = { noinline "malterlib-sized" "malterlib-sized-aliases"="??_ECObject@@UEAAPEAXI@Z=0" }

!0 = !{i64 0, ptr @"??_ECObject@@UEAAPEAXI@Z", null}
!1 = !{i64 0, ptr @"??_7CObject@@6B@", null}

;--- unflagged.ll
;; The destructors of the class compiled without the flag.
target datalayout = "e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-windows-msvc"

$"??_GCObject@@UEAAPEAXI@Z" = comdat any

@__mib_sized_kept = internal constant [1 x ptr] [ptr @"??_ECObject@@UEAAPEAXI@Z"], section "llvm.metadata"
@llvm.compiler.used = appending global [1 x ptr] [ptr @__mib_sized_kept], section "llvm.metadata"

@"??_ECObject@@UEAAPEAXI@Z" = weak dso_local unnamed_addr alias ptr (ptr, i32), ptr @"??_GCObject@@UEAAPEAXI@Z"

define linkonce_odr ptr @"??_GCObject@@UEAAPEAXI@Z"(ptr %this, i32 %flags) #0 comdat {
  ret ptr %this
}

attributes #0 = { noinline }
