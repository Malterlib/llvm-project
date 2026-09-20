// RUN: %clang_cc1 -std=c++20 -triple arm64-apple-macosx13 -fmalterlib-sized-destructors -O2 -flto=thin -disable-llvm-passes -emit-llvm -o - %s | FileCheck %s

// An array filler whose constructor isn't inlined, also with link-time
// optimization, `__attribute__((noinline))`, depends on that constructor only,
// which installs the vtable itself, not on the vtable of this module that an
// ordinary construction installs.
using Size = decltype(sizeof(0));
inline void *operator new(Size, void *p) noexcept { return p; }
inline void *operator new[](Size, void *p) noexcept { return p; }
struct __attribute__((visibility("hidden"))) A {
  __attribute__((visibility("default"), noinline)) A();
  A(int) {}
  virtual ~A();
  int value;
};
A *ordinary(void *p) { return new (p) A(1); }
[[malterlib::sized_construction]] A *site(void *p, int n) { return new (p) A[n]{}; }
// CHECK: @__mib_sized_ref = private constant [2 x ptr] [ptr @_ZN1AC1Ev, ptr @_ZN1AC1Ev.mib_sized]
// CHECK: !malterlib.sized.inlinable = !{![[DELETING:[0-9]+]]}
// CHECK: ![[DELETING]] = !{ptr @_ZN1AD0Ev, !"_ZN1AD0Ev"}
