// RUN: %check_clang_tidy -std=c++23-or-later %s malterlib-runs-to-completion %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -I %S/Inputs

#include "malterlib-runs-to-completion.h"

TCFuture<int> fg_Work(int _Value);

// An explicit object parameter taken by value is a value of the coroutine.
struct DMibRunsToCompletion CExplicitObject
{
	TCFuture<int> operator () (this CExplicitObject _Self, int _Value)
	{
		co_await fg_Work(0);
		co_return _Value;
	}
};

DMibRunsToCompletion TCFuture<int> fg_ExplicitObject()
{
	CExplicitObject Functor;
	TCFutureVector<int> Results;
	auto JoinResults = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	Functor(1) > Results;
	co_return 0;
}

// An explicit object parameter of an entry operator is a parameter, not the
// first argument after the object.
DMibRunsToCompletion TCFuture<int> fg_Round67Leaf();
struct CRound67Entry
{
	DMibRunsToCompletionEntry TCFuture<int> operator () (this CRound67Entry _Self, NFunction::TCFunction<TCFuture<int> ()> _fCallback)
	{
		auto Started = fg_CallSafe(_fCallback);
		co_await fg_ContinueRunningOnActor(fg_ConcurrentActor());
		co_return co_await fg_Move(Started);
	}
};
TCFuture<int> fg_Round67Entry()
{
	co_return co_await CRound67Entry{}
		(
			[]() -> TCFuture<int>
			{
				co_return co_await fg_Round67Leaf();
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: 'fg_Round67Leaf' runs to completion, but is called here before the entry switches to a concurrent actor, so it runs on an actor that can be destroyed while 'fg_Round67Leaf' refers to its data; call it after the switch [malterlib-runs-to-completion]
			}
		)
	;
}

// Round 124: a coroutine never runs in a constant evaluation, so the branch of
// `if consteval` that runs otherwise is always taken.
namespace NRound124
{
DMibRunsToCompletionEntry TCFuture<int> fg_NotConsteval() {
  if !consteval {
    co_await fg_ContinueRunningOnActor(fg_ConcurrentActor());
  }
  co_return 0;
}

DMibRunsToCompletionEntry TCFuture<int> fg_ConstevalElse() {
  if consteval {
  } else {
    co_await fg_ContinueRunningOnActor(fg_ConcurrentActor());
  }
  co_return 0;
}

// A switch in the branch that runs when constant evaluated never runs, so
// the coroutine never suspends.
DMibRunsToCompletionEntry TCFuture<int> fg_Consteval() {
  if consteval {
    co_await fg_ContinueRunningOnActor(fg_ConcurrentActor());
  }
  co_return 0;
}
}

namespace NRound183 {
// Not a suspension in the branch of `if consteval` that runs when constant
// evaluated, which a coroutine never is.
DMibRunsToCompletionEntry TCFuture<int> fg_Start() {
  if consteval {
    co_await CAwaiter{};
  } else {
    co_await fg_ContinueRunningOnActor(fg_ConcurrentActor());
  }
  co_return 0;
}
}
