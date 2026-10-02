// RUN: %check_clang_tidy -std=c++20-or-later %s malterlib-continue-running-on-actor %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -I %S/Inputs

#include "malterlib-concurrency.h"

struct CInternal
{
	int m_Value = 0;
};

struct CMyActor;
void fg_Mutate(CMyActor *_pActor);

void fg_ClearHandle(TCActor<> &o_Actor);
void fg_ReplaceThrough(TCActor<> *_pActor);

struct CReplaceHandle
{
	void operator()(TCActor<> &o_Actor) const;
};
void fg_ReadHandle(TCActor<> const &_Actor);

struct CMyActor : CActor
{
	int &f_Value();

	TCFuture<int> f_Switch(bool _bSwitch)
	{
		auto &Internal = *mp_pInternal;
		int *pCopy = new int(m_Value);
		int Before = m_Value + Internal.m_Value;

		auto BlockingActorCheckout = fg_BlockingActor();
		if (_bSwitch)
			co_await fg_ContinueRunningOnActor(BlockingActorCheckout);

		int After = m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
		int Inner = Internal.m_Value + *pCopy;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
		f_Other();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]

		// Nothing keeps the actor alive meanwhile, so its handles, `self` and
		// making a handle to it, `fg_ThisActor(this)`, use the object too.
		auto Actor = m_OtherActor;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
		m_OtherActor = Actor;
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]

		m_OtherActor.f_Clear();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]

		co_await fg_ContinueRunningOnActor(fg_ThisActor(this));
// CHECK-MESSAGES: :[[@LINE-1]]:51: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
		int Back = m_Value;

		co_await fg_ContinueRunningOnActor(BlockingActorCheckout);
		co_await fg_ContinueRunningOnActor(self);
// CHECK-MESSAGES: :[[@LINE-1]]:38: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
		Back += m_Value;

		co_return Before + After + Inner + Back;
	}

	TCFuture<int> f_Lambda()
	{
		auto fLambda = [pThis = this, ThisActor = fg_ThisActor(this)]() -> TCFuture<int>
			{
				co_await fg_ContinueRunningOnActor(fg_BlockingActor());
				int Value = pThis->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
				co_await fg_ContinueRunningOnActor(ThisActor);
				co_return Value + pThis->m_Value;
			}
		;
		co_return 0;
	}

	template <typename tf_CActor>
	TCFuture<int> f_Template(tf_CActor _Other)
	{
		co_await fg_ContinueRunningOnActor(_Other);
		co_return m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}

	TCFuture<int> f_Operator()
	{
		co_await fg_ContinueRunningOnActor(fg_BlockingActor());
		this->operator()();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
		co_return 0;
	}

	TCFuture<int> f_Static()
	{
		co_await fg_ContinueRunningOnActor(fg_BlockingActor());
		co_return this->fs_Get() + this->mc_Value;
	}

	template <typename tf_CType>
	TCFuture<int> f_MemberTemplate()
	{
		co_await fg_ContinueRunningOnActor(fg_BlockingActor());
		this->template f_Work<tf_CType>();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
		co_return 0;
	}

	TCFuture<int> f_HandleReference()
	{
		auto const &ThisActor = self;
		co_await fg_ContinueRunningOnActor(fg_BlockingActor());
		co_await fg_ContinueRunningOnActor(ThisActor);
// CHECK-MESSAGES: :[[@LINE-1]]:38: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
		co_return m_Value;
	}

	TCFuture<int> f_PointerAlias()
	{
		auto *pThis = this;
		co_await fg_ContinueRunningOnActor(fg_BlockingActor());
		int Value = pThis->fs_Get() + pThis->mc_Value;
		co_await fg_ContinueRunningOnActor(fg_ThisActor(pThis));
// CHECK-MESSAGES: :[[@LINE-1]]:51: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
		Value += m_Value;
		co_await fg_ContinueRunningOnActor(fg_BlockingActor());
		co_await fg_ContinueRunningOnActor(pThis->self);
// CHECK-MESSAGES: :[[@LINE-1]]:38: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
		co_return Value + m_Value;
	}

	TCFuture<int> f_PassedThis()
	{
		auto ThisActor = fg_ThisActor(this);
		auto fMove = [ThisActor]() mutable
			{
				auto Moved = fg_Move(ThisActor);
			}
		;
		co_await fg_ContinueRunningOnActor(fg_BlockingActor());
		fg_Mutate(this);
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
		auto *pThis = this;
		co_await fg_ContinueRunningOnActor(ThisActor);
		co_return m_Value;
	}

	TCFuture<int> f_ReassignedPointer()
	{
		auto *pValue = &m_Value;
		pValue = new int(42);
		int *pLater = nullptr;
		pLater = &m_Value;
		co_await fg_ContinueRunningOnActor(fg_BlockingActor());
		co_return *pValue + *pLater;
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}

	TCFuture<int> f_ParenthesizedHandle()
	{
		co_await fg_ContinueRunningOnActor(fg_BlockingActor());
		(m_OtherActor).f_Clear();
// CHECK-MESSAGES: :[[@LINE-1]]:4: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
		co_return 0;
	}

	TCFuture<int> f_CapturedPointer()
	{
		auto *pThis = this;
		auto fLambda = [pThis]() -> TCFuture<int>
			{
				co_await fg_ContinueRunningOnActor(fg_BlockingActor());
				co_return pThis->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
			}
		;
		co_return 0;
	}

	TCFuture<int> f_CaptureAfterReassign()
	{
		// A capture copies where the pointer points when the lambda is made.
		int Local = 0;
		int *pValue = &Local;
		pValue = &m_Value;
		auto fInto = [pValue]() -> TCFuture<int>
			{
				co_await fg_ContinueRunningOnActor(fg_BlockingActor());
				co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
			}
		;
		int *pOther = &m_Value;
		pOther = &Local;
		auto fElsewhere = [pOther]() -> TCFuture<int>
			{
				co_await fg_ContinueRunningOnActor(fg_BlockingActor());
				co_return *pOther;
			}
		;
		co_return 0;
	}

	TCFuture<int> f_ChangedCopy()
	{
		// The lambda changes its copy of the handle, so switching to it isn't
		// switching back.
		auto ThisActor = fg_ThisActor(this);
		auto fLambda = [this, ThisActor]() mutable -> TCFuture<int>
			{
				co_await fg_ContinueRunningOnActor(fg_BlockingActor());
				ThisActor = m_OtherActor;
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
				co_await fg_ContinueRunningOnActor(ThisActor);
				co_return m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
			}
		;
		co_return 0;
	}

	TCFuture<int> f_Invoked()
	{
		// A lambda called right away runs on the other actor.
		co_await fg_ContinueRunningOnActor(fg_BlockingActor());
		co_return [this] { return m_Value; }();
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}

	TCFuture<int> f_NamedInvoked()
	{
		auto fRead = [this] { return m_Value; };
		co_await fg_ContinueRunningOnActor(fg_BlockingActor());
		co_return fRead();
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}

	TCFuture<int> f_AddressEscapes()
	{
		TCActor<> ThisActor = fg_ThisActor(this);
		fg_ReplaceThrough(&ThisActor);
		co_await fg_ContinueRunningOnActor(fg_BlockingActor());
		co_await fg_ContinueRunningOnActor(ThisActor);
		co_return m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}

	TCFuture<int> f_ReassignedCapture()
	{
		// A mutable lambda that points its capture elsewhere.
		auto fLambda = [pValue = &m_Value]() mutable -> TCFuture<int>
			{
				int Local = 0;
				pValue = &Local;
				co_await fg_ContinueRunningOnActor(fg_BlockingActor());
				co_return *pValue;
			}
		;
		co_return 0;
	}

	TCFuture<int> f_RedirectedAfterCapture()
	{
		// The lambda's copy still points into the object.
		int Local = 0;
		int *pValue = &m_Value;
		auto fRead = [pValue] { return *pValue; };
		pValue = &Local;
		co_await fg_ContinueRunningOnActor(fg_BlockingActor());
		co_return fRead() + *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}

	TCFuture<int> f_CallbackChanges()
	{
		TCActor<> ThisActor = fg_ThisActor(this);
		void (*fReplace)(TCActor<> &) = &fg_ClearHandle;
		fReplace(ThisActor);
		co_await fg_ContinueRunningOnActor(fg_BlockingActor());
		co_await fg_ContinueRunningOnActor(ThisActor);
		co_return m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}

	TCFuture<int> f_HandleThroughOperator()
	{
		co_await fg_ContinueRunningOnActor(fg_BlockingActor());
		CReplaceHandle{}(m_OtherActor);
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
		fg_ReplaceThrough(&m_OtherActor);
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
		co_return 0;
	}

	TCFuture<int> f_InvokedLocalAlias()
	{
		co_await fg_ContinueRunningOnActor(fg_BlockingActor());
		co_return [this] { auto *pThis = this; return pThis->m_Value; }();
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}

	TCFuture<int> f_InvokedNestedNotRun()
	{
		// A nested lambda that is not called doesn't run.
		co_await fg_ContinueRunningOnActor(fg_BlockingActor());
		co_return [this] { auto fUnused = [this] { return m_Value; }; return 0; }();
	}

	TCFuture<int> f_InvokedHandle()
	{
		// So does `self` inside a lambda called right away.
		co_await fg_ContinueRunningOnActor(fg_BlockingActor());
		auto ThisActor = [this] { return self; }();
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
		co_return 0;
	}

	TCFuture<int> f_ChangedCapturedAlias(CMyActor *_pOther)
	{
		auto *pThis = this;
		auto fLambda = [this, pThis, _pOther]() mutable -> TCFuture<int>
			{
				pThis = _pOther;
				co_await fg_ContinueRunningOnActor(fg_BlockingActor());
				co_await fg_ContinueRunningOnActor(pThis->self);
// CHECK-MESSAGES: :[[@LINE-1]]:40: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
				co_return m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
			}
		;
		co_return 0;
	}

	TCFuture<int> f_InvokedAlias()
	{
		auto fRead = [pThis = this] { return pThis->m_Value; };
		co_await fg_ContinueRunningOnActor(fg_BlockingActor());
		co_return fRead();
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}

	TCFuture<int> f_FunctorChanges()
	{
		TCActor<> ThisActor = fg_ThisActor(this);
		CReplaceHandle Replace;
		Replace(ThisActor);
		co_await fg_ContinueRunningOnActor(fg_BlockingActor());
		co_await fg_ContinueRunningOnActor(ThisActor);
		co_return m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}

	TCFuture<int> f_MutableReference()
	{
		co_await fg_ContinueRunningOnActor(fg_BlockingActor());
		fg_ClearHandle(m_OtherActor);
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
		fg_ReadHandle(m_OtherActor);
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
		co_return 0;
	}

	TCFuture<int> f_ChangedThroughAlias()
	{
		auto ThisActor = fg_ThisActor(this);
		auto &Alias = ThisActor;
		Alias = m_OtherActor;
		co_await fg_ContinueRunningOnActor(fg_BlockingActor());
		co_await fg_ContinueRunningOnActor(ThisActor);
		co_return m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}

	TCFuture<int> f_FlowAlias()
	{
		// A copy of a pointer that was assigned into the object.
		int *pFirst = nullptr;
		pFirst = &m_Value;
		int *pSecond = pFirst;

		// A copy of a pointer that was assigned elsewhere.
		int Local = 0;
		int *pThird = &m_Value;
		pThird = &Local;
		int *pFourth = pThird;

		co_await fg_ContinueRunningOnActor(fg_BlockingActor());
		int Value = *pFourth;
		co_return Value + *pSecond;
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}

	static int fs_Get();
	template <typename tf_CType>
	void f_Work();
	static constexpr int mc_Value = 1;
	void operator()();
	void f_Other();

	CInternal *mp_pInternal = nullptr;
	TCActor<> m_OtherActor;
	int m_Value = 0;
};
template TCFuture<int> CMyActor::f_Template<TCActor<>>(TCActor<> _Other);
template TCFuture<int> CMyActor::f_MemberTemplate<int>();

template <typename t_CBase>
struct TCDependentActor : CActor, t_CBase
{
	TCFuture<int> f_DependentBase()
	{
		co_await fg_ContinueRunningOnActor(fg_BlockingActor());
		this->f_Work();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
		co_return this->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}

	TCFuture<int> f_StaticThroughAlias()
	{
		// A static member function template called through an alias of this.
		auto *pThis = this;
		co_await fg_ContinueRunningOnActor(fg_BlockingActor());
		co_return pThis->template fs_Make<int>();
	}

	template <typename tf_CType>
	static int fs_Make();

	TCFuture<int> f_DependentSelf()
	{
		co_await fg_ContinueRunningOnActor(fg_BlockingActor());
		co_await fg_ContinueRunningOnActor(this->self);
// CHECK-MESSAGES: :[[@LINE-1]]:38: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
		co_return this->m_Value;
	}
};
struct CDependentActorBase
{
	void f_Work();
	int m_Value = 0;
};
template struct TCDependentActor<CDependentActorBase>;

// An object that is not an actor's has no actor to race with.
struct CNotActorHelper : CAllowUnsafeThis
{
	TCFuture<int> f_Switch()
	{
		co_await fg_ContinueRunningOnActor(fg_BlockingActor());
		co_return m_Value;
	}

	int m_Value = 0;
};

// A parameter that points or refers to the internals of an actor is used like
// `this`, like in a static function that moves its work to a blocking actor.
struct CStoreInternal : CActorInternal
{
	static TCFuture<int> fs_Cleanup(CStoreInternal *_pThis, CStoreInternal &_This)
	{
		int Prefix = _pThis->m_Prefix + _This.m_Prefix;

		co_await fg_ContinueRunningOnActor(fg_BlockingActor());

		int After = Prefix;
		After += _pThis->m_Prefix;
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
		After += _This.m_Prefix;
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
		co_return After;
	}

	int m_Prefix = 0;
};

TCFuture<int> fg_Cleanup(CStoreInternal *_pInternal, CInternal *_pOther)
{
	co_await fg_ContinueRunningOnActor(fg_BlockingActor());
	co_return _pInternal->m_Prefix + _pOther->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
}

// Switching back to the actor of the parameter makes it safe to use again,
// but reading its `self`, or making a handle to it, after switching away is a
// use of it, which can be gone by then.
struct CSwitchBack : CActorInternal
{
	static TCFuture<int> fs_SwitchBack(CMyActor *_pActor)
	{
		co_await fg_ContinueRunningOnActor(fg_BlockingActor());
		co_await fg_ContinueRunningOnActor(fg_ThisActor(_pActor));
// CHECK-MESSAGES: :[[@LINE-1]]:51: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
		co_return _pActor->m_Value;
	}

	static TCFuture<int> fs_SwitchBackSelf(CMyActor *_pActor)
	{
		co_await fg_ContinueRunningOnActor(fg_BlockingActor());
		co_await fg_ContinueRunningOnActor(_pActor->self);
// CHECK-MESSAGES: :[[@LINE-1]]:38: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
		co_return _pActor->m_Value;
	}
};

// Switching to the actor of a parameter doesn't switch back to the actor of
// the object, nor to that of another parameter.
struct COtherActor : CActor
{
	TCFuture<int> f_SwitchToOther(CMyActor *_pOther)
	{
		co_await fg_ContinueRunningOnActor(fg_ThisActor(_pOther));
		co_return m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}

	static TCFuture<int> fs_SwitchToFirst(CMyActor *_pFirst, CStoreInternal *_pSecond)
	{
		co_await fg_ContinueRunningOnActor(fg_ThisActor(_pFirst));
		co_return _pSecond->m_Prefix;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}

	int m_Value = 0;
};

// A parameter that is given another value may point to another actor.
CMyActor *fg_OtherActor();

TCFuture<int> fg_Reassigned(CMyActor *_pActor)
{
	CMyActor *pOriginal = _pActor;
	co_await fg_ContinueRunningOnActor(fg_BlockingActor());
	_pActor = fg_OtherActor();
	co_await fg_ContinueRunningOnActor(fg_ThisActor(_pActor));
// CHECK-MESSAGES: :[[@LINE-1]]:50: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	co_return pOriginal->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
}

// A parameter changed through a reference may point to another actor, but a
// reference parameter can't be changed.
void fg_Replace(CMyActor *&o_pActor);

TCFuture<int> fg_ReplacedThroughReference(CMyActor *_pActor)
{
	CMyActor *pOriginal = _pActor;
	fg_Replace(_pActor);
	co_await fg_ContinueRunningOnActor(fg_BlockingActor());
	co_await fg_ContinueRunningOnActor(fg_ThisActor(_pActor));
// CHECK-MESSAGES: :[[@LINE-1]]:50: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	co_return pOriginal->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
}

TCFuture<int> fg_ReferenceSwitchBack(CMyActor &_Actor)
{
	co_await fg_ContinueRunningOnActor(fg_BlockingActor());
	co_await fg_ContinueRunningOnActor(fg_ThisActor(&_Actor));
// CHECK-MESSAGES: :[[@LINE-1]]:51: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	co_return _Actor.m_Value;
}

// A parameter given another actor's object is still the object of an actor.
TCFuture<int> fg_ReassignedParameter(CMyActor *_pActor)
{
	co_await fg_ContinueRunningOnActor(fg_BlockingActor());
	_pActor = fg_OtherActor();
	co_return _pActor->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
}

// A lambda that captures the object of an actor uses it like `this`.
TCFuture<int> fg_CapturedActor(CMyActor *_pActor)
{
	co_return co_await fg_CallSafe
		(
			[_pActor]() -> TCFuture<int>
			{
				co_await fg_ContinueRunningOnActor(fg_BlockingActor());
				co_return _pActor->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
			}
		)
	;
}

// Switching back through an unchanged local handle; `self` and the handles of
// the object are uses of it after switching away, which nothing keeps alive.
TCFuture<int> fg_SwitchBackThroughHandle(CMyActor *_pActor)
{
	auto OwnActor = fg_ThisActor(_pActor);
	co_await fg_ContinueRunningOnActor(fg_BlockingActor());
	co_await fg_ContinueRunningOnActor(OwnActor);
	co_return _pActor->m_Value;
}

// With an actor of its own, the object of another actor in a parameter is
// used only before any switch.
struct CForeignUser : CActor
{
	TCFuture<int> f_UseOther(CMyActor *_pOther)
	{
		int Before = _pOther->m_Value;
		co_await fg_ContinueRunningOnActor(fg_BlockingActor());
		int After = _pOther->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
		co_await fg_ContinueRunningOnActor(fg_ThisActor(_pOther));
// CHECK-MESSAGES: :[[@LINE-1]]:51: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
		co_await fg_ContinueRunningOnActor(self);
// CHECK-MESSAGES: :[[@LINE-1]]:38: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
		co_return Before + After + _pOther->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:30: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}
};

// A lambda whose only actor object is its parameter switches back to it.
void fg_LambdaParameter()
{
	auto fRun = [](CMyActor *_pActor) -> TCFuture<int>
		{
			co_await fg_ContinueRunningOnActor(fg_BlockingActor());
			co_await fg_ContinueRunningOnActor(fg_ThisActor(_pActor));
// CHECK-MESSAGES: :[[@LINE-1]]:52: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
			co_return _pActor->m_Value;
		}
	;
}

// Aliases and other uses of another actor's object.
void fg_ReadActor(CMyActor &_Actor);

struct CForeignAliases : CActor
{
	TCFuture<int> f_Aliases(CMyActor *_pOther)
	{
		CMyActor *pAlias = nullptr;
		pAlias = _pOther;
		int &Value = _pOther->m_Value;
		co_await fg_ContinueRunningOnActor(fg_BlockingActor());
		fg_ReadActor(*_pOther);
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
		co_return pAlias->m_Value + Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
// CHECK-MESSAGES: :[[@LINE-2]]:31: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}
};

// Switching back through an alias of the parameter.
TCFuture<int> fg_SwitchBackThroughAlias(CMyActor *_pActor)
{
	auto *pAlias = _pActor;
	co_await fg_ContinueRunningOnActor(fg_BlockingActor());
	co_await fg_ContinueRunningOnActor(fg_ThisActor(pAlias));
// CHECK-MESSAGES: :[[@LINE-1]]:50: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	co_return _pActor->m_Value;
}

// A lambda with an alias of `this` has an actor of its own; the object of
// another actor it captures is used only before any switch.
struct COwnAndOther : CActor
{
	TCFuture<int> f_Run(CMyActor *_pOther)
	{
		co_return co_await fg_CallSafe
			(
				[pThis = this, _pOther]() -> TCFuture<int>
				{
					co_await fg_ContinueRunningOnActor(fg_BlockingActor());
					co_await fg_ContinueRunningOnActor(pThis->self);
// CHECK-MESSAGES: :[[@LINE-1]]:41: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
					co_return _pOther->m_Value + pThis->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
				}
			)
		;
	}

	int m_Value = 0;
};

// A lambda made in a member function of an actor has that actor, also when it
// only captures a pointer into the object; and what a member function of
// another actor's object returns a reference to is in that object.
struct CInterior : CActor
{
	TCFuture<int> f_Run(CMyActor *_pOther)
	{
		co_await fg_CallSafe
			(
				[pValue = &m_Value]() -> TCFuture<int>
				{
					co_await fg_ContinueRunningOnActor(fg_BlockingActor());
					co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
				}
			)
		;
		int &Value = _pOther->f_Value();
		co_await fg_ContinueRunningOnActor(fg_BlockingActor());
		co_return Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}

	int m_Value = 0;
};

// A pointer or reference into the object of an actor parameter that a lambda
// captures is followed to where the lambda is made, also through the captures
// of a lambda around it; with an actor of its own, the object of another actor
// is used only before any switch.
TCFuture<int> fg_CapturedInterior(CMyActor *_pActor, TCActor<> _Other)
{
	int *pValue = &_pActor->m_Value;
	int *pOwn = new int(0);
	co_await fg_CallSafe
		(
			[pValue, pOwn, _Other]() -> TCFuture<int>
			{
				co_await fg_ContinueRunningOnActor(_Other);
				co_return *pValue + *pOwn;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
			}
		)
	;
	co_return co_await fg_CallSafe
		(
			[_pActor]() -> TCFuture<int>
			{
				int &Value = _pActor->m_Value;
				co_return co_await fg_CallSafe
					(
						[&Value]() -> TCFuture<int>
						{
							co_await fg_ContinueRunningOnActor(fg_BlockingActor());
							co_return Value;
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
						}
					)
				;
			}
		)
	;
}

struct CForeignInterior : CActor
{
	TCFuture<int> f_Run(CMyActor *_pOther)
	{
		int *pOther = &_pOther->m_Value;
		co_return co_await fg_CallSafe
			(
				[pOther, this]() -> TCFuture<int>
				{
					int Before = *pOther;
					co_await fg_ContinueRunningOnActor(self);
					co_return Before + *pOther + m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:26: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
				}
			)
		;
	}

	// A lambda in a default member initializer is made by a constructor of
	// the actor.
	TCFunction<TCFuture<int> ()> m_fRun = [this]() -> TCFuture<int>
		{
			co_await fg_ContinueRunningOnActor(fg_BlockingActor());
			co_return m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
		}
	;

	int m_Value = 0;
};

// Init captures into the object of an actor parameter, captures through two
// lambdas, and a capture into another actor's object, which switching to the
// actor of the only parameter doesn't make safe.
TCFuture<int> fg_CapturedInitInterior(CMyActor *_pActor, CMyActor *_pOther)
{
	co_await fg_CallSafe
		(
			[pValue = &_pActor->m_Value, &Value = _pActor->m_Value]() -> TCFuture<int>
			{
				co_await fg_ContinueRunningOnActor(fg_BlockingActor());
				co_return *pValue + Value;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
// CHECK-MESSAGES: :[[@LINE-2]]:25: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
			}
		)
	;
	int *pValue = &_pActor->m_Value;
	co_await fg_CallSafe
		(
			[pValue]() -> TCFuture<int>
			{
				co_return co_await fg_CallSafe
					(
						[pValue]() -> TCFuture<int>
						{
							co_await fg_ContinueRunningOnActor(fg_BlockingActor());
							co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
						}
					)
				;
			}
		)
	;
	co_return co_await fg_CallSafe
		(
			[pValue](CMyActor *_pSwitchTo) -> TCFuture<int>
			{
				co_await fg_ContinueRunningOnActor(fg_ThisActor(_pSwitchTo));
				co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
			}
			, _pOther
		)
	;
}

// A recaptured init capture, a pointer from a lookup, a sole captured actor that
// is switched back to, an alias of `self` of the only actor parameter, and a
// reference to it.
struct CLookup
{
	int *operator () (int _Key);
};

struct CLookupActor : CActor
{
	CLookup m_Items;
	int m_Value = 0;
};

TCFuture<int> fg_RecapturedInit(CLookupActor *_pActor)
{
	co_await fg_CallSafe
		(
			[pValue = &_pActor->m_Value]() -> TCFuture<int>
			{
				co_return co_await fg_CallSafe
					(
						[pValue]() -> TCFuture<int>
						{
							co_await fg_ContinueRunningOnActor(fg_BlockingActor());
							co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
						}
					)
				;
			}
		)
	;
	co_await fg_CallSafe
		(
			[pItem = _pActor->m_Items(1)]() -> TCFuture<int>
			{
				co_await fg_ContinueRunningOnActor(fg_BlockingActor());
				co_return *pItem;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
			}
		)
	;
	co_return co_await fg_CallSafe
		(
			[_pActor]() -> TCFuture<int>
			{
				co_await fg_ContinueRunningOnActor(fg_BlockingActor());
				co_await fg_ContinueRunningOnActor(fg_ThisActor(_pActor));
// CHECK-MESSAGES: :[[@LINE-1]]:53: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
				co_return _pActor->m_Value;
			}
		)
	;
}

TCFuture<int> fg_SelfAlias(CLookupActor *_pActor)
{
	auto Self = _pActor->self;
	auto const &SelfReference = _pActor->self;
	co_await fg_ContinueRunningOnActor(fg_BlockingActor());
	auto Handle = SelfReference;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	co_await fg_ContinueRunningOnActor(Self);
	co_return _pActor->m_Value;
}

// Copying a pointer to the object into a lambda dispatched to its actor doesn't
// use the object.
TCFuture<int> fg_DispatchBack(CLookupActor *_pActor)
{
	co_await fg_ContinueRunningOnActor(fg_BlockingActor());
	co_return co_await (fg_ThisActor(_pActor) / [_pActor]() -> TCFuture<int> { co_return _pActor->m_Value; });
// CHECK-MESSAGES: :[[@LINE-1]]:35: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
}

// A pointer cast to a base of the object still points into it.
struct CDataBase
{
	int m_Data = 0;
};

struct CCastActor : CActor, CDataBase
{
};

TCFuture<int> fg_CastToBase(CCastActor *_pActor)
{
	auto *pData = static_cast<CDataBase *>(_pActor);
	co_await fg_ContinueRunningOnActor(fg_BlockingActor());
	co_return pData->m_Data;
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
}

TCFuture<int> fg_CastToInterior(CLookupActor *_pActor)
{
	auto *pValue = static_cast<int *>(&_pActor->m_Value);
	co_await fg_ContinueRunningOnActor(fg_BlockingActor());
	co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
}

// A template is checked through its instantiations with actors, and a sole
// captured actor pointer that a mutable lambda doesn't change is switched back
// to.
template <typename t_CActor>
TCFuture<int> fg_TemplateActor(t_CActor *_pActor)
{
	co_await fg_ContinueRunningOnActor(fg_BlockingActor());
	co_return _pActor->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
}

TCFuture<int> fg_MutableCapture(CLookupActor *_pActor)
{
	co_await fg_TemplateActor(_pActor);
	co_return co_await fg_CallSafe
		(
			[_pActor, Count = 0]() mutable -> TCFuture<int>
			{
				++Count;
				co_await fg_ContinueRunningOnActor(fg_BlockingActor());
				co_await fg_ContinueRunningOnActor(fg_ThisActor(_pActor));
// CHECK-MESSAGES: :[[@LINE-1]]:53: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
				co_return _pActor->m_Value + Count;
			}
		)
	;
}

// A lambda in a template that captures what can be an actor's object, a pointer
// captured by reference that points into the object later, and a copy of what
// a reference into the object refers to.
template <typename t_CActor>
TCFuture<int> fg_TemplateCapture(t_CActor *_pActor)
{
	co_return co_await fg_CallSafe
		(
			[_pActor]() -> TCFuture<int>
			{
				co_await fg_ContinueRunningOnActor(fg_BlockingActor());
				co_return _pActor->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
			}
		)
	;
}

int fg_LaterAssigned(CLookupActor *_pActor, TCActor<> const &_Actor)
{
	int *pValue = nullptr;
	auto fRun = [&pValue]() -> TCFuture<int>
		{
			co_await fg_ContinueRunningOnActor(fg_BlockingActor());
			co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
		}
	;
	pValue = &_pActor->m_Value;
	fg_TemplateCapture(_pActor);
	return (g_Dispatch(_Actor) / fRun).f_CallSync();
}

TCFuture<int> fg_CopiedThroughReference(CLookupActor *_pActor)
{
	int &Value = _pActor->m_Value;
	co_return co_await fg_CallSafe
		(
			[Value]() -> TCFuture<int>
			{
				co_await fg_ContinueRunningOnActor(fg_BlockingActor());
				co_return Value;
			}
		)
	;
}

// Switching back through a cast to a base, and a handle made in a lambda called
// right away.
TCFuture<int> fg_SwitchBackThroughBase(CLookupActor *_pActor)
{
	co_await fg_ContinueRunningOnActor(fg_BlockingActor());
	co_await fg_ContinueRunningOnActor(static_cast<CActor *>(_pActor)->self);
// CHECK-MESSAGES: :[[@LINE-1]]:37: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
// CHECK-MESSAGES: :[[@LINE-2]]:59: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	co_return _pActor->m_Value;
}

TCFuture<int> fg_HandleInLambda(CLookupActor *_pActor)
{
	co_await fg_ContinueRunningOnActor(fg_BlockingActor());
	auto Actor = [_pActor] { return fg_ThisActor(_pActor); }();
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	co_return 0;
}

// Reading the value of the pointer doesn't use the object, but copying what a
// reference into it refers to does.
TCFuture<int> fg_PointerValues(CLookupActor *_pActor)
{
	co_await fg_ContinueRunningOnActor(fg_BlockingActor());
	CLookupActor *pCopy = _pActor;
	if (!_pActor || _pActor == pCopy)
		co_return 0;
	co_await fg_ContinueRunningOnActor(fg_ThisActor(_pActor));
// CHECK-MESSAGES: :[[@LINE-1]]:50: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	co_return pCopy->m_Value;
}

struct CCaptureCopy : CActor
{
	TCFuture<int> f_Run()
	{
		int &Value = m_Value;
		co_await fg_ContinueRunningOnActor(fg_BlockingActor());
		auto fGet = [Value] { return Value; };
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
		co_await fg_ContinueRunningOnActor(self);
// CHECK-MESSAGES: :[[@LINE-1]]:38: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
		co_return fGet();
	}

	int m_Value = 0;
};

// A lambda called right away that copies what a reference refers to, and one
// run in place through fg_CallSafe.
TCFuture<int> fg_InvokedCopy(CLookupActor *_pActor)
{
	int &Value = _pActor->m_Value;
	auto fRead = [Value] { return Value; };
	co_await fg_ContinueRunningOnActor(fg_BlockingActor());
	co_return fRead();
}

struct CInPlace : CActor
{
	TCFuture<int> f_Run()
	{
		auto *pThis = this;
		co_await fg_ContinueRunningOnActor(fg_BlockingActor());
		co_return co_await fg_CallSafe
			(
				[pThis]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
				{
					co_return pThis->m_Value;
				}
			)
		;
	}

	int m_Value = 0;
};

// A lambda that switches back by itself runs where it is made until then.
struct CSwitchesBack : CActor
{
	TCFuture<int> f_Run(TCActor<> _Other)
	{
		co_await fg_ContinueRunningOnActor(_Other);
		co_return co_await fg_CallSafe
			(
				[this]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, {{.*}}
				{
					++m_Value;
					co_await fg_ContinueRunningOnActor(self);
					co_return m_Value;
				}
			)
		;
	}

	TCFuture<int> f_SwitchesFirst(TCActor<> _Other)
	{
		co_await fg_ContinueRunningOnActor(_Other);
		co_return co_await fg_CallSafe
			(
				[this]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
				{
					co_await fg_ContinueRunningOnActor(self);
					co_return ++m_Value;
				}
			)
		;
	}

	int m_Value = 0;
};

// A lambda held in a local runs where it is called, until it switches by
// itself.
struct CHeldLambda : CActor
{
	int m_Value = 0;
};

TCFuture<int> fg_HeldLambda(CHeldLambda *_pActor, TCActor<> _Other)
{
	auto fRead = [_pActor]() -> TCFuture<int>
		{
			co_return _pActor->m_Value;
		}
	;
	co_await fg_ContinueRunningOnActor(_Other);
	co_return co_await fg_CallSafe(fRead);
// CHECK-MESSAGES: :[[@LINE-1]]:21: warning: {{.*}}
}

TCFuture<int> fg_HeldSwitchesBack(CHeldLambda *_pActor, TCActor<> _Other)
{
	auto fRead = [_pActor]() -> TCUnsafeFuture<int>
		{
			co_await fg_ContinueRunningOnActor(_pActor->self);
			co_return _pActor->m_Value;
		}
	;
	co_await fg_ContinueRunningOnActor(_Other);
	co_return co_await fRead();
// CHECK-MESSAGES: :[[@LINE-1]]:21: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
}

// A name of a structured binding captured by reference refers into the object.
struct CBound
{
	int m_X;
	int m_Y;
};

struct CBindingActor : CActor
{
	CBound m_Data;
};

void fg_Binding(CBindingActor *_pActor, TCActor<> _Other)
{
	auto &[X, Y] = _pActor->m_Data;
	fg_CallSafe
		(
			[&X, _Other]() -> TCFuture<int>
			{
				co_await fg_ContinueRunningOnActor(_Other);
				co_return X;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, {{.*}}
			}
		)
	;
}

// A switch that a lambda makes only on some paths doesn't move the uses after
// it on the other paths.
struct CConditionalLambdaSwitch : CActor
{
	TCFuture<int> f_Run(bool _bBack)
	{
		co_await fg_ContinueRunningOnActor(fg_BlockingActor());
		co_return co_await fg_CallSafe
			(
				[this, _bBack]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' {{.*}}
				{
					if (_bBack)
						co_await fg_ContinueRunningOnActor(self);
					co_return m_Value;
				}
			)
		;
	}

	int m_Value = 0;
};

// Making the awaitable of a switch without awaiting it doesn't switch.
struct CUnawaitedSwitch : CActor
{
	TCFuture<int> f_Run(TCActor<> _Other)
	{
		co_await fg_ContinueRunningOnActor(_Other);
		co_return co_await fg_CallSafe
			(
				[this]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' {{.*}}
				{
					(void)fg_ContinueRunningOnActor(self);
					co_return m_Value;
				}
			)
		;
	}

	int m_Value = 0;
};

// A switch made before and kept in a local.
struct CStoredSwitch : CActor
{
	TCFuture<int> f_Run(TCActor<> _Other)
	{
		auto Switch = fg_ContinueRunningOnActor(_Other);
		co_await fg_Move(Switch);
		co_return ++m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' {{.*}}
	}

	int m_Value = 0;
};

// A stored switch that is assigned another may switch anywhere.
struct CReassignedSwitch : CActor
{
	TCFuture<int> f_Run(TCActor<> _Other)
	{
		auto Switch = fg_ContinueRunningOnActor(fg_ThisActor(this));
		Switch = fg_ContinueRunningOnActor(_Other);
		co_await fg_Move(Switch);
		co_return ++m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' {{.*}}
	}

	int m_Value = 0;
};

// What runs on the actor that the coroutine switched to: a scope exit, a
// lambda dispatched to the current actor, and switches through a conditional,
// a reference or a pointer to the function.
struct CEdgeSwitch : CActor
{
	int mp_Value = 0;
	TCActor<> mp_Handle;

	TCFuture<int> f_ScopeExit()
	{
		auto Cleanup = g_OnScopeExit / [this]
// CHECK-MESSAGES: :[[@LINE-1]]:34: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
			{
				mp_Value = 0;
			}
		;
		co_await fg_ContinueRunningOnActor(fg_BlockingActor());
		co_return 0;
	}

	TCFuture<int> f_Conditional(bool _bChoose, TCActor<> _ActorA, TCActor<> _ActorB)
	{
		co_await (_bChoose ? fg_ContinueRunningOnActor(_ActorA) : fg_ContinueRunningOnActor(_ActorB));
		mp_Value = 42;
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
		co_return 0;
	}

	TCFuture<int> f_DispatchToCurrent(TCActor<> _Other)
	{
		co_await fg_ContinueRunningOnActor(_Other);
		co_await (g_Dispatch / [this] { return ++mp_Value; });
// CHECK-MESSAGES: :[[@LINE-1]]:26: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
		co_return 0;
	}

	TCFuture<int> f_PointerAlias(TCActor<> _Other)
	{
		int *pValue = nullptr;
		int *&pAlias = pValue;
		pAlias = &mp_Value;
		co_await fg_ContinueRunningOnActor(_Other);
		co_return ++*pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}

	TCFuture<int> f_SwitchAlias(TCActor<> _Other)
	{
		auto Transfer = fg_ContinueRunningOnActor(_Other);
		auto &TransferAlias = Transfer;
		co_await TransferAlias;
		co_return ++mp_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}

	TCFuture<int> f_SwitchPointer(TCActor<> _Other)
	{
		auto fSwitch = static_cast<CCoroutineTransferOwnership (*)(TCActor<> const &)>(&fg_ContinueRunningOnActor);
		co_await fSwitch(_Other);
		co_return ++mp_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}

	TCFuture<int> f_HandleCast(TCActor<> _Other)
	{
		co_await fg_ContinueRunningOnActor(_Other);
		static_cast<TCActor<> &>(mp_Handle).f_Clear();
// CHECK-MESSAGES: :[[@LINE-1]]:28: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
		co_return 0;
	}
};

// A jump can pass the switch back of a lambda that runs where it is made.
struct CJumpPastSwitch : CActor
{
	int mp_Value = 0;

	TCFuture<int> f_Run(TCActor<> _Other)
	{
		co_await fg_ContinueRunningOnActor(_Other);
		co_await fg_CallSafe
			(
				[this]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
				{
					goto Use;
					co_await fg_ContinueRunningOnActor(fg_ThisActor(this));
				Use:
					co_return mp_Value;
				}
			)
		;
		co_return 0;
	}
};

// A transfer given as a parameter, a stored one that is exchanged, and what
// the arguments of a lambda's first switch do before it switches.
struct CTransferEdges : CActor
{
	int mp_Value = 0;

	TCFuture<int> f_GivenTransfer(CCoroutineTransferOwnership _Transfer)
	{
		co_await fg_Move(_Transfer);
		mp_Value = 1;
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
		co_return 0;
	}

	TCFuture<int> f_Exchanged(TCActor<> _Other)
	{
		auto Transfer = fg_ContinueRunningOnActor(fg_ThisActor(this));
		(void)fg_Exchange(Transfer, fg_ContinueRunningOnActor(_Other));
		co_await fg_Move(Transfer);
		co_return mp_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}

	TCFuture<int> f_SwitchArgument(TCActor<> _Other)
	{
		co_await fg_ContinueRunningOnActor(_Other);
		co_return co_await fg_CallSafe
			(
				[this]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
				{
					co_await fg_ContinueRunningOnActor((++mp_Value, fg_ThisActor(this)));
					co_return 0;
				}
			)
		;
	}
};

// A handle chosen by a conditional, changed.
struct CChosenHandle : CActor
{
	TCActor<> mp_First;
	TCActor<> mp_Second;

	TCFuture<int> f_Run(bool _bFirst)
	{
		co_await fg_ContinueRunningOnActor(fg_BlockingActor());
		(_bFirst ? mp_First : mp_Second).f_Clear();
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
// CHECK-MESSAGES: :[[@LINE-2]]:25: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
		co_return 0;
	}
};

// A pointer into the object through std::addressof, or set by fg_Exchange.
namespace std
{
	template <typename t_CType>
	t_CType *addressof(t_CType &_Value) noexcept;
}
struct CAddressActor : CActor
{
	int m_Value = 0;
	TCFuture<int> f_Address(TCActor<> _Other)
	{
		auto *pValue = std::addressof(m_Value);
		co_await fg_ContinueRunningOnActor(_Other);
		*pValue = 1;
// CHECK-MESSAGES: :[[@LINE-1]]:4: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
		co_return 0;
	}
	TCFuture<int> f_Exchange(TCActor<> _Other)
	{
		int *pValue = nullptr;
		fg_Exchange(pValue, &m_Value);
		co_await fg_ContinueRunningOnActor(_Other);
		*pValue = 1;
// CHECK-MESSAGES: :[[@LINE-1]]:4: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
		co_return 0;
	}
};

// The cleanup of fg_AsyncDestroy runs where its guard is destroyed, and a
// handle of the actor through (*this), which is a use of it.
struct CAsyncCleanupActor : CActor
{
	int m_Value = 0;
	TCFuture<int> f_Run(TCActor<> _Other)
	{
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
		co_await fg_ContinueRunningOnActor(_Other);
		co_return 0;
	}
	TCFuture<int> f_SwitchBack(TCActor<> _Other)
	{
		co_await fg_ContinueRunningOnActor(_Other);
		co_await fg_ContinueRunningOnActor((*this).self);
// CHECK-MESSAGES: :[[@LINE-1]]:39: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
		co_return m_Value;
	}
};

// An ordinary future assigned another one resumes on the actor it is awaited
// on.
TCFuture<int> fg_Suspend();
struct CReassignedFuture : CActor
{
	int m_Value = 0;
	TCFuture<int> f_Run()
	{
		auto Future = fg_Suspend();
		Future = fg_Suspend();
		co_await fg_Move(Future);
		m_Value = 1;
		co_return 0;
	}
};

// A member through a pointer to it.
struct CMemberPointerActor : CActor
{
	int m_Value = 0;
	TCFuture<int> f_Run(TCActor<> _Other)
	{
		int CMemberPointerActor::*pMember = &CMemberPointerActor::m_Value;
		int &Value = this->*pMember;
		co_await fg_ContinueRunningOnActor(_Other);
		Value = 1;
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
		co_return 0;
	}
};

// The last of a comma expression points into the object, a cast to a base to
// reach a static member doesn't use the object, and a scope exit cancelled
// before switching away doesn't run there.
struct CStaticBase
{
	static int ms_Value;
};
void fg_UseActor(CActor *_pActor);
struct CCastToBaseActor : CActor, CStaticBase
{
	int m_Value = 0;
	TCFuture<int> f_Comma(TCActor<> _Other)
	{
		int *pValue = (void(0), &m_Value);
		co_await fg_ContinueRunningOnActor(_Other);
		co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}

	TCFuture<int> f_StaticThroughCast(TCActor<> _Other)
	{
		co_await fg_ContinueRunningOnActor(_Other);
		co_return static_cast<CStaticBase *>(this)->ms_Value + static_cast<CCastToBaseActor *>(this)->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:58: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}

	TCFuture<int> f_PassedThroughCast(TCActor<> _Other)
	{
		co_await fg_ContinueRunningOnActor(_Other);
		fg_UseActor(static_cast<CActor *>(this));
// CHECK-MESSAGES: :[[@LINE-1]]:37: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
		co_return 0;
	}

	TCFuture<int> f_Cancelled(TCActor<> _Other)
	{
		auto Cleanup = g_OnScopeExit / [this]
			{
				m_Value = 0;
			}
		;
		Cleanup.f_Clear();
		co_await fg_ContinueRunningOnActor(_Other);
		co_return 0;
	}

	TCFuture<int> f_CancelledOnOnePath(TCActor<> _Other, bool _bCancel)
	{
		auto Cleanup = g_OnScopeExit / [this]
// CHECK-MESSAGES: :[[@LINE-1]]:34: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
			{
				m_Value = 0;
			}
		;
		if (_bCancel)
			Cleanup.f_ClearFunctor();
		co_await fg_ContinueRunningOnActor(_Other);
		co_return 0;
	}
};

// A lambda called through its call operator by name runs where it is called,
// and a static member reached through `this` isn't in the object.
struct CRound53Actor : CActor
{
	int m_Value = 0;
	static constexpr int mc_Value = 7;
	TCFuture<int> f_CalledByName(TCActor<> _Other)
	{
		auto fRead = [this] { return m_Value; };
		co_await fg_ContinueRunningOnActor(_Other);
		co_return fRead.operator()();
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}
	TCFuture<int> f_Static(TCActor<> _Other)
	{
		int const *pValue = &this->mc_Value;
		co_await fg_ContinueRunningOnActor(_Other);
		co_return *pValue;
	}
};

// A lambda that calls another one that uses the object, after a switch.
struct CRound55Nested : CActor
{
	int m_Value = 0;
	TCFuture<int> f_Run()
	{
		auto fRead = [this] { return m_Value; };
		auto fOuter = [&fRead] { return fRead(); };
		co_await fg_ContinueRunningOnActor(fg_BlockingActor());
		co_return fOuter();
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}
};

// g_OnScopeExitActor dispatches the cleanup to the actor it is registered on.
struct CRound55ScopeExitActor : CActor
{
	int m_Value = 0;
	TCFuture<int> f_Run(TCActor<> _Other)
	{
		auto Cleanup = g_OnScopeExitActor / [this] { ++m_Value; };
		co_await fg_ContinueRunningOnActor(_Other);
		co_return 0;
	}
	TCFuture<int> f_AfterSwitch(TCActor<> _Other)
	{
		co_await fg_ContinueRunningOnActor(_Other);
		auto Cleanup = g_OnScopeExitActor / [this] { ++m_Value; };
// CHECK-MESSAGES: :[[@LINE-1]]:39: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
		co_return 0;
	}
};

// The cleanup of fg_AsyncDestroy awaited early only on one path still runs
// where its guard is destroyed on the other.
struct CRound55ConditionalCleanup : CActor
{
	int m_Value = 0;
	TCFuture<int> f_Run(TCActor<> _Other, bool _bEarly)
	{
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
		if (_bEarly)
			co_await fg_Move(Cleanup);
		co_await fg_ContinueRunningOnActor(_Other);
		co_return 0;
	}
	TCFuture<int> f_Early(TCActor<> _Other)
	{
		auto Cleanup = co_await fg_AsyncDestroy
			(
				[this]() -> TCFuture<int>
				{
					m_Value = 1;
					co_return 0;
				}
			)
		;
		co_await fg_Move(Cleanup);
		co_await fg_ContinueRunningOnActor(_Other);
		co_return 0;
	}
};

// Running a scope exit early, `Cleanup()`, clears it, also through a
// reference to it.
struct CRound55ScopeExitRun : CActor
{
	int m_Value = 0;
	TCFuture<int> f_Run(TCActor<> _Other)
	{
		auto Cleanup = g_OnScopeExit / [this] { m_Value = 1; };
		Cleanup();
		co_await fg_ContinueRunningOnActor(_Other);
		co_return 0;
	}
	TCFuture<int> f_Alias(TCActor<> _Other)
	{
		auto Cleanup = g_OnScopeExit / [this] { m_Value = 1; };
		auto &Alias = Cleanup;
		Alias.f_Clear();
		co_await fg_ContinueRunningOnActor(_Other);
		co_return 0;
	}
};

// Clearing the guard of fg_AsyncDestroy cancels its cleanup.
struct CRound56ClearedCleanup : CActor
{
	int m_Value = 0;
	TCFuture<int> f_Run(TCActor<> _Other)
	{
		auto Cleanup = co_await fg_AsyncDestroy
			(
				[this]() -> TCFuture<int>
				{
					co_return m_Value;
				}
			)
		;
		Cleanup.f_Clear();
		co_await fg_ContinueRunningOnActor(_Other);
		co_return 0;
	}
};

// A guard given by reference may stay, and either guard of a conditional
// starts its cleanup where it is destroyed.
template <typename t_CType>
void fg_Round57Inspect(t_CType &_Value) noexcept;
struct CRound57Cleanup : CActor
{
	int m_Value = 0;
	TCFuture<int> f_Inspected()
	{
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
		fg_Round57Inspect(Cleanup);
		co_await fg_ContinueRunningOnActor(fg_BlockingActor());
		co_return 0;
	}
	TCFuture<int> f_Conditional(bool _bChoose)
	{
		auto Cleanup = _bChoose
			? co_await fg_AsyncDestroy
				(
					[]() -> TCFuture<int>
					{
						co_return 0;
					}
				)
			: co_await fg_AsyncDestroy
				(
					[this]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
					{
						m_Value = 1;
						co_return 0;
					}
				)
		;
		co_await fg_ContinueRunningOnActor(fg_BlockingActor());
		co_return 0;
	}
};

// A reference that fg_Const gives back, and swapped pointers.
template <typename t_CType>
t_CType const &fg_Round59Const(t_CType const &_Value) noexcept
{
	return _Value;
}
template <typename t_CType>
void fg_Swap(t_CType &_Left, t_CType &_Right);
struct CRound59Actor : CActor
{
	int m_Value = 0;
	TCFuture<int> f_Const(TCActor<> _Other)
	{
		auto const &Value = fg_Round59Const(m_Value);
		co_await fg_ContinueRunningOnActor(_Other);
		co_return Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}
	TCFuture<int> f_Swapped(TCActor<> _Other)
	{
		int Local = 0;
		int *pA = &m_Value;
		int *pB = &Local;
		fg_Swap(pA, pB);
		co_await fg_ContinueRunningOnActor(_Other);
		int Result = *pA;
		co_return Result + *pB;
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}
};

// A member function called after a switch in its arguments runs on the actor
// switched to, and awaiting a transfer back, also moved, doesn't change where
// it goes.
struct CRound62Actor : CActor
{
	int m_Value = 0;
	void f_Update(int _Value)
	{
		m_Value = _Value;
	}

	TCFuture<int> f_InArgument()
	{
		f_Update((co_await fg_ContinueRunningOnActor(fg_BlockingActor()), 1));
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
		co_return 0;
	}

	TCFuture<int> f_Back()
	{
		auto Back = fg_ContinueRunningOnActor(self);
		co_await fg_ContinueRunningOnActor(fg_BlockingActor());
		co_await Back;
		co_return m_Value;
	}

	TCFuture<int> f_MovedBack()
	{
		auto Back = fg_ContinueRunningOnActor(self);
		co_await fg_ContinueRunningOnActor(fg_BlockingActor());
		co_await fg_Move(Back);
		co_return m_Value;
	}
};

// A member function of a member called after a switch in its arguments, a
// stored transfer changed through a reference to it, and a branch of an `if
// constexpr` in a called lambda that is discarded.
struct CRound63Value
{
	int m_Value = 0;
	int f_Read(int _Offset) const
	{
		return m_Value + _Offset;
	}
};
struct CRound63Actor : CActor
{
	CRound63Value m_Value;
	int m_Count = 0;

	TCFuture<int> f_Member(TCActor<> _Other)
	{
		co_return m_Value.f_Read((co_await fg_ContinueRunningOnActor(_Other), 1));
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}

	TCFuture<int> f_Alias()
	{
		auto Back = fg_ContinueRunningOnActor(self);
		auto &Alias = Back;
		Alias = fg_ContinueRunningOnActor(fg_BlockingActor());
		co_await fg_ContinueRunningOnActor(fg_BlockingActor());
		co_await fg_Move(Back);
		co_return m_Count;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}

	TCFuture<int> f_Discarded()
	{
		co_await fg_ContinueRunningOnActor(fg_BlockingActor());
		co_return co_await fg_CallSafe
			(
				[this]() -> TCFuture<int>
				{
					if constexpr (false)
						co_return m_Count;
					co_return 0;
				}
			)
		;
	}
};

// What a call whose arguments switch is given by reference, and a lookup it
// is made on, are used after the switch.
int fg_Round65Read(int const &_Value, int _Other);
struct CRound65Value
{
	int f_Read(int _Offset);
};
struct CRound65Values
{
	CRound65Value &operator[](int _Key);
};
struct CRound65Actor : CActor
{
	int m_Value = 0;
	CRound65Values mp_Items;

	TCFuture<int> f_Argument(TCActor<> _Other)
	{
		auto &Value = m_Value;
		co_return fg_Round65Read(Value, (co_await fg_ContinueRunningOnActor(_Other), 0));
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}

	TCFuture<int> f_Lookup(TCActor<> _Other)
	{
		co_return mp_Items[0].f_Read((co_await fg_ContinueRunningOnActor(_Other), 1));
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}
};

// What an exchange returns is the pointer it replaces, into the object.
struct CRound66Actor : CActor
{
	int m_Value;

	TCFuture<int> f_Exchanged()
	{
		auto *pOriginal = &m_Value;
		auto *pValue = fg_Exchange(pOriginal, nullptr);
		auto Other = fg_BlockingActor();
		co_await fg_ContinueRunningOnActor(Other);
		co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}
};

// A call through a pointer to a member function whose arguments switch uses
// the object after the switch.
struct CRound67Actor : CActor
{
	int m_Value = 0;
	int f_Read(int _Offset) const
	{
		return m_Value + _Offset;
	}

	TCFuture<int> f_Run()
	{
		auto fRead = &CRound67Actor::f_Read;
		co_return (this->*fRead)((co_await fg_ContinueRunningOnActor(fg_BlockingActor()), 0));
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}
};

// A call through a pointer to a function, or a constructor, whose other
// arguments switch uses what it is given by reference after the switch.
struct CRound69Copy
{
	int m_Value;
	CRound69Copy(int const &_Value, int _Extra)
		: m_Value(_Value + _Extra)
	{
	}
};
struct CRound69Actor : CActor
{
	int m_Value = 0;

	TCFuture<int> f_Pointer(void (*_fUse)(int &_Value, int _Extra), TCActor<> _Other)
	{
		_fUse(m_Value, (co_await fg_ContinueRunningOnActor(_Other), 0));
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
		co_return 0;
	}

	TCFuture<int> f_Construct(TCActor<> _Other)
	{
		CRound69Copy Copy{m_Value, (co_await fg_ContinueRunningOnActor(_Other), 0)};
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
		co_return 0;
	}
};

// A scope exit guard moved into another one runs its callback where that one
// is destroyed.
struct CRound69Cleanup : CActor
{
	int m_Value = 0;

	TCFuture<int> f_Moved()
	{
		auto Own = fg_ThisActor(this);
		auto Cleanup = g_OnScopeExit / [this]
// CHECK-MESSAGES: :[[@LINE-1]]:34: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
			{
				++m_Value;
			}
		;
		{
			auto Moved = fg_Move(Cleanup);
			co_await fg_ContinueRunningOnActor(fg_BlockingActor());
		}
		co_await fg_ContinueRunningOnActor(Own);
		co_return 0;
	}
};

// Switching back through a moved handle of the own actor is switching back.
struct CRound70Back : CActor
{
	int m_Value = 0;

	TCFuture<int> f_Back()
	{
		auto ThisActor = fg_ThisActor(this);
		co_await fg_ContinueRunningOnActor(fg_BlockingActor());
		co_await fg_ContinueRunningOnActor(fg_Move(ThisActor));
		co_return m_Value;
	}
};

// A stored switch assigned through a pointer to it may switch elsewhere, and
// what an assignment assigns is what the value is.
struct CRound71Actor : CActor
{
	int m_Value = 0;

	TCFuture<int> f_Pointer()
	{
		auto Back = fg_ContinueRunningOnActor(fg_ThisActor(this));
		auto *pBack = &Back;
		*pBack = fg_ContinueRunningOnActor(fg_BlockingActor());
		co_await fg_Move(Back);
		co_return m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}

	TCFuture<int> f_Chained(TCActor<> _Other)
	{
		int *pFirst = nullptr;
		int *pSecond = nullptr;
		pSecond = (pFirst = &m_Value);
		co_await fg_ContinueRunningOnActor(_Other);
		co_return *pSecond;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}
};

// A handle assigned through a cast to a reference is changed, and one assigned
// only where it doesn't run isn't.
struct CRound75Handles : CActor
{
	int m_Value;

	TCFuture<int> f_Cast(TCActor<CRound75Handles> _Other)
	{
		auto ThisActor = fg_ThisActor(this);
		static_cast<decltype(ThisActor) &>(ThisActor) = _Other;
		co_await fg_ContinueRunningOnActor(ThisActor);
		co_return m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}

	TCFuture<int> f_NotRun(TCActor<CRound75Handles> _Other)
	{
		auto ThisActor = fg_ThisActor(this);
		(void)sizeof(ThisActor = _Other);
		if constexpr (false)
			ThisActor = _Other;
		co_await fg_ContinueRunningOnActor(ThisActor);
		co_return m_Value;
	}
};

// A stored switch assigned through a cast to a reference, or only where it
// doesn't run, and lambdas that forward to one that uses the object.
struct CRound76Actor : CActor
{
	int m_Value = 0;

	TCFuture<int> f_Cast(TCActor<> _Other)
	{
		auto Own = fg_ThisActor(this);
		auto Back = fg_ContinueRunningOnActor(Own);
		static_cast<decltype(Back) &>(Back) = fg_ContinueRunningOnActor(_Other);
		co_await fg_Move(Back);
		co_return m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}

	TCFuture<int> f_Discarded(TCActor<> _Other)
	{
		auto Own = fg_ThisActor(this);
		auto Back = fg_ContinueRunningOnActor(Own);
		if constexpr (false)
			Back = fg_ContinueRunningOnActor(_Other);
		co_await fg_Move(Back);
		co_return m_Value;
	}

	TCFuture<int> f_Forwarding(TCActor<> _Other)
	{
		auto fRead = [this] { return m_Value; };
		auto fA = [&] { return fRead(); };
		auto fB = [&] { return fA(); };
		auto fC = [&] { return fB(); };
		auto fD = [&] { return fC(); };
		auto fE = [&] { return fD(); };
		co_await fg_ContinueRunningOnActor(_Other);
		co_return fE();
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}
};

// The entries of a range-for over the data of the actor are of the actor,
// also those that can't be removed.
struct CRound77Range : CActor
{
	int m_Values[2];

	TCFuture<int> f_Run(TCActor<> _Other)
	{
		for (int &Value : m_Values)
		{
			co_await fg_ContinueRunningOnActor(_Other);
			Value = 42;
// CHECK-MESSAGES: :[[@LINE-1]]:4: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
			co_return 0;
		}
		co_return 0;
	}
};

// The captures of lambdas nested in each other are each looked at once.
struct CRound78Nested : CActor
{
	int m_Value;

	void f_Run()
	{
		int *p0 = &m_Value;
		int *p1 = &m_Value;
		int *p2 = &m_Value;
		int *p3 = &m_Value;
		int *p4 = &m_Value;
		auto f0 = [p0, p1, p2, p3, p4]
			{
				auto f1 = [p0, p1, p2, p3, p4]
					{
						auto f2 = [p0, p1, p2, p3, p4]
							{
								auto f3 = [p0, p1, p2, p3, p4]
									{
										auto f4 = [p0, p1, p2, p3, p4]
											{
												auto f5 = [p0, p1, p2, p3, p4]
													{
														auto f6 = [p0, p1, p2, p3, p4]
															{
																auto f7 = [p0, p1, p2, p3, p4]() -> TCFuture<int>
																	{
																		co_await fg_ContinueRunningOnActor(fg_BlockingActor());
																		co_return *p0 + *p1 + *p2 + *p3 + *p4;
// CHECK-MESSAGES: :[[@LINE-1]]:30: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
// CHECK-MESSAGES: :[[@LINE-2]]:36: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
// CHECK-MESSAGES: :[[@LINE-3]]:42: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
// CHECK-MESSAGES: :[[@LINE-4]]:48: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
// CHECK-MESSAGES: :[[@LINE-5]]:54: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
																	}
																;
															}
														;
													}
												;
											}
										;
									}
								;
							}
						;
					}
				;
			}
		;
	}
};

// Pointed into the object through a cast to a reference.
struct CRound80Cast : CActor
{
	int m_Value;

	TCFuture<int> f_Run()
	{
		int *pValue = nullptr;
		static_cast<int *&>(pValue) = &m_Value;
		co_await fg_ContinueRunningOnActor(fg_BlockingActor());
		co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}
};

// A transfer back to the own actor kept in a local made from a conditional.
struct CRound81Back : CActor
{
	int m_Value;

	TCFuture<int> f_Run(bool _bChoose)
	{
		TCActor<> Back = self;
		auto Transfer = _bChoose ? fg_ContinueRunningOnActor(Back) : fg_ContinueRunningOnActor(Back);
		co_await fg_ContinueRunningOnActor(fg_BlockingActor());
		co_await fg_Move(Transfer);
		co_return m_Value;
	}
};

// A scope exit that is a discarded temporary runs its lambda where it is
// made.
struct CRound82Temporary : CActor
{
	int m_Value;

	TCFuture<int> f_Race(TCActor<> _Other)
	{
		co_await fg_ContinueRunningOnActor(_Other);
		(void)(g_OnScopeExit / [this]
// CHECK-MESSAGES: :[[@LINE-1]]:26: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
			{
				m_Value = 1;
			})
		;
		co_return 0;
	}
};

// A pointer to the object assigned to either of a conditional is in both.
struct CRound84Either : CActor
{
	int m_Value;

	TCFuture<int> f_Run(TCActor<> _Other, bool _bFirst)
	{
		int *pFirst = nullptr;
		int *pSecond = nullptr;
		(_bFirst ? pFirst : pSecond) = &m_Value;
		co_await fg_ContinueRunningOnActor(_Other);
		co_return _bFirst ? *pFirst : *pSecond;
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
// CHECK-MESSAGES: :[[@LINE-2]]:34: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}
};

// What a pointer to the object points to accessed after a switch in the same
// expression.
struct CRound85Subscript : CActor
{
	int m_Value;

	TCFuture<int> f_Run(TCActor<> _Other)
	{
		int *pValue = &m_Value;
		co_return pValue[(co_await fg_ContinueRunningOnActor(_Other), 0)];
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}
};

// A lambda called assigns what it captures by reference; a swap through a
// cast to a reference swaps where the pointers point; and a lambda that only
// clears its copy of a pointer doesn't use what it points to.
struct CRound87Actor : CActor
{
	int m_Value = 0;

	TCFuture<int> f_Lambda()
	{
		int *pValue = nullptr;
		auto fInit = [&pValue, this] { pValue = &m_Value; };
		fInit();
		co_await fg_ContinueRunningOnActor(fg_BlockingActor());
		co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}

	TCFuture<int> f_Swap()
	{
		int Local = 0;
		int *pValue = &m_Value;
		int *pLocal = &Local;
		fg_Swap(static_cast<int *&>(pValue), pLocal);
		co_await fg_ContinueRunningOnActor(fg_BlockingActor());
		co_return *pLocal;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}

	TCFuture<int> f_Clear()
	{
		int *pValue = &m_Value;
		auto fClear = [pValue]() mutable { pValue = nullptr; };
		co_await fg_ContinueRunningOnActor(fg_BlockingActor());
		fClear();
		co_return 0;
	}
};

// What a coroutine lambda that `fg_CallSafe` runs assigns.
struct CRound91Actor : CActor
{
	int m_Value;
	TCFuture<int> f_Run()
	{
		int *pValue = nullptr;
		co_await fg_CallSafe
			(
				[&]() -> TCFuture<int>
				{
					pValue = &m_Value;
					co_return 0;
				}
			)
		;
		co_await fg_ContinueRunningOnActor(fg_BlockingActor());
		co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}
};

// What a lambda called where what it throws is caught assigns after a call
// that can throw may not be assigned, and one that points its own copy
// elsewhere before reading through it reads that.
void fg_Round92MayThrow();
struct CRound92Actor : CActor
{
	int m_Value;

	TCFuture<int> f_Caught(TCActor<> _Other)
	{
		int Local = 1;
		int *pValue = &m_Value;
		auto fRedirect = [&pValue, &Local]
			{
				fg_Round92MayThrow();
				pValue = &Local;
			}
		;
		co_await fg_ContinueRunningOnActor(_Other);
		try
		{
			fRedirect();
		}
		catch (...)
		{
		}
		co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}

	TCFuture<int> f_Elsewhere(TCActor<> _Other)
	{
		int Local = 1;
		int *pValue = &m_Value;
		auto fRead = [pValue, &Local]() mutable
			{
				pValue = &Local;
				return *pValue;
			}
		;
		co_await fg_ContinueRunningOnActor(_Other);
		co_return fRead();
	}
};

// What a helper returns of its argument, and only the branch a constant
// condition chooses.
int g_Round93Global;
int *fg_Round93Identity(int *_pValue)
{
	return _pValue;
}

struct CRound93Switched : public CActor
{
	int m_Value;

	TCFuture<int> f_Identity(TCActor<> _Other)
	{
		int *pValue = fg_Round93Identity(&m_Value);
		co_await fg_ContinueRunningOnActor(_Other);
		co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}

	TCFuture<int> f_ConstantCondition(TCActor<> _Other)
	{
		int *pValue = false ? &m_Value : &g_Round93Global;
		co_await fg_ContinueRunningOnActor(_Other);
		co_return *pValue;
	}
};

// A local pointer assigned through a pointer to it.
struct CRound95Indirect : CActor
{
	int m_Value;
	TCFuture<int> f_Run()
	{
		int *pValue = nullptr;
		int **ppValue = &pValue;
		*ppValue = &m_Value;
		co_await fg_ContinueRunningOnActor(fg_BlockingActor());
		co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}
};

// A pointer given a unary plus is the same, and a scope exit moved from is
// cancelled with the one moved to.
struct CRound96Actor : CActor
{
	int m_Value;

	TCFuture<int> f_Plus(TCActor<> _Other)
	{
		int *pValue = +&m_Value;
		co_await fg_ContinueRunningOnActor(_Other);
		co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}

	TCFuture<int> f_MovedGuard()
	{
		auto Cleanup = g_OnScopeExit / [this]
			{
				++m_Value;
			}
		;
		auto Moved = fg_Move(Cleanup);
		Moved.f_Clear();
		co_await fg_ContinueRunningOnActor(fg_BlockingActor());
		co_return 0;
	}
};

// An ordinary future reached through many references doesn't switch.
TCFuture<int> fg_Round97Suspend();

struct CRound97Actor : CActor
{
	int m_Value;

	TCFuture<int> f_Run()
	{
		co_await fg_ContinueRunningOnActor(fg_ThisActor(this));
		auto Future = fg_Round97Suspend();
		auto &r1 = Future, &r2 = r1, &r3 = r2, &r4 = r3, &r5 = r4, &r6 = r5, &r7 = r6, &r8 = r7, &r9 = r8;
		co_await fg_Move(r9);
		co_return m_Value;
	}
};

// A saved switch reached through many conditionals of the same one is told
// once each.
struct CRound99Chain : CActor
{
	int m_Value;

	TCFuture<int> f_Run(bool _bChoose)
	{
		auto Other = fg_BlockingActor();
		auto Switch0 = fg_ContinueRunningOnActor(Other);
		auto &&Switch1 = _bChoose ? Switch0 : Switch0;
		auto &&Switch2 = _bChoose ? Switch1 : Switch1;
		auto &&Switch3 = _bChoose ? Switch2 : Switch2;
		auto &&Switch4 = _bChoose ? Switch3 : Switch3;
		auto &&Switch5 = _bChoose ? Switch4 : Switch4;
		auto &&Switch6 = _bChoose ? Switch5 : Switch5;
		auto &&Switch7 = _bChoose ? Switch6 : Switch6;
		auto &&Switch8 = _bChoose ? Switch7 : Switch7;
		auto &&Switch9 = _bChoose ? Switch8 : Switch8;
		auto &&Switch10 = _bChoose ? Switch9 : Switch9;
		auto &&Switch11 = _bChoose ? Switch10 : Switch10;
		auto &&Switch12 = _bChoose ? Switch11 : Switch11;
		auto &&Switch13 = _bChoose ? Switch12 : Switch12;
		auto &&Switch14 = _bChoose ? Switch13 : Switch13;
		auto &&Switch15 = _bChoose ? Switch14 : Switch14;
		auto &&Switch16 = _bChoose ? Switch15 : Switch15;
		auto &&Switch17 = _bChoose ? Switch16 : Switch16;
		auto &&Switch18 = _bChoose ? Switch17 : Switch17;
		auto &&Switch19 = _bChoose ? Switch18 : Switch18;
		auto &&Switch20 = _bChoose ? Switch19 : Switch19;
		auto &&Switch21 = _bChoose ? Switch20 : Switch20;
		auto &&Switch22 = _bChoose ? Switch21 : Switch21;
		auto &&Switch23 = _bChoose ? Switch22 : Switch22;
		auto &&Switch24 = _bChoose ? Switch23 : Switch23;
		co_await fg_Move(Switch24);
		co_return m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}
};

// A lambda held in a local, dispatched to the current actor after a switch.
struct CRound101Stored : CActor
{
	int m_Value;

	TCFuture<int> f_Run()
	{
		auto fRead = [this]
			{
				return m_Value;
			}
		;
		co_await fg_ContinueRunningOnActor(fg_BlockingActor());
		co_return co_await (g_Dispatch / fRead);
// CHECK-MESSAGES: :[[@LINE-1]]:36: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}
};

// Either of a conditional pointer into the object, accessed after a switch.
struct CRound102Conditional : CActor
{
	int m_Values[2];

	TCFuture<int> f_Read(TCActor<> _Other, bool _bChoose)
	{
		int *pValue = m_Values;
		co_return (_bChoose ? pValue : pValue)[(co_await fg_ContinueRunningOnActor(_Other), 0)];
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}
};

// Assigned through a chain of local references.
struct CRound103Aliases : CActor
{
	int m_Value;

	TCFuture<int> f_Run()
	{
		int *pValue = nullptr;
		int *&pFirst = pValue;
		int *&pSecond = pFirst;
		int *&pThird = pSecond;
		int *&pFourth = pThird;
		pFourth = &m_Value;
		co_await fg_ContinueRunningOnActor(fg_BlockingActor());
		co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}
};
#define DRound104Twice(d_Code) d_Code d_Code
#define DRound104Many(d_Code) DRound104Twice(DRound104Twice(DRound104Twice(DRound104Twice(DRound104Twice(DRound104Twice(DRound104Twice(DRound104Twice(DRound104Twice(DRound104Twice(DRound104Twice(d_Code)))))))))))

// Many branches after a switch, told in time that grows with their number.
struct CRound104Branches : CActor
{
	int m_Value;

	TCFuture<int> f_Run(TCActor<> _Other, bool _bFlag)
	{
		co_await fg_ContinueRunningOnActor(_Other);
		DRound104Many(if (_bFlag) _bFlag = !_bFlag;)
		co_return m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}
};

// A pointer assigned in a lambda that another lambda calls.
struct CRound105Nested : CActor
{
	int m_Value = 0;

	TCFuture<int> f_Read(TCActor<CRound105Nested> _Other)
	{
		int Local = 1;
		int *pValue = &Local;
		auto fSet = [&]
			{
				pValue = &m_Value;
			}
		;
		auto fWrapper = [&]
			{
				fSet();
			}
		;
		fWrapper();
		co_await fg_ContinueRunningOnActor(_Other);
		co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}
};

// A pointer assigned in a lambda called by its operator.
struct CRound105ExplicitCall : CActor
{
	int m_Value = 0;

	TCFuture<int> f_Read(TCActor<CRound105ExplicitCall> _Other)
	{
		int Local = 1;
		int *pValue = &Local;
		auto fSet = [&]
			{
				pValue = &m_Value;
			}
		;
		fSet.operator()();
		co_await fg_ContinueRunningOnActor(_Other);
		co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}
};

// A pointer to a member applied after the switch in its right operand.
struct CRound105MemberPointer : CActor
{
	int m_Value = 0;

	TCFuture<int> f_Read(TCActor<CRound105MemberPointer> _Other)
	{
		co_return this->*(co_await fg_ContinueRunningOnActor(_Other), &CRound105MemberPointer::m_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}
};

// A pointer pointed elsewhere by an awaited coroutine lambda, before it first
// suspends.
struct CRound106Redirected : CActor
{
	int m_Value = 0;

	TCFuture<int> f_Read(TCActor<CRound106Redirected> _Other)
	{
		int Local = 0;
		int *pValue = &m_Value;
		co_await fg_CallSafe
			(
				[&pValue, &Local]() -> TCFuture<int>
				{
					pValue = &Local;
					co_return 0;
				}
			)
		;
		co_await fg_ContinueRunningOnActor(_Other);
		co_return *pValue;
	}
};

// A lambda that points a pointer elsewhere before it calls one that reads it.
struct CRound107Wrapper : CActor
{
	int m_Value = 0;

	TCFuture<int> f_Read(TCActor<CRound107Wrapper> _Other)
	{
		int Local = 7;
		auto *pValue = &m_Value;
		auto fRead = [&]
			{
				return *pValue;
			}
		;
		auto fWrapper = [&]
			{
				pValue = &Local;
				return fRead();
			}
		;
		co_await fg_ContinueRunningOnActor(_Other);
		co_return fWrapper();
	}
};

// The address of a member given to what reads it after another argument
// switches.
extern int g_Round107Sink;

struct CRound107Read
{
	CRound107Read(int const *_pValue, int _Index)
	{
		g_Round107Sink = *_pValue;
	}
};

int fg_Round107Read(int const *_pValue, int _Index);

struct CRound107Address : CActor
{
	int m_Value = 0;

	TCFuture<int> f_Construct(TCActor<CRound107Address> _Other)
	{
		CRound107Read Read{&m_Value, (co_await fg_ContinueRunningOnActor(_Other), 0)};
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
		co_return 0;
	}

	TCFuture<int> f_Call(TCActor<CRound107Address> _Other)
	{
		co_return fg_Round107Read(&m_Value, (co_await fg_ContinueRunningOnActor(_Other), 0));
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}
};

// A switch back kept in a local and awaited in a lambda.
struct CRound107StoredSwitch : CActor
{
	int m_Value = 0;

	TCFuture<int> f_Read(TCActor<CRound107StoredSwitch> _Other)
	{
		auto Own = fg_ThisActor(this);
		co_await fg_ContinueRunningOnActor(_Other);
		co_return co_await fg_CallSafe
			(
				[this, Own]() -> TCFuture<int>
				{
					auto SwitchBack = fg_ContinueRunningOnActor(Own);
					co_await fg_Move(SwitchBack);
					co_return m_Value;
				}
			)
		;
	}
};

// However many lambdas are around it.
#define DRound107Wrap(...) ([pValue, _Other] { return fg_CallSafe(__VA_ARGS__); })
#define DRound107Wrap4(d_Lambda) DRound107Wrap(DRound107Wrap(DRound107Wrap(DRound107Wrap(d_Lambda))))
#define DRound107Wrap16(d_Lambda) DRound107Wrap4(DRound107Wrap4(DRound107Wrap4(DRound107Wrap4(d_Lambda))))

struct CRound107Deep : CActor
{
	int m_Value = 0;

	TCFuture<int> f_Read(TCActor<CRound107Deep> _Other)
	{
		int *pValue = &m_Value;
		co_return co_await fg_CallSafe
			(
				DRound107Wrap16
				(
					(
						[pValue, _Other]() -> TCFuture<int>
						{
							co_await fg_ContinueRunningOnActor(_Other);
							co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
						}
					)
				)
			)
		;
	}
};

// Not a use in a branch that a constant condition never takes.
struct CRound107NeverUsed : CActor
{
	int m_Value = 0;

	TCFuture<int> f_Read(TCActor<CRound107NeverUsed> _Other)
	{
		co_await fg_ContinueRunningOnActor(_Other);
		auto fRead = [this]
			{
				if (false)
					return m_Value;
				return 0;
			}
		;
		co_return fRead();
	}
};

// A lambda that runs another twice, with what the pointers hold each time.
struct CRound109Twice : CActor
{
	int m_Value = 1;

	TCFuture<int> f_Read(TCActor<CRound109Twice> _Other)
	{
		int Local = 0;
		int *pValue = &Local;
		int *pSource = &Local;
		auto fCopy = [&pValue, &pSource]
			{
				pValue = pSource;
			}
		;
		auto fTwice = [this, &fCopy, &pSource]
			{
				fCopy();
				pSource = &m_Value;
				fCopy();
			}
		;
		fTwice();
		co_await fg_ContinueRunningOnActor(_Other);
		co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}
};

// A timer given an empty actor runs on the current one.
struct CRound109Timer : CActor
{
	int m_Value = 0;

	TCFuture<int> f_Run()
	{
		co_await fg_ContinueRunningOnActor(fg_BlockingActor());
		fg_RegisterTimer
			(
				1.0
				, [this]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
				{
					co_return m_Value;
				}
				, TCActor<>{}
			)
		;
		co_return 0;
	}
};

// A saved switch assigned through a conditional, or through an alias of one,
// may switch elsewhere.
struct CRound111Actor : CActor
{
	int m_Value;
	TCFuture<int> f_Conditional(bool _bChoose, TCActor<> _Other)
	{
		auto Own = fg_ThisActor(this);
		auto Back = fg_ContinueRunningOnActor(Own);
		auto Spare = fg_ContinueRunningOnActor(Own);
		(_bChoose ? Back : Spare) = fg_ContinueRunningOnActor(_Other);
		co_await fg_ContinueRunningOnActor(_Other);
		co_await fg_Move(Back);
		m_Value = 1;
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
		co_return 0;
	}
	TCFuture<int> f_Alias(bool _bChoose, TCActor<> _Other)
	{
		auto Own = fg_ThisActor(this);
		auto Back = fg_ContinueRunningOnActor(Own);
		auto Spare = fg_ContinueRunningOnActor(Own);
		auto &Either = _bChoose ? Back : Spare;
		Either = fg_ContinueRunningOnActor(_Other);
		co_await fg_ContinueRunningOnActor(_Other);
		co_await fg_Move(Back);
		m_Value = 1;
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
		co_return 0;
	}
};

// Past as many lambda calls as are followed, what a lambda assigns is still
// applied, as maybe.
#define DRound112Step(d_Index, d_Previous) auto f##d_Index = [&] { f##d_Previous(); f##d_Previous(); }
struct CRound112Budget : CActor
{
	int m_Value;

	TCFuture<int> f_Run(TCActor<> _Other)
	{
		int *p = nullptr;
		auto fSet = [&] { p = &m_Value; };
		auto f0 = [] {};
		DRound112Step(1, 0); DRound112Step(2, 1); DRound112Step(3, 2);
		DRound112Step(4, 3); DRound112Step(5, 4); DRound112Step(6, 5);
		DRound112Step(7, 6); DRound112Step(8, 7); DRound112Step(9, 8);
		auto fRun = [&] { f9(); fSet(); };
		fRun();
		co_await fg_ContinueRunningOnActor(_Other);
		co_return *p;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}
};

// Round 113: a constant conditional switches as its chosen value does
namespace NRound113Constant
{
struct Actor : CActor {
  int value;
  TCFuture<int> f(TCActor<> other) {
    auto own = fg_ThisActor(this);
    co_await (true ? fg_ContinueRunningOnActor(own) : fg_ContinueRunningOnActor(other));
    co_return value;
  }
};
}

// A transfer of a class derived from CCoroutineTransferOwnership switches too.
namespace NRound116DerivedTransfer {
struct Transfer : CCoroutineTransferOwnership {};
struct App : CActor {
  int value;
  TCFuture<int> bad(Transfer transfer) {
    co_await fg_Move(transfer);
    co_return value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
  }
};
}

// A virtual call can replace the actor a handle refers to, through an override.
namespace NRound116VirtualReplace {
struct Replacer : CAllowUnsafeThis {
  virtual void replace(TCActor<> &actor) {}
};
struct Other : Replacer {
  TCActor<> other;
  void replace(TCActor<> &actor) override {
    actor = other;
  }
};
struct A : CActor {
  int value;
  TCFuture<int> bad(Replacer *r) {
    TCActor<> home = fg_ThisActor(this);
    auto away = fg_BlockingActor();
    co_await fg_ContinueRunningOnActor(away);
    r->replace(home);
    co_await fg_ContinueRunningOnActor(home);
    co_return value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
  }
};
}

// The address taken by __builtin_addressof is into the object, like &.
namespace NRound116BuiltinAddress {
struct Actor : CActor {
  int value;
  TCActor<> other;
  TCFuture<int> run() {
    int *p = __builtin_addressof(value);
    co_await fg_ContinueRunningOnActor(other);
    co_return *p;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
  }
};
}

// A class reached through many paths of its bases is looked at once.
namespace NRound118SharedBases {
template<int N> struct Layer;
template<int N> struct Left : virtual Layer<N-1> {};
template<int N> struct Right : virtual Layer<N-1> {};
template<int N> struct Layer : Left<N>, Right<N> {};
template<> struct Layer<0> {};

struct Awaitable : Layer<20> {
  TCAwaiter<int> operator co_await() const;
};

#define TEN \
  co_await a; co_await a; co_await a; co_await a; co_await a; \
  co_await a; co_await a; co_await a; co_await a; co_await a;

TCFuture<int> run(Awaitable const &a) {
  TEN TEN TEN TEN TEN TEN TEN TEN TEN TEN
  co_return 0;
}
}

// fg_CallSafeDispatched runs what it is given on the current actor.
namespace NRound118CurrentDispatch {
struct Actor : CActor {
  int value;
  TCFuture<int> test(TCActor<> other) {
    co_await fg_ContinueRunningOnActor(other);
    co_return co_await fg_CallSafeDispatched(
      [this]() -> TCFuture<int> { co_return value; });
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
  }
};
}

// A local reference to a pointer is the pointer it is bound to.
namespace NRound118PointerReference {
struct Test : CActor {
  int m_Value;

  TCFuture<int> f(TCActor<> other) {
    int local = 0;
    int *p = &local;
    int *&alias = p;
    p = &m_Value;
    co_await fg_ContinueRunningOnActor(other);
    co_return *alias;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
  }
};
}

// Assigning through a captured reference writes what it refers to.
namespace NRound118ReferenceWrite {
TCFuture<int> work();

struct Test : CActor {
  int m_Value;

  TCFuture<int> race(TCActor<> other) {
    int &value = m_Value;
    auto write = [&value] { value = 1; };
    co_await fg_ContinueRunningOnActor(other);
    write();
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
    co_return 0;
  }

};
}

// A temporary scope exit runs at the end of the full expression it is made in.
namespace NRound118TemporaryExit {
struct Test : CActor {
  int m_Value;

  TCFuture<int> f(TCActor<> other) {
    ((void)(g_OnScopeExit / [this] { m_Value = 1; }),
// CHECK-MESSAGES: :[[@LINE-1]]:29: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
     co_await fg_ContinueRunningOnActor(other));
    co_return 0;
  }
};
}

// Round 119: what a lambda called in place returns, and the value of a
// statement expression, point where that does.
namespace NRound119LambdaReturn
{
struct CRunner : CActor {
  int m_Value = 0;

  TCFuture<int> f_Lambda() {
    auto pValue = [this] { return &m_Value; }();
    co_await fg_ContinueRunningOnActor(fg_BlockingActor());
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
  }

  TCFuture<int> f_Statement() {
    auto pValue = ({ &m_Value; });
    co_await fg_ContinueRunningOnActor(fg_BlockingActor());
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
  }
};
}

// Round 120: a pointer into the object kept in a static local or an assigned
// parameter.
namespace NRound120StaticParameter
{
struct CRunner : CActor {
  int m_Value = 0;

  TCFuture<int> f_Static(TCActor<> _Other) {
    static int *s_pValue = nullptr;
    s_pValue = &m_Value;
    co_await fg_ContinueRunningOnActor(_Other);
    co_return *s_pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
  }

  TCUnsafeFuture<int> f_Parameter(TCActor<> _Other, int *_pValue) {
    int Before = *_pValue;
    _pValue = &m_Value;
    co_await fg_ContinueRunningOnActor(_Other);
    co_return *_pValue + Before;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
  }
};
}

// Round 121: a scope exit made by calling the operator explicitly or through a
// reference to the helper, a reference to a pointer to the object of an actor,
// and what a lambda called in place returns through a local it assigns, but
// not from a branch that is discarded.
namespace NRound121
{
struct CApp : CActor {
  int m_Value = 0;
  int *mp_pOther = nullptr;

  TCFuture<int> f_Explicit(TCActor<> _Other) {
    auto Cleanup = g_OnScopeExit.operator/([this] { m_Value = 1; });
// CHECK-MESSAGES: :[[@LINE-1]]:44: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
    co_await fg_ContinueRunningOnActor(_Other);
    co_return 0;
  }

  TCFuture<int> f_Helper(TCActor<> _Other) {
    auto const &Helper = g_OnScopeExit;
    auto Cleanup = Helper / [this] { m_Value = 1; };
// CHECK-MESSAGES: :[[@LINE-1]]:29: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
    co_await fg_ContinueRunningOnActor(_Other);
    co_return 0;
  }

  TCFuture<int> f_Assigned(TCActor<> _Other) {
    int *pValue = [this] {
      int *pInner = nullptr;
      pInner = &m_Value;
      return pInner;
    }();
    co_await fg_ContinueRunningOnActor(_Other);
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
  }

  TCFuture<int> f_Discarded(TCActor<> _Other) {
    static int s_Global = 0;
    int *pValue = [this] {
      if constexpr (false)
        return &m_Value;
      return &s_Global;
    }();
    co_await fg_ContinueRunningOnActor(_Other);
    co_return *pValue;
  }
};

TCFuture<int> fg_Reference(CApp *const &_pActor, TCActor<> _Other) {
  co_await fg_ContinueRunningOnActor(_Other);
  co_return _pActor->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
}
}

// Round 122: switching back through a comma, a pointer to a local pointer as
// that pointer is, a lambda run by `std::invoke`, and lambdas that call each
// other many times.
namespace std {
template <typename t_FFunction>
auto invoke(t_FFunction &&_fFunction) -> decltype(_fFunction()) { return _fFunction(); }
}

namespace NRound122
{
void fg_Log();
int g_Global;
extern bool g_bChoose;
#define DRound122Next(d_N, d_P) auto f##d_N = [&] { return g_bChoose ? f##d_P() : f##d_P(); };

struct CApp : CActor {
  int m_Value;

  TCFuture<int> f_Back() {
    auto Own = fg_ThisActor(this);
    co_await fg_ContinueRunningOnActor(fg_BlockingActor());
    co_await (fg_Log(), fg_ContinueRunningOnActor(Own));
    co_return m_Value;
  }

  TCFuture<int> f_Missed(TCActor<> _Other) {
    int *pValue = nullptr;
    int **ppValue = &pValue;
    pValue = &m_Value;
    co_await fg_ContinueRunningOnActor(_Other);
    co_return **ppValue;
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
  }

  TCFuture<int> f_Local(TCActor<> _Other) {
    int Local = 3;
    int *pValue = &m_Value;
    int **ppValue = &pValue;
    pValue = &Local;
    co_await fg_ContinueRunningOnActor(_Other);
    co_return **ppValue;
  }

  TCFuture<int> f_Invoke(TCActor<> _Other) {
    auto fRead = [this] { return m_Value; };
    co_await fg_ContinueRunningOnActor(_Other);
    co_return std::invoke(fRead);
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
  }

  TCFuture<int> f_Many(TCActor<> _Other) {
    auto f0 = [] { return &g_Global; };
    DRound122Next(1, 0) DRound122Next(2, 1) DRound122Next(3, 2) DRound122Next(4, 3)
    DRound122Next(5, 4) DRound122Next(6, 5) DRound122Next(7, 6) DRound122Next(8, 7)
    DRound122Next(9, 8) DRound122Next(10, 9) DRound122Next(11, 10) DRound122Next(12, 11)
    DRound122Next(13, 12) DRound122Next(14, 13) DRound122Next(15, 14) DRound122Next(16, 15)
    DRound122Next(17, 16) DRound122Next(18, 17) DRound122Next(19, 18) DRound122Next(20, 19)
    DRound122Next(21, 20) DRound122Next(22, 21) DRound122Next(23, 22) DRound122Next(24, 23)
    int *pValue = f24();
    co_await fg_ContinueRunningOnActor(_Other);
    co_return *pValue;
  }
};
}

// Round 126: a handle changed through a const reference to it whose const is
// cast away isn't the own actor any more, nor is a stored switch changed
// through a pointer to const, and a cast to a reference to the own handle is
// it.
namespace NRound126
{
struct CApp : CActor {
  int m_Value;

  TCFuture<int> f_ConstCast(TCActor<CApp> _Other) {
    auto Own = fg_ThisActor(this);
    auto const &Alias = Own;
    const_cast<decltype(Own) &>(Alias) = _Other;
    co_await fg_ContinueRunningOnActor(Own);
    co_return m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
  }

  TCFuture<int> f_StoredConstCast(TCActor<> _Other) {
    auto Own = fg_ThisActor(this);
    auto Back = fg_ContinueRunningOnActor(Own);
    auto const &Alias = Back;
    auto const *pBack = &Alias;
    *const_cast<decltype(Back) *>(pBack) = fg_ContinueRunningOnActor(_Other);
    co_await fg_Move(Back);
    co_return m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
  }

  TCFuture<int> f_Cast(TCActor<> _Other) {
    auto Own = fg_ThisActor(this);
    co_await fg_ContinueRunningOnActor(_Other);
    co_await fg_ContinueRunningOnActor(static_cast<decltype(Own) const &>(Own));
    co_return m_Value;
  }
};
}

// Round 127: a structured binding of the object is used like it, and an
// assignment of a stored switch that can't run doesn't change it.
namespace NRound127
{
struct CApp : CActor {
  struct CData {
    int m_Value;
  } m_Data;
  int m_Value;

  TCFuture<int> f_Binding(TCActor<> _Other) {
    auto &[Value] = m_Data;
    co_await fg_ContinueRunningOnActor(_Other);
    Value = 42;
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
    co_return Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
  }

  TCFuture<int> f_Unreachable(TCActor<> _Other) {
    auto Own = fg_ThisActor(this);
    auto Back = fg_ContinueRunningOnActor(Own);
    if (false)
      Back = fg_ContinueRunningOnActor(_Other);
    co_await fg_ContinueRunningOnActor(_Other);
    co_await fg_Move(Back);
    co_return m_Value;
  }
};
}

// Round 129: a called lambda that only compares a pointer into the object
// doesn't use it, lambdas that call each other are looked into once for what
// they can read, and the first value of `?:` is its condition.
namespace NRound129
{
int g_Global;

struct CApp : CActor {
  int m_Value;

  TCFuture<int> f_Compare(TCActor<> _Other) {
    int *pValue = &m_Value;
    co_await fg_ContinueRunningOnActor(_Other);
    bool bEmpty = [pValue] { return pValue == nullptr; }();
    co_return bEmpty ? 1 : 0;
  }

#define DRound129Step(d_New, d_Old)                                            \
  int *p##d_New = &m_Value;                                                    \
  auto f##d_New = [&] {                                                        \
    f##d_Old();                                                                \
    p##d_New = nullptr;                                                        \
    f##d_Old();                                                                \
  };

  TCFuture<int> f_Nested(TCActor<> _Other) {
    int *p0 = &m_Value;
    auto f0 = [&] { p0 = nullptr; };
    DRound129Step(1, 0) DRound129Step(2, 1) DRound129Step(3, 2)
    DRound129Step(4, 3) DRound129Step(5, 4) DRound129Step(6, 5)
    DRound129Step(7, 6) DRound129Step(8, 7) DRound129Step(9, 8)
    DRound129Step(10, 9) DRound129Step(11, 10) DRound129Step(12, 11)
    DRound129Step(13, 12) DRound129Step(14, 13) DRound129Step(15, 14)
    DRound129Step(16, 15) DRound129Step(17, 16) DRound129Step(18, 17)
    DRound129Step(19, 18) DRound129Step(20, 19) DRound129Step(21, 20)
    DRound129Step(22, 21) DRound129Step(23, 22) DRound129Step(24, 23)
    co_await fg_ContinueRunningOnActor(_Other);
    f24();
    co_return 0;
  }

  TCFuture<int> f_Elvis() {
    int *pValue = &m_Value ?: &g_Global;
    co_await fg_ContinueRunningOnActor(fg_BlockingActor());
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
  }
};
}

// Round 132: a pointer pointed elsewhere after it is read in the same
// expression is read as it was.
namespace NRound132
{
struct CApp : CActor {
  int m_Value;

  TCFuture<int> f_Run(int _Local) {
    int *pValue = &m_Value;
    co_return pValue[(co_await fg_ContinueRunningOnActor(fg_BlockingActor()),
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
                      pValue = &_Local, 0)];
  }
};
}

// Round 133: guards held in an array have their cleanups, and an assignment
// through a reference bound to either of a conditional may assign either.
namespace NRound133
{
struct CMap {
  int &operator[](int _Key);
};

struct CApp : CActor {
  int m_Value;

  TCFuture<int> f_Array() {
    auto fCleanup = [this]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:21: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
      m_Value = 1;
      co_return 0;
    };
    using CGuard = decltype(fg_AsyncDestroy(fCleanup).await_resume());
    CGuard Guards[] = {co_await fg_AsyncDestroy(fCleanup)};
    co_await fg_ContinueRunningOnActor(fg_BlockingActor());
    co_return 0;
  }

  TCFuture<int> f_Either(bool _bChoose) {
    int *pFirst = nullptr, *pSecond = nullptr;
    int *&pAlias = _bChoose ? pFirst : pSecond;
    pAlias = &m_Value;
    co_await fg_ContinueRunningOnActor(fg_BlockingActor());
    co_return pFirst ? *pFirst : 0;
// CHECK-MESSAGES: :[[@LINE-1]]:25: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
  }
};
}

// Round 135: a conditional of handles of the own actor switches back.
namespace NRound135
{
struct CApp : CActor {
  int m_Value;

  TCFuture<int> f_Run(bool _bChoose, TCActor<> _Other) {
    auto Own = fg_ThisActor(this);
    auto OwnCopy = Own;
    co_await fg_ContinueRunningOnActor(_Other);
    co_await fg_ContinueRunningOnActor(_bChoose ? Own : OwnCopy);
    co_return m_Value;
  }
};
}

// Round 136: each alias is followed once, also one many are bound to, and a
// lambda std::invoke runs assigns where it runs.
namespace NRound136
{
int g_Value;

struct CApp : CActor {
  int m_Value;

  TCFuture<int> f_Aliases(bool _bChoose, TCActor<> _Other) {
    int *pValue = &g_Value, *pSpare = &g_Value;
    int *&r0 = pSpare;
    int *&r1 = _bChoose ? r0 : r0;
    int *&r2 = _bChoose ? r1 : r1;
    int *&r3 = _bChoose ? r2 : r2;
    int *&r4 = _bChoose ? r3 : r3;
    int *&r5 = _bChoose ? r4 : r4;
    int *&r6 = _bChoose ? r5 : r5;
    int *&r7 = _bChoose ? r6 : r6;
    int *&r8 = _bChoose ? r7 : r7;
    int *&pAlias = _bChoose ? pValue : r8;
    pAlias = &m_Value;
    co_await fg_ContinueRunningOnActor(_Other);
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
  }

  TCFuture<int> f_Invoke(TCActor<> _Other) {
    int Local = 0;
    int *pValue = &Local;
    std::invoke([this, &pValue] { pValue = &m_Value; });
    co_await fg_ContinueRunningOnActor(_Other);
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
  }
};
}

namespace NRound137 {
// A reference to a pointer member, `int *const &pValue = m_pValue;`, reads the
// actor when it is compared after switching away.
struct CApp : CActor {
  int *m_pValue = nullptr;

  TCFuture<int> f_Reference() {
    int *const &pValue = m_pValue;
    co_await fg_ContinueRunningOnActor(fg_BlockingActor());
    co_return pValue != nullptr;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
  }

  // But a copy of it doesn't.
  TCFuture<int> f_Copy() {
    int *pValue = m_pValue;
    co_await fg_ContinueRunningOnActor(fg_BlockingActor());
    co_return pValue != nullptr;
  }
};
}

namespace NRound140 {
// A guard moved through a local reference to it, `fg_Move(Alias)`, and then
// cleared, doesn't run the callback.
struct CApp : CActor {
  int m_Value;

  TCFuture<int> f_Moved(TCActor<> _Other) {
    auto Guard = g_OnScopeExit / [this] { ++m_Value; };
    auto &Alias = Guard;
    auto Moved = fg_Move(Alias);
    Moved.f_Clear();
    co_await fg_ContinueRunningOnActor(_Other);
    co_return 0;
  }

  // But one that isn't cleared does.
  TCFuture<int> f_Kept(TCActor<> _Other) {
    auto Guard = g_OnScopeExit / [this] { ++m_Value; };
// CHECK-MESSAGES: :[[@LINE-1]]:34: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
    auto &Alias = Guard;
    auto Moved = fg_Move(Alias);
    co_await fg_ContinueRunningOnActor(_Other);
    co_return 0;
  }
};
}

namespace NRound141 {
struct CApp : CActor {
  int m_Value;
  int f_Read(int _Add) const { return m_Value + _Add; }

  // The object of a call through what a pointer to it points to, `(*this)
  // .f_Read(...)`, is used where the call runs.
  TCFuture<int> f_Dereferenced() {
    co_return (*this).f_Read(
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
        (co_await fg_ContinueRunningOnActor(fg_BlockingActor()), 0));
  }

  // A switch moved into another local switches where it does.
  TCFuture<int> f_Moved() {
    auto Own = fg_ThisActor(this);
    auto Switch = fg_ContinueRunningOnActor(Own);
    auto Moved = fg_Move(Switch);
    co_await fg_Move(Moved);
    co_return m_Value;
  }
};
}

namespace NRound143 {
// A lambda that another runs by `std::invoke`.
struct CApp : CActor {
  int m_Value;
  TCFuture<int> f_Nested() {
    auto fRead = [this] { return m_Value; };
    auto fWrapper = [&fRead] { return std::invoke(fRead); };
    co_await fg_ContinueRunningOnActor(fg_BlockingActor());
    co_return fWrapper();
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
  }
};
}

namespace NRound145 {
struct CApp : CActor {
  int m_Value;

  // The handle changed through a C-style cast of a const alias.
  TCFuture<int> f_Changed(TCActor<> _Other) {
    TCActor<> Own = fg_ThisActor(this);
    auto const &Alias = Own;
    (TCActor<> &)Alias = _Other;
    co_await fg_ContinueRunningOnActor(_Other);
    co_await fg_ContinueRunningOnActor(Own);
    co_return m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
  }

  // But not by what a requirement only names.
  TCFuture<int> f_Required(TCActor<> _Other) {
    TCActor<> Own = fg_ThisActor(this);
    static_assert(requires { Own = _Other; });
    co_await fg_ContinueRunningOnActor(_Other);
    co_await fg_ContinueRunningOnActor(Own);
    co_return m_Value;
  }
};
}

namespace NRound146 {
// A wrapper whose `operator co_await` gives the awaiter of a switch switches.
struct CWrappedSwitch
{
	TCActor<> m_Target;
	auto operator co_await()
	{
		return fg_ContinueRunningOnActor(m_Target).operator co_await();
	}
};

struct CWrapped : CActor
{
	int m_Value = 0;
	TCFuture<int> f_Run(TCActor<> _Other)
	{
		co_await CWrappedSwitch{_Other};
		co_return m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}
};
}

namespace NRound147 {
// A static member through a conditional that chooses `this`.
struct CStatic : CActor
{
	static constexpr int mc_Value = 7;
	int m_Value = 0;

	TCFuture<int> f_Static()
	{
		co_await fg_ContinueRunningOnActor(fg_BlockingActor());
		co_return (true ? this : nullptr)->mc_Value;
	}

	// But not one that isn't static.
	TCFuture<int> f_Member(bool _bChoose)
	{
		co_await fg_ContinueRunningOnActor(fg_BlockingActor());
		co_return (_bChoose ? this : nullptr)->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:25: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}
};
}

namespace NRound149 {
struct CApp : CActor
{
	int m_Value;

	// An assignment that a constant condition never runs.
	TCFuture<int> f_Never(TCActor<> _Other)
	{
		auto Own = fg_ThisActor(this);
		if (false)
			Own = _Other;
		co_await fg_ContinueRunningOnActor(_Other);
		co_await fg_ContinueRunningOnActor(Own);
		co_return m_Value;
	}

	// A chain of unchanged copies of the own actor.
	TCFuture<int> f_Copies(TCActor<> _Other)
	{
		auto Own0 = fg_ThisActor(this);
		auto Own1 = Own0;
		auto Own2 = Own1;
		auto Own3 = Own2;
		auto Own4 = Own3;
		auto Own5 = Own4;
		auto Own6 = Own5;
		auto Own7 = Own6;
		auto Own8 = Own7;
		auto Own9 = Own8;
		co_await fg_ContinueRunningOnActor(_Other);
		co_await fg_ContinueRunningOnActor(Own9);
		co_return m_Value;
	}
};
}

namespace NRound152 {
struct CApp : CActor
{
	int m_Value = 0;
	int m_Values[2][2] = {};

	// What an assignment of a pointer gives, read through.
	TCFuture<int> f_Assigned(TCActor<> _Other)
	{
		int *pValue = nullptr;
		int *pSaved = &m_Value;
		co_await fg_ContinueRunningOnActor(_Other);
		co_return *(pValue = pSaved);
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}

	// A row of an array member, indexed after switching.
	TCFuture<int> f_Rows(TCActor<> _Other)
	{
		auto pRows = m_Values;
		co_return pRows[0][(co_await fg_ContinueRunningOnActor(_Other), 0)];
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}

	// A reference to what an assignment changes.
	TCFuture<int> f_AssignmentReference(TCActor<> _Other)
	{
		int &Value = (m_Value = 1);
		co_await fg_ContinueRunningOnActor(_Other);
		co_return Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}

	// An assignment that never runs.
	TCFuture<int> f_NeverAssigned(TCActor<> _Other)
	{
		auto Own = fg_ThisActor(this);
		while (false)
			Own = _Other;
		co_await fg_ContinueRunningOnActor(_Other);
		co_await fg_ContinueRunningOnActor(Own);
		co_return m_Value;
	}
};
}

namespace NRound154 {
struct CApp : CActor {
  int m_Value = 0;
  // Written after the right side of a compound assignment, which runs first.
  TCFuture<int> f_Compound(TCActor<> _Other) {
    m_Value += (co_await fg_ContinueRunningOnActor(_Other), 1);
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
    co_return 0;
  }
  // Where a stepped pointer pointed, accessed after the switch.
  TCFuture<int> f_Stepped(TCActor<> _Other) {
    int *pValue = &m_Value;
    co_return (pValue++)[(co_await fg_ContinueRunningOnActor(_Other), 0)];
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
  }
  // Its own actor converted by name.
  TCFuture<int> f_Home() {
    auto Home = TCActor<>(self);
    co_await fg_ContinueRunningOnActor(fg_BlockingActor());
    co_await fg_ContinueRunningOnActor(Home);
    co_return m_Value;
  }
};
}

namespace NRound158 {
// The address a helper returns of what it is given by reference.
int *fg_Address(int &_Value) { return &_Value; }
struct CApp : CActor {
  int m_Value;
  TCFuture<int> f_Address(TCActor<> _Other) {
    int *pValue = fg_Address(m_Value);
    co_await fg_ContinueRunningOnActor(_Other);
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
  }
  // A lambda wrapped by name.
  TCFuture<int> f_Braces(TCActor<> _Other) {
    auto fRead = TCFunction<int()>{[this] { return m_Value; }};
    co_await fg_ContinueRunningOnActor(_Other);
    co_return fRead();
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
  }
  TCFuture<int> f_Parentheses(TCActor<> _Other) {
    auto fRead = TCFunction<int()>([this] { return m_Value; });
    co_await fg_ContinueRunningOnActor(_Other);
    co_return fRead();
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
  }
};
}

namespace NRound159 {
struct CApp : CActor {
  int m_Value = 0;
  // A copy that a lambda around it captures is reset, not the pointer.
  TCFuture<int> f_CopyReset(TCActor<> _Other) {
    int *pValue = &m_Value;
    auto fResetCopy = [pValue]() mutable {
      auto fReset = [&pValue] { pValue = nullptr; };
      fReset();
    };
    fResetCopy();
    co_await fg_ContinueRunningOnActor(_Other);
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
  }
  // Laundered, it is the object.
  TCFuture<int> f_Laundered(TCActor<> _Other) {
    CApp *pThis = __builtin_launder(this);
    co_await fg_ContinueRunningOnActor(_Other);
    co_return pThis->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
  }
};
}

namespace NRound160 {
// Pointed at the object through a copy of a pointer to the pointer.
struct CApp : CActor {
  int m_Value;
  TCFuture<int> f_Run() {
    int Local = 0;
    int *pValue = &Local;
    int **ppValue = &pValue;
    int **ppCopy = ppValue;
    *ppCopy = &m_Value;
    co_await fg_ContinueRunningOnActor(fg_BlockingActor());
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
  }
};
}

namespace NRound161 {
struct CRead {
  int m_Copy;
  CRead(int const &_Value, int) : m_Copy(_Value) {}
};
struct CApp : CActor {
  int m_Value = 0;
  bool m_bChoose = false;
  // The object given through a comma or a conditional, used after the switch.
  TCFuture<int> f_Comma() {
    CRead Result{(void(0), m_Value), (co_await fg_ContinueRunningOnActor(fg_BlockingActor()), 0)};
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
    co_return Result.m_Copy;
  }
  TCFuture<int> f_Conditional() {
    CRead Result{(m_bChoose ? m_Value : m_Value), (co_await fg_ContinueRunningOnActor(fg_BlockingActor()), 0)};
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
    co_return Result.m_Copy;
  }
};
}

namespace NRound163 {
// Reassigned through a long chain of references.
struct CApp : CActor {
  int m_Value = 0;
  TCFuture<int> f_Run(TCActor<> _Other) {
    auto Own = fg_ThisActor(this);
    auto Back = fg_ContinueRunningOnActor(Own);
    auto &A0 = Back;
    auto &A1 = A0;
    auto &A2 = A1;
    auto &A3 = A2;
    auto &A4 = A3;
    auto &A5 = A4;
    auto &A6 = A5;
    auto &A7 = A6;
    auto &A8 = A7;
    A8 = fg_ContinueRunningOnActor(_Other);
    co_await Back;
    co_return m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
  }
};
}

namespace NRound164 {
// An unrelated assignment through a long chain of references bound to
// conditionals is told apart in time linear in its length.
#define DAlias(d_Next, d_Previous)                                             \
  int &r##d_Next = _bChoose ? r##d_Previous : r##d_Previous
struct CApp : CActor {
  TCFuture<int> f_Run(bool _bChoose) {
    auto Back = fg_ContinueRunningOnActor(fg_ThisActor(this));
    int Value = 0;
    int &r0 = Value;
    DAlias(1, 0); DAlias(2, 1); DAlias(3, 2); DAlias(4, 3); DAlias(5, 4);
    DAlias(6, 5); DAlias(7, 6); DAlias(8, 7); DAlias(9, 8); DAlias(10, 9);
    DAlias(11, 10); DAlias(12, 11); DAlias(13, 12); DAlias(14, 13);
    DAlias(15, 14); DAlias(16, 15); DAlias(17, 16); DAlias(18, 17);
    DAlias(19, 18); DAlias(20, 19); DAlias(21, 20); DAlias(22, 21);
    DAlias(23, 22); DAlias(24, 23); DAlias(25, 24); DAlias(26, 25);
    DAlias(27, 26); DAlias(28, 27); DAlias(29, 28); DAlias(30, 29);
    DAlias(31, 30); DAlias(32, 31);
    r32 = 1;
    co_await Back;
    co_return 0;
  }
};
#undef DAlias
}

namespace NRound168 {
// Pointed into the object by a function it is given to by reference.
void fg_Set(int *&o_pValue, int *_pValue) { o_pValue = _pValue; }
struct CApp : CActor {
  int m_Value;
  TCFuture<int> f_Switch(TCActor<> _Other) {
    int *pValue = nullptr;
    fg_Set(pValue, &m_Value);
    co_await fg_ContinueRunningOnActor(_Other);
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
  }
};
}

namespace NRound170 {
// A copy of a pointer stepped into the object.
struct CApp : CActor {
  int m_Values[4];
  TCFuture<int> f_Stepped(TCActor<> _Other) {
    int *pValue = m_Values;
    int *pNext = ++pValue;
    co_await fg_ContinueRunningOnActor(_Other);
    co_return *pNext;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
  }
};
}

namespace NRound171 {
// Not of the own actor after an alias of it is assigned another.
struct CApp : CActor {
  int m_Value = 0;
  TCFuture<int> f_Run(TCActor<CApp> _Other) {
    auto Own = fg_ThisActor(this);
    auto const &Alias0 = Own, &Alias1 = Alias0, &Alias2 = Alias1,
               &Alias3 = Alias2, &Alias4 = Alias3, &Alias5 = Alias4,
               &Alias6 = Alias5, &Alias7 = Alias6, &Alias8 = Alias7;
    const_cast<TCActor<CApp> &>(Alias8) = _Other;
    co_await fg_ContinueRunningOnActor(Own);
    co_return m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
  }
};
}

namespace NRound173 {
// A cleanup run early through a pointer to its guard, on another actor.
struct CApp : CActor {
  int m_Value;
  TCFuture<int> f_Run(TCActor<> _Other) {
    auto Guard = co_await fg_AsyncDestroy(
        [this]() -> TCFuture<int> { co_return m_Value; });
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
    auto Own = fg_ThisActor(this);
    auto *pGuard = &Guard;
    co_await fg_ContinueRunningOnActor(_Other);
    co_await fg_Move(*pGuard);
    co_await fg_ContinueRunningOnActor(Own);
    co_return 0;
  }
};
}

namespace NRound176 {
void fg_Read(int const *_pValue, int _Other);
int g_Value;
struct CApp : CActor {
  int m_Value;
  // A scope exit made in either value of a conditional.
  TCFuture<int> f_Chosen(TCActor<> _Other, bool _bChoose) {
    auto fCallback = [this] { ++m_Value; };
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
    auto Cleanup = _bChoose ? g_OnScopeExit / fg_Move(fCallback)
                            : g_OnScopeExit / fg_Move(fCallback);
    co_await fg_ContinueRunningOnActor(_Other);
    co_return 0;
  }
  // Moved many times, and cleared.
  TCFuture<int> f_Moved(TCActor<> _Other) {
    auto Cleanup0 = g_OnScopeExit / [this] { ++m_Value; };
    auto Cleanup1 = fg_Move(Cleanup0);
    auto Cleanup2 = fg_Move(Cleanup1);
    auto Cleanup3 = fg_Move(Cleanup2);
    auto Cleanup4 = fg_Move(Cleanup3);
    auto Cleanup5 = fg_Move(Cleanup4);
    Cleanup5.f_Clear();
    co_await fg_ContinueRunningOnActor(_Other);
    co_return 0;
  }
  // Given in either value of a conditional to a call that runs on the other
  // actor.
  TCFuture<int> f_Given(bool _bChoose) {
    fg_Read(_bChoose ? &m_Value : &g_Value,
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
            (co_await fg_ContinueRunningOnActor(fg_BlockingActor()), 0));
    co_return 0;
  }
};
}

namespace NRound177 {
struct CApp : CActor {
  int m_Value;
  // What a lambda gives back of what it is given, and one called through
  // std::invoke.
  TCFuture<int> f_Given(TCActor<> _Other) {
    int *pValue = [](int &_Value) { return &_Value; }(m_Value);
    co_await fg_ContinueRunningOnActor(_Other);
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
  }
  TCFuture<int> f_Invoked(TCActor<> _Other) {
    int *pValue = std::invoke([this] { return &m_Value; });
    co_await fg_ContinueRunningOnActor(_Other);
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
  }
};
}

namespace NRound178 {
struct CApp : CActor {
  int m_Value;
};
extern CApp *const g_pOther;
// A pointer given by reference, which is pointed at another actor.
TCUnsafeFuture<int> fg_Run(CApp *&_pApp, TCActor<> _Other) {
  auto Back = fg_ThisActor(_pApp);
  _pApp = g_pOther;
  co_await fg_ContinueRunningOnActor(_Other);
  co_await fg_ContinueRunningOnActor(Back);
  co_return _pApp->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
}
}

namespace NRound179 {
// Assigned through a pointer to it made with addressof.
struct CApp : CActor {
  int m_Value;
  TCFuture<int> f_Run(TCActor<> _Other) {
    int *pValue = nullptr;
    int **ppValue = __builtin_addressof(pValue);
    *ppValue = &m_Value;
    co_await fg_ContinueRunningOnActor(_Other);
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
  }
};
}

namespace NRound180 {
void fg_Set(int *&o_pValue, int &_Value) { o_pValue = &_Value; }
int g_Value;
struct CApp : CActor {
  int m_Value;
  // Set by a helper to the address of what it is given.
  TCFuture<int> f_Set() {
    int *pValue = nullptr;
    fg_Set(pValue, m_Value);
    co_await fg_ContinueRunningOnActor(fg_BlockingActor());
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
  }
  // Not a value a lambda's local is made with that it replaces before it
  // returns it.
  TCFuture<int> f_Replaced() {
    auto *pValue = [this] {
      auto *pInner = &m_Value;
      pInner = &g_Value;
      return pInner;
    }();
    co_await fg_ContinueRunningOnActor(fg_BlockingActor());
    co_return *pValue;
  }
};
}

namespace NRound181 {
struct CApp : CActor {
  int m_Value = 42;
  // An alias of this changed by inline assembly.
  TCFuture<int> f_Assembly(CApp *_pOther) {
    CApp *pOwn = this;
    asm("" : "=r"(pOwn) : "0"(_pOther));
    auto Target = fg_ThisActor(pOwn);
    co_await fg_ContinueRunningOnActor(Target);
    co_return m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
  }
  // Through the first value of a GNU conditional.
  TCFuture<int> f_Elvis(TCActor<> _Other) {
    int *pValue = &m_Value;
    co_return (pValue ?: nullptr)[(co_await fg_ContinueRunningOnActor(_Other), 0)];
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
  }
};
}

namespace NRound186 {
// A helper that sets the pointer it is given to either of two it is given.
void fg_Select(int *&o_pValue, int *_pFirst, int *_pSecond, bool _bFirst) {
  o_pValue = _bFirst ? _pFirst : _pSecond;
}
struct CApp : CActor {
  int m_Value;
  TCFuture<int> f_Other(TCActor<> _Other, bool _bFirst, int *_pOther) {
    int *pValue = nullptr;
    fg_Select(pValue, _pOther, &m_Value, _bFirst);
    co_await fg_ContinueRunningOnActor(_Other);
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
  }
  TCFuture<int> f_Neither(TCActor<> _Other, bool _bFirst, int *_pOther) {
    int *pValue = nullptr;
    fg_Select(pValue, _pOther, _pOther, _bFirst);
    co_await fg_ContinueRunningOnActor(_Other);
    co_return *pValue;
  }
};
}

namespace NRound192 {
// A helper that sets a pointer in either branch through itself, nested 24
// deep, keeps one set of each parameter to another.
template <unsigned t_Depth>
void fg_Select(int *&o_pValue, int *_pFirst, int *_pSecond, bool _bFirst) {
  if constexpr (t_Depth == 0) {
    o_pValue = _bFirst ? _pFirst : _pSecond;
  } else {
    if (_bFirst)
      fg_Select<t_Depth - 1>(o_pValue, _pFirst, _pSecond, _bFirst);
    else
      fg_Select<t_Depth - 1>(o_pValue, _pSecond, _pFirst, _bFirst);
  }
}
// Only the value a constant condition picks.
template <bool t_bFirst>
void fg_Pick(int *&o_pValue, int *_pFirst, int *_pSecond) {
  o_pValue = t_bFirst ? _pFirst : _pSecond;
}
struct CMyActor : CActor {
  int m_Value;
  TCFuture<int> f_Select(TCActor<> _Other, bool _bFirst) {
    int Local = 7;
    int *pValue = nullptr;
    fg_Select<24>(pValue, &m_Value, &Local, _bFirst);
    co_await fg_ContinueRunningOnActor(_Other);
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
  }
  TCFuture<int> f_PickLocal(TCActor<> _Other) {
    int Local = 7;
    int *pValue = nullptr;
    fg_Pick<true>(pValue, &Local, &m_Value);
    co_await fg_ContinueRunningOnActor(_Other);
    co_return *pValue;
  }
  TCFuture<int> f_PickMember(TCActor<> _Other) {
    int Local = 7;
    int *pValue = nullptr;
    fg_Pick<false>(pValue, &Local, &m_Value);
    co_await fg_ContinueRunningOnActor(_Other);
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
  }
};
}

namespace NRound195 {
// A helper that can throw before it points a pointer elsewhere, called in a
// `try` that catches it, may leave it pointing into the object.
void fg_MayThrow();
void fg_Set(int *&o_pValue, int *_pNew) {
  fg_MayThrow();
  o_pValue = _pNew;
}
struct CMyActor : CActor {
  int m_Value;
  TCFuture<int> f_Caught(TCActor<> _Other) {
    int Local = 7;
    int *pValue = &m_Value;
    try {
      fg_Set(pValue, &Local);
    } catch (...) {
    }
    co_await fg_ContinueRunningOnActor(_Other);
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
  }
  TCFuture<int> f_Uncaught(TCActor<> _Other) {
    int Local = 7;
    int *pValue = &m_Value;
    fg_Set(pValue, &Local);
    co_await fg_ContinueRunningOnActor(_Other);
    co_return *pValue;
  }
};
}

namespace NRound197 {
// An iterator with a destructor into the object.
template <typename t_CKey, typename t_CValue>
struct TCMap {
  struct CIterator {
    t_CValue *mp_pValue;
    ~CIterator() {}
    t_CValue *operator->() const;
  };
  CIterator f_GetIterator();
};
struct CValue {
  int m_Value;
};
struct CMyActor : CActor {
  TCMap<int, CValue> mp_Items;
  TCFuture<int> f_Run(TCActor<> _Other) {
    auto iEntry = mp_Items.f_GetIterator();
    co_await fg_ContinueRunningOnActor(_Other);
    co_return iEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
  }
};
}

namespace NRound198 {
// What a getter of the object can be seen to give back of static storage
// isn't of the object.
struct CMyActor : CActor {
  char const *f_Category() const { return "Downloads"; }
  TCFuture<int> f_Use(TCActor<> _Other) {
    auto pText = f_Category();
    co_await fg_ContinueRunningOnActor(_Other);
    co_return pText[0];
  }
};
}

namespace NRound200 {
// Pointed at an offset into the object by a function it is given to by
// reference.
void fg_Next(int *&o_pValue, int *_pBase) { o_pValue = _pBase + 1; }
struct CApp : CActor {
  int m_Values[2];
  TCFuture<int> f_Switch(TCActor<> _Other) {
    int *pValue = nullptr;
    fg_Next(pValue, m_Values);
    co_await fg_ContinueRunningOnActor(_Other);
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
  }
};
}

namespace NRound205 {
// A reference bound to a cleanup that is moved, which moves nothing, leaves
// it to run when its scope ends.
struct CApp : CActor {
  int m_Value = 0;
  TCFuture<int> f_Alias(TCActor<> _Other) {
    auto Cleanup = g_OnScopeExit / [this] { ++m_Value; };
// CHECK-MESSAGES: :[[@LINE-1]]:36: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
    {
      auto &&Alias = fg_Move(Cleanup);
    }
    co_await fg_ContinueRunningOnActor(_Other);
    co_return 0;
  }
  // What `fg_CallSafe` runs after its arguments, which can switch.
  TCFuture<int> f_CallSafe(TCActor<> _Other) {
    co_return co_await fg_CallSafe(
        [this](int _Unused) -> TCFuture<int> { co_return m_Value; },
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
        (co_await fg_ContinueRunningOnActor(_Other), 0));
  }
};
}

namespace NRound206 {
// The guard of `fg_AsyncDestroy` forwarded as an lvalue keeps its cleanup.
struct CApp : CActor {
  int m_Value;
  TCFuture<int> f_Test(TCActor<> _Other) {
    auto Cleanup = co_await fg_AsyncDestroy([this]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:45: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
      m_Value = 1;
      co_return 0;
    });
    auto &Alias = fg_Forward<decltype(Cleanup) &>(Cleanup);
    co_await fg_ContinueRunningOnActor(_Other);
    co_return 0;
  }
};
}

namespace NRound208 {
void fg_Read(int const *_pValue, int _Other);
struct CMyActor : CActor {
  int m_Value;
  // Only the value a constant condition chooses of a conditional given to a
  // call that runs on the other actor.
  template <bool t_bUseActorData> TCFuture<int> f_Run() {
    int Local = 7;
    fg_Read(t_bUseActorData ? &m_Value : &Local,
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
            (co_await fg_ContinueRunningOnActor(fg_BlockingActor()), 0));
    co_return 0;
  }
  TCFuture<int> f_Literal() {
    int Local = 7;
    fg_Read(false ? &m_Value : &Local,
            (co_await fg_ContinueRunningOnActor(fg_BlockingActor()), 0));
    co_return 0;
  }
};
void fg_Instantiate(CMyActor &_Actor) {
  _Actor.f_Run<false>();
  _Actor.f_Run<true>();
}
}

namespace NRound210 {
// Pointed at the object by a lambda through what it is given, like a
// function.
struct CMyActor : CActor {
  int m_Value;
  TCFuture<int> f_Switch(TCActor<> _Other) {
    int *pValue = nullptr;
    auto fSet = [](int *&o_pValue, int *_pValue) { o_pValue = _pValue; };
    fSet(pValue, &m_Value);
    co_await fg_ContinueRunningOnActor(_Other);
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
  }
  TCFuture<int> f_Local(TCActor<> _Other) {
    int Local = 0;
    int *pValue = &m_Value;
    auto fSet = [](int *&o_pValue, int *_pValue) { o_pValue = _pValue; };
    fSet(pValue, &Local);
    co_await fg_ContinueRunningOnActor(_Other);
    co_return *pValue;
  }
};
}

namespace NRound215 {
NMib::NStorage::TCSharedPointer<NMib::COnScopeExit> fg_OnScopeExitShared(TCFunction<void ()> &&_fCleanup);
// Releasing a shared cleanup runs it when it is the last, but clearing what it
// points to cancels it.
struct CApp : CActor {
  int m_Value = 0;
  TCFuture<int> f_Release(TCActor<> _Other) {
    auto pCleanup = fg_OnScopeExitShared([this] { ++m_Value; });
// CHECK-MESSAGES: :[[@LINE-1]]:42: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
    co_await fg_ContinueRunningOnActor(_Other);
    pCleanup.f_Clear();
    co_return 0;
  }
  TCFuture<int> f_Cancel(TCActor<> _Other) {
    auto pCleanup = fg_OnScopeExitShared([this] { ++m_Value; });
    pCleanup->f_Clear();
    co_await fg_ContinueRunningOnActor(_Other);
    co_return 0;
  }
};
}

namespace NRound217 {
// Calls what it is given before it returns.
template <typename tf_FRead>
int fg_Read(tf_FRead &&_fRead) {
  return _fRead();
}
// Only keeps what it is given.
template <typename tf_FRead>
int fg_Keep(tf_FRead &&_fRead);
struct CApp : CActor {
  int m_Value = 0;
  // A helper that calls the lambda runs it on the actor the coroutine is on
  // after the switch.
  TCFuture<int> f_ReadHelper(TCActor<> _Other) {
    co_await fg_ContinueRunningOnActor(_Other);
    co_return fg_Read([this] { return m_Value; });
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
  }
  TCFuture<int> f_ReadGenericHelper(TCActor<> _Other) {
    co_await fg_ContinueRunningOnActor(_Other);
    co_return [](auto _fRead) { return _fRead(); }([this] { return m_Value; });
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
  }
  TCFuture<int> f_ReadHeld(TCActor<> _Other) {
    auto fRead = [this] { return m_Value; };
    co_await fg_ContinueRunningOnActor(_Other);
    co_return fg_Read(fRead);
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
  }
  // Before the switch it is still on the actor.
  TCFuture<int> f_ReadBefore(TCActor<> _Other) {
    int Value = fg_Read([this] { return m_Value; });
    co_await fg_ContinueRunningOnActor(_Other);
    co_return Value;
  }
  TCFuture<int> f_KeepHelper(TCActor<> _Other) {
    co_await fg_ContinueRunningOnActor(_Other);
    co_return fg_Keep([] { return 1; });
  }
};
}

namespace NRound218 {
// A shared cleanup made with `/` runs like one made by fg_OnScopeExitShared.
struct CApp : CActor {
  int m_Value = 0;
  TCFuture<int> f_Shared(TCActor<> _Other) {
    auto pCleanup = g_OnScopeExitShared / [this] { ++m_Value; };
// CHECK-MESSAGES: :[[@LINE-1]]:43: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
    co_await fg_ContinueRunningOnActor(_Other);
    co_return 0;
  }
  TCFuture<int> f_SharedCancelled(TCActor<> _Other) {
    auto pCleanup = g_OnScopeExitShared / [this] { ++m_Value; };
    pCleanup->f_Clear();
    co_await fg_ContinueRunningOnActor(_Other);
    co_return 0;
  }
};
}

namespace NRound225 {
// A use of what a captured pointer points to that never runs.
struct CApp : CActor {
  int m_Value = 0;
  TCFuture<int> f_Discarded() {
    auto *pValue = &m_Value;
    auto fRead = [pValue] {
      if constexpr (false)
        return *pValue;
      return 0;
    };
    co_await fg_ContinueRunningOnActor(fg_BlockingActor());
    co_return fRead();
  }
  TCFuture<int> f_NotCalled() {
    auto *pValue = &m_Value;
    auto fRead = [pValue] {
      auto fInner = [pValue] { return *pValue; };
      (void)fInner;
      return 0;
    };
    co_await fg_ContinueRunningOnActor(fg_BlockingActor());
    co_return fRead();
  }
  TCFuture<int> f_Used() {
    auto *pValue = &m_Value;
    auto fRead = [pValue] { return *pValue; };
    co_await fg_ContinueRunningOnActor(fg_BlockingActor());
    co_return fRead();
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
  }
};
}

namespace NRound228 {
// A local function wrapper moved where it is called holds the lambda it is
// made with.
struct CApp : CActor {
  int mp_Value = 0;
  TCFuture<int> f_Run(TCActor<> _Other) {
    TCFunction<TCFuture<int>()> fRead = [this]() -> TCFuture<int> {
      co_return mp_Value;
    };
    co_await fg_ContinueRunningOnActor(fg_Move(_Other));
    co_return co_await fg_CallSafe(fg_Move(fRead));
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
  }
};
}

namespace NRound231 {
// A switch back forwarded or copied into another keeps where it switches to.
struct CApp : CActor {
  int m_Value = 0;
  TCFuture<int> f_Forwarded(TCActor<> _Other) {
    auto Own = fg_ThisActor(this);
    auto Back = fg_ContinueRunningOnActor(Own);
    auto Pending = fg_Forward<decltype(Back)>(Back);
    co_await fg_ContinueRunningOnActor(_Other);
    co_await Pending;
    co_return m_Value;
  }
  TCFuture<int> f_Copied(TCActor<> _Other) {
    auto Own = fg_ThisActor(this);
    auto Back = fg_ContinueRunningOnActor(Own);
    auto Pending = Back;
    co_await fg_ContinueRunningOnActor(_Other);
    co_await Pending;
    co_return m_Value;
  }
  TCFuture<int> f_CopiedAway(TCActor<> _Other) {
    auto Away = fg_ContinueRunningOnActor(_Other);
    auto Pending = Away;
    co_await Pending;
    co_return m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
  }
};
}

namespace NRound233 {
// What a function sets a pointer it is given the address of to.
void fg_Set(int **_ppTarget, int *_pSource) { *_ppTarget = _pSource; }
struct CApp : CActor {
  int m_Value = 0;
  TCFuture<int> f_Run(TCActor<> _Other) {
    int *pValue = nullptr;
    fg_Set(&pValue, &m_Value);
    co_await fg_ContinueRunningOnActor(_Other);
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
  }
};
}

namespace NRound233Cast {
// A local function wrapper moved by a cast to an rvalue reference holds the
// lambda it is made with.
struct CApp : CActor {
  int mp_Value = 0;
  TCFuture<int> f_Run(TCActor<> _Other) {
    TCFunction<TCFuture<int>()> fRead = [this]() -> TCFuture<int> {
      co_return mp_Value;
    };
    co_await fg_ContinueRunningOnActor(fg_Move(_Other));
    co_return co_await fg_CallSafe(
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
        static_cast<TCFunction<TCFuture<int>()> &&>(fRead));
  }
};
}

namespace NRound237 {
// A switch kept in a local and awaited through a reference to it.
struct CApp : CActor {
  int m_Value;
  TCFuture<int> f_Run() {
    auto Own = fg_ThisActor(this);
    co_await fg_ContinueRunningOnActor(fg_BlockingActor());
    co_return co_await fg_CallSafe([this, Own]() -> TCFuture<int> {
      auto Back = fg_ContinueRunningOnActor(Own);
      auto &Alias = Back;
      co_await fg_Move(Alias);
      co_return m_Value;
    });
  }
};
}

// An unchanged local that holds an empty actor is replaced with the current
// one too.
struct CRound243Timer : CActor
{
	int m_Value = 0;

	TCFuture<int> f_Run(TCActor<> _Other)
	{
		TCActor<> Empty;
		co_await fg_ContinueRunningOnActor(_Other);
		fg_RegisterTimer
			(
				1.0
				, [this]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
				{
					co_return m_Value;
				}
				, Empty
			)
		;
		co_return 0;
	}
};

namespace NRound244 {
NMib::NStorage::TCSharedPointer<NMib::COnScopeExit> fg_OnScopeExitShared(TCFunction<void ()> &&_fCleanup);
// A copy of a shared cleanup keeps it from running when another is released.
struct CApp : CActor {
  int m_Value = 0;
  TCFuture<int> f_Kept(TCActor<> _Other) {
    auto pCleanup = fg_OnScopeExitShared([this] { ++m_Value; });
// CHECK-MESSAGES: :[[@LINE-1]]:42: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
    auto pKeep = pCleanup;
    pCleanup.f_Clear();
    co_await fg_ContinueRunningOnActor(_Other);
    co_return 0;
  }
  TCFuture<int> f_Back(TCActor<> _Other) {
    auto ThisActor = fg_ThisActor(this);
    auto pCleanup = fg_OnScopeExitShared([this] { ++m_Value; });
    auto pKeep = pCleanup;
    co_await fg_ContinueRunningOnActor(_Other);
    pCleanup.f_Clear();
    co_await fg_ContinueRunningOnActor(ThisActor);
    co_return 0;
  }
};
}

namespace NRound245 {
// A call through an unchanged local reference to a function wrapper runs the
// lambda it holds.
struct CApp : CActor {
  int m_Value = 0;
  TCFuture<int> f_Read(TCActor<> _Other) {
    TCFunction<int ()> fRead = [this] { return m_Value; };
    auto &fAlias = fRead;
    co_await fg_ContinueRunningOnActor(_Other);
    co_return fAlias();
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
  }
};
}
