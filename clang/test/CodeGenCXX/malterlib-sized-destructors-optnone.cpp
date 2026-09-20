// RUN: %clang_cc1 -std=c++20 -triple x86_64-windows-msvc -fms-extensions -fno-rtti -fmalterlib-sized-destructors -O2 -emit-llvm -o - %s | FileCheck %s --implicit-check-not='!{ptr @"??_SA@@6B@"' --implicit-check-not='!{ptr @"??_EA@@UEAAPEAXI@Z"'
// RUN: %clang_cc1 -std=c++20 -triple x86_64-windows-msvc -fms-extensions -fno-rtti -fmalterlib-sized-destructors -O2 -flto=thin -emit-llvm -o - %s | FileCheck %s --implicit-check-not='!{ptr @"??_SA@@6B@"' --implicit-check-not='!{ptr @"??_EA@@UEAAPEAXI@Z"'

// A site depends on an imported constructor that isn't inlined, `optnone`,
// also with link-time optimization, only by calling it: not on the local
// vftable its body installs, nor on the deleting destructor in it, which an
// ordinary construction uses, as the imported one installs the vftable of the
// image that has it.
using Size = decltype(sizeof(0));
inline void *operator new(Size, void *p) noexcept { return p; }
struct __declspec(dllimport) A {
  __attribute__((optnone)) A() {}
  A(int) {}
  virtual ~A() {}
  int value;
};
extern "C" [[malterlib::sized_construction]] A *make(void *p) { return new (p) A; }
extern "C" A *ordinary(void *p) { return new (p) A(1); }
// CHECK: !malterlib.sized.inlinable = !{![[CTOR:[0-9]+]]}
// CHECK: ![[CTOR]] = !{ptr @"??0A@@QEAA@XZ", !"??0A@@QEAA@XZ"}
