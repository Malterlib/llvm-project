// The checks that analyse flow report nothing in a translation unit with an
// error that stops it from compiling, also not that a function is too complex
// to analyse; the same code without the error is checked. The naming checks
// still report there.
// RUN: %check_clang_tidy -std=c++20-or-later %s malterlib-continue-running-on-actor,malterlib-async-destroy,malterlib-actor-dispatch-this %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -I %S/Inputs
// RUN: not clang-tidy --checks='-*,malterlib-actor-*,malterlib-coroutine-*,malterlib-async-destroy,malterlib-coawait-*,malterlib-continue-running-on-actor,malterlib-runs-to-completion,malterlib-thread-local-storage' --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" %s -- -std=c++20 -I %S/Inputs -DBROKEN 2>&1 | FileCheck --implicit-check-not='[malterlib-' --implicit-check-not='Stack dump' --check-prefix=CHECK-BROKEN %s
// RUN: not clang-tidy --checks='-*,malterlib-function-naming' --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" %s -- -std=c++20 -I %S/Inputs -DBROKEN 2>&1 | FileCheck --check-prefix=CHECK-NAMING %s

#include "malterlib-concurrency.h"

struct CTarget
{
	TCFuture<int> f_Destroy();
	int m_Value = 0;
};

TCFuture<int> fg_Suspend();

struct CApp : CActor
{
	int m_Value = 0;

	TCFuture<int> f_Switch()
	{
		CTarget Target;
		auto Cleanup = co_await fg_AsyncDestroy
			(
				[&Target]() -> TCFuture<int>
				{
					co_await fg_Suspend();
					co_await Target.f_Destroy();
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the cleanup function of 'fg_AsyncDestroy' uses 'Target', which it captures by reference, after its first suspension, where the reference is gone; copy or move it into a local before the suspension [malterlib-async-destroy]
					co_return 0;
				}
			)
		;
		auto BlockingActorCheckout = fg_BlockingActor();
		co_await fg_ContinueRunningOnActor(BlockingActorCheckout);
		co_return m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}

	void f_Dispatch(TCActor<> _Other)
	{
		g_Dispatch(_Other) / [this]
			{
				return m_Value;
			}
		;
// CHECK-MESSAGES: :[[@LINE-5]]:25: warning: a lambda that captures 'this' of actor 'CApp' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
	}
};

void Bad()
{
}
// CHECK-NAMING: :[[@LINE-3]]:6: warning: global function 'Bad' should have the 'fg_' prefix [malterlib-function-naming]

#ifdef BROKEN
int g_Broken = NotDeclared;
// CHECK-BROKEN: error: use of undeclared identifier 'NotDeclared'
// CHECK-NAMING: error: use of undeclared identifier 'NotDeclared'
#endif
