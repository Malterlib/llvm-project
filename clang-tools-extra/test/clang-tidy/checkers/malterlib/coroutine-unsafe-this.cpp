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

// A shared pointer made of what can be null keeps nothing alive: of a
// pointer read back through its address, `*&pMaybe`, which is what the
// pointer is, and of a `dynamic_cast`, which gives null when the object isn't
// of the class.
struct CNullableBase
{
	virtual ~CNullableBase();
	int m_Value = 0;
	TCUnsafeFuture<int> f_Run(bool _bKeep);
};
struct CNullableDerived : CNullableBase
{
};
TCUnsafeFuture<int> CNullableBase::f_Run(bool _bKeep)
{
	CNullableBase *const pMaybe = _bKeep ? this : nullptr;
	co_await
		(
			g_Dispatch / [this, pThis = TCSharedPointer<CNullableBase>{fg_Explicit(static_cast<CNullableBase *>(*&pMaybe))}]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: a TCFuture coroutine lambda captures 'this' of 'CNullableBase', which is not an actor, so the object may be gone when it runs or resumes; derive 'CNullableBase' from CActor or CAllowUnsafeThis, keep it alive with a shared pointer in the capture, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
			{
				co_await fg_Suspend();
				co_return m_Value;
			}
		)
	;
	co_await
		(
			g_Dispatch / [this, pThis = TCSharedPointer<CNullableDerived>{fg_Explicit(dynamic_cast<CNullableDerived *>(&*this))}]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: a TCFuture coroutine lambda captures 'this' of 'CNullableBase', which is not an actor, so the object may be gone when it runs or resumes; derive 'CNullableBase' from CActor or CAllowUnsafeThis, keep it alive with a shared pointer in the capture, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
			{
				co_await fg_Suspend();
				co_return m_Value;
			}
		)
	;
	// A `dynamic_cast` to the class itself or a base doesn't check, and can't
	// give null; the address of what a pointer that can be null points to,
	// `&*pMaybe`, is that pointer.
	co_await
		(
			g_Dispatch / [this, pThis = TCSharedPointer<CNullableBase>{fg_Explicit(dynamic_cast<CNullableBase *>(this))}]() -> TCFuture<int>
			{
				co_await fg_Suspend();
				co_return m_Value;
			}
		)
	;
	co_await
		(
			g_Dispatch / [this, pThis = TCSharedPointer<CNullableBase>{fg_Explicit(static_cast<CNullableBase *>(&*pMaybe))}]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: a TCFuture coroutine lambda captures 'this' of 'CNullableBase', which is not an actor, so the object may be gone when it runs or resumes; derive 'CNullableBase' from CActor or CAllowUnsafeThis, keep it alive with a shared pointer in the capture, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
			{
				co_await fg_Suspend();
				co_return m_Value;
			}
		)
	;
	co_return 0;
}

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
		// But one whose values can't be null on any path.
		void f_KeepEither(bool _bFirst)
		{
			fg_CallSafe
				(
					[this, pThis = TCSharedPointer<CState>{fg_Explicit(_bFirst ? this : this)}]() -> TCFuture<int>
					{
						co_await fg_Suspend();
						co_return m_Value;
					}
				)
			;
		}
		// Nor one that can be null through a helper that gives back what it is
		// given.
		template <typename t_CType>
		static t_CType *fs_Identity(t_CType *_pValue)
		{
			return _pValue;
		}
		void f_MaybeThroughHelper(bool _bKeep)
		{
			fg_CallSafe
				(
					[this, pThis = TCSharedPointer<CState>{fg_Explicit(fs_Identity(_bKeep ? this : nullptr))}]() -> TCFuture<int>
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

// A member function whose result must not outlive its object, which its callers
// keep in a local declared after the object in the same block: the guard
// lambda it gives runs when the guard is destroyed, while the object is alive,
// until it first suspends.
namespace NResultEndsBeforeObject
{
#define DMibResultEndsBeforeObject [[clang::annotate("malterlib-result-ends-before-object")]]
	using namespace NMib::NConcurrency;
	template <typename t_FDestroy>
	CAsyncDestroyAwaiter fg_AsyncDestroyGeneric(t_FDestroy &&_fDestroy)
	{
		return {};
	}
	TCFuture<int> fg_StopTimer();

	struct CGuardWithField
	{
		void f_Clear();
		int m_Value = 0;
	};

	struct CDestroyingGuard
	{
		~CDestroyingGuard();
	};

	struct CMovableGuard
	{
		CMovableGuard(CMovableGuard &&_Other, int _Extra = 0);
		void f_Clear();
	};

	struct CBaseGuard
	{
		CBaseGuard(CBaseGuard &&_Other);
		void f_Clear();
	};
	struct CDerivedGuard : CBaseGuard
	{
		CDerivedGuard(CDerivedGuard &&_Other);
	};

	// Awaitable through a free `operator co_await`, but not a future
	// FutureTypes lists.
	struct CFreeAwaitable
	{
		int m_Value = 0;
	};
	CAsyncDestroyAwaiter operator co_await(CFreeAwaitable &&_Awaitable);

	// An awaiter, not a future, whose await gives a future.
	struct CStage
	{
		bool await_ready() const noexcept;
		void await_suspend(std::coroutine_handle<>) const noexcept;
		TCUnsafeFuture<CAsyncDestroyAwaiter> await_resume() noexcept;
	};

	struct CLambdaGuard
	{
		template <typename t_FFunction>
		CLambdaGuard(t_FFunction &&_fFunction)
		{
		}
	};

	struct CDesignatedGuard
	{
		int m_Value = 0;
		CLambdaGuard m_Guard;
	};

	struct CUnitVoidFutureFunction
	{
		template <typename t_FFunction>
		CUnitVoidFutureFunction(t_FFunction &&_fFunction)
		{
		}
	};
	CAsyncDestroyAwaiter fg_AsyncDestroyFunction(CUnitVoidFutureFunction &&_fDestroy);
	struct CConvertible
	{
		operator CAsyncDestroyAwaiter() const;
	};
	template <typename t_FFunction>
	CConvertible fg_MakeConvertible(t_FFunction &&_fFunction)
	{
		return {};
	}
	struct CPairGuard
	{
		template <typename t_FFunction>
		CPairGuard(t_FFunction &&_fFunction, int _Value)
		{
		}
	};

	struct CLaunches
	{
		DMibResultEndsBeforeObject TCUnsafeFuture<CAsyncDestroyAwaiter> f_Init();
		DMibResultEndsBeforeObject CAsyncDestroyAwaiter f_InitFunction();
		DMibResultEndsBeforeObject CAsyncDestroyAwaiter f_InitConvertible();
		DMibResultEndsBeforeObject CAsyncDestroyAwaiter f_InitStatement();
		DMibResultEndsBeforeObject TCFuture<CPairGuard> f_InitCoPair();
		DMibResultEndsBeforeObject CLambdaGuard f_InitConverted();
		DMibResultEndsBeforeObject CLambdaGuard f_InitConstructed();
		DMibResultEndsBeforeObject TCFuture<CLambdaGuard> f_InitCoConverted();
		DMibResultEndsBeforeObject TCFuture<CLambdaGuard> f_InitCoBraced();
		DMibResultEndsBeforeObject CDesignatedGuard f_InitDesignated();
		DMibResultEndsBeforeObject CDesignatedGuard f_InitPositional();
		DMibResultEndsBeforeObject CDestroyingGuard f_InitDestroying();
		DMibResultEndsBeforeObject CFreeAwaitable f_InitFree();
		DMibResultEndsBeforeObject TCUnsafeFuture<TCUnsafeFuture<CAsyncDestroyAwaiter>> f_InitNested();
		DMibResultEndsBeforeObject TCUnsafeFuture<CStage> f_InitStaged();
		DMibResultEndsBeforeObject CStage f_InitStage();
		DMibResultEndsBeforeObject TCUnsafeFuture<TCUnsafeFuture<TCUnsafeFuture<TCUnsafeFuture<TCUnsafeFuture<TCUnsafeFuture<TCUnsafeFuture<TCUnsafeFuture<TCUnsafeFuture<TCUnsafeFuture<TCUnsafeFuture<TCUnsafeFuture<TCUnsafeFuture<TCUnsafeFuture<TCUnsafeFuture<TCUnsafeFuture<TCUnsafeFuture<CAsyncDestroyAwaiter>>>>>>>>>>>>>>>>> f_InitDeep();
		TCUnsafeFuture<CAsyncDestroyAwaiter> f_InitUnannotated();
		DMibResultEndsBeforeObject TCUnsafeFuture<CAsyncDestroyAwaiter> f_InitUsesLate();
		DMibResultEndsBeforeObject TCUnsafeFuture<CAsyncDestroyAwaiter> f_InitSubmits();
		DMibResultEndsBeforeObject CAsyncDestroyAwaiter f_InitDiscards();
		DMibResultEndsBeforeObject CAsyncDestroyAwaiter f_InitDiscardsMacro();
		DMibResultEndsBeforeObject CGuardWithField f_InitGuard();
		DMibResultEndsBeforeObject CGuardWithField *f_InitPointer();
		DMibResultEndsBeforeObject CGuardWithField &f_InitReference();
		DMibResultEndsBeforeObject CMovableGuard f_InitMovable();
		DMibResultEndsBeforeObject CDerivedGuard f_InitDerived();
		DMibResultEndsBeforeObject CAsyncDestroyAwaiter f_InitChooses(bool _bChoose);
		DMibResultEndsBeforeObject CAsyncDestroyAwaiter f_InitCounts();
		DMibResultEndsBeforeObject TCFuture<CAsyncDestroyAwaiter> f_InitFuture();
		DMibResultEndsBeforeObject TCFuture<CAsyncDestroyAwaiter> f_InitCaptures();
		DMibResultEndsBeforeObject TCFuture<CAsyncDestroyAwaiter> f_InitInvokes();
		DMibResultEndsBeforeObject int f_InitToken();
		DMibResultEndsBeforeObject int f_InitRegisters();
		DMibResultEndsBeforeObject CAsyncDestroyAwaiter f_InitByMember();
		DMibResultEndsBeforeObject CAsyncDestroyAwaiter f_InitByPointer();
		DMibResultEndsBeforeObject CAsyncDestroyAwaiter f_InitReceiver();
		void f_Abort();
		CGuardWithField f_MakeGuard();
		int m_Value = 0;
	};

	TCUnsafeFuture<CAsyncDestroyAwaiter> CLaunches::f_Init()
	{
		co_return fg_AsyncDestroyGeneric
			(
				[this]() -> TCFuture<int>
				{
					auto Value = m_Value;
					co_await fg_StopTimer();
					(void)Value;
					co_return 0;
				}
			)
		;
	}

	TCUnsafeFuture<CAsyncDestroyAwaiter> CLaunches::f_InitUnannotated()
	{
		co_return fg_AsyncDestroyGeneric
			(
				[this]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: a TCFuture coroutine lambda captures 'this' of 'CLaunches', which is not an actor, so the object may be gone when it runs or resumes; derive 'CLaunches' from CActor or CAllowUnsafeThis, keep it alive with a shared pointer in the capture, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
				{
					auto Value = m_Value;
					co_await fg_StopTimer();
					(void)Value;
					co_return 0;
				}
			)
		;
	}

	// Not one that uses the object after it suspends.
	TCUnsafeFuture<CAsyncDestroyAwaiter> CLaunches::f_InitUsesLate()
	{
		co_return fg_AsyncDestroyGeneric
			(
				[this]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: a TCFuture coroutine lambda captures 'this' of 'CLaunches', which is not an actor, so the object may be gone when it runs or resumes; derive 'CLaunches' from CActor or CAllowUnsafeThis, keep it alive with a shared pointer in the capture, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
				{
					co_await fg_StopTimer();
					auto Value = m_Value;
					(void)Value;
					co_return 0;
				}
			)
		;
	}

	// Not a lambda it gives elsewhere than in its result, which the result
	// doesn't end.
	template <typename t_FFunction>
	void fg_Submit(t_FFunction &&_fFunction)
	{
	}

	TCUnsafeFuture<CAsyncDestroyAwaiter> CLaunches::f_InitSubmits()
	{
		fg_Submit
			(
				[this]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: a TCFuture coroutine lambda captures 'this' of 'CLaunches', which is not an actor, so the object may be gone when it runs or resumes; derive 'CLaunches' from CActor or CAllowUnsafeThis, keep it alive with a shared pointer in the capture, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
				{
					auto Value = m_Value;
					co_await fg_StopTimer();
					(void)Value;
					co_return 0;
				}
			)
		;
		co_return fg_AsyncDestroyGeneric
			(
				[this]() -> TCFuture<int>
				{
					auto Value = m_Value;
					co_await fg_StopTimer();
					(void)Value;
					co_return 0;
				}
			)
		;
	}

	// Nor one that is discarded where it is returned, or that only decides
	// which is.
	CAsyncDestroyAwaiter CLaunches::f_InitDiscards()
	{
		return
			(
				fg_Submit
				(
					[this]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: a TCFuture coroutine lambda captures 'this' of 'CLaunches', which is not an actor, so the object may be gone when it runs or resumes; derive 'CLaunches' from CActor or CAllowUnsafeThis, keep it alive with a shared pointer in the capture, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
					{
						auto Value = m_Value;
						co_await fg_StopTimer();
						(void)Value;
						co_return 0;
					}
				)
				, CAsyncDestroyAwaiter{}
			)
		;
	}

	// Also when a macro writes the comma, which is told by the AST, not by
	// where the operands are written.
#define DDiscardFirst(d_First, d_Second) ((d_First), (d_Second))
	CAsyncDestroyAwaiter CLaunches::f_InitDiscardsMacro()
	{
		return DDiscardFirst
			(
				fg_Submit
				(
					[this]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: a TCFuture coroutine lambda captures 'this' of 'CLaunches', which is not an actor, so the object may be gone when it runs or resumes; derive 'CLaunches' from CActor or CAllowUnsafeThis, keep it alive with a shared pointer in the capture, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
					{
						auto Value = m_Value;
						co_await fg_StopTimer();
						(void)Value;
						co_return 0;
					}
				)
				, CAsyncDestroyAwaiter{}
			)
		;
	}

	// Nor one given to what makes a scalar of it.
	template <typename t_FFunction>
	int fg_CountOf(t_FFunction &&_fFunction)
	{
		return 0;
	}
	CAsyncDestroyAwaiter CLaunches::f_InitCounts()
	{
		return fg_AsyncDestroyGeneric
			(
				fg_CountOf
				(
					[this]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: a TCFuture coroutine lambda captures 'this' of 'CLaunches', which is not an actor, so the object may be gone when it runs or resumes; derive 'CLaunches' from CActor or CAllowUnsafeThis, keep it alive with a shared pointer in the capture, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
					{
						auto Value = m_Value;
						co_await fg_StopTimer();
						(void)Value;
						co_return 0;
					}
				)
			)
		;
	}

	// Nor is the member itself, a coroutine, for what the lambda of its result
	// does.
	TCFuture<CAsyncDestroyAwaiter> CLaunches::f_InitFuture()
	{
		co_return fg_AsyncDestroyGeneric
			(
				[this]() -> TCFuture<int>
				{
					auto Value = m_Value;
					co_await fg_StopTimer();
					(void)Value;
					co_return 0;
				}
			)
		;
	}

	// But not one that is called there, which isn't kept.
	TCFuture<CAsyncDestroyAwaiter> CLaunches::f_InitInvokes()
// CHECK-MESSAGES: :[[@LINE-1]]:44: warning: TCFuture coroutine 'f_InitInvokes' is a member of 'CLaunches', which is not an actor, and uses its object after a suspension, when it may be gone; derive 'CLaunches' from CActor or CAllowUnsafeThis, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
	{
		co_return [this](int _Value)
			{
				(void)m_Value;
				(void)_Value;
				return CAsyncDestroyAwaiter{};
			}
			(co_await fg_StopTimer())
		;
	}

	// Nor one of what is then converted to what isn't an object, which doesn't
	// keep it.
	struct CToken
	{
		operator int() const;
	};
	template <typename t_FFunction>
	CToken fg_MakeToken(t_FFunction &&_fFunction)
	{
		return {};
	}
	int CLaunches::f_InitToken()
	{
		return static_cast<int>
			(
				fg_MakeToken
				(
					[this]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: a TCFuture coroutine lambda captures 'this' of 'CLaunches', which is not an actor, so the object may be gone when it runs or resumes; derive 'CLaunches' from CActor or CAllowUnsafeThis, keep it alive with a shared pointer in the capture, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
					{
						auto Value = m_Value;
						co_await fg_StopTimer();
						(void)Value;
						co_return 0;
					}
				)
			)
		;
	}

	// Nor one given to a helper that gives back a number of the same type as
	// the result, like a token of a registration, which ends nothing when it
	// is destroyed.
	int CLaunches::f_InitRegisters()
	{
		return fg_CountOf
			(
				[this]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: a TCFuture coroutine lambda captures 'this' of 'CLaunches', which is not an actor, so the object may be gone when it runs or resumes; derive 'CLaunches' from CActor or CAllowUnsafeThis, keep it alive with a shared pointer in the capture, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
				{
					auto Value = m_Value;
					co_await fg_StopTimer();
					(void)Value;
					co_return 0;
				}
			)
		;
	}

	// Also one converted to what is returned by a constructor that isn't
	// written, like one that is.
	CLambdaGuard CLaunches::f_InitConverted()
	{
		return
			[this]() -> TCFuture<int>
			{
				auto Value = m_Value;
				co_await fg_StopTimer();
				(void)Value;
				co_return 0;
			}
		;
	}
	CLambdaGuard CLaunches::f_InitConstructed()
	{
		return CLambdaGuard
			(
				[this]() -> TCFuture<int>
				{
					auto Value = m_Value;
					co_await fg_StopTimer();
					(void)Value;
					co_return 0;
				}
			)
		;
	}

	// Also by `co_return`, which converts it in what it gives to the promise.
	TCFuture<CLambdaGuard> CLaunches::f_InitCoConverted()
	{
		co_return
			[this]() -> TCFuture<int>
			{
				auto Value = m_Value;
				co_await fg_StopTimer();
				(void)Value;
				co_return 0;
			}
		;
	}
	// Also in braces.
	TCFuture<CLambdaGuard> CLaunches::f_InitCoBraced()
	{
		co_return
		{
			[this]() -> TCFuture<int>
			{
				auto Value = m_Value;
				co_await fg_StopTimer();
				(void)Value;
				co_return 0;
			}
		};
	}
	// Also an initializer of an aggregate, also with a designator.
	CDesignatedGuard CLaunches::f_InitDesignated()
	{
		return CDesignatedGuard
			{
				.m_Value = 1,
				.m_Guard =
					[this]() -> TCFuture<int>
					{
						auto Value = m_Value;
						co_await fg_StopTimer();
						(void)Value;
						co_return 0;
					}
			}
		;
	}
	CDesignatedGuard CLaunches::f_InitPositional()
	{
		return CDesignatedGuard
			{
				1,
				[this]() -> TCFuture<int>
				{
					auto Value = m_Value;
					co_await fg_StopTimer();
					(void)Value;
					co_return 0;
				}
			}
		;
	}

	// Also a direct argument of a function that converts it to its parameter,
	// like `fg_AsyncDestroyGeneric(FUnitVoidFutureFunction &&)`.
	CAsyncDestroyAwaiter CLaunches::f_InitFunction()
	{
		return fg_AsyncDestroyFunction
			(
				[this]() -> TCFuture<int>
				{
					auto Value = m_Value;
					co_await fg_StopTimer();
					(void)Value;
					co_return 0;
				}
			)
		;
	}
	// Also one of several arguments in braces of `co_return`.
	TCFuture<CPairGuard> CLaunches::f_InitCoPair()
	{
		co_return
			{
				[this]() -> TCFuture<int>
				{
					auto Value = m_Value;
					co_await fg_StopTimer();
					(void)Value;
					co_return 0;
				}
				, 1
			}
		;
	}
	// But not what a user-defined conversion operator converts, nor one in a
	// statement expression.
	CAsyncDestroyAwaiter CLaunches::f_InitConvertible()
	{
		return fg_MakeConvertible
			(
				[this]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: a TCFuture coroutine lambda captures 'this' of 'CLaunches', which is not an actor, so the object may be gone when it runs or resumes; derive 'CLaunches' from CActor or CAllowUnsafeThis, keep it alive with a shared pointer in the capture, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
				{
					auto Value = m_Value;
					co_await fg_StopTimer();
					(void)Value;
					co_return 0;
				}
			)
		;
	}
	CAsyncDestroyAwaiter CLaunches::f_InitStatement()
	{
		return
			({
				fg_AsyncDestroyGeneric
				(
					[this]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: a TCFuture coroutine lambda captures 'this' of 'CLaunches', which is not an actor, so the object may be gone when it runs or resumes; derive 'CLaunches' from CActor or CAllowUnsafeThis, keep it alive with a shared pointer in the capture, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
					{
						auto Value = m_Value;
						co_await fg_StopTimer();
						(void)Value;
						co_return 0;
					}
				);
			})
		;
	}

	// Nor one given to a member function, which makes what is returned, also
	// of what is made of it, `fg_Wrap(...).f_Finish()`: only a direct argument
	// of a constructor or of a function that isn't a member, which makes what
	// is returned, is part of it.
	struct CMaker
	{
		template <typename t_FFunction>
		CAsyncDestroyAwaiter f_Make(t_FFunction &&_fFunction)
		{
			return {};
		}
	};
	struct CWrapped
	{
		CAsyncDestroyAwaiter f_Finish();
	};
	template <typename t_FFunction>
	CWrapped fg_Wrap(t_FFunction &&_fFunction)
	{
		return {};
	}
	// Also a function that isn't a member called through a pointer to it.
	CAsyncDestroyAwaiter fg_AsyncDestroyThroughPointer(NMib::NFunction::TCFunction<TCFuture<int> ()> _fDestroy);
	CAsyncDestroyAwaiter CLaunches::f_InitByPointer()
	{
		auto fFactory = &fg_AsyncDestroyThroughPointer;
		return fFactory
			(
					[this]() -> TCFuture<int>
					{
						auto Value = m_Value;
						co_await fg_StopTimer();
						(void)Value;
						co_return 0;
					}
			)
		;
	}
	CAsyncDestroyAwaiter CLaunches::f_InitByMember()
	{
		return CMaker{}.f_Make
			(
					[this]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: a TCFuture coroutine lambda captures 'this' of 'CLaunches', which is not an actor, so the object may be gone when it runs or resumes; derive 'CLaunches' from CActor or CAllowUnsafeThis, keep it alive with a shared pointer in the capture, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
					{
						auto Value = m_Value;
						co_await fg_StopTimer();
						(void)Value;
						co_return 0;
					}
			)
		;
	}
	CAsyncDestroyAwaiter CLaunches::f_InitReceiver()
	{
		return fg_Wrap
			(
					[this]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: a TCFuture coroutine lambda captures 'this' of 'CLaunches', which is not an actor, so the object may be gone when it runs or resumes; derive 'CLaunches' from CActor or CAllowUnsafeThis, keep it alive with a shared pointer in the capture, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
					{
						auto Value = m_Value;
						co_await fg_StopTimer();
						(void)Value;
						co_return 0;
					}
			)
			.f_Finish()
		;
	}

	// Not one that overrides what isn't annotated so, through whose callers the
	// result can come.
	struct CBaseLaunches
	{
		virtual TCUnsafeFuture<CAsyncDestroyAwaiter> f_Init();
	};
	struct CDerivedLaunches : CBaseLaunches
	{
		DMibResultEndsBeforeObject TCUnsafeFuture<CAsyncDestroyAwaiter> f_Init() override;
		int m_Value = 0;
	};
	TCUnsafeFuture<CAsyncDestroyAwaiter> CDerivedLaunches::f_Init()
	{
		co_return fg_AsyncDestroyGeneric
			(
				[this]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: a TCFuture coroutine lambda captures 'this' of 'CDerivedLaunches', which is not an actor, so the object may be gone when it runs or resumes; derive 'CDerivedLaunches' from CActor or CAllowUnsafeThis, keep it alive with a shared pointer in the capture, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
				{
					auto Value = m_Value;
					co_await fg_StopTimer();
					(void)Value;
					co_return 0;
				}
			)
		;
	}

	// What the captures of its lambda are made of runs in the member, also
	// after it suspends.
	TCFuture<CAsyncDestroyAwaiter> CLaunches::f_InitCaptures()
// CHECK-MESSAGES: :[[@LINE-1]]:44: warning: TCFuture coroutine 'f_InitCaptures' is a member of 'CLaunches', which is not an actor, and uses its object after a suspension, when it may be gone; derive 'CLaunches' from CActor or CAllowUnsafeThis, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
	{
		co_await fg_StopTimer();
		co_return fg_AsyncDestroyGeneric
			(
				[Value = m_Value]() -> TCFuture<int>
				{
					co_return Value;
				}
			)
		;
	}

	// Each function it overrides once, also through diamonds, and none exempt
	// when they can't all be gone through.
	struct CDiamondUnannotated
	{
		virtual TCUnsafeFuture<CAsyncDestroyAwaiter> f_Init();
	};
	struct CDiamond0
	{
		DMibResultEndsBeforeObject virtual TCUnsafeFuture<CAsyncDestroyAwaiter> f_Init();
	};
	struct CDiamond1A : public CDiamond0
	{
		DMibResultEndsBeforeObject TCUnsafeFuture<CAsyncDestroyAwaiter> f_Init() override;
	};
	struct CDiamond1B : public CDiamond0
	{
		DMibResultEndsBeforeObject TCUnsafeFuture<CAsyncDestroyAwaiter> f_Init() override;
	};
	struct CDiamond2A : public CDiamond1A, public CDiamond1B
	{
		DMibResultEndsBeforeObject TCUnsafeFuture<CAsyncDestroyAwaiter> f_Init() override;
	};
	struct CDiamond2B : public CDiamond1A, public CDiamond1B
	{
		DMibResultEndsBeforeObject TCUnsafeFuture<CAsyncDestroyAwaiter> f_Init() override;
	};
	struct CDiamond3A : public CDiamond2A, public CDiamond2B
	{
		DMibResultEndsBeforeObject TCUnsafeFuture<CAsyncDestroyAwaiter> f_Init() override;
	};
	struct CDiamond3B : public CDiamond2A, public CDiamond2B
	{
		DMibResultEndsBeforeObject TCUnsafeFuture<CAsyncDestroyAwaiter> f_Init() override;
	};
	struct CDiamond4A : public CDiamond3A, public CDiamond3B
	{
		DMibResultEndsBeforeObject TCUnsafeFuture<CAsyncDestroyAwaiter> f_Init() override;
	};
	struct CDiamond4B : public CDiamond3A, public CDiamond3B
	{
		DMibResultEndsBeforeObject TCUnsafeFuture<CAsyncDestroyAwaiter> f_Init() override;
	};
	struct CDiamond5A : public CDiamond4A, public CDiamond4B
	{
		DMibResultEndsBeforeObject TCUnsafeFuture<CAsyncDestroyAwaiter> f_Init() override;
	};
	struct CDiamond5B : public CDiamond4A, public CDiamond4B
	{
		DMibResultEndsBeforeObject TCUnsafeFuture<CAsyncDestroyAwaiter> f_Init() override;
	};
	struct CDiamond6A : public CDiamond5A, public CDiamond5B
	{
		DMibResultEndsBeforeObject TCUnsafeFuture<CAsyncDestroyAwaiter> f_Init() override;
	};
	struct CDiamond6B : public CDiamond5A, public CDiamond5B
	{
		DMibResultEndsBeforeObject TCUnsafeFuture<CAsyncDestroyAwaiter> f_Init() override;
	};
	struct CDiamondLaunches : public CDiamondUnannotated, public CDiamond6A, public CDiamond6B
	{
		DMibResultEndsBeforeObject TCUnsafeFuture<CAsyncDestroyAwaiter> f_Init() override;
		int m_Value = 0;
	};
	TCUnsafeFuture<CAsyncDestroyAwaiter> CDiamondLaunches::f_Init()
	{
		co_return fg_AsyncDestroyGeneric
			(
				[this]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: a TCFuture coroutine lambda captures 'this' of 'CDiamondLaunches', which is not an actor, so the object may be gone when it runs or resumes; derive 'CDiamondLaunches' from CActor or CAllowUnsafeThis, keep it alive with a shared pointer in the capture, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
				{
					auto Value = m_Value;
					co_await fg_StopTimer();
					(void)Value;
					co_return 0;
				}
			)
		;
	}

	// Nor one of either value of a conditional: what is returned is told from
	// the top, and only a constructor or a call that makes it, of which it is
	// a direct argument, keeps it.
	CAsyncDestroyAwaiter CLaunches::f_InitChooses(bool _bChoose)
	{
		return _bChoose
			? fg_AsyncDestroyGeneric
			(
				[this]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: a TCFuture coroutine lambda captures 'this' of 'CLaunches', which is not an actor, so the object may be gone when it runs or resumes; derive 'CLaunches' from CActor or CAllowUnsafeThis, keep it alive with a shared pointer in the capture, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
				{
					auto Value = m_Value;
					co_await fg_StopTimer();
					(void)Value;
					co_return 0;
				}
			)
			: CAsyncDestroyAwaiter{}
		;
	}

	TCUnsafeFuture<int> fg_Callers()
	{
		CLaunches Launches;
		auto DestroyLaunches = co_await co_await Launches.f_Init();
		// Awaited where it is moved, or used as an object, it doesn't leave.
		DestroyLaunches.f_Clear();
		auto DestroyAwaited = co_await co_await Launches.f_Init();
		co_await fg_Move(DestroyAwaited);

		// Not one kept before the object, nor not kept in a local.
		TCUnsafeFuture<CAsyncDestroyAwaiter> Early;
		{
			CLaunches Inner;
			Early = Inner.f_Init();
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: the result of 'f_Init' must not outlive its object; keep it in a local declared after the object, a local, in the same block, and don't return, move or capture it [malterlib-coroutine-unsafe-this]
		}
		co_return 0;
	}

	// Not of an object a pointer points to, which the local doesn't keep.
	CLaunches *fg_GetLaunches();
	TCUnsafeFuture<int> fg_PointerCaller()
	{
		CLaunches *pLaunches = fg_GetLaunches();
		auto DestroyLaunches = co_await co_await pLaunches->f_Init();
// CHECK-MESSAGES: :[[@LINE-1]]:55: warning: the result of 'f_Init' must not outlive its object; keep it in a local declared after the object, a local, in the same block, and don't return, move or capture it [malterlib-coroutine-unsafe-this]
		co_return 0;
	}

	// Not one that never runs, after what always leaves.
	CGuardWithField fg_ReturnsAfterReturn()
	{
		CLaunches Launches;
		return CGuardWithField{};
		return Launches.f_InitGuard();
	}
	// Also after a throw or a call that doesn't return.
	[[noreturn]] void fg_Abort();
	CGuardWithField fg_ReturnsAfterThrow()
	{
		CLaunches Launches;
		throw 1;
		return Launches.f_InitGuard();
	}
	CGuardWithField fg_ReturnsAfterNoReturn()
	{
		CLaunches Launches;
		fg_Abort();
		return Launches.f_InitGuard();
	}

	// Nor one kept in a local that is returned, moved out or captured.
	CGuardWithField fg_ReturnsHolder()
	{
		CLaunches Launches;
		auto Destroy = Launches.f_InitGuard();
// CHECK-MESSAGES: :[[@LINE-1]]:27: warning: the result of 'f_InitGuard' must not outlive its object; keep it in a local declared after the object, a local, in the same block, and don't return, move or capture it [malterlib-coroutine-unsafe-this]
		return Destroy;
	}

	void fg_Keep(CAsyncDestroy &&_Destroy);
	TCUnsafeFuture<int> fg_MovesHolder()
	{
		CLaunches Launches;
		auto DestroyLaunches = co_await co_await Launches.f_Init();
// CHECK-MESSAGES: :[[@LINE-1]]:53: warning: the result of 'f_Init' must not outlive its object; keep it in a local declared after the object, a local, in the same block, and don't return, move or capture it [malterlib-coroutine-unsafe-this]
		fg_Keep(fg_Move(DestroyLaunches));
		co_return 0;
	}

	// Nor one whose data member is used, which can be moved out, nor one
	// awaited without being moved.
	int fg_UsesHolderField()
	{
		CLaunches Launches;
		auto Guard = Launches.f_InitGuard();
// CHECK-MESSAGES: :[[@LINE-1]]:25: warning: the result of 'f_InitGuard' must not outlive its object; keep it in a local declared after the object, a local, in the same block, and don't return, move or capture it [malterlib-coroutine-unsafe-this]
		return fg_Move(Guard.m_Value);
	}
	// Nor one moved into what another constructor makes, which can keep it.
	struct CEscapingAwaiter
	{
		CEscapingAwaiter(CAsyncDestroy &&_Destroy);
		bool await_ready() const noexcept;
		void await_suspend(std::coroutine_handle<>) const noexcept;
		void await_resume() noexcept;
	};
	TCUnsafeFuture<int> fg_ConvertsHolder()
	{
		CLaunches Launches;
		auto DestroyLaunches = co_await co_await Launches.f_Init();
// CHECK-MESSAGES: :[[@LINE-1]]:53: warning: the result of 'f_Init' must not outlive its object; keep it in a local declared after the object, a local, in the same block, and don't return, move or capture it [malterlib-coroutine-unsafe-this]
		co_await CEscapingAwaiter(fg_Move(DestroyLaunches));
		co_return 0;
	}
	// Nor a holder made by another constructor of the result.
	struct CGuardHolder
	{
		CGuardHolder(CGuardWithField &&_Guard);
	};
	void fg_ConstructsHolder()
	{
		CLaunches Launches;
		CGuardHolder Holder = Launches.f_InitGuard();
// CHECK-MESSAGES: :[[@LINE-1]]:34: warning: the result of 'f_InitGuard' must not outlive its object; keep it in a local declared after the object, a local, in the same block, and don't return, move or capture it [malterlib-coroutine-unsafe-this]
	}
	// Nor one that doesn't own it: a pointer, a reference to what isn't a
	// temporary it extends the life of, or what another constructor makes of
	// what an await gives.
	void fg_HoldsPointer()
	{
		CLaunches Launches;
		auto *pGuard = Launches.f_InitPointer();
// CHECK-MESSAGES: :[[@LINE-1]]:27: warning: the result of 'f_InitPointer' must not outlive its object; keep it in a local declared after the object, a local, in the same block, and don't return, move or capture it [malterlib-coroutine-unsafe-this]
	}
	// Also a reference to one, which extends the life of the pointer, not of
	// what it points to.
	void fg_HoldsPointerByReference()
	{
		CLaunches Launches;
		auto &&pGuard = Launches.f_InitPointer();
// CHECK-MESSAGES: :[[@LINE-1]]:28: warning: the result of 'f_InitPointer' must not outlive its object; keep it in a local declared after the object, a local, in the same block, and don't return, move or capture it [malterlib-coroutine-unsafe-this]
		pGuard->f_Clear();
	}
	// Nor one with a cleanup function, which is given its address where it
	// goes and can keep it.
	void fg_SaveGuard(CGuardWithField *_pGuard);
	void fg_HoldsWithCleanup()
	{
		CLaunches Launches;
		CGuardWithField Guard __attribute__((cleanup(fg_SaveGuard))) = Launches.f_InitGuard();
// CHECK-MESSAGES: :[[@LINE-1]]:75: warning: the result of 'f_InitGuard' must not outlive its object; keep it in a local declared after the object, a local, in the same block, and don't return, move or capture it [malterlib-coroutine-unsafe-this]
	}
	void fg_HoldsPointerByConstReference()
	{
		CLaunches Launches;
		CGuardWithField *const &pGuard = Launches.f_InitPointer();
// CHECK-MESSAGES: :[[@LINE-1]]:45: warning: the result of 'f_InitPointer' must not outlive its object; keep it in a local declared after the object, a local, in the same block, and don't return, move or capture it [malterlib-coroutine-unsafe-this]
		pGuard->f_Clear();
	}
	void fg_HoldsReference()
	{
		CLaunches Launches;
		auto &Guard = Launches.f_InitReference();
// CHECK-MESSAGES: :[[@LINE-1]]:26: warning: the result of 'f_InitReference' must not outlive its object; keep it in a local declared after the object, a local, in the same block, and don't return, move or capture it [malterlib-coroutine-unsafe-this]
		Guard.f_Clear();
	}
	struct CDestroyHolder
	{
		CDestroyHolder(CAsyncDestroy &&_Destroy);
	};
	TCUnsafeFuture<int> fg_ConvertsAwaited()
	{
		CLaunches Launches;
		CDestroyHolder Holder = co_await co_await Launches.f_Init();
// CHECK-MESSAGES: :[[@LINE-1]]:54: warning: the result of 'f_Init' must not outlive its object; keep it in a local declared after the object, a local, in the same block, and don't return, move or capture it [malterlib-coroutine-unsafe-this]
		co_return 0;
	}
	// But a reference to the temporary it extends the life of does.
	void fg_HoldsTemporary()
	{
		CLaunches Launches;
		auto &&Guard = Launches.f_InitGuard();
		Guard.f_Clear();
	}
	// Also one a move constructor makes of it, with default arguments too, and
	// a reference to the temporary it makes.
	void fg_MovesIntoHolder()
	{
		CLaunches Launches;
		auto Guard = CMovableGuard(fg_Move(Launches.f_InitMovable()));
		Guard.f_Clear();
		auto &&Extended = CGuardWithField(fg_Move(Launches.f_InitGuard()));
		Extended.f_Clear();
	}
	// But not a structured binding of it, whose bindings aren't followed.
	int fg_Decomposes()
	{
		CLaunches Launches;
		auto [Value] = Launches.f_InitGuard();
// CHECK-MESSAGES: :[[@LINE-1]]:27: warning: the result of 'f_InitGuard' must not outlive its object; keep it in a local declared after the object, a local, in the same block, and don't return, move or capture it [malterlib-coroutine-unsafe-this]
		return Value;
	}
	// Also one its type is made of it without a construction, `CGuard(...)`.
	void fg_ElidedHolder()
	{
		CLaunches Launches;
		auto Guard = CGuardWithField(Launches.f_InitGuard());
		Guard.f_Clear();
		auto BracedGuard = CGuardWithField{Launches.f_InitGuard()};
		BracedGuard.f_Clear();
	}
	// What awaiting a future of a guard gives is checked like the future: not
	// returned, but kept in a local that doesn't escape.
	TCUnsafeFuture<CAsyncDestroy> fg_ReturnsAwaitedFuture()
	{
		CLaunches Launches;
		auto Pending = co_await Launches.f_Init();
// CHECK-MESSAGES: :[[@LINE-1]]:36: warning: the result of 'f_Init' must not outlive its object; keep it in a local declared after the object, a local, in the same block, and don't return, move or capture it [malterlib-coroutine-unsafe-this]
		co_return co_await fg_Move(Pending);
	}
	// A future is only awaited directly, the call the operand of `co_await`:
	// one kept, also awaited later, in the body of an `if` without braces, is
	// reported.
	TCUnsafeFuture<int> fg_AwaitsInUnbracedIf(bool _bRun)
	{
		CLaunches Launches;
		auto Pending = Launches.f_Init();
// CHECK-MESSAGES: :[[@LINE-1]]:27: warning: the result of 'f_Init' must not outlive its object; keep it in a local declared after the object, a local, in the same block, and don't return, move or capture it [malterlib-coroutine-unsafe-this]
		if (_bRun)
			co_await fg_Move(Pending);
		co_return 0;
	}
	// Nor one awaited into a local that isn't automatic.
	TCUnsafeFuture<int> fg_AwaitsIntoStatic()
	{
		CLaunches Launches;
		auto Pending = co_await Launches.f_Init();
// CHECK-MESSAGES: :[[@LINE-1]]:36: warning: the result of 'f_Init' must not outlive its object; keep it in a local declared after the object, a local, in the same block, and don't return, move or capture it [malterlib-coroutine-unsafe-this]
		static auto DestroyAwaiter = co_await fg_Move(Pending);
		co_return 0;
	}
	// Nor a future that is never awaited, whose producer goes on.
	void fg_KeepsPendingFuture()
	{
		CLaunches Launches;
		auto Pending = Launches.f_Init();
// CHECK-MESSAGES: :[[@LINE-1]]:27: warning: the result of 'f_Init' must not outlive its object; keep it in a local declared after the object, a local, in the same block, and don't return, move or capture it [malterlib-coroutine-unsafe-this]
	}
	// Nor one kept and then awaited: there's no following it to the await.
	TCUnsafeFuture<int> fg_KeepsAwaitedFuture()
	{
		CLaunches Launches;
		auto Pending = Launches.f_Init();
// CHECK-MESSAGES: :[[@LINE-1]]:27: warning: the result of 'f_Init' must not outlive its object; keep it in a local declared after the object, a local, in the same block, and don't return, move or capture it [malterlib-coroutine-unsafe-this]
		auto DestroyAwaiter = co_await fg_Move(Pending);
		auto DestroyLaunches = co_await fg_Move(DestroyAwaiter);
		co_return 0;
	}
	// But a future awaited directly, in parentheses too, gives what the holder
	// rule checks: here kept in a local and awaited from it.
	TCUnsafeFuture<int> fg_AwaitsFutureDirectly()
	{
		CLaunches Launches;
		auto DestroyAwaiter = co_await (Launches.f_Init());
		auto DestroyLaunches = co_await fg_Move(DestroyAwaiter);
		co_return 0;
	}
	// A future is a type FutureTypes lists: one only awaitable, here through a
	// free `operator co_await`, isn't one, and is kept by the holder rule.
	TCUnsafeFuture<int> fg_AwaitsUnlistedAwaitable()
	{
		CLaunches Launches;
		auto DestroyLaunches = co_await co_await Launches.f_InitFree();
		auto Kept = Launches.f_InitFree();
		auto Awaited = co_await fg_Move(Kept);
		co_return 0;
	}
	// Also a future an awaiter that isn't one gives, awaited directly or
	// through a holder.
	TCUnsafeFuture<int> fg_AwaitsStagedFuture()
	{
		CLaunches Launches;
		auto DestroyLaunches = co_await co_await co_await co_await Launches.f_InitStaged();
		auto Pending = co_await co_await Launches.f_InitStaged();
// CHECK-MESSAGES: :[[@LINE-1]]:45: warning: the result of 'f_InitStaged' must not outlive its object; keep it in a local declared after the object, a local, in the same block, and don't return, move or capture it [malterlib-coroutine-unsafe-this]
		co_return 0;
	}
	TCUnsafeFuture<int> fg_AwaitsStagedHolder()
	{
		CLaunches Launches;
		auto Stage = co_await Launches.f_InitStaged();
// CHECK-MESSAGES: :[[@LINE-1]]:34: warning: the result of 'f_InitStaged' must not outlive its object; keep it in a local declared after the object, a local, in the same block, and don't return, move or capture it [malterlib-coroutine-unsafe-this]
		auto Pending = co_await fg_Move(Stage);
		co_return 0;
	}
	// What an await of a holder gives is followed like the result of the call:
	// a future it gives is awaited directly, also when it is discarded.
	TCUnsafeFuture<int> fg_DiscardsHolderFuture()
	{
		CLaunches Launches;
		auto Stage = Launches.f_InitStage();
// CHECK-MESSAGES: :[[@LINE-1]]:25: warning: the result of 'f_InitStage' must not outlive its object; keep it in a local declared after the object, a local, in the same block, and don't return, move or capture it [malterlib-coroutine-unsafe-this]
		co_await fg_Move(Stage);
		co_return 0;
	}
	TCUnsafeFuture<int> fg_AwaitsHolderFuture()
	{
		CLaunches Launches;
		auto Stage = Launches.f_InitStage();
		co_await co_await fg_Move(Stage);
		co_return 0;
	}
	// What the await of a holder gives is discarded also when it is cast to
	// void.
	TCUnsafeFuture<int> fg_DiscardsAwaitedHolderExplicitly()
	{
		CLaunches Launches;
		auto DestroyAwaiter = co_await (Launches.f_Init());
		(void)co_await fg_Move(DestroyAwaiter);
		auto StaticAwaiter = co_await (Launches.f_Init());
		static_cast<void>(co_await fg_Move(StaticAwaiter));
		auto FunctionalAwaiter = co_await (Launches.f_Init());
		void(co_await fg_Move(FunctionalAwaiter));
		co_return 0;
	}
	// Also in the init statement of an `if`, a `for` or a `switch`.
	TCUnsafeFuture<int> fg_DiscardsAwaitedHolderInInit(bool _bRun)
	{
		CLaunches Launches;
		auto IfAwaiter = co_await (Launches.f_Init());
		if (co_await fg_Move(IfAwaiter); _bRun)
			co_return 1;
		auto ForAwaiter = co_await (Launches.f_Init());
		for (co_await fg_Move(ForAwaiter); _bRun;)
			_bRun = false;
		auto SwitchAwaiter = co_await (Launches.f_Init());
		switch (co_await fg_Move(SwitchAwaiter); _bRun)
		{
		default:
			break;
		}
		co_return 0;
	}
	// But not a future it gives, which is awaited directly.
	TCUnsafeFuture<int> fg_DiscardsHolderFutureExplicitly()
	{
		CLaunches Launches;
		auto Stage = Launches.f_InitStage();
// CHECK-MESSAGES: :[[@LINE-1]]:25: warning: the result of 'f_InitStage' must not outlive its object; keep it in a local declared after the object, a local, in the same block, and don't return, move or capture it [malterlib-coroutine-unsafe-this]
		(void)co_await fg_Move(Stage);
		co_return 0;
	}
	// Also one on the way to a holder, moved instead of awaited directly.
	TCUnsafeFuture<int> fg_MovesFutureToHolder()
	{
		CLaunches Launches;
		auto Guard = co_await fg_Move(co_await fg_Move(Launches.f_InitStage()));
// CHECK-MESSAGES: :[[@LINE-1]]:59: warning: the result of 'f_InitStage' must not outlive its object; keep it in a local declared after the object, a local, in the same block, and don't return, move or capture it [malterlib-coroutine-unsafe-this]
		co_return 0;
	}
	// A future left when the awaits go deeper than followed is reported.
	TCUnsafeFuture<int> fg_AwaitsDeepFuture()
	{
		CLaunches Launches;
		auto Pending = co_await co_await co_await co_await co_await co_await co_await co_await co_await co_await co_await co_await co_await co_await co_await co_await Launches.f_InitDeep();
// CHECK-MESSAGES: :[[@LINE-1]]:171: warning: the result of 'f_InitDeep' must not outlive its object; keep it in a local declared after the object, a local, in the same block, and don't return, move or capture it [malterlib-coroutine-unsafe-this]
		co_return 0;
	}
	// While what an await gives is a future too, it is awaited directly.
	TCUnsafeFuture<int> fg_AwaitsNestedFuture()
	{
		CLaunches Launches;
		auto DestroyLaunches = co_await co_await co_await Launches.f_InitNested();
		auto Pending = co_await Launches.f_InitNested();
// CHECK-MESSAGES: :[[@LINE-1]]:36: warning: the result of 'f_InitNested' must not outlive its object; keep it in a local declared after the object, a local, in the same block, and don't return, move or capture it [malterlib-coroutine-unsafe-this]
		co_return 0;
	}
	// Also one initialized in braces, of its type.
	void fg_BracesHolder()
	{
		CLaunches Launches;
		CGuardWithField Guard{Launches.f_InitGuard()};
		Guard.f_Clear();
		auto AutoGuard{Launches.f_InitGuard()};
		AutoGuard.f_Clear();
	}
	// Not one of a base, which slices it.
	void fg_SlicesHolder()
	{
		CLaunches Launches;
		CBaseGuard Guard(Launches.f_InitDerived());
// CHECK-MESSAGES: :[[@LINE-1]]:29: warning: the result of 'f_InitDerived' must not outlive its object; keep it in a local declared after the object, a local, in the same block, and don't return, move or capture it [malterlib-coroutine-unsafe-this]
		Guard.f_Clear();
		auto Sliced = CBaseGuard(Launches.f_InitDerived());
// CHECK-MESSAGES: :[[@LINE-1]]:37: warning: the result of 'f_InitDerived' must not outlive its object; keep it in a local declared after the object, a local, in the same block, and don't return, move or capture it [malterlib-coroutine-unsafe-this]
		Sliced.f_Clear();
	}
	// Also through a member pointer that isn't changed.
	CGuardWithField fg_ReturnsThroughMemberPointer()
	{
		CLaunches Launches;
		auto fInit = &CLaunches::f_InitGuard;
		return (Launches.*fInit)();
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the result of 'f_InitGuard' must not outlive its object; keep it in a local declared after the object, a local, in the same block, and don't return, move or capture it [malterlib-coroutine-unsafe-this]
	}
	// Also one in parentheses, and not changed by what doesn't evaluate it.
	CGuardWithField fg_ReturnsThroughParenthesizedMemberPointer()
	{
		CLaunches Launches;
		auto fInit = &CLaunches::f_InitGuard;
		(void)sizeof(fInit);
		(void)noexcept(fInit);
		return (Launches.*(fInit))();
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the result of 'f_InitGuard' must not outlive its object; keep it in a local declared after the object, a local, in the same block, and don't return, move or capture it [malterlib-coroutine-unsafe-this]
	}
	// Each statement of the function once, also through nested awaits.
	TCFuture<int> fg_Pass(int _Value);
	TCUnsafeFuture<int> fg_NestedAwaitsMemberPointer()
	{
		CLaunches Launches;
		auto fInit = &CLaunches::f_InitGuard;
		auto Guard = (Launches.*fInit)();
		Guard.f_Clear();
		co_return co_await fg_Pass(co_await fg_Pass(co_await fg_Pass(co_await fg_Pass(co_await fg_Pass(co_await fg_Pass(co_await fg_Pass(co_await fg_Pass(co_await fg_Pass(co_await fg_Pass(co_await fg_Pass(co_await fg_Pass(co_await fg_Pass(co_await fg_Pass(co_await fg_Pass(co_await fg_Pass(co_await fg_Pass(co_await fg_Pass(co_await fg_Pass(co_await fg_Pass(co_await fg_Pass(co_await fg_Pass(co_await fg_Pass(co_await fg_Pass(co_await fg_Pass(co_await fg_Pass(co_await fg_Pass(co_await fg_Pass(co_await fg_Suspend()))))))))))))))))))))))))))));
	}
	// But not through a parameter, whose default is not what it is given.
	CGuardWithField fg_ReturnsThroughParameterPointer(CGuardWithField (CLaunches::*const _fInit)() = &CLaunches::f_InitGuard)
	{
		CLaunches Launches;
		return (Launches.*_fInit)();
	}
	// But not one any lambda in the function names, whatever it does with it:
	// one that captures a copy of it, also one that doesn't change it, also a
	// call in the lambda, through its copy, or one that captures it by
	// reference.
	CGuardWithField fg_ReturnsThroughCopiedMemberPointer()
	{
		CLaunches Launches;
		auto fInit = &CLaunches::f_InitGuard;
		auto fChange = [fInit]() mutable
			{
				fInit = nullptr;
				return fInit;
			}
		;
		(void)fChange();
		return (Launches.*fInit)();
	}
	CGuardWithField fg_CallsThroughChangedCopy()
	{
		auto fInit = &CLaunches::f_InitGuard;
		auto fCall = [fInit]() mutable
			{
				CLaunches Launches;
				fInit = &CLaunches::f_MakeGuard;
				return (Launches.*fInit)();
			}
		;
		return fCall();
	}
	CGuardWithField fg_CallsThroughCopy()
	{
		auto fInit = &CLaunches::f_InitGuard;
		auto fCall = [fInit]()
			{
				CLaunches Launches;
				return (Launches.*fInit)();
			}
		;
		return fCall();
	}
	CGuardWithField fg_ReturnsThroughReferencedMemberPointer()
	{
		CLaunches Launches;
		auto fInit = &CLaunches::f_InitGuard;
		auto fChange = [&fInit]()
			{
				fInit = nullptr;
			}
		;
		fChange();
		return (Launches.*fInit)();
	}
	// A call of a member function of the holder in parentheses is one too.
	void fg_ClearsParenthesizedHolder()
	{
		CLaunches Launches;
		auto Guard = Launches.f_InitGuard();
		(Guard).f_Clear();
	}
	// Also one assigned only in what a constant left side short-circuits.
	CGuardWithField fg_ReturnsThroughShortCircuitedReassign()
	{
		CLaunches Launches;
		auto fInit = &CLaunches::f_InitGuard;
		(void)(false && (fInit = &CLaunches::f_MakeGuard));
		(void)(true || (fInit = &CLaunches::f_MakeGuard));
		return (Launches.*fInit)();
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the result of 'f_InitGuard' must not outlive its object; keep it in a local declared after the object, a local, in the same block, and don't return, move or capture it [malterlib-coroutine-unsafe-this]
	}
	// Nor one that isn't a local of the function the call is in: global,
	// static or a member, nor a call in the body of a lambda.
	constexpr auto gc_fInitGuard = &CLaunches::f_InitGuard;
	struct CPointers
	{
		static constexpr auto msc_fInitGuard = &CLaunches::f_InitGuard;
	};
	CGuardWithField fg_ReturnsThroughNonLocalMemberPointer()
	{
		CLaunches Launches;
		static auto const fStatic = &CLaunches::f_InitGuard;
		(void)(Launches.*gc_fInitGuard)();
		(void)(Launches.*CPointers::msc_fInitGuard)();
		(void)(Launches.*fStatic)();
		auto fCall = []
			{
				CLaunches Inner;
				auto fInit = &CLaunches::f_InitGuard;
				return (Inner.*fInit)();
			}
		;
		(void)fCall;
		return (Launches.*fStatic)();
	}
	// Also one named in the signature of a lambda.
	CGuardWithField fg_ReturnsThroughMemberPointerInLambdaSignature()
	{
		CLaunches Launches;
		auto fInit = &CLaunches::f_InitGuard;
		auto fCheck = []() -> decltype(fInit)
			{
				return nullptr;
			}
		;
		(void)fCheck;
		return (Launches.*fInit)();
	}
	// Also one named in the template parameters of a lambda, also in a type.
	CGuardWithField fg_ReturnsThroughMemberPointerInLambdaTemplate()
	{
		CLaunches Launches;
		constexpr auto fInit = &CLaunches::f_InitGuard;
		auto fDefault = []<auto t_fTarget = fInit>()
			{
			}
		;
		auto fTyped = []<decltype(fInit) t_fTarget>()
			{
			}
		;
		(void)fDefault;
		(void)fTyped;
		return (Launches.*fInit)();
	}
	// Where a use and a lambda are spelled tells, not where a macro that
	// writes both is expanded: a use beside a lambda a macro writes is
	// still told, one in a lambda a macro writes isn't.
	// Told where the macro spells the call.
#define DCallBesideLambda(d_pMember, d_Object) ([] {}, (d_Object.*d_pMember)())
// CHECK-MESSAGES: :[[@LINE-1]]:56: warning: the result of 'f_InitGuard' must not outlive its object; keep it in a local declared after the object, a local, in the same block, and don't return, move or capture it [malterlib-coroutine-unsafe-this]
#define DCallInLambda(d_pMember)                                               \
	[&]                                                                        \
	{                                                                          \
		CLaunches Inner;                                                       \
		return (Inner.*d_pMember)();                                           \
	}
	CGuardWithField fg_ReturnsThroughMemberPointerBesideMacroLambda()
	{
		CLaunches Launches;
		auto fInit = &CLaunches::f_InitGuard;
		return DCallBesideLambda(fInit, Launches);
	}
	CGuardWithField fg_ReturnsThroughMemberPointerInMacroLambda()
	{
		CLaunches Launches;
		auto fInit = &CLaunches::f_InitGuard;
		auto fCall = DCallInLambda(fInit);
		(void)fCall;
		return (Launches.*fInit)();
	}
	// Also a const or constexpr one that a lambda names.
	CGuardWithField fg_ReturnsThroughConstMemberPointerInLambda()
	{
		CLaunches Launches;
		constexpr auto fInit = &CLaunches::f_InitGuard;
		auto const fConst = &CLaunches::f_InitGuard;
		auto fCall = [&]
			{
				CLaunches Inner;
				(void)(Inner.*fConst)();
				return (Inner.*fInit)();
			}
		;
		(void)fCall;
		(void)(Launches.*fConst)();
		return (Launches.*fInit)();
	}
	// Also one that a lambda in what isn't evaluated only reads.
	CGuardWithField fg_ReturnsThroughUnevaluatedLambdaRead()
	{
		CLaunches Launches;
		auto fInit = &CLaunches::f_InitGuard;
		using CReads = decltype
			(
				[&]
				{
					return fInit;
				}
			)
		;
		return (Launches.*fInit)();
	}
	// Nor one changed in the body of a lambda in what isn't evaluated, which runs
	// when it is called.
	CGuardWithField fg_ReturnsThroughUnevaluatedLambdaReassign()
	{
		CLaunches Launches;
		auto fInit = &CLaunches::f_InitGuard;
		using CChanges = decltype
			(
				[&]
				{
					fInit = &CLaunches::f_MakeGuard;
					CLaunches Inner;
					return (Inner.*fInit)();
				}
			)
		;
		return (Launches.*fInit)();
	}
	// But not one the right side of which can run.
	CGuardWithField fg_ReturnsThroughConditionalReassign(bool _bChange)
	{
		CLaunches Launches;
		auto fInit = &CLaunches::f_InitGuard;
		(void)(_bChange && (fInit = &CLaunches::f_MakeGuard));
		return (Launches.*fInit)();
	}
	// Also one assigned only in what never runs, a discarded `if constexpr`
	// branch.
	CGuardWithField fg_ReturnsThroughDiscardedReassign()
	{
		CLaunches Launches;
		auto fInit = &CLaunches::f_InitGuard;
		if constexpr (false)
			fInit = &CLaunches::f_InitGuard;
		return (Launches.*fInit)();
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the result of 'f_InitGuard' must not outlive its object; keep it in a local declared after the object, a local, in the same block, and don't return, move or capture it [malterlib-coroutine-unsafe-this]
	}
	// But not a call that never runs, which makes no result, in a branch a
	// constant condition never takes.
	CGuardWithField fg_ReturnsNeverTaken()
	{
		CLaunches Launches;
		if (false)
			return Launches.f_InitGuard();
		while (false)
			return Launches.f_InitGuard();
		return false ? Launches.f_InitGuard() : CGuardWithField{};
	}
	// Also one whose result has cleanups, or in parentheses.
	void fg_DiscardsNeverTaken()
	{
		CLaunches Launches;
		if (false)
			Launches.f_InitDestroying();
		if (false)
			(void)[Guard = Launches.f_InitDestroying()] {};
		(void)(false && (Launches.f_InitPointer() != nullptr));
		(void)(true || ((Launches.f_InitPointer())));
	}
	// But one that runs is reported.
	void fg_DiscardsTaken()
	{
		CLaunches Launches;
		if (true)
			Launches.f_InitDestroying();
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: the result of 'f_InitDestroying' must not outlive its object; keep it in a local declared after the object, a local, in the same block, and don't return, move or capture it [malterlib-coroutine-unsafe-this]
		if (true)
			(void)[Guard = Launches.f_InitDestroying()] {};
// CHECK-MESSAGES: :[[@LINE-1]]:28: warning: the result of 'f_InitDestroying' must not outlive its object; keep it in a local declared after the object, a local, in the same block, and don't return, move or capture it [malterlib-coroutine-unsafe-this]
	}
	// Also one initialized in braces.
	CGuardWithField fg_ReturnsThroughBracedMemberPointer()
	{
		CLaunches Launches;
		auto const fInit{&CLaunches::f_InitGuard};
		return (Launches.*fInit)();
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the result of 'f_InitGuard' must not outlive its object; keep it in a local declared after the object, a local, in the same block, and don't return, move or capture it [malterlib-coroutine-unsafe-this]
	}
	// The body of a lambda in what isn't evaluated, `decltype`, runs when the
	// lambda is called.
	using CRunsLater = decltype([] {
		CLaunches Launches;
		return Launches.f_InitGuard();
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: the result of 'f_InitGuard' must not outlive its object; keep it in a local declared after the object, a local, in the same block, and don't return, move or capture it [malterlib-coroutine-unsafe-this]
	});
	// But not what isn't evaluated in the body, as anywhere else, also
	// `__builtin_constant_p(...)`.
	using CRunsNone = decltype([] {
		CLaunches Launches;
		(void)sizeof(Launches.f_InitGuard());
		return __builtin_constant_p(Launches.f_InitGuard());
	});
	// Also through an explicit cast of its address.
	CGuardWithField fg_ReturnsThroughCastMemberPointer()
	{
		CLaunches Launches;
		auto const fInit = static_cast<CGuardWithField (CLaunches::*)()>(&CLaunches::f_InitGuard);
		return (Launches.*fInit)();
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the result of 'f_InitGuard' must not outlive its object; keep it in a local declared after the object, a local, in the same block, and don't return, move or capture it [malterlib-coroutine-unsafe-this]
	}
	// Also through a copy of a constant one, or the cast address itself.
	CGuardWithField fg_ReturnsThroughMemberPointerCopy()
	{
		CLaunches Launches;
		constexpr auto fInit = &CLaunches::f_InitGuard;
		constexpr auto fCopy = fInit;
		return (Launches.*fCopy)();
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the result of 'f_InitGuard' must not outlive its object; keep it in a local declared after the object, a local, in the same block, and don't return, move or capture it [malterlib-coroutine-unsafe-this]
	}
	CGuardWithField fg_ReturnsThroughCastAddress()
	{
		CLaunches Launches;
		return (Launches.*static_cast<CGuardWithField (CLaunches::*)()>(&CLaunches::f_InitGuard))();
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the result of 'f_InitGuard' must not outlive its object; keep it in a local declared after the object, a local, in the same block, and don't return, move or capture it [malterlib-coroutine-unsafe-this]
	}
	void fg_KeepsThroughMemberPointer()
	{
		CLaunches Launches;
		auto const fInit = &CLaunches::f_InitGuard;
		auto Guard = (Launches.*fInit)();
		Guard.f_Clear();
	}
	void fg_CallsHolderMember()
	{
		CLaunches Launches;
		auto Guard = Launches.f_InitGuard();
		Guard.f_Clear();
	}

	TCUnsafeFuture<int> fg_CapturesHolder()
	{
		CLaunches Launches;
		auto DestroyLaunches = co_await co_await Launches.f_Init();
// CHECK-MESSAGES: :[[@LINE-1]]:53: warning: the result of 'f_Init' must not outlive its object; keep it in a local declared after the object, a local, in the same block, and don't return, move or capture it [malterlib-coroutine-unsafe-this]
		fg_Submit([&] { DestroyLaunches.f_Clear(); });
		co_return 0;
	}

	// The calls of a template's instantiations, also dependent ones.
	template <typename t_CLaunches>
	TCUnsafeFuture<CAsyncDestroyAwaiter> fg_ReturnInit(t_CLaunches &_Launches)
	{
		return _Launches.f_Init();
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: the result of 'f_Init' must not outlive its object; keep it in a local declared after the object, a local, in the same block, and don't return, move or capture it [malterlib-coroutine-unsafe-this]
	}

	template <typename t_CLaunches>
	TCUnsafeFuture<int> fg_TemplateCallers()
	{
		t_CLaunches Launches;
		auto DestroyLaunches = co_await co_await Launches.f_Init();
		co_return 0;
	}

	void fg_Instantiate()
	{
		CLaunches Launches;
		(void)fg_ReturnInit(Launches);
		(void)fg_TemplateCallers<CLaunches>();
	}

	// Also a member operator.
	struct COperatorLaunches
	{
		DMibResultEndsBeforeObject TCUnsafeFuture<CAsyncDestroyAwaiter> operator()();
	};
	TCUnsafeFuture<CAsyncDestroyAwaiter> fg_ReturnsOperator()
	{
		COperatorLaunches Launches;
		return Launches();
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the result of 'operator()' must not outlive its object; keep it in a local declared after the object, a local, in the same block, and don't return, move or capture it [malterlib-coroutine-unsafe-this]
	}
	TCUnsafeFuture<int> fg_KeepsOperator()
	{
		COperatorLaunches Launches;
		auto DestroyLaunches = co_await co_await Launches();
		co_return 0;
	}

	// Not one that isn't evaluated, or never runs, which makes no result, also
	// what `__builtin_assume` is given.
	template <bool t_bRun>
	void fg_Unevaluated()
	{
		CLaunches Launches;
		__builtin_assume(Launches.f_InitPointer() != nullptr);
		using CResult = decltype(Launches.f_Init());
		(void)sizeof(Launches.f_Init());
		(void)noexcept(Launches.f_Init());
		(void)__datasizeof(Launches.f_InitGuard());
		(void)__builtin_classify_type(Launches.f_InitGuard());
		(void)__is_lvalue_expr(Launches.f_InitGuard());
		(void)__builtin_object_size(Launches.f_InitPointer(), 0);
		if constexpr (t_bRun)
			(void)Launches.f_Init();
	}
	template void fg_Unevaluated<false>();
	// Also the alignment of a variable length array, whose size is told at
	// run time, but not its alignment.
	void fg_UnevaluatedAlignment(int _nLength)
	{
		CLaunches Launches;
		int Values[_nLength];
		(void)__alignof__((Launches.f_InitGuard(), Values));
	}
}

// A copy of the object is its own, not the object, also when it shares what
// a shared pointer of it points to.
struct CCopiedState
{
	TCSharedPointer<int> m_pData;
	int m_Value = 0;

	TCFuture<int> f_Run()
	{
		CCopiedState Copy = *this;
		auto fRead = [pCopy = &Copy]() -> TCFuture<int>
			{
				co_await fg_Suspend();
				co_return pCopy->m_Value;
			}
		;
		int Value = co_await fg_CallSafe(fg_Move(fRead));
		co_return Value;
	}
};
