#pragma once

// A minimal model of the Malterlib concurrency API for the coroutine checks.

namespace std
{
	template <typename t_CPromise = void>
	struct coroutine_handle
	{
		static coroutine_handle from_address(void *);
		void *address() const;
		template <typename t_COther>
		coroutine_handle(coroutine_handle<t_COther>);
		coroutine_handle();
	};

	template <typename t_CReturn, typename ...tp_CArgs>
	struct coroutine_traits
	{
		using promise_type = typename t_CReturn::promise_type;
	};

	struct suspend_never
	{
		bool await_ready() const noexcept { return true; }
		void await_suspend(coroutine_handle<>) const noexcept {}
		void await_resume() const noexcept {}
	};

	template <typename t_CType>
	t_CType &&move(t_CType &_Value);
}

namespace NMib
{
	template <typename t_CType>
	t_CType &&fg_Move(t_CType &_Value)
	{
		return static_cast<t_CType &&>(_Value);
	}

	template <typename t_CType>
	t_CType &&fg_Move(t_CType &&_Value)
	{
		return static_cast<t_CType &&>(_Value);
	}

	template <typename t_CType, typename t_CNew>
	t_CType fg_Exchange(t_CType &_Value, t_CNew &&_New);

	template <typename t_CType>
	t_CType &&fg_Forward(t_CType &_Value)
	{
		return static_cast<t_CType &&>(_Value);
	}
}

namespace NMib::NStorage
{
	template <typename t_CType>
	struct TCSharedPointer;
}

namespace NMib::NConcurrency
{
	enum ECoroutineFlag
	{
		ECoroutineFlag_None = 0
		, ECoroutineFlag_AllowReferences = 1
		, ECoroutineFlag_CaptureExceptions = 2
	};

	// Like the library's, whose await_ready can't be seen to be ready, so the
	// await suspends.
	template <typename t_CType>
	struct TCAwaiter
	{
		bool await_ready() const noexcept;
		void await_suspend(std::coroutine_handle<>) const noexcept {}
		t_CType await_resume() const noexcept { return {}; }
	};
	using CAwaiter = TCAwaiter<int>;

	// Suspends, and resumes with an exception or a value, like awaiting a
	// future.
	struct CThrowingAwaiter
	{
		bool await_ready() const noexcept { return false; }
		void await_suspend(std::coroutine_handle<>) const noexcept {}
		int await_resume() const;
	};

	template <typename t_CType, ECoroutineFlag t_Flags>
	struct TCFutureWithFlags
	{
		struct promise_type
		{
			TCFutureWithFlags get_return_object();
			std::suspend_never initial_suspend();
			std::suspend_never final_suspend() noexcept;
			void return_value(t_CType);
			void unhandled_exception();
			CAwaiter await_transform(ECoroutineFlag);
			template <typename t_CAwaitable>
			t_CAwaitable &&await_transform(t_CAwaitable &&_Awaitable)
			{
				return static_cast<t_CAwaitable &&>(_Awaitable);
			}
		};

		TCAwaiter<t_CType> operator co_await () &&
		{
			return {};
		}

		t_CType f_CallSync() &&;
		t_CType f_CallSync(double _Timeout) &&;

		template <typename t_CMessage>
		TCFutureWithFlags operator % (t_CMessage const &) &&
		{
			return {};
		}
	};

	template <typename t_CType>
	struct TCFuture : TCFutureWithFlags<t_CType, ECoroutineFlag_None>
	{
		struct promise_type
		{
			TCFuture get_return_object();
			std::suspend_never initial_suspend();
			std::suspend_never final_suspend() noexcept;
			void return_value(t_CType);
			void unhandled_exception();
			CAwaiter await_transform(ECoroutineFlag);
			template <typename t_CAwaitable>
			t_CAwaitable &&await_transform(t_CAwaitable &&_Awaitable)
			{
				return static_cast<t_CAwaitable &&>(_Awaitable);
			}
		};

		TCAwaiter<t_CType> operator co_await () &&
		{
			return {};
		}

		t_CType f_CallSync() &&;
		t_CType f_CallSync(double _Timeout) &&;
		template <typename t_CRunLoop>
		t_CType f_CallSync(NStorage::TCSharedPointer<t_CRunLoop> const &_pRunLoop, double _Timeout = -1.0) &&;
		TCFuture f_Dispatch() &&;
		TCFuture f_Wrap() &&;
		TCFuture &&f_Move()
		{
			return static_cast<TCFuture &&>(*this);
		}

		template <typename t_CMessage>
		TCFuture operator % (t_CMessage const &) &&
		{
			return {};
		}
	};

	template <typename t_CType>
	using TCUnsafeFuture = TCFutureWithFlags<t_CType, ECoroutineFlag_AllowReferences>;

	template <typename t_CActor>
	struct TCActor;

	struct CDispatchHelperWithActor;

	struct CActor;

	struct CThisActor
	{
		template <typename t_FFunction>
		TCFuture<int> operator / (t_FFunction &&) const
		{
			return {};
		}

		operator TCActor<CActor> () const;

		template <typename t_FFunction, typename ...tp_CParams>
		TCFuture<int> f_Invoke(t_FFunction &&, tp_CParams &&...) const
		{
			return {};
		}
	};

	// What CActor::f_Dispatch runs, like a `TCFunctionMovable<void ()>`.
	struct CDispatchedFunction
	{
		template <typename t_FFunction>
		CDispatchedFunction(t_FFunction &&_fFunction);
		void operator ()();
	};

	struct CActor
	{
		CThisActor self;

		template <typename t_CReturn, typename ...tp_CParams>
		t_CReturn f_DispatchWithReturn(int);
		TCFuture<int> f_Dispatch(CDispatchedFunction &&_fToDispatch);
	};

	struct CActorInternal
	{
	};

	struct CAllowUnsafeThis
	{
	};

	template <typename t_CActor = CActor>
	struct TCActor;

	template <typename t_CActor = CActor>
	struct TCWeakActor
	{
		TCActor<t_CActor> f_Lock() const;
	};

	template <typename t_CActor>
	struct TCActor
	{
		TCActor() = default;
		TCWeakActor<t_CActor> f_Weak() const;
		template <typename t_COther>
		TCActor(TCActor<t_COther> const &)
		{
		}

		template <typename t_FFunction>
		TCFuture<int> operator / (t_FFunction &&) const
		{
			return {};
		}

		void f_Clear();
		CDispatchHelperWithActor f_Dispatch() const;

		template <typename t_FFunction>
		TCFuture<int> f_Bind(t_FFunction &&) const
		{
			return {};
		}

		template <auto t_pFunction, typename t_FFunction, typename ...tp_CParams>
		TCFuture<int> f_Bind(t_FFunction &&, tp_CParams &&...) const
		{
			return {};
		}

		template <auto t_pFunction, int t_Options = 0, typename t_FFunction, typename ...tp_CParams>
		TCFuture<int> f_BindByValue(t_FFunction &&, tp_CParams &&...) const
		{
			return {};
		}

		template <typename t_CMemberFunction, typename ...tp_CParams>
		TCFuture<int> operator () (t_CMemberFunction, tp_CParams &&...) const
		{
			return {};
		}
	};

	template <typename tf_CActor>
	TCActor<tf_CActor> fg_ThisActor(tf_CActor const *_pActor);

	struct CBlockingActorCheckout
	{
	};
	CBlockingActorCheckout fg_BlockingActor();

	struct CDispatchHelperWithActor
	{
		template <typename t_FFunction>
		TCFuture<int> operator / (t_FFunction &&) const
		{
			return {};
		}
	};

	struct CDispatchHelper
	{
		template <typename t_FFunction>
		TCFuture<int> operator / (t_FFunction &&) const
		{
			return {};
		}
		CDispatchHelperWithActor operator () (TCActor<> const &) const;
		CDispatchHelperWithActor operator () (CBlockingActorCheckout const &) const;
	};

	extern CDispatchHelper const &g_Dispatch;

	struct CConcurrentDispatchHelper
	{
		template <typename t_FFunction>
		TCFuture<int> operator / (t_FFunction &&) const
		{
			return {};
		}
	};

	extern CConcurrentDispatchHelper const &g_ConcurrentDispatch;

	struct CSuspendNever
	{
		bool await_ready() const noexcept
		{
			return true;
		}
		void await_suspend(std::coroutine_handle<>) const noexcept
		{
		}
		void await_resume() const noexcept
		{
		}
	};

	struct CCaptureExceptionScope
	{
		~CCaptureExceptionScope();
	};

	struct CCaptureExceptionScopeAwaiter : public CSuspendNever
	{
		CCaptureExceptionScope await_resume() const noexcept;
	};

	struct CCaptureExceptionsHelper
	{
		CCaptureExceptionScopeAwaiter operator co_await () const noexcept;
	};
	extern CCaptureExceptionsHelper g_CaptureExceptions;

	struct CCoroutineTransferOwnershipAwaiter
	{
		bool await_ready() const noexcept;
		void await_suspend(std::coroutine_handle<>) const noexcept;
		void await_resume() const noexcept;
	};

	struct CCoroutineTransferOwnership
	{
		CCoroutineTransferOwnershipAwaiter operator co_await();
	};
	CCoroutineTransferOwnership fg_ContinueRunningOnActor(TCActor<> const &_Actor);
	CCoroutineTransferOwnership fg_ContinueRunningOnActor(CBlockingActorCheckout const &_Checkout);

	struct CAsyncDestroy
	{
		CAsyncDestroy(CAsyncDestroy &&);
		~CAsyncDestroy();
		void f_Clear();
		CAwaiter operator co_await () &&;
		CAwaiter f_Wrap() &&;
	};

	struct CAsyncDestroyAwaiter
	{
		bool await_ready() const noexcept;
		void await_suspend(std::coroutine_handle<>) const noexcept;
		CAsyncDestroy await_resume() noexcept;
	};

	struct CActorSubscription
	{
	};

	struct CActorSubscriptionHelperWithActor
	{
		template <typename t_FCleanup>
		CActorSubscription operator / (t_FCleanup &&) const;
	};

	struct CActorSubscriptionHelper
	{
		template <typename t_FCleanup>
		CActorSubscription operator / (t_FCleanup &&) const;
		CActorSubscriptionHelperWithActor operator () (TCActor<> const &_Actor) const;
	};

	struct CBlockingActorSubscriptionHelper
	{
		template <typename t_FCleanup>
		CActorSubscription operator / (t_FCleanup &&) const;
	};

	extern CActorSubscriptionHelper const &g_ActorSubscription;
	extern CBlockingActorSubscriptionHelper const &g_BlockingActorSubscription;

	template <typename t_FDestroy>
	CAsyncDestroyAwaiter fg_AsyncDestroy(t_FDestroy &&_fDestroy)
	{
		return {};
	}

	template <typename t_FDestroy>
	CAsyncDestroyAwaiter fg_AsyncDestroyByValue(t_FDestroy _fDestroy)
	{
		return {};
	}

	// Like the library, which gives the object back by an rvalue reference,
	// for a variable to be made of it.
	template <typename t_CThis>
	struct TCMoveThis
	{
		bool await_ready() const noexcept { return true; }
		void await_suspend(std::coroutine_handle<>) const noexcept {}
		t_CThis await_resume() const noexcept;
	};

	template <typename t_CThis>
	TCMoveThis<t_CThis &&> fg_MoveThis(t_CThis &_This)
	{
		return {};
	}

	template <typename t_FCallable>
	TCFuture<int> fg_CallSafe(t_FCallable &&_fCallable)
	{
		return {};
	}

	template <typename t_FCallable, typename t_CParam>
	TCFuture<int> fg_CallSafe(t_FCallable &&_fCallable, t_CParam &&_Param)
	{
		return {};
	}

	template <typename t_FFunction>
	TCFuture<int> fg_Dispatch(t_FFunction &&_fFunction)
	{
		return {};
	}

	template <typename t_FFunction>
	TCFuture<int> fg_Dispatch(TCActor<> const &_Actor, t_FFunction &&_fFunction)
	{
		return {};
	}

	template <typename t_FFunction>
	TCFuture<int> fg_ConcurrentDispatch(t_FFunction &&_fFunction)
	{
		return {};
	}

	template <typename t_FFunction>
	TCFuture<int> fg_CallSafeDispatched(t_FFunction &&_fFunction)
	{
		return {};
	}

	template <typename t_FFunction>
	TCFuture<int> fg_CallSafeDispatchedOn(TCActor<> const &_Actor, t_FFunction &&_fFunction)
	{
		return {};
	}
}

namespace NMib::NStorage
{
	template <typename t_CType>
	struct TCSharedPointer
	{
		TCSharedPointer();
		TCSharedPointer(t_CType *_pObject);
		~TCSharedPointer();
		t_CType *operator -> () const;
		t_CType &operator * () const;
		t_CType *f_Detach();
		void f_Clear();
	};

	template <typename t_CType>
	struct TCWeakPointer
	{
		TCWeakPointer();
		TCWeakPointer(t_CType *_pObject);
	};
}

namespace NMib::NFunction
{
	template <typename t_CSignature>
	struct TCFunction;

	template <typename t_CReturn, typename ...tp_CParams>
	struct TCFunction<t_CReturn (tp_CParams...)>
	{
		template <typename t_FFunction>
		TCFunction(t_FFunction &&_fFunction)
		{
		}
		t_CReturn operator () (tp_CParams...) const;
	};
}

namespace NMib
{
	template <typename t_CType>
	t_CType *fg_Explicit(t_CType *_pObject)
	{
		return _pObject;
	}

	template <typename t_CType>
	NStorage::TCSharedPointer<t_CType> fg_ToSharedPointer(t_CType &&_Object)
	{
		return {};
	}
}

namespace NMib
{
	struct COnScopeExit
	{
		~COnScopeExit();
		void operator () ();
		void f_Clear();
		void f_ClearFunctor();
	};

	struct COnScopeExitHelper
	{
		template <typename t_FFunction>
		COnScopeExit operator / (t_FFunction &&) const
		{
			return {};
		}
	};

	extern COnScopeExitHelper const &g_OnScopeExit;

	// Runs the callback when it is destroyed, also while an exception
	// unwinds.
	struct COnScopeExitHelperWithException
	{
		template <typename t_FFunction>
		COnScopeExit operator / (t_FFunction &&) const
		{
			return {};
		}
	};

	extern COnScopeExitHelperWithException const &g_OnScopeExitWithException;

	// Runs the callback when it is destroyed, and catches what it throws.
	struct COnScopeExitCatchHelper
	{
		template <typename t_FFunction>
		COnScopeExit operator / (t_FFunction &&) const
		{
			return {};
		}
	};

	extern COnScopeExitCatchHelper const &g_OnScopeExitCatch;

	// Makes a shared pointer to the guard, which runs the callback when the
	// last one is released.
	struct COnScopeExitSharedHelper
	{
		template <typename t_FFunction>
		NStorage::TCSharedPointer<COnScopeExit> operator / (t_FFunction &&) const
		{
			return {};
		}
	};

	extern COnScopeExitSharedHelper const &g_OnScopeExitShared;
}

namespace NMib::NConcurrency
{
	// The callback runs on the actor, by default the current one, through a
	// weak actor functor.
	TCFuture<int> fg_RegisterTimer(double _Period, NFunction::TCFunction<TCFuture<int> ()> &&_fCallback, TCActor<> const &_Actor = TCActor<>());
	TCFuture<int> fg_OneshotTimerAbortable(double _Period, NFunction::TCFunction<TCFuture<int> ()> &&_fCallback, TCActor<> const &_Actor = TCActor<>());

	// Dispatches the callback, when the scope exit runs, to the actor given,
	// or to the actor that is current where it is made.
	struct COnScopeExitShared
	{
		~COnScopeExitShared();
	};

	struct COnScopeExitActorHelperWithActor
	{
		template <typename tf_FOnScopeExit>
		COnScopeExitShared operator / (tf_FOnScopeExit &&_fOnExitFunctor) &&
		{
			return {};
		}
	};

	struct COnScopeExitActorHelper
	{
		template <typename tf_FOnScopeExit>
		COnScopeExitShared operator / (tf_FOnScopeExit &&_fOnExitFunctor) const
		{
			return {};
		}

		COnScopeExitActorHelperWithActor operator () (TCActor<> const &_Actor) const;
	};

	extern COnScopeExitActorHelper const &g_OnScopeExitActor;
}

using namespace NMib;
using namespace NMib::NConcurrency;
using namespace NMib::NStorage;
using namespace NMib::NFunction;
