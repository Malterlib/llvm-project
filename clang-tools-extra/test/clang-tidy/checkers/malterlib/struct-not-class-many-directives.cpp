// RUN: %python %S/Inputs/many-conditional-records.py 65536 > %t.cpp
// RUN: clang-tidy --checks='-*,malterlib-struct-not-class' --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" %t.cpp -- -std=c++20 2>&1 | FileCheck --allow-empty %s

// The conditional directives of a translation unit with many structs, each of
// which has one before its 'public:' section, are found by a binary search of
// those the preprocessor reported, in the order of the translation unit, in
// time linear in the number of structs, not by going through them for each.

// CHECK-NOT: warning:
