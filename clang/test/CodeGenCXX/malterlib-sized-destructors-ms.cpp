// RUN: %clang_cc1 -std=c++20 -triple x86_64-pc-windows-msvc -fmalterlib-sized-destructors -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -std=c++20 -triple aarch64-pc-windows-msvc -fmalterlib-sized-destructors -emit-llvm -o - %s | FileCheck %s

// CHECK: %struct.__malterlib_destroy_result = type { ptr, i64 }

struct Simple {
  virtual ~Simple();
  int i;
};
Simple::~Simple() {}

// The complete-object destructor is unchanged.
//
// CHECK-LABEL: define{{.*}} void @"??1Simple@@UEAA@XZ"(

Simple *make() { return new Simple; }

unsigned long destroy(Simple *p, void **memory) {
  return __builtin_malterlib_destroy(p, memory);
}

// The builtin calls the deleting destructor with flags 0, which destroys the
// object without freeing it, and reads the result.
//
// CHECK-LABEL: define{{.*}} @"?destroy@@YAKPEAUSimple@@PEAPEAX@Z"(
// CHECK: %[[FN:.*]] = load ptr, ptr %vfn
// CHECK: call %struct.__malterlib_destroy_result %[[FN]](ptr noundef %{{.*}}, i32 noundef 0) #[[DESTROY:[0-9]+]]
// CHECK: load ptr, ptr %__memory
// CHECK: load i64, ptr %__size

void plainDelete(Simple *p) { delete p; }

// A delete expression still passes flags 1.
//
// CHECK-LABEL: define{{.*}} void @"?plainDelete@@YAXPEAUSimple@@@Z"(
// CHECK: call %struct.__malterlib_destroy_result %{{.*}}(ptr noundef %{{.*}}, i32 noundef 1)

void explicitDestructorCall(Simple *p) { p->~Simple(); }

// So does a virtual destructor call, with flags 0.
//
// CHECK-LABEL: define{{.*}} void @"?explicitDestructorCall@@YAXPEAUSimple@@@Z"(
// CHECK: call %struct.__malterlib_destroy_result %{{.*}}(ptr noundef %{{.*}}, i32 noundef 0)

// The deleting destructor keeps its flags parameter and returns 'this' as the
// first half of its result, which every caller that predates
// -fmalterlib-sized-destructors reads as before.
//
// CHECK-LABEL: define{{.*}} %struct.__malterlib_destroy_result @"??_GSimple@@UEAAPEAXI@Z"(ptr noundef %this, i32 noundef %should_call_delete)
// CHECK: store ptr %{{.*}}, ptr %__memory
// CHECK: store i64 16, ptr %__size
// CHECK: and i32 %{{.*}}, 1
// CHECK: br i1 %{{.*}}, label %dtor.continue, label %dtor.call_delete
// CHECK: ret %struct.__malterlib_destroy_result

// The builtin's call stays a call: the linker checks the destructor's
// definition, which inlining after devirtualization would take out of the link.
//
// CHECK: attributes #[[DESTROY]] = { noinline{{.*}} }
