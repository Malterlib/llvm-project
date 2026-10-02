// RUN: %check_clang_tidy -std=c++20-or-later %s malterlib-coroutine-unsafe-this %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -I %S/Inputs

#include "malterlib-concurrency.h"

TCFuture<int> fg_Suspend();

struct CMyActor : CActor
{
	TCFuture<int> f_Function()
	{
		co_return m_Value;
	}

	TCFuture<int> f_Lambda()
	{
		auto fLambda = [this]() -> TCFuture<int>
			{
				co_return m_Value;
			}
		;
		co_return 0;
	}

	int m_Value = 0;
};

struct CAllowed : CAllowUnsafeThis
{
	TCFuture<int> f_Function()
	{
		co_return m_Value;
	}

	int m_Value = 0;
};

struct CUnevaluated
{
	TCFuture<int> f_Size()
	{
		using CThis = decltype(this);
		co_return sizeof(m_Value) + alignof(decltype(m_Value)) + noexcept(m_Value);
	}

	int m_Value = 0;
};

struct CMovesOther
{
	TCFuture<int> f_MovesLocal()
	{
		CMovesOther Local;
		co_await fg_Suspend();
		auto Moved = co_await fg_MoveThis(Local);
		co_return 0;
	}
};

struct CHelper
{
	// A member function runs in its call until it first suspends, while the
	// caller has the object.
	TCFuture<int> f_UsesThisBefore()
	{
		int Value = m_Value;
		co_await fg_Suspend();
		co_return Value;
	}

	TCFuture<int> f_UsesThis()
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: TCFuture coroutine 'f_UsesThis' is a member of 'CHelper', which is not an actor, and uses its object after a suspension, when it may be gone; derive 'CHelper' from CActor or CAllowUnsafeThis, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
	{
		co_await fg_Suspend();
		co_return m_Value;
	}

	TCFuture<int> f_UsesAlias()
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: TCFuture coroutine 'f_UsesAlias' is a member of 'CHelper', which is not an actor, and uses its object after a suspension, when it may be gone; derive 'CHelper' from CActor or CAllowUnsafeThis, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
	{
		auto *pThis = this;
		int &Value = m_Value;
		co_await fg_Suspend();
		co_return pThis->m_Value + Value;
	}

	TCFuture<int> f_NoThis()
	{
		co_return 0;
	}

	TCUnsafeFuture<int> f_Unsafe()
	{
		co_await fg_Suspend();
		co_return m_Value;
	}

	TCFuture<int> f_Moved()
	{
		auto This = co_await fg_MoveThis(*this);
		co_await fg_Suspend();
		co_return This.m_Value;
	}

	TCFuture<int> f_MovedLate()
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: TCFuture coroutine 'f_MovedLate' is a member of 'CHelper', which is not an actor, and uses its object after a suspension, when it may be gone; derive 'CHelper' from CActor or CAllowUnsafeThis, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
	{
		co_await fg_Suspend();
		auto This = co_await fg_MoveThis(*this);
		co_return This.m_Value;
	}

	TCFuture<int> f_MovedInLoop()
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: TCFuture coroutine 'f_MovedInLoop' is a member of 'CHelper', which is not an actor, and uses its object after a suspension, when it may be gone; derive 'CHelper' from CActor or CAllowUnsafeThis, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
	{
		for (int iTry = 0; iTry < 2; ++iTry)
		{
			auto This = co_await fg_MoveThis(*this);
			co_await fg_Suspend();
		}
		co_return 0;
	}

	TCFuture<int> f_MovedAfterUnreachable()
	{
		if (false)
			co_await fg_Suspend();
		auto This = co_await fg_MoveThis(*this);
		co_await fg_Suspend();
		co_return This.m_Value;
	}

	TCFuture<int> f_NotAwaitedAfterUse()
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: TCFuture coroutine 'f_NotAwaitedAfterUse' is a member of 'CHelper', which is not an actor, and uses its object after a suspension, when it may be gone; derive 'CHelper' from CActor or CAllowUnsafeThis, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
	{
		int Copy = m_Value;
		auto Awaiter = fg_MoveThis(*this);
		co_await fg_Suspend();
		auto This = co_await fg_Move(Awaiter);
		co_return This.m_Value + Copy;
	}

	TCFuture<int> f_NotAwaited()
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: TCFuture coroutine 'f_NotAwaited' is a member of 'CHelper', which is not an actor, and uses its object after a suspension, when it may be gone; derive 'CHelper' from CActor or CAllowUnsafeThis, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
	{
		auto Awaiter = fg_MoveThis(*this);
		co_await fg_Suspend();
		auto This = co_await fg_Move(Awaiter);
		co_return This.m_Value;
	}

	TCFuture<int> f_LocalClass()
	{
		struct CLocal
		{
			int f_Get() { return m_Value; }
			int m_Value = 0;
		};
		co_await fg_Suspend();
		CLocal Local;
		co_return Local.f_Get();
	}

	static TCFuture<int> fs_Static()
	{
		co_await fg_Suspend();
		co_return 0;
	}

	// Making the lambdas after a suspension uses the object too.
	TCFuture<int> f_Lambda()
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: TCFuture coroutine 'f_Lambda' is a member of 'CHelper', which is not an actor, and uses its object after a suspension, when it may be gone; derive 'CHelper' from CActor or CAllowUnsafeThis, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
	{
		// A lambda called directly runs while the caller has the object, until
		// it first suspends.
		co_await [this]() -> TCFuture<int>
			{
				int Value = m_Value;
				co_await fg_Suspend();
				co_return Value;
			}
			()
		;
		co_await [this]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: a TCFuture coroutine lambda captures 'this' of 'CHelper', which is not an actor, so the object may be gone when it runs or resumes; derive 'CHelper' from CActor or CAllowUnsafeThis, keep it alive with a shared pointer in the capture, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
			{
				co_await fg_Suspend();
				co_return m_Value;
			}
			()
		;
		// A dispatched lambda can run after the object is gone.
		co_await
			(
				g_Dispatch / [this]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: a TCFuture coroutine lambda captures 'this' of 'CHelper', which is not an actor, so the object may be gone when it runs or resumes; derive 'CHelper' from CActor or CAllowUnsafeThis, keep it alive with a shared pointer in the capture, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
				{
					co_return m_Value;
				}
			)
		;
		co_await
			(
				g_Dispatch / [pThis = this]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: a TCFuture coroutine lambda captures 'pThis', which points to or into the object of 'CHelper', which is not an actor, so the object may be gone when it runs or resumes; derive 'CHelper' from CActor or CAllowUnsafeThis, keep it alive with a shared pointer in the capture, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
				{
					co_return pThis->m_Value;
				}
			)
		;
		auto *pAlias = this;
		co_await
			(
				g_Dispatch / [pAlias]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: a TCFuture coroutine lambda captures 'pAlias', which points to or into the object of 'CHelper', which is not an actor, so the object may be gone when it runs or resumes; derive 'CHelper' from CActor or CAllowUnsafeThis, keep it alive with a shared pointer in the capture, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
				{
					co_return pAlias->m_Value;
				}
			)
		;
		auto *pChanged = this;
		pChanged = nullptr;
		co_await
			(
				g_Dispatch / [pChanged]() -> TCFuture<int>
				{
					co_return 0;
				}
			)
		;
		co_await
			(
				g_Dispatch / [*this]() -> TCFuture<int>
				{
					co_return m_Value;
				}
			)
		;
		// A shared pointer to the object keeps it alive while the closure is.
		co_await
			(
				g_Dispatch / [this, pThis = TCSharedPointer<CHelper>{fg_Explicit(this)}]() -> TCFuture<int>
				{
					co_await fg_Suspend();
					co_return m_Value;
				}
			)
		;
		// Also a copy of an unchanged local shared pointer.
		TCSharedPointer<CHelper> pThis{fg_Explicit(this)};
		co_await
			(
				g_Dispatch / [this, pThis]() -> TCFuture<int>
				{
					co_await fg_Suspend();
					co_return m_Value;
				}
			)
		;
		// Not one emptied by a named assignment.
		TCSharedPointer<CHelper> pEmptied{fg_Explicit(this)};
		pEmptied.operator=(TCSharedPointer<CHelper>{});
		co_await
			(
				g_Dispatch / [this, pEmptied]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: a TCFuture coroutine lambda captures 'this' of 'CHelper', which is not an actor, so the object may be gone when it runs or resumes; derive 'CHelper' from CActor or CAllowUnsafeThis, keep it alive with a shared pointer in the capture, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
				{
					co_await fg_Suspend();
					co_return m_Value;
				}
			)
		;
		// An alias of the object that a mutable lambda changes its copy of
		// doesn't own the object there.
		auto *pSelf = this;
		co_await fg_CallSafe
			(
				[this, pSelf]() mutable -> TCFuture<int>
				{
					pSelf = m_pOther;
					return g_Dispatch / [this, pKeep = TCSharedPointer<CHelper>{pSelf}]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:27: warning: a TCFuture coroutine lambda captures 'this' of 'CHelper', which is not an actor, so the object may be gone when it runs or resumes; derive 'CHelper' from CActor or CAllowUnsafeThis, keep it alive with a shared pointer in the capture, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
						{
							co_await fg_Suspend();
							co_return m_Value;
						}
					;
				}
			)
		;
		// It must own this object, and keep it.
		co_await
			(
				g_Dispatch / [this, pOther = TCSharedPointer<CHelper>{m_pOther}]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: a TCFuture coroutine lambda captures 'this' of 'CHelper', which is not an actor, so the object may be gone when it runs or resumes; derive 'CHelper' from CActor or CAllowUnsafeThis, keep it alive with a shared pointer in the capture, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
				{
					co_await fg_Suspend();
					co_return m_Value;
				}
			)
		;
		co_await
			(
				g_Dispatch / [this, pThis = TCSharedPointer<CHelper>{this}]() mutable -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: a TCFuture coroutine lambda captures 'this' of 'CHelper', which is not an actor, so the object may be gone when it runs or resumes; derive 'CHelper' from CActor or CAllowUnsafeThis, keep it alive with a shared pointer in the capture, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
				{
					pThis = {};
					co_await fg_Suspend();
					co_return m_Value;
				}
			)
		;
		co_await
			(
				g_Dispatch / [this, pThis = TCSharedPointer<CHelper>{this}]() mutable -> TCFuture<int>
				{
					co_await fg_Suspend();
					co_return m_Value + pThis->m_Value;
				}
			)
		;
		co_await
			(
				g_Dispatch / [this, pThis = TCWeakPointer<CHelper>{fg_Explicit(this)}]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: a TCFuture coroutine lambda captures 'this' of 'CHelper', which is not an actor, so the object may be gone when it runs or resumes; derive 'CHelper' from CActor or CAllowUnsafeThis, keep it alive with a shared pointer in the capture, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
				{
					co_await fg_Suspend();
					co_return m_Value;
				}
			)
		;
		co_return 0;
	}

	// Waiting for the dispatched lambda, `f_CallSync()`, doesn't keep the
	// object, since what is between can leave first.
	int f_Joined(TCActor<> const &_Actor)
	{
		return fg_Dispatch
			(
				_Actor
				, [this]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: a TCFuture coroutine lambda captures 'this' of 'CHelper', which is not an actor, so the object may be gone when it runs or resumes; derive 'CHelper' from CActor or CAllowUnsafeThis, keep it alive with a shared pointer in the capture, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
				{
					co_await fg_Suspend();
					co_return m_Value;
				}
			)
			.f_CallSync()
		;
	}

	// A lambda around it that can run later doesn't keep the object, even if
	// it waits for the coroutine.
	TCFunction<int ()> f_Escaping()
	{
		return [this]() -> int
			{
				return fg_Dispatch
					(
						[this]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: a TCFuture coroutine lambda captures 'this' of 'CHelper', which is not an actor, so the object may be gone when it runs or resumes; derive 'CHelper' from CActor or CAllowUnsafeThis, keep it alive with a shared pointer in the capture, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
						{
							co_await fg_Suspend();
							co_return m_Value;
						}
					)
					.f_CallSync()
				;
			}
		;
	}

	int m_Value = 0;
	CHelper *m_pOther = nullptr;
};

// In a template the type of the capture is deduced only later. The owner
// keeps the object only while something keeps the closure, which returning it
// doesn't show.
template <typename t_CType>
struct TCState
{
	TCFunction<TCFuture<int> ()> f_Make()
	{
		return [this, pThis = TCSharedPointer<TCState>{fg_Explicit(this)}]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: a TCFuture coroutine lambda captures 'this' of 'TCState', which is not an actor, so the object may be gone when it runs or resumes; derive 'TCState' from CActor or CAllowUnsafeThis, keep it alive with a shared pointer in the capture, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
			{
				co_await fg_Suspend();
				co_return m_Value;
			}
		;
	}

	t_CType m_Value = {};
};
template struct TCState<int>;

// A shared pointer in a temporary closure that is called directly goes when
// the call ends.
struct CKeptTemporary
{
	TCFuture<int> f_Run()
	{
		return [this, pThis = TCSharedPointer<CKeptTemporary>{this}]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: a TCFuture coroutine lambda captures 'this' of 'CKeptTemporary', which is not an actor, so the object may be gone when it runs or resumes; derive 'CKeptTemporary' from CActor or CAllowUnsafeThis, keep it alive with a shared pointer in the capture, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
			{
				auto *pObject = this;
				co_await fg_Suspend();
				co_return pObject->m_Value;
			}
			()
		;
	}

	int m_Value = 0;
};

// An owner that can be released through a conditional doesn't keep the object.
struct CReleased
{
	// Capturing the object in a lambda keeps a reference to it.
	TCFuture<int> f_Run(bool _bOther)
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: TCFuture coroutine 'f_Run' is a member of 'CReleased', which is not an actor, and uses its object after a suspension, when it may be gone; derive 'CReleased' from CActor or CAllowUnsafeThis, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
	{
		co_return co_await
			(
				g_Dispatch / [this, pThis = TCSharedPointer<CReleased>{this}, pOther = TCSharedPointer<CReleased>{}, _bOther]() mutable -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: a TCFuture coroutine lambda captures 'this' of 'CReleased', which is not an actor, so the object may be gone when it runs or resumes; derive 'CReleased' from CActor or CAllowUnsafeThis, keep it alive with a shared pointer in the capture, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
				{
					(_bOther ? pOther : pThis) = {};
					co_await fg_Suspend();
					co_return m_Value;
				}
			)
		;
	}

	int m_Value = 0;
};

template <typename t_CType>
struct TCDispatchedState
{
	TCFuture<int> f_Run()
	{
		return g_Dispatch / [this, pThis = TCSharedPointer<TCDispatchedState>{fg_Explicit(this)}]() -> TCFuture<int>
			{
				co_await fg_Suspend();
				co_return m_Value;
			}
		;
	}

	t_CType m_Value = {};
};

// A lambda called directly in a default member initializer runs while the
// object is made.
struct CMemberInitializer
{
	int m_Value = 0;
	TCFuture<int> m_Future = [this]() -> TCFuture<int>
		{
			int Copy = m_Value;
			co_await fg_Suspend();
			co_return Copy;
		}
		()
	;
};

// An alias of `this` cast to a derived class.
struct CDerivedHelper;

struct CBaseHelper
{
	TCFuture<int> f_Run();
	int m_Value = 0;
};

struct CDerivedHelper : CBaseHelper
{
};

TCFuture<int> CBaseHelper::f_Run()
{
	return [pThis = static_cast<CDerivedHelper *>(this)]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: a TCFuture coroutine lambda captures 'pThis', which points to or into the object of 'CBaseHelper', which is not an actor, so the object may be gone when it runs or resumes; derive 'CBaseHelper' from CActor or CAllowUnsafeThis, keep it alive with a shared pointer in the capture, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
		{
			auto *pCopy = pThis;
			co_await fg_Suspend();
			co_return pCopy->m_Value;
		}
		()
	;
}

struct CRound35Base
{
	int m_Value = 0;
};

// A base that depends on a template parameter is known in the
// instantiations.
template <typename t_CBase>
struct TCRound35Helper : t_CBase
{
	TCFuture<int> f_Run()
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: TCFuture coroutine 'f_Run' is a member of 'TCRound35Helper', which is not an actor, and uses its object after a suspension, when it may be gone; derive 'TCRound35Helper' from CActor or CAllowUnsafeThis, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
	{
		co_await fg_Suspend();
		co_return this->m_Value;
	}
};

TCFuture<int> fg_StartRound35()
{
	TCRound35Helper<CRound35Base> Local;
	return Local.f_Run();
}

// Flags that depend on a template parameter are known in the instantiations.
struct CTemplateFlags
{
	int m_Value;
	template <ECoroutineFlag t_Flags>
	TCFutureWithFlags<int, t_Flags> f_Run()
// CHECK-MESSAGES: :[[@LINE-1]]:34: warning: TCFuture coroutine 'f_Run' is a member of 'CTemplateFlags', which is not an actor, and uses its object after a suspension, when it may be gone; derive 'CTemplateFlags' from CActor or CAllowUnsafeThis, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
	{
		co_await CThrowingAwaiter{};
		co_return m_Value;
	}
};
auto fg_StartTemplateFlags(CTemplateFlags &_Object)
{
	return _Object.f_Run<ECoroutineFlag_None>();
}

// The object of a call through a pointer to a member function whose arguments
// suspend is used after the suspension.
struct CRound67Object
{
	int m_Value = 0;
	int f_Read(int _Offset) const
	{
		return m_Value + _Offset;
	}

	TCFuture<int> f_Run()
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: TCFuture coroutine 'f_Run' is a member of 'CRound67Object', which is not an actor, and uses its object after a suspension, when it may be gone; derive 'CRound67Object' from CActor or CAllowUnsafeThis, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
	{
		auto fRead = &CRound67Object::f_Read;
		co_return (this->*fRead)(co_await fg_Suspend());
	}
};

// What initializes the captures of a lambda given to a member function can
// suspend before the function runs.
struct CRound68Object
{
	int m_Value = 0;
	template <typename t_FCallback>
	TCFuture<int> f_Read(t_FCallback _fCallback)
	{
		co_return m_Value;
	}

	TCFuture<int> f_Run()
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: TCFuture coroutine 'f_Run' is a member of 'CRound68Object', which is not an actor, and uses its object after a suspension, when it may be gone; derive 'CRound68Object' from CActor or CAllowUnsafeThis, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
	{
		co_return co_await f_Read
			(
				[Value = co_await fg_Suspend()]
				{
					return Value;
				}
			)
		;
	}
};

// A move of the object that never runs doesn't use it.
struct CRound89Discarded
{
	TCFuture<int> f_Run()
	{
		if constexpr (false)
			fg_MoveThis(*this);
		if (false)
			fg_MoveThis(*this);
		co_return 0;
	}
};

// A generic lambda whose result depends on what it is called with, and a
// lambda in a template whose base may be an actor, are checked where they
// are instantiated.
TCFuture<int> fg_Round90Work();
template <typename t_FCallable>
TCFuture<int> fg_Round90CallWithWork(t_FCallable &&_fCallable)
{
	return _fCallable(fg_Round90Work());
}
struct CRound90Generic
{
	int m_Value;
	TCFuture<int> f_Start()
	{
		return fg_Round90CallWithWork
			(
				[this](auto _Future) -> decltype(_Future)
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: a TCFuture coroutine lambda captures 'this' of 'CRound90Generic', which is not an actor, so the object may be gone when it runs or resumes; derive 'CRound90Generic' from CActor or CAllowUnsafeThis, keep it alive with a shared pointer in the capture, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
				{
					co_await fg_Move(_Future);
					co_return m_Value;
				}
			)
		;
	}
};
struct CRound90Empty
{
};
template <typename t_CBase>
struct TCRound90State : t_CBase
{
	int m_Value;
	TCFuture<int> f_Start()
	{
		return fg_CallSafe
			(
				[this]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: a TCFuture coroutine lambda captures 'this' of 'TCRound90State', which is not an actor, so the object may be gone when it runs or resumes; derive 'TCRound90State' from CActor or CAllowUnsafeThis, keep it alive with a shared pointer in the capture, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
				{
					co_await fg_Round90Work();
					co_return m_Value;
				}
			)
		;
	}
};
TCFuture<int> fg_Round90Use(TCRound90State<CRound90Empty> &_State)
{
	return _State.f_Start();
}

// Through a long chain of unchanged aliases.
struct CRound90Aliases
{
	int m_Value;
	TCFuture<int> f_Start()
	{
		auto *pA = this;
		auto *pB = pA;
		auto *pC = pB;
		auto *pD = pC;
		auto *pE = pD;
		auto *pF = pE;
		return fg_CallSafe
			(
				[pF]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: a TCFuture coroutine lambda captures 'pF', which points to or into the object of 'CRound90Aliases', which is not an actor, so the object may be gone when it runs or resumes; derive 'CRound90Aliases' from CActor or CAllowUnsafeThis, keep it alive with a shared pointer in the capture, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
				{
					co_await fg_Round90Work();
					co_return pF->m_Value;
				}
			)
		;
	}
};

// The const of a capture that keeps the object cast away.
struct CRound101State
{
	int m_Value = 0;

	TCFuture<int> f_Run()
	{
		return fg_CallSafe
			(
				[this, pKeep = TCSharedPointer<CRound101State>{fg_Explicit(this)}]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: a TCFuture coroutine lambda captures 'this' of 'CRound101State', which is not an actor, so the object may be gone when it runs or resumes; derive 'CRound101State' from CActor or CAllowUnsafeThis, keep it alive with a shared pointer in the capture, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
				{
					const_cast<TCSharedPointer<CRound101State> &>(pKeep) = TCSharedPointer<CRound101State>{};
					co_await fg_Suspend();
					co_return m_Value;
				}
			)
		;
	}
};

// fg_MoveThis gives a reference to the object, which only a variable made of
// it moves into the coroutine.
struct CRound110MoveThis
{
	int m_Value;

	TCFuture<int> f_Bound()
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: TCFuture coroutine 'f_Bound' is a member of 'CRound110MoveThis', which is not an actor, and uses its object after a suspension, when it may be gone; derive 'CRound110MoveThis' from CActor or CAllowUnsafeThis, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
	{
		auto &&Self = co_await fg_MoveThis(*this);
		co_await fg_Suspend();
		co_return Self.m_Value;
	}

	TCFuture<int> f_Moved()
	{
		auto Self = co_await fg_MoveThis(*this);
		co_await fg_Suspend();
		co_return Self.m_Value;
	}
};

// Round 113: a reference to the owner can reset it
namespace NRound113Alias
{
TCFuture<int> work();
struct State {
  int value;
  TCFuture<int> f() {
    return fg_CallSafe([this, keep = TCSharedPointer<State>{fg_Explicit(this)}]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:25: warning: a TCFuture coroutine lambda captures 'this' of 'State', which is not an actor, so the object may be gone when it runs or resumes; derive 'State' from CActor or CAllowUnsafeThis, keep it alive with a shared pointer in the capture, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
      auto const &alias = keep;
      const_cast<TCSharedPointer<State>&>(alias) = {};
      co_await work();
      co_return value;
    });
  }
};
}

// Round 135: a pointer to the owning capture, also a reference bound through
// a conditional, can reset it.
namespace NRound135
{
TCFuture<int> fg_Suspend();

struct CHelper {
  int m_Value;
  TCFuture<int> f_Pointer() {
    return fg_CallSafe(
        [this, pThis = TCSharedPointer<CHelper>{fg_Explicit(this)}]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: a TCFuture coroutine lambda captures 'this' of 'CHelper', which is not an actor, so the object may be gone when it runs or resumes; derive 'CHelper' from CActor or CAllowUnsafeThis, keep it alive with a shared pointer in the capture, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
          auto const *pAlias = &pThis;
          *const_cast<TCSharedPointer<CHelper> *>(pAlias) = TCSharedPointer<CHelper>{};
          co_await fg_Suspend();
          co_return m_Value;
        });
  }
};
}

namespace NRound138 {
TCFuture<int> fg_Work();

// A reset of the shared pointer that keeps the object, also through more
// references to it than are looked through, `pP = {}`.
struct CState {
  int m_Value;
  void f_Start() {
    (void)fg_CallSafe([this, pThis = TCSharedPointer<CState>{fg_Explicit(this)}]() mutable -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda captures 'this' of 'CState', which is not an actor, so the object may be gone when it runs or resumes; derive 'CState' from CActor or CAllowUnsafeThis, keep it alive with a shared pointer in the capture, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
      auto &pA = pThis, &pB = pA, &pC = pB, &pD = pC, &pE = pD, &pF = pE, &pG = pF, &pH = pG;
      auto &pI = pH, &pJ = pI, &pK = pJ, &pL = pK, &pM = pL, &pN = pM, &pO = pN, &pP = pO;
      pP = TCSharedPointer<CState>{};
      co_await fg_Work();
      co_return m_Value;
    });
  }

  // But not when none of them resets it.
  void f_Kept() {
    (void)fg_CallSafe([this, pThis = TCSharedPointer<CState>{fg_Explicit(this)}]() mutable -> TCFuture<int> {
      auto &pA = pThis, &pB = pA, &pC = pB, &pD = pC, &pE = pD, &pF = pE, &pG = pF, &pH = pG;
      auto &pI = pH, &pJ = pI, &pK = pJ, &pL = pK, &pM = pL, &pN = pM, &pO = pN, &pP = pO;
      co_await fg_Work();
      co_return m_Value + pP->m_Value;
    });
  }
};

// A static member named through the object, `this->ms_Value`, doesn't use it.
struct CStatic {
  static int ms_Value;
  static int fs_Get();
  int m_Value;

  TCFuture<int> f_Static() {
    co_await fg_Work();
    co_return this->ms_Value + this->fs_Get();
  }

  TCFuture<int> f_Member() {
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: TCFuture coroutine 'f_Member' is a member of 'CStatic', which is not an actor, and uses its object after a suspension, when it may be gone; derive 'CStatic' from CActor or CAllowUnsafeThis, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
    co_await fg_Work();
    co_return this->m_Value;
  }
};
}

namespace NRound141 {
// A lambda called where it is made, which isn't a coroutine, runs there.
struct CState {
  int m_Value;
  TCFuture<int> f_Immediate() {
    co_return [this] { return m_Value; }();
  }
};
}

namespace NRound147 {
CAwaiter fg_Suspend();

// A static member through a conditional that chooses `this`.
struct CStatic
{
	static constexpr int mc_Value = 7;

	TCFuture<int> f_Static()
	{
		co_await fg_Suspend();
		co_return (true ? this : nullptr)->mc_Value;
	}
};
}

namespace NRound154 {
// Its object returned through a lambda held in a local that is only called.
struct CHelper {
  int m_Value;
  TCFuture<int const *> f_Named() {
// CHECK-MESSAGES: :[[@LINE-1]]:25: warning: TCFuture coroutine 'f_Named' is a member of 'CHelper', which is not an actor, and uses its object after a suspension, when it may be gone; derive 'CHelper' from CActor or CAllowUnsafeThis, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
    auto fGet = [this] { return &m_Value; };
    co_return fGet();
  }
};
}

namespace NRound157 {
TCFuture<int> fg_Suspend();
// A pointer into the object captured.
struct CState {
  int m_Value = 0;
  TCFuture<int> f_Start() {
    return fg_CallSafe([pValue = &m_Value]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:25: warning: a TCFuture coroutine lambda captures 'pValue', which points to or into the object of 'CState', which is not an actor, so the object may be gone when it runs or resumes; derive 'CState' from CActor or CAllowUnsafeThis, keep it alive with a shared pointer in the capture, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
      co_await fg_Suspend();
      co_return *pValue;
    });
  }
};
}

namespace NRound163 {
// Into the object through a pointer to a member.
struct CState {
  int m_Value = 1;
  TCFuture<int> f_Run() {
    auto pMember = &CState::m_Value;
    return fg_CallSafe([pValue = &(this->*pMember)]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:25: warning: a TCFuture coroutine lambda captures 'pValue', which points to or into the object of 'CState', which is not an actor, so the object may be gone when it runs or resumes; derive 'CState' from CActor or CAllowUnsafeThis, keep it alive with a shared pointer in the capture, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
      co_await CAwaiter{};
      co_return *pValue;
    });
  }
};
}

namespace NRound165 {
TCFuture<int> fg_Work();
// A capture of either value of a conditional, or of the last of a comma.
struct CState {
  int m_Value;
  void f_Conditional(bool _bUse, TCFuture<int> &o_Result) {
    o_Result = fg_CallSafe([pState = _bUse ? this : nullptr]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:29: warning: a TCFuture coroutine lambda captures 'pState', which points to or into the object of 'CState', which is not an actor, so the object may be gone when it runs or resumes; derive 'CState' from CActor or CAllowUnsafeThis, keep it alive with a shared pointer in the capture, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
      co_await fg_Work();
      co_return pState ? pState->m_Value : 0;
    });
  }
  void f_Comma(TCFuture<int> &o_Result) {
    o_Result = fg_CallSafe(
        [pState = (static_cast<void>(0), this)]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: a TCFuture coroutine lambda captures 'pState', which points to or into the object of 'CState', which is not an actor, so the object may be gone when it runs or resumes; derive 'CState' from CActor or CAllowUnsafeThis, keep it alive with a shared pointer in the capture, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
          co_await fg_Work();
          co_return pState->m_Value;
        });
  }
};
}

namespace NRound169 {
TCFuture<int> fg_Work();
struct CState;
// The pointer that keeps it cleared by a function that casts the const away.
void fg_Release(TCSharedPointer<CState> const &_pState) {
  const_cast<TCSharedPointer<CState> &>(_pState) = {};
}
struct CState {
  int m_Value;
  TCFuture<int> f_Run() {
    return fg_CallSafe(
        [this, pKeep = TCSharedPointer<CState>{fg_Explicit(this)}]()
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: a TCFuture coroutine lambda captures 'this' of 'CState', which is not an actor, so the object may be gone when it runs or resumes; derive 'CState' from CActor or CAllowUnsafeThis, keep it alive with a shared pointer in the capture, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
            -> TCFuture<int> {
          fg_Release(pKeep);
          co_await fg_Work();
          co_return m_Value;
        });
  }
};
}

namespace NRound170 {
CAwaiter fg_Suspend();
// What the result keeps of the object outlives the pointer that keeps it.
struct CState {
  int m_Value;
  auto f_Address() {
    return fg_CallSafe(
        [this, pKeep = TCSharedPointer<CState>{fg_Explicit(this)}]()
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: a TCFuture coroutine lambda captures 'this' of 'CState', which is not an actor, so the object may be gone when it runs or resumes; derive 'CState' from CActor or CAllowUnsafeThis, keep it alive with a shared pointer in the capture, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
            -> TCFuture<int *> {
          co_await fg_Suspend();
          co_return &m_Value;
        });
  }
  auto f_Value() {
    return fg_CallSafe(
        [this, pKeep = TCSharedPointer<CState>{fg_Explicit(this)}]()
            -> TCFuture<int> {
          co_await fg_Suspend();
          co_return m_Value;
        });
  }
};
// Released by functions that cast the const away through a reference to
// it, or deeper, but not by one that only discards it, nor many times over
// in time that grows with each.
struct CHelper;
using FHelper = TCSharedPointer<CHelper>;
void fg_ReleaseAlias(FHelper const &_pHelper) {
  auto const &pAlias = _pHelper;
  const_cast<FHelper &>(pAlias) = {};
}
void fg_Release0(FHelper const &_pHelper) {
  const_cast<FHelper &>(_pHelper) = {};
}
#define DLink(d_Next, d_Previous)                                              \
  void fg_Release##d_Next(FHelper const &_pHelper) {                          \
    fg_Release##d_Previous(_pHelper);                                          \
  }
DLink(1, 0) DLink(2, 1) DLink(3, 2) DLink(4, 3) DLink(5, 4) DLink(6, 5)
DLink(7, 6) DLink(8, 7) DLink(9, 8)
#undef DLink
void fg_Inspect(FHelper const &_pHelper) { (void)_pHelper; }
void fg_Wide0(FHelper const &) {}
#define DLink(d_Next, d_Previous)                                              \
  void fg_Wide##d_Next(FHelper const &_pHelper) {                             \
    fg_Wide##d_Previous(_pHelper);                                             \
    fg_Wide##d_Previous(_pHelper);                                             \
    fg_Wide##d_Previous(_pHelper);                                             \
    fg_Wide##d_Previous(_pHelper);                                             \
  }
DLink(1, 0) DLink(2, 1) DLink(3, 2) DLink(4, 3) DLink(5, 4) DLink(6, 5)
DLink(7, 6) DLink(8, 7) DLink(9, 8) DLink(10, 9) DLink(11, 10) DLink(12, 11)
#undef DLink
struct CHelper {
  int m_Value;
  auto f_Alias() {
    return fg_CallSafe([this, pThis = FHelper{this}]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:25: warning: a TCFuture coroutine lambda captures 'this' of 'CHelper', which is not an actor, so the object may be gone when it runs or resumes; derive 'CHelper' from CActor or CAllowUnsafeThis, keep it alive with a shared pointer in the capture, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
      fg_ReleaseAlias(pThis);
      co_await fg_Suspend();
      co_return m_Value;
    });
  }
  auto f_Deep() {
    return fg_CallSafe([this, pThis = FHelper{this}]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:25: warning: a TCFuture coroutine lambda captures 'this' of 'CHelper', which is not an actor, so the object may be gone when it runs or resumes; derive 'CHelper' from CActor or CAllowUnsafeThis, keep it alive with a shared pointer in the capture, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
      fg_Release9(pThis);
      co_await fg_Suspend();
      co_return m_Value;
    });
  }
  auto f_Discard() {
    return fg_CallSafe([this, pThis = FHelper{this}]() -> TCFuture<int> {
      fg_Inspect(pThis);
      co_await fg_Suspend();
      co_return m_Value;
    });
  }
  auto f_Wide() {
    return fg_CallSafe([this, pThis = FHelper{this}]() -> TCFuture<int> {
      fg_Wide12(pThis);
      co_await fg_Suspend();
      co_return m_Value;
    });
  }
};
}

namespace NRound172 {
CAwaiter fg_Pause();
// Reset by the call operator of an object.
struct CReset {
  template <class t_CType>
  void operator()(TCSharedPointer<t_CType> const &_pValue) const {
    const_cast<TCSharedPointer<t_CType> &>(_pValue) = {};
  }
};
struct CState {
  int m_Value;
  TCFuture<int> f_Start() {
    return fg_CallSafe(
        [this, pThis = TCSharedPointer<CState>{fg_Explicit(this)}]()
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: a TCFuture coroutine lambda captures 'this' of 'CState', which is not an actor, so the object may be gone when it runs or resumes; derive 'CState' from CActor or CAllowUnsafeThis, keep it alive with a shared pointer in the capture, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
            -> TCFuture<int> {
          CReset{}(pThis);
          co_await fg_Pause();
          co_return m_Value;
        });
  }
};
}

namespace NRound173 {
CAwaiter fg_Work();
// Reset by an override that a virtual call can run.
struct CState;
using FState = TCSharedPointer<CState>;
struct CReset {
  virtual void f_Reset(FState const &_pState) const {}
};
struct CActualReset : CReset {
  void f_Reset(FState const &_pState) const override {
    const_cast<FState &>(_pState) = FState{};
  }
};
extern CReset &g_Reset;
struct CState {
  int m_Value;
  TCFuture<int> f_Run() {
    return fg_CallSafe([this, pThis = FState{fg_Explicit(this)}]()
// CHECK-MESSAGES: :[[@LINE-1]]:25: warning: a TCFuture coroutine lambda captures 'this' of 'CState', which is not an actor, so the object may be gone when it runs or resumes; derive 'CState' from CActor or CAllowUnsafeThis, keep it alive with a shared pointer in the capture, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
                           -> TCFuture<int> {
      g_Reset.f_Reset(pThis);
      co_await fg_Work();
      co_return m_Value;
    });
  }
};
}

namespace NRound177 {
CAwaiter fg_Work();
// A copy of an alias that both values of a conditional make of this.
struct CHelper {
  int m_Value;
  TCFuture<int> f_Run(bool _bChoose) {
    auto *pAlias = _bChoose ? this : this;
    auto *pCopy = pAlias;
    return fg_CallSafe([pCopy]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:25: warning: a TCFuture coroutine lambda captures 'pCopy', which points to or into the object of 'CHelper', which is not an actor, so the object may be gone when it runs or resumes; derive 'CHelper' from CActor or CAllowUnsafeThis, keep it alive with a shared pointer in the capture, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
      co_await fg_Work();
      co_return pCopy->m_Value;
    });
  }
};
}

namespace NRound178 {
TCFuture<int> fg_Wait();
// A view made of what fg_MoveThis gives, which still refers to the object.
struct CHelper;
struct CView {
  int const *m_pValue;
  CView(CHelper &&_Helper);
};
struct CHelper {
  int m_Value;
  TCFuture<int> f_Run() {
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: TCFuture coroutine 'f_Run' is a member of 'CHelper', which is not an actor, and uses its object after a suspension, when it may be gone; derive 'CHelper' from CActor or CAllowUnsafeThis, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
    CView View = co_await fg_MoveThis(*this);
    co_await fg_Wait();
    co_return *View.m_pValue;
  }
};
inline CView::CView(CHelper &&_Helper) : m_pValue(&_Helper.m_Value) {}
}

namespace NRound180 {
TCFuture<int> fg_Wait();
// Moved into the frame in either value of a conditional.
struct CState {
  int m_Value;
  TCFuture<int> f_Run(bool _bChoose) {
    auto This = _bChoose ? co_await fg_MoveThis(*this) : co_await fg_MoveThis(*this);
    co_await fg_Wait();
    co_return This.m_Value;
  }
};
}

namespace NRound181 {
CAwaiter fg_Work();
// A lambda that keeps the object alive, which its own check tells of.
struct CState {
  int m_Value;
  TCFuture<int> f_Run() {
    co_return co_await fg_CallSafe(
        [this, pKeep = NMib::NStorage::TCSharedPointer<CState>{fg_Explicit(this)}]()
            -> TCFuture<int> {
          co_await fg_Work();
          co_return m_Value;
        });
  }
};
}

namespace NRound182 {
// A copy of an alias that is this or null.
struct CHelper {
  int m_Value = 0;
  void f_Run(bool _bChoose) {
    auto *pAlias = _bChoose ? this : nullptr;
    fg_CallSafe([pCopy = pAlias]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: a TCFuture coroutine lambda captures 'pCopy', which points to or into the object of 'CHelper', which is not an actor, so the object may be gone when it runs or resumes; derive 'CHelper' from CActor or CAllowUnsafeThis, keep it alive with a shared pointer in the capture, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
      co_await CAwaiter{};
      co_return pCopy ? pCopy->m_Value : 0;
    });
  }
};
}

namespace NRound183 {
struct CState {
  int m_Value = 1;
  // An alias of this moved into the capture.
  TCFuture<int> f_Moved() {
    auto *pThis = this;
    return fg_CallSafe([pMoved = fg_Move(pThis)]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:25: warning: a TCFuture coroutine lambda captures 'pMoved', which points to or into the object of 'CState', which is not an actor, so the object may be gone when it runs or resumes; derive 'CState' from CActor or CAllowUnsafeThis, keep it alive with a shared pointer in the capture, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
      co_await CAwaiter{};
      co_return pMoved->m_Value;
    });
  }
  // A pointer into the object that a member function gives.
  int *f_Get() { return &m_Value; }
  TCFuture<int> f_Got() {
    return fg_CallSafe([pValue = f_Get()]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:25: warning: a TCFuture coroutine lambda captures 'pValue', which points to or into the object of 'CState', which is not an actor, so the object may be gone when it runs or resumes; derive 'CState' from CActor or CAllowUnsafeThis, keep it alive with a shared pointer in the capture, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
      co_await CAwaiter{};
      co_return *pValue;
    });
  }
};
// Not a copy of the value of a pointer member, which points elsewhere.
int g_Value;
struct CHolder {
  int *const m_pValue = &g_Value;
  TCFuture<int> f_Copied() {
    return fg_CallSafe([pValue = m_pValue]() -> TCFuture<int> {
      co_await CAwaiter{};
      co_return *pValue;
    });
  }
};
}

namespace NRound211
{
	TCFuture<int> fg_Suspend();
	void fg_HandOff(struct CState *_pState);
	// Not a copy of a local shared pointer that gave the object away.
	struct CState
	{
		int m_Value;
		void f_Start()
		{
			TCSharedPointer<CState> pThis{fg_Explicit(this)};
			fg_HandOff(pThis.f_Detach());
			fg_CallSafe
				(
					[this, pThis]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: a TCFuture coroutine lambda captures 'this' of 'CState', which is not an actor, so the object may be gone when it runs or resumes; derive 'CState' from CActor or CAllowUnsafeThis, keep it alive with a shared pointer in the capture, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
					{
						co_await fg_Suspend();
						co_return m_Value;
					}
				)
			;
		}
	};
}

namespace NRound216
{
	TCFuture<int> fg_Suspend();
	struct CState
	{
		int m_Value;
		// A copy of a local shared pointer to it keeps it alive.
		void f_Start()
		{
			TCSharedPointer<CState> pThis{fg_Explicit(this)};
			fg_CallSafe
				(
					[this, pKeep = pThis]() -> TCFuture<int>
					{
						co_await fg_Suspend();
						co_return m_Value;
					}
				)
			;
		}
		// Not one that can be null.
		void f_Maybe(bool _bKeep)
		{
			fg_CallSafe
				(
					[this, pThis = TCSharedPointer<CState>{fg_Explicit(_bKeep ? this : nullptr)}]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: a TCFuture coroutine lambda captures 'this' of 'CState', which is not an actor, so the object may be gone when it runs or resumes; derive 'CState' from CActor or CAllowUnsafeThis, keep it alive with a shared pointer in the capture, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
					{
						co_await fg_Suspend();
						co_return m_Value;
					}
				)
			;
		}
	};
}

namespace NRound221 {
TCFuture<int> fg_Suspend();
// A lambda that is never made uses nothing.
struct CHelper {
  int m_Value = 0;
  void f_Run() {
    if constexpr (false) {
      fg_CallSafe([this]() -> TCFuture<int> {
        co_await fg_Suspend();
        co_return m_Value;
      });
    }
    if (false) {
      fg_CallSafe([this]() -> TCFuture<int> {
        co_await fg_Suspend();
        co_return m_Value;
      });
    }
  }
  void f_Made() {
    fg_CallSafe([this]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: a TCFuture coroutine lambda captures 'this' of 'CHelper', which is not an actor, so the object may be gone when it runs or resumes; derive 'CHelper' from CActor or CAllowUnsafeThis, keep it alive with a shared pointer in the capture, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
      co_await fg_Suspend();
      co_return m_Value;
    });
  }
};
}

namespace NRound232
{
	// A copy of an unchanged local pointer into the object is of the object.
	CAwaiter fg_Work();

	struct CState
	{
		int m_Value;

		TCFuture<int> f_Run()
		{
			int *pValue = &m_Value;
			return fg_CallSafe
				(
					[pCopy = pValue]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: a TCFuture coroutine lambda captures 'pCopy', which points to or into the object of 'CState', which is not an actor, so the object may be gone when it runs or resumes; derive 'CState' from CActor or CAllowUnsafeThis, keep it alive with a shared pointer in the capture, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
					{
						co_await fg_Work();
						co_return *pCopy;
					}
				)
			;
		}
	};
}

namespace NRound322 {
TCFuture<int> fg_Suspend();
// Aliases of this that each are a conditional of the one before in both
// values, each looked at once for each depth.
#define DMibPick2(P) (_bChoose ? (P) : (P))
#define DMibPick4(P) DMibPick2(DMibPick2(P))
#define DMibPick8(P) DMibPick2(DMibPick4(P))
#define DMibPick16(P) DMibPick2(DMibPick8(P))
#define DMibPick32(P) DMibPick2(DMibPick16(P))
struct CHelper {
  int m_Value;
  int *f_Get() { return &m_Value; }
  void f_Test(bool _bChoose) {
    auto *p0 = this;
    auto *p1 = DMibPick32(p0);
    auto *p2 = DMibPick32(p1);
    auto *p3 = DMibPick32(p2);
    auto *p4 = DMibPick32(p3);
    auto *p5 = DMibPick32(p4);
    fg_CallSafe([pValue = p5->f_Get()]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: a TCFuture coroutine lambda captures 'pValue', which points to or into the object of 'CHelper', which is not an actor, so the object may be gone when it runs or resumes; derive 'CHelper' from CActor or CAllowUnsafeThis, keep it alive with a shared pointer in the capture, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
      co_await fg_Suspend();
      co_return *pValue;
    });
  }
};
struct CActorHelper : CActor {
  int m_Value;
  int *f_Get() { return &m_Value; }
  void f_Test(bool _bChoose) {
    auto *p0 = this;
    auto *p1 = DMibPick32(p0);
    auto *p2 = DMibPick32(p1);
    auto *p3 = DMibPick32(p2);
    auto *p4 = DMibPick32(p3);
    auto *p5 = DMibPick32(p4);
    fg_CallSafe([pValue = p5->f_Get()]() -> TCFuture<int> {
      co_await fg_Suspend();
      co_return *pValue;
    });
  }
};
#undef DMibPick2
#undef DMibPick4
#undef DMibPick8
#undef DMibPick16
#undef DMibPick32
}
