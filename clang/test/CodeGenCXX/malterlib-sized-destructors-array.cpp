// RUN: %clang_cc1 -std=c++20 -triple x86_64-linux-gnu -fmalterlib-sized-destructors -fcxx-exceptions -fexceptions -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -std=c++20 -triple x86_64-pc-windows-msvc -fmalterlib-sized-destructors -fcxx-exceptions -fexceptions -emit-llvm -o - %s | FileCheck %s --check-prefix=MSVC

// A construction site that makes an array depends on the constructors of its
// elements, as one that makes an object does on its constructor.
using Size = decltype(sizeof(0));
void *operator new[](Size, void *Storage) noexcept;
struct A {
  A();
  A(int Value);
  virtual ~A();
  int i;
};
struct B {
  B();
  virtual ~B();
};
[[malterlib::sized_construction]] A *makeArray(void *Storage) {
  return new (Storage) A[2];
}
// Of each element of a list, and of what fills the rest.
[[malterlib::sized_construction]] A *makeList(void *Storage) {
  return new (Storage) A[3]{A(1)};
}
// Of either result of a conditional, which constructs the element directly.
bool alternate();
struct C {
  C(int Value);
  C(long Value);
  virtual ~C();
};
[[malterlib::sized_construction]] C *makeChosen(void *Storage) {
  return new (Storage) C[1]{alternate() ? C(1) : C(2l)};
}
// Not of one that throws, which constructs nothing, and of the last of a
// comma.
[[malterlib::sized_construction]] C *makeThrowing(void *Storage) {
  return new (Storage) C[2]{alternate() ? C(1) : throw 0, (alternate(), C(2l))};
}
// Of an array of arrays.
[[malterlib::sized_construction]] B *makeNested(void *Storage) {
  return &(*new (Storage) B[2][2])[0];
}
// Not of one without elements, which constructs none.
struct D {
  D();
  virtual ~D();
};
[[malterlib::sized_construction]] D *makeEmpty(void *Storage) {
  return new (Storage) D[0];
}
[[malterlib::sized_construction]] D *makeEmptyNested(void *Storage) {
  return &(*new (Storage) D[2][0])[0];
}

// An element in a result of a condition that isn't constant, `alternate() ?
// C(1) : throw 0`, also what fills the rest of a list, which a count known only
// once optimized can leave out, only while what is emitted uses it, also the
// vtable inlining it installs.
// CHECK: @__mib_sized_ref = private constant [8 x ptr] [ptr @_ZN1AC1Ev, ptr @_ZN1AC1Ev.mib_sized, ptr @_ZN1AC1Ei, ptr @_ZN1AC1Ei.mib_sized, ptr @_ZN1CC1El, ptr @_ZN1CC1El.mib_sized, ptr @_ZN1BC1Ev, ptr @_ZN1BC1Ev.mib_sized], section ".data.rel.ro.__mib_sized_ref"
// Also the deleting destructors the vtables other modules define hold, which
// link-time optimization can call directly.
// CHECK: !malterlib.sized.inlinable = !{![[FILLER:[0-9]+]], ![[CTOR:[0-9]+]], ![[VTABLE:[0-9]+]], ![[A:[0-9]+]], ![[C:[0-9]+]], ![[B:[0-9]+]]}
// CHECK-DAG: ![[A]] = !{ptr @_ZN1AD0Ev, !"_ZN1AD0Ev"}
// CHECK-DAG: ![[FILLER]] = !{ptr @_ZTV1A, !"_ZTV1A"}
// CHECK-DAG: ![[CTOR]] = !{ptr @_ZN1CC1Ei, !"_ZN1CC1Ei"}
// CHECK-DAG: ![[VTABLE]] = !{ptr @_ZTV1C, !"_ZTV1C"}
// CHECK-DAG: ![[C]] = !{ptr @_ZN1CD0Ev, !"_ZN1CD0Ev"}
// CHECK-DAG: ![[B]] = !{ptr @_ZN1BD0Ev, !"_ZN1BD0Ev"}
// MSVC: @__mib_sized_ref = private constant [8 x ptr] [ptr @"??0A@@QEAA@XZ", ptr @"??0A@@QEAA@XZ.mib_sized", ptr @"??0A@@QEAA@H@Z", ptr @"??0A@@QEAA@H@Z.mib_sized", ptr @"??0C@@QEAA@J@Z", ptr @"??0C@@QEAA@J@Z.mib_sized", ptr @"??0B@@QEAA@XZ", ptr @"??0B@@QEAA@XZ.mib_sized"], section ".rdata$mibszf"
// MSVC: !malterlib.sized.inlinable = !{![[FILLER:[0-9]+]], ![[CTOR:[0-9]+]], ![[VTABLE:[0-9]+]], ![[DELETING:[0-9]+]], ![[FILLERDELETING:[0-9]+]]}
// MSVC-DAG: ![[FILLER]] = !{ptr @"??_7A@@6B@", !"??_7A@@6B@"}
// MSVC-DAG: ![[CTOR]] = !{ptr @"??0C@@QEAA@H@Z", !"??0C@@QEAA@H@Z"}
// MSVC-DAG: ![[VTABLE]] = !{ptr @"??_7C@@6B@", !"??_7C@@6B@"}
// MSVC-DAG: ![[DELETING]] = !{ptr @"??_EC@@UEAAPEAXI@Z", !"??_EC@@UEAAPEAXI@Z"}
// MSVC-DAG: ![[FILLERDELETING]] = !{ptr @"??_EA@@UEAAPEAXI@Z", !"??_EA@@UEAAPEAXI@Z"}
