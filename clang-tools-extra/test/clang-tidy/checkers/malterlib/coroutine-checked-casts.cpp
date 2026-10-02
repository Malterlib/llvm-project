// RUN: %check_clang_tidy -std=c++20-or-later %s malterlib-runs-to-completion,malterlib-coroutine-reference-capture,malterlib-coroutine-reference-parameter %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -I %S/Inputs

// Where a check narrows a declaration, an expression or a type to a kind, the
// kinds that look like it and aren't: a captured structured binding is a
// BindingDecl, not a variable. Each is read like what it stands for.

#include "malterlib-runs-to-completion.h"

struct CPair
{
	int m_First;
	int m_Second;
};

DMibRunsToCompletion TCFuture<int> fg_Step();

// A lambda held in a local whose calls are all awaited runs in its context,
// with a binding captured as with a variable.
DMibRunsToCompletion TCFuture<int> fg_JoinedBinding()
{
	auto [First, Second] = CPair{1, 2};
	auto fRun = [First, &Second, Copy = First]() -> TCFuture<int>
		{
			co_await fg_Step();
			co_return First + Second + Copy;
		}
	;
	co_return co_await fRun();
}

DMibRunsToCompletion TCFuture<int> fg_JoinedVariable()
{
	int First = 1;
	int Second = 2;
	auto fRun = [First, &Second, Copy = First]() -> TCFuture<int>
		{
			co_await fg_Step();
			co_return First + Second + Copy;
		}
	;
	co_return co_await fRun();
}

DMibRunsToCompletion TCFuture<int> fg_NotJoinedBinding()
{
	auto [First, Second] = CPair{1, 2};
	auto fRun = [&First, Second]() -> TCFuture<int>
		{
// CHECK-MESSAGES: :[[@LINE-2]]:16: warning: a TCFuture coroutine lambda uses 'First', which it captures by reference, after a suspension, {{.*}}
			co_await fg_Step();
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'fg_Step' runs to completion, but is called here where the code doesn't, {{.*}}
			co_return First + Second;
		}
	;
	fRun();
	co_return 0;
}

DMibRunsToCompletion TCFuture<int> fg_NotJoinedVariable()
{
	int First = 1;
	int Second = 2;
	auto fRun = [&First, Second]() -> TCFuture<int>
		{
// CHECK-MESSAGES: :[[@LINE-2]]:16: warning: a TCFuture coroutine lambda uses 'First', which it captures by reference, after a suspension, {{.*}}
			co_await fg_Step();
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'fg_Step' runs to completion, but is called here where the code doesn't, {{.*}}
			co_return First + Second;
		}
	;
	fRun();
	co_return 0;
}

// The last statement of a statement expression is its value, which the
// compound statement it is in gives on without being an expression; in any
// other compound statement it is dropped.
namespace NStatementExpressions
{
	TCFuture<int> fg_Work();
	struct CStrSpan
	{
		char const *m_pData;
		unsigned long m_Length;
	};
	struct CSourceStrSpan
	{
		CStrSpan m_Text;
		unsigned long m_Line;
	};

	TCFuture<unsigned long> fg_Line(CSourceStrSpan _Source)
	{
		co_await fg_Work();
		co_return ({ _Source.m_Line; });
	}
	TCFuture<CStrSpan> fg_Text(CSourceStrSpan _Source)
// CHECK-MESSAGES: :[[@LINE-1]]:44: warning: TCFuture coroutine 'fg_Text' uses '_Source' after a suspension, {{.*}}
	{
		co_await fg_Work();
		co_return ({ _Source.m_Text; });
	}
	TCFuture<int> fg_Dropped(CSourceStrSpan _Source)
	{
		co_await fg_Work();
		{
			_Source.m_Text;
		}
		co_return 0;
	}
}
