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

namespace NExplicitObjectGetters {
// What a getter with an explicit object gives of a member of its object lives
// as long as the object, as of one with an implicit object, also called
// through a pointer and as an operator; not of what it doesn't hold.
struct CState {
  int m_Value;
};
template <typename t_CType>
struct TCOptional {
  t_CType m_Value;
  t_CType *m_pOther;
  explicit TCOptional(t_CType _Value) : m_Value(_Value), m_pOther(nullptr) {}
  template <typename tf_CThis>
  auto &f_Get(this tf_CThis &&_This) { return _This.m_Value; }
  template <typename tf_CThis>
  auto &operator*(this tf_CThis &&_This) { return _This.m_Value; }
  template <typename tf_CThis>
  auto &f_GetOther(this tf_CThis &&_This) { return *_This.m_pOther; }
};
// What a pointer member that states that only its class points it
// (DMibSetByOwnClass) points to is where the class points it, nowhere here.
template <typename t_CType>
struct TCKnownOptional {
  t_CType m_Value;
  DMibSetByOwnClass t_CType *m_pOther;
  explicit TCKnownOptional(t_CType _Value) : m_Value(_Value), m_pOther(nullptr) {}
  template <typename tf_CThis>
  auto &f_GetOther(this tf_CThis &&_This) { return *_This.m_pOther; }
};
DMibRunsToCompletion TCFuture<int> fg_Read(CState const &_Value);
DMibRunsToCompletion TCFuture<int> fg_Optional() {
  TCOptional<CState> State(CState{42});
  TCOptional<CState> *pState = &State;
  TCFutureVector<int> Results;
  auto Join = co_await fg_AsyncDestroy([&]() -> TCFuture<int> {
    co_await fg_AllDone(Results);
    co_return 0;
  });
  fg_Read(State.f_Get()) > Results;
  fg_Read(pState->f_Get()) > Results;
  fg_Read(*State) > Results;
  fg_Read(State.f_GetOther()) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: 'fg_Read' runs to completion and is joined by 'Join', but this argument may not live until then; pass a local declared before 'Join' [malterlib-runs-to-completion]
  TCKnownOptional<CState> Known(CState{42});
  fg_Read(Known.f_GetOther()) > Results;
  co_await fg_Move(Join);
  co_return 0;
}
}
