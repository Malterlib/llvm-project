// RUN: %clang_cc1 -std=c++20 -triple x86_64-linux-gnu -emit-llvm -o - %s | FileCheck %s

// A call with a default argument or member initializer that is rewritten for
// where it is used, for the immediate invocations and source locations in it,
// is rewritten again where Sema builds the call again, like the inner calls
// of an immediate invocation it rebuilds when a function has more than one:
// each has the location of its own use.

consteval int ce(int v) { return v; }
consteval int id(int x) { return x; }
constexpr int pf(int v = ce(__builtin_LINE())) { return v; }
struct S {
  int a = ce(__builtin_LINE());
};
constexpr int f0(int v = ce(__builtin_LINE())) { return v; }
constexpr int f1(int v = f0() + 1000) { return v; }
void use(int);

// CHECK-LABEL: define{{.*}} void @_Z4testv()
void test() {
  // CHECK: call void @_Z3usei(i32 noundef [[#@LINE+1]])
  use(id(pf()));
  // CHECK: call void @_Z3usei(i32 noundef [[#@LINE+1]])
  use(id(pf()) + 0);
  // CHECK: call void @_Z3usei(i32 noundef [[#@LINE+1]])
  use(id(S{}.a));
  // CHECK: call void @_Z3usei(i32 noundef [[#@LINE+1000+1]])
  use(id(f1()));
}
