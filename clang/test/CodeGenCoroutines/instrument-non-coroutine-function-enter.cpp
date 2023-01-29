// RUN: %clang_cc1 -std=c++20 -triple x86_64-linux-gnu -emit-llvm -disable-llvm-passes -o - %s | FileCheck %s
// RUN: %clang_cc1 -std=c++20 -triple x86_64-linux-gnu -emit-llvm -disable-llvm-passes -o - %s | FileCheck %s --check-prefix=ENTRY

#include "Inputs/coroutine.h"

// A function that returns a class with the attribute by value and isn't a
// coroutine calls the function the attribute names when it is entered.

struct [[clang::instrument_non_coroutine_function_enter("fg_Inspect")]] CTesting {
  struct promise_type {
    CTesting get_return_object() { return {}; }
    std::suspend_never initial_suspend() noexcept { return {}; }
    std::suspend_never final_suspend() noexcept { return {}; }
    void return_void() {}
    void unhandled_exception() {}
  };
};

CTesting fg_Plain() { return {}; }
// CHECK: define{{.*}} void @_Z8fg_Plainv() #[[INSTRUMENTED:[0-9]+]]

[[clang::instrument_non_coroutine_function_enter_disable]] CTesting fg_Disabled() { return {}; }
// CHECK: define{{.*}} void @_Z11fg_Disabledv() #[[PLAIN:[0-9]+]]

// Also where a declaration before the definition gives its function type the
// attribute, which the definition's type doesn't have.
CTesting fg_DisabledInDeclaration()
    [[clang::instrument_non_coroutine_function_enter_disable]];
CTesting fg_DisabledInDeclaration() { return {}; }
// CHECK: define{{.*}} void @_Z24fg_DisabledInDeclarationv() #[[PLAIN]]

CTesting fg_Coroutine() { co_return; }
// CHECK: define{{.*}} void @_Z12fg_Coroutinev() #[[COROUTINE:[0-9]+]]

int fg_Other() { return 0; }
// CHECK: define{{.*}} i32 @_Z8fg_Otherv() #[[PLAIN]]

void fg_Lambdas() {
  auto fInstrumented = []() -> CTesting { return {}; };
  auto fDisabled = [] [[clang::instrument_non_coroutine_function_enter_disable]] () -> CTesting { return {}; };
  fInstrumented();
  fDisabled();
}
// CHECK: define{{.*}} void @"_ZZ10fg_LambdasvENK3$_0clEv"({{.*}}) #[[INSTRUMENTED]]
// CHECK: define{{.*}} void @"_ZZ10fg_LambdasvENK3$_1clEv"({{.*}}) #[[PLAIN]]

// CHECK-DAG: attributes #[[INSTRUMENTED]] = {{{.*}}"instrument-function-entry"="fg_Inspect"
// CHECK-DAG: attributes #[[PLAIN]] = {
// CHECK-DAG: attributes #[[COROUTINE]] = {

// Only that group instruments the entry, not those of the coroutine and the
// other functions.
// ENTRY: "instrument-function-entry"
// ENTRY-NOT: "instrument-function-entry"
