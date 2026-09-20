// RUN: %clang_cc1 -std=c++20 -triple arm64-apple-macosx13 -fmalterlib-sized-destructors -O2 -flto=thin -disable-llvm-passes -emit-llvm -o - %s | FileCheck %s

// The deleting destructor in a slot of a vtable another module defines is
// listed only while what is emitted calls it, also where link-time
// optimization can import its body: what installs the vtable is checked for
// it, and one another image defines, also hidden there, can't be imported or
// checked.
using Size = decltype(sizeof(0));
inline void *operator new(Size, void *p) noexcept { return p; }
struct A {
  A();
  __attribute__((visibility("hidden"))) virtual ~A();
};
[[malterlib::sized_construction]] A *make(void *p) { return new (p) A; }
Size destroy(A *p, void **memory) { return __builtin_malterlib_destroy(p, memory); }
// CHECK: @__mib_sized_ref = private constant [2 x ptr] [ptr @_ZN1AC1Ev, ptr @_ZN1AC1Ev.mib_sized]
// CHECK: !malterlib.sized.inlinable = !{![[DELETING:[0-9]+]]}
// CHECK: ![[DELETING]] = !{ptr @_ZN1AD0Ev, !"_ZN1AD0Ev"}
