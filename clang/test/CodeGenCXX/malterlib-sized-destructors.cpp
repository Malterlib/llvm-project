// RUN: %clang_cc1 -std=c++20 -triple aarch64-apple-macosx -fmalterlib-sized-destructors -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -std=c++20 -triple x86_64-linux-gnu -fmalterlib-sized-destructors -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -std=c++20 -triple i386-linux-gnu -fmalterlib-sized-destructors -emit-llvm -o - %s | FileCheck %s --check-prefixes=I386,REGPARM0
// RUN: %clang_cc1 -std=c++20 -triple i386-linux-gnu -mregparm 1 -fmalterlib-sized-destructors -emit-llvm -o - %s | FileCheck %s --check-prefixes=I386,REGPARM1
// RUN: %clang_cc1 -std=c++20 -triple aarch64-apple-macosx -DOFF -emit-llvm -o - %s | FileCheck %s --check-prefix=OFF

// CHECK: %struct.__malterlib_destroy_result = type { ptr, i64 }
// I386: %struct.__malterlib_destroy_result = type { ptr, i32 }
//
// The result is returned in registers where the C ABI would return the struct
// in memory, and 'this' is passed as a deleting destructor compiled without
// the flag receives it: in the register -mregparm hands it, which a hidden
// return pointer would otherwise take.
//
// REGPARM0: define{{.*}} %struct.__malterlib_destroy_result @_ZN6SimpleD0Ev(ptr noundef %this)
// REGPARM1: define{{.*}} %struct.__malterlib_destroy_result @_ZN6SimpleD0Ev(ptr inreg noundef %this)

struct Simple {
  virtual ~Simple();
  int i;
};
Simple::~Simple() {}

// The complete-object and base-object destructors are unchanged.
//
// CHECK-LABEL: define{{.*}} @_ZN6SimpleD2Ev(
// CHECK-LABEL: define{{.*}} @_ZN6SimpleD1Ev(

// The deleting destructor strips the mode from 'this', keeps the object's
// address and size in its result, and only frees the object when the caller
// did not ask for the sized mode.
//
// CHECK-LABEL: define{{.*}} %struct.__malterlib_destroy_result @_ZN6SimpleD0Ev(ptr noundef %this)
// CHECK: %[[INT:.*]] = ptrtoint ptr %{{.*}} to i64
// CHECK: %dtor.sized = and i64 %[[INT]], 1
// CHECK: %[[THIS:.*]] = call ptr @llvm.ptrmask.p0.i64(ptr %{{.*}}, i64 -2)
// CHECK: %dtor.should_delete = xor i64 %dtor.sized, 1
// CHECK: store ptr %[[THIS]], ptr %__memory
// CHECK: store i64 16, ptr %__size
// CHECK: call{{.*}} @_ZN6SimpleD1Ev(ptr noundef nonnull align 8 {{.*}} %[[THIS]])
// CHECK: and i64 %dtor.should_delete, 1
// CHECK: br i1 %{{.*}}, label %dtor.continue, label %dtor.call_delete
// CHECK: dtor.call_delete:
// CHECK: call void @_ZdlPvm(ptr noundef %[[THIS]], i64 noundef 16)
// CHECK: dtor.continue:
// CHECK: ret %struct.__malterlib_destroy_result

// Without the flag the deleting destructor is unchanged.
//
// OFF-LABEL: define{{.*}} void @_ZN6SimpleD0Ev(ptr noundef nonnull align 8
// OFF-NOT: ptrmask
// OFF: ret void

struct Base2 {
  virtual ~Base2();
  virtual void f();
  long b;
};
struct Multi : Simple, Base2 {
  ~Multi() override;
  long m;
};
Multi::~Multi() {}

// A this-adjusting thunk passes the mode on: the offset it adds is a multiple
// of the alignment of a polymorphic base.
//
// CHECK-LABEL: define{{.*}} %struct.__malterlib_destroy_result @_ZThn16_N5MultiD0Ev(ptr noundef %this)
// CHECK-NOT: ptrmask
// CHECK: %[[ADJ:.*]] = getelementptr inbounds i8, ptr %{{.*}}, i64 -16
// CHECK: call %struct.__malterlib_destroy_result @_ZN5MultiD0Ev(ptr noundef %[[ADJ]])
// CHECK: ret %struct.__malterlib_destroy_result

struct VBase {
  virtual ~VBase();
  long v;
};
struct VDerived : virtual VBase {
  ~VDerived() override;
  long d;
};
VDerived::~VDerived() {}

// A virtual thunk reads the vptr through 'this', so it strips the mode before
// the adjustment and applies it again afterwards.
//
// CHECK-LABEL: define{{.*}} %struct.__malterlib_destroy_result @_ZTv0_n24_N8VDerivedD0Ev(ptr noundef %this)
// CHECK: %dtor.sized = and i64 %{{.*}}, 1
// CHECK: %[[UNTAGGED:.*]] = call ptr @llvm.ptrmask.p0.i64(ptr %{{.*}}, i64 -2)
// CHECK: %vtable = load ptr, ptr %[[UNTAGGED]]
// CHECK: %[[OFFSET:.*]] = load i64
// CHECK: %[[ADJUSTED:.*]] = getelementptr inbounds i8, ptr %[[UNTAGGED]], i64 %[[OFFSET]]
// CHECK: %this.tagged = getelementptr i8, ptr %[[ADJUSTED]], i64 %dtor.sized
// CHECK: call %struct.__malterlib_destroy_result @_ZN8VDerivedD0Ev(ptr noundef %this.tagged)

#ifndef OFF
unsigned long destroy(Simple *p, void **memory) {
  return __builtin_malterlib_destroy(p, memory);
}
#endif

// The builtin selects the sized mode with bit 0 of 'this' and loads the vtable
// through the object's real address.
//
// CHECK-LABEL: define{{.*}} @_Z7destroyP6SimplePPv(
// CHECK: %[[P:.*]] = load ptr, ptr %p.addr
// CHECK: %this.sized = getelementptr inbounds i8, ptr %[[P]], i64 1
// CHECK: %vtable = load ptr, ptr %[[P]]
// CHECK: %[[FN:.*]] = load ptr, ptr %vfn
// CHECK: %[[RESULT:.*]] = call %struct.__malterlib_destroy_result %[[FN]](ptr noundef %this.sized) #[[DESTROY:[0-9]+]]
// CHECK: extractvalue %struct.__malterlib_destroy_result %[[RESULT]], 0
// CHECK: extractvalue %struct.__malterlib_destroy_result %[[RESULT]], 1

void plainDelete(Simple *p) { delete p; }

// A delete expression is unchanged: it passes the object's address as it is.
//
// CHECK-LABEL: define{{.*}} void @_Z11plainDeleteP6Simple(
// CHECK-NOT: getelementptr inbounds i8, ptr %{{.*}}, i64 1
// CHECK: call %struct.__malterlib_destroy_result %{{.*}}(ptr noundef %{{.*}})

// The builtin's call stays a call: the linker checks the destructor's
// definition, which inlining after devirtualization would take out of the link.
//
// CHECK: attributes #[[DESTROY]] = { noinline{{.*}} }
