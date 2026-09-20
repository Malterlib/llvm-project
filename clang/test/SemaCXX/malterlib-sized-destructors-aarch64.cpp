// RUN: %clang_cc1 -std=c++20 -triple aarch64-linux-gnu -fmalterlib-sized-destructors -fsyntax-only -verify %s
// expected-no-diagnostics

// The preserving calling conventions keep none of the registers a sized
// deleting destructor returns its object and size in on AArch64.

struct PreserveMost {
  __attribute__((preserve_most)) virtual ~PreserveMost();
};

struct PreserveAll {
  __attribute__((preserve_all)) virtual ~PreserveAll();
};
