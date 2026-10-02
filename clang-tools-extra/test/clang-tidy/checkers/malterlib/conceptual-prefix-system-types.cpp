// RUN: %check_clang_tidy -std=c++20-or-later %s malterlib-conceptual-prefix %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -I %S/Inputs

#include "conceptual-prefix-system.h"

// A type declared in a system header has a name of its own library, which
// isn't named as a function type by its `F` prefix, nor is one the options
// list in NotFunctionTypes.
FILETIME g_Time;
FSRef g_Ref;
FSystemAlias<int> g_Aliased;
void fg_Times(FILETIME const &_Time, FSRef *_pRef);

typedef struct CNotFunctionStruct
{
	int m_Value;
} FNotFunctionStruct;
FNotFunctionStruct g_NotFunction;

// One declared here is.
using FCallback = void (*)(int _Value);
FCallback g_Callback;
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: 'g_Callback' is a function object and should have the 'f' concept prefix after its storage prefix [malterlib-conceptual-prefix]
typedef struct CFunctionStruct
{
	int m_Value;
} FFunctionStruct;
FFunctionStruct g_FunctionStruct;
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: 'g_FunctionStruct' is a function object and should have the 'f' concept prefix after its storage prefix [malterlib-conceptual-prefix]

// A qualified one of NotFunctionTypes is only the one it names.
namespace NLibrary
{
	struct CForeign
	{
		int m_Value;
	};
	using FForeign = CForeign;
}
namespace NOther
{
	struct CForeign
	{
		int m_Value;
	};
	using FForeign = CForeign;
}
NLibrary::FForeign g_Foreign;
NOther::FForeign g_OtherForeign;
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: 'g_OtherForeign' is a function object and should have the 'f' concept prefix after its storage prefix [malterlib-conceptual-prefix]
