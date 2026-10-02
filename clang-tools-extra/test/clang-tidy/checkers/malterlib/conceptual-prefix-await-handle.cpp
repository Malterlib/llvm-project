// RUN: clang-tidy --checks='-*,malterlib-conceptual-prefix' --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" %s -- -std=c++20 -I %S/Inputs 2>&1 | FileCheck --allow-empty %s

// What `co_await` gives is named as the awaiter it awaits is given it, through
// the `await_transform` of the promise that gives back what it is given, also
// a handle, which the type it is deduced as doesn't keep.

#include "malterlib-concurrency.h"

namespace NRound337Await
{
	using CActorSubscription = NMib::NStorage::TCSharedPointer<int>;

	NMib::NConcurrency::TCFuture<int> fg_Test()
	{
		auto Subscription = co_await NMib::NConcurrency::TCAwaiter<CActorSubscription>{};
		(void)Subscription;
		co_return 0;
	}
}

// Also what a future it awaits gives, through the awaiter the promise makes of
// it.
namespace NRound338Await
{
	using CActorSubscription = NMib::NStorage::TCSharedPointer<int>;

	NMib::NConcurrency::TCFuture<int> fg_Test()
	{
		auto Subscription = co_await NMib::NConcurrency::TCFuture<CActorSubscription>{};
		(void)Subscription;
		co_return 0;
	}
}

// CHECK-NOT: {{warning|error}}:
