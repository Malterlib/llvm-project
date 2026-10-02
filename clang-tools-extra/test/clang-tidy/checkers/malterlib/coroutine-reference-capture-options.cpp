// RUN: %check_clang_tidy -std=c++20-or-later %s malterlib-coroutine-reference-capture %t -- \
// RUN:   -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" \
// RUN:   -- -I %S/Inputs
// RUN: %check_clang_tidy -std=c++20-or-later -check-suffixes=,UNLISTED %s malterlib-coroutine-reference-capture %t -- \
// RUN:   -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml', SafeCallees: '-fg_CallSafe'}}" \
// RUN:   -- -I %S/Inputs

#include "malterlib-concurrency.h"

// A lambda given to `fg_CallSafe` runs right away, where what it captures is,
// while the options keep it among the SafeCallees, and is any other function
// it is given to once they don't.
void fg_Run()
{
	int Value = 7;
	fg_CallSafe
		(
			[&Value]() -> TCFuture<int>
// CHECK-MESSAGES-UNLISTED: :[[@LINE-1]]:6: warning: a TCFuture coroutine lambda captures 'Value' by reference but is not called directly where 'Value' is, so it can run after 'Value' is gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
			{
				co_return Value;
			}
		)
	;
}

// One given to another function is reported either way.
template <typename t_FFunction>
void fg_Store(t_FFunction &&_fFunction)
{
}

void fg_RunStored()
{
	int Value = 7;
	fg_Store
		(
			[&Value]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: a TCFuture coroutine lambda captures 'Value' by reference but is not called directly where 'Value' is, so it can run after 'Value' is gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
			{
				co_return Value;
			}
		)
	;
}
