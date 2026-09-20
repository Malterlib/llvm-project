; REQUIRES: x86
;; The vector deleting destructor is a weak alias of the scalar one, which
;; lld-link resolves late, as it does a weak alias in an object, so link-time
;; optimization leaves it declared in the object it makes. The code generator
;; gives the declaration the marker of the alias, a weak alias of the scalar
;; destructor's marker, which a copy compiled without
;; -fmalterlib-sized-destructors does not have.

; RUN: rm -rf %t; split-file %s %t
; RUN: opt -module-summary %t/flagged.ll -o %t/flagged.obj
; RUN: opt -module-summary %t/unflagged.ll -o %t/unflagged.obj
; RUN: lld-link /dll /noentry %t/flagged.obj /out:%t/flagged.dll
; RUN: lld-link /dll /noentry %t/flagged.obj %t/unflagged.obj /out:%t/flagged-first.dll
; RUN: env LLD_IN_TEST=1 not lld-link /dll /noentry %t/unflagged.obj %t/flagged.obj \
; RUN:   /out:%t/unflagged-first.dll 2>&1 | FileCheck %s

;; Full link-time optimization does the same.
; RUN: llvm-as %t/flagged.ll -o %t/flagged-full.obj
; RUN: llvm-as %t/unflagged.ll -o %t/unflagged-full.obj
; RUN: lld-link /dll /noentry %t/flagged-full.obj /out:%t/flagged-full.dll
; RUN: lld-link /dll /noentry %t/flagged-full.obj %t/unflagged-full.obj /out:%t/flagged-first-full.dll
; RUN: env LLD_IN_TEST=1 not lld-link /dll /noentry %t/unflagged-full.obj %t/flagged-full.obj \
; RUN:   /out:%t/unflagged-first-full.dll 2>&1 | FileCheck %s

; CHECK:      error: public: __cdecl CObject::CObject(void) depends on const CObject::`vftable'
; CHECK-NEXT: >>> const CObject::`vftable' depends on public: virtual void * __cdecl CObject::`vector deleting dtor'(unsigned int)
; CHECK-NEXT: >>> public: virtual void * __cdecl CObject::`vector deleting dtor'(unsigned int) was not compiled with -fmalterlib-sized-destructors
; CHECK-NEXT: >>> its marker ??_ECObject@@UEAAPEAXI@Z.mib_sized is undefined

;; A construction site in another module, which imports and inlines the
;; constructor, depends on the marker of the copy its module exports.
; RUN: opt -module-summary %t/site.ll -o %t/site.obj
; RUN: opt -module-summary %t/class.ll -o %t/class.obj
; RUN: lld-link /dll /noentry -lldsavetemps %t/site.obj %t/class.obj /out:%t/import.dll
; RUN: llvm-dis %t/site.obj.3.import.bc -o - | FileCheck %s --check-prefix=IMPORT
; IMPORT: define available_externally {{.*}} @"??0CObject@@QEAA@XZ"
; RUN: env LLD_IN_TEST=1 not lld-link /dll /noentry %t/site.obj %t/unflagged.obj %t/class.obj \
; RUN:   /out:%t/import-unflagged.dll 2>&1 | FileCheck %s

;--- flagged.ll
target datalayout = "e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-windows-msvc"

$"??0CObject@@QEAA@XZ" = comdat any
$"??_GCObject@@UEAAPEAXI@Z" = comdat any
$"??_7CObject@@6B@" = comdat any

@"??_7CObject@@6B@" = linkonce_odr unnamed_addr constant { [1 x ptr] } { [1 x ptr] [ptr @"??_ECObject@@UEAAPEAXI@Z"] }, comdat, !malterlib.sized.deps !0 #0
@"??0CObject@@QEAA@XZ.mib_sized" = external constant i8
@__mib_sized_ref = private constant [2 x ptr] [ptr @"??0CObject@@QEAA@XZ", ptr @"??0CObject@@QEAA@XZ.mib_sized"], section ".rdata$mibszf"
@__mib_sized_kept = internal constant [2 x ptr] [ptr @"??_7CObject@@6B@", ptr @"??_ECObject@@UEAAPEAXI@Z"], section "llvm.metadata"
@llvm.compiler.used = appending global [2 x ptr] [ptr @__mib_sized_ref, ptr @__mib_sized_kept], section "llvm.metadata"

@"??_ECObject@@UEAAPEAXI@Z" = weak dso_local unnamed_addr alias { ptr, i64 } (ptr, i32), ptr @"??_GCObject@@UEAAPEAXI@Z"

define dllexport ptr @"?fg_Make@@YAPEAUCObject@@PEAX@Z"(ptr %memory) {
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

;--- site.ll
target datalayout = "e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-windows-msvc"

@"??0CObject@@QEAA@XZ.mib_sized" = external constant i8
@__mib_sized_ref = private constant [2 x ptr] [ptr @"??0CObject@@QEAA@XZ", ptr @"??0CObject@@QEAA@XZ.mib_sized"], section ".rdata$mibszf"
@llvm.compiler.used = appending global [1 x ptr] [ptr @__mib_sized_ref], section "llvm.metadata"

declare ptr @"??0CObject@@QEAA@XZ"(ptr)

define dllexport ptr @"?fg_Make@@YAPEAUCObject@@PEAX@Z"(ptr %memory) {
  %object = call ptr @"??0CObject@@QEAA@XZ"(ptr %memory)
  ret ptr %object
}

;--- class.ll
target datalayout = "e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-windows-msvc"

$"??0CObject@@QEAA@XZ" = comdat any
$"??_GCObject@@UEAAPEAXI@Z" = comdat any
$"??_7CObject@@6B@" = comdat any

@"??_7CObject@@6B@" = linkonce_odr unnamed_addr constant { [1 x ptr] } { [1 x ptr] [ptr @"??_ECObject@@UEAAPEAXI@Z"] }, comdat, !malterlib.sized.deps !0 #0
@__mib_sized_kept = internal constant [2 x ptr] [ptr @"??_7CObject@@6B@", ptr @"??_ECObject@@UEAAPEAXI@Z"], section "llvm.metadata"
@llvm.compiler.used = appending global [1 x ptr] [ptr @__mib_sized_kept], section "llvm.metadata"

@"??_ECObject@@UEAAPEAXI@Z" = weak dso_local unnamed_addr alias { ptr, i64 } (ptr, i32), ptr @"??_GCObject@@UEAAPEAXI@Z"

define dllexport void @"?fg_Construct@@YAXPEAX@Z"(ptr %memory) {
  %object = call ptr @"??0CObject@@QEAA@XZ"(ptr %memory)
  ret void
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
attributes #1 = { "malterlib-sized" }
attributes #2 = { noinline "malterlib-sized" "malterlib-sized-aliases"="??_ECObject@@UEAAPEAXI@Z=0" }

!0 = !{i64 0, ptr @"??_ECObject@@UEAAPEAXI@Z", null}
!1 = !{i64 0, ptr @"??_7CObject@@6B@", null}
