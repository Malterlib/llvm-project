; Tests that an "inalloca" argument-memory alloca is never moved into the
; coroutine frame, even when its address escapes into memory (as happens with
; the -O0 mirror store). The inalloca ABI (i386-windows-msvc) requires the
; block to be at the top of the stack when the call executes.
; RUN: opt < %s -passes='cgscc(coro-split)' -S | FileCheck %s

target datalayout = "e-m:x-p:32:32-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:32-n8:16:32-a:0:32-S32"
target triple = "i386-pc-windows-msvc"

%struct.NonTrivial = type { ptr }

define ptr @f() presplitcoroutine {
entry:
  %argmem.mirror = alloca ptr, align 4
  %id = call token @llvm.coro.id(i32 0, ptr null, ptr null, ptr null)
  %size = call i32 @llvm.coro.size.i32()
  %alloc = call ptr @malloc(i32 %size)
  %hdl = call ptr @llvm.coro.begin(token %id, ptr %alloc)
  %ss = call ptr @llvm.stacksave()
  %argmem = alloca inalloca <{ %struct.NonTrivial }>, align 4
  ; The -O0 style escape: the block pointer is stored into a local mirror.
  store ptr %argmem, ptr %argmem.mirror, align 4
  %argmem.reload = load ptr, ptr %argmem.mirror, align 4
  %arg = getelementptr inbounds <{ %struct.NonTrivial }>, ptr %argmem.reload, i32 0, i32 0
  call x86_thiscallcc void @NonTrivial.ctor(ptr %arg)
  call void @takes_by_value(ptr inalloca(<{ %struct.NonTrivial }>) %argmem)
  call void @llvm.stackrestore(ptr %ss)
  %sp1 = call i8 @llvm.coro.suspend(token none, i1 false)
  switch i8 %sp1, label %suspend [i8 0, label %resume
                                  i8 1, label %cleanup]
resume:
  br label %cleanup

cleanup:
  %mem = call ptr @llvm.coro.free(token %id, ptr %hdl)
  call void @free(ptr %mem)
  br label %suspend

suspend:
  call void @llvm.coro.end(ptr %hdl, i1 0, token none)
  ret ptr %hdl
}

; The inalloca block must stay a stack alloca in the ramp function and must
; not get a slot in the frame.
; CHECK-NOT:   %f.Frame = type {{.*}}<{ %struct.NonTrivial }>
; CHECK-LABEL: define ptr @f()
; CHECK:         %argmem = alloca inalloca <{ %struct.NonTrivial }>, align 4
; CHECK:         call void @takes_by_value(ptr inalloca(<{ %struct.NonTrivial }>) %argmem){{$}}

declare ptr @llvm.coro.free(token, ptr)
declare i32 @llvm.coro.size.i32()
declare i8  @llvm.coro.suspend(token, i1)
declare token @llvm.coro.id(i32, ptr, ptr, ptr)
declare ptr @llvm.coro.begin(token, ptr)
declare void @llvm.coro.end(ptr, i1, token)
declare ptr @llvm.stacksave()
declare void @llvm.stackrestore(ptr)

declare x86_thiscallcc void @NonTrivial.ctor(ptr)
declare void @takes_by_value(ptr inalloca(<{ %struct.NonTrivial }>))
declare noalias ptr @malloc(i32)
declare void @free(ptr)
