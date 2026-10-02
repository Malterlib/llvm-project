// RUN: %python -c "print('template <typename... tp_CTypes> struct TCChoice;\n' + ''.join('template <typename t_FCallback> using TCLevel%%d = typename TCChoice<%%s>::CType;\n' %% (i, ', '.join(['t_FCallback'] + ['t_FCallback' if i == 1 else 'TCLevel%%d<t_FCallback>' %% (i - 1)] * 200)) for i in range(1, 5)) + 'template <typename tf_FCallback> void fg_Run(TCLevel4<tf_FCallback> _fValue);\nint g_fWrong;')" > %t.cpp
// RUN: clang-tidy %t.cpp '-checks=-*,malterlib-conceptual-prefix' -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -std=c++20 2>&1 | FileCheck %s

// Aliases that give one of their arguments, each given the one before 200
// times, are asked about each type once at each depth (round 221).
// CHECK-NOT: '_fValue'
// CHECK: warning: 'g_fWrong' has the 'f' concept prefix
