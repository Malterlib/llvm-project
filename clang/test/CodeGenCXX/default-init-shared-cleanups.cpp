// RUN: %clang_cc1 -triple x86_64-linux-gnu -std=c++20 -emit-llvm -o - %s | FileCheck %s

// The uses of a default member initializer, or of a default argument, with a
// source location in it that a use rewrites, share the rewrite for the same
// outer use, like the members `first` and `second` of one aggregate
// initialization. Each use is its own full expression, or is in one, which
// destroys the temporaries of the rewrite at its end, each time.

int destroyed;
struct Temporary {
  ~Temporary() { ++destroyed; }
};
int consume(Temporary const &, int) { return 0; }
int byValue(Temporary, int) { return 0; }
struct Member {
  int value = consume(Temporary{}, __builtin_LINE());
  int other = byValue(Temporary{}, __builtin_LINE());
};
struct Object {
  Member first, second;
};

int consumeDefault(Temporary const & = Temporary{}, int = __builtin_LINE()) {
  return 0;
}
struct WithDefault {
  int value = consumeDefault();
};
struct Pair {
  WithDefault first, second;
};

// CHECK-LABEL: define {{.*}} @_Z6objectv(
// CHECK-COUNT-4: call void @_ZN9TemporaryD{{[12]}}Ev
// CHECK: load i32, ptr @destroyed
int object() {
  Object object{};
  return destroyed;
}

// CHECK-LABEL: define {{.*}} @_Z4pairv(
// CHECK-COUNT-2: call void @_ZN9TemporaryD{{[12]}}Ev
// CHECK: load i32, ptr @destroyed
int pair() {
  Pair pair{};
  return destroyed;
}

// So do the uses of one whose temporary is read, which nothing refers to
// after, and which is rewritten for its temporaries.
struct Readable {
  int value = 1;
  ~Readable() { ++destroyed; }
};
struct ReadMember {
  int value = Readable{}.value;
};
struct ReadPair {
  ReadMember first, second;
};

// CHECK-LABEL: define {{.*}} @_Z8readPairv(
// CHECK-COUNT-2: call void @_ZN8ReadableD{{[12]}}Ev
// CHECK: load i32, ptr @destroyed
int readPair() {
  ReadPair pair{};
  return destroyed;
}
