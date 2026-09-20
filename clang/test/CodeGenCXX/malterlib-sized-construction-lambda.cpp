// RUN: %clang_cc1 -std=c++23 -triple x86_64-pc-windows-msvc -fms-extensions -fmalterlib-sized-destructors -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -std=c++23 -triple x86_64-pc-windows-msvc -fms-extensions -fmalterlib-sized-destructors -emit-llvm -o - -DORDINARY %s | FileCheck %s --check-prefix=ORDINARY

// A lambda with the attribute is a construction site, also in a function
// without it, and one without it is not, also in a function with it.
using Size = decltype(sizeof(0));
void *operator new(Size, void *) noexcept;
struct A {
  A();
  virtual ~A();
};
A produce();
#ifndef ORDINARY
extern "C" A *make(void *p) {
  auto factory = [] [[malterlib::sized_construction]] (void *q) {
    return new (q) A;
  };
  return factory(p);
}
// CHECK: @__mib_sized_ref = private constant [2 x ptr] [ptr @"??0A@@QEAA@XZ", ptr @"??0A@@QEAA@XZ.mib_sized"]
// The site is marked as one, which the backend keeps the references of
// wherever its code is emitted.
// CHECK: define {{.*}} @"??R<lambda_0>@?0??make@@9@QEBA?A?<auto>@@PEAX@Z"({{.*}}) {{.*}}#[[SITE:[0-9]+]]
// CHECK: attributes #[[SITE]] = {{{.*}}"malterlib-sized-site"
#else
extern "C" [[malterlib::sized_construction]] void *makeOrdinary(void *p) {
  auto factory = [](void *q) { return new (q) A(produce()); };
  return factory(p);
}
// ORDINARY-NOT: @__mib_sized_ref
#endif
