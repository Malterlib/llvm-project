// RUN: %python -c "print('void fg_Run()\n{\n' + '[]{\n' * 30 + 'int wrong = 0; (void)wrong;\nstruct Bad {};\n' + '}();\n' * 30 + '}')" > %t.cpp
// RUN: clang-tidy %t.cpp '-checks=-*,malterlib-variable-naming,malterlib-type-naming' -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -std=c++20 2>&1 | FileCheck %s

// A diagnostic naming a declaration in 30 nested lambdas formats each enclosing
// function once (round 221: twice per level before, 2^30).
// CHECK: warning: {{.*}}'wrong'{{.*}} [malterlib-variable-naming]
// CHECK: warning: {{.*}}'Bad'{{.*}} [malterlib-type-naming]
