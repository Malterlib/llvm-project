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

// A generic cleanup is analyzed as what its call operator is instantiated as,
// never as the pattern, which has no control flow until it is instantiated.
namespace NReview2Round193Generic
{
	template <typename t_FDestroy>
	CAsyncDestroyAwaiter fg_AsyncDestroyGeneric(t_FDestroy &&_fDestroy)
	{
		(void)_fDestroy();
		return {};
	}

	TCFuture<int> fg_Work();

	TCFuture<int> fg_Clean()
	{
		int Value = 42;
		auto Cleanup = co_await fg_AsyncDestroyGeneric([&Value](this auto const &_Self) -> TCFuture<int> { Value = 0; co_return 0; });
		co_return 0;
	}

	// One that is never instantiated has no body to analyze.
	TCFuture<int> fg_NotInstantiated()
	{
		int Value = 42;
		auto Cleanup = co_await fg_AsyncDestroy([&Value](this auto const &_Self) -> TCFuture<int> { co_await fg_Work(); Value = 0; co_return 0; });
		co_return 0;
	}

	TCFuture<int> fg_UsedAfterSuspension()
	{
		int Value = 42;
		auto Cleanup = co_await fg_AsyncDestroyGeneric([&Value](this auto const &_Self) -> TCFuture<int> { co_await fg_Work(); Value = 0; co_return 0; });
// CHECK-MESSAGES: :[[@LINE-1]]:122: warning: the cleanup function of 'fg_AsyncDestroy' uses 'Value', which it captures by reference, after its first suspension, where the reference is gone; copy or move it into a local before the suspension [malterlib-async-destroy]
		co_return 0;
	}
}
