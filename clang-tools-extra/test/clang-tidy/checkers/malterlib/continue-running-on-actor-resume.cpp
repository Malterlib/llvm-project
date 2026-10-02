// RUN: %check_clang_tidy -std=c++20-or-later %s malterlib-continue-running-on-actor %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -I %S/Inputs

#include "malterlib-actor-phase.h"

TCFuture<int> fg_Work();

// The callback of fg_OnResume runs where the coroutine resumes, also on the
// actor it switches to, and a cleanup awaited early through f_Wrap runs where
// it is awaited. The coroutines switch back through a handle made before
// switching away, since nothing keeps the actor alive on another actor.
struct CResumeCallbacks : CActor
{
	int m_Value = 0;

	TCFuture<int> f_Run(TCActor<> _Other)
	{
		auto ThisActor = fg_ThisActor(this);
		auto OnResume = co_await fg_OnResume
			(
				[this]() -> CExceptionPointer
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
				{
					++m_Value;
					return {};
				}
			)
		;
		co_await fg_ContinueRunningOnActor(_Other);
		co_await fg_ContinueRunningOnActor(ThisActor);
		co_return 0;
	}

	TCFuture<int> f_Wrapped(TCActor<> _Other)
	{
		auto ThisActor = fg_ThisActor(this);
		auto Cleanup = co_await fg_AsyncDestroy
			(
				[this]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
				{
					m_Value = 1;
					co_return 0;
				}
			)
		;
		co_await fg_ContinueRunningOnActor(_Other);
		co_await fg_Move(Cleanup).f_Wrap();
		co_await fg_ContinueRunningOnActor(ThisActor);
		co_return 0;
	}

	TCFuture<int> f_Stays()
	{
		auto OnResume = co_await fg_OnResume
			(
				[this]() -> CExceptionPointer
				{
					++m_Value;
					return {};
				}
			)
		;
		co_return 0;
	}

	TCFuture<int> f_RegisteredOnOther(TCActor<> _Other)
	{
		auto ThisActor = fg_ThisActor(this);
		co_await fg_ContinueRunningOnActor(_Other);
		{
			auto OnResume = co_await fg_OnResume
				(
					[this]() -> CExceptionPointer
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
					{
						++m_Value;
						return {};
					}
				)
			;
			co_await fg_Work();
		}
		co_await fg_ContinueRunningOnActor(ThisActor);
		co_return 0;
	}

	TCFuture<int> f_RegisteredAfterBack(TCActor<> _Other)
	{
		auto ThisActor = fg_ThisActor(this);
		co_await fg_ContinueRunningOnActor(_Other);
		co_await fg_ContinueRunningOnActor(ThisActor);
		auto OnResume = co_await fg_OnResume
			(
				[this]() -> CExceptionPointer
				{
					++m_Value;
					return {};
				}
			)
		;
		co_await fg_Work();
		co_return 0;
	}

	TCFuture<int> f_RegisteredInIf(TCActor<> _Other)
	{
		auto ThisActor = fg_ThisActor(this);
		if
			(
				auto OnResume = co_await fg_OnResume
					(
						[this]() -> CExceptionPointer
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
						{
							++m_Value;
							return {};
						}
					)
				;
				true
			)
		{
			co_await fg_ContinueRunningOnActor(_Other);
			co_await fg_ContinueRunningOnActor(ThisActor);
		}
		co_return 0;
	}

	TCFuture<int> f_RegisteredOnOtherRunsRightAway(TCActor<> _Other)
	{
		auto ThisActor = fg_ThisActor(this);
		co_await fg_ContinueRunningOnActor(_Other);
		{
			auto OnResume = co_await fg_OnResume
				(
					[this]() -> CExceptionPointer
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
					{
						++m_Value;
						return {};
					}
				)
			;
		}
		co_await fg_ContinueRunningOnActor(ThisActor);
		co_return 0;
	}
};

// An awaiter of fg_OnResume made before it is awaited, and a cleanup awaited
// early through a reference to its guard.
struct CRound54Callbacks : CActor
{
	int m_Value = 0;

	TCFuture<int> f_Stored(TCActor<> _Other)
	{
		auto ThisActor = fg_ThisActor(this);
		auto Awaiter = fg_OnResume
			(
				[this]() -> CExceptionPointer
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
				{
					++m_Value;
					return {};
				}
			)
		;
		auto OnResume = co_await fg_Move(Awaiter);
		co_await fg_ContinueRunningOnActor(_Other);
		co_await fg_ContinueRunningOnActor(ThisActor);
		co_return 0;
	}

	TCFuture<int> f_Alias(TCActor<> _Other)
	{
		auto ThisActor = fg_ThisActor(this);
		auto Cleanup = co_await fg_AsyncDestroy
			(
				[this]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
				{
					++m_Value;
					co_return 0;
				}
			)
		;
		auto &Alias = Cleanup;
		co_await fg_ContinueRunningOnActor(_Other);
		co_await fg_Move(Alias);
		co_await fg_ContinueRunningOnActor(ThisActor);
		co_return 0;
	}
};

// A phase guard of the actor reads the object where the coroutine resumes, and
// so does either callback of a conditional registration.
struct CRound56Guard : CPhaseActor
{
	int m_Value = 0;

	TCFuture<int> f_Guard(TCActor<> _Other)
	{
		auto ThisActor = fg_ThisActor(this);
		auto Guard = co_await f_CheckDestroyedOnResume();
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
		co_await fg_ContinueRunningOnActor(_Other);
		co_await fg_ContinueRunningOnActor(ThisActor);
		co_return 0;
	}

	TCFuture<int> f_Conditional(TCActor<> _Other, bool _bChoose)
	{
		auto ThisActor = fg_ThisActor(this);
		auto OnResume = _bChoose
			? co_await fg_OnResume
				(
					[this]() -> CExceptionPointer
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
					{
						++m_Value;
						return {};
					}
				)
			: co_await fg_OnResume
				(
					[]() -> CExceptionPointer
					{
						return {};
					}
				)
		;
		co_await fg_ContinueRunningOnActor(_Other);
		co_await fg_ContinueRunningOnActor(ThisActor);
		co_return 0;
	}
};

// An iterator of the data is used like a pointer into it.
struct CRound89Iterator : CActor
{
	TCMap<int, int> mp_Items;

	TCFuture<int> f_Iterator()
	{
		auto iEntry = mp_Items.f_GetIterator();
		auto Other = fg_BlockingActor();
		co_await fg_ContinueRunningOnActor(Other);
		co_return *iEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}
};

// A registration made through a comma is alive too.
struct CRound94Comma : CActor
{
	int m_Value = 0;

	TCFuture<int> f_Run(TCActor<> _Other)
	{
		auto Scope =
			(
				0,
				co_await fg_OnResume
				(
					[this]() -> CExceptionPointer
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
					{
						++m_Value;
						return {};
					}
				)
			)
		;
		co_await fg_ContinueRunningOnActor(_Other);
		co_return 0;
	}
};

// Round 123: a registration of either value of a conditional that is awaited,
// and a guard awaited through a reference to the awaiter made before.
struct CRound123Awaited : CPhaseActor
{
	int m_Value = 0;

	TCFuture<int> f_Conditional(bool _bChoose, TCActor<> _Other)
	{
		auto Watch = co_await
			(
				_bChoose
				? fg_OnResume
					(
						[this]() -> CExceptionPointer
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
						{
							(void)m_Value;
							return nullptr;
						}
					)
				: fg_OnResume
					(
						[this]() -> CExceptionPointer
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
						{
							(void)m_Value;
							return nullptr;
						}
					)
			)
		;
		co_await fg_ContinueRunningOnActor(_Other);
		co_return 0;
	}

	TCFuture<int> f_Alias(TCActor<> _Other)
	{
		auto Awaiter = f_CheckDestroyedOnResume();
		auto &Alias = Awaiter;
		auto Watch = co_await fg_Move(Alias);
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
		co_await fg_ContinueRunningOnActor(_Other);
		co_return 0;
	}

	TCFuture<int> f_SelfMade(TCActor<> _Other)
	{
		TCFuture<COnResumeScope> &Alias = Alias;
		auto Watch = co_await fg_Move(Alias);
		co_await fg_ContinueRunningOnActor(_Other);
		co_return 0;
	}
};

namespace NRound144 {
// A registration that is a temporary of the full expression, alive at a switch
// later in it.
struct CApp : CActor {
  int m_Value;
  TCFuture<int> f_Temporary(TCActor<> _Other) {
    auto Own = fg_ThisActor(this);
    ((void)(co_await fg_OnResume([this]() -> CExceptionPointer {
// CHECK-MESSAGES: :[[@LINE-1]]:34: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
       ++m_Value;
       return {};
     })),
     co_await fg_ContinueRunningOnActor(_Other));
    co_await fg_ContinueRunningOnActor(Own);
    co_return 0;
  }
};
}

namespace NRound154 {
// Either registration of a conditional, alive until the end of the full
// expression.
struct CApp : CActor {
  int m_Value = 0;
  TCFuture<int> f_Conditional(TCActor<> _Other, bool _bChoose) {
    ((void)co_await (_bChoose ? fg_OnResume([this]() -> CExceptionPointer {
// CHECK-MESSAGES: :[[@LINE-1]]:45: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
                                  ++m_Value;
                                  return {};
                                })
                              : fg_OnResume([]() -> CExceptionPointer { return {}; })),
     co_await fg_ContinueRunningOnActor(_Other));
    co_return 0;
  }
};
}

namespace NRound167 {
// A temporary registration made before a switch in the same full
// expression, also on the right of an assignment, which runs first, but not
// one in another arm of a conditional than the switch.
struct CApp : CActor {
  int m_Value = 0;
  TCFuture<int> f_Assigned(TCActor<> _Other) {
    int Slots[1]{};
    Slots[(co_await fg_ContinueRunningOnActor(_Other), 0)] =
        (co_await fg_OnResume([this]() -> CExceptionPointer {
// CHECK-MESSAGES: :[[@LINE-1]]:31: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
           ++m_Value;
           return {};
         }),
         0);
    co_return 0;
  }
  TCFuture<int> f_Exclusive(TCActor<> _Other, bool _bChoose) {
    (void)(_bChoose ? (co_await fg_OnResume([this]() -> CExceptionPointer {
                         ++m_Value;
                         return {};
                       }),
                       0)
                    : (co_await fg_ContinueRunningOnActor(_Other), 0));
    co_return 0;
  }
};
}

namespace NRound187 {
// A registration a member function makes and gives back.
struct CApp : CActor {
  int m_Value = 0;
  auto f_Watch() {
    return fg_OnResume([this]() -> CExceptionPointer {
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
      ++m_Value;
      return {};
    });
  }
  auto f_WatchAgain() { return f_Watch(); }
  TCFuture<int> f_Run(TCActor<> _Other) {
    auto OnResume = co_await f_Watch();
    co_await fg_ContinueRunningOnActor(_Other);
    co_return 0;
  }
  TCFuture<int> f_Through(TCActor<> _Other) {
    auto OnResume = co_await f_WatchAgain();
    co_await fg_ContinueRunningOnActor(_Other);
    co_return 0;
  }
};
}

namespace NRound224
{
	// A callback held in a local runs where `fg_OnResume` registers it, after
	// a switch made after the lambda.
	struct CApp : CPhaseActor
	{
		TCFuture<int> f_Registered(TCActor<> _Other)
		{
			auto fOnResume = [this]() -> CExceptionPointer
				{
					if (f_IsDestroyed())
						return fg_MakeError("Shutting down");
					return nullptr;
				}
			;
			co_await fg_ContinueRunningOnActor(_Other);
			auto OnResume = co_await fg_OnResume(fOnResume);
// CHECK-MESSAGES: :[[@LINE-1]]:29: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
			co_return 0;
		}
		TCFuture<int> f_RegisteredBefore(TCActor<> _Other)
		{
			auto fOnResume = [this]() -> CExceptionPointer
				{
					if (f_IsDestroyed())
						return fg_MakeError("Shutting down");
					return nullptr;
				}
			;
			auto OnResume = co_await fg_OnResume(fOnResume);
			co_return 0;
		}
	};
}

namespace NRound240 {
// A lookup a helper makes in what it is given by reference.
int *fg_Find(TCMap<int, int> &_Items) { return _Items.f_FindEqual(1); }
struct CApp : CActor {
  TCMap<int, int> mp_Items;
  TCFuture<int> f_Run(TCActor<> _Other) {
    int *pValue = fg_Find(mp_Items);
    co_await fg_ContinueRunningOnActor(_Other);
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
  }
};
}
