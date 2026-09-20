// RUN: %clang_cc1 -std=c++20 -triple x86_64-linux-gnu -fno-rtti -fmalterlib-sized-destructors -O2 -flto -emit-llvm -o - %s | FileCheck %s

// A constructor that isn't inlined only here, where `#pragma clang optimize
// off` makes it `optnone`, isn't one that is never inlined: link-time
// optimization can choose the definition of another module, which it can
// inline, which installs the vtable this module has, so the site depends on
// what the constructor's body depends on.
using Size = decltype(sizeof(0));
inline void *operator new(Size, void *p) noexcept { return p; }
#pragma clang optimize off
template <class T> struct A {
  A(int x) : value(x) {}
  virtual ~A() {}
  T value;
};
#pragma clang optimize on
extern "C" [[malterlib::sized_construction]] A<int> *make(void *p, bool b) {
  return new (p) A<int>(b ? A<int>(1) : A<int>(2));
}
// CHECK: !malterlib.sized.inlinable = !{
// CHECK-DAG: !{ptr @_ZN1AIiEC1Ei, !"_ZN1AIiEC1Ei"}
// CHECK-DAG: !{ptr @_ZTV1AIiE, !"_ZTV1AIiE"}
