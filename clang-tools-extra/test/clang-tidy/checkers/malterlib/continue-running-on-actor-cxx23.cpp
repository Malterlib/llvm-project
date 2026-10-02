// RUN: %check_clang_tidy -std=c++23-or-later %s malterlib-continue-running-on-actor %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -I %S/Inputs

#include "malterlib-concurrency.h"

// A static call operator gets the object it is called on as its first
// argument.
struct CRound108Replace
{
	static void operator()(TCActor<> &o_Actor, TCActor<> _Other)
	{
		o_Actor = fg_Move(_Other);
	}
};

struct CRound108Static : CActor
{
	int m_Value = 0;

	TCFuture<int> f_Run(TCActor<> _Other)
	{
		TCActor<> Own = fg_ThisActor(this);
		CRound108Replace{}(Own, _Other);
		co_await fg_ContinueRunningOnActor(Own);
		m_Value = 1;
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
		co_return 0;
	}
};
