// RUN: %check_clang_tidy -std=c++23-or-later %s malterlib-coroutine-reference-parameter %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -I %S/Inputs

#include "malterlib-concurrency.h"

TCFuture<int> fg_Suspend();

// What only runs when constant evaluated doesn't run in a coroutine.
TCFuture<int> fg_Consteval(int &_Value)
{
	co_await fg_Suspend();
	if consteval
	{
		co_return _Value;
	}
	else
	{
		co_return 0;
	}
}

// What runs when not constant evaluated does.
TCFuture<int> fg_NotConsteval(int &_Value)
// CHECK-MESSAGES: :[[@LINE-1]]:36: warning: TCFuture coroutine 'fg_NotConsteval' uses '_Value' after a suspension, {{.*}}
{
	co_await fg_Suspend();
	if !consteval
	{
		co_return _Value;
	}
	else
	{
		co_return 0;
	}
}

namespace NRound144 {
TCFuture<int> fg_Work();

// The explicit object of a member function of an actor, which is its object.
struct CApp : CActor {
  int m_Value;
  TCFuture<int> f_Explicit(this CApp &_Self) {
    co_await fg_Work();
    co_return _Self.m_Value;
  }
};
}
