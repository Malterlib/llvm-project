// RUN: %clang_cc1 -std=c++20 -triple x86_64-pc-windows-msvc -fmalterlib-sized-destructors -O2 -disable-llvm-passes -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -std=c++20 -triple x86_64-linux-gnu -fmalterlib-sized-destructors -O2 -disable-llvm-passes -emit-llvm -o - %s | FileCheck %s --check-prefix=ELF
// RUN: %clang_cc1 -std=c++20 -triple x86_64-pc-windows-msvc -fmalterlib-sized-destructors -O2 -disable-llvm-passes -emit-llvm -o - -DCHOSEN %s | FileCheck %s --check-prefix=CHOSEN
// RUN: %clang_cc1 -std=c++20 -triple x86_64-pc-windows-msvc -fmalterlib-sized-destructors -O2 -flto=thin -disable-llvm-passes -emit-llvm -o - -DCHOSEN %s | FileCheck %s --check-prefix=LTO
// RUN: %clang_cc1 -std=c++20 -triple x86_64-pc-windows-msvc -fmalterlib-sized-destructors -fms-extensions -O2 -disable-llvm-passes -emit-llvm -o - -DCALLED %s | FileCheck %s --check-prefix=CHOSEN
// RUN: %clang_cc1 -std=c++20 -triple x86_64-pc-windows-msvc -fmalterlib-sized-destructors -O2 -disable-llvm-passes -emit-llvm -o - -DUNEVALUATED %s | FileCheck %s
// RUN: %clang_cc1 -std=c++20 -triple x86_64-pc-windows-msvc -fmalterlib-sized-destructors -O2 -disable-llvm-passes -emit-llvm -o - -DCONSTEVAL %s | FileCheck %s
// RUN: %clang_cc1 -std=c++20 -triple x86_64-pc-windows-msvc -fmalterlib-sized-destructors -O2 -disable-llvm-passes -emit-llvm -o - -DSHORT %s | FileCheck %s
// RUN: %clang_cc1 -std=c++20 -triple x86_64-pc-windows-msvc -fms-extensions -fmalterlib-sized-destructors -O2 -disable-llvm-passes -emit-llvm -o - -DDELEGATE %s | FileCheck %s --check-prefix=DELEGATE
// RUN: %clang_cc1 -std=c++20 -triple x86_64-linux-gnu -fmalterlib-sized-destructors -O2 -disable-llvm-passes -emit-llvm -o - -DLOCAL %s | FileCheck %s --check-prefix=LOCAL

// A construction site that constructs with one result of a conditional
// records the constructor of each, but of a constant condition only the one it
// chooses, which is the only one it calls.

using Size = decltype(sizeof(0));
void *operator new(Size, void *) noexcept;

struct A {
  A(int);
  A(long);
  virtual ~A();
  int i;
};

#if !defined(CHOSEN) && !defined(CALLED)
#ifdef CONSTEVAL
// Also of one that tells whether it is constant evaluated, which it isn't
// where the program runs, also through a function that gives that.
constexpr bool isConstantEvaluated() { return __builtin_is_constant_evaluated(); }
[[malterlib::sized_construction]] A *makeConstantEvaluated(void *p) {
  return new (p) A(isConstantEvaluated() ? A(2L) : A(1));
}
// Also an immediate invocation, whose value is the program's.
consteval bool direct() { return true; }
[[malterlib::sized_construction]] A *makeImmediate(void *p) {
  return new (p) A(direct() ? A(1) : A(2L));
}
#elif defined(SHORT)
// Also of one whose call the operand before it leaves out, `c || f()`.
bool alternate();
[[malterlib::sized_construction]] A *makeAnd(void *p) {
  return new (p) A((sizeof(void *) == 4 && alternate()) ? A(2L) : A(1));
}
#endif
[[malterlib::sized_construction]] A *makeConstant(void *p) {
#ifdef UNEVALUATED
  // Also of one whose calls aren't evaluated, `noexcept(A(1))`.
  return new (p) A(noexcept(A(1)) || sizeof(A(1)) ? A(1) : A(2L));
#elif defined(CONSTEVAL)
  return new (p) A(__builtin_is_constant_evaluated() ? A(2L) : A(1));
#elif defined(SHORT)
  return new (p) A((sizeof(void *) == 8 || alternate()) ? A(1) : A(2L));
#else
  return new (p) A(true ? A(1) : A(2L));
#endif
}
// CHECK: @__mib_sized_ref = private constant [2 x ptr] [ptr @"??0A@@QEAA@H@Z", ptr @"??0A@@QEAA@H@Z.mib_sized"]
// ELF: @__mib_sized_ref = private constant [2 x ptr] [ptr @_ZN1AC1Ei, ptr @_ZN1AC1Ei.mib_sized]
#elif defined(CALLED)
// Not of one that calls a function, also a constexpr one, whose value can be
// another where the program runs, like that of `__builtin_constant_p` in an
// imported one, which the site calls.
__declspec(dllimport) __declspec(noinline) constexpr bool chooseImported(int Value) {
  return __builtin_constant_p(Value);
}
[[malterlib::sized_construction]] A *makeCalled(void *p) {
  return new (p) A(chooseImported(0) ? A(1) : A(2L));
}
#else
bool choose();
[[malterlib::sized_construction]] A *makeChosen(void *p) {
  return new (p) A(choose() ? A(1) : A(2L));
}
// Of either, which the optimizer can still choose between, each only while
// what is emitted uses it, also what it depends on, like the vftable inlining
// it installs, also from another module, by name.
// CHOSEN-NOT: @__mib_sized_ref =
// CHOSEN: !malterlib.sized.inlinable = !{![[INT:[0-9]+]], ![[LONG:[0-9]+]], ![[VTABLE:[0-9]+]], ![[DELETING:[0-9]+]]}
// CHOSEN: ![[INT]] = !{ptr @"??0A@@QEAA@H@Z", !"??0A@@QEAA@H@Z"}
// CHOSEN: ![[LONG]] = !{ptr @"??0A@@QEAA@J@Z", !"??0A@@QEAA@J@Z"}
// CHOSEN: ![[VTABLE]] = !{ptr @"??_7A@@6B@", !"??_7A@@6B@"}
// CHOSEN: ![[DELETING]] = !{ptr @"??_EA@@UEAAPEAXI@Z", !"??_EA@@UEAAPEAXI@Z"}
// With link-time optimization, which can import the body of one the module
// only declares and inline it, whose dependencies the module doesn't know,
// each is referenced as it is.
// LTO: @__mib_sized_ref = private constant [4 x ptr] [ptr @"??0A@@QEAA@H@Z", ptr @"??0A@@QEAA@H@Z.mib_sized", ptr @"??0A@@QEAA@J@Z", ptr @"??0A@@QEAA@J@Z.mib_sized"]
#endif

#ifdef DELEGATE
// Also what a constructor listed that way delegates to, through each inline
// one, which a site that inlined them all calls itself: the dllimport one.
struct B {
  B() : B(0) {}
  B(int) : B(0L) {}
  B(short) {}
  __declspec(dllimport) B(long);
  virtual ~B() {}
};
[[malterlib::sized_construction]] B *fg_MakeDelegated(void *p, bool b) {
  return new (p) B(b ? B() : B(short(0)));
}
// DELEGATE: !malterlib.sized.inlinable = !{
// DELEGATE-DAG: !{ptr @"??0B@@QEAA@XZ", !"??0B@@QEAA@XZ"}
// DELEGATE-DAG: !{ptr @"??0B@@QEAA@H@Z", !"??0B@@QEAA@H@Z"}
// DELEGATE-DAG: !{ptr @"??0B@@QEAA@J@Z", !"??0B@@QEAA@J@Z"}
#endif

#ifdef LOCAL
// The marker alias of a local constructor listed that way is kept, which the
// optimizer would otherwise remove while a site still calls it.
namespace {
struct Local {
  Local(int);
  Local(long);
  virtual ~Local();
};
Local::Local(int) {}
Local::Local(long) {}
Local::~Local() {}
}
[[malterlib::sized_construction]] void *fg_MakeLocal(void *p, bool b) {
  return new (p) Local(b ? Local(1) : Local(2L));
}
// LOCAL: @llvm.compiler.used = appending global [{{[0-9]+}} x ptr] [{{.*}}ptr @_ZN12_GLOBAL__N_15LocalC1Ei.mib_sized,{{.*}}ptr @_ZN12_GLOBAL__N_15LocalC1El.mib_sized
#endif
