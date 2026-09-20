// RUN: %clang_cc1 -std=c++20 -triple arm64-apple-macosx -fmalterlib-sized-destructors -debug-info-kind=limited -emit-llvm -o - %s | FileCheck %s

struct Simple {
  virtual ~Simple();
  int i;
};
Simple::~Simple() {}

// A debugger finds 'this' through its declared location, so the deleting
// destructor stores the address of the object there rather than the value it
// was called with, which carries the mode in bit 0.
//
// CHECK-LABEL: define{{.*}} %struct.__malterlib_destroy_result @_ZN6SimpleD0Ev
// CHECK: #dbg_declare(ptr %this.addr, ![[VAR:[0-9]+]], !DIExpression()
// CHECK: %[[THIS:.*]] = call ptr @llvm.ptrmask.p0.i64(ptr %{{.*}}, i64 -2)
// CHECK: store ptr %[[THIS]], ptr %this.addr
// CHECK: ![[VAR]] = !DILocalVariable(name: "this", arg: 1
