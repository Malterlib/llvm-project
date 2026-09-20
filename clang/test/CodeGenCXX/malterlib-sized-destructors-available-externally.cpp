// RUN: %clang_cc1 -std=c++20 -triple x86_64-pc-windows-msvc -fms-extensions -fmalterlib-sized-destructors -O2 -disable-llvm-passes -emit-llvm -o - -DMSVC %s | FileCheck %s --check-prefix=MSVC
// RUN: %clang_cc1 -std=c++20 -triple x86_64-linux-gnu -fmalterlib-sized-destructors -O2 -disable-llvm-passes -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -std=c++20 -triple x86_64-windows-gnu -fmalterlib-sized-destructors -O2 -disable-llvm-passes -emit-llvm -o - -DMINGW %s | FileCheck %s --check-prefix=MINGW
// RUN: %clang_cc1 -std=c++20 -triple x86_64-pc-windows-msvc -fms-extensions -fmalterlib-sized-destructors -O2 -disable-llvm-passes -emit-llvm -o - -DDELEGATING %s | FileCheck %s --check-prefix=DELEGATING
// RUN: %clang_cc1 -std=c++20 -triple x86_64-pc-windows-msvc -fms-extensions -fmalterlib-sized-destructors -O2 -disable-llvm-passes -emit-llvm -o - -DCHAINED %s | FileCheck %s --check-prefix=CHAINED
// RUN: %clang_cc1 -std=c++20 -triple x86_64-pc-windows-msvc -fms-extensions -fmalterlib-sized-destructors -O2 -disable-llvm-passes -emit-llvm -o - -DIMPORTEDFILLER %s | FileCheck %s --check-prefix=IMPORTEDFILLER
// RUN: %clang_cc1 -std=c++20 -triple x86_64-pc-windows-msvc -fms-extensions -fmalterlib-sized-destructors -O2 -disable-llvm-passes -emit-llvm -o - -DBRIDGE %s | FileCheck %s --check-prefix=BRIDGE
// RUN: %clang_cc1 -std=c++20 -triple x86_64-pc-windows-msvc -fms-extensions -fmalterlib-sized-destructors -O2 -disable-llvm-passes -emit-llvm -o - -DEXTERN %s | FileCheck %s --check-prefix=EXTERN
// RUN: %clang_cc1 -std=c++20 -triple x86_64-pc-windows-msvc -fms-extensions -fmalterlib-sized-destructors -O2 -disable-llvm-passes -emit-llvm -o - -DOWNER %s | FileCheck %s --check-prefix=OWNER
// RUN: %clang_cc1 -std=c++20 -triple x86_64-pc-windows-msvc -fms-extensions -fmalterlib-sized-destructors -O2 -disable-llvm-passes -emit-llvm -o - -DNOINLINE %s | FileCheck %s --check-prefix=NOINLINE --implicit-check-not='??_7Delegating@@6B@'
// RUN: %clang_cc1 -std=c++20 -triple x86_64-pc-windows-msvc -fms-extensions -fmalterlib-sized-destructors -O0 -emit-llvm -o - -DNOINLINE %s | FileCheck %s --check-prefix=NOINLINE --implicit-check-not='??_7Delegating@@6B@'

// A site that can inline the body of a definition this module only has to
// inline, an imported one or an instantiation another module has, binds what it
// depends on in this image, which the module keeps no other record of: those
// are listed too, like the local vftable an inline constructor of a dllimport
// class installs, which the site uses only where it inlined it. The definition
// itself, and what it depends on that is one too, the module lists without
// keeping it, which the backend references only while the site still calls it.

#if defined(OWNER)
// An owner in a module without a site depends on it only while it calls it,
// and on what it depends on itself, the vtable it installs, which the owner
// installs where it inlines it, only while something emitted uses that.
template <class T> struct Owned {
  Owned();
  Owned(int) {}
  virtual ~Owned() {}
};
extern template Owned<int>::Owned(int);
template <> Owned<int>::Owned() : Owned(0) {}
// OWNER: @__mib_sized_kept = internal constant [2 x ptr] [ptr @"??_7?$Owned@H@@6B@", ptr @"??_E?$Owned@H@@UEAAPEAXI@Z"]
// OWNER: define {{.*}} @"??0?$Owned@H@@QEAA@XZ"({{.*}} !malterlib.sized.deps ![[OWNER:[0-9]+]]
// OWNER: ![[OWNER]] = !{i64 0, ptr @"??0?$Owned@H@@QEAA@H@Z", !"malterlib.sized.may-go", ptr @"??_7?$Owned@H@@6B@", !"malterlib.sized.may-go"}
#elif defined(EXTERN)
// So is the constructor of an extern template an inline constructor delegates
// to, which the module has only to inline.
template <class T> struct Delegating {
  Delegating() : Delegating(0) {}
  Delegating(int Value) : m(Value) {}
  virtual ~Delegating() {}
  int m;
};
extern template Delegating<int>::Delegating(int);
[[malterlib::sized_construction]] Delegating<int> *makeDelegating() {
  return new Delegating<int>;
}
// The owner's dependency on it isn't kept, which would keep its symbol in
// an object link-time optimization makes, also where nothing calls it.
// EXTERN: @__mib_sized_ref = private constant [2 x ptr] [ptr @"??0?$Delegating@H@@QEAA@XZ", ptr @"??0?$Delegating@H@@QEAA@XZ.mib_sized"]
// EXTERN: @__mib_sized_kept = internal constant [2 x ptr] [ptr @"??_7?$Delegating@H@@6B@", ptr @"??_E?$Delegating@H@@UEAAPEAXI@Z"]
// EXTERN: !malterlib.sized.inlinable = !{![[DELEGATED:[0-9]+]], ![[INSTALLED:[0-9]+]], ![[DELETING:[0-9]+]]}
// EXTERN: ![[DELEGATED]] = !{ptr @"??0?$Delegating@H@@QEAA@H@Z", !"??0?$Delegating@H@@QEAA@H@Z"}
// EXTERN: ![[INSTALLED]] = !{ptr @"??_7?$Delegating@H@@6B@", !"??_7?$Delegating@H@@6B@"}
// EXTERN: ![[DELETING]] = !{ptr @"??_E?$Delegating@H@@UEAAPEAXI@Z", !"??_E?$Delegating@H@@UEAAPEAXI@Z"}
// EXTERN: !{i64 0, ptr @"??0?$Delegating@H@@QEAA@H@Z", !"malterlib.sized.may-go", ptr @"??_7?$Delegating@H@@6B@", !"malterlib.sized.may-go"}
#elif defined(DELEGATING)
// So is the dllimport constructor an inline constructor delegates to, whose
// body it inlines, which the site depends on only while it still calls it, and
// what the inlined body installs in its place, this image's vftable.
struct Delegating {
  __declspec(dllimport) Delegating(int) {}
  Delegating() : Delegating(0) {}
  virtual ~Delegating() {}
};
[[malterlib::sized_construction]] Delegating *makeDelegating() { return new Delegating; }
// DELEGATING: @__mib_sized_ref = private constant [2 x ptr] [ptr @"??0Delegating@@QEAA@XZ", ptr @"??0Delegating@@QEAA@XZ.mib_sized"]
// DELEGATING: !malterlib.sized.inlinable = !{!{{[0-9]+}}, !{{[0-9]+}}, !{{[0-9]+}}}
// DELEGATING-DAG: = !{ptr @"??0Delegating@@QEAA@H@Z", !"??0Delegating@@QEAA@H@Z"}
// DELEGATING-DAG: = !{ptr @"??_7Delegating@@6B@", !"??_7Delegating@@6B@"}
// DELEGATING-DAG: = !{ptr @"??_EDelegating@@UEAAPEAXI@Z", !"??_EDelegating@@UEAAPEAXI@Z"}
#elif defined(CHAINED)
// Also through a chain of those it delegates to that ends with a dllimport one,
// which the site and the owner can inline the whole of.
struct Chained {
  Chained() : Chained(0) {}
  Chained(int Value) : Chained(long(Value)) {}
  __declspec(dllimport) Chained(long) {}
  virtual ~Chained() {}
};
[[malterlib::sized_construction]] Chained *makeChained() { return new Chained; }
// CHAINED: @__mib_sized_ref = private constant [2 x ptr] [ptr @"??0Chained@@QEAA@XZ", ptr @"??0Chained@@QEAA@XZ.mib_sized"]
// CHAINED: define {{.*}} @"??0Chained@@QEAA@XZ"({{.*}} !malterlib.sized.deps ![[OUTER:[0-9]+]]
// CHAINED: !malterlib.sized.inlinable = !{![[DELEGATED:[0-9]+]], ![[INSTALLED:[0-9]+]], ![[DELETING:[0-9]+]]}
// CHAINED: ![[DELEGATED]] = !{ptr @"??0Chained@@QEAA@J@Z", !"??0Chained@@QEAA@J@Z"}
// CHAINED: ![[INSTALLED]] = !{ptr @"??_7Chained@@6B@", !"??_7Chained@@6B@"}
// CHAINED: ![[DELETING]] = !{ptr @"??_EChained@@UEAAPEAXI@Z", !"??_EChained@@UEAAPEAXI@Z"}
// CHAINED: ![[OUTER]] = !{i64 0, ptr @"??0Chained@@QEAA@H@Z", null, ptr @"??0Chained@@QEAA@J@Z", !"malterlib.sized.may-go", ptr @"??_7Chained@@6B@", !"malterlib.sized.may-go"}
#elif defined(BRIDGE)
// An owner that inlines the constructor it delegates to, and what that one
// delegates to in turn, depends on what that calls itself, like a dllimport
// constructor another image defines without the flag.
template <class T> struct Bridged {
  __declspec(dllexport) Bridged();
  Bridged(int) : Bridged(0L) {}
  Bridged(long) : Bridged(short(0)) {}
  __declspec(dllimport) Bridged(short);
  virtual ~Bridged() {}
  T m;
};
template <> __declspec(dllexport) Bridged<int>::Bridged();
template <> __declspec(dllimport) Bridged<int>::Bridged(short);
extern template Bridged<int>::Bridged(int);
template <> Bridged<int>::Bridged() : Bridged(0) {}
// BRIDGE: define {{.*}} @"??0?$Bridged@H@@QEAA@XZ"({{.*}} !malterlib.sized.deps ![[BRIDGE:[0-9]+]]
// BRIDGE: ![[BRIDGE]] = !{i64 0, ptr @"??0?$Bridged@H@@QEAA@H@Z", !"malterlib.sized.may-go", ptr @"??0?$Bridged@H@@QEAA@J@Z", !"malterlib.sized.may-go", ptr @"??0?$Bridged@H@@QEAA@F@Z", !"malterlib.sized.may-go"}
#elif defined(IMPORTEDFILLER)
// Not the local vftable of a class whose elements an array filler constructs
// with a constructor of another image the module has no body of, which
// installs that image's vftable, although an inline constructor installs it
// elsewhere in the module.
struct __declspec(dllimport) Filled {
  Filled();
  Filled(int) {}
  virtual ~Filled() {}
};
void *operator new(decltype(sizeof(0)), void *p) noexcept;
void *operator new[](decltype(sizeof(0)), void *p) noexcept;
Filled *ordinaryFilled(void *p) { return new (p) Filled(1); }
[[malterlib::sized_construction]] Filled *makeFilled(void *p, decltype(sizeof(0)) n) {
  return new (p) Filled[n]{};
}
// Also one it has the body of that isn't inlined.
struct __declspec(dllimport) NoInlineFilled {
  __declspec(noinline) NoInlineFilled() {}
  NoInlineFilled(int) {}
  virtual ~NoInlineFilled() {}
};
NoInlineFilled *ordinaryNoInline(void *p) { return new (p) NoInlineFilled(1); }
[[malterlib::sized_construction]] NoInlineFilled *makeNoInline(void *p, decltype(sizeof(0)) n) {
  return new (p) NoInlineFilled[n]{};
}
// Also one that delegates to such a constructor, which installs them.
struct __declspec(dllimport) DelegatingFilled {
  DelegatingFilled() : DelegatingFilled(0L) {}
  DelegatingFilled(int) {}
  DelegatingFilled(long);
  virtual ~DelegatingFilled() {}
};
DelegatingFilled *ordinaryDelegating(void *p) { return new (p) DelegatingFilled(1); }
[[malterlib::sized_construction]] DelegatingFilled *makeDelegatingFilled(void *p, decltype(sizeof(0)) n) {
  return new (p) DelegatingFilled[n]{};
}
// IMPORTEDFILLER: !malterlib.sized.inlinable = !{![[FILLER:[0-9]+]], ![[UNINLINED:[0-9]+]], ![[FILLERDELEGATOR:[0-9]+]], ![[FILLERDELEGATEE:[0-9]+]]}
// IMPORTEDFILLER: ![[FILLER]] = !{ptr @"??0Filled@@QEAA@XZ", !"??0Filled@@QEAA@XZ"}
// IMPORTEDFILLER: ![[UNINLINED]] = !{ptr @"??0NoInlineFilled@@QEAA@XZ", !"??0NoInlineFilled@@QEAA@XZ"}
// IMPORTEDFILLER: ![[FILLERDELEGATOR]] = !{ptr @"??0DelegatingFilled@@QEAA@XZ", !"??0DelegatingFilled@@QEAA@XZ"}
// IMPORTEDFILLER: ![[FILLERDELEGATEE]] = !{ptr @"??0DelegatingFilled@@QEAA@J@Z", !"??0DelegatingFilled@@QEAA@J@Z"}
#elif defined(NOINLINE)
// Not what one it can't inline installs, which installs the vftable of the
// image that defines it: the owner depends only on it, which it calls.
struct Delegating {
  __declspec(dllimport) __declspec(noinline) Delegating(int) {}
  Delegating() : Delegating(0) {}
  virtual ~Delegating() {}
};
[[malterlib::sized_construction]] Delegating *makeDelegating() { return new Delegating; }
// NOINLINE: define {{.*}} @"??0Delegating@@QEAA@XZ"({{.*}} !malterlib.sized.deps ![[DEPS:[0-9]+]]
// NOINLINE: ![[DEPS]] = !{i64 0, ptr @"??0Delegating@@QEAA@H@Z", null}
#elif defined(MINGW)
// MinGW keeps an inline function of a dllimport class as an inline
// definition of its own, which the site that inlines it binds the import of
// what it depends on with, whichever copy the link keeps: the site depends on
// those too.
struct __declspec(dllimport) Imported {
  Imported() {}
  virtual ~Imported() {}
  int x;
};
[[malterlib::sized_construction]] Imported *makeImported() { return new Imported; }
// MINGW: @__mib_sized_ref = private constant [4 x ptr] [ptr @_ZN8ImportedC1Ev, ptr @_ZN8ImportedC1Ev.mib_sized, ptr @_ZTV8Imported, ptr @_ZTV8Imported.mib_sized]
#elif defined(MSVC)
struct __declspec(dllimport) Imported {
  Imported() {}
  virtual ~Imported() {}
  int x;
};
[[malterlib::sized_construction]] Imported *makeImported() { return new Imported; }
// Also what the local vftable depends on in turn, the deleting destructor it
// holds, which a site that inlined the constructor uses through it.
// MSVC-NOT: @__mib_sized_ref =
// MSVC: !malterlib.sized.inlinable = !{!{{[0-9]+}}, !{{[0-9]+}}, !{{[0-9]+}}}
// MSVC-DAG: = !{ptr @"??0Imported@@QEAA@XZ", !"??0Imported@@QEAA@XZ"}
// MSVC-DAG: = !{ptr @"??_SImported@@6B@", !"??_SImported@@6B@"}
// MSVC-DAG: = !{ptr @"??_EImported@@UEAAPEAXI@Z", !"??_EImported@@UEAAPEAXI@Z"}
#else
template <class T> struct Instantiated {
  Instantiated() {}
  virtual ~Instantiated() {}
  T x;
};
extern template struct Instantiated<int>;
[[malterlib::sized_construction]] Instantiated<int> *makeInstantiated() {
  return new Instantiated<int>;
}
// CHECK-NOT: @__mib_sized_ref =
// Also the deleting destructor the vtable another module defines holds.
// CHECK: !malterlib.sized.inlinable = !{![[C1:[0-9]+]], ![[DELETING:[0-9]+]], ![[C2:[0-9]+]], ![[VTABLE:[0-9]+]]}
// CHECK: ![[C1]] = !{ptr @_ZN12InstantiatedIiEC1Ev, !"_ZN12InstantiatedIiEC1Ev"}
// CHECK: ![[DELETING]] = !{ptr @_ZN12InstantiatedIiED0Ev, !"_ZN12InstantiatedIiED0Ev"}
// CHECK: ![[C2]] = !{ptr @_ZN12InstantiatedIiEC2Ev, !"_ZN12InstantiatedIiEC2Ev"}
// CHECK: ![[VTABLE]] = !{ptr @_ZTV12InstantiatedIiE, !"_ZTV12InstantiatedIiE"}
#endif
