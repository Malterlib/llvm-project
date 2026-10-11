// RUN: %clang_cc1 -std=c++20 -triple x86_64-linux-gnu -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -std=c++20 -triple arm64-apple-macosx13 -emit-llvm -o - %s | FileCheck %s

// A chain of diamonds, each two classes with a virtual base of the one
// before, reaches each virtual base through each class of each diamond above
// it. The vbase offsets and the vtables of the virtual bases of the vtable of
// the last one, and the flags of its type info, are found in time linear in
// the length of the chain, not exponential, and are the same.

template <int N> struct CLevel;
template <int N, int Side> struct CBranch : virtual CLevel<N - 1> {};
template <int N> struct CLevel : CBranch<N, 0>, CBranch<N, 1> {};
template <> struct CLevel<0> {
  virtual void f();
};

struct CTop : CLevel<40> {
  void f() override;
};
void CTop::f() {}

// CHECK: @_ZTV4CTop = {{.*}}constant
// The second walk of the bases of each virtual base gives the non-diamond
// repeat flag for its non-virtual bases, as the walk of each path did.
// CHECK: @_ZTI6CLevelILi40EE = linkonce_odr {{.*}}constant {{.*}} i32 3, i32 2,

// And the search of whether a nearly empty virtual base of a class has storage
// of its own, which the bases of each class of a chain, each with a primary
// base of its own, are searched for.
template <int N> struct CStorage;
template <int N> struct CPrimary {
  virtual void g();
};
template <int N> struct CStorageLeft : CPrimary<N>, virtual CStorage<N - 1> {};
template <int N> struct CStorageRight : CPrimary<N>, virtual CStorage<N - 1> {};
template <int N> struct CStorage : CStorageLeft<N>, CStorageRight<N> {};
template <> struct CStorage<0> {
  virtual void f();
};
struct CStorageFinal : CStorage<40> {
  int x;
};
// CHECK-DAG: define {{.*}} @_Z10fg_StorageP13CStorageFinal(
int fg_Storage(CStorageFinal *p) { return p->x; }
