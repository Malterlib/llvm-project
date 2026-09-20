// RUN: %clang_cc1 -std=c++20 -triple x86_64-linux-gnu -fno-rtti -fmalterlib-sized-destructors -emit-llvm -o - %s | FileCheck %s --check-prefixes=CHECK,ITANIUM
// RUN: %clang_cc1 -std=c++20 -triple x86_64-windows-msvc -fms-extensions -fno-rtti -fmalterlib-sized-destructors -emit-llvm -o - %s | FileCheck %s --check-prefixes=CHECK,MS

using Size = decltype(sizeof(0));
void *operator new(Size, void *p) noexcept { return p; }
void operator delete(void *) noexcept {}
void operator delete(void *, Size) noexcept {}
void inspect(void *);

// Each Microsoft vftable records the slots of its own layout only: the left
// one the deleting destructor, the right one its adjustor thunk.
struct L { virtual ~L() {} int l; };
struct R { virtual ~R() {} int r; };
struct D : L, R { D() {} ~D() override { inspect(this); } };

[[malterlib::sized_construction]] D *make(void *p) { return new (p) D; }

Size destroy(L *p, void **memory) {
  return __builtin_malterlib_destroy(p, memory);
}

// MS: @"??_7D@@6BL@@@" = {{.*}} !malterlib.sized.deps ![[LEFT:[0-9]+]]
// MS: @"??_7D@@6BR@@@" = {{.*}} !malterlib.sized.deps ![[RIGHT:[0-9]+]]

// The stores of the vtable pointers a destructor makes while it destroys are
// told apart from those of a construction, which a site inlined in a
// destructor makes.
// ITANIUM-LABEL: define linkonce_odr void @_ZN1DC2Ev(
// MS-LABEL: define linkonce_odr dso_local noundef ptr @"??0D@@QEAA@XZ"(
// CHECK-NOT: destructor-vptr
// CHECK: ret
// ITANIUM-LABEL: define linkonce_odr void @_ZN1DD2Ev(
// MS-LABEL: define linkonce_odr dso_local void @"??1D@@UEAA@XZ"(
// CHECK: store ptr {{.*}}, !malterlib.sized.destructor-vptr ![[NONE:[0-9]+]]
// CHECK: ret

// MS-DAG: ![[LEFT]] = !{i64 0, ptr @"??_ED@@UEAAPEAXI@Z", null}
// MS-DAG: ![[RIGHT]] = !{i64 0, ptr @"??_ED@@WBA@EAAPEAXI@Z", null}
// CHECK-DAG: ![[NONE]] = !{}
