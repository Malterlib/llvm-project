// RUN: %check_clang_tidy -std=c++20-or-later %s malterlib-coroutine-lambda-call %t -- \
// RUN:   -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml', malterlib-coroutine-lambda-call.SafeCallees: '-*;CSafeWrapper;fg_Keep'}}" \
// RUN:   -- -I %S/Inputs

#include "malterlib-concurrency.h"

TCFuture<int> fg_Suspend();

struct CSafeWrapper
{
	template <typename t_FFunction>
	CSafeWrapper(t_FFunction &&_fFunction)
	{
	}
};

template <typename t_FFunction>
void fg_Keep(t_FFunction &&_fFunction)
{
}

void fg_Wrappers(int _Value)
{
	CSafeWrapper Copied = [_Value]() -> TCFuture<int>
		{
			co_await fg_Suspend();
			co_return _Value;
		}
	;
	CSafeWrapper Braced{[_Value]() -> TCFuture<int>
		{
			co_await fg_Suspend();
			co_return _Value;
		}};
	fg_Keep
		(
			[_Value]() -> TCFuture<int>
			{
				co_await fg_Suspend();
				co_return _Value;
			}
		)
	;
	// The option replaces the default list, also fg_CallSafe, which calls the
	// closure right away where it is one of them.
	fg_CallSafe
		(
			[_Value]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-3]]:2: warning: the closure of a lambda coroutine that uses its captures after a suspension is given to 'fg_CallSafe', which can call it directly; {{.*}}
			{
				co_await fg_Suspend();
				co_return _Value;
			}
		)
	;
	fg_Dispatch
		(
			[_Value]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-3]]:2: warning: the closure of a lambda coroutine that uses its captures after a suspension is given to 'fg_Dispatch', which can call it directly; {{.*}}
			{
				co_await fg_Suspend();
				co_return _Value;
			}
		)
	;
}
