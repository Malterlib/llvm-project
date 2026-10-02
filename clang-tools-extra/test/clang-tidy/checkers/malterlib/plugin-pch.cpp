// The definitions a compile instantiates of the templates of a precompiled
// header are checked there, which the precompiled header couldn't, since it
// didn't know what they are instantiated with, also of a member defined
// outside of its class, and of a variable template, and the members of a
// specialization of a class template whose types depend on what it is made
// with, also in an anonymous member union, and its non-type parameters of
// such types, and of its member functions, also those whose return types
// depend on them, also of a specialization the header only declared, and the
// non-type parameters of such types of function and variable templates. Not what an explicit instantiation declaration,
// `extern template`, says another file makes, which is checked there, but a
// specialization of a member template of it, which that file doesn't make.
// REQUIRES: malterlib-plugin
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -std=c++20 -x c++-header -fplugin-arg-malterlib-checks='-*,malterlib-coroutine-reference-parameter,malterlib-conceptual-prefix' '-fplugin-arg-malterlib-header-filter=plugin-pch\.h' -I %S/Inputs %S/Inputs/plugin-pch.h -o %t.pch 2>&1 | count 0
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -std=c++20 -fsyntax-only -fplugin-arg-malterlib-checks='-*,malterlib-coroutine-reference-parameter,malterlib-conceptual-prefix' '-fplugin-arg-malterlib-header-filter=plugin-pch\.h' -I %S/Inputs -include-pch %t.pch %s 2>&1 | FileCheck --check-prefix=CHECK-USE --implicit-check-not=warning: --implicit-check-not=error: %s
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -std=c++20 -fsyntax-only -fplugin-arg-malterlib-checks='-*,malterlib-coroutine-reference-parameter,malterlib-conceptual-prefix' '-fplugin-arg-malterlib-header-filter=plugin-pch\.h' -I %S/Inputs %s 2>&1 | FileCheck --check-prefix=CHECK-USE --implicit-check-not=warning: --implicit-check-not=error: %s
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -std=c++20 -fsyntax-only -DINSTANCE -fplugin-arg-malterlib-checks='-*,malterlib-coroutine-reference-parameter,malterlib-conceptual-prefix' '-fplugin-arg-malterlib-header-filter=plugin-pch\.h' -I %S/Inputs -include-pch %t.pch %s 2>&1 | FileCheck --check-prefix=CHECK-INSTANCE --implicit-check-not=warning: --implicit-check-not=error: %s
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -std=c++20 -fsyntax-only -DINSTANCE -fplugin-arg-malterlib-checks='-*,malterlib-coroutine-reference-parameter,malterlib-conceptual-prefix' '-fplugin-arg-malterlib-header-filter=plugin-pch\.h' -I %S/Inputs %s 2>&1 | FileCheck --check-prefix=CHECK-INSTANCE --implicit-check-not=warning: --implicit-check-not=error: %s

// CHECK-USE-DAG: plugin-pch.h:7:35: warning: TCFuture coroutine 'fg_PchRead' uses '_Value' after a suspension{{.*}}[malterlib-coroutine-reference-parameter]
// CHECK-USE-DAG: plugin-pch.h:16:34: warning: TCFuture coroutine 'f_InClass' uses '_Value' after a suspension{{.*}}[malterlib-coroutine-reference-parameter]
// CHECK-USE-DAG: plugin-pch.h:21:36: warning: TCFuture coroutine 'f_OutOfLine' uses '_Value' after a suspension{{.*}}[malterlib-coroutine-reference-parameter]
// CHECK-USE-DAG: plugin-pch.h:40:34: warning: TCFuture coroutine 'f_ReadAs' uses '_Value' after a suspension{{.*}}[malterlib-coroutine-reference-parameter]
// CHECK-USE-DAG: plugin-pch.h:50:13: warning: 'g_pPchValue' has the 'p' concept prefix of a pointer, but is not one{{.*}}[malterlib-conceptual-prefix]
// CHECK-USE-DAG: plugin-pch.h:60:{{[0-9]+}}: warning: 'm_pValue' has the 'p' concept prefix of a pointer, but is not one{{.*}}[malterlib-conceptual-prefix]
// CHECK-USE-DAG: plugin-pch.h:73:{{[0-9]+}}: warning: 'm_pValue' has the 'p' concept prefix of a pointer, but is not one{{.*}}[malterlib-conceptual-prefix]
// CHECK-USE-DAG: plugin-pch.h:76:{{[0-9]+}}: warning: 't_fFlag' has the 'f' concept prefix of a function object, but is not one{{.*}}[malterlib-conceptual-prefix]
// CHECK-USE-DAG: plugin-pch.h:101:{{[0-9]+}}: warning: 'm_pValue' has the 'p' concept prefix of a pointer, but is not one{{.*}}[malterlib-conceptual-prefix]
// CHECK-USE-DAG: plugin-pch.h:110:{{[0-9]+}}: warning: '_pValue' has the 'p' concept prefix of a pointer, but is not one{{.*}}[malterlib-conceptual-prefix]
// CHECK-USE-DAG: plugin-pch.h:117:{{[0-9]+}}: warning: 'ms_pValue' has the 'p' concept prefix of a pointer, but is not one{{.*}}[malterlib-conceptual-prefix]
// CHECK-USE-DAG: plugin-pch.h:92:{{[0-9]+}}: warning: 'ms_pValue' has the 'p' concept prefix of a pointer, but is not one{{.*}}[malterlib-conceptual-prefix]
// CHECK-USE-DAG: plugin-pch.h:125:{{[0-9]+}}: warning: '_pValue' has the 'p' concept prefix of a pointer, but is not one{{.*}}[malterlib-conceptual-prefix]
// CHECK-USE-DAG: plugin-pch.h:142:{{[0-9]+}}: warning: 'm_pValue' has the 'p' concept prefix of a pointer, but is not one{{.*}}[malterlib-conceptual-prefix]
// CHECK-USE-DAG: plugin-pch.h:136:{{[0-9]+}}: warning: '_fReady' has the 'f' concept prefix of a function object, but is not one{{.*}}[malterlib-conceptual-prefix]
// CHECK-USE-DAG: plugin-pch.h:150:{{[0-9]+}}: warning: '_pValue' has the 'p' concept prefix of a pointer, but is not one{{.*}}[malterlib-conceptual-prefix]
// CHECK-USE-DAG: plugin-pch.h:156:{{[0-9]+}}: warning: 'tf_fFlag' has the 'f' concept prefix of a function object, but is not one{{.*}}[malterlib-conceptual-prefix]
// CHECK-USE-DAG: plugin-pch.h:161:{{[0-9]+}}: warning: 't_fFlag' has the 'f' concept prefix of a function object, but is not one{{.*}}[malterlib-conceptual-prefix]
// CHECK-USE-DAG: plugin-pch.h:168:{{[0-9]+}}: warning: '_fDone' has the 'f' concept prefix of a function object, but is not one{{.*}}[malterlib-conceptual-prefix]
// CHECK-USE-DAG: plugin-pch.h:178:{{[0-9]+}}: warning: 'm_pValue' has the 'p' concept prefix of a pointer, but is not one{{.*}}[malterlib-conceptual-prefix]
// CHECK-INSTANCE: plugin-pch.h:34:31: warning: TCFuture coroutine 'f_Read' uses '_Value' after a suspension{{.*}}[malterlib-coroutine-reference-parameter]

#include "plugin-pch.h"

#ifdef INSTANCE
template struct TCPchExtern<int &>;
#else
TCFuture<int> fg_Use(int &_Value)
{
	TCPchBox<int &> Box;
	TCPchExtern<int &> Extern;
	(void)Box.f_InClass(_Value);
	(void)Box.f_OutOfLine(_Value);
	(void)Extern.f_Read(_Value);
	(void)Extern.f_ReadAs<int &>(_Value);
	fg_PchFlag<CPchBoolTraits, true>();
	return fg_PchRead<int &>(_Value);
}

int g_PchResult = g_pPchValue<int>;
TCPchField<CPchNumberTraits> g_PchField;
TCPchUnion<CPchNumberTraits> g_PchUnion;
TCPchFlag<CPchBoolTraits, true> g_PchFlag;
TCPchGeneric<bool> g_PchGeneric;
TCPchStatic<CPchNumberTraits> g_PchStatic;
TCPchOuter<CPchNumberTraits>::CInner g_PchInner;
TCPchMethod<CPchNumberTraits> g_PchMethod;
int g_PchNested = TCPchNested<CPchNumberTraits>::CInner::ms_pValue;
TCPchInitializer<CPchNumberTraits> g_PchInitializer;
TCPchReturn<CPchBoolTraits> g_PchReturn;
TCPchDeclared<CPchNumberTraits> g_PchDeclared;
static_assert(requires { fg_PchSignature<CPchNumberTraits>(0); });
int g_PchFlagUse = g_PchFlagValue<CPchBoolTraits, true>;
TCPchDefaulted<CPchBoolTraits> g_PchDefaulted;
extern template struct TCPchExternField<CPchNumberTraits>;
#endif
