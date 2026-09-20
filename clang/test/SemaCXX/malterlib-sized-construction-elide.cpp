// Before C++17 a copy or move from a call's result is elided, and the call
// constructs the object, with the vtable of its own image.
// RUN: %clang_cc1 -std=c++14 -triple x86_64-linux-gnu -fmalterlib-sized-destructors -fsyntax-only -verify %s
// RUN: %clang_cc1 -std=c++14 -triple x86_64-linux-gnu -fmalterlib-sized-destructors -fno-elide-constructors -fsyntax-only -verify=noelide %s
// noelide-no-diagnostics

struct Moved {
  virtual ~Moved();
  Moved();
  Moved(Moved &&);
};
Moved produceMoved();

[[malterlib::sized_construction]] Moved *constructMoved() {
  Moved local;
  new Moved(static_cast<Moved &&>(local));
  new Moved(Moved());
  return new Moved(produceMoved()); // expected-error {{a function with the 'malterlib::sized_construction' attribute cannot construct 'Moved' from a value another function produces}}
}
