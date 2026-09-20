// RUN: %clang_cc1 -std=c++20 -triple x86_64-linux-gnu -fmalterlib-sized-destructors -emit-llvm-only -verify %s

// A default argument or a default member initializer is checked where a sized
// construction site evaluates it, not where it appears.

struct A {
  virtual ~A();
};
A produce();

void sink(A * = new A(produce())); // expected-error {{a function with the 'malterlib::sized_construction' attribute cannot construct 'A' from a value another function produces}}
void plainSink(A * = new A);

struct Holder {
  A *p = new A(produce()); // expected-error {{a function with the 'malterlib::sized_construction' attribute cannot construct 'A' from a value another function produces}}
};

[[malterlib::sized_construction]] void sizedSink() {
  sink();
  plainSink();
}

[[malterlib::sized_construction]] void sizedHolder() {
  Holder h{};
}

void unsized() {
  sink();
  Holder h{};
}
