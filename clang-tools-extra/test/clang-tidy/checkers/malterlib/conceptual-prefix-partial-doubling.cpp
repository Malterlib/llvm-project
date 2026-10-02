// RUN: %python -c "print('template <typename t_CFirst, typename t_CSecond> struct TCPair {};\ntemplate <typename t_CFirst, typename t_CSecond> struct TCBox;\ntemplate <typename t_CType> struct TCBox<t_CType, t_CType> { int f_Get(); };\ntemplate <typename tf_CType> TCBox<tf_CType, tf_CType> fg_Make(tf_CType *);\nusing CLevel0 = int;\n' + ''.join('using CLevel%d = TCPair<CLevel%d, CLevel%d>;\n' % (i, i - 1, i - 1) for i in range(1, 27)) + 'void fg_Run()\n{\n\tauto Box = fg_Make(static_cast<CLevel26 *>(nullptr));\n\tauto Value = Box.f_Get();\n\tauto pValue = Box.f_Get();\n\t(void)Value;\n\t(void)pValue;\n}')" > %t.cpp
// RUN: clang-tidy %t.cpp '-checks=-*,malterlib-conceptual-prefix' -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -std=c++20 2>&1 | FileCheck %s

// A partial specialization's parameter bound twice to a deduced type of 2^26
// parts is compared by the aliases at its top, not by printing it (round 221).
// CHECK: warning: 'pValue' has the 'p' concept prefix of a pointer, but is not one [malterlib-conceptual-prefix]
