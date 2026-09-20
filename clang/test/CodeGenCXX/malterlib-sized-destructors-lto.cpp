// RUN: %clang_cc1 -std=c++20 -triple x86_64-pc-windows-msvc -fno-rtti -fmalterlib-sized-destructors -flto=thin -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -std=c++20 -triple x86_64-pc-windows-msvc -fno-rtti -fmalterlib-sized-destructors -flto=full -emit-llvm -o - %s | FileCheck %s

// Link-time optimization compiles the code after the linker resolved its
// symbols. lld-link resolves the vector deleting destructor, a weak alias of
// the scalar one, late, as it does a weak alias in an object, and the
// optimization leaves the alias declared. So the scalar destructor names the
// alias's marker, which the code generator makes of the scalar destructor's,
// and the vftable's dependency on the alias names no marker of its own.

void *operator new(decltype(sizeof(0)), void *memory) noexcept;

struct Object {
  virtual ~Object() {}
};

[[malterlib::sized_construction]] Object *make(void *memory) {
  return new (memory) Object;
}

// CHECK-DAG: @"??_7Object@@6B@" = {{.*}}, !malterlib.sized.deps ![[VFTABLE:[0-9]+]] #[[MARKED:[0-9]+]]
// CHECK-DAG: @__mib_sized_kept = internal constant [2 x ptr] [ptr @"??_7Object@@6B@", ptr @"??_EObject@@UEAAPEAXI@Z"], section "llvm.metadata"
// CHECK-DAG: @llvm.compiler.used = appending global [2 x ptr] [ptr @__mib_sized_ref, ptr @__mib_sized_kept]
// CHECK-DAG: @"??_EObject@@UEAAPEAXI@Z" = weak dso_local unnamed_addr alias %struct.__malterlib_destroy_result (ptr, i32), ptr @"??_GObject@@UEAAPEAXI@Z"
// CHECK-DAG: define linkonce_odr {{.*}} @"??_GObject@@UEAAPEAXI@Z"({{.*}} #[[SCALAR:[0-9]+]] comdat
// CHECK-DAG: attributes #[[MARKED]] = { "malterlib-sized" }
// CHECK-DAG: attributes #[[SCALAR]] = { {{.*}}"malterlib-sized" "malterlib-sized-aliases"="??_EObject@@UEAAPEAXI@Z=0"
// CHECK-DAG: ![[VFTABLE]] = !{i64 0, ptr @"??_EObject@@UEAAPEAXI@Z", null}
