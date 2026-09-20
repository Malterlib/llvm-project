// RUN: %clang_cc1 -std=c++20 -triple x86_64-linux-gnu -fno-rtti -fmalterlib-sized-destructors -O2 -emit-llvm -o - %s | FileCheck %s --implicit-check-not=_ZN1AC1Ei --implicit-check-not=_ZN1AC2Ei --implicit-check-not=_Z5valuei

// A construction site that constructs with an immediate constructor,
// `consteval A(int)`, which the code never calls, depends on the vtables
// the value constant evaluation made installs, not on the constructor, which
// isn't emitted, nor what it calls, like an immediate helper.
using Size = decltype(sizeof(0));
inline void *operator new(Size, void *p) noexcept { return p; }
consteval int value(int x) { return x + 1; }
struct A {
  consteval A(int x) : x(value(x)) {}
  constexpr virtual ~A() {}
  int x;
};
extern "C" [[malterlib::sized_construction]] A *make(void *p) {
  return new (p) A(1);
}
// CHECK: @__mib_sized_ref = private constant [2 x ptr] [ptr @_ZTV1A, ptr @_ZTV1A.mib_sized]
