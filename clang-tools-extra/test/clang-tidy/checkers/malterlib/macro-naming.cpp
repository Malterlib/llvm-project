// RUN: %check_clang_tidy %s malterlib-macro-naming %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}"

#define DMacro 1
#define DFunctionLike(d_Value, d_bFlag, d_CType, ...) d_Value
#define inline_always inline
// A name in capitals is no upper camel case, also of one that configures
// external code, which has a NOLINT, nor after the prefix, nor are its
// parameters.
#define NOMINMAX
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: macro 'NOMINMAX' should have the 'D' prefix followed by upper camel case [malterlib-macro-naming]
#define NOMINMAX_ALLOWED // NOLINT(malterlib-macro-naming)
#define identity(value) (value)
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: parameter 'value' of macro 'identity' should have the 'd_' prefix followed by upper camel case [malterlib-macro-naming]
#define API_CALL(x) x
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: macro 'API_CALL' should have the 'D' prefix followed by upper camel case [malterlib-macro-naming]
// CHECK-MESSAGES: :[[@LINE-2]]:18: warning: parameter 'x' of macro 'API_CALL' should have the 'd_' prefix followed by upper camel case [malterlib-macro-naming]
#define _GNU_SOURCE
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: macro '_GNU_SOURCE' should have the 'D' prefix followed by upper camel case [malterlib-macro-naming]
#define AVLKEY(x) (x->m_Data)
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: macro 'AVLKEY' should have the 'D' prefix followed by upper camel case [malterlib-macro-naming]
// CHECK-MESSAGES: :[[@LINE-2]]:16: warning: parameter 'x' of macro 'AVLKEY' should have the 'd_' prefix followed by upper camel case [malterlib-macro-naming]
#define DESKTOP_ALL 1
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: macro 'DESKTOP_ALL' should have the 'D' prefix followed by upper camel case [malterlib-macro-naming]
#define DTypeOf(d_TYPE, d_ID) d_TYPE
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: parameter 'd_TYPE' of macro 'DTypeOf' should have the 'd_' prefix followed by upper camel case [malterlib-macro-naming]
#define DMibID 1
#define DID 1

#define Macro 1
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: macro 'Macro' should have the 'D' prefix followed by upper camel case [malterlib-macro-naming]
#define Dlower 1
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: macro 'Dlower' should have the 'D' prefix followed by upper camel case [malterlib-macro-naming]
#define DParameters(_Value, d_value, Value, d_Good_bad)
// Each parameter is reported where it is declared.
// CHECK-MESSAGES: :[[@LINE-2]]:21: warning: parameter '_Value' of macro 'DParameters' should have the 'd_' prefix followed by upper camel case [malterlib-macro-naming]
// CHECK-MESSAGES: :[[@LINE-3]]:29: warning: parameter 'd_value' of macro 'DParameters' should have the 'd_' prefix followed by upper camel case [malterlib-macro-naming]
// CHECK-MESSAGES: :[[@LINE-4]]:38: warning: parameter 'Value' of macro 'DParameters' should have the 'd_' prefix followed by upper camel case [malterlib-macro-naming]
// CHECK-MESSAGES: :[[@LINE-5]]:45: warning: parameter 'd_Good_bad' of macro 'DParameters' should have the 'd_' prefix followed by upper camel case [malterlib-macro-naming]
#define DGood_bad 1
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: macro 'DGood_bad' should have the 'D' prefix followed by upper camel case [malterlib-macro-naming]
#define DGroup_Value 1

#define NoLint 1 // NOLINT(malterlib-macro-naming)

// Comments between the parameters are skipped.
#define DCommented(/* Wrong */ Wrong) Wrong
// CHECK-MESSAGES: :[[@LINE-1]]:32: warning: parameter 'Wrong' of macro 'DCommented' should have the 'd_' prefix followed by upper camel case [malterlib-macro-naming]
#define DParenthesized(/* ) */ Wrong) Wrong
// CHECK-MESSAGES: :[[@LINE-1]]:32: warning: parameter 'Wrong' of macro 'DParenthesized' should have the 'd_' prefix followed by upper camel case [malterlib-macro-naming]

// A name doesn't end with an underscore.
#define DMacro_ 0
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: macro 'DMacro_' should have the 'D' prefix followed by upper camel case [malterlib-macro-naming]

// A group can be a built-in type of Malterlib, which is lower case.
#define DMibPCanDo_fp32 1
#define DCompiler_clang 1
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: macro 'DCompiler_clang' should have the 'D' prefix followed by upper camel case [malterlib-macro-naming]

// A parameter spelled across a line splice.
#define DSpliced( \
	Wro\
ng) Wrong
// CHECK-MESSAGES: :[[@LINE-2]]:2: warning: parameter 'Wrong' of macro 'DSpliced' should have the 'd_' prefix followed by upper camel case [malterlib-macro-naming]

// A parameter after a line splice is reported where its name is.
#define DRound64(\
Wrong) Wrong
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: parameter 'Wrong' of macro 'DRound64' should have the 'd_' prefix followed by upper camel case [malterlib-macro-naming]

// The types of the language are built-in types.
#define DRound99PBits_int 32
#define DRound99Flag_bool 1

// Round 123: the types of the language with an underscore are built-in types.
#define DRound123PBits_char8_t 8
#define DRound123PBits_wchar_t_Size 4
#define DRound123PBits_char8_tSize 8
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: macro 'DRound123PBits_char8_tSize' should have the 'D' prefix followed by upper camel case [malterlib-macro-naming]

// Round 332: a parameter is at least as long as the minimum after its prefix.
#define DRound332(d_X, d_Value) d_X + d_Value
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: parameter 'd_X' of macro 'DRound332' should have at least 2 characters after its prefixes [malterlib-macro-naming]
// Also after a type prefix.
#define DRound333(d_CX, d_ICX, d_CType) d_CType
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: parameter 'd_CX' of macro 'DRound333' should have at least 2 characters after its prefixes [malterlib-macro-naming]
// CHECK-MESSAGES: :[[@LINE-2]]:25: warning: parameter 'd_ICX' of macro 'DRound333' should have at least 2 characters after its prefixes [malterlib-macro-naming]
