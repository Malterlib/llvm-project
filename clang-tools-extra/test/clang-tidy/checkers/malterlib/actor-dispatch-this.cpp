// RUN: %check_clang_tidy -std=c++20-or-later %s malterlib-actor-dispatch-this %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -I %S/Inputs

#include "malterlib-concurrency.h"

template <typename tf_CType>
void fg_Clobber(tf_CType &o_Value);

struct CMyActor : CActor
{
	void f_Dispatch()
	{
		auto BlockingActorCheckout = fg_BlockingActor();
		g_Dispatch(BlockingActorCheckout) / [this]()
// CHECK-MESSAGES: :[[@LINE-1]]:40: warning: a lambda that captures 'this' of actor 'CMyActor' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
			{
				return m_Value;
			}
		;

		g_Dispatch(m_OtherActor) / [pThis = this]()
// CHECK-MESSAGES: :[[@LINE-1]]:31: warning: a lambda that captures 'this' of actor 'CMyActor' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
			{
				return pThis->m_Value;
			}
		;

		m_OtherActor / [=]()
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: a lambda that captures 'this' of actor 'CMyActor' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
			{
				return m_Value;
			}
		;

		// The own actor.
		g_Dispatch / [this]()
			{
				return m_Value;
			}
		;
		fg_ThisActor(this) / [this]()
			{
				return m_Value;
			}
		;
		auto ThisActor = fg_ThisActor(this);
		g_Dispatch(ThisActor) / [this]()
			{
				return m_Value;
			}
		;

		// A handle to another actor, or one that was changed.
		fg_ThisActor(pOther) / [this]()
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'fg_ThisActor' is given the object of an actor, which it uses like 'this' on the actor the call runs on, but this may be of another actor; give it the object of the current actor, like 'this', or call it on the actor of the object [malterlib-actor-dispatch-this]
// CHECK-MESSAGES: :[[@LINE-2]]:27: warning: a lambda that captures 'this' of actor 'CMyActor' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
			{
				return m_Value;
			}
		;
		auto Changed = fg_ThisActor(this);
		Changed = m_OtherActor;
		g_Dispatch(Changed) / [this]()
// CHECK-MESSAGES: :[[@LINE-1]]:26: warning: a lambda that captures 'this' of actor 'CMyActor' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
			{
				return m_Value;
			}
		;

		auto *pThis = this;
		g_Dispatch(fg_ThisActor(pThis)) / [this]()
			{
				return m_Value;
			}
		;

		// The own actor through `self`, and its conversion to a handle.
		self / [this]()
			{
				return m_Value;
			}
		;
		g_Dispatch(self) / [this]()
			{
				return m_Value;
			}
		;
		ThisActor.f_Dispatch() / [this]()
			{
				return m_Value;
			}
		;
		m_OtherActor.f_Dispatch() / [this]()
// CHECK-MESSAGES: :[[@LINE-1]]:32: warning: a lambda that captures 'this' of actor 'CMyActor' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
			{
				return m_Value;
			}
		;

		// A captured alias of `this`.
		g_Dispatch(m_OtherActor) / [pThis]()
// CHECK-MESSAGES: :[[@LINE-1]]:31: warning: a lambda that captures 'this' of actor 'CMyActor' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
			{
				return pThis->m_Value;
			}
		;

		g_Dispatch(pThis->self) / [this]()
			{
				return m_Value;
			}
		;

		// Not evaluated.
		using CResult = decltype(g_Dispatch(m_OtherActor) / [this]() { return m_Value; });

		// A lambda in a local.
		auto fWork = [this]()
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: a lambda that captures 'this' of actor 'CMyActor' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
			{
				return m_Value;
			}
		;
		g_Dispatch(m_OtherActor) / fg_Move(fWork);

		// A handle a helper can change through a non-const reference.
		auto Clobbered = fg_ThisActor(this);
		fg_Clobber(Clobbered);
		g_Dispatch(Clobbered) / [this]()
// CHECK-MESSAGES: :[[@LINE-1]]:28: warning: a lambda that captures 'this' of actor 'CMyActor' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
			{
				return m_Value;
			}
		;

		// A dispatcher kept in a local still runs on its actor.
		auto Dispatch = g_Dispatch(self);
		Dispatch / [this]()
			{
				return m_Value;
			}
		;
		auto OtherDispatch = g_Dispatch(m_OtherActor);
		OtherDispatch / [this]()
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: a lambda that captures 'this' of actor 'CMyActor' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
			{
				return m_Value;
			}
		;

		// Copies are safe.
		g_Dispatch(BlockingActorCheckout) / [Value = m_Value]()
			{
				return Value;
			}
		;
	}

	TCActor<> m_OtherActor;
	CMyActor *pOther = nullptr;
	int m_Value = 0;
};

// The own actor through a weak handle that is locked again.
struct CWeakSelf : CActor
{
	void f_Run()
	{
		auto ThisWeak = fg_ThisActor(this).f_Weak();
		auto Locked = ThisWeak.f_Lock();
		g_Dispatch(Locked) / [this]()
			{
				return m_Value;
			}
		;
		[ThisWeak = fg_ThisActor(this).f_Weak(), this]()
			{
				g_Dispatch(ThisWeak.f_Lock()) / [this]()
					{
						return m_Value;
					}
				;
			}
			()
		;
	}

	int m_Value = 0;
};

// A timer callback runs on the actor it is given, and a subscription cleanup
// on the actor or blocking actor it is made with.
struct CTimers : CActor
{
	void f_Run()
	{
		fg_RegisterTimer
			(
				1.0
				, [this]() -> TCFuture<int>
				{
					co_return m_Value;
				}
			)
		;
		fg_RegisterTimer
			(
				1.0
				, [this]() -> TCFuture<int>
				{
					co_return m_Value;
				}
				, fg_ThisActor(this)
			)
		;
		// An empty actor is replaced with the current one.
		fg_RegisterTimer
			(
				1.0
				, [this]() -> TCFuture<int>
				{
					co_return m_Value;
				}
				, {}
			)
		;
		fg_RegisterTimer
			(
				1.0
				, [this]() -> TCFuture<int>
				{
					co_return m_Value;
				}
				, TCActor<>()
			)
		;
		fg_RegisterTimer
			(
				1.0
				, [this]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: a lambda that captures 'this' of actor 'CTimers' runs on another actor, {{.*}}
				{
					co_return m_Value;
				}
				, m_OtherActor
			)
		;
		auto OnActor = g_ActorSubscription(m_OtherActor) / [this]()
// CHECK-MESSAGES: :[[@LINE-1]]:55: warning: a lambda that captures 'this' of actor 'CTimers' runs on another actor, {{.*}}
			{
				m_Value = 0;
			}
		;
		auto OnBlocking = g_BlockingActorSubscription / [this]()
// CHECK-MESSAGES: :[[@LINE-1]]:52: warning: a lambda that captures 'this' of actor 'CTimers' runs on another actor, {{.*}}
			{
				m_Value = 0;
			}
		;
		auto OnOwn = g_ActorSubscription / [this]()
			{
				m_Value = 0;
			}
		;
	}

	TCActor<> m_OtherActor;
	int m_Value = 0;
};

// Dispatched to another actor, or a concurrent one.
struct CRound35Dispatch : CActor
{
	TCFuture<int> f_Run(TCActor<> _Other)
	{
		co_await fg_Dispatch(_Other, [this] { return ++m_Value; });
// CHECK-MESSAGES: :[[@LINE-1]]:33: warning: {{.*}}
		co_await fg_ConcurrentDispatch([this] { return ++m_Value; });
// CHECK-MESSAGES: :[[@LINE-1]]:35: warning: {{.*}}
		co_await fg_Dispatch(self, [this] { return ++m_Value; });
		co_return 0;
	}

	int m_Value = 0;
};

// A bound dispatch runs the function on the actor it is bound to.
struct CRound36Bind : CActor
{
	TCFuture<int> f_Run(TCActor<> _Other)
	{
		co_await _Other.f_Bind<&CActor::f_DispatchWithReturn<TCFuture<int>>>
			(
				[this]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: {{.*}}
				{
					co_return ++m_Value;
				}
			)
		;
		co_await _Other.f_BindByValue<&CActor::f_DispatchWithReturn<TCFuture<int>>>
			(
				[this]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: {{.*}}
				{
					co_return ++m_Value;
				}
			)
		;
		co_return 0;
	}

	int m_Value = 0;
};

// The dispatch operator called by name.
struct CRound37Named : CActor
{
	void f_Run(TCActor<> _Other)
	{
		g_Dispatch(_Other).operator/([this] { return ++m_Value; });
// CHECK-MESSAGES: :[[@LINE-1]]:33: warning: {{.*}}
	}

	int m_Value = 0;
};

// `g_ConcurrentDispatch /` runs on a concurrent actor, and
// `fg_CallSafeDispatchedOn` on the actor it is given.
struct CEdgeDispatch : CActor
{
	int mp_Value = 0;
	TCFuture<int> f_Concurrent()
	{
		co_return co_await (g_ConcurrentDispatch / [this] { return ++mp_Value; });
// CHECK-MESSAGES: :[[@LINE-1]]:47: warning: a lambda that captures 'this' of actor 'CEdgeDispatch' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
	}
	TCFuture<int> f_CallSafeOn(TCActor<> _Other)
	{
		co_return co_await fg_CallSafeDispatchedOn(_Other, [this] { return ++mp_Value; });
// CHECK-MESSAGES: :[[@LINE-1]]:55: warning: a lambda that captures 'this' of actor 'CEdgeDispatch' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
	}
};

// A lambda made into a function object first.
struct CWrappedDispatch : CActor
{
	int mp_Value = 0;
	void f_Run(TCActor<> _Other)
	{
		(void)fg_Dispatch(_Other, TCFunction<int ()>{[this] { return ++mp_Value; }});
// CHECK-MESSAGES: :[[@LINE-1]]:49: warning: a lambda that captures 'this' of actor 'CWrappedDispatch' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
	}
};

namespace std
{
	template <typename t_CType>
	t_CType *addressof(t_CType &_Value) noexcept;
}

// A pointer or reference into the object.
struct CMemberCaptures : CActor
{
	int mp_Value = 0;
	TCActor<> mp_Other;

	void f_Run()
	{
		(void)(g_Dispatch(mp_Other) / [pValue = &mp_Value] { return ++*pValue; });
// CHECK-MESSAGES: :[[@LINE-1]]:34: warning: a lambda that captures 'this' of actor 'CMemberCaptures' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
		(void)(g_Dispatch(mp_Other) / [&Value = mp_Value] { return ++Value; });
// CHECK-MESSAGES: :[[@LINE-1]]:35: warning: a lambda that captures 'this' of actor 'CMemberCaptures' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
	}

	// A pointer copied before it is pointed elsewhere, and one pointed
	// elsewhere before it is copied.
	void f_RunLocal()
	{
		int Local = 0;
		auto *pValue = &mp_Value;
		(void)(g_Dispatch(mp_Other) / [pValue] { return ++*pValue; });
// CHECK-MESSAGES: :[[@LINE-1]]:34: warning: a lambda that captures 'this' of actor 'CMemberCaptures' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
		pValue = &Local;
		(void)(g_Dispatch(mp_Other) / [pValue] { return ++*pValue; });
	}

	// A pointer pointed elsewhere on only some paths.
	void f_RunMaybeLocal(bool _bLocal)
	{
		int Local = 0;
		auto *pValue = &mp_Value;
		if (_bLocal)
			pValue = &Local;
		(void)(g_Dispatch(mp_Other) / [pValue] { return ++*pValue; });
// CHECK-MESSAGES: :[[@LINE-1]]:34: warning: a lambda that captures 'this' of actor 'CMemberCaptures' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
	}

	// A copy of a pointer into it, its address through std::addressof, a
	// pointer assigned before the lambda, and a binding of a reference to it.
	struct CParts
	{
		int m_Part;
	};
	CParts mp_Parts;
	void f_RunAliases()
	{
		auto *pValue = &mp_Value;
		auto *pAlias = pValue;
		(void)(g_Dispatch(mp_Other) / [pAlias] { return ++*pAlias; });
// CHECK-MESSAGES: :[[@LINE-1]]:34: warning: a lambda that captures 'this' of actor 'CMemberCaptures' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
		(void)(g_Dispatch(mp_Other) / [pAddress = std::addressof(mp_Value)] { return ++*pAddress; });
// CHECK-MESSAGES: :[[@LINE-1]]:34: warning: a lambda that captures 'this' of actor 'CMemberCaptures' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
		int *pAssigned;
		pAssigned = &mp_Value;
		(void)(g_Dispatch(mp_Other) / [pAssigned] { return ++*pAssigned; });
// CHECK-MESSAGES: :[[@LINE-1]]:34: warning: a lambda that captures 'this' of actor 'CMemberCaptures' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
		auto &[Part] = mp_Parts;
		(void)(g_Dispatch(mp_Other) / [&Part] { return ++Part; });
// CHECK-MESSAGES: :[[@LINE-1]]:35: warning: a lambda that captures 'this' of actor 'CMemberCaptures' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
	}

	// Through a local pointer to it, into an array of it, and pointed into it
	// on only some paths.
	int mp_Values[2] = {};
	void f_RunThroughThis(bool _bUse)
	{
		auto *pThis = this;
		(void)(g_Dispatch(mp_Other) / [pValue = &pThis->mp_Value] { return ++*pValue; });
// CHECK-MESSAGES: :[[@LINE-1]]:34: warning: a lambda that captures 'this' of actor 'CMemberCaptures' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
		(void)(g_Dispatch(mp_Other) / [pValue = mp_Values + 1] { return ++*pValue; });
// CHECK-MESSAGES: :[[@LINE-1]]:34: warning: a lambda that captures 'this' of actor 'CMemberCaptures' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
		int *pMaybe = nullptr;
		if (_bUse)
			pMaybe = &mp_Value;
		(void)(g_Dispatch(mp_Other) / [pMaybe] { return pMaybe ? ++*pMaybe : 0; });
// CHECK-MESSAGES: :[[@LINE-1]]:34: warning: a lambda that captures 'this' of actor 'CMemberCaptures' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
	}

	// A copy of a pointer through a reference to it, and what a member
	// function of a member gives.
	struct CLookup
	{
		int *f_FindEqual(int _Key);
	};
	CLookup mp_Lookup;
	void f_RunLookups()
	{
		int *pValue = &mp_Value;
		int *&pAlias = pValue;
		(void)(g_Dispatch(mp_Other) / [pAlias] { return ++*pAlias; });
// CHECK-MESSAGES: :[[@LINE-1]]:34: warning: a lambda that captures 'this' of actor 'CMemberCaptures' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
		(void)(g_Dispatch(mp_Other) / [pEntry = mp_Lookup.f_FindEqual(1)] { return ++*pEntry; });
// CHECK-MESSAGES: :[[@LINE-1]]:34: warning: a lambda that captures 'this' of actor 'CMemberCaptures' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
	}

	// A pointer captured by reference, pointed into it later.
	void f_RunByReference()
	{
		int *pValue = nullptr;
		auto fWork = [&pValue]
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: a lambda that captures 'this' of actor 'CMemberCaptures' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
			{
				return *pValue;
			}
		;
		pValue = &mp_Value;
		(void)(g_Dispatch(mp_Other) / fWork);
	}

	// A member through a pointer to it.
	void f_RunMemberPointer()
	{
		int CMemberCaptures::*pMember = &CMemberCaptures::mp_Value;
		(void)(g_Dispatch(mp_Other) / [pValue = &(this->*pMember)] { return ++*pValue; });
// CHECK-MESSAGES: :[[@LINE-1]]:34: warning: a lambda that captures 'this' of actor 'CMemberCaptures' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
	}

	// The value of a pointer member points to what isn't the object.
	int *mp_pShared = nullptr;
	void f_RunPointed()
	{
		(void)(g_Dispatch(mp_Other) / [pShared = mp_pShared] { return ++*pShared; });
	}
};

// A pointer or reference to the object of an actor, like a parameter of a
// static function, runs on another actor too, unless it is dispatched to the
// actor of that object.
struct CStaticHelpers : CActor
{
	int m_Value = 0;

	static TCFuture<int> fs_Run(CStaticHelpers *_pThis, TCActor<> _Other)
	{
		co_return co_await (_Other / [_pThis] { return _pThis->m_Value; });
// CHECK-MESSAGES: :[[@LINE-1]]:33: warning: a lambda that captures '_pThis', which points or refers to the object of actor 'CStaticHelpers', runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
	}

	static TCFuture<int> fs_RunRenamed(CStaticHelpers &_This, TCActor<> _Other)
	{
		co_return co_await (g_Dispatch(_Other) / [pThis = &_This] { return pThis->m_Value; });
// CHECK-MESSAGES: :[[@LINE-1]]:45: warning: a lambda that captures 'pThis', which points or refers to the object of actor 'CStaticHelpers', runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
	}

	// `_pThis->self /` dispatches to the current actor, which is the actor of
	// `_pThis`, which its callers are checked to give it.
	static TCFuture<int> fs_RunOnOwn(CStaticHelpers *_pThis)
	{
		auto Result = co_await (fg_ThisActor(_pThis) / [_pThis] { return _pThis->m_Value; });
		Result += co_await (g_Dispatch / [_pThis] { return _pThis->m_Value; });
		co_return Result + co_await (_pThis->self / [_pThis] { return _pThis->m_Value; });
	}
};

TCFuture<int> fg_RunFree(CStaticHelpers &_Actor, TCActor<> _Other)
{
	co_return co_await fg_Dispatch(_Other, [&_Actor] { return _Actor.m_Value; });
// CHECK-MESSAGES: :[[@LINE-1]]:43: warning: a lambda that captures '_Actor', which points or refers to the object of actor 'CStaticHelpers', runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
}

// A pointer into the object of an actor parameter, a capture of another actor
// on the own actor, a pointer pointed into the object through fg_Exchange or a
// reference to it, an unchanged alias of the parameter, and a static member.
struct CRound53Data : CActor
{
	int m_Value = 0;
	static constexpr int mc_Value = 7;

	static TCFuture<int> fs_Field(CRound53Data *_pThis, TCActor<> _Other)
	{
		co_return co_await (g_Dispatch(_Other) / [pValue = &_pThis->m_Value] { return *pValue; });
// CHECK-MESSAGES: :[[@LINE-1]]:45: warning: a lambda that captures 'pValue', which points or refers to the object of actor 'CRound53Data', runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
	}

	TCFuture<int> f_ReadOther(CRound53Data *_pOther)
	{
		co_return co_await fg_Dispatch(fg_ThisActor(this), [_pOther] { return _pOther->m_Value; });
// CHECK-MESSAGES: :[[@LINE-1]]:55: warning: a lambda that captures '_pOther', which points or refers to the object of actor 'CRound53Data', runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
	}

	void f_Exchanged(TCActor<> _Other)
	{
		int *pValue = nullptr;
		fg_Exchange(pValue, &m_Value);
		(void)(_Other / [pValue] { *pValue = 1; return 0; });
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: a lambda that captures 'this' of actor 'CRound53Data' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
		int *pThroughAlias = nullptr;
		int *&pAlias = pThroughAlias;
		pAlias = &m_Value;
		(void)(_Other / [pThroughAlias] { *pThroughAlias = 1; return 0; });
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: a lambda that captures 'this' of actor 'CRound53Data' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
	}

	static TCFuture<int> fs_Alias(CRound53Data *_pActor)
	{
		auto *pAlias = _pActor;
		co_return co_await (fg_ThisActor(_pActor) / [pAlias] { return pAlias->m_Value; });
	}

	void f_Static(TCActor<> _Other)
	{
		int const *pValue = &this->mc_Value;
		(void)(_Other / [pValue] { return *pValue; });
	}
};

// Either value of a conditional, and a copy of a value read through a
// reference to the object of an actor, which is a value of its own.
struct CRound54Data : CActor
{
	int m_Value = 0;
	void f_Chosen(bool _bChoose)
	{
		static int s_Other = 0;
		(void)fg_ConcurrentDispatch([pValue = _bChoose ? &m_Value : &s_Other] { return *pValue; });
// CHECK-MESSAGES: :[[@LINE-1]]:32: warning: a lambda that captures 'this' of actor 'CRound54Data' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
	}
	static void fs_Copied(CRound54Data *_pActor)
	{
		int &Value = _pActor->m_Value;
		(void)fg_ConcurrentDispatch([Value] { return Value; });
		(void)fg_ConcurrentDispatch([Copy = _pActor->m_Value] { return Copy; });
	}
};

// A scope exit that dispatches to another actor, and a pointer to the object of
// an actor chosen by a conditional.
struct CRound55Actor : CActor
{
	int m_Value = 0;
	void f_ScopeExit(TCActor<> _Other)
	{
		auto Cleanup = g_OnScopeExitActor(_Other) / [this] { ++m_Value; };
// CHECK-MESSAGES: :[[@LINE-1]]:48: warning: a lambda that captures 'this' of actor 'CRound55Actor' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
		auto OwnCleanup = g_OnScopeExitActor / [this] { ++m_Value; };
	}
	static void fs_Chosen(CRound55Actor *_pActor, bool _bChoose)
	{
		g_ConcurrentDispatch / [pActor = _bChoose ? _pActor : nullptr]
			{
				if (pActor)
					pActor->m_Value = 1;
			}
		;
// CHECK-MESSAGES: :[[@LINE-6]]:27: warning: a lambda that captures 'pActor', which points or refers to the {{.*}}
	}
};

// A function wrapper held in a local and moved to another actor, and a local
// pointer assigned the data of an actor.
struct CRound58Actor : CActor
{
	int m_Value = 0;
	void f_Wrapper(TCActor<> _Other)
	{
		TCFunction<int ()> fCallback{[this] { return ++m_Value; }};
// CHECK-MESSAGES: :[[@LINE-1]]:33: warning: a lambda that captures 'this' of actor 'CRound58Actor' runs on another actor, {{.*}}
		(void)fg_Dispatch(_Other, fg_Move(fCallback));
	}
	static void fs_Assigned(CRound58Actor *_pActor)
	{
		int *pValue = nullptr;
		pValue = &_pActor->m_Value;
		(void)fg_ConcurrentDispatch([pValue] { return *pValue; });
// CHECK-MESSAGES: :[[@LINE-1]]:32: warning: a lambda that captures 'pValue', which points or refers to the {{.*}}
	}
};

// A timer on the own actor with a capture of another actor's object.
struct CRound59Timer : CActor
{
	int m_Value = 0;
	void f_Timer(CRound59Timer *_pOther)
	{
		(void)fg_RegisterTimer
			(
				1.0
				, [_pOther]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: a lambda that captures '_pOther', which points or refers to the {{.*}}
				{
					co_return _pOther->m_Value;
				}
			)
		;
	}
};

// A function wrapper assigned another lambda dispatches the one it was last
// assigned.
struct CRound61Actor : CActor
{
	int m_Value = 0;
	void f_Assigned(TCActor<> _Other)
	{
		TCFunction<int ()> fCallback{[] { return 0; }};
		fCallback = [this] { return m_Value; };
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: a lambda that captures 'this' of actor 'CRound61Actor' runs on another actor, {{.*}}
		(void)fg_Dispatch(_Other, fg_Move(fCallback));
	}
	void f_Replaced(TCActor<> _Other)
	{
		TCFunction<int ()> fCallback{[this] { return m_Value; }};
		fCallback = [] { return 0; };
		(void)fg_Dispatch(_Other, fg_Move(fCallback));
	}
};

// Each value a conditional capture can be is checked, and so is an offset
// into an array member of the object of an actor.
struct CRound62Actor : CActor
{
	int m_Value = 0;
	int m_Values[2] = {};
};
CRound62Actor *fg_Round62Other();
void fg_Round62Choose(CRound62Actor *_pFirst, bool _bChoose)
{
	CRound62Actor *pSecond = fg_Round62Other();
	(void)fg_Dispatch(fg_ThisActor(_pFirst), [pActor = _bChoose ? _pFirst : pSecond] { return pActor->m_Value; });
// CHECK-MESSAGES: :[[@LINE-1]]:44: warning: a lambda that captures 'pActor', which points or refers to the object of actor 'CRound62Actor', runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
}
TCFuture<int> fg_Round62Offset(CRound62Actor *_pActor)
{
	return fg_ConcurrentDispatch([pValue = _pActor->m_Values + 1] { return ++*pValue; });
// CHECK-MESSAGES: :[[@LINE-1]]:32: warning: a lambda that captures 'pValue', which points or refers to the object of actor 'CRound62Actor', runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
}

// A pointer to the own object assigned another through a conditional isn't
// the own object, and a callback assigned in a branch can be either lambda.
struct CRound63Actor : CActor
{
	int m_Value = 0;

	void f_Conditional(CRound63Actor *_pOther, bool _bChoose)
	{
		auto *pSelf = this;
		auto *pSpare = this;
		(_bChoose ? pSelf : pSpare) = _pOther;
		g_Dispatch(fg_ThisActor(pSelf)) / [this] { return m_Value; };
// CHECK-MESSAGES: :[[@LINE-1]]:27: warning: 'fg_ThisActor' is given the object of an actor, which it uses like 'this' on the actor the call runs on, but this may be of another actor; give it the object of the current actor, like 'this', or call it on the actor of the object [malterlib-actor-dispatch-this]
// CHECK-MESSAGES: :[[@LINE-2]]:38: warning: a lambda that captures 'this' of actor 'CRound63Actor' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
	}

	void f_Branch(bool _bChoose, TCActor<> _Other)
	{
		TCFunction<int ()> fCallback{[] { return 0; }};
		if (_bChoose)
			fCallback = [this] { return ++m_Value; };
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: a lambda that captures 'this' of actor 'CRound63Actor' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
		(void)fg_Dispatch(_Other, fg_Move(fCallback));
	}
};

// Either function object of a conditional is dispatched, and a branch of an
// `if constexpr` that is discarded never runs.
struct CRound64Actor : CActor
{
	int m_Value = 0;

	void f_Conditional(TCActor<> _Other, bool _bChoose)
	{
		(void)fg_Dispatch(_Other, _bChoose ? TCFunction<int ()>{[this] { return m_Value; }} : TCFunction<int ()>{[] { return 0; }});
// CHECK-MESSAGES: :[[@LINE-1]]:60: warning: a lambda that captures 'this' of actor 'CRound64Actor' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
	}

	void f_Discarded(TCActor<> _Other)
	{
		if constexpr (false)
			(void)fg_Dispatch(_Other, [this] { return m_Value; });
	}
};

// A handle derived from TCActor dispatches like it, and an actor call of
// f_DispatchWithReturn runs what it is given on the actor.
template <typename t_CActor>
struct TCRound68Interface : TCActor<t_CActor>
{
};
struct CRound68Actor : CActor
{
	int m_Value = 0;

	TCFuture<int> f_Interface(TCRound68Interface<CActor> _Other)
	{
		co_return co_await (_Other / [this] { return m_Value; });
// CHECK-MESSAGES: :[[@LINE-1]]:33: warning: a lambda that captures 'this' of actor 'CRound68Actor' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
	}

	TCFuture<int> f_ActorCall(TCActor<> _Other)
	{
		co_return co_await _Other(&CActor::f_DispatchWithReturn<TCFuture<int>>, [this]() -> TCFuture<int> { co_return m_Value; });
// CHECK-MESSAGES: :[[@LINE-1]]:76: warning: a lambda that captures 'this' of actor 'CRound68Actor' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
	}
};

// A pointer changed after the handle was made from it may be of another actor.
struct CRound70Actor : CActor
{
	int m_Value;
};
void fg_Round70Changed(CRound70Actor *_pActor, CRound70Actor *_pOther)
{
	auto Target = fg_ThisActor(_pActor);
// CHECK-MESSAGES: :[[@LINE-1]]:29: warning: 'fg_ThisActor' is given the object of an actor, which it uses like 'this' on the actor the call runs on, but this may be of another actor; give it the object of the current actor, like 'this', or call it on the actor of the object [malterlib-actor-dispatch-this]
	_pActor = _pOther;
	Target / [_pActor]
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: a lambda that captures '_pActor', which points or refers to the object of actor 'CRound70Actor', runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
		{
			return _pActor->m_Value;
		}
	;
}

// A copy of a pointer into the object is so also when what it was copied from
// is changed after.
struct CRound71Copy : CActor
{
	int m_Value;

	TCFuture<int> f_Copy(TCActor<> _Other)
	{
		int *pValue = &m_Value;
		int *pCopy = pValue;
		pValue = nullptr;
		co_return co_await (_Other / [pCopy] { return *pCopy; });
// CHECK-MESSAGES: :[[@LINE-1]]:33: warning: a lambda that captures 'this' of actor 'CRound71Copy' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
	}
};

// A type reached through many paths of shared bases is looked into once per
// base.
template <int t_Depth>
struct TCRound74Right;
template <int t_Depth>
struct TCRound74Left : virtual TCRound74Left<t_Depth - 1>, virtual TCRound74Right<t_Depth - 1>
{
};
template <int t_Depth>
struct TCRound74Right : virtual TCRound74Left<t_Depth - 1>, virtual TCRound74Right<t_Depth - 1>
{
};
template <>
struct TCRound74Left<0>
{
};
template <>
struct TCRound74Right<0>
{
};
template <typename t_CObject, typename t_FFunction>
int operator / (t_CObject const &_Object, t_FFunction &&_fFunction)
{
	return 0;
}
#define DRound74Call _Shared / [] {};
#define DRound74Ten DRound74Call DRound74Call DRound74Call DRound74Call DRound74Call DRound74Call DRound74Call DRound74Call DRound74Call DRound74Call
void fg_Round74Shared(TCRound74Left<20> const &_Shared)
{
	DRound74Ten DRound74Ten DRound74Ten DRound74Ten DRound74Ten
	DRound74Ten DRound74Ten DRound74Ten DRound74Ten DRound74Ten
}

// A function object assigned itself is followed once.
struct CRound75SelfAssigned : CActor
{
	int m_Value;

	void f_Run(TCActor<> _Other, bool _bChoose)
	{
		TCFunction<int ()> fCallback = [this] { return m_Value; };
// CHECK-MESSAGES: :[[@LINE-1]]:35: warning: a lambda that captures 'this' of actor 'CRound75SelfAssigned' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
		if (_bChoose)
			fCallback = fCallback;
		if (_bChoose)
			fCallback = fCallback;
		if (_bChoose)
			fCallback = fCallback;
		if (_bChoose)
			fCallback = fCallback;
		if (_bChoose)
			fCallback = fCallback;
		if (_bChoose)
			fCallback = fCallback;
		if (_bChoose)
			fCallback = fCallback;
		if (_bChoose)
			fCallback = fCallback;
		if (_bChoose)
			fCallback = fCallback;
		if (_bChoose)
			fCallback = fCallback;
		_Other / fCallback;
	}
};

// A reference that a helper gives back as it is.
template <typename t_CType>
t_CType const &fg_Round81Const(t_CType const &_Value) noexcept
{
	return _Value;
}
struct CRound81Const : CActor
{
	int m_Value;

	void f_Run(TCActor<> _Other)
	{
		int const &Alias = fg_Round81Const(m_Value);
		g_Dispatch(_Other) / [&Alias]
// CHECK-MESSAGES: :[[@LINE-1]]:26: warning: a lambda that captures 'this' of actor 'CRound81Const' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
			{
				return Alias;
			}
		;
	}
};

// Nested awaits in a dispatched lambda are looked at once each, not once per
// path through the operands, which is exponential in their depth.
TCFuture<int> fg_Round82Work(int _Value);
#define DRound82Await(d_Value) co_await fg_Round82Work(d_Value)
#define DRound82Await4(d_Value) DRound82Await(DRound82Await(DRound82Await(DRound82Await(d_Value))))
#define DRound82Await32(d_Value) DRound82Await4(DRound82Await4(DRound82Await4(DRound82Await4(DRound82Await4(DRound82Await4(DRound82Await4(DRound82Await4(d_Value))))))))
struct CRound82Nested : CActor
{
	int m_Value;

	void f_Run(TCActor<> _Other)
	{
		g_Dispatch(_Other) / [this]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:25: warning: a lambda that captures 'this' of actor 'CRound82Nested' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
			{
				co_return DRound82Await32(m_Value);
			}
		;
	}
};

// An init capture of a pointer to an actor made of what can't be told is that
// actor's object.
struct CRound83Actor : CActor
{
	int m_Value;
};
CRound83Actor *fg_Round83GetActor();
TCFuture<int> fg_Round83Init(TCActor<> _Other)
{
	co_return co_await (_Other / [pActor = fg_Round83GetActor()] { return ++pActor->m_Value; });
// CHECK-MESSAGES: :[[@LINE-1]]:32: warning: a lambda that captures 'pActor', which points or refers to the object of actor 'CRound83Actor', runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
}
// But not the value of a pointer member, which is dispatched to the actor it
// is of.
struct CRound83Internal : CActorInternal
{
	int m_Value;
};
struct CRound83Owner : CActor
{
	CRound83Internal *mp_pInternal;
	TCActor<> mp_Owner;

	TCFuture<int> f_Run()
	{
		co_return co_await (mp_Owner / [pInternal = mp_pInternal] { return ++pInternal->m_Value; });
	}
};

// A saved pointer to the dispatch function dispatches too, and what is given
// to the function, like the object, is used on the other actor.
namespace NMib::NConcurrency
{
	template <typename t_FFunction, typename ...tp_CArguments>
	TCFuture<int> fg_Dispatch(TCActor<> const &_Actor, t_FFunction &&_fFunction, tp_CArguments &&..._Arguments);
}
struct CRound87Actor : CActor
{
	int m_Value;

	TCFuture<int> f_Saved(TCActor<> _Other)
	{
		auto fDispatch = &CActor::f_DispatchWithReturn<int>;
		co_await _Other(fDispatch, [this] { return m_Value; });
// CHECK-MESSAGES: :[[@LINE-1]]:31: warning: a lambda that captures 'this' of actor 'CRound87Actor' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
		co_return 0;
	}

	void f_Given(TCActor<> _Other)
	{
		fg_Dispatch
			(
				_Other
				, [](CRound87Actor *_pActor)
				{
					return _pActor->m_Value;
				}
				, this
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: the object of actor 'CRound87Actor' is given to a function that runs on another actor, where using it races with its own actor; give copies of what it needs instead [malterlib-actor-dispatch-this]
			)
		;
		fg_Dispatch
			(
				_Other
				, [](int _Value)
				{
					return _Value;
				}
				, m_Value
			)
		;
	}
};

// Also a local alias of the object given to the function.
struct CRound89Actor : CActor
{
	int m_Value;

	void f_Given(TCActor<> _Other)
	{
		auto *pThis = this;
		fg_Dispatch
			(
				_Other
				, [](CRound89Actor *_pActor)
				{
					return _pActor->m_Value;
				}
				, pThis
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: the object of actor 'CRound89Actor' is given to a function that runs on another actor, where using it races with its own actor; give copies of what it needs instead [malterlib-actor-dispatch-this]
			)
		;
	}
};

// A dispatch in a template whose arguments are dependent.
struct CRound90Actor : CActor
{
	int m_Value;
	template <typename t_CType>
	void f_Start(t_CType _Value)
	{
		fg_ConcurrentDispatch
			(
				[this, _Value]
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: a lambda that captures 'this' of actor 'CRound90Actor' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
				{
					m_Value = _Value;
				}
			)
		;
	}
};
void fg_Round90Use(CRound90Actor &_Actor)
{
	_Actor.f_Start(1);
}

// A pointer pointed into the object through a chain of references to it, a
// swap with a local that points into it, or a long chain of copies, is still
// into the object.
template <typename t_CType>
void fg_Swap(t_CType &_Left, t_CType &_Right);

struct CRound93DispatchAliases : public CActor
{
	int m_Value;
	TCActor<> m_Other;

	void f_ThroughAliases()
	{
		int *pValue = nullptr;
		int *&pAlias = pValue;
		int *&pOther = pAlias;
		pOther = &m_Value;
		fg_Dispatch(m_Other, [pValue] { *pValue = 1; });
// CHECK-MESSAGES: :[[@LINE-1]]:25: warning: a lambda that captures 'this' of actor 'CRound93DispatchAliases' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
	}

	void f_Swapped()
	{
		int *pValue = nullptr;
		int *pOther = &m_Value;
		fg_Swap(pValue, pOther);
		fg_Dispatch(m_Other, [pValue] { *pValue = 1; });
// CHECK-MESSAGES: :[[@LINE-1]]:25: warning: a lambda that captures 'this' of actor 'CRound93DispatchAliases' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
	}

	void f_SwappedNull()
	{
		int *pValue = nullptr;
		int *pOther = nullptr;
		fg_Swap(pValue, pOther);
		fg_Dispatch(m_Other, [pValue] { if (pValue) *pValue = 1; });
	}

	void f_Copies()
	{
		int *p0 = &m_Value, *p1 = p0, *p2 = p1, *p3 = p2, *p4 = p3, *p5 = p4, *p6 = p5, *p7 = p6, *p8 = p7;
		fg_Dispatch(m_Other, [p8] { *p8 = 1; });
// CHECK-MESSAGES: :[[@LINE-1]]:25: warning: a lambda that captures 'this' of actor 'CRound93DispatchAliases' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
	}
};

// What a helper returns of its argument, `fg_Round93Identity(&m_Value)`.
int *fg_Round93Identity(int *_pValue)
{
	return _pValue;
}

struct CRound93DispatchIdentity : public CActor
{
	int m_Value;

	void f_Dispatch(TCActor<> _Other)
	{
		_Other / [pValue = fg_Round93Identity(&m_Value)] { return *pValue; };
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: a lambda that captures 'this' of actor 'CRound93DispatchIdentity' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
	}
};

// Chains of references through conditionals are followed once each, also
// long ones, and a reference to a constant global isn't into the object.
int const gc_Round94Global = 7;
#define DRound94Reference(d_To, d_From) int const &r##d_To = _bChoose ? r##d_From : r##d_From

struct CRound94ReferenceChain : CActor
{
	void f_Run(bool _bChoose, TCActor<> _Other)
	{
		int const &r0 = gc_Round94Global;
		DRound94Reference(1, 0); DRound94Reference(2, 1); DRound94Reference(3, 2); DRound94Reference(4, 3);
		DRound94Reference(5, 4); DRound94Reference(6, 5); DRound94Reference(7, 6); DRound94Reference(8, 7);
		DRound94Reference(9, 8); DRound94Reference(10, 9); DRound94Reference(11, 10); DRound94Reference(12, 11);
		DRound94Reference(13, 12); DRound94Reference(14, 13); DRound94Reference(15, 14); DRound94Reference(16, 15);
		DRound94Reference(17, 16); DRound94Reference(18, 17); DRound94Reference(19, 18); DRound94Reference(20, 19);
		DRound94Reference(21, 20); DRound94Reference(22, 21); DRound94Reference(23, 22); DRound94Reference(24, 23);
		DRound94Reference(25, 24); DRound94Reference(26, 25); DRound94Reference(27, 26); DRound94Reference(28, 27);
		DRound94Reference(29, 28); DRound94Reference(30, 29); DRound94Reference(31, 30); DRound94Reference(32, 31);
		DRound94Reference(33, 32); DRound94Reference(34, 33); DRound94Reference(35, 34); DRound94Reference(36, 35);
		g_Dispatch(_Other) / [&Value = r36]
			{
				return Value;
			}
		;
	}
};

// The first value of `?:`, which is its condition, and only the value a
// constant condition chooses.
constexpr int gc_Round95Global = 7;

struct CRound95Conditional : CActor
{
	int m_Value;
	TCActor<> m_Other;

	void f_Elvis()
	{
		auto pValue = &m_Value ?: &gc_Round95Global;
		m_Other / [pValue] { return *pValue; };
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a lambda that captures 'this' of actor 'CRound95Conditional' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
	}

	void f_Constant()
	{
		auto pValue = false ? &m_Value : &gc_Round95Global;
		m_Other / [pValue] { return *pValue; };
	}
};

// Through many copies of the lambda.
struct CRound100Copies : CActor
{
	int m_Value;
	void f_Run(TCActor<> _Other)
	{
		auto fFirst = [this]
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: a lambda that captures 'this' of actor 'CRound100Copies' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
			{
				++m_Value;
			}
		;
		auto fSecond = fFirst;
		auto fThird = fSecond;
		auto fFourth = fThird;
		g_Dispatch(_Other) / fFourth;
	}
};

// What a lookup into the object points to.
struct CRound102Map
{
	int *f_FindEqual(int _Key);
};

struct CRound102Deref : CActor
{
	CRound102Map m_Items;
	void f_Run(TCActor<> _Other)
	{
		auto &Entry = *m_Items.f_FindEqual(1);
		g_Dispatch(_Other) / [&Entry]
// CHECK-MESSAGES: :[[@LINE-1]]:26: warning: a lambda that captures 'this' of actor 'CRound102Deref' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
			{
				return Entry;
			}
		;
	}
};

// Dispatched on the current actor, with the object of another actor, also
// given after the function.
namespace NMib::NConcurrency
{
	template <typename tf_FFunction, typename tf_CParam>
	TCFuture<int> fg_Dispatch(TCActor<> const &_Actor, tf_FFunction &&_fFunction, tf_CParam &&_Param);
}

struct CRound104Current : CActor
{
	int m_Value;
	TCFuture<int> f_Self(CRound104Current *_pOther)
	{
		return self / [_pOther]
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: a lambda that captures '_pOther', which points or refers to the object of actor 'CRound104Current', runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
			{
				return _pOther->m_Value;
			}
		;
	}
	TCFuture<int> f_Dispatch(CRound104Current *_pOther)
	{
		return g_Dispatch / [_pOther]
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a lambda that captures '_pOther', which points or refers to the object of actor 'CRound104Current', runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
			{
				return _pOther->m_Value;
			}
		;
	}
	TCFuture<int> f_Given(CRound104Current *_pOther)
	{
		return fg_Dispatch
			(
				fg_ThisActor(this)
				, [](CRound104Current *_pActor)
				{
					return _pActor->m_Value;
				}
				, _pOther
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: the object of actor 'CRound104Current' is given to a function that runs on another actor, where using it races with its own actor; give copies of what it needs instead [malterlib-actor-dispatch-this]
			)
		;
	}
};

// `self` of another object dispatches to the current actor too.
struct CRound109OtherSelf : CActor
{
	int m_Value = 0;

	void f_Run(CRound109OtherSelf *_pOther)
	{
		_pOther->self / [_pOther]
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: a lambda that captures '_pOther', which points or refers to the object of actor 'CRound109OtherSelf', runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
			{
				return _pOther->m_Value;
			}
		;
	}
};

// A free or static function given the object of an actor uses it like `this`
// on the actor the call runs on, so it is given the object of that actor:
// `this`, a parameter given it in turn, or an unchanged alias of them, or
// none, but not another, nor a parameter that is changed, also of a template
// that its instantiation makes take the object of an actor.
struct CRound111Actor : CActor
{
	int m_Value = 0;
	CRound111Actor *m_pOther = nullptr;

	static int fs_Read(CRound111Actor *_pThis)
	{
		return _pThis->m_Value;
	}
	static int fs_Forward(CRound111Actor &_This)
	{
		return fs_Read(&_This);
	}
	int f_Run()
	{
		auto *pThis = this;
		return fs_Read(this) + fs_Read(pThis) + fs_Forward(*this) + fs_Read(nullptr) + fs_Read(m_pOther);
// CHECK-MESSAGES: :[[@LINE-1]]:90: warning: 'fs_Read' is given the object of an actor, which it uses like 'this' on the actor the call runs on, but this may be of another actor; give it the object of the current actor, like 'this', or call it on the actor of the object [malterlib-actor-dispatch-this]
	}
};
int fg_Round111Changed(CRound111Actor *_pActor, CRound111Actor *_pOther)
{
	_pOther = _pActor->m_pOther;
	return CRound111Actor::fs_Read(_pActor) + CRound111Actor::fs_Read(_pOther);
// CHECK-MESSAGES: :[[@LINE-1]]:68: warning: 'fs_Read' is given the object of an actor, which it uses like 'this' on the actor the call runs on, but this may be of another actor; give it the object of the current actor, like 'this', or call it on the actor of the object [malterlib-actor-dispatch-this]
}
template <typename t_CActor>
int fg_Round111Generic(t_CActor *_pActor)
{
	return _pActor->m_Value;
}
int fg_Round111UseGeneric(CRound111Actor *_pActor)
{
	return fg_Round111Generic(_pActor->m_pOther);
// CHECK-MESSAGES: :[[@LINE-1]]:28: warning: 'fg_Round111Generic' is given the object of an actor, which it uses like 'this' on the actor the call runs on, but this may be of another actor; give it the object of the current actor, like 'this', or call it on the actor of the object [malterlib-actor-dispatch-this]
}
// A function that only gives back what it is given, like fg_Forward and
// fg_Move, isn't reported: what is done with its result is.
int fg_Round112Forward(CRound111Actor *_pActor)
{
	CRound111Actor &Other = fg_Forward<CRound111Actor &>(*_pActor->m_pOther);
	CRound111Actor &&Moved = fg_Move(*_pActor->m_pOther);
	return CRound111Actor::fs_Read(&fg_Forward<CRound111Actor &>(*_pActor->m_pOther)) + Other.m_Value + Moved.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:33: warning: 'fs_Read' is given the object of an actor, which it uses like 'this' on the actor the call runs on, but this may be of another actor; give it the object of the current actor, like 'this', or call it on the actor of the object [malterlib-actor-dispatch-this]
}
// Nor one that only constructs or destroys with the object, like
// fg_DeleteObject and fg_ConstructObject, which manage it, but one that also
// uses it is.
struct CRound112Holder
{
	CRound112Holder(CRound111Actor &_Actor)
		: m_pActor(&_Actor)
	{
	}
	CRound111Actor *m_pActor;
};
template <typename t_CObject>
void fg_Round112Destroy(t_CObject *_pObject)
{
	if (_pObject)
		_pObject->~t_CObject();
	::operator delete(static_cast<void *>(_pObject));
}
template <typename t_CObject>
void fg_Round112Delete(t_CObject *_pObject)
{
	if (_pObject != nullptr)
		delete _pObject;
}
template <typename t_CObject, typename... tp_CParams>
t_CObject *fg_Round112Construct(tp_CParams &&...p_Params)
{
	return new t_CObject(fg_Forward<tp_CParams>(p_Params)...);
}
template <typename t_CObject>
int fg_Round112DeleteAndRead(t_CObject *_pObject)
{
	int Value = _pObject->m_Value;
	delete _pObject;
	return Value;
}
int fg_Round112Manage(CRound111Actor *_pActor)
{
	fg_Round112Destroy(_pActor->m_pOther);
	fg_Round112Delete(_pActor->m_pOther);
	delete fg_Round112Construct<CRound112Holder>(*_pActor->m_pOther);
	return fg_Round112DeleteAndRead(_pActor->m_pOther);
// CHECK-MESSAGES: :[[@LINE-1]]:34: warning: 'fg_Round112DeleteAndRead' is given the object of an actor, which it uses like 'this' on the actor the call runs on, but this may be of another actor; give it the object of the current actor, like 'this', or call it on the actor of the object [malterlib-actor-dispatch-this]
}
// A member function of a class that isn't an actor, nor a helper of one, is
// given the object of the actor it runs on like a free function, and its
// parameter is that object: also given on through a forwarding reference,
// which doesn't change it, but not once something assigns it through one.
template <typename... tp_CParams>
int fg_Round112Take(tp_CParams &&...p_Params)
{
	return sizeof...(p_Params);
}
template <typename t_CType>
void fg_Round112Reset(t_CType &&_Value)
{
	_Value = nullptr;
}
struct CRound112Interface
{
	template <typename tf_CThis>
	int f_Publish(tf_CThis *_pThis)
	{
		return fg_Round112Take(_pThis) + CRound111Actor::fs_Read(_pThis);
	}
	int f_Reset(CRound111Actor *_pThis)
	{
		fg_Round112Reset(_pThis);
		return CRound111Actor::fs_Read(_pThis);
// CHECK-MESSAGES: :[[@LINE-1]]:34: warning: 'fs_Read' is given the object of an actor, which it uses like 'this' on the actor the call runs on, but this may be of another actor; give it the object of the current actor, like 'this', or call it on the actor of the object [malterlib-actor-dispatch-this]
	}
};
// A helper of an actor, which derives from CActorInternal, uses the object
// of the actor it is constructed with like 'this', so it must be constructed
// with the object of the current actor, also through fg_Construct, but not
// in generic code that gives on what it is given.
namespace NMib
{
	template <typename t_CParam>
	struct TCConstruct
	{
		t_CParam &&m_Param;
	};
	template <typename t_CParam>
	TCConstruct<t_CParam> fg_Construct(t_CParam &&_Param)
	{
		return {static_cast<t_CParam &&>(_Param)};
	}
	template <typename t_CType>
	struct TCUniquePointer
	{
		template <typename t_CParam>
		TCUniquePointer(TCConstruct<t_CParam> &&_Construct)
			: m_pData(new t_CType(static_cast<t_CParam &&>(_Construct.m_Param)))
		{
		}
		t_CType *m_pData;
	};
}
struct CRound112Helper : CActorInternal
{
	CRound112Helper(CRound111Actor *_pThis)
		: m_pThis(_pThis)
	{
	}
	CRound111Actor *m_pThis;
};
struct CRound112Owner : CRound111Actor
{
	CRound112Owner()
		: mp_pInternal(NMib::fg_Construct(this))
		, mp_pOther(NMib::fg_Construct(m_pOther))
// CHECK-MESSAGES: :[[@LINE-1]]:34: warning: 'CRound112Helper' is constructed with the object of an actor, which it uses like 'this' on the actor it runs on, but this may be of another actor; give it the object of the current actor, like 'this' [malterlib-actor-dispatch-this]
	{
		CRound112Helper Direct(this);
		CRound112Helper Other(m_pOther);
// CHECK-MESSAGES: :[[@LINE-1]]:25: warning: 'CRound112Helper' is constructed with the object of an actor, {{.*}}
	}
	NMib::TCUniquePointer<CRound112Helper> mp_pInternal;
	NMib::TCUniquePointer<CRound112Helper> mp_pOther;
};

struct CRound112Publisher : CRound111Actor
{
	CRound112Interface m_Interface;
	int f_Run()
	{
		return m_Interface.f_Publish(this) + m_Interface.f_Publish(m_pOther) + m_Interface.f_Reset(this);
// CHECK-MESSAGES: :[[@LINE-1]]:62: warning: 'f_Publish' is given the object of an actor, which it uses like 'this' on the actor the call runs on, but this may be of another actor; give it the object of the current actor, like 'this', or call it on the actor of the object [malterlib-actor-dispatch-this]
	}
};

// So such a parameter is the object of the current actor: a lambda that
// captures it runs on its actor with `g_Dispatch /`, but not on another, nor
// does another function on another actor get it.
TCFuture<int> fg_Round111Own(CRound111Actor *_pActor, TCActor<> _Other)
{
	auto *pOther = _pActor->m_pOther;
	co_await (g_Dispatch / [_pActor] { return _pActor->m_Value; });
	co_await (g_Dispatch / [pOther] { return pOther->m_Value; });
// CHECK-MESSAGES: :[[@LINE-1]]:26: warning: a lambda that captures 'pOther', which points or refers to the object of actor 'CRound111Actor', runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
	co_await (_Other / [_pActor] { return _pActor->m_Value; });
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: a lambda that captures '_pActor', which points or refers to the object of actor 'CRound111Actor', runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
	co_return 0;
}
struct CRound111Helpers
{
	static void fs_Give(CRound111Actor *_pActor, TCActor<> _Other)
	{
		fg_Dispatch
			(
				_Other
				, [](CRound111Actor *_pGiven)
				{
					return _pGiven->m_Value;
				}
				, _pActor
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: the object of actor 'CRound111Actor' is given to a function that runs on another actor, where using it races with its own actor; give copies of what it needs instead [malterlib-actor-dispatch-this]
			)
		;
	}
};

// What the object owns, its internals, is of the current actor too, also
// through a local reference to it.
template <typename t_CType>
struct TCRound111Owned
{
	t_CType &operator * () const;
	t_CType *operator -> () const;
};
struct CRound111Owner : CActor
{
	struct CInternal : CActorInternal
	{
		int m_Value = 0;
		static int fs_Read(CInternal *_pThis)
		{
			return _pThis->m_Value;
		}
	};
	TCRound111Owned<CInternal> mp_pInternal;
	CInternal *mp_pOther = nullptr;

	int f_Run()
	{
		auto &Internal = *mp_pInternal;
		return CInternal::fs_Read(&Internal) + CInternal::fs_Read(&*mp_pInternal) + CInternal::fs_Read(mp_pOther);
// CHECK-MESSAGES: :[[@LINE-1]]:98: warning: 'fs_Read' is given the object of an actor, which it uses like 'this' on the actor the call runs on, but this may be of another actor; give it the object of the current actor, like 'this', or call it on the actor of the object [malterlib-actor-dispatch-this]
	}
};

// In a template, a parameter that its instantiations make the object of an
// actor is that of the current actor too.
template <typename t_CActor>
TCFuture<int> fg_Round111TemplateOwn(t_CActor *_pActor, TCActor<> _Other)
{
	co_await (g_Dispatch / [_pActor] { return _pActor->m_Value; });
	co_await (_Other / [_pActor] { return _pActor->m_Value; });
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: a lambda that captures '_pActor', which points or refers to the object of actor 'CRound111Actor', runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
	co_return 0;
}
TCFuture<int> fg_Round111UseTemplateOwn(CRound111Actor *_pActor, TCActor<> _Other)
{
	return fg_Round111TemplateOwn(_pActor, _Other);
}

// A void * given to a function whose body can be seen is looked into.
namespace NRound115VoidPointer {
struct App;
int read(void *);
int helper(App *p) { return read(p); }
struct App : CActor {
  int value;
  int f(App *other) { return helper(other); }
// CHECK-MESSAGES: :[[@LINE-1]]:37: warning: 'helper' is given the object of an actor, which it uses like 'this' on the actor the call runs on, but this may be of another actor; give it the object of the current actor, like 'this', or call it on the actor of the object [malterlib-actor-dispatch-this]
};
int read(void *p) {
  return static_cast<App *>(p)->value;
}
}

// A constructor whose body can be seen is looked into.
namespace NRound115Constructor {
struct App;
struct Read {
  int value;
  Read(App *p);
};
int helper(App *p) { return Read(p).value; }
struct App : CActor {
  int value;
  int f(App *other) { return helper(other); }
// CHECK-MESSAGES: :[[@LINE-1]]:37: warning: 'helper' is given the object of an actor, which it uses like 'this' on the actor the call runs on, but this may be of another actor; give it the object of the current actor, like 'this', or call it on the actor of the object [malterlib-actor-dispatch-this]
};
Read::Read(App *p) : value(p->value) {}
}

// __builtin_addressof of a member is into the object like &.
namespace NRound116BuiltinAddress {
struct Actor : CActor {
  int value;
  TCActor<> other;
  void dispatch() {
    other / [p = __builtin_addressof(value)] {
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a lambda that captures 'this' of actor 'Actor' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
      return *p;
    };
  }
};
}

// Round 125: CActor::f_Dispatch runs what it is given on the actor, like
// f_DispatchWithReturn.
struct CRound125Actor : CActor
{
	int m_Value = 0;

	void f_Run(TCActor<> _Other)
	{
		_Other(&CActor::f_Dispatch, [this] { m_Value = 1; });
// CHECK-MESSAGES: :[[@LINE-1]]:32: warning: a lambda that captures 'this' of actor 'CRound125Actor' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
		_Other.f_Bind<&CActor::f_Dispatch>([this] { m_Value = 2; });
// CHECK-MESSAGES: :[[@LINE-1]]:39: warning: a lambda that captures 'this' of actor 'CRound125Actor' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
	}
};

// Round 129: what a function that calls itself back does isn't kept from
// before it is decided, also when another call asked it first.
namespace NRound129
{
struct CApp : CActor {
  int m_Value;
};

void fg_A(CApp *_pApp, int _Depth);
void fg_B(CApp *_pApp, int _Depth);

void fg_Prime() { fg_A(nullptr, 0); }

void fg_A(CApp *_pApp, int _Depth) {
  if (!_pApp)
    return;
  if (_Depth)
    fg_B(_pApp, _Depth - 1);
  ++_pApp->m_Value;
}

void fg_B(CApp *_pApp, int _Depth) { fg_A(_pApp, _Depth); }

struct COther : CActor {
  void f_Test(CApp *_pOther) { fg_B(_pOther, 0); }
// CHECK-MESSAGES: :[[@LINE-1]]:37: warning: 'fg_B' is given the object of an actor, which it uses like 'this' on the actor the call runs on, but this may be of another actor; give it the object of the current actor, like 'this', or call it on the actor of the object [malterlib-actor-dispatch-this]
};
}

// Round 135: a default argument is what it is made of, and a conditional of
// handles of the own actor is one.
namespace NRound135
{
struct CApp : CActor {
  int m_Value;
  void f_Run();
  TCFuture<int> f_Dispatch(bool _bChoose);
};

extern CApp *g_pOther;

int fg_Read(CApp *_pApp = g_pOther) { return _pApp->m_Value; }

void CApp::f_Run() { (void)fg_Read(); }
// CHECK-MESSAGES: :[[@LINE-1]]:28: warning: 'fg_Read' is given the object of an actor, which it uses like 'this' on the actor the call runs on, but this may be of another actor; give it the object of the current actor, like 'this', or call it on the actor of the object [malterlib-actor-dispatch-this]

TCFuture<int> CApp::f_Dispatch(bool _bChoose) {
  auto Own = fg_ThisActor(this);
  auto OwnCopy = Own;
  co_return co_await ((_bChoose ? Own : OwnCopy) / [this]() -> TCFuture<int> {
    co_return m_Value;
  });
}
}

// Round 136: what a pointer points into is followed through any number of
// copies, and of a pointer assigned more than once, the last assignment.
namespace NRound136
{
struct CApp : CActor {
  int m_Value;
  void f_Run(CApp *_pOther);
};

void fg_Copies(CApp *_pActor) {
  int *p0 = &_pActor->m_Value;
  int *p1 = p0;
  int *p2 = p1;
  int *p3 = p2;
  int *p4 = p3;
  int *p5 = p4;
  fg_ConcurrentDispatch([p5] { return *p5; });
// CHECK-MESSAGES: :[[@LINE-1]]:26: warning: a lambda that captures 'p5', which points or refers to the object of actor 'CApp', runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
}

void CApp::f_Run(CApp *_pOther) {
  CApp *pOwn = this;
  int *pValue = nullptr;
  pValue = &pOwn->m_Value;
  pValue = &_pOther->m_Value;
  fg_Dispatch(fg_ThisActor(pOwn), [pValue] { return *pValue; });
// CHECK-MESSAGES: :[[@LINE-1]]:36: warning: a lambda that captures 'pValue', which points or refers to the object of actor 'CApp', runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
}
}

namespace NRound137 {
// What a wrapper is assigned after it is dispatched, which the dispatch has a
// copy of, doesn't run on the other actor.
struct CApp : CActor {
  int m_Value;
  void f_After(bool _bChoose, TCActor<> _Other) {
    TCFunction<int()> fCallback{[] { return 0; }};
    if (_bChoose)
      fCallback = [] { return 1; };
    fg_Dispatch(_Other, fCallback);
    fCallback = [this] { return m_Value; };
  }

  // But what it is assigned before it, and in a loop around it, does.
  void f_Before(bool _bChoose, TCActor<> _Other) {
    TCFunction<int()> fCallback{[] { return 0; }};
    if (_bChoose)
      fCallback = [this] { return m_Value; };
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: a lambda that captures 'this' of actor 'CApp' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
    fg_Dispatch(_Other, fCallback);
  }

  void f_Loop(int _nTimes, TCActor<> _Other) {
    TCFunction<int()> fCallback{[] { return 0; }};
    for (int iTime = 0; iTime < _nTimes; ++iTime) {
      if (iTime > 0)
        fg_Dispatch(_Other, fCallback);
      fCallback = [this] { return m_Value; };
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: a lambda that captures 'this' of actor 'CApp' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
    }
  }
};
}

namespace NRound141 {
struct CApp : CActor {
  int m_Value;
};
extern CApp *g_pOther;

// A helper constructed with the object a default argument gives,
// `CHelper(CApp *_pApp = g_pOther)`.
struct CHelper : CActorInternal {
  CHelper(CApp *_pApp = g_pOther) { _pApp->m_Value = 1; }
};

struct CCurrent : CActor {
  void f_Run() { CHelper Helper; }
// CHECK-MESSAGES: :[[@LINE-1]]:26: warning: 'CHelper' is constructed with the object of an actor, which it uses like 'this' on the actor it runs on, but this may be of another actor; give it the object of the current actor, like 'this' [malterlib-actor-dispatch-this]
};
}

namespace NRound142 {
// A dispatch through an unchanged local pointer to it, `auto fDispatch =
// &fg_ConcurrentDispatch<...>; fDispatch(fCallback)`.
struct CApp : CActor {
  int m_Value;
  void f_Pointer() {
    auto fCallback = [this] { return m_Value; };
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a lambda that captures 'this' of actor 'CApp' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
    auto fDispatch = &fg_ConcurrentDispatch<decltype(fCallback) &>;
    fDispatch(fCallback);
  }
};
}

namespace NRound143 {
struct CApp : CActor {
  int m_Value;
};
extern CApp *g_pOther;
constexpr int gc_Value = 0;

// A pointer captured before it is assigned, `pValue = nullptr` after, is what
// it was made with.
void fg_Before(CApp *_pApp, TCActor<> _Other) {
  int *pValue = &_pApp->m_Value;
  g_Dispatch(_Other) / [pValue] { return *pValue; };
// CHECK-MESSAGES: :[[@LINE-1]]:25: warning: a lambda that captures 'pValue', which points or refers to the object of actor 'CApp', runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
  pValue = nullptr;
}

// Each alias of a chain of conditionals of the same ones is told once, so the
// other value is told too.
void fg_Chain(CApp *_pSafe, bool _bChoose) {
  int *p0 = &_pSafe->m_Value;
  int *p1 = _bChoose ? p0 : p0;
  int *p2 = _bChoose ? p1 : p1;
  int *p3 = _bChoose ? p2 : p2;
  int *p4 = _bChoose ? p3 : p3;
  int *p5 = _bChoose ? p4 : p4;
  int *p6 = _bChoose ? p5 : p5;
  int *p7 = _bChoose ? p6 : p6;
  int *p8 = _bChoose ? p7 : p7;
  int *p9 = _bChoose ? p8 : p8;
  int *p10 = _bChoose ? p9 : p9;
  int *pValue = _bChoose ? p10 : &g_pOther->m_Value;
  g_Dispatch(fg_ThisActor(_pSafe)) / [pValue] { return *pValue; };
// CHECK-MESSAGES: :[[@LINE-1]]:39: warning: a lambda that captures 'pValue', which points or refers to the object of actor 'CApp', runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
}

// Only the value a constant condition chooses.
void fg_Constant(CApp *_pApp, TCActor<> _Other) {
  const int *pValue = false ? &_pApp->m_Value : &gc_Value;
  g_Dispatch(_Other) / [pValue] { return *pValue; };
}
}

namespace NRound145 {
using NMib::NFunction::TCFunction;

struct CApp : CActor {
  int m_Value;

  // Either of a conditional of closures, `bChoose ? fCallback : fCallback`.
  void f_Conditional(TCActor<> _Other, bool _bChoose) {
    auto fCallback = [this] { return m_Value; };
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a lambda that captures 'this' of actor 'CApp' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
    fg_Dispatch(_Other, _bChoose ? fCallback : fCallback);
  }

  // A dispatch through a dereferenced local pointer to it.
  void f_Dereferenced() {
    auto fCallback = [this] { return m_Value; };
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a lambda that captures 'this' of actor 'CApp' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
    auto fDispatch = &fg_ConcurrentDispatch<decltype(fCallback) &>;
    (*fDispatch)(fCallback);
  }

  // Only what a constant condition chooses.
  void f_Constant(TCActor<> _Other) {
    fg_Dispatch(_Other, false ? TCFunction<int()>{[this] { return m_Value; }}
                              : TCFunction<int()>{[] { return 1; }});
  }

  // The handle changed through a C-style cast of a const alias.
  void f_Changed(TCActor<> _Other) {
    TCActor<> Own = fg_ThisActor(this);
    auto const &Alias = Own;
    (TCActor<> &)Alias = _Other;
    fg_Dispatch(Own, [this] { return m_Value; });
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a lambda that captures 'this' of actor 'CApp' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
  }
};
}

namespace NRound147 {
struct CApp : CActor
{
	int m_Value = 0;
	TCActor<> m_Other;

	// A pointer into the actor through a comma or an assignment.
	void f_Comma()
	{
		m_Other / [pValue = (void(), &m_Value)]
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a lambda that captures 'this' of actor 'CApp' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
			{
				return *pValue;
			}
		;
	}

	void f_Assigned()
	{
		int *pValue = nullptr;
		m_Other / [pCopy = (pValue = &m_Value)]
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a lambda that captures 'this' of actor 'CApp' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
			{
				return *pCopy;
			}
		;
	}
};
}

namespace NRound148 {
int g_Value;
int &fg_Id(int &_Value)
{
	return _Value;
}
int &fg_Global(int const &_Value)
{
	return g_Value;
}

struct CApp : CActor
{
	int m_Value;

	// Nested helpers whose bodies can be seen are each looked at once.
	void f_Nested()
	{
		auto Other = fg_BlockingActor();
		g_Dispatch(Other) / [pValue = &fg_Id(fg_Id(fg_Id(fg_Id(fg_Id(fg_Id(fg_Id(fg_Id(fg_Id(fg_Id(fg_Id(fg_Id(fg_Id(fg_Id(fg_Id(fg_Id(fg_Id(fg_Id(fg_Id(fg_Id(fg_Id(fg_Id(fg_Id(fg_Id(fg_Id(fg_Id(fg_Id(fg_Id(fg_Id(fg_Id(fg_Id(fg_Id(g_Value))))))))))))))))))))))))))))))))] { return *pValue; };
	}

	// A helper that gives back a global, whatever it is given.
	void f_Global()
	{
		auto Other = fg_BlockingActor();
		g_Dispatch(Other) / [pValue = &fg_Global(m_Value)] { return *pValue; };
	}
};
}

namespace NRound149 {
// A graph of helpers that many paths share, which calls itself back, is
// looked into in time linear in its size.
struct CApp : CActor
{
};
bool fg_Again();
void fg_Root(CApp *_pApp);
#define DRound149Many(e) e; e; e; e; e; e; e; e; e; e; e; e; e; e; e; e;
void fg_F0(CApp *_pApp)
{
	if (fg_Again())
		fg_Root(_pApp);
}
void fg_F1(CApp *_pApp) { DRound149Many(fg_F0(_pApp)) }
void fg_F2(CApp *_pApp) { DRound149Many(fg_F1(_pApp)) }
void fg_F3(CApp *_pApp) { DRound149Many(fg_F2(_pApp)) }
void fg_F4(CApp *_pApp) { DRound149Many(fg_F3(_pApp)) }
void fg_F5(CApp *_pApp) { DRound149Many(fg_F4(_pApp)) }
void fg_F6(CApp *_pApp) { DRound149Many(fg_F5(_pApp)) }
void fg_F7(CApp *_pApp) { DRound149Many(fg_F6(_pApp)) }
void fg_Root(CApp *_pApp)
{
	fg_F7(_pApp);
}
}

namespace NRound152 {
// Through an unchanged local pointer to a static member function.
struct CApp : CActor
{
	int m_Value;
	CApp *m_pOther;

	static void fs_Update(CApp *_pApp)
	{
		++_pApp->m_Value;
	}

	void f_Indirect()
	{
		auto fUpdate = &CApp::fs_Update;
		fUpdate(m_pOther);
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: 'fs_Update' is given the object of an actor, which it uses like 'this' on the actor the call runs on, but this may be of another actor; give it the object of the current actor, like 'this', or call it on the actor of the object [malterlib-actor-dispatch-this]
	}

	// What a prefix increment changes, which it refers to.
	void f_Increment(TCActor<> _Other)
	{
		int &Value = ++m_Value;
		(void)(_Other / [&Value] { return Value; });
// CHECK-MESSAGES: :[[@LINE-1]]:21: warning: a lambda that captures 'this' of actor 'CApp' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
	}
};
}

namespace NRound154 {
struct CApp : CActor {
  int m_Value;
  // Its own actor converted by name.
  TCFuture<int> f_Dispatch() { co_return co_await (TCActor<>(self) / [this] { return m_Value; }); }
};
}

namespace NRound159 {
// The address a helper returns of what it is given.
int *fg_Address(int &_Value) { return &_Value; }
struct CApp : CActor {
  int m_Value;
  void f_Dispatch(TCActor<> _Other) { _Other / [pValue = fg_Address(m_Value)] { ++*pValue; }; }
// CHECK-MESSAGES: :[[@LINE-1]]:49: warning: a lambda that captures 'this' of actor 'CApp' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
};
}

namespace NRound164 {
int g_Value;
struct CApp : CActor {
  int m_Value = 0;
  // A pointer assigned through a reference bound to a conditional of it.
  void f_ConditionalAlias(bool _bChoose, TCActor<> _Other) {
    int *pFirst = &g_Value, *pSecond = &g_Value;
    int *&pAlias = _bChoose ? pFirst : pSecond;
    pAlias = &m_Value;
    (void)(_Other / [pFirst] { return *pFirst; });
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: a lambda that captures 'this' of actor 'CApp' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
  }
  // Through a copy of a local pointer to the dispatch.
  void f_CopiedPointer() {
    auto fRead = [this] { return m_Value; };
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: a lambda that captures 'this' of actor 'CApp' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
    auto fDispatch = &fg_ConcurrentDispatch<decltype(fRead) &>;
    auto fCopy = fDispatch;
    fCopy(fRead);
  }
};
}

namespace NMib::NConcurrency {
template <typename t_FFunction, typename t_CParameter>
TCFuture<int> fg_Dispatch(TCActor<> const &_Actor, t_FFunction &&_fFunction,
                          t_CParameter &&_Parameter);
}
namespace NRound166 {
struct CApp : CActor {
  int m_Value;
  void f_Run(CApp *_pOther, bool _bChoose);
};
struct CRead {
  int operator()(CApp *_pApp) const { return _pApp->m_Value; }
};
// Either of a conditional given on.
void CApp::f_Run(CApp *_pOther, bool _bChoose) {
  fg_Dispatch(fg_ThisActor(this), CRead{}, _bChoose ? _pOther : this);
// CHECK-MESSAGES: :[[@LINE-1]]:44: warning: the object of actor 'CApp' is given to a function that runs on another actor, where using it races with its own actor; give copies of what it needs instead [malterlib-actor-dispatch-this]
}
// Not the object of the actor it runs on.
void fg_RunOwn(CApp *_pOwn) {
  fg_Dispatch(fg_ThisActor(_pOwn), CRead{}, _pOwn);
// CHECK-MESSAGES: :[[@LINE-1]]:28: warning: 'fg_ThisActor' is given the object of an actor, which it uses like 'this' on the actor the call runs on, but this may be of another actor; give it the object of the current actor, like 'this', or call it on the actor of the object [malterlib-actor-dispatch-this]
}
}

namespace NRound171 {
// Not a timer given an unchanged empty actor, which runs on the current one.
struct CApp : CActor {
  int m_Value;
  void f_Start() {
    TCActor<> const Actor{};
    fg_RegisterTimer(
        1.0, [this]() -> TCFuture<int> { co_return m_Value; }, Actor);
  }
};
// Not of the own actor after an alias of it is assigned another.
struct COwn : CActor {
  int m_Value = 0;
  TCFuture<int> f_Run(TCActor<COwn> _Other) {
    auto Own = fg_ThisActor(this);
    auto const &Alias0 = Own, &Alias1 = Alias0, &Alias2 = Alias1,
               &Alias3 = Alias2, &Alias4 = Alias3, &Alias5 = Alias4,
               &Alias6 = Alias5, &Alias7 = Alias6, &Alias8 = Alias7;
    const_cast<TCActor<COwn> &>(Alias8) = _Other;
    Own / [this] { return m_Value; };
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: a lambda that captures 'this' of actor 'COwn' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
    co_return 0;
  }
};
}

namespace NRound177 {
// The object of another actor given by a reference to a pointer.
struct CApp : CActor {
  int m_Value;
  CApp *m_pOther;
  void f_Run();
};
void fg_Mutate(CApp *const &_pApp) { ++_pApp->m_Value; }
void CApp::f_Run() { fg_Mutate(m_pOther); }
// CHECK-MESSAGES: :[[@LINE-1]]:32: warning: 'fg_Mutate' is given the object of an actor, which it uses like 'this' on the actor the call runs on, but this may be of another actor; give it the object of the current actor, like 'this', or call it on the actor of the object [malterlib-actor-dispatch-this]
}

namespace std {
template <typename t_FFunction, typename... tp_CParams>
decltype(auto) invoke(t_FFunction &&_fFunction, tp_CParams &&..._Params) {
  return _fFunction(static_cast<tp_CParams &&>(_Params)...);
}
}

namespace NRound178 {
// An actor call made by the name of the operator, and through std::invoke.
struct CApp : CActor {
  int m_Value;
  TCFuture<int> f_Named(TCActor<> _Other) {
    co_return co_await _Other.operator()(&CActor::f_DispatchWithReturn<int>,
                                         [this] { return m_Value; });
// CHECK-MESSAGES: :[[@LINE-1]]:43: warning: a lambda that captures 'this' of actor 'CApp' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
  }
  TCFuture<int> f_Invoked(TCActor<> _Other) {
    co_return co_await std::invoke(_Other, &CActor::f_DispatchWithReturn<int>,
                                   [this] { return m_Value; });
// CHECK-MESSAGES: :[[@LINE-1]]:37: warning: a lambda that captures 'this' of actor 'CApp' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
  }
};
}

namespace NRound180 {
struct CApp : CActor {
  int m_Value;
};
// The object of an actor given by a reference to a pointer, captured by copy.
void fg_Referred(CApp *const &_pActor) {
  fg_ConcurrentDispatch([_pActor] { return _pActor->m_Value; });
// CHECK-MESSAGES: :[[@LINE-1]]:26: warning: a lambda that captures '_pActor', which points or refers to the object of actor 'CApp', runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
}
// Through a chain of conditionals, each of the same alias, told once each.
void fg_Chained(CApp *p0, bool _bChoose) {
#define DRound180Link(d_N, d_P) CApp *p##d_N = _bChoose ? p##d_P : p##d_P;
  DRound180Link(1, 0) DRound180Link(2, 1) DRound180Link(3, 2)
  DRound180Link(4, 3) DRound180Link(5, 4) DRound180Link(6, 5)
  DRound180Link(7, 6) DRound180Link(8, 7) DRound180Link(9, 8)
  DRound180Link(10, 9) DRound180Link(11, 10) DRound180Link(12, 11)
  DRound180Link(13, 12) DRound180Link(14, 13) DRound180Link(15, 14)
  DRound180Link(16, 15) DRound180Link(17, 16) DRound180Link(18, 17)
  DRound180Link(19, 18) DRound180Link(20, 19) DRound180Link(21, 20)
  DRound180Link(22, 21) DRound180Link(23, 22) DRound180Link(24, 23)
  DRound180Link(25, 24) DRound180Link(26, 25) DRound180Link(27, 26)
  DRound180Link(28, 27) DRound180Link(29, 28) DRound180Link(30, 29)
  DRound180Link(31, 30) DRound180Link(32, 31) DRound180Link(33, 32)
  DRound180Link(34, 33) DRound180Link(35, 34) DRound180Link(36, 35)
  DRound180Link(37, 36) DRound180Link(38, 37) DRound180Link(39, 38)
  DRound180Link(40, 39)
#undef DRound180Link
  fg_ConcurrentDispatch([p40] { return p40->m_Value; });
// CHECK-MESSAGES: :[[@LINE-1]]:26: warning: a lambda that captures 'p40', which points or refers to the object of actor 'CApp', runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
}
}

namespace NRound186 {
// What a postfix increment of a pointer into the object gives points into it.
struct CApp : CActor {
  int m_Values[2];
  int m_nValues = 0;
  TCFuture<int> f_Run(TCActor<> _Other) {
    int *pValue = m_Values;
    co_await (_Other / [pSaved = pValue++] {
// CHECK-MESSAGES: :[[@LINE-1]]:25: warning: a lambda that captures 'this' of actor 'CApp' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
      return *pSaved;
    });
    co_await (_Other / [nSaved = m_nValues++] {
      return nSaved;
    });
    co_return 0;
  }
};
}

namespace NRound191 {
// A functor whose calls run on another actor, made with g_ActorFunctor, also
// given a subscription.
struct CActorSubscription {};
template <typename t_FFunction>
struct TCActorFunctor {
  TCFuture<int> operator()() const;
};
template <bool t_bCoalesced>
struct TCActorFunctorHelperWithProperties {
  TCActorFunctorHelperWithProperties(TCActor<> const &_Actor);
  template <typename tf_FFunction>
  TCActorFunctor<TCFuture<int>()> operator/(tf_FFunction &&_fFunction) &&;
  TCActorFunctorHelperWithProperties &&operator()(CActorSubscription &&_Subscription) &&;
};
struct TCActorFunctorHelper {
  TCActorFunctorHelperWithProperties<false> operator()(TCActor<> const &_Actor) const;
  TCActorFunctorHelperWithProperties<false> operator()(CActorSubscription &&_Subscription) const;
};
extern TCActorFunctorHelper const g_ActorFunctor;
struct CApp : CActor {
  int m_Value = 0;
  TCFuture<int> f_ReadOn(TCActor<> _Other) {
    auto fRead = g_ActorFunctor(_Other) / [this]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:44: warning: a lambda that captures 'this' of actor 'CApp' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
      co_return m_Value;
    };
    co_return co_await fRead();
  }
  TCFuture<int> f_Subscribed(TCActor<> _Other, CActorSubscription _Subscription) {
    auto fRead = g_ActorFunctor(_Other)(static_cast<CActorSubscription &&>(_Subscription)) / [this]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:95: warning: a lambda that captures 'this' of actor 'CApp' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
      co_return m_Value;
    };
    co_return co_await fRead();
  }
  // Given only a subscription, it runs on the current actor.
  TCFuture<int> f_Current(CActorSubscription _Subscription) {
    auto fRead = g_ActorFunctor(static_cast<CActorSubscription &&>(_Subscription)) / [this]() -> TCFuture<int> {
      co_return m_Value;
    };
    co_return co_await fRead();
  }
};
}

namespace NRound197 {
// A weak functor runs its lambda on the actor it is given, like a strong one.
template <typename t_FFunction>
struct TCActorFunctorWeak {
  void operator()() const;
};
template <bool t_bCoalesced>
struct TCActorFunctorWeakHelperWithProperties {
  template <typename tf_FFunction>
  TCActorFunctorWeak<tf_FFunction> operator/(tf_FFunction &&_fFunction) &&;
};
template <bool t_bCoalesced>
struct TCActorFunctorWeakHelper {
  TCActorFunctorWeakHelperWithProperties<t_bCoalesced> operator()(TCActor<> const &_Actor) const;
};
extern TCActorFunctorWeakHelper<false> const &g_ActorFunctorWeak;
struct CApp : CActor {
  int m_Value;
  void f_Run(TCActor<> _Other) {
    auto fCallback = g_ActorFunctorWeak(_Other) / [this] { ++m_Value; };
// CHECK-MESSAGES: :[[@LINE-1]]:52: warning: a lambda that captures 'this' of actor 'CApp' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
    fCallback();
  }
};
}

namespace NRound201 {
// Into what a shared pointer member owns, through its operators, also through
// a reference to it.
struct CInternal : CActorInternal {
  int m_Value;
};
struct CApp : CActor {
  TCSharedPointer<CInternal> mp_pInternal;
  void f_Arrow(TCActor<> _Other) {
    g_Dispatch(_Other) / [pValue = &mp_pInternal->m_Value] { *pValue = 1; };
// CHECK-MESSAGES: :[[@LINE-1]]:27: warning: a lambda that captures 'this' of actor 'CApp' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
  }
  void f_Star(TCActor<> _Other) {
    auto &Internal = *mp_pInternal;
    g_Dispatch(_Other) / [pValue = &Internal.m_Value] { *pValue = 1; };
// CHECK-MESSAGES: :[[@LINE-1]]:27: warning: a lambda that captures 'this' of actor 'CApp' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
  }
};
}

namespace NRound214 {
// Also a functor made with fg_ActorFunctor, whose calls run on the actor it is
// given.
struct CActorSubscription {};
template <typename t_FFunction>
struct TCActorFunctor {
  TCFuture<int> operator()() const;
};
template <typename tf_FFunctor>
TCActorFunctor<TCFuture<int>()> fg_ActorFunctor(TCActor<> const &_Actor, tf_FFunctor &&_fFunctor, CActorSubscription &&_Subscription = {});
struct CApp : CActor {
  int m_Value = 0;
  TCFuture<int> f_Run(TCActor<> _Other) {
    auto fRead = fg_ActorFunctor(_Other, [this]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:43: warning: a lambda that captures 'this' of actor 'CApp' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
      co_return m_Value;
    });
    co_return co_await fRead();
  }
};
}

namespace NRound230
{
	// What a helper points a pointer at, through what it is given, and what a
	// lambda called in place gives back, of what it is given or captures.
	void fg_Set(int *&o_pValue, int &_Value)
	{
		o_pValue = &_Value;
	}

	struct CApp : CActor
	{
		int m_Value;

		void f_Helper(TCActor<> _Other)
		{
			int *pValue = nullptr;
			fg_Set(pValue, m_Value);
			_Other / [pValue]
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a lambda that captures 'this' of actor 'CApp' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
				{
					return *pValue;
				}
			;
		}

		void f_Lambda(TCActor<> _Other)
		{
			auto *pValue = [](int &_Value)
				{
					return &_Value;
				}
				(m_Value)
			;
			_Other / [pValue]
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a lambda that captures 'this' of actor 'CApp' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
				{
					return *pValue;
				}
			;
		}

		void f_Captured(TCActor<> _Other)
		{
			auto *pValue = [this]
				{
					return &m_Value;
				}
				()
			;
			_Other / [pValue]
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a lambda that captures 'this' of actor 'CApp' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
				{
					return *pValue;
				}
			;
		}
	};
}

namespace NRound233
{
	// Deeply nested awaits, of calls that can't be given the object of an
	// actor, are told in a time that grows with how deep they are, not faster.
	TCFuture<int> fg_Work(int _Value);

#define DRound233Await1(d_Value) co_await fg_Work(d_Value)
#define DRound233Await2(d_Value) DRound233Await1(DRound233Await1(d_Value))
#define DRound233Await4(d_Value) DRound233Await2(DRound233Await2(d_Value))
#define DRound233Await8(d_Value) DRound233Await4(DRound233Await4(d_Value))
#define DRound233Await16(d_Value) DRound233Await8(DRound233Await8(d_Value))
#define DRound233Await32(d_Value) DRound233Await16(DRound233Await16(d_Value))
#define DRound233Await64(d_Value) DRound233Await32(DRound233Await32(d_Value))
#define DRound233Await128(d_Value) DRound233Await64(DRound233Await64(d_Value))

	struct CApp : CActor
	{
		TCFuture<int> f_Work()
		{
			co_return DRound233Await128(DRound233Await64(DRound233Await32(DRound233Await16(0))));
		}
	};
}

namespace NRound234 {
struct CApp : CActor {
  int m_Value;
};
// A virtual hook whose base does nothing, which an override can make use it.
struct CInterface {
  virtual void f_Use(CApp *_pApp) {}
};
struct CImplementation : CInterface {
  void f_Use(CApp *_pApp) override { ++_pApp->m_Value; }
};
struct CCaller : CActor {
  void f_Run(CInterface &_Interface, CApp *_pOther) {
    _Interface.f_Use(_pOther);
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: 'f_Use' is given the object of an actor, which it uses like 'this' on the actor the call runs on, but this may be of another actor; give it the object of the current actor, like 'this', or call it on the actor of the object [malterlib-actor-dispatch-this]
  }
  // Not one that can't be overridden.
  void f_Final(CImplementation &_Implementation, CApp *_pOther) {
    _Implementation.CInterface::f_Use(_pOther);
  }
};
}

namespace NMib::NConcurrency {
template <typename t_FSignature>
struct TCActorFunctor {
  TCActorFunctor(TCActor<> &&_Actor, TCFunction<t_FSignature> &&_fFunctor,
                 CActorSubscription &&_Subscription = CActorSubscription{});
  TCFuture<int> operator()() const;
};
}
namespace NRound237 {
// A functor made directly runs on the actor it is given.
struct CApp : CActor {
  int m_Value;
  TCFuture<int> f_Read(TCActor<> _Other) {
    TCActorFunctor<TCFuture<int>()> fRead(
        fg_Move(_Other), [this]() -> TCFuture<int> { co_return m_Value; });
// CHECK-MESSAGES: :[[@LINE-1]]:27: warning: a lambda that captures 'this' of actor 'CApp' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
    co_return co_await fRead();
  }
  TCFuture<int> f_Own() {
    TCActorFunctor<TCFuture<int>()> fRead(
        fg_ThisActor(this), [this]() -> TCFuture<int> { co_return m_Value; });
    co_return co_await fRead();
  }
};
}

namespace NRound239 {
// Also a weak functor made with fg_ActorFunctorWeak.
template <typename t_FFunction>
struct TCWeakFunctor {
  TCFuture<int> operator()() const;
};
template <typename tf_FFunctor>
TCWeakFunctor<TCFuture<int>()> fg_ActorFunctorWeak(TCActor<> const &_Actor,
                                                   tf_FFunctor &&_fFunctor);
struct CApp : CActor {
  int m_Value = 0;
  void f_Run(TCActor<> _Other) {
    auto fRead = fg_ActorFunctorWeak(_Other, [this]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:47: warning: a lambda that captures 'this' of actor 'CApp' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
      co_return m_Value;
    });
    fRead();
  }
};
}

// A handle initialized with itself names no known actor, and the analysis
// ends.
struct CRound243SelfInit : CActor
{
	int m_Value = 0;

	void f_Run()
	{
		TCActor<> Actor = Actor;
		fg_RegisterTimer
			(
				1.0
				, [this]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: a lambda that captures 'this' of actor 'CRound243SelfInit' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
				{
					co_return m_Value;
				}
				, Actor
			)
		;
	}
};

// Each value a called lambda returns is followed once, also of helpers that
// each call the one before from two branches, which would otherwise be
// followed once for each path, a number that doubles with each helper.
namespace NRound370 {
int g_Value;
#define DLink(_Next, _Previous) auto f##_Next = [&] { return _bChoose ? f##_Previous() : f##_Previous(); };
#define DChain DLink(1, 0) DLink(2, 1) DLink(3, 2) DLink(4, 3) DLink(5, 4) DLink(6, 5) DLink(7, 6) DLink(8, 7) DLink(9, 8) DLink(10, 9) DLink(11, 10) DLink(12, 11) DLink(13, 12) DLink(14, 13) DLink(15, 14) DLink(16, 15) DLink(17, 16) DLink(18, 17) DLink(19, 18) DLink(20, 19) DLink(21, 20) DLink(22, 21) DLink(23, 22) DLink(24, 23)
struct CApp : CActor {
  int m_Value = 0;
  void f_Global(TCActor<> _Other, bool _bChoose) {
    auto f0 = [] { return &g_Value; };
    DChain
    auto *pValue = f24();
    _Other / [pValue] { ++*pValue; };
  }
  void f_Member(TCActor<> _Other, bool _bChoose) {
    auto f0 = [&] { return &m_Value; };
    DChain
    auto *pValue = f24();
    _Other / [pValue] { ++*pValue; };
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: a lambda that captures 'this' of actor 'CApp' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
  }
};
#undef DChain
#undef DLink
}
