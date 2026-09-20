// RUN: %clang_cc1 -std=c++20 -triple x86_64-linux-gnu -fmalterlib-sized-destructors -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -std=c++20 -triple arm64-apple-macosx -fmalterlib-sized-destructors -emit-llvm -o - %s | FileCheck %s --check-prefix=MACHO
// RUN: %clang_cc1 -std=c++20 -triple x86_64-pc-windows-msvc -fmalterlib-sized-destructors -emit-llvm -o - -DMSVC %s | FileCheck %s --check-prefix=MSVC
// RUN: %clang_cc1 -std=c++20 -triple i686-pc-windows-msvc -fmalterlib-sized-destructors -emit-llvm -o - -DMSVC %s | FileCheck %s --check-prefix=MSVC32
// RUN: %clang_cc1 -std=c++20 -triple i686-w64-windows-gnu -fmalterlib-sized-destructors -emit-llvm -o - -DMINGW32 %s | FileCheck %s --check-prefix=MINGW32
// RUN: %clang_cc1 -std=c++20 -triple x86_64-linux-gnu -fmalterlib-sized-destructors -mconstructor-aliases -emit-llvm -o - %s | FileCheck %s --check-prefix=ALIAS
// RUN: %clang_cc1 -std=c++20 -triple x86_64-linux-gnu -emit-llvm -o - %s | FileCheck %s --check-prefix=OFF
// RUN: %clang_cc1 -std=c++20 -triple x86_64-linux-gnu -fmalterlib-sized-destructors -fexperimental-relative-c++-abi-vtables -pic-level 2 -emit-llvm -o - %s | FileCheck %s --check-prefix=RELATIVE
// RUN: %clang_cc1 -std=c++20 -triple x86_64-linux-gnu -fmalterlib-sized-destructors -emit-llvm -o - -DINHERIT %s | FileCheck %s --check-prefix=INHERIT
// RUN: %clang_cc1 -std=c++20 -triple x86_64-linux-gnu -fmalterlib-sized-destructors -emit-llvm -o - -DINHERIT %s | FileCheck %s --check-prefix=INHERIT-NONE
// RUN: %clang_cc1 -std=c++20 -triple i686-pc-windows-msvc -fmalterlib-sized-destructors -emit-llvm -o - -DINHERIT %s | FileCheck %s --check-prefix=INHERIT-MS
// RUN: %clang_cc1 -std=c++20 -triple i686-pc-windows-msvc -fmalterlib-sized-destructors -emit-llvm -o - -DINHERIT %s | FileCheck %s --check-prefix=INHERIT-MS-NONE
// RUN: %clang_cc1 -std=c++20 -triple x86_64-pc-windows-msvc -fmalterlib-sized-destructors -fms-extensions -emit-llvm -o - -DNOVTABLE %s | FileCheck %s --check-prefix=NOVTABLE
// RUN: %clang_cc1 -std=c++20 -triple x86_64-apple-macosx -fmalterlib-sized-destructors -emit-llvm -o - -DVECTORCALL %s | FileCheck %s --check-prefix=VECTORCALL
// RUN: %clang_cc1 -std=c++20 -triple x86_64-linux-gnu -fmalterlib-sized-destructors -emit-llvm -o - -DIMPLICIT %s | FileCheck %s --check-prefix=IMPLICIT

// A class whose definitions all come from another object.
struct Foreign {
  Foreign();
  virtual ~Foreign();
  int i;
};

// A class defined here.
struct Local {
  Local();
  virtual ~Local();
  long l;
};
Local::Local() {}
Local::~Local() {}

// A class whose definitions are inline: every translation unit that uses them
// emits a copy, and the linker keeps one of them.
struct Inline {
  Inline() {}
  virtual ~Inline() {}
  int i;
};

// A class whose vtables point to a thunk for the deleting destructor.
struct Base1 {
  virtual ~Base1();
  int a;
};
struct Base2 {
  virtual ~Base2();
  int b;
};
struct Multi : Base1, Base2 {
  Multi();
  virtual ~Multi();
};

// A class whose vtable and destructor thunk are emitted with its inline
// destructor: the thunk is first emitted for the vtable, as available
// externally, and becomes a definition when the destructor is.
struct MultiInline : Base1, Base2 {
  virtual ~MultiInline() {}
  virtual void f() {}
};
MultiInline *makeMultiInline() { return new MultiInline; }

// The deleting destructors this translation unit emits, their thunks, the
// constructors of their classes and the vtables get a marker. An external
// definition carries the attribute the backend turns into a marker symbol
// inside the definition, which follows the copy of an inline definition the
// linker keeps. The other members get none.
//
// CHECK-DAG: define{{.*}} @_ZN5LocalD0Ev({{.*}} #[[SIZED:[0-9]+]]
// CHECK-DAG: define{{.*}} @_ZN5LocalC1Ev({{.*}} #[[SIZED]]{{.*}} !malterlib.sized.deps ![[LOCAL_C1:[0-9]+]]
// CHECK-DAG: define linkonce_odr{{.*}} @_ZN6InlineD0Ev({{.*}} #[[SIZED]]
// CHECK-DAG: define linkonce_odr{{.*}} @_ZThn16_N11MultiInlineD0Ev({{.*}} #[[THUNK:[0-9]+]]
// CHECK-DAG: define{{.*}} @_ZN5LocalD1Ev({{.*}} #[[PLAIN:[0-9]+]]
// CHECK-DAG: @_ZTV5Local = {{.*}}, align 8, !malterlib.sized.deps ![[LOCAL_VTABLE:[0-9]+]] #[[VTABLE:[0-9]+]]
// CHECK-DAG: attributes #[[VTABLE]] = { "malterlib-sized" }
// CHECK-DAG: attributes #[[SIZED]] = {{{.*}}"malterlib-sized"
// CHECK-DAG: attributes #[[THUNK]] = {{{.*}}"malterlib-sized"
// CHECK-DAG: attributes #[[PLAIN]] = { {{[^"]*}}"min-legal-vector-width"
//
// A local definition has no other copies, and its marker is an alias, which
// the IR keeps apart from the local definitions of the same name other
// translation units have.
// CHECK-DAG: @_ZN12_GLOBAL__N_16HiddenD0Ev.mib_sized = internal alias %struct.__malterlib_destroy_result (ptr), ptr @_ZN12_GLOBAL__N_16HiddenD0Ev
//
// A complete constructor that is an alias of the base one is what a site
// calls; the base constructor lists it with its offset, so that its marker
// does not depend on the alias surviving optimization.
// ALIAS-DAG: @_ZN5LocalC1Ev = unnamed_addr alias void (ptr), ptr @_ZN5LocalC2Ev
// ALIAS-DAG: define{{.*}} @_ZN5LocalC2Ev({{.*}} #[[ALIAS_CTOR:[0-9]+]]
// ALIAS-DAG: attributes #[[ALIAS_CTOR]] = {{{.*}}"malterlib-sized" "malterlib-sized-aliases"="_ZN5LocalC1Ev=0"
//
// The vector deleting destructor the vftable points to is an alias of the
// scalar one.
// MSVC-DAG: define{{.*}} @"??_GLocal@@UEAAPEAXI@Z"({{.*}} #[[MS_DTOR:[0-9]+]]
// MSVC-DAG: attributes #[[MS_DTOR]] = {{{.*}}"malterlib-sized" "malterlib-sized-aliases"="??_ELocal@@UEAAPEAXI@Z=0"
// MSVC-DAG: define{{.*}} @"??0Local@@QEAA@XZ"({{.*}} #[[MS_CTOR:[0-9]+]]
// MSVC-DAG: attributes #[[MS_CTOR]] = {{{.*}}"malterlib-sized"
//
// A relative vtable a definition renamed to a local name has its marker on the
// public alias, which the references name.
// RELATIVE-NOT: .local.mib_sized
// RELATIVE: @_ZTV5Local = {{.*}}alias
//
// OFF-NOT: "malterlib-sized
// OFF-NOT: mib_sized

template <typename T>
[[malterlib::sized_construction]] T *make() {
  return new T;
}

Foreign *makeForeign() { return make<Foreign>(); }
namespace {
struct Hidden {
  Hidden() {}
  virtual ~Hidden() {}
};
} // namespace
void *makeHidden() { return make<Hidden>(); }
Local *makeLocal() { return make<Local>(); }
Inline *makeInline() { return make<Inline>(); }
Multi *makeMulti() { return make<Multi>(); }

// A library may hide the deleting destructor the vtable its exported
// constructor installs binds.
struct __attribute__((visibility("default"))) HiddenDtor {
  HiddenDtor();
  __attribute__((visibility("hidden"))) virtual ~HiddenDtor();
};
HiddenDtor *makeHiddenDtor() { return make<HiddenDtor>(); }

// A construction site refers to the constructor it calls, a definition of
// this module too, since the link checks the copies of other images as well.
// That constructor depends on the vtables it installs, and they on the
// deleting destructors and thunks in their slots, each checked in the image
// that defines it, so a library may hide what its exported constructor
// installs. The constructor is referenced itself, which keeps it through
// link-time optimization, and through its marker, which only a definition
// compiled with the flag has.
//
// CHECK-DAG: @_ZN7ForeignC1Ev.mib_sized = external constant i8
// CHECK-DAG: @__mib_sized_ref = private constant [12 x ptr] [ptr @_ZN7ForeignC1Ev, ptr @_ZN7ForeignC1Ev.mib_sized, ptr @_ZN12_GLOBAL__N_16HiddenC1Ev, ptr @_ZN12_GLOBAL__N_16HiddenC1Ev.mib_sized, ptr @_ZN5LocalC1Ev, ptr @_ZN5LocalC1Ev.mib_sized, ptr @_ZN6InlineC1Ev, ptr @_ZN6InlineC1Ev.mib_sized, ptr @_ZN5MultiC1Ev, ptr @_ZN5MultiC1Ev.mib_sized, ptr @_ZN10HiddenDtorC1Ev, ptr @_ZN10HiddenDtorC1Ev.mib_sized], section ".data.rel.ro.__mib_sized_ref", align 8
// A marker proves its claim only when what its definition binds in its image
// was compiled with the flag too: a constructor the vtable it installs, a
// vtable the deleting destructors and thunks in its slots. The owner's
// definition lists them, at the owner's offset into it, for the backend to
// record with the copy of the definition it emits.
//
// CHECK-DAG: define{{.*}} @_ZN5LocalC2Ev({{.*}}!malterlib.sized.deps ![[LOCAL_CTOR:[0-9]+]]
// CHECK-DAG: ![[LOCAL_CTOR]] = !{i64 0, ptr @_ZTV5Local, null}
// A complete constructor that calls the base one depends on it too: the link
// may bind that call to another image's copy.
// CHECK-DAG: ![[LOCAL_C1]] = !{i64 0, ptr @_ZN5LocalC2Ev, null, ptr @_ZTV5Local, null}
// CHECK-DAG: ![[LOCAL_VTABLE]] = !{i64 0, ptr @_ZN5LocalD0Ev, null}
// The marker of a local definition is carried as the value, which the
// optimizer and the linking of modules may rename apart from the definition.
// CHECK-DAG: !{i64 0, ptr @_ZTVN12_GLOBAL__N_16HiddenE, ptr @_ZTVN12_GLOBAL__N_16HiddenE.mib_sized}
// CHECK-DAG: @llvm.compiler.used = appending global [{{[0-9]+}} x ptr] [ptr @__mib_sized_ref{{.*}}]
//
// MACHO-DAG: @__mib_sized_ref = private constant [12 x ptr] [ptr @_ZN7ForeignC1Ev, ptr @_ZN7ForeignC1Ev.mib_sized, {{.*}}], section "__DATA_CONST,__mib_sized_ref", align 8
//
// MSVC-DAG: @__mib_sized_ref = private constant [14 x ptr] [ptr @"??0Imported@@QEAA@XZ", ptr @"??0Imported@@QEAA@XZ.mib_sized", ptr @"??0Foreign@@QEAA@XZ", ptr @"??0Foreign@@QEAA@XZ.mib_sized", ptr @"??0Hidden@?A0x{{[0-9A-F]+}}@@QEAA@XZ", ptr @"??0Hidden@?A0x{{[0-9A-F]+}}@@QEAA@XZ.mib_sized", ptr @"??0Local@@QEAA@XZ", ptr @"??0Local@@QEAA@XZ.mib_sized", ptr @"??0Inline@@QEAA@XZ", ptr @"??0Inline@@QEAA@XZ.mib_sized", ptr @"??0Multi@@QEAA@XZ", ptr @"??0Multi@@QEAA@XZ.mib_sized", ptr @"??0HiddenDtor@@QEAA@XZ", ptr @"??0HiddenDtor@@QEAA@XZ.mib_sized"], section ".rdata$mibszf"
// MSVC32-DAG: @__mib_sized_ref = private constant [14 x ptr] [{{.*}}ptr @"??0Foreign@@QAE@XZ", ptr @"??0Foreign@@QAE@XZ.mib_sized", {{.*}}]
// The stdcall decoration of a name is part of the name of its marker, on both
// sides of the reference.
// MINGW32-DAG: @__mib_sized_ref = private constant [14 x ptr] [{{.*}}ptr @"\01__ZN8StdMultiC1Ev@4", ptr @"\01__ZN8StdMultiC1Ev@4.mib_sized"], section ".rdata$mibszf", align 4
//
// A definition another image exports has its marker exported with it, and a
// site refers to the marker through its import thunk. The deleting destructor
// of an imported class is this module's own.
// MSVC-DAG: @"??0Imported@@QEAA@XZ.mib_sized" = external constant i8
//
// The linker reads the section as nothing but pairs of references, so a
// sanitizer must not pad it.
// RUN: %clang_cc1 -std=c++20 -triple x86_64-linux-gnu -fmalterlib-sized-destructors -fsanitize=address -emit-llvm -o - %s | FileCheck %s --check-prefix=ASAN
// ASAN: @__mib_sized_ref = private constant {{.*}}, section ".data.rel.ro.__mib_sized_ref", no_sanitize_address, align 8
//
// A new-expression outside a sized construction site refers to nothing.
Foreign *plainNew() { return new Foreign; }

// CHECK-NOT: @__mib_sized_ref.1

#ifdef MSVC
struct __attribute__((dllimport)) Imported {
  Imported();
  virtual ~Imported();
};
[[malterlib::sized_construction]] Imported *makeImported() { return new Imported; }
#endif

#ifdef MINGW32
// A calling convention the object file decorates the names of.
struct StdBase1 {
  virtual ~StdBase1() __attribute__((stdcall));
  int a;
};
struct StdBase2 {
  virtual ~StdBase2() __attribute__((stdcall));
  int b;
};
struct StdMulti : StdBase1, StdBase2 {
  __attribute__((stdcall)) StdMulti() {}
  virtual ~StdMulti() __attribute__((stdcall)) {}
};
StdMulti *makeStdMulti() { return make<StdMulti>(); }
#endif

#ifdef INHERIT
// A call emits an inheriting constructor whose arguments it cannot forward
// inline, which leaves no definition to refer to: a site then depends on the
// vtables alone, and so does a constructor that delegates to it.
struct VarBase {
  VarBase(int, ...);
  virtual ~VarBase();
};
struct VarDerived : VarBase {
  using VarBase::VarBase;
};
[[malterlib::sized_construction]] VarDerived *makeVarDerived() {
  return new VarDerived(1, 2);
}
struct VarDelegating : VarBase {
  using VarBase::VarBase;
  VarDelegating();
};
VarDelegating::VarDelegating() : VarDelegating(1, 2) {}

// INHERIT: @__mib_sized_ref = {{.*}}[ptr @_ZTV10VarDerived, ptr @_ZTV10VarDerived.mib_sized,
// INHERIT: define{{.*}} @_ZN13VarDelegatingC1Ev({{.*}}!malterlib.sized.deps ![[DELEGATING:[0-9]+]]
// INHERIT: ![[DELEGATING]] = !{i64 0, ptr @_ZTV13VarDelegating, null}
// INHERIT-NONE-NOT: define{{.*}} @_ZN10VarDerivedC
// INHERIT-NONE-NOT: define{{.*}} @_ZN13VarDelegatingCI
// INHERIT-MS: @__mib_sized_ref = {{.*}}[ptr @"??_7VarDerived@@6B@", ptr @"??_7VarDerived@@6B@.mib_sized",
// INHERIT-MS: define{{.*}} @"??0VarDelegating@@QAE@XZ"({{.*}}!malterlib.sized.deps ![[DELEGATING:[0-9]+]]
// INHERIT-MS: ![[DELEGATING]] = !{i64 0, ptr @"??_7VarDelegating@@6B@", null}
// INHERIT-MS-NONE-NOT: define{{.*}} @"??0VarDerived@@
// INHERIT-MS-NONE-NOT: define{{.*}} @"??0VarDelegating@@{{[^"]*}}@HZZ"
#endif

#ifdef NOVTABLE
// A novtable class leaves its vftable to a derived class, and its
// constructor, which installs none, depends on none.
struct __declspec(novtable) NoVTable {
  NoVTable();
  virtual ~NoVTable();
  virtual void f();
};
NoVTable::NoVTable() {}

// NOVTABLE-NOT: @"??_7NoVTable@@6B@"
// NOVTABLE: define {{.*}}@"??0NoVTable@@QEAA@XZ"({{.*}} #[[NOVTABLE_CTOR:[0-9]+]]
// NOVTABLE-NOT: @"??_7NoVTable@@6B@"
// NOVTABLE: attributes #[[NOVTABLE_CTOR]] = {{{.*}}"malterlib-sized"
#endif

#ifdef VECTORCALL
// A site refers to the marker of a definition whose symbol a calling
// convention decorates by that symbol.
struct VectorCall {
  __attribute__((vectorcall)) VectorCall();
  virtual __attribute__((vectorcall)) ~VectorCall();
};
[[malterlib::sized_construction]] VectorCall *makeVectorCall() {
  return new VectorCall;
}

namespace {
struct LocalVectorCall {
  LocalVectorCall() {}
  virtual __attribute__((vectorcall)) ~LocalVectorCall() {}
};
} // namespace
void *makeLocalVectorCall() { return make<LocalVectorCall>(); }

// VECTORCALL-DAG: @"\01_ZN10VectorCallC1Ev@@8.mib_sized" = external constant i8
// VECTORCALL-DAG: @"\01_ZN12_GLOBAL__N_115LocalVectorCallD0Ev@@8.mib_sized" = internal alias
#endif

#ifdef IMPLICIT
// Sema defines an implicit destructor only at the end of the translation
// unit, and the vtable that depends on it has the module emit it.
struct ImplicitBase {
  virtual ~ImplicitBase() {}
};
struct ImplicitDerived : ImplicitBase {};
[[malterlib::sized_construction]] ImplicitDerived *makeImplicit() {
  return new ImplicitDerived;
}

// A definition an owner depends on stays for the backend to name, whether this
// module defines it or link-time optimization links it in from another.
struct Declared {
  Declared();
  Declared(int);
  virtual ~Declared();
};
Declared::Declared() : Declared(1) {}

// IMPLICIT: @_ZTV15ImplicitDerived = {{.*}}!malterlib.sized.deps ![[IMPLICIT_VTABLE:[0-9]+]]
// IMPLICIT: @llvm.compiler.used = {{.*}}ptr @_ZTV8Declared{{.*}}ptr @_ZN8DeclaredC2Ei
// IMPLICIT: define linkonce_odr {{.*}}@_ZN15ImplicitDerivedD0Ev(
// IMPLICIT: ![[IMPLICIT_VTABLE]] = !{i64 0, ptr @_ZN15ImplicitDerivedD0Ev, null}
#endif
