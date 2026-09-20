// RUN: %clang_cc1 -std=c++20 -triple arm64-apple-macosx13 -fmalterlib-sized-destructors -O2 -flto=thin -disable-llvm-passes -emit-llvm -o - %s | FileCheck %s --implicit-check-not='!{ptr @_ZTV1B,' --implicit-check-not='!{ptr @_ZTV1C,' --implicit-check-not='!{ptr @_ZTV1D,' --implicit-check-not='!{ptr @_ZTV1G,'

// An array filler whose delegating constructors lead to one that isn't
// inlined, also with link-time optimization, `__attribute__((noinline))`,
// anywhere in the chain, depends on the constructors only, the one that isn't
// inlined installing the vtable itself, not on the vtable of this module that
// an ordinary construction installs.
using Size = decltype(sizeof(0));
inline void *operator new(Size, void *p) noexcept { return p; }
inline void *operator new[](Size, void *p) noexcept { return p; }
// In the middle of the chain, and at its start.
struct __attribute__((visibility("hidden"))) B {
  B() : B(0) {}
  __attribute__((visibility("default"), noinline)) B(int) : B(0L) {}
  B(long) {}
  virtual ~B();
};
B *ordinaryB(void *p) { return new (p) B(0L); }
[[malterlib::sized_construction]] B *siteB(void *p, int n) { return new (p) B[n]{}; }

struct __attribute__((visibility("hidden"))) C {
  __attribute__((visibility("default"), noinline)) C() : C(0L) {}
  C(long) {}
  virtual ~C();
};
C *ordinaryC(void *p) { return new (p) C(0L); }
[[malterlib::sized_construction]] C *siteC(void *p, int n) { return new (p) C[n]{}; }

// Also through a delegating constructor template, whose specialization is
// instantiated only at the end of the module.
template <int N> struct Tag {};
struct __attribute__((visibility("hidden"))) D {
  __attribute__((visibility("default"), noinline)) D(Tag<0>);
  template <int N> D(Tag<N>) : D(Tag<0>{}) {}
  D() : D(Tag<1>{}) {}
  D(long) {}
  virtual ~D();
};
D *ordinaryD(void *p) { return new (p) D(0L); }
[[malterlib::sized_construction]] D *siteD(void *p, int n) { return new (p) D[n]{}; }

// However long the chain is.
template <unsigned N> struct GTag {};
struct __attribute__((visibility("hidden"))) G {
  __attribute__((visibility("default"), noinline)) G(GTag<0>);
  template <unsigned N> G(GTag<N>) : G(GTag<N - 1>{}) {}
  G() : G(GTag<20>{}) {}
  G(long) {}
  virtual ~G();
};
G *ordinaryG(void *p) { return new (p) G(0L); }
[[malterlib::sized_construction]] G *siteG(void *p, int n) { return new (p) G[n]{}; }

// But not where one in the chain can be inlined.
struct __attribute__((visibility("hidden"))) E {
  E() : E(0) {}
  E(int) : E(0L) {}
  E(long) {}
  virtual ~E();
};
E *ordinaryE(void *p) { return new (p) E(0L); }
[[malterlib::sized_construction]] E *siteE(void *p, int n) { return new (p) E[n]{}; }
template <int N> struct F {
  F() : F(Tag<1>{}) {}
  template <int M> F(Tag<M>) : F(0L) {}
  F(long) {}
  virtual ~F();
};
F<0> *ordinaryF(void *p) { return new (p) F<0>(0L); }
[[malterlib::sized_construction]] F<0> *siteF(void *p, int n) { return new (p) F<0>[n]{}; }
// CHECK: !malterlib.sized.inlinable =
// CHECK-DAG: !{ptr @_ZTV1E, !"_ZTV1E"}
// CHECK-DAG: !{ptr @_ZTV1FILi0EE, !"_ZTV1FILi0EE"}
