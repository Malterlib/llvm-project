// RUN: %check_clang_tidy -std=c++20-or-later %s malterlib-coroutine-lambda-call %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -I %S/Inputs

#include "malterlib-concurrency.h"

TCFuture<int> fg_Suspend();
TCFuture<int> fg_Get(int _Value);
void fg_Store(TCFunction<TCFuture<int> ()> &&_fFunction);

template <typename t_FFunction>
void fg_StoreGeneric(t_FFunction &&_fFunction)
{
}

// An awaiter that suspends, and reads itself when the coroutine resumes.
struct CBorrowingAwaiter
{
	bool await_ready() const noexcept
	{
		return false;
	}
	void await_suspend(std::coroutine_handle<>) const noexcept
	{
	}
	int await_resume() const noexcept
	{
		return *m_pValue;
	}

	int const *m_pValue = nullptr;
};

struct CText
{
	char const *m_pData = nullptr;
};

template <typename t_CChar>
struct TCStrSpan
{
	TCStrSpan(CText const &_Text);
	t_CChar const *m_pData;
};
using CStrSpan = TCStrSpan<char>;

int const &fg_Identity(int const &_Value);
CBorrowingAwaiter fg_MakeAwaiter(int const &_Value);
TCFuture<int> fg_Write(int const &_Value);

struct CGuard
{
	CGuard(int const &_Value);
	~CGuard();
};

struct CData
{
	CBorrowingAwaiter f_Read() const;
	int f_Length() const;
	CText f_Text() const;
};

void fg_UseActor(TCActor<> const &_Actor);

TCFuture<int> fg_Calls()
{
	int Value = 0;

	co_await [Value]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: a lambda coroutine that uses its captures after a suspension is called directly; the captures dangle when the closure is a temporary, so run it through fg_CallSafe, g_Dispatch, 'self /' or an actor functor [malterlib-coroutine-lambda-call]
		{
			co_await fg_Suspend();
			co_return Value;
		}
		()
	;

	auto fHeld = [Value]() -> TCFuture<int>
		{
			co_await fg_Suspend();
			co_return Value;
		}
	;
	using CResult = decltype(fHeld());
	co_await fHeld();
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: a lambda coroutine that uses its captures after a suspension is called directly; {{.*}}

	// Without captures there is nothing to dangle.
	co_await []() -> TCFuture<int>
		{
			co_await fg_Suspend();
			co_return 0;
		}
		()
	;

	// A direct call runs the lambda until it first suspends, while the
	// closure is there, so it may use its captures until then.
	co_await [Value]() -> TCFuture<int>
		{
			int Copy = Value;
			co_await fg_Suspend();
			co_return Copy;
		}
		()
	;

	// A reference into a capture dangles with the closure too.
	co_await [Value]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: a lambda coroutine that uses its captures after a suspension is called directly; {{.*}}
		{
			int const &Alias = Value;
			int const *pAlias = nullptr;
			pAlias = &Alias;
			co_await fg_Suspend();
			co_return *pAlias;
		}
		()
	;

	// Through casts, moves and views too.
	co_await [Value]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: a lambda coroutine that uses its captures after a suspension is called directly; {{.*}}
		{
			auto *pAlias = static_cast<int const *>(fg_Move(&Value));
			co_await fg_Suspend();
			co_return *pAlias;
		}
		()
	;
	co_await [Text = CText{}]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: a lambda coroutine that uses its captures after a suspension is called directly; {{.*}}
		{
			CStrSpan Span = Text;
			co_await fg_Suspend();
			co_return Span.m_pData ? 1 : 0;
		}
		()
	;
	// Before the first suspension a capture may only be used as a value, so a
	// pointer to it counts even if it is pointed elsewhere later.
	co_await [Value]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: a lambda coroutine that uses its captures after a suspension is called directly; {{.*}}
		{
			int Local = Value;
			int const *pValue = &Value;
			pValue = &Local;
			co_await fg_Suspend();
			co_return *pValue;
		}
		()
	;
	// An awaiter that refers to a capture uses it when the coroutine resumes.
	co_await [Value]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: a lambda coroutine that uses its captures after a suspension is called directly; {{.*}}
		{
			co_return co_await CBorrowingAwaiter{&Value};
		}
		()
	;
	co_await [Awaiter = CBorrowingAwaiter{}]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: a lambda coroutine that uses its captures after a suspension is called directly; {{.*}}
		{
			co_return co_await Awaiter;
		}
		()
	;

	// What keeps a reference: a reference returned by a function, an RAII
	// object, an awaiter made from it or a named awaiter.
	co_await [Value]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: a lambda coroutine that uses its captures after a suspension is called directly; {{.*}}
		{
			int const &Alias = fg_Identity(Value);
			co_await fg_Suspend();
			co_return Alias;
		}
		()
	;
	co_await [Value]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: a lambda coroutine that uses its captures after a suspension is called directly; {{.*}}
		{
			CGuard Guard(Value);
			co_await fg_Suspend();
			co_return 0;
		}
		()
	;
	co_await [Value]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: a lambda coroutine that uses its captures after a suspension is called directly; {{.*}}
		{
			co_return co_await fg_MakeAwaiter(Value);
		}
		()
	;
	co_await [Value]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: a lambda coroutine that uses its captures after a suspension is called directly; {{.*}}
		{
			CBorrowingAwaiter Awaiter{&Value};
			co_return co_await Awaiter;
		}
		()
	;
	co_await [Data = CData{}]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: a lambda coroutine that uses its captures after a suspension is called directly; {{.*}}
		{
			co_return co_await Data.f_Read();
		}
		()
	;
	// Exchanged values keep nothing.
	co_await [pValue = &Value]() mutable -> TCFuture<int>
		{
			int *pOld = fg_Exchange(pValue, nullptr);
			co_await fg_Suspend();
			co_return *pOld;
		}
		()
	;

	// Values, objects returned by value, handles, and functions that return a
	// TCFuture, keep nothing.
	co_await [Value, Data = CData{}, Actor = TCActor<>()]() -> TCFuture<int>
		{
			int Length = Data.f_Length();
			CText Text = Data.f_Text();
			fg_UseActor(Actor);
			co_await fg_Write(Value);
			co_return Length;
		}
		()
	;

	// What the first `co_await` awaits runs before it suspends.
	co_await [Value]() -> TCFuture<int>
		{
			co_return co_await fg_Get(Value);
		}
		()
	;

	// A use in a loop can run after the suspension of an earlier iteration.
	co_await [Value]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: a lambda coroutine that uses its captures after a suspension is called directly; {{.*}}
		{
			int Sum = 0;
			for (int iTry = 0; iTry < 2; ++iTry)
				Sum += co_await fg_Get(Value);
			co_return Sum;
		}
		()
	;

	// A lambda the coroutine makes can run whenever it is called.
	co_await [Value]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: a lambda coroutine that uses its captures after a suspension is called directly; {{.*}}
		{
			auto fGet = [&]
				{
					return Value;
				}
			;
			co_await fg_Get(0);
			co_return fGet();
		}
		()
	;

	co_await fg_CallSafe
		(
			[Value]() -> TCFuture<int>
			{
				co_await fg_Suspend();
				co_return Value;
			}
		)
	;

	co_await
		(
			g_Dispatch / [Value]() -> TCFuture<int>
			{
				co_await fg_Suspend();
				co_return Value;
			}
		)
	;

	co_await fg_Dispatch
		(
			[Value]() -> TCFuture<int>
			{
				co_await fg_Suspend();
				co_return Value;
			}
		)
	;

	co_await TCActor<>().f_Bind<&CActor::f_DispatchWithReturn<TCFuture<int>>>
		(
			[Value]() -> TCFuture<int>
			{
				co_await fg_Suspend();
				co_return Value;
			}
		)
	;

	co_await (g_Dispatch / fg_Move(fHeld));

	// Other uses can call the closure directly, or keep it after the captures
	// are gone.
	fg_Store
		(
			[Value]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-3]]:2: warning: the closure of a lambda coroutine that uses its captures after a suspension is given to 'fg_Store', which can call it directly; give it only to fg_CallSafe, g_Dispatch, 'self /', an actor functor or another function that keeps it alive while it runs [malterlib-coroutine-lambda-call]
			{
				co_await fg_Suspend();
				co_return Value;
			}
		)
	;

	// A local function wrapper holds the closure; where it goes tells.
	TCFunction<TCFuture<int> ()> fFunction = [Value]() -> TCFuture<int>
		{
			co_await fg_Suspend();
			co_return Value;
		}
	;

	TCFunction<TCFuture<int> ()> fAssigned = fFunction;
	fg_Store(fg_Move(fAssigned));
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: the closure of a lambda coroutine that uses its captures after a suspension is given to 'fg_Store', which can call it directly; {{.*}}
	fAssigned = [Value]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: the closure of a lambda coroutine that uses its captures after a suspension is given to a 'TCFunction', which can call it directly; {{.*}}
		{
			co_await fg_Suspend();
			co_return Value;
		}
	;

	auto fStored = [Value]() -> TCFuture<int>
		{
			co_await fg_Suspend();
			co_return Value;
		}
	;
	fg_StoreGeneric(fg_Move(fStored));
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: the closure of a lambda coroutine that uses its captures after a suspension is given to 'fg_StoreGeneric', which can call it directly; {{.*}}

	// A closure that doesn't use its captures after a suspension can go
	// anywhere.
	fg_Store
		(
			[Value]() -> TCFuture<int>
			{
				co_return Value;
			}
		)
	;

	auto fUnsafe = [Value]() -> TCUnsafeFuture<int>
		{
			co_await fg_Suspend();
			co_return Value;
		}
	;
	co_await fUnsafe();
	fg_StoreGeneric(fUnsafe);

	co_return 0;
}

auto fg_Returned(int _Value)
{
	return [_Value]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: the closure of a lambda coroutine that uses its captures after a suspension is given to the caller, which can call it directly; {{.*}}
		{
			co_await fg_Suspend();
			co_return _Value;
		}
	;
}

// The shapes of the runtime checks of Malterlib's lambda reference tests.
struct CTestActor : CActor
{
	TCActor<> m_TestActor;

	TCFuture<int> f_TestLambdaReference()
	{
		int Value = co_await [=, this, Value2 = 20]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a lambda coroutine that uses its captures after a suspension is called directly; {{.*}}
			{
				int Value = co_await (m_TestActor / [] { return 5; });
				co_return Value + Value2;
			}
			()
		;
		co_return Value;
	}

	TCFuture<int> f_TestLambdaReferenceRecursive()
	{
		int Value = co_await
			(
				self / [=, this, Value2 = 20]() -> TCFuture<int>
				{
					co_return co_await [=, this]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:25: warning: a lambda coroutine that uses its captures after a suspension is called directly; {{.*}}
						{
							int Value = co_await (m_TestActor / [] { return 5; });
							co_return Value + Value2;
						}
						()
					;
				}
			)
		;
		co_return Value;
	}

	TCFuture<int> f_TestLambdaReferenceCorrected()
	{
		int Value = co_await
			(
				self / [=, this, Value2 = 20]() -> TCFuture<int>
				{
					int Value = co_await (m_TestActor / [] { return 5; });
					co_return Value + Value2;
				}
			)
		;
		co_return Value;
	}

	TCFuture<int> f_TestLambdaReferenceCorrectedDispatched()
	{
		int Value = co_await
			(
				g_Dispatch / [=, this, Value2 = 20]() -> TCFuture<int>
				{
					int Value = co_await (m_TestActor / [] { return 5; });
					co_return Value + Value2;
				}
			)
		;
		co_return Value;
	}

	TCFuture<int> f_TestLambdaReferenceNoCoroDirectReturn()
	{
		co_return co_await [=, this, Value2 = 20]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: a lambda coroutine that uses its captures after a suspension is called directly; {{.*}}
			{
				int Value = co_await (m_TestActor / [] { return 5; });
				co_return Value + Value2;
			}
			()
		;
	}

	TCFuture<int> f_TestLambdaReferenceNoCoroDirectReturnRecursive()
	{
		co_return co_await
			(
				self / [=, this, Value2 = 20]() -> TCFuture<int>
				{
					co_return co_await
						(
							[=, this]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: a lambda coroutine that uses its captures after a suspension is called directly; {{.*}}
							{
								int Value = co_await (m_TestActor / [] { return 5; });
								co_return Value + Value2;
							}
							()
						)
					;
				}
			)
		;
	}

	TCFuture<int> f_TestLambdaReferenceNoCoroDispatched()
	{
		TCFuture<int> Future = [=, this, Value2 = 20]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:26: warning: a lambda coroutine that uses its captures after a suspension is called directly; {{.*}}
			{
				int Value = co_await (m_TestActor / [] { return 5; });
				co_return Value + Value2;
			}
			()
		;
		co_return co_await fg_Move(Future);
	}

	TCFuture<int> f_TestLambdaReferenceNoCoroDispatchedCorrected()
	{
		TCFuture<int> Future = self / [=, this, Value2 = 20]() -> TCFuture<int>
			{
				int Value = co_await (m_TestActor / [] { return 5; });
				co_return Value + Value2;
			}
		;
		co_return co_await fg_Move(Future);
	}
};

// `operator /` through a dependent type.
template <typename t_CActor>
TCFuture<int> fg_DependentDispatch(t_CActor const &_Actor, int _Value)
{
	co_return co_await
		(
			_Actor / [_Value]() -> TCFuture<int>
			{
				co_await fg_Suspend();
				co_return _Value;
			}
		)
	;
}

// In a template, the type of a local that holds a closure can be undeduced.
template <typename t_CType>
TCFuture<int> fg_TemplateLocal(t_CType _Value)
{
	auto fLambda = [_Value]() -> TCFuture<int>
		{
			co_await fg_Suspend();
			co_return _Value;
		}
	;
	co_return co_await fg_Dispatch(fg_Move(fLambda));
}

// Unevaluated operands don't make or call the closure.
using CUnevaluated = decltype
	(
		fg_StoreGeneric
		(
			[Value = 0]() -> TCFuture<int>
			{
				co_await fg_Suspend();
				co_return Value;
			}
		)
	)
;

// A copy of the object, `[*this]`, lives in the closure.
struct CCopied
{
	TCFuture<int> f_Run()
	{
		return [*this]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: a lambda coroutine that uses its captures after a suspension is called directly; {{.*}}
			{
				int const *pValue = &m_Value;
				co_await fg_Suspend();
				co_return *pValue;
			}
			()
		;
	}

	int m_Value = 0;
};

// fg_Forward passes the closure on.
TCFuture<int> fg_Forwarded(int _Value)
{
	auto fLambda = [_Value]() -> TCFuture<int>
		{
			co_await fg_Suspend();
			co_return _Value;
		}
	;
	co_return co_await fg_CallSafe(fg_Forward<decltype(fLambda)>(fLambda));
}

// Expressions that pass the closure on to fg_CallSafe.
TCFuture<int> fg_PassedOn(int _Value, bool _bFirst)
{
	auto fLambda = [_Value]() -> TCFuture<int>
		{
			co_await fg_Suspend();
			co_return _Value;
		}
	;
	auto fCopy = fLambda;
	co_await fg_CallSafe(_bFirst ? fLambda : fCopy);
	co_return co_await fg_CallSafe(static_cast<decltype(fLambda) &&>(fLambda));
}

// A shared pointer to the closure that is only given to fg_CallSafe.
TCFuture<int> fg_SharedClosure(int _Value)
{
	auto fGet = [_Value]() -> TCFuture<int>
		{
			co_await fg_Suspend();
			co_return _Value;
		}
	;
	auto pGet = fg_ToSharedPointer(fg_Move(fGet));
	co_await fg_CallSafe(pGet);
	co_return co_await fg_CallSafe(pGet);
}

TCFuture<int> fg_SharedClosureStored(int _Value)
{
	auto fGet = [_Value]() -> TCFuture<int>
		{
			co_await fg_Suspend();
			co_return _Value;
		}
	;
	auto pGet = fg_ToSharedPointer(fg_Move(fGet));
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: the closure of a lambda coroutine that uses its captures after a suspension is given to 'fg_ToSharedPointer', which can call it directly; {{.*}}
	fg_StoreGeneric(pGet);
	co_return 0;
}

// The other arguments of a dispatch are given to the dispatched function.
template <typename t_FFunction, typename t_FCallback>
TCFuture<int> fg_Dispatch(TCActor<> const &_Actor, t_FFunction &&_fFunction, t_FCallback &&_Callback)
{
	return {};
}

TCFuture<int> fg_DispatchedArgument(int _Value)
{
	co_return co_await fg_Dispatch
		(
			TCActor<>()
			, [](auto _fCallback)
			{
				return _fCallback();
			}
			, [_Value]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-8]]:21: warning: the closure of a lambda coroutine that uses its captures after a suspension is given to 'fg_Dispatch', which can call it directly; {{.*}}
			{
				co_await fg_Suspend();
				co_return _Value;
			}
		)
	;
}

// A shared pointer to the closure given to fg_CallSafe right away, or through
// fg_Move.
TCFuture<int> fg_SharedClosureDirect(int _Value)
{
	auto fGet = [_Value]() -> TCFuture<int>
		{
			co_await fg_Suspend();
			co_return _Value;
		}
	;
	auto fOther = fGet;
	co_await fg_CallSafe(fg_ToSharedPointer(fg_Move(fGet)));
	auto pOther = fg_ToSharedPointer(fg_Move(fOther));
	co_return co_await fg_CallSafe(fg_Move(pOther));
}

// A copy of a shared pointer to the closure that is only given to fg_CallSafe.
TCFuture<int> fg_SharedClosureCopy(int _Value)
{
	auto fGet = [_Value]() -> TCFuture<int>
		{
			co_await fg_Suspend();
			co_return _Value;
		}
	;
	auto pGet = fg_ToSharedPointer(fg_Move(fGet));
	auto pCopy = pGet;
	co_return co_await fg_CallSafe(pCopy);
}

// An explicit call of a dispatch operator.
TCFuture<int> fg_ExplicitDispatch(int _Value)
{
	co_return co_await g_Dispatch.operator /
		(
			[_Value]() -> TCFuture<int>
			{
				co_await fg_Suspend();
				co_return _Value;
			}
		)
	;
}

// The address of a captured handle given away keeps a reference into the
// closure; a closure after a comma goes to the call.
void fg_SaveActor(TCActor<> const *_pActor);
void fg_Log();

TCFuture<int> fg_HandleAddress(int _Value)
{
	co_await [Actor = TCActor<>()]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: a lambda coroutine that uses its captures after a suspension is called directly; {{.*}}
		{
			fg_SaveActor(&Actor);
			co_await fg_Suspend();
			co_return 0;
		}
		()
	;
	co_return co_await fg_CallSafe
		(
			(
				fg_Log()
				, [_Value]() -> TCFuture<int>
				{
					co_await fg_Suspend();
					co_return _Value;
				}
			)
		)
	;
}

// An object made from the shared pointer may only point to the closure, but a
// closure cast to void is discarded.
template <typename t_CFunction>
struct TCBorrowed
{
	TCBorrowed(TCSharedPointer<t_CFunction> const &_pFunction);
	TCFuture<int> operator () () const;
	t_CFunction *m_pFunction;
};

TCFuture<int> fg_SharedClosureBorrowed(int _Value)
{
	auto fGet = [_Value]() -> TCFuture<int>
		{
			co_await fg_Suspend();
			co_return _Value;
		}
	;
	auto fUnused = [_Value]() -> TCFuture<int>
		{
			co_await fg_Suspend();
			co_return _Value;
		}
	;
	(void)fUnused;
	co_return co_await fg_CallSafe(TCBorrowed{fg_ToSharedPointer(fg_Move(fGet))});
// CHECK-MESSAGES: :[[@LINE-1]]:44: warning: the closure of a lambda coroutine that uses its captures after a suspension is given to 'fg_ToSharedPointer', which can call it directly; {{.*}}
}

TCFuture<int> fg_SharedClosureBorrowedLocal(int _Value)
{
	auto fGet = [_Value]() -> TCFuture<int>
		{
			co_await fg_Suspend();
			co_return _Value;
		}
	;
	auto pGet = fg_ToSharedPointer(fg_Move(fGet));
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: the closure of a lambda coroutine that uses its captures after a suspension is given to 'fg_ToSharedPointer', which can call it directly; {{.*}}
	TCBorrowed<decltype(fGet)> Borrowed{pGet};
	co_return co_await fg_CallSafe(Borrowed);
}

// `f_Bind` of another function, or an argument of the dispatched function, can
// drop the closure while its coroutine is suspended.
struct CInvoker : CActor
{
	TCFuture<int> f_Invoke(TCFunction<TCFuture<int> ()> _fCallback);
};

TCFuture<int> fg_BoundOther(TCActor<CInvoker> const &_Actor, int _Value)
{
	co_await _Actor.f_Bind<&CInvoker::f_Invoke>
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: the closure of a lambda coroutine that uses its captures after a suspension is given to 'f_Bind', which can call it directly; {{.*}}
		(
			[_Value]() -> TCFuture<int>
			{
				co_await fg_Suspend();
				co_return _Value;
			}
		)
	;
	auto fInner = [_Value]() -> TCFuture<int>
		{
			co_await fg_Suspend();
			co_return _Value;
		}
	;
	co_return co_await _Actor.f_Bind<&CActor::f_DispatchWithReturn<TCFuture<int>>>
// CHECK-MESSAGES: :[[@LINE-1]]:21: warning: the closure of a lambda coroutine that uses its captures after a suspension is given to 'f_Bind', which can call it directly; {{.*}}
		(
			[](auto _fCallback) -> TCFuture<int>
			{
				co_return co_await _fCallback();
			}
			, fg_Move(fInner)
		)
	;
}

// Without a suspension, what `co_return` gives must not refer to the captures.
TCFuture<int const *> fg_ReturnsCapture()
{
	co_return co_await [Value = 42]() -> TCFuture<int const *>
// CHECK-MESSAGES: :[[@LINE-1]]:21: warning: a lambda coroutine that uses its captures after a suspension is called directly; {{.*}}
		{
			co_return &Value;
		}
		()
	;
}

TCFuture<CText> fg_ReturnsCopy()
{
	co_return co_await [Text = CText{}]() -> TCFuture<CText>
		{
			co_return Text;
		}
		()
	;
}

// Only the `return_value` that `co_return` calls stores what it gets, and it
// stores it as the result, which must not be a view of the captures.
struct CSink
{
	void return_value(CData const &_Data);
};

TCFuture<int> fg_ReturnValue(CSink &_Sink)
{
	co_await [Data = CData{}, &_Sink]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: a lambda coroutine that uses its captures after a suspension is called directly; {{.*}}
		{
			_Sink.return_value(Data);
			co_await fg_Suspend();
			co_return 0;
		}
		()
	;
	[Text = CText{}]() -> TCFuture<CStrSpan>
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: a lambda coroutine that uses its captures after a suspension is called directly; {{.*}}
		{
			co_return Text;
		}
		()
	;
	co_return 0;
}

// Only `CActor::f_DispatchWithReturn` keeps the closure alive, also through a
// constant, and through an `&` that may be overloaded in a template.
struct COwnDispatch : CActor
{
	template <typename t_CReturn, typename ...tp_CParams>
	t_CReturn f_DispatchWithReturn(int);
};

constexpr auto gc_pDispatch = &CActor::f_DispatchWithReturn<TCFuture<int>>;

TCFuture<int> fg_BoundDispatch(TCActor<COwnDispatch> const &_Actor, int _Value)
{
	co_await _Actor.f_Bind<&COwnDispatch::f_DispatchWithReturn<TCFuture<int>>>
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: the closure of a lambda coroutine that uses its captures after a suspension is given to 'f_Bind', {{.*}}
		(
			[_Value]() -> TCFuture<int>
			{
				co_await fg_Suspend();
				co_return _Value;
			}
		)
	;
	co_return co_await _Actor.f_Bind<gc_pDispatch>
		(
			[_Value]() -> TCFuture<int>
			{
				co_await fg_Suspend();
				co_return _Value;
			}
		)
	;
}

namespace NOverloadedAddress
{
	struct CTag
	{
	};
	CTag operator & (CTag);

	template <typename t_CActor>
	TCFuture<int> fg_Bound(t_CActor const &_Actor, int _Value)
	{
		co_return co_await _Actor.template f_BindByValue<&CActor::f_DispatchWithReturn<TCFuture<int>, t_CActor>>
			(
				[_Value]() -> TCFuture<int>
				{
					co_await fg_Suspend();
					co_return _Value;
				}
			)
		;
	}
}

// The result of `co_return` must not be able to refer to anything, whatever
// the operand makes on the way, and in a template, what a dependent operator
// gives can be a pointer.
TCFuture<int> fg_ResultTypes()
{
	int Value = 0;
	[Text = CText{}]() -> TCFuture<CStrSpan>
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: a lambda coroutine that uses its captures after a suspension is called directly; {{.*}}
		{
			co_return CText(Text);
		}
		()
	;
	// A copy of a pointer to what isn't in the closure is a value.
	[pValue = &Value]() -> TCFuture<int *>
		{
			co_return pValue;
		}
		()
	;
	co_return 0;
}

template <typename t_CType>
TCFuture<int> fg_DependentOperator(t_CType _Value)
{
	co_return co_await [_Value]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:21: warning: a lambda coroutine that uses its captures after a suspension is called directly; {{.*}}
		{
			auto pValue = _Value + 1;
			co_await fg_Suspend();
			co_return *pValue;
		}
		()
	;
}
struct CDependentValues
{
	int const *operator + (int _Offset) const
	{
		return m_Values + _Offset;
	}
	int m_Values[2] = {};
};
template TCFuture<int> fg_DependentOperator<CDependentValues>(CDependentValues _Value);

// Either value of a conditional given to fg_CallSafe keeps the closure.
TCFuture<int> fg_SharedClosureConditional(int _Value, bool _bFirst)
{
	auto fGet = [_Value]() -> TCFuture<int>
		{
			co_await fg_Suspend();
			co_return _Value;
		}
	;
	auto pGet = fg_ToSharedPointer(fg_Move(fGet));
	auto pOther = pGet;
	co_return co_await fg_CallSafe(_bFirst ? pGet : pOther);
}

// Without a suspension, an alias of a capture can reach a result that can refer
// to it; an awaiter made from what a capture gives can keep a reference to
// it; and `f_Invoke` keeps only the function it runs alive.
struct CTextAwaiter
{
	CTextAwaiter(CText const &_Text);
	bool await_ready() const noexcept;
	void await_suspend(std::coroutine_handle<>) const noexcept;
	int await_resume() const noexcept;
};

TCFuture<int> fg_ResultAlias(CThisActor const &_Self)
{
	[Value = 0]() -> TCFuture<int const *>
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: a lambda coroutine that uses its captures after a suspension is called directly; {{.*}}
		{
			auto pValue = &Value;
			co_return pValue;
		}
		()
	;
	co_await [Data = CData{}]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: a lambda coroutine that uses its captures after a suspension is called directly; {{.*}}
		{
			co_return co_await CTextAwaiter{Data.f_Text()};
		}
		()
	;
	co_return co_await _Self.f_Invoke
// CHECK-MESSAGES: :[[@LINE-1]]:21: warning: the closure of a lambda coroutine that uses its captures after a suspension is given to 'f_Invoke', {{.*}}
		(
			[](auto _fCallback) -> TCFuture<int>
			{
				co_return co_await _fCallback();
			}
			, [Data = CData{}]() -> TCFuture<int>
			{
				co_await fg_Suspend();
				co_return Data.f_Length();
			}
		)
	;
}

// A local initialized with a closure in a template, with parentheses or braces.
template <typename t_CType>
TCFuture<int> fg_ParenthesizedLocal(t_CType _Value)
{
	auto fGet([_Value]() -> TCFuture<int>
		{
			co_await fg_Suspend();
			co_return _Value;
		});
	auto fOther{[_Value]() -> TCFuture<int>
		{
			co_await fg_Suspend();
			co_return _Value;
		}};
	co_await fg_CallSafe(fGet);
	co_return co_await fg_CallSafe(fOther);
}

// A copy of an initializer list doesn't copy its elements, a pointer alias can
// reach a result that can refer to it, and safe operators can be called
// explicitly.
namespace std
{
	template <typename t_CType>
	class initializer_list
	{
	public:
		t_CType const *begin() const;
	private:
		t_CType const *mp_pBegin;
		unsigned long m_Size;
	};
}

TCFuture<int> fg_InitializerList()
{
	co_await [Values = {1, 2, 3}]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: a lambda coroutine that uses its captures after a suspension is called directly; {{.*}}
		{
			auto Copy = Values;
			co_await fg_Suspend();
			co_return *Copy.begin();
		}
		()
	;
	// A copy of a pointer to what isn't in the closure is a value.
	int Value = 0;
	[pValue = &Value]() -> TCFuture<int *>
		{
			auto *pCopy = pValue;
			co_return pCopy;
		}
		()
	;
	co_return 0;
}

struct CDispatchTag
{
};

template <typename t_FFunction>
TCFuture<int> operator / (CDispatchTag, t_FFunction &&)
{
	return {};
}

TCFuture<int> fg_ExplicitOperator(int _Value)
{
	co_return co_await operator /
		(
			CDispatchTag{}
			, [_Value]() -> TCFuture<int>
			{
				co_await fg_Suspend();
				co_return _Value;
			}
		)
	;
}

// In a template, what a lambda captures implicitly is known in its
// instantiations; and an actor call gives a closure to a function that holds it
// as it is, which can drop it.
template <typename t_FFunction>
TCFuture<int> fg_CallsDirectly(t_FFunction &&_fFunction)
{
	return _fFunction(1);
}

template <typename t_CType>
TCFuture<int> fg_ImplicitCapture(t_CType _Value)
{
	co_return co_await fg_CallsDirectly
// CHECK-MESSAGES: :[[@LINE-1]]:21: warning: the closure of a lambda coroutine that uses its captures after a suspension is given to 'fg_CallsDirectly', {{.*}}
		(
			[=](auto _Other) -> TCFuture<int>
			{
				co_await fg_Suspend();
				co_return _Value + _Other;
			}
		)
	;
}

// A closure held through a pointer, like a function wrapper holds it, is the
// holder's: the call through the pointer isn't reported, but where the
// closure is given to the holder is.
template <typename t_FFunction>
struct TCHeldThroughPointer
{
	TCHeldThroughPointer(t_FFunction *_pFunction)
		: m_pFunction(_pFunction)
	{
	}

	TCFuture<int> f_Call()
	{
		return (*m_pFunction)();
	}

	t_FFunction *m_pFunction;
};

TCFuture<int> fg_HeldThroughPointer(int _Value)
{
	auto fGet = [_Value]() -> TCFuture<int>
		{
			co_await fg_Suspend();
			co_return _Value;
		}
	;
	TCHeldThroughPointer<decltype(fGet)> Held(&fGet);
// CHECK-MESSAGES: :[[@LINE-1]]:44: warning: the closure of a lambda coroutine that uses its captures after a suspension is given to an expression, {{.*}}
	co_return co_await Held.f_Call();
}

struct CForwarder : CActor
{
	template <typename t_FCallback>
	TCFuture<int> f_Forward(t_FCallback _fCallback)
	{
		return _fCallback();
	}
};

TCFuture<int> fg_ActorCallArgument(TCActor<CForwarder> const &_Actor, int _Value)
{
	co_await fg_ImplicitCapture(_Value);
	auto fGet = [_Value]() -> TCFuture<int>
		{
			co_await fg_Suspend();
			co_return _Value;
		}
	;
	co_return co_await _Actor(&CForwarder::f_Forward<decltype(fGet)>, fg_Move(fGet));
// CHECK-MESSAGES: :[[@LINE-1]]:21: warning: the closure of a lambda coroutine that uses its captures after a suspension is given to 'TCActor', {{.*}}
}

// A closure in a function wrapper given to fg_CallSafe, a lambda made before
// the first suspension that copies `this`, and a closure that one lambda
// captures and uses twice, which is reported once.
struct CNestedThis : CActor
{
	TCFuture<int> f_Run()
	{
		co_await fg_CallSafe
			(
				TCFunction<TCFuture<int> ()>
				(
					[this]() -> TCFuture<int>
					{
						co_await fg_Suspend();
						co_return m_Value;
					}
				)
			)
		;
		co_return co_await [this]() -> TCFuture<int>
			{
				auto fGet = [this]
					{
						return m_Value;
					}
				;
				co_await fg_Suspend();
				co_return fGet();
			}
			()
		;
	}

	int m_Value = 0;
};

TCFuture<int> fg_CapturedTwice(int _Value)
{
	auto fGet = [_Value]() -> TCFuture<int>
		{
			co_await fg_Suspend();
			co_return _Value;
		}
	;
	auto fTwice = [fGet]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: the closure of a lambda coroutine that uses its captures after a suspension is given to a lambda that captures it, {{.*}}
		{
			co_await fGet();
			co_return co_await fGet();
		}
	;
	co_return co_await fg_CallSafe(fTwice);
}

// A timer and a command registration keep the closure in an actor functor.
struct CCommands
{
	void f_RegisterCommand(int _Command, TCFunction<TCFuture<int> ()> &&_fRunCommand);
};

TCFuture<int> fg_TimerAndCommand(CCommands &_Commands, int _Value)
{
	co_await fg_RegisterTimer
		(
			1.0
			, [_Value]() -> TCFuture<int>
			{
				co_await fg_Suspend();
				co_return _Value;
			}
		)
	;
	_Commands.f_RegisterCommand
		(
			1
			, [_Value]() -> TCFuture<int>
			{
				co_await fg_Suspend();
				co_return _Value;
			}
		)
	;
	co_return 0;
}

// A converting constructor of the result that keeps a reference to a capture,
// also in a coroutine that doesn't suspend.
struct CKeepsReference
{
	int const *m_pValue;
	CKeepsReference(int const &_Value) : m_pValue(&_Value) {}
};

void fg_ConvertingResult()
{
	[Value = 42]() -> TCFuture<CKeepsReference>
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: {{.*}}
	{
		co_return Value;
	}();
}

// A local closure inside a lambda, used through fg_CallSafe, doesn't escape
// through the lambda it is in; a member of a capture converted into the result
// does.
void fg_LocalInLambda()
{
	g_Dispatch / []() -> TCFuture<int>
		{
			auto fInner = [Value = 1]() -> TCFuture<int>
				{
					co_await fg_Suspend();
					co_return Value;
				}
			;
			co_return co_await fg_CallSafe(fInner);
		}
	;
}

struct CHasValue
{
	int m_Value = 1;
};

void fg_ConvertingMember()
{
	[Data = CHasValue{}]() -> TCFuture<CKeepsReference>
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: {{.*}}
	{
		co_return Data.m_Value;
	}();
}

// A converting constructor that takes a pointer to a capture.
struct CKeepsPointer
{
	int const *m_pValue;
	CKeepsPointer(int const *_pValue) : m_pValue(_pValue) {}
};

void fg_ConvertingPointer()
{
	[Value = 42]() -> TCFuture<CKeepsPointer>
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: {{.*}}
	{
		co_return &Value;
	}();
}

// An aggregate result that refers to a capture, a borrowed pointer given to the
// result, and a suspension in what a nested lambda captures.
struct CRefersToValue
{
	int const &m_Value;
};

void fg_AggregateResult()
{
	[Value = 42]() -> TCFuture<CRefersToValue>
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: {{.*}}
	{
		co_return CRefersToValue{Value};
	}();
}

TCFuture<int> fg_NestedCaptureSuspends()
{
	return [Value = 42]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: {{.*}}
		{
			auto fHelper = []() -> TCFuture<int>
				{
					auto fNested = [Inner = co_await fg_Suspend()] { return Inner; };
					co_return fNested();
				}
			;
			co_await fHelper();
			co_return Value;
		}();
}

// A converting constructor of the result given a local reference to a capture.
void fg_ConvertingAlias()
{
	[Value = 42]() -> TCFuture<CKeepsReference>
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: {{.*}}
	{
		int const &Alias = Value;
		co_return Alias;
	}();
}

// A pointer alias of a capture given to the result, and an aggregate result
// with a base.
struct CAggregateBaseResult
{
};

struct CRefersWithBase : CAggregateBaseResult
{
	int const &m_Value;
};

void fg_PointerAliasResult()
{
	[Value = 42]() -> TCFuture<CKeepsPointer>
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: {{.*}}
	{
		int const *pValue = &Value;
		co_return pValue;
	}();
	[Value = 42]() -> TCFuture<CRefersWithBase>
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: {{.*}}
	{
		co_return CRefersWithBase{{}, Value};
	}();
}

struct CReferenceResult
{
	int const &m_Value;
};

struct CPointerResult
{
	int const *m_pValue = nullptr;
};

// A result that keeps a reference to a capture, returned through a local.
void fg_ReturnedLocalResult()
{
	[Value = 42]() -> TCFuture<CKeepsReference>
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: {{.*}}
	{
		CKeepsReference Result(Value);
		co_return Result;
	}();
	[Value = 42]() -> TCFuture<CReferenceResult>
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: {{.*}}
	{
		CReferenceResult Result{Value};
		co_return Result;
	}();
	[Value = 42]() -> TCFuture<CPointerResult>
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: {{.*}}
	{
		CPointerResult Result;
		Result.m_pValue = &Value;
		co_return Result;
	}();
	[Value = 42]() -> TCFuture<CPointerResult>
	{
		CPointerResult Result;
		co_return Result;
	}();
}

struct CInnerPointer
{
	int const *m_pValue;
};

struct COuterResult
{
	CInnerPointer m_Inner;
};

struct COuterConverted
{
	CKeepsReference m_Inner;
};

// What a member that is an object of its own keeps.
void fg_NestedResult()
{
	[Value = 1]() -> TCFuture<COuterResult>
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: {{.*}}
	{
		co_return COuterResult{CInnerPointer{&Value}};
	}();
	[Value = 1]() -> TCFuture<COuterConverted>
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: {{.*}}
	{
		co_return COuterConverted{CKeepsReference(Value)};
	}();
}

struct CReferenceBase
{
	int const &m_Value;
};

struct CDerivedResult : CReferenceBase
{
};

// Either value of a conditional, and the initializers of the bases.
void fg_ConditionalAndBaseResult(bool _bFirst)
{
	[Value = 42, _bFirst]() -> TCFuture<CReferenceResult>
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: {{.*}}
	{
		co_return _bFirst ? CReferenceResult{Value} : CReferenceResult{Value};
	}();
	[Value = 42]() -> TCFuture<CDerivedResult>
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: {{.*}}
	{
		co_return CDerivedResult{{Value}};
	}();
}

// A call through a function wrapper made from the lambda.
TCFuture<int> fg_ThroughWrapper(int _Value)
{
	TCFunction<TCFuture<int> ()> fFunction = [_Value]() -> TCFuture<int>
		{
			co_await fg_Suspend();
			co_return _Value;
		}
	;
	return fFunction();
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: a lambda coroutine that uses its captures after a suspension is called directly; {{.*}}
}

union CUnionResult
{
	int m_Tag;
	int const *m_pValue;
};

// A union initializes the member it names.
void fg_UnionResult()
{
	[Value = 42]() -> TCFuture<CUnionResult>
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: {{.*}}
	{
		co_return CUnionResult{.m_pValue = &Value};
	}();
}

struct CNestedPointer
{
	CInnerPointer m_Inner;
};

// Assigned to a member of a member of the returned local.
void fg_NestedAssignment()
{
	[Value = 42]() -> TCFuture<CNestedPointer>
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: {{.*}}
	{
		CNestedPointer Result{};
		Result.m_Inner.m_pValue = &Value;
		co_return Result;
	}();
}

// A conditional pointer, and a pointer local assigned a borrowed address.
void fg_ConditionalPointerResult(bool _bFirst)
{
	[Value = 42, _bFirst]() -> TCFuture<CPointerResult>
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: {{.*}}
	{
		co_return CPointerResult{_bFirst ? &Value : nullptr};
	}();
	[Value = 42, _bFirst]() -> TCFuture<CPointerResult>
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: {{.*}}
	{
		int const *pValue = nullptr;
		if (_bFirst)
			pValue = &Value;
		co_return CPointerResult{pValue};
	}();
}

// A test category keeps its closure alive while it runs it, which its
// operator gives it to.
struct CTestCategoryScope
{
	template <typename tf_CFunction>
	void operator % (tf_CFunction &&_fCategory)
	{
	}
};
void fg_TestCategory()
{
	int Value = 0;
	CTestCategoryScope() % [&]() -> TCFuture<int>
		{
			co_await fg_Suspend();
			co_return ++Value;
		}
	;
}

// A lambda whose introducer comes from a macro, which the code that uses the
// macro writes the rest of, is reported where the macro is used.
struct CUnknownScope
{
	template <typename tf_CFunction>
	void operator % (tf_CFunction &&_fFunction)
	{
	}
};
#define DUnknownCategory CUnknownScope() % [&] ()
void fg_MacroCategory()
{
	int Value = 0;
	DUnknownCategory -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: {{.*}}
		{
			co_await fg_Suspend();
			co_return ++Value;
		}
	;
}

// `fg_CallSafeDispatched` keeps the function it is given alive.
TCFuture<int> fg_CallSafeDispatchedKeeps()
{
	co_return co_await fg_CallSafeDispatched
		(
			[Value = 42]() -> TCFuture<int>
			{
				co_await fg_Suspend();
				co_return Value;
			}
		)
	;
}

// What runs a lambda keeps it alive while it runs, but not what its result
// refers to.
void fg_ReturnsCaptureThroughDispatch()
{
	(void)(g_Dispatch / [Value = 42]() -> TCFuture<int const *>
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: a lambda coroutine returns what refers to its captures, which are gone once what it is given to has run it; return a copy [malterlib-coroutine-lambda-call]
		{
			co_await fg_Suspend();
			co_return &Value;
		}
	);
}

// Through a local, also a view, but not a copy of a pointer to what isn't in
// the closure.
int g_PointedGlobal;
void fg_ResultEscapes()
{
	(void)(g_Dispatch / [Value = 1]() -> TCFuture<int const *>
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: a lambda coroutine returns what refers to its captures, which are gone once what it is given to has run it; return a copy [malterlib-coroutine-lambda-call]
		{
			auto *pValue = &Value;
			co_return pValue;
		}
	);
	(void)(g_Dispatch / [pGlobal = &g_PointedGlobal]() -> TCFuture<int *>
		{
			co_return pGlobal;
		}
	);
}

// Bound to a reference member of the result, and a pointer pointed elsewhere
// before it is returned.
struct CRefResult
{
	int const &m_Value;
};
void fg_ReferenceMemberResult()
{
	(void)(g_Dispatch / [Value = 42]() -> TCFuture<CRefResult>
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: a lambda coroutine returns what refers to its captures, which are gone once what it is given to has run it; return a copy [malterlib-coroutine-lambda-call]
		{
			co_return CRefResult{Value};
		}
	);
	(void)(g_Dispatch / [Value = 42]() -> TCFuture<int const *>
		{
			int const *pValue = &Value;
			pValue = &g_PointedGlobal;
			co_return pValue;
		}
	);
}

// An array decayed to a pointer, and a pointer that a member function of a
// copy gives, but not a value computed from an address.
struct CTextResult
{
	char m_Text[16];
	char const *f_GetStr() const;
};
int fg_ReadPointed(int const *_pValue);
void fg_ResultThroughMembers()
{
	(void)(g_Dispatch / [Text = CTextResult{}]() -> TCFuture<char const *>
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: a lambda coroutine returns what refers to its captures, which are gone once what it is given to has run it; return a copy [malterlib-coroutine-lambda-call]
		{
			co_return Text.m_Text;
		}
	);
	(void)(g_Dispatch / [Text = CTextResult{}]() -> TCFuture<char const *>
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: a lambda coroutine returns what refers to its captures, which are gone once what it is given to has run it; return a copy [malterlib-coroutine-lambda-call]
		{
			co_return Text.f_GetStr();
		}
	);
	(void)(g_Dispatch / [Value = 42]() -> TCFuture<int>
		{
			co_return fg_ReadPointed(&Value);
		}
	);
}

// What a pointer into a capture points to, copied into the result, but not an
// address through that pointer.
struct CPointedEntry
{
	int m_Value;
	int const *f_Find() const;
};
void fg_ResultThroughPointer()
{
	(void)(g_Dispatch / [Entry = CPointedEntry{}]() -> TCFuture<int>
		{
			auto pValue = Entry.f_Find();
			co_return *pValue;
		}
	);
	(void)(g_Dispatch / [Entry = CPointedEntry{}]() -> TCFuture<int const *>
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: a lambda coroutine returns what refers to its captures, which are gone once what it is given to has run it; return a copy [malterlib-coroutine-lambda-call]
		{
			auto pEntry = &Entry;
			co_return &pEntry->m_Value;
		}
	);
}

// A result made by a constructor that keeps the address of what it is given,
// also converted to, a part of a returned local assigned an address, and the
// object copied by `[*this]`.
struct CKeptResult
{
	int const *m_pValue;
	CKeptResult(int const &_Value) : m_pValue(&_Value) {}
};
struct CPartsResult
{
	int const *m_pValue = nullptr;
	int const *m_pValues[1] = {};
};
struct CCopiedObject
{
	int m_Value = 42;
	void f_Run();
};
void fg_ResultKeepsAddress()
{
	(void)(g_Dispatch / [Value = 42]() -> TCFuture<CKeptResult>
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: a lambda coroutine returns what refers to its captures, which are gone once what it is given to has run it; return a copy [malterlib-coroutine-lambda-call]
		{
			co_return CKeptResult(Value);
		}
	);
	(void)(g_Dispatch / [Value = 42]() -> TCFuture<CKeptResult>
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: a lambda coroutine returns what refers to its captures, which are gone once what it is given to has run it; return a copy [malterlib-coroutine-lambda-call]
		{
			co_return Value;
		}
	);
	(void)(g_Dispatch / [Value = 42]() -> TCFuture<CPartsResult>
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: a lambda coroutine returns what refers to its captures, which are gone once what it is given to has run it; return a copy [malterlib-coroutine-lambda-call]
		{
			CPartsResult Result;
			Result.m_pValues[0] = &Value;
			co_return Result;
		}
	);
}
void CCopiedObject::f_Run()
{
	(void)(g_Dispatch / [*this]() -> TCFuture<int const *>
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: a lambda coroutine returns what refers to its captures, which are gone once what it is given to has run it; return a copy [malterlib-coroutine-lambda-call]
		{
			co_return &m_Value;
		}
	);
}

// Assigned to an element of an array member of the returned local.
void fg_ElementAssignment()
{
	[Value = 42]() -> TCFuture<CPartsResult>
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: {{.*}}
	{
		CPartsResult Result;
		Result.m_pValues[0] = &Value;
		co_return Result;
	}();
}

// Assigned through a reference to a part of the returned local, and a pointer
// whose reassignment a jump passes.
int g_JumpedGlobal;
void fg_ResultThroughAliasAndJump(bool _bJump)
{
	(void)(g_Dispatch / [Value = 42]() -> TCFuture<CPartsResult>
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: a lambda coroutine returns what refers to its captures, which are gone once what it is given to has run it; return a copy [malterlib-coroutine-lambda-call]
		{
			CPartsResult Result;
			auto &pValue = Result.m_pValue;
			pValue = &Value;
			co_return Result;
		}
	);
	(void)(g_Dispatch / [Value = 42, _bJump]() -> TCFuture<int const *>
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: a lambda coroutine returns what refers to its captures, which are gone once what it is given to has run it; return a copy [malterlib-coroutine-lambda-call]
		{
			int const *pValue = &Value;
			if (_bJump)
				goto End;
			pValue = &g_JumpedGlobal;
		End:
			co_return pValue;
		}
	);
}

// All of the returned local assigned.
void fg_ResultAssignedWhole()
{
	(void)(g_Dispatch / [Value = 42]() -> TCFuture<CPartsResult>
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: a lambda coroutine returns what refers to its captures, which are gone once what it is given to has run it; return a copy [malterlib-coroutine-lambda-call]
		{
			CPartsResult Result;
			Result = CPartsResult{&Value};
			co_return Result;
		}
	);
}

// An element copied from an array of the closure is a value.
TCFuture<int> fg_Round54Element()
{
	int Values[1] = {42};
	co_return co_await fg_CallSafe
		(
			[Values]() -> TCFuture<int>
			{
				co_return Values[0];
			}
		)
	;
}

// The address of a capture through std::addressof, or kept by a constructor
// in its body, refers to the capture, but that of a static member doesn't.
namespace std
{
	template <typename t_CType>
	t_CType *addressof(t_CType &_Value);
}
struct CRound56Kept
{
	int const *m_pValue;
	CRound56Kept(int const &_Value)
	{
		m_pValue = &_Value;
	}
};
struct CRound56Static
{
	inline static int ms_Value = 1;
};
void fg_Round56Results()
{
	(void)(g_Dispatch / [Value = 42]() -> TCFuture<int const *>
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: a lambda coroutine returns what refers to its captures, {{.*}}
		{
			co_return std::addressof(Value);
		}
	);
	(void)(g_Dispatch / [Value = 42]() -> TCFuture<CRound56Kept>
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: a lambda coroutine returns what refers to its captures, {{.*}}
		{
			co_return CRound56Kept(Value);
		}
	);
	(void)(g_Dispatch / [Data = CRound56Static{}]() -> TCFuture<int *>
		{
			co_return &Data.ms_Value;
		}
	);
}

// What refers to a capture through a local reference, a cast or a function
// that returns what it is given refers to the closure; a discarded branch of
// an `if constexpr` doesn't return.
int const &fg_Round70Const(int const &_Value)
{
	return _Value;
}
void fg_Round70Results()
{
	fg_CallSafe
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: a lambda coroutine returns what refers to its captures, which are gone once what it is given to has run it; return a copy [malterlib-coroutine-lambda-call]
		(
			[Value = 42]() -> TCFuture<int const *>
			{
				auto const &Alias = Value;
				co_return &Alias;
			}
		)
	;
	fg_CallSafe
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: a lambda coroutine returns what refers to its captures, which are gone once what it is given to has run it; return a copy [malterlib-coroutine-lambda-call]
		(
			[Value = 42]() -> TCFuture<int const *>
			{
				co_return &fg_Round70Const(Value);
			}
		)
	;
	fg_CallSafe
		(
			[Value = 42]() -> TCFuture<int const *>
			{
				if constexpr (false)
					co_return &Value;
				co_return nullptr;
			}
		)
	;
}

// A constructor that keeps the address of what it is given through
// std::addressof keeps it; the conversion uses the constructor it selects.
struct CRound72Kept
{
	int const *m_pValue;
	CRound72Kept(int const &_Value)
		: m_pValue(std::addressof(_Value))
	{
	}
};
struct CRound72Selected
{
	int m_Value;
	int const *m_pValue = nullptr;
	CRound72Selected(int &_Value)
		: m_Value(_Value)
	{
	}
	CRound72Selected(int const &_Value)
		: m_Value(_Value), m_pValue(&_Value)
	{
	}
};
void fg_Round72Results()
{
	fg_CallSafe
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: a lambda coroutine returns what refers to its captures, which are gone once what it is given to has run it; return a copy [malterlib-coroutine-lambda-call]
		(
			[Value = 42]() -> TCFuture<CRound72Kept>
			{
				co_return CRound72Kept(Value);
			}
		)
	;
	fg_CallSafe
		(
			[Value = 42]() mutable -> TCFuture<CRound72Selected>
			{
				co_return Value;
			}
		)
	;
}

// Nested awaits in a lambda are looked at once each, not once per path
// through the operands, which is exponential in their depth.
TCFuture<int> fg_Round82Work(int _Value);
#define DRound82Await(d_Value) co_await fg_Round82Work(d_Value)
#define DRound82Await4(d_Value) DRound82Await(DRound82Await(DRound82Await(DRound82Await(d_Value))))
#define DRound82Await32(d_Value) DRound82Await4(DRound82Await4(DRound82Await4(DRound82Await4(DRound82Await4(DRound82Await4(DRound82Await4(DRound82Await4(d_Value))))))))
void fg_Round82Nested()
{
	int Value = 1;
	fg_CallSafe
		(
			[&Value]() -> TCFuture<int>
			{
				co_return DRound82Await32(Value);
			}
		)
	;
}

// What a member function of a capture returns a reference to is in the
// capture.
struct CRound82Value
{
	int m_Value;
	int const &f_Get() const
	{
		return m_Value;
	}
};
void fg_Round82Getter()
{
	fg_CallSafe
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: a lambda coroutine returns what refers to its captures, which are gone once what it is given to has run it; return a copy [malterlib-coroutine-lambda-call]
		(
			[Value = CRound82Value{}]() -> TCFuture<int const *>
			{
				co_return &Value.f_Get();
			}
		)
	;
}

// A pointer that a member function of a capture returns, whose body returns
// what isn't in the capture, isn't in it.
struct CRound83Info
{
	char const *f_GetName() const
	{
		return "Name";
	}
};
void fg_Round83Name()
{
	fg_CallSafe
		(
			[Info = CRound83Info{}]() -> TCFuture<char const *>
			{
				co_return Info.f_GetName();
			}
		)
	;
}

// A converting constructor that keeps the address of what it is given cast to
// another pointer, one that only keeps it in a local of it, and what a capture
// by reference points to accessed after a suspension.
struct CRound85Kept
{
	void const *m_pValue;
	CRound85Kept(int const &_Value) : m_pValue(static_cast<void const *>(&_Value)) {}
};
struct CRound85Scratch
{
	int m_Value;
	CRound85Scratch(int const &_Value) : m_Value(_Value)
	{
		int const *pScratch;
		pScratch = &_Value;
	}
};
void fg_Round85Converted()
{
	fg_CallSafe
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: a lambda coroutine returns what refers to its captures, which are gone once what it is given to has run it; return a copy [malterlib-coroutine-lambda-call]
		(
			[Value = 1]() -> TCFuture<CRound85Kept>
			{
				co_return Value;
			}
		)
	;
	fg_CallSafe
		(
			[Value = 1]() -> TCFuture<CRound85Scratch>
			{
				co_return Value;
			}
		)
	;
}

// Through a long chain of local copies of a pointer into a capture.
void fg_Round91Chain()
{
	fg_CallSafe
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: a lambda coroutine returns what refers to its captures, which are gone once what it is given to has run it; return a copy [malterlib-coroutine-lambda-call]
		(
			[Value = 42]() -> TCFuture<int const *>
			{
				auto pValue0 = &Value;
				auto pValue1 = pValue0;
				auto pValue2 = pValue1;
				auto pValue3 = pValue2;
				auto pValue4 = pValue3;
				co_return pValue4;
			}
		)
	;
}

TCFuture<int> fg_Round93Suspend();

// What a helper returns of its argument, which it gets the address of.
int const *fg_Round93Address(int const &_Value)
{
	return &_Value;
}

void fg_Round93HelperAddress()
{
	fg_CallSafe
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: a lambda coroutine returns what refers to its captures, which are gone once what it is given to has run it; return a copy [malterlib-coroutine-lambda-call]
		(
			[Value = 42]() -> TCFuture<int const *>
			{
				co_return fg_Round93Address(Value);
			}
		)
	;
}

// A constructor that only reads through what it is given, and one that keeps
// it only in a branch that isn't compiled.
struct CRound93ReadResult
{
	CRound93ReadResult(int const *_pValue)
		: m_Value(*_pValue)
	{
	}

	int m_Value;
};

struct CRound93DiscardedResult
{
	CRound93DiscardedResult(int const &_Value)
		: m_Value(_Value)
	{
		if constexpr (false)
			m_pValue = &_Value;
	}

	int const *m_pValue = nullptr;
	int m_Value;
};

void fg_Round93ReadOnlyConstructors()
{
	auto fRead = [Value = 42]() -> TCFuture<CRound93ReadResult>
		{
			co_return CRound93ReadResult(&Value);
		}
	;
	fg_CallSafe(fRead);
	auto fDiscarded = [Value = 42]() -> TCFuture<CRound93DiscardedResult>
		{
			co_return CRound93DiscardedResult(Value);
		}
	;
	fg_CallSafe(fDiscarded);
}

// The address of what an overloaded subscript of a capture returns.
struct CRound98Values
{
	int m_Data[2];
	int const &operator[](int _Index) const
	{
		return m_Data[_Index];
	}
};

auto fg_Round98Subscript()
{
	return fg_CallSafe
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: a lambda coroutine returns what refers to its captures, which are gone once what it is given to has run it; return a copy [malterlib-coroutine-lambda-call]
		(
			[Values = CRound98Values{}]() -> TCFuture<int const *>
			{
				co_return &Values[0];
			}
		)
	;
}

// A reference member bound to a member of a capture.
struct CRound98Value
{
	int m_Value;
	int const *f_Get() const
	{
		return &m_Value;
	}
};
struct CRound98Ref
{
	int const &m_Value;
};

auto fg_Round98Member()
{
	return fg_CallSafe
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: a lambda coroutine returns what refers to its captures, which are gone once what it is given to has run it; return a copy [malterlib-coroutine-lambda-call]
		(
			[Value = CRound98Value{42}]() -> TCFuture<CRound98Ref>
			{
				co_return CRound98Ref{Value.m_Value};
			}
		)
	;
}

// A getter of a capture whose type depends on template parameters, told in
// the instantiations.
template <typename tf_CValue>
auto fg_Round98Template()
{
	return fg_CallSafe
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: a lambda coroutine returns what refers to its captures, which are gone once what it is given to has run it; return a copy [malterlib-coroutine-lambda-call]
		(
			[Value = tf_CValue{}]() -> TCFuture<int const *>
			{
				co_return Value.f_Get();
			}
		)
	;
}

auto fg_Round98UseTemplate()
{
	return fg_Round98Template<CRound98Value>();
}

// A getter that returns a global through a local is fine.
int g_Round98Global;

struct CRound98Global
{
	int const *f_Get() const
	{
		int const *pValue = &g_Round98Global;
		return pValue;
	}
};

auto fg_Round98Global()
{
	return fg_CallSafe
		(
			[Value = CRound98Global{}]() -> TCFuture<int const *>
			{
				co_return Value.f_Get();
			}
		)
	;
}

// A captured pointer copied into the result doesn't refer to the closure, but
// its address does.
int g_Round105Value;

void fg_Round105CopiedPointer()
{
	[pValue = &g_Round105Value]() -> TCFuture<int *>
	{
		co_return pValue;
	}();
	[pValue = &g_Round105Value]() -> TCFuture<int *const *>
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: a lambda coroutine that uses its captures after a suspension is called directly; the captures dangle when the closure is a temporary, so run it through fg_CallSafe, g_Dispatch, 'self /' or an actor functor [malterlib-coroutine-lambda-call]
	{
		co_return &pValue;
	}();
}

// A local returned twice, made from what isn't in the object.
struct CRound106Value
{
	int const *f_Get(bool _bChoose) const
	{
		int const *pValue = &g_Round105Value;
		if (_bChoose)
			return pValue;
		return pValue;
	}
};

auto fg_Round106ReturnedTwice()
{
	return fg_CallSafe
		(
			[Value = CRound106Value{}]() -> TCFuture<int const *>
			{
				co_return Value.f_Get(false);
			}
		)
	;
}

// The address of what a getter of a capture returns a reference to, through
// std::addressof, and of a capture through __builtin_addressof.
struct CRound108Value
{
	int m_Value;
	int const &f_Get() const
	{
		return m_Value;
	}
};

void fg_Round108Addresses()
{
	auto Getter = fg_CallSafe
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: a lambda coroutine returns what refers to its captures, which are gone once what it is given to has run it; return a copy [malterlib-coroutine-lambda-call]
		(
			[Value = CRound108Value{}]() -> TCFuture<int const *>
			{
				co_return std::addressof(Value.f_Get());
			}
		)
	;
	auto Builtin = fg_CallSafe
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: a lambda coroutine returns what refers to its captures, which are gone once what it is given to has run it; return a copy [malterlib-coroutine-lambda-call]
		(
			[Value = 1]() -> TCFuture<int const *>
			{
				co_return __builtin_addressof(Value);
			}
		)
	;
}

// Round 113: a delegating constructor keeps what its target keeps
namespace NRound113Delegating
{
struct Result {
  const int *p;
  Result(const int &x) : Result(&x) {}
  Result(const int *v) : p(v) {}
};
void start() {
  auto future = fg_CallSafe([x = 1]() -> TCFuture<Result> { co_return x; });
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: a lambda coroutine returns what refers to its captures, which are gone once what it is given to has run it; return a copy [malterlib-coroutine-lambda-call]
}
}

// Round 113: a lambda helper gives back the address it is given
namespace NRound113LambdaHelper
{
void start() {
  auto future = fg_CallSafe([x = 1]() -> TCFuture<const int *> {
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: a lambda coroutine returns what refers to its captures, which are gone once what it is given to has run it; return a copy [malterlib-coroutine-lambda-call]
    co_return [](const int &v) { return &v; }(x);
  });
}
}

// Round 113: only the chosen value of a constant conditional is returned
namespace NRound113Constant
{
void start() {
  auto future = fg_CallSafe([x = 1]() -> TCFuture<const int *> {
    co_return true ? nullptr : &x;
  });
}
}

// A conversion operator giving a pointer into a capture is returning the capture.
namespace NRound115ConversionOperator {
struct CValue {
  int m_Value = 42;
  operator int const *() const { return &m_Value; }
};
auto fg_Run() {
  return fg_CallSafe([Value = CValue{}]() -> TCFuture<int const *> {
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: a lambda coroutine returns what refers to its captures, which are gone once what it is given to has run it; return a copy [malterlib-coroutine-lambda-call]
    co_return Value;
  });
}
}

// A getter choosing between globals with a member doesn't return the capture.
namespace NRound115ConditionalGlobals {
int First, Second;
struct CChoice {
  bool m_bFirst;
  int *f_Get() const { return m_bFirst ? &First : &Second; }
};
auto fg_Run() {
  return fg_CallSafe([Choice = CChoice{true}]() -> TCFuture<int *> {
    co_return Choice.f_Get();
  });
}
}

// Round 126: a result made from a member of a capture, or from what a prefix
// increment of one gives, refers into the closure.
namespace NRound126
{
struct CData {
  int m_X;
};
struct CMemberResult {
  int const &m_Value;
  CMemberResult(CData const &_Data) : m_Value(_Data.m_X) {}
};
struct CResult {
  int const &m_Value;
  CResult(int const &_Value) : m_Value(_Value) {}
};

auto fg_Member() {
  return fg_CallSafe([Data = CData{42}]() -> TCFuture<CMemberResult> {
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: a lambda coroutine returns what refers to its captures, which are gone once what it is given to has run it; return a copy [malterlib-coroutine-lambda-call]
    co_return CMemberResult(Data);
  });
}

auto fg_Increment() {
  return fg_CallSafe([Value = 42]() mutable -> TCFuture<CResult> {
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: a lambda coroutine returns what refers to its captures, which are gone once what it is given to has run it; return a copy [malterlib-coroutine-lambda-call]
    co_return CResult(++Value);
  });
}
}

// Round 129: a scope exit calls what it is given and then destroys it, but
// one of an actor dispatches it.
namespace NRound129
{
TCFuture<int> fg_Work();

void fg_ScopeExit() {
  auto Cleanup = g_OnScopeExit / [Value = 7]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: the closure of a lambda coroutine that uses its captures after a suspension is given to a scope exit, which can call it directly; give it only to fg_CallSafe, g_Dispatch, 'self /', an actor functor or another function that keeps it alive while it runs [malterlib-coroutine-lambda-call]
    co_await fg_Work();
    co_return Value;
  };
}

void fg_ScopeExitActor() {
  auto Cleanup = g_OnScopeExitActor / [Value = 7]() -> TCFuture<int> {
    co_await fg_Work();
    co_return Value;
  };
}
}

// Round 130: the result can point into the closure through either of a
// conditional, through any number of references, and through what a getter
// assigns a static local.
namespace NRound130
{
int g_Global;

void fg_Conditional(bool _bChoose) {
  fg_CallSafe([Value = 42, _bChoose]() -> TCFuture<int const *> {
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: a lambda coroutine returns what refers to its captures, which are gone once what it is given to has run it; return a copy [malterlib-coroutine-lambda-call]
    co_return &(_bChoose ? Value : g_Global);
  });
}

void fg_Aliases() {
  fg_CallSafe([Value = 42]() -> TCFuture<int const *> {
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: a lambda coroutine returns what refers to its captures, which are gone once what it is given to has run it; return a copy [malterlib-coroutine-lambda-call]
    int const &a = Value, &b = a, &c = b, &d = c, &e = d, &f = e, &g = f,
              &h = g;
    co_return &h;
  });
}

struct CData {
  int m_Value;
  int const *f_Get() const {
    static int const *s_pValue;
    s_pValue = &m_Value;
    return s_pValue;
  }
};

void fg_Static() {
  fg_CallSafe([Value = CData{42}]() -> TCFuture<int const *> {
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: a lambda coroutine returns what refers to its captures, which are gone once what it is given to has run it; return a copy [malterlib-coroutine-lambda-call]
    co_return Value.f_Get();
  });
}
}

// Round 133: any number of references bound to conditionals of a capture
// refer to it.
namespace NRound133
{
#define DRound133Ref(d_New, d_Old) const int &r##d_New = _bChoose ? r##d_Old : r##d_Old;

void fg_Chain(bool _bChoose) {
  fg_CallSafe([_bChoose, Value = 42]() -> TCFuture<const int *> {
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: a lambda coroutine returns what refers to its captures, which are gone once what it is given to has run it; return a copy [malterlib-coroutine-lambda-call]
    const int &r0 = Value;
    DRound133Ref(1, 0) DRound133Ref(2, 1) DRound133Ref(3, 2) DRound133Ref(4, 3)
    DRound133Ref(5, 4) DRound133Ref(6, 5) DRound133Ref(7, 6) DRound133Ref(8, 7)
    DRound133Ref(9, 8) DRound133Ref(10, 9) DRound133Ref(11, 10)
    DRound133Ref(12, 11) DRound133Ref(13, 12) DRound133Ref(14, 13)
    DRound133Ref(15, 14) DRound133Ref(16, 15) DRound133Ref(17, 16)
    DRound133Ref(18, 17) DRound133Ref(19, 18) DRound133Ref(20, 19)
    co_return &r20;
  });
}
}

// Round 134: a constructor of the result keeps the address also through a
// local pointer or a conditional, not in a branch that never runs; an
// assignment of all of it replaces what came before, and a return that never
// runs gives nothing.
namespace NRound134
{
struct CSaved {
  const int *m_pValue;
  CSaved(const int &_Value) {
    const int *pSaved = &_Value;
    m_pValue = pSaved;
  }
};

void fg_Saved() {
  (void)fg_CallSafe([Value = 42]() -> TCFuture<CSaved> { co_return Value; });
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: a lambda coroutine returns what refers to its captures, which are gone once what it is given to has run it; return a copy [malterlib-coroutine-lambda-call]
}

struct CChosen {
  const int *m_pValue;
  CChosen(const int &_Value) : m_pValue(true ? &_Value : nullptr) {}
};

void fg_Chosen() {
  (void)fg_CallSafe([Value = 1]() -> TCFuture<CChosen> { co_return CChosen{Value}; });
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: a lambda coroutine returns what refers to its captures, which are gone once what it is given to has run it; return a copy [malterlib-coroutine-lambda-call]
}

struct CNever {
  const int *m_pValue = nullptr;
  CNever(const int &_Value) {
    if (false)
      m_pValue = &_Value;
  }
};

void fg_Never() {
  (void)fg_CallSafe([Value = 1]() -> TCFuture<CNever> { co_return CNever{Value}; });
}

struct CResult {
  const int *m_pValue;
};

void fg_Replaced() {
  (void)fg_CallSafe([Value = 42]() -> TCFuture<CResult> {
    CResult Result{&Value};
    Result = CResult{nullptr};
    co_return Result;
  });
}

void fg_Unreachable() {
  (void)fg_CallSafe([Value = 42]() -> TCFuture<const int *> {
    if (false)
      co_return &Value;
    co_return nullptr;
  });
}

void fg_AfterReturn() {
  (void)fg_CallSafe([Value = 42]() -> TCFuture<const int *> {
    co_return nullptr;
    co_return &Value;
  });
}
}

// Round 135: a constructor of the result keeps the address also through a
// local reference, not one a constant condition doesn't choose, and an
// aggregate made with parentheses binds its reference members.
namespace NRound135
{
struct CAlias {
  const int *m_pValue;
  CAlias(const int &_Value) {
    const int &Alias = _Value;
    m_pValue = &Alias;
  }
};

auto fg_Alias() {
  return fg_CallSafe([Value = 42]() -> TCFuture<CAlias> { co_return CAlias(Value); });
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: a lambda coroutine returns what refers to its captures, which are gone once what it is given to has run it; return a copy [malterlib-coroutine-lambda-call]
}

struct CNotChosen {
  const int *m_pValue;
  CNotChosen(const int &_Value) : m_pValue(false ? &_Value : nullptr) {}
};

auto fg_NotChosen() {
  return fg_CallSafe([Value = 42]() -> TCFuture<CNotChosen> { co_return CNotChosen(Value); });
}

struct CRef {
  int const &m_Value;
};

void fg_Parens() {
  (void)fg_CallSafe([Value = 42]() -> TCFuture<CRef> { co_return CRef(Value); });
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: a lambda coroutine returns what refers to its captures, which are gone once what it is given to has run it; return a copy [malterlib-coroutine-lambda-call]
}
}

namespace NRound138 {
// A reference member bound to what a pointer it is given points to, `m_Value(
// *_pValue)`, keeps it.
struct CReference {
  int const &m_Value;
  explicit CReference(int const *_pValue) : m_Value(*_pValue) {}
};

// But a copy of it doesn't.
struct CCopy {
  int m_Value;
  explicit CCopy(int const *_pValue) : m_Value(*_pValue) {}
};

void fg_Results() {
  auto Reference = fg_CallSafe([Value = 42]() -> TCFuture<CReference> {
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: a lambda coroutine returns what refers to its captures, which are gone once what it is given to has run it; return a copy [malterlib-coroutine-lambda-call]
    co_return CReference(&Value);
  });
  auto Copy = fg_CallSafe([Value = 42]() -> TCFuture<CCopy> {
    co_return CCopy(&Value);
  });
}
}

namespace NRound140 {
// A lambda in the default argument of a lambda in an operand of a rewritten
// operator.
struct CCompared {
  bool operator==(int) const;
};

bool fg_Compared() {
  return [](TCFuture<int> _Pending = [Value = 7]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:38: warning: a lambda coroutine that uses its captures after a suspension is called directly; the captures dangle when the closure is a temporary, so run it through fg_CallSafe, g_Dispatch, 'self /' or an actor functor [malterlib-coroutine-lambda-call]
           co_await CAwaiter{};
           co_return Value;
         }()) { return 0; }() == CCompared{};
}
}

namespace NRound141 {
struct CValue {
  int m_Value;
  int f_Read(int _Add) const { return m_Value + _Add; }
};

// The object of a call through what a pointer to it points to, `(&Value)[0]
// .f_Read(...)`, is used where the call runs.
TCFuture<int> fg_Indirect(CValue _Value) {
  return [_Value]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: a lambda coroutine that uses its captures after a suspension is called directly; the captures dangle when the closure is a temporary, so run it through fg_CallSafe, g_Dispatch, 'self /' or an actor functor [malterlib-coroutine-lambda-call]
    co_return (&_Value)[0].f_Read(co_await fg_Suspend());
  }();
}

// Copies of a closure through as many locals as there are, which are only
// given to fg_CallSafe.
void fg_Copies() {
  auto f0 = [Value = 42]() -> TCFuture<int> {
    co_await fg_Suspend();
    co_return Value;
  };
  auto f1 = f0;
  auto f2 = f1;
  auto f3 = f2;
  auto f4 = f3;
  auto f5 = f4;
  auto f6 = f5;
  fg_CallSafe(fg_Move(f6));
}
}

namespace NRound143 {
// The address of what a helper gives back of what it is given, `&fg_Same(
// _Value)`.
const int &fg_Same(const int &_Value) { return _Value; }

struct CHelped {
  const int *m_pValue;
  CHelped(const int &_Value) : m_pValue(&fg_Same(_Value)) {}
};

// Through the constructor of a base.
struct CBase {
  const int *m_pValue;
  CBase(const int &_Value) : m_pValue(&_Value) {}
};

struct CDerived : CBase {
  CDerived(const int &_Value) : CBase(_Value) {}
};

void fg_Results() {
  fg_CallSafe([Value = 1]() -> TCFuture<CHelped> { co_return CHelped{Value}; });
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: a lambda coroutine returns what refers to its captures, which are gone once what it is given to has run it; return a copy [malterlib-coroutine-lambda-call]
  fg_CallSafe([Value = 1]() -> TCFuture<CDerived> { co_return CDerived{Value}; });
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: a lambda coroutine returns what refers to its captures, which are gone once what it is given to has run it; return a copy [malterlib-coroutine-lambda-call]
}
}

namespace NRound144 {
// A braced member initializer, `m_pValue{&_Value}`.
struct CBraced {
  int const *m_pValue;
  CBraced(int const &_Value) : m_pValue{&_Value} {}
};

// A reference member bound to either of a conditional.
inline bool g_bChoose;
inline int g_Value;
struct CConditional {
  int const &m_Value;
  CConditional(int const &_Value) : m_Value(g_bChoose ? _Value : g_Value) {}
};

void fg_Results() {
  auto Braced = fg_CallSafe([Value = 42]() -> TCFuture<CBraced> { co_return Value; });
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: a lambda coroutine returns what refers to its captures, which are gone once what it is given to has run it; return a copy [malterlib-coroutine-lambda-call]
  auto Conditional =
      fg_CallSafe([Value = 42]() -> TCFuture<CConditional> { co_return Value; });
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: a lambda coroutine returns what refers to its captures, which are gone once what it is given to has run it; return a copy [malterlib-coroutine-lambda-call]
}
}

namespace NRound145 {
// Through a cast to a reference, `&static_cast<int const &>(_Value)`.
struct CCast {
  const int *m_pValue;
  CCast(const int &_Value) : m_pValue(&static_cast<const int &>(_Value)) {}
};

// Through more constructors of bases than are looked into.
template <int t_Depth>
struct TCDeep : TCDeep<t_Depth - 1> {
  TCDeep(const int &_Value) : TCDeep<t_Depth - 1>(_Value) {}
};
template <>
struct TCDeep<0> {
  const int *m_pValue;
  TCDeep(const int &_Value) : m_pValue(&_Value) {}
};

// A chain of conditionals of the same references, each told once, also one
// that refers to itself.
extern int g_Global;
extern bool g_bChoose;
struct CChain {
  const int *m_pValue;
  CChain(const int &_Value) {
    const int &r0 = g_Global;
    const int &r1 = g_bChoose ? r0 : r0;
    const int &r2 = g_bChoose ? r1 : r1;
    const int &r3 = g_bChoose ? r2 : r2;
    const int &r4 = g_bChoose ? r3 : r3;
    const int &r5 = g_bChoose ? r4 : r4;
    const int &r6 = g_bChoose ? r5 : r5;
    const int &r7 = g_bChoose ? r6 : r6;
    const int &r8 = g_bChoose ? r7 : r7;
    const int &r9 = g_bChoose ? r8 : r8;
    const int &r10 = g_bChoose ? r9 : r9;
    const int &r11 = g_bChoose ? r10 : r10;
    const int &r12 = g_bChoose ? r11 : r11;
    const int &r13 = g_bChoose ? r12 : r12;
    const int &r14 = g_bChoose ? r13 : r13;
    const int &r15 = g_bChoose ? r14 : r14;
    const int &r16 = g_bChoose ? r15 : r15;
    const int &r17 = g_bChoose ? r16 : r16;
    const int &r18 = g_bChoose ? r17 : r17;
    const int &r19 = g_bChoose ? r18 : r18;
    const int &r20 = g_bChoose ? r19 : r19;
    const int &r21 = g_bChoose ? r20 : r20;
    const int &r22 = g_bChoose ? r21 : r21;
    const int &r23 = g_bChoose ? r22 : r22;
    const int &r24 = g_bChoose ? r23 : r23;
    const int &r25 = g_bChoose ? r24 : r24;
    const int &r26 = g_bChoose ? r25 : r25;
    const int &r27 = g_bChoose ? r26 : r26;
    const int &r28 = g_bChoose ? r27 : r27;
    const int &r29 = g_bChoose ? r28 : r28;
    const int &r30 = g_bChoose ? r29 : r29;
    const int &r31 = g_bChoose ? r30 : r30;
    const int &r32 = g_bChoose ? r31 : r31;
    const int &r33 = g_bChoose ? r32 : r32;
    const int &r34 = g_bChoose ? r33 : r33;
    const int &r35 = g_bChoose ? r34 : r34;
    const int &r36 = g_bChoose ? r35 : r35;
    const int &r37 = g_bChoose ? r36 : r36;
    const int &r38 = g_bChoose ? r37 : r37;
    const int &r39 = g_bChoose ? r38 : r38;
    const int &r40 = g_bChoose ? r39 : r39;
    m_pValue = &r40;
  }
};

struct CItself {
  int const *m_pValue;
  CItself(int const &_Unused, bool _bChoose) {
    int const &Value = _bChoose ? Value : g_Global;
    m_pValue = &Value;
  }
};

void fg_Results(bool _bChoose) {
  fg_CallSafe([Value = 42]() -> TCFuture<CCast> { co_return CCast(Value); });
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: a lambda coroutine returns what refers to its captures, which are gone once what it is given to has run it; return a copy [malterlib-coroutine-lambda-call]
  fg_CallSafe([Value = 42]() -> TCFuture<TCDeep<17>> { co_return TCDeep<17>(Value); });
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: a lambda coroutine returns what refers to its captures, which are gone once what it is given to has run it; return a copy [malterlib-coroutine-lambda-call]
  fg_CallSafe([Value = 42]() -> TCFuture<CChain> { co_return CChain(Value); });
  fg_CallSafe([Value = 1, _bChoose]() -> TCFuture<CItself> { co_return CItself(Value, _bChoose); });
}
}

namespace NRound147 {
// The constructors of bases that many classes share are each looked at once.
int g_Global;
#define DRound147Leaf(K) \
	struct K##0 { \
		int m_Value; K##0(int const &_Value = ::NRound147::g_Global) : m_Value(_Value) {} \
	};
DRound147Leaf(CA) DRound147Leaf(CB) DRound147Leaf(CC)
#define DRound147One(K, N, P) \
	struct K##N : virtual CA##P, virtual CB##P, virtual CC##P { \
		K##N(int const &_Value = ::NRound147::g_Global) : CA##P(_Value), CB##P(_Value), CC##P(_Value) {} \
	};
#define DRound147Level(N, P) DRound147One(CA, N, P) DRound147One(CB, N, P) DRound147One(CC, N, P)
DRound147Level(1, 0) DRound147Level(2, 1) DRound147Level(3, 2) DRound147Level(4, 3) DRound147Level(5, 4)
DRound147Level(6, 5) DRound147Level(7, 6) DRound147Level(8, 7) DRound147Level(9, 8) DRound147Level(10, 9)

void fg_Shared(int _Value)
{
	fg_CallSafe([_Value]() -> TCFuture<CA10> { co_return CA10(_Value); });
}
}

namespace NRound148 {
// What a lambda called right away returns is the result, which points into
// the closure.
auto fg_Dangling()
{
	return [Value = 1]() -> TCFuture<const int *>
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: a lambda coroutine that uses its captures after a suspension is called directly; the captures dangle when the closure is a temporary, so run it through fg_CallSafe, g_Dispatch, 'self /' or an actor functor [malterlib-coroutine-lambda-call]
		{
			co_return [&Value] { return &Value; }();
		}()
	;
}
}

namespace NRound149 {
TCFuture<int> fg_Suspend();

// What never runs.
void fg_Never()
{
	if constexpr (false)
	{
		[Value = 1]() -> TCFuture<int>
			{
				co_await fg_Suspend();
				co_return Value;
			}
		();
	}
	if (false)
	{
		[Value = 1]() -> TCFuture<int>
			{
				co_await fg_Suspend();
				co_return Value;
			}
		();
	}
}

// Through more helpers that give back what they are given.
int const &fg_Same(int const &_Value)
{
	return _Value;
}
#define DRound149Same2(x) fg_Same(fg_Same(x))
#define DRound149Same8(x) DRound149Same2(DRound149Same2(DRound149Same2(DRound149Same2(x))))

void fg_Helpers()
{
	fg_CallSafe([Value = 1]() -> TCFuture<int const *> { co_return &DRound149Same8(DRound149Same8(Value)); });
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: a lambda coroutine returns what refers to its captures, which are gone once what it is given to has run it; return a copy [malterlib-coroutine-lambda-call]
}
}

namespace NRound151 {
struct CInfo
{
	char const *f_Name() const
	{
		return "Name";
	}
};

// A capture whose member function gives only what has static storage.
void fg_Static()
{
	auto Future = [Info = CInfo{}]() -> TCFuture<char const *> { co_return Info.f_Name(); }();
}
}

namespace NRound154 {
// The address of a capture, returned by a lambda called where it is made.
auto fg_Helper(int _Value) {
  return fg_CallSafe([_Value]() -> TCFuture<int const *> { co_return [&] { return &_Value; }(); });
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: a lambda coroutine returns what refers to its captures, which are gone once what it is given to has run it; return a copy [malterlib-coroutine-lambda-call]
}
}

namespace NRound155 {
int g_Global;
// What a constructor keeps through a local reference to what it is given.
struct CThroughAlias {
  int const *m_pValue;
  CThroughAlias(int const *_pValue) {
    int const &Alias = *_pValue;
    m_pValue = &Alias;
  }
};
// What the override of a virtual getter returns.
struct CBase {
  virtual int const *f_Get() const { return &g_Global; }
};
struct CDerived : CBase {
  int m_Value = 1;
  int const *f_Get() const override { return &m_Value; }
};
// Only what isn't in a discarded branch is returned.
struct CDiscarded {
  int m_Value;
  int const *f_Get() const {
    if constexpr (false)
      return &m_Value;
    return &g_Global;
  }
};
// Through what designates it after changing it.
struct CStepped {
  int *m_pValue;
  CStepped(int &_Value) : m_pValue(&++_Value) {}
};
// Through a long chain of local references.
struct CChain {
  int const *m_pValue;
  CChain(int const &_Value) {
    int const &A = _Value, &B = A, &C = B, &D = C, &E = D, &F = E;
    int const &G = F, &H = G, &I = H, &J = I, &K = J, &L = K;
    int const &M = L, &N = M, &O = N, &Q = O, &R = Q;
    m_pValue = &R;
  }
};
// Through a reference to a local, which is gone after the constructor.
struct CScratch {
  int m_Value;
  CScratch(int const &_Value) {
    int const *pScratch = nullptr;
    int const *&pAlias = pScratch;
    pAlias = &_Value;
    m_Value = *pScratch;
  }
};
// Kept by an initializer that the body resets first.
struct CReset {
  int const *m_pValue;
  CReset(int const &_Value) : m_pValue(&_Value) { m_pValue = nullptr; }
};
void fg_Test() {
  fg_CallSafe([Value = 1]() -> TCFuture<CThroughAlias> { co_return CThroughAlias(&Value); });
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: a lambda coroutine returns what refers to its captures, which are gone once what it is given to has run it; return a copy [malterlib-coroutine-lambda-call]
  fg_CallSafe([Object = CDerived{}]() -> TCFuture<int const *> { co_return static_cast<CBase const &>(Object).f_Get(); });
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: a lambda coroutine returns what refers to its captures, which are gone once what it is given to has run it; return a copy [malterlib-coroutine-lambda-call]
  fg_CallSafe([Value = CDiscarded{}]() -> TCFuture<int const *> { co_return Value.f_Get(); });
  fg_CallSafe([Value = 42]() mutable -> TCFuture<CStepped> { co_return CStepped{Value}; });
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: a lambda coroutine returns what refers to its captures, which are gone once what it is given to has run it; return a copy [malterlib-coroutine-lambda-call]
  fg_CallSafe([Value = 42]() -> TCFuture<CChain> { co_return CChain{Value}; });
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: a lambda coroutine returns what refers to its captures, which are gone once what it is given to has run it; return a copy [malterlib-coroutine-lambda-call]
  fg_CallSafe([Value = 42]() -> TCFuture<CScratch> { co_return CScratch{Value}; });
  fg_CallSafe([Value = 42]() -> TCFuture<CReset> { co_return CReset{Value}; });
}
}

namespace NRound156 {
// The address a helper returns, kept by a constructor.
int const *fg_Address(int const &_Value) { return &_Value; }
struct CResult {
  int const *m_pValue;
  CResult(int const &_Value) : m_pValue(fg_Address(_Value)) {}
};
auto fg_Test() {
  return fg_CallSafe([Value = 42]() -> TCFuture<CResult> { co_return Value; });
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: a lambda coroutine returns what refers to its captures, which are gone once what it is given to has run it; return a copy [malterlib-coroutine-lambda-call]
}
}

namespace NRound158 {
// A chain of conditionals of the same locals in a constructor.
extern bool g_bChoose;
int g_Global;
#define DRound158Step(d_Next, d_Previous) const int *p##d_Next = g_bChoose ? p##d_Previous : p##d_Previous;
struct CResult {
  const int *m_pValue;
  CResult(const int &) {
    const int *p0 = &g_Global;
    DRound158Step(1, 0) DRound158Step(2, 1) DRound158Step(3, 2) DRound158Step(4, 3)
    DRound158Step(5, 4) DRound158Step(6, 5) DRound158Step(7, 6) DRound158Step(8, 7)
    DRound158Step(9, 8) DRound158Step(10, 9) DRound158Step(11, 10) DRound158Step(12, 11)
    DRound158Step(13, 12) DRound158Step(14, 13) DRound158Step(15, 14) DRound158Step(16, 15)
    DRound158Step(17, 16) DRound158Step(18, 17) DRound158Step(19, 18) DRound158Step(20, 19)
    DRound158Step(21, 20) DRound158Step(22, 21) DRound158Step(23, 22) DRound158Step(24, 23)
    m_pValue = p24;
  }
};
void fg_Test() {
  int Value = 1;
  [Value]() -> TCFuture<CResult> { co_return CResult(Value); }();
}
}

namespace NRound164 {
int g_Value;
struct CObject {
  virtual int *f_Get() const final { return &g_Value; }
};
// A getter that gives only what has static storage and can't be overridden.
void fg_Final() {
  CObject Object;
  [Object]() -> TCFuture<int *> { co_return Object.f_Get(); }();
}
}

namespace NRound166 {
// A constructor whose body can't be seen may keep what it is given.
struct CResult {
  int const &m_Value;
  explicit CResult(int const &_Value);
};
auto fg_Hidden() {
  return fg_CallSafe(
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: a lambda coroutine returns what refers to its captures, which are gone once what it is given to has run it; return a copy [malterlib-coroutine-lambda-call]
      [Value = 42]() -> TCFuture<CResult> { co_return CResult(Value); });
}
// Not one that only makes what the result is computed from, `fg_Read(Json)`.
struct CJson {
  CJson(int const &_Value);
};
struct CInfo {
  int m_Value;
};
int fg_Read(CJson const &_Json);
auto fg_Computed() {
  return fg_CallSafe([Value = 42]() -> TCFuture<CInfo> {
    CJson Json(Value);
    CInfo Info;
    Info.m_Value = fg_Read(Json);
    co_return Info;
  });
}
// Not one that reads through it after moving along it.
struct CRead {
  int m_Value;
  CRead(int const *_pValue) : m_Value(*(_pValue + 0)) {}
};
auto fg_Read() {
  return fg_CallSafe(
      [Value = 42]() -> TCFuture<CRead> { co_return CRead(&Value); });
}
}

namespace NRound170 {
// Not a member of the result assigned something else after.
struct CResult {
  int const *m_pValue;
};
auto fg_Overwritten() {
  return fg_CallSafe([Value = 42]() -> TCFuture<CResult> {
    CResult Result{};
    Result.m_pValue = &Value;
    Result.m_pValue = nullptr;
    co_return Result;
  });
}
}

namespace NRound172 {
// The address of a member of what a helper is given, also called through a
// pointer to it that can't change.
struct CData {
  int m_Value;
};
int const *fg_Address(CData const &_Data) { return &_Data.m_Value; }
auto fg_Member() {
  return fg_CallSafe([Data = CData{42}]() -> TCFuture<int const *> {
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: a lambda coroutine returns what refers to its captures, which are gone once what it is given to has run it; return a copy [malterlib-coroutine-lambda-call]
    co_return fg_Address(Data);
  });
}
int const *fg_AddressOf(int const &_Value) { return &_Value; }
auto fg_Pointer() {
  return fg_CallSafe(
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: a lambda coroutine returns what refers to its captures, which are gone once what it is given to has run it; return a copy [malterlib-coroutine-lambda-call]
      [Value = 42, fGet = &fg_AddressOf]() -> TCFuture<int const *> {
        co_return fGet(Value);
      });
}
// Many copies of a closure are looked at in time linear in their number.
TCFuture<int> fg_Pause();
#define DOne                                                                  \
  {                                                                            \
    auto fCopy = fTask;                                                        \
    fg_CallSafe(fCopy);                                                        \
  }
#define DTwice(d_Code) d_Code d_Code
#define DSixteen(d_Code) DTwice(DTwice(DTwice(DTwice(d_Code))))
void fg_Copies() {
  auto fTask = [Value = 1]() -> TCFuture<int> {
    co_await fg_Pause();
    co_return Value;
  };
  DSixteen(DSixteen(DSixteen(DOne)))
}
#undef DSixteen
#undef DTwice
#undef DOne
}

namespace NRound175 {
// Kept only where a constant condition never runs.
struct CResult {
  int const *m_pValue;
  CResult(int const *_pValue) : m_pValue(nullptr) {
    if (false)
      m_pValue = _pValue;
  }
};
struct CChosen {
  int const *m_pValue;
  CChosen(int const *_pValue) : m_pValue(false ? _pValue : nullptr) {}
};
struct CKept {
  int const *m_pValue;
  CKept(int const *_pValue) : m_pValue(true ? _pValue : nullptr) {}
};
void fg_Run() {
  fg_CallSafe([Value = 1]() -> TCFuture<CResult> { co_return CResult(&Value); });
  fg_CallSafe([Value = 1]() -> TCFuture<CChosen> { co_return CChosen(&Value); });
  fg_CallSafe([Value = 1]() -> TCFuture<CKept> { co_return CKept(&Value); });
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: a lambda coroutine returns what refers to its captures, which are gone once what it is given to has run it; return a copy [malterlib-coroutine-lambda-call]
}
}

namespace NRound176 {
// A scope exit that also runs while an exception unwinds runs the lambda,
// and destroys it, when it ends.
void fg_Run(int _Value) {
  auto Cleanup = NMib::g_OnScopeExitWithException / [_Value]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: the closure of a lambda coroutine that uses its captures after a suspension is given to a scope exit, which can call it directly; give it only to fg_CallSafe, g_Dispatch, 'self /', an actor functor or another function that keeps it alive while it runs [malterlib-coroutine-lambda-call]
    co_await CAwaiter{};
    co_return _Value;
  };
}
}

namespace NRound177 {
int g_Value;
// A virtual getter called on a captured value, whose type is what it is.
struct CValue {
  virtual int *f_Get() const { return &g_Value; }
};
void fg_Run() {
  auto fRun = [Value = CValue{}]() -> TCFuture<int> {
    int *pValue = Value.f_Get();
    co_await CAwaiter{};
    co_return *pValue;
  };
  fRun();
}
}

namespace NRound179 {
int g_Value;
void fg_Run() {
  // What a lambda called by the name of its operator gives of a capture.
  fg_CallSafe([Value = 1]() -> TCFuture<int const *> {
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: a lambda coroutine returns what refers to its captures, which are gone once what it is given to has run it; return a copy [malterlib-coroutine-lambda-call]
    co_return [&Value] { return &Value; }.operator()();
  });
  // Not what a comma discards.
  fg_CallSafe([Value = 1]() -> TCFuture<int const *> {
    co_return ((void)&Value, &g_Value);
  });
}
// A shared pointer to the closure copied many times, only to fg_CallSafe.
TCFuture<int> fg_Copied() {
  auto p0 = fg_ToSharedPointer([n = 1]() -> TCFuture<int> {
    co_await CAwaiter{};
    co_return n;
  });
  auto p1 = p0;
  auto p2 = p1;
  auto p3 = p2;
  auto p4 = p3;
  auto p5 = p4;
  co_return co_await fg_CallSafe(p5);
}
}

namespace NRound180 {
struct CRef {
  int const &m_Value;
};
// A reference member bound to what a local pointer to a capture points to.
void fg_Run() {
  fg_CallSafe([Value = 1]() -> TCFuture<CRef> {
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: a lambda coroutine returns what refers to its captures, which are gone once what it is given to has run it; return a copy [malterlib-coroutine-lambda-call]
    auto *pValue = &Value;
    co_return CRef{*pValue};
  });
}
}

namespace NRound183 {
int g_Value;
// A virtual getter of a final class, through a copy of a reference.
struct CBase {
  virtual int *f_Get() const { return &g_Value; }
};
struct CValue final : CBase {};
auto fg_Start(CValue const &_Value) {
  return fg_CallSafe([_Value]() -> TCFuture<int *> { co_return _Value.f_Get(); });
}
}

namespace NRound191 {
// A closure given to the factory of an actor functor, which keeps it while it
// runs.
template <typename t_FFunction>
struct TCActorFunctor {
  template <typename tf_FFunction>
  TCActorFunctor(TCActor<> const &_Actor, tf_FFunction &&_fFunction) {}
  TCFuture<int> operator()() const;
};
template <typename tf_FFunction>
TCActorFunctor<TCFuture<int>()> fg_ActorFunctor(TCActor<> const &_Actor, tf_FFunction &&_fFunction) {
  return {_Actor, static_cast<tf_FFunction &&>(_fFunction)};
}
TCFuture<int> fg_Work();
void fg_Register(TCActor<> _Actor, int _Value) {
  auto fRead = fg_ActorFunctor(_Actor, [_Value]() -> TCFuture<int> {
    co_await fg_Work();
    co_return _Value;
  });
  (void)fRead();
  TCActorFunctor<TCFuture<int>()> fTyped(_Actor, [_Value]() -> TCFuture<int> {
    co_await fg_Work();
    co_return _Value;
  });
  (void)fTyped();
}
}

namespace NRound212
{
	// Not a result made by a constructor whose helper only copies what the
	// pointer it is given points to.
	struct CStr
	{
		char m_Text[2];
		CStr(char const *_pStr)
		{
			f_SetStr(_pStr);
		}
		char const *f_GetStr() const
		{
			return m_Text;
		}
		void f_SetStr(char const *_pStr)
		{
			m_Text[0] = _pStr[0];
			m_Text[1] = 0;
		}
	};
	// But one whose helper keeps it.
	struct CView
	{
		char const *m_pText;
		CView(char const *_pStr)
		{
			f_Set(_pStr);
		}
		void f_Set(char const *_pStr)
		{
			m_pText = _pStr;
		}
	};
	void fg_Run()
	{
		(void)fg_CallSafe
			(
				[Text = CStr("x")]() -> TCFuture<CStr>
				{
					co_return CStr(Text.f_GetStr());
				}
			)
		;
		(void)fg_CallSafe
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: a lambda coroutine returns what refers to its captures, which are gone once what it is given to has run it; return a copy [malterlib-coroutine-lambda-call]
			(
				[Text = CStr("x")]() -> TCFuture<CView>
				{
					co_return CView(Text.f_GetStr());
				}
			)
		;
	}
}

namespace NRound218
{
TCFuture<int> fg_Work();
// A shared scope exit calls what it is given too.
void fg_SharedScopeExit() {
  auto pCleanup = g_OnScopeExitShared / [Value = 7]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: the closure of a lambda coroutine that uses its captures after a suspension is given to a scope exit, which can call it directly; give it only to fg_CallSafe, g_Dispatch, 'self /', an actor functor or another function that keeps it alive while it runs [malterlib-coroutine-lambda-call]
    co_await fg_Work();
    co_return Value;
  };
}
}

namespace NRound225
{
// A copy made by helpers that call themselves, or copy by stepping a
// pointer, owns what it copies.
struct CStr {
  char m_Text[16];
  CStr(char const *_pStr) { f_SetStr(_pStr); }
  char const *f_GetStr() const { return m_Text; }
  void f_SetStr(char const *_pStr) {
    if (*_pStr == ' ')
      f_SetStr(_pStr + 1);
    else {
      m_Text[0] = *_pStr;
      m_Text[1] = 0;
    }
  }
};
void fg_Recursive() {
  (void)fg_CallSafe([Text = CStr("x")]() -> TCFuture<CStr> {
    co_return CStr(Text.f_GetStr());
  });
}
char *fg_StrCopy(char *o_pTo, char const *_pFrom) {
  while (*_pFrom)
    *o_pTo++ = *_pFrom++;
  *o_pTo = 0;
  return o_pTo;
}
struct CCopied {
  char m_Text[16];
  CCopied(char const *_pStr) { fg_StrCopy(m_Text, _pStr); }
  char const *f_GetStr() const { return m_Text; }
};
void fg_Stepped() {
  (void)fg_CallSafe([Text = CCopied("x")]() -> TCFuture<CCopied> {
    co_return CCopied(Text.f_GetStr());
  });
}
}

namespace NRound228
{
	// A local function wrapper moved into another, which is moved where it is
	// called, holds the lambda it is made with.
	TCFuture<int> fg_Run()
	{
		TCFunction<TCFuture<int> ()> fTask = [Value = 1]() -> TCFuture<int>
			{
				co_await fg_Suspend();
				co_return Value;
			}
		;
		auto fOther = fg_Move(fTask);
		co_return co_await fg_CallSafe(fg_Move(fOther));
	}
}

namespace NRound236 {
// What a local that is assigned what it held may still point into.
void fg_Kept(bool _bKeep) {
  int Value = 42;
  fg_CallSafe([Value, _bKeep]() -> TCFuture<int const *> {
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: a lambda coroutine returns what refers to its captures, which are gone once what it is given to has run it; return a copy [malterlib-coroutine-lambda-call]
    int const *pValue = &Value;
    pValue = _bKeep ? pValue : nullptr;
    co_return pValue;
  });
}
// Not what is assigned in what never runs.
void fg_Discarded() {
  int Value = 42;
  fg_CallSafe([Value]() -> TCFuture<int const *> {
    int const *pValue = nullptr;
    if constexpr (false)
      pValue = &Value;
    co_return pValue;
  });
}
void fg_Never() {
  int Value = 42;
  fg_CallSafe([Value]() -> TCFuture<int const *> {
    int const *pValue = nullptr;
    if (false)
      pValue = &Value;
    co_return pValue;
  });
}
}

namespace NRound238 {
TCFuture<int> fg_Suspend();
// Either of a conditional made into a local and given to fg_CallSafe.
TCFuture<int> fg_Selected(bool _bChoose) {
  auto pWork = fg_ToSharedPointer([Value = 42]() -> TCFuture<int> {
    co_await fg_Suspend();
    co_return Value;
  });
  auto pCopy = pWork;
  auto pSelected = _bChoose ? pWork : pCopy;
  co_return co_await fg_CallSafe(pSelected);
}
// A pointer of the closure assigned after it suspends is in the closure.
TCFuture<int> fg_Assigned() {
  return [pValue = static_cast<int *>(nullptr)]() mutable -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: a lambda coroutine that uses its captures after a suspension is called directly; the captures dangle when the closure is a temporary, so run it through fg_CallSafe, g_Dispatch, 'self /' or an actor functor [malterlib-coroutine-lambda-call]
    int Value = 42;
    co_await fg_Suspend();
    pValue = &Value;
    co_return *pValue;
  }();
}
}

namespace NRound239 {
// Copies of a shared closure made by conditionals, each told once.
#define DStep(d_N, d_P) auto p##d_N = _bMove ? fg_Move(p##d_P) : p##d_P
TCFuture<int> fg_Run(bool _bMove) {
  auto p0 = fg_ToSharedPointer([Value = 42]() -> TCFuture<int> {
    co_await CAwaiter{};
    co_return Value;
  });
  DStep(1, 0); DStep(2, 1); DStep(3, 2); DStep(4, 3); DStep(5, 4);
  DStep(6, 5); DStep(7, 6); DStep(8, 7); DStep(9, 8); DStep(10, 9);
  DStep(11, 10); DStep(12, 11); DStep(13, 12); DStep(14, 13); DStep(15, 14);
  DStep(16, 15); DStep(17, 16); DStep(18, 17); DStep(19, 18); DStep(20, 19);
  DStep(21, 20); DStep(22, 21); DStep(23, 22); DStep(24, 23); DStep(25, 24);
  DStep(26, 25); DStep(27, 26); DStep(28, 27); DStep(29, 28); DStep(30, 29);
  co_return co_await fg_CallSafe(p30);
}
#undef DStep
// A weak actor functor keeps the closure while it runs.
template <typename t_FFunction>
struct TCActorFunctorWeak {
  template <typename tf_FFunction>
  TCActorFunctorWeak(TCActor<> const &_Actor, tf_FFunction &&_fFunction) {}
  TCFuture<int> operator()() const;
};
template <typename tf_FFunction>
TCActorFunctorWeak<TCFuture<int>()>
fg_ActorFunctorWeak(TCActor<> const &_Actor, tf_FFunction &&_fFunction) {
  return {_Actor, static_cast<tf_FFunction &&>(_fFunction)};
}
void fg_Register(TCActor<> _Actor) {
  auto fRead = fg_ActorFunctorWeak(_Actor, [Value = 42]() -> TCFuture<int> {
    co_await CAwaiter{};
    co_return Value;
  });
  (void)fRead();
  TCActorFunctorWeak<TCFuture<int>()> fTyped(
      _Actor, [Value = 42]() -> TCFuture<int> {
        co_await CAwaiter{};
        co_return Value;
      });
  (void)fTyped();
}
}

// A call through a function wrapper where it doesn't run.
namespace NRound244
{
	constexpr bool gc_bDirect = false;
	TCFuture<int> fg_Work();

	TCFuture<int> fg_Run()
	{
		NMib::NFunction::TCFunction<TCFuture<int> ()> fWork = [Value = 1]() -> TCFuture<int>
			{
				co_await fg_Work();
				co_return Value;
			}
		;
		if (false)
			co_await fWork();
		if constexpr (gc_bDirect)
			co_return co_await fWork();
		else
			co_return co_await fg_CallSafe(fWork);
	}
}

// Given to fg_CallSafe through an unchanged local reference.
namespace NRound245
{
	TCFuture<int> fg_Work();

	TCFuture<int> fg_Run()
	{
		TCFunction<TCFuture<int> ()> fRead = [Value = 1]() -> TCFuture<int>
			{
				co_await fg_Work();
				co_return Value;
			}
		;
		auto &fAlias = fRead;
		co_return co_await fg_CallSafe(fAlias);
	}
}
