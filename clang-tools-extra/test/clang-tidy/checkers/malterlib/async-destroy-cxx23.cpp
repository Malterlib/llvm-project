// RUN: %check_clang_tidy -std=c++23-or-later %s malterlib-async-destroy %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -I %S/Inputs

#include "malterlib-concurrency.h"

// Teardown through an explicit object parameter, like the one through a
// method qualified with `&&`.
struct CRound208Resource
{
	TCFuture<int> f_Destroy(this CRound208Resource &&_Self);
};

TCFuture<int> fg_Round208ExplicitObject()
{
	CRound208Resource Resource;
	auto Cleanup = g_OnScopeExit / [&]
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: the teardown of an actor or functor in a coroutine uses 'fg_AsyncDestroy' instead of 'g_OnScopeExit' [malterlib-async-destroy]
		{
			fg_Move(Resource).f_Destroy();
		}
	;
	co_return 0;
}

// A synchronous one that returns nothing is not asynchronous teardown.
struct CRound208SyncResource
{
	void f_Destroy(this CRound208SyncResource &&_Self);
};

TCFuture<int> fg_Round208ExplicitObjectSync()
{
	CRound208SyncResource Resource;
	auto Cleanup = g_OnScopeExit / [&]
		{
			fg_Move(Resource).f_Destroy();
		}
	;
	co_return 0;
}
