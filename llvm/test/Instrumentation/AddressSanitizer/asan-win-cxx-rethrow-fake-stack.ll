; RUN: opt < %s -passes=asan -asan-use-stack-safety=0 -S | FileCheck %s

; Regular Windows C++ EH functions should still use ASan fake-stack UAR
; instrumentation, but functions where an exception can be dispatched while
; control is inside a funclet (bare rethrow, or a throwing call made from a
; catch handler) cannot use the fake stack.

target triple = "x86_64-pc-windows-msvc"

@x = global ptr null
@_TI1H = external global i8

define i32 @regular_throw() sanitize_address personality ptr @__CxxFrameHandler3 {
; CHECK-LABEL: define i32 @regular_throw(
; CHECK: call i64 @__asan_stack_malloc_
entry:
  %tmp = alloca i32, align 4
  %stack_buffer = alloca [42 x i8], align 1
  %p = getelementptr inbounds i8, ptr %stack_buffer, i64 13
  store ptr %p, ptr @x, align 8
  store i32 1, ptr %tmp, align 4
  invoke void @_CxxThrowException(ptr %tmp, ptr @_TI1H)
          to label %unreachable unwind label %catch.dispatch

catch.dispatch:
  %0 = catchswitch within none [label %catch] unwind to caller

catch:
  %1 = catchpad within %0 [ptr null, i32 64, ptr null]
  catchret from %1 to label %return

return:
  ret i32 0

unreachable:
  unreachable
}

define i32 @rethrow() sanitize_address personality ptr @__CxxFrameHandler3 {
; CHECK-LABEL: define i32 @rethrow(
; CHECK-NOT: __asan_stack_malloc
; CHECK: invoke void @_CxxThrowException(ptr null, ptr null)
entry:
  %tmp = alloca i32, align 4
  %stack_buffer = alloca [42 x i8], align 1
  %p = getelementptr inbounds i8, ptr %stack_buffer, i64 13
  store ptr %p, ptr @x, align 8
  store i32 1, ptr %tmp, align 4
  invoke void @_CxxThrowException(ptr %tmp, ptr @_TI1H)
          to label %unreachable unwind label %catch.dispatch

catch.dispatch:
  %0 = catchswitch within none [label %catch] unwind label %catch.dispatch1

catch:
  %1 = catchpad within %0 [ptr null, i32 64, ptr null]
  invoke void @_CxxThrowException(ptr null, ptr null) [ "funclet"(token %1) ]
          to label %unreachable unwind label %catch.dispatch1

catch.dispatch1:
  %2 = catchswitch within none [label %catch2] unwind to caller

catch2:
  %3 = catchpad within %2 [ptr null, i32 64, ptr null]
  catchret from %3 to label %return

return:
  ret i32 0

unreachable:
  unreachable
}

define i32 @throw_from_catch_call() sanitize_address personality ptr @__CxxFrameHandler3 {
; CHECK-LABEL: define i32 @throw_from_catch_call(
; CHECK-NOT: __asan_stack_malloc
; CHECK: call void @may_throw()
entry:
  %tmp = alloca i32, align 4
  %stack_buffer = alloca [42 x i8], align 1
  %p = getelementptr inbounds i8, ptr %stack_buffer, i64 13
  store ptr %p, ptr @x, align 8
  store i32 1, ptr %tmp, align 4
  invoke void @_CxxThrowException(ptr %tmp, ptr @_TI1H)
          to label %unreachable unwind label %catch.dispatch

catch.dispatch:
  %0 = catchswitch within none [label %catch] unwind to caller

catch:
  %1 = catchpad within %0 [ptr null, i32 64, ptr null]
  call void @may_throw() [ "funclet"(token %1) ]
  catchret from %1 to label %return

return:
  ret i32 0

unreachable:
  unreachable
}

define i32 @nounwind_call_in_catch() sanitize_address personality ptr @__CxxFrameHandler3 {
; CHECK-LABEL: define i32 @nounwind_call_in_catch(
; CHECK: call i64 @__asan_stack_malloc_
entry:
  %tmp = alloca i32, align 4
  %stack_buffer = alloca [42 x i8], align 1
  %p = getelementptr inbounds i8, ptr %stack_buffer, i64 13
  store ptr %p, ptr @x, align 8
  store i32 1, ptr %tmp, align 4
  invoke void @_CxxThrowException(ptr %tmp, ptr @_TI1H)
          to label %unreachable unwind label %catch.dispatch

catch.dispatch:
  %0 = catchswitch within none [label %catch] unwind to caller

catch:
  %1 = catchpad within %0 [ptr null, i32 64, ptr null]
  call void @no_throw() [ "funclet"(token %1) ]
  catchret from %1 to label %return

return:
  ret i32 0

unreachable:
  unreachable
}

define i32 @throw_from_cleanup_call() sanitize_address personality ptr @__CxxFrameHandler3 {
; A may-throw call in a cleanup funclet outside any catch handler does not
; make the EH runtime resolve parent-frame state, so the fake stack stays.
; CHECK-LABEL: define i32 @throw_from_cleanup_call(
; CHECK: call i64 @__asan_stack_malloc_
entry:
  %tmp = alloca i32, align 4
  %stack_buffer = alloca [42 x i8], align 1
  %p = getelementptr inbounds i8, ptr %stack_buffer, i64 13
  store ptr %p, ptr @x, align 8
  store i32 1, ptr %tmp, align 4
  invoke void @_CxxThrowException(ptr %tmp, ptr @_TI1H)
          to label %unreachable unwind label %cleanup

cleanup:
  %0 = cleanuppad within none []
  call void @may_throw() [ "funclet"(token %0) ]
  cleanupret from %0 unwind to caller

unreachable:
  unreachable
}

define i32 @throw_from_cleanup_in_catch() sanitize_address personality ptr @__CxxFrameHandler3 {
; A may-throw call in a cleanup pad nested inside a catch handler still runs
; with control inside the catch funclet, so the fake stack must be disabled.
; CHECK-LABEL: define i32 @throw_from_cleanup_in_catch(
; CHECK-NOT: __asan_stack_malloc
; CHECK: call void @may_throw()
entry:
  %tmp = alloca i32, align 4
  %stack_buffer = alloca [42 x i8], align 1
  %p = getelementptr inbounds i8, ptr %stack_buffer, i64 13
  store ptr %p, ptr @x, align 8
  store i32 1, ptr %tmp, align 4
  invoke void @_CxxThrowException(ptr %tmp, ptr @_TI1H)
          to label %unreachable unwind label %catch.dispatch

catch.dispatch:
  %0 = catchswitch within none [label %catch] unwind to caller

catch:
  %1 = catchpad within %0 [ptr null, i32 64, ptr null]
  invoke void @no_throw() [ "funclet"(token %1) ]
          to label %catch.cont unwind label %cleanup

catch.cont:
  catchret from %1 to label %return

cleanup:
  %2 = cleanuppad within %1 []
  call void @may_throw() [ "funclet"(token %2) ]
  cleanupret from %2 unwind to caller

return:
  ret i32 0

unreachable:
  unreachable
}

declare i32 @__CxxFrameHandler3(...)
declare void @_CxxThrowException(ptr, ptr) noreturn
declare void @may_throw()
declare void @no_throw() nounwind
