// RUN: %clang_cc1 -std=c++20 -triple x86_64-pc-windows-msvc -fms-extensions -fmalterlib-sized-destructors -O2 -disable-llvm-passes -emit-llvm -o - -DCHAIN %s | FileCheck %s --check-prefix=CHAIN
// RUN: %clang_cc1 -std=c++20 -triple x86_64-pc-windows-msvc -fms-extensions -fmalterlib-sized-destructors -O2 -flto=thin -disable-llvm-passes -emit-llvm -o - -DOWNER %s | FileCheck %s --check-prefix=OWNER
// RUN: %clang_cc1 -std=c++20 -triple arm64-apple-macos13 -fmalterlib-sized-destructors -O2 -disable-llvm-passes -emit-llvm -o - -DSITE %s | FileCheck %s --check-prefix=SITE
// RUN: %clang_cc1 -std=c++20 -triple x86_64-windows-gnu -fmalterlib-sized-destructors -O2 -mconstructor-aliases -disable-llvm-passes -emit-llvm -o - -DALIAS %s | FileCheck %s --check-prefix=ALIAS
// RUN: %clang_cc1 -std=c++20 -triple x86_64-pc-windows-msvc -fms-extensions -fmalterlib-sized-destructors -O2 -disable-llvm-passes -emit-llvm -o - -DFILLER %s | FileCheck %s --check-prefix=FILLER
// RUN: %clang_cc1 -std=c++20 -triple x86_64-pc-windows-msvc -fms-extensions -fmalterlib-sized-destructors -O2 -disable-llvm-passes -emit-llvm -o - -DINHERITED %s | FileCheck %s --check-prefix=INHERITED
// RUN: %clang_cc1 -std=c++20 -triple x86_64-pc-windows-msvc -fms-extensions -fmalterlib-sized-destructors -O2 -disable-llvm-passes -emit-llvm -o - -DLONGCHAIN %s | FileCheck %s --check-prefix=LONGCHAIN

using Size = decltype(sizeof(0));
void *operator new(Size, void *) noexcept;

#if defined(CHAIN)
// A delegating constructor depends on the vtable of its image only where the
// chain of those it delegates to ends with one this module defines, not with
// a dllimport one, which installs that of its own image: on that one, which
// it calls where it inlined the one it delegates to.
struct A {
  A();
  A(int) : A(0L) {}
  __declspec(dllimport) A(long);
  A(short) {}
  virtual ~A() {}
  int x;
};
A::A() : A(0) {}
// CHAIN: define {{.*}} @"??0A@@QEAA@XZ"({{.*}} !malterlib.sized.deps ![[DEPS:[0-9]+]]
// CHAIN: ![[DEPS]] = !{i64 0, ptr @"??0A@@QEAA@H@Z", null, ptr @"??0A@@QEAA@J@Z", !"malterlib.sized.may-go"}
#elif defined(OWNER)
// With link-time optimization, what an owner depends on through a function
// it only has to inline that this module only declares stays required: its
// body can be imported and inlined, with what it installs, which this module
// doesn't know.
template <class T> struct A {
  A();
  A(int) : A(0L) {}
  A(long);
  virtual ~A() {}
  int x;
};
extern template A<int>::A();
extern template A<int>::A(int);
extern template A<int>::A(long);
template <class T> A<T>::A() : A(0) {}
template A<int>::A();
// OWNER: !{i64 0, ptr @"??0?$A@H@@QEAA@H@Z", !"malterlib.sized.may-go", ptr @"??0?$A@H@@QEAA@J@Z", null}
#elif defined(SITE)
// A site whose vtable another module defines lists the deleting destructor it
// holds, which link-time optimization can call directly, devirtualized.
template <class T> struct Box {
  Box() {}
  virtual ~Box() {}
  T value;
};
extern template struct Box<int>;
[[malterlib::sized_construction]] Size probe(void *_pStorage) {
  auto *p = new (_pStorage) Box<int>;
  void *pMemory;
  return __builtin_malterlib_destroy(p, &pMemory);
}
// SITE: !malterlib.sized.inlinable = !{
// SITE-DAG: !{ptr @_ZN3BoxIiED0Ev, !"_ZN3BoxIiED0Ev"}
// SITE-DAG: !{ptr @_ZTV3BoxIiE, !"_ZTV3BoxIiE"}
#elif defined(ALIAS)
// Also what a constructor listed through its alias depends on, the complete
// object constructor that is the base one, `C1` of `C2`, whose dependencies
// are recorded with that: the dllimport one it delegates to.
struct A {
  A(int);
  A(short);
  __declspec(dllimport) A(long);
  virtual ~A() {}
};
A::A(int) : A(0L) {}
A::A(short) {}
[[malterlib::sized_construction]] A *make(void *p, bool b) {
  return new (p) A(b ? A(1) : A(short(2)));
}
// ALIAS: !malterlib.sized.inlinable = !{
// ALIAS-DAG: !{ptr @_ZN1AC1Ei, !"_ZN1AC1Ei"}
// ALIAS-DAG: !{ptr @_ZN1AC2El, !"_ZN1AC2El"}
#elif defined(FILLER)
// The constructor of the filler of an array, which can construct none, is
// listed, which the backend references only while what is emitted uses it.
void *operator new[](Size, void *p) noexcept;
struct __declspec(dllimport) A {
  A();
  A(int) {}
  virtual ~A() {}
};
[[malterlib::sized_construction]] A *makeArray(void *p, int n) {
  return new (p) A[n]{A(1)};
}
// FILLER-NOT: @__mib_sized_ref =
// FILLER: !malterlib.sized.inlinable = !{
// FILLER-DAG: !{ptr @"??0A@@QEAA@XZ", !"??0A@@QEAA@XZ"}
#elif defined(INHERITED)
// An inherited constructor emitted inline in a result of a condition that
// isn't constant installs the vtables only while what is emitted uses it.
void *operator new(Size, void *p) noexcept;
struct Token {
  ~Token() {}
};
struct Base {
  Base(Token) {}
};
struct __declspec(dllimport) D : Base {
  using Base::Base;
  D(long);
  virtual ~D() {}
};
bool policy();
[[malterlib::sized_construction]] D *make(void *p) {
  return new (p) D(policy() ? D(Token{}) : D(1L));
}
// INHERITED-NOT: @__mib_sized_ref =
// INHERITED: !malterlib.sized.inlinable = !{
// INHERITED-DAG: !{ptr @"??0D@@QEAA@J@Z", !"??0D@@QEAA@J@Z"}
// INHERITED-DAG: !{ptr @"??_SD@@6B@", !"??_SD@@6B@"}
#elif defined(LONGCHAIN)
// A chain of delegating constructors that ends with a dllimport one, however
// long, installs the vtable of that one's image, not one of this module.
template <unsigned N> struct Tag {};
struct A {
  __declspec(dllimport) A(Tag<0>);
  template <unsigned N> A(Tag<N>) : A(Tag<N - 1>{}) {}
  virtual ~A();
};
[[malterlib::sized_construction]] A *site(void *p) {
  return new (p) A(Tag<18>{});
}
// LONGCHAIN-NOT: comdat($"??_7A@@6B@")
// LONGCHAIN: @__mib_sized_ref =
// LONGCHAIN-NOT: comdat($"??_7A@@6B@")
#endif
