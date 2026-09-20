// RUN: %clang_cc1 -std=c++20 -triple x86_64-windows-msvc -fms-extensions -fno-rtti -fmalterlib-sized-destructors -O2 -flto=thin -fwhole-program-vtables -emit-llvm -o - %s | FileCheck %s

// A site that constructs with the constructor of a DLL, which installs the
// DLL's vtable, also depends on the deleting destructor in a slot of the
// vtable of this module, whose vtables aren't external on the Microsoft ABI,
// while what is emitted calls it directly, as whole-program devirtualization
// can make the builtin's call of it.
using Size = decltype(sizeof(0));
inline void *operator new(Size, void *p) noexcept { return p; }
struct A final {
  __declspec(dllimport) __declspec(noinline) A();
  A(int) {}
  virtual ~A() {}
  int value;
};
extern "C" [[malterlib::sized_construction]] Size probe(void *p, void **memory) {
  return __builtin_malterlib_destroy(new (p) A, memory);
}
// By its name, which the backend finds where link-time optimization brings
// in its definition, which this module doesn't have.
// CHECK: !malterlib.sized.inlinable = !{
// CHECK-DAG: !{{{.*}}, !"??_EA@@UEAAPEAXI@Z"}

// RUN: %clang_cc1 -std=c++20 -triple x86_64-windows-msvc -fms-extensions -fno-rtti -fmalterlib-sized-destructors -O2 -emit-llvm -o - %s -DNOT_FINAL | FileCheck %s --check-prefix=VIRTUAL
// RUN: %clang_cc1 -std=c++20 -triple x86_64-windows-msvc -fms-extensions -fno-rtti -fmalterlib-sized-destructors -O2 -emit-llvm -o - %s -DFINAL | FileCheck %s --check-prefix=FINAL
// But not where the call goes through the vtables the object has, which the
// imported constructor installs and their proof covers, of a class that isn't
// final, without whole-program devirtualization; but where it is final.
#if defined(NOT_FINAL) || defined(FINAL)
#ifdef FINAL
#define DFinal final
#else
#define DFinal
#endif
struct B DFinal {
  __declspec(dllimport) __declspec(noinline) B();
  B(int) {}
  virtual ~B() {}
  int value;
};
extern "C" B *ordinaryB(void *p) { return new (p) B(1); }
extern "C" [[malterlib::sized_construction]] Size probeB(void *p, void **memory) {
  return __builtin_malterlib_destroy(new (p) B, memory);
}
#endif
// VIRTUAL-NOT: !"??_EB@@UEAAPEAXI@Z"
// FINAL: !{{{.*}}, !"??_EB@@UEAAPEAXI@Z"}
