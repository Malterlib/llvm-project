// RUN: %check_clang_tidy -std=c++20-or-later %s malterlib-coroutine-reference-capture %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -I %S/Inputs

#include "malterlib-concurrency.h"

TCFuture<int> fg_Suspend();
int fg_CallLater(TCFunction<int ()> _fFunction);
TCFuture<int> fg_Get(int _Value);

TCFuture<int> fg_Captures()
{
	int Value = 0;
	int Other = 0;

	auto fByValue = [Value]() -> TCFuture<int>
		{
			co_await fg_Suspend();
			co_return Value;
		}
	;
	co_await (g_Dispatch / fByValue);

	// A direct call runs the lambda right away, where the variables are, until
	// it first suspends.
	co_await [&Value]() -> TCFuture<int>
		{
			co_return co_await fg_Get(Value);
		}
		()
	;

	co_await [&Value]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: a TCFuture coroutine lambda uses 'Value', which it captures by reference, after a suspension, when 'Value' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
		{
			co_await fg_Suspend();
			co_return Value;
		}
		()
	;

	co_await [&]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: a TCFuture coroutine lambda uses 'Other', which it captures by reference, after a suspension, when 'Other' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
		{
			int Sum = Value;
			co_await fg_Suspend();
			co_return Sum + Other;
		}
		()
	;

	// A reference to the variable is used like it.
	co_await [&Value]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: a TCFuture coroutine lambda uses 'Value', which it captures by reference, after a suspension, {{.*}}
		{
			int &Alias = Value;
			co_await fg_Suspend();
			co_return Alias;
		}
		()
	;

	auto fHeld = [&Value]() -> TCFuture<int>
		{
			int Copy = Value;
			co_await fg_Suspend();
			co_return Copy;
		}
	;
	co_await fHeld();
	co_await fg_CallSafe(fg_Move(fHeld));

	// A lambda that is dispatched can run after the variables are gone, even
	// before it suspends.
	co_await
		(
			g_Dispatch / [&Value]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: a TCFuture coroutine lambda captures 'Value' by reference but is not called directly where 'Value' is, so it can run after 'Value' is gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
			{
				co_return Value;
			}
		)
	;

	auto fDispatched = [&]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: a TCFuture coroutine lambda captures 'Value' by reference but is not called directly where 'Value' is, {{.*}}
		{
			co_return Value + Other;
		}
	;
	co_await fDispatched();
	co_await (g_Dispatch / fDispatched);

	auto fUnsafe = [&Value]() -> TCUnsafeFuture<int>
		{
			co_return Value;
		}
	;

	// Not a coroutine.
	auto fNotCoroutine = [&Value]() -> TCFuture<int>
		{
			return {};
		}
	;

	// The cleanup function of fg_AsyncDestroy may capture by reference.
	auto Cleanup = co_await fg_AsyncDestroy
		(
			[&Value]() -> TCFuture<int>
			{
				co_return Value;
			}
		)
	;
	auto fNamedCleanup = [&Value]() -> TCFuture<int>
		{
			co_return Value;
		}
	;
	auto NamedCleanup = co_await fg_AsyncDestroy(fg_Move(fNamedCleanup));
	auto MovedCleanup = co_await fg_AsyncDestroy
		(
			fg_Move
			(
				[&Value]() -> TCFuture<int>
				{
					co_return Value;
				}
			)
		)
	;

	co_return 0;
}

// A lambda that returns a TCUnsafeFuture may capture by reference what the
// function waiting for it keeps.
int fg_JoinedUnsafe(TCActor<> const &_Actor)
{
	int Value = 0;
	return (g_Dispatch(_Actor) / [&Value]() -> TCUnsafeFuture<int>
		{
			co_await fg_Suspend();
			co_return Value;
		}
	).f_CallSync();
}

// Waiting for the dispatched lambda, `f_CallSync()`, doesn't keep what it
// captures by reference: what is between the dispatch and the wait can leave
// the function first. Like a coroutine given references, it must run to
// completion, or return a TCUnsafeFuture when what it refers to is known to
// outlive it.
int fg_Joined(TCActor<> const &_Actor)
{
	int Value = 0;
	int Result = (g_Dispatch(_Actor) / [&Value]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:39: warning: a TCFuture coroutine lambda captures 'Value' by reference but is not called directly where 'Value' is, so it can run after 'Value' is gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
		{
			co_await fg_Suspend();
			co_return Value;
		}
	).f_CallSync();

	Result += fg_Dispatch
		(
			_Actor
			, [&]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: a TCFuture coroutine lambda captures 'Value' by reference but is not called directly where 'Value' is, so it can run after 'Value' is gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
			{
				co_await fg_Suspend();
				co_return Value;
			}
		)
		.f_CallSync()
	;

	Result += _Actor.f_Bind<&CActor::f_DispatchWithReturn<TCFuture<int>>>
		(
			[&]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: a TCFuture coroutine lambda captures 'Value' by reference but is not called directly where 'Value' is, so it can run after 'Value' is gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
			{
				co_await fg_Suspend();
				co_return Value;
			}
		)
		.f_CallSync()
	;

	// A timeout lets the function go on while the lambda runs.
	Result += fg_Dispatch
		(
			_Actor
			, [&]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: a TCFuture coroutine lambda captures 'Value' by reference but is not called directly where 'Value' is, {{.*}}
			{
				co_await fg_Suspend();
				co_return Value;
			}
		)
		.f_CallSync(1.0)
	;

	// A block called directly keeps the variables of the function around it.
	Result += [&]()
		{
			return fg_Dispatch
				(
					_Actor
					, [&]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: a TCFuture coroutine lambda captures 'Value' by reference but is not called directly where 'Value' is, so it can run after 'Value' is gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
					{
						co_await fg_Suspend();
						co_return Value;
					}
				)
				.f_CallSync()
			;
		}
		()
	;

	// The variables of another function are not kept by waiting in a lambda
	// that can run later.
	TCFunction<int ()> fOuter = [&]()
		{
			return fg_Dispatch
				(
					_Actor
					, [&]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: a TCFuture coroutine lambda captures 'Value' by reference but is not called directly where 'Value' is, {{.*}}
					{
						co_await fg_Suspend();
						co_return Value;
					}
				)
				.f_CallSync()
			;
		}
	;

	return Result + fg_CallLater(fg_Move(fOuter));
}

// A coroutine doesn't wait in f_CallSync: it is not to block its actor.
TCFuture<int> fg_JoinedInCoroutine(TCActor<> _Actor)
{
	int Value = 0;
	int Result = fg_Dispatch
		(
			_Actor
			, [&]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: a TCFuture coroutine lambda captures 'Value' by reference but is not called directly where 'Value' is, {{.*}}
			{
				co_await fg_Suspend();
				co_return Value;
			}
		)
		.f_CallSync()
	;
	co_return Result;
}

// A lambda around it that can run later doesn't keep the variable, even if it
// calls the coroutine directly.
TCFunction<int ()> fg_Escaping()
{
	int Value = 0;
	return [&Value]() -> int
		{
			fg_CallSafe
				(
					[&Value]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: a TCFuture coroutine lambda captures 'Value' by reference but is not called directly where 'Value' is, {{.*}}
					{
						int Copy = Value;
						co_await fg_Suspend();
						co_return Copy;
					}
				)
			;
			return 0;
		}
	;
}

// A lambda around it that holds a copy keeps it while it runs.
TCFunction<int ()> fg_Copied()
{
	int Value = 0;
	return [Value]() -> int
		{
			fg_CallSafe
				(
					[&Value]() -> TCFuture<int>
					{
						int Copy = Value;
						co_await fg_Suspend();
						co_return Copy;
					}
				)
			;
			return 0;
		}
	;
}

// fg_CallSafe calls only its first argument right away.
TCFuture<int> fg_CallSafeArgument()
{
	int Value = 0;
	co_return co_await fg_CallSafe
		(
			[](auto _fCallback) -> TCFuture<int>
			{
				co_await fg_Suspend();
				co_return co_await _fCallback();
			}
			, [&Value]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: a TCFuture coroutine lambda captures 'Value' by reference but is not called directly where 'Value' is, {{.*}}
			{
				co_return Value;
			}
		)
	;
}

// Only the dispatched function is joined, not what it is given.
template <typename t_FFunction, typename t_FCallback>
TCFuture<int> fg_Dispatch(TCActor<> const &_Actor, t_FFunction &&_fFunction, t_FCallback &&_fCallback)
{
	return {};
}

int fg_JoinedCallback(TCActor<> const &_Actor)
{
	int Value = 0;
	int Result = fg_Dispatch
		(
			_Actor
			, [](auto _fCallback)
			{
				return 0;
			}
			, [&Value]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: a TCFuture coroutine lambda captures 'Value' by reference but is not called directly where 'Value' is, {{.*}}
			{
				co_await fg_Suspend();
				co_return Value;
			}
		)
		.f_CallSync()
	;
	struct CStore;
	Result += _Actor.f_Bind<&CActor::f_DispatchWithReturn<int>>
		(
			[&Value]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: a TCFuture coroutine lambda captures 'Value' by reference but is not called directly where 'Value' is, so it can run after 'Value' is gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
			{
				co_await fg_Suspend();
				co_return Value;
			}
		)
		.f_CallSync()
	;
	return Result;
}

// A lambda held in a local and joined, in a generic block, and a reference
// init capture of a member of a local.
struct CData
{
	int m_Value = 0;
};

// A dependent initializer of a local.
template <typename t_CType>
void fg_DependentInit(int _Value)
{
	t_CType Object([_Value]() -> TCFuture<int>
		{
			co_await fg_Suspend();
			co_return _Value;
		}
	);
}

TCFuture<int> fg_ExplicitCall()
{
	int Value = 0;
	auto fGet = [&Value]() -> TCFuture<int>
		{
			int Copy = Value;
			co_await fg_Suspend();
			co_return Copy;
		}
	;
	co_return co_await fGet.operator ()();
}

// A join with only a run loop, and an element of a local array.
struct CRunLoop;

// A view captured by reference still views the data when it is exchanged out,
// and so does a pointer to a local, which a copy of it points to.
struct CViewed
{
};

struct CStrSpan
{
	CStrSpan() = default;
	CStrSpan(CViewed const &_Data);
	char const *m_pData = nullptr;
};

int fg_Length(CStrSpan _Span);

TCFuture<int> fg_ExchangedView(CViewed const &_Data)
{
	CViewed Data = _Data;
	CStrSpan Span = Data;
	int Value = 0;
	int *pValue = &Value;
	co_await [&Span]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: a TCFuture coroutine lambda uses 'Span', which it captures by reference, after a suspension, {{.*}}
		{
			CStrSpan Copy = fg_Exchange(Span, CStrSpan{});
			co_await fg_Suspend();
			co_return fg_Length(Copy);
		}
		()
	;
	co_return co_await [&pValue]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a TCFuture coroutine lambda uses 'pValue', which it captures by reference, after a suspension, {{.*}}
		{
			int *pCopy = fg_Exchange(pValue, nullptr);
			co_await fg_Suspend();
			co_return *pCopy;
		}
		()
	;
}

// A lambda that is the result of a statement expression leaves the scope of
// what it captures.
TCFuture<int> fg_StatementExpression()
{
	auto fGet = ({ int Value = 1; [&Value]() -> TCFuture<int> { int Copy = Value; co_await fg_Suspend(); co_return Copy; }; });
// CHECK-MESSAGES: :[[@LINE-1]]:34: warning: a TCFuture coroutine lambda captures 'Value' by reference but is not called directly where 'Value' is, {{.*}}
	co_return co_await fGet();
}

// An explicit call of the call operator of a generic lambda in a template.
template <typename t_CType>
TCFuture<int> fg_ExplicitGenericCall()
{
	int Value = 0;
	co_return co_await [&Value]<typename tf_CType>() -> TCFuture<int>
		{
			int Copy = Value;
			co_await fg_Suspend();
			co_return Copy;
		}
		.template operator ()<t_CType>()
	;
}

// A statement expression whose lambda captures a copy that needs cleanups, and
// a member of the object, which a blocking join keeps.
struct CNonTrivial
{
	CNonTrivial();
	CNonTrivial(CNonTrivial const &);
	~CNonTrivial();
};

TCFuture<int> fg_StatementExpressionCleanups()
{
	auto fGet = ({ int Value = 1; CNonTrivial Data; [Data, &Value]() -> TCFuture<int> { int Copy = Value; co_await fg_Suspend(); co_return Copy; }; });
// CHECK-MESSAGES: :[[@LINE-1]]:58: warning: a TCFuture coroutine lambda captures 'Value' by reference but is not called directly where 'Value' is, {{.*}}
	co_return co_await fGet();
}

struct CJoinedMember
{
	int f_Joined(TCActor<> const &_Actor)
	{
		return (g_Dispatch(_Actor) / [&Value = m_Value]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:34: warning: a TCFuture coroutine lambda captures 'Value' by reference but is not called directly where 'Value' is, so it can run after 'Value' is gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
			{
				co_await fg_Suspend();
				co_return Value;
			}
		).f_CallSync();
	}

	int m_Value = 0;
};

// Without a suspension, a result that can't refer to anything keeps nothing.
struct CText
{
	char const *m_pData = nullptr;
};

CText fg_TextOf(int const &_Value);
int fg_LengthOf(CText const &_Text);

TCFuture<int> fg_NoSuspensionResult()
{
	int Value = 0;
	co_return co_await [&Value]() -> TCFuture<int>
		{
			co_return fg_LengthOf(fg_TextOf(Value));
		}
		()
	;
}

// The cleanup of fg_AsyncDestroy runs when its guard is destroyed, while the
// variables declared before it are there, until its first suspension; a
// factory that makes it is followed to its calls.
TCFuture<int> fg_AsyncDestroyCleanups()
{
	int Value = 0;
	auto fMakeCleanup = [&](int const &_Value)
		{
			return [&]() -> TCFuture<int>
				{
					int Copy = _Value + Value;
					co_await fg_Suspend();
					co_return Copy;
				}
			;
		}
	;
	auto Cleanup = co_await fg_AsyncDestroy(fMakeCleanup(Value));
	auto Direct = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: a TCFuture coroutine lambda uses 'Value', which it captures by reference, after a suspension, {{.*}}
			{
				co_await fg_Suspend();
				co_return Value;
			}
		)
	;
	auto fMakeTemporary = [&](int const &_Value)
		{
			return [&]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: a TCFuture coroutine lambda captures '_Value' by reference but is not called directly where '_Value' is, {{.*}}
				{
					int Copy = _Value;
					co_await fg_Suspend();
					co_return Copy;
				}
			;
		}
	;
	auto Temporary = co_await fg_AsyncDestroy(fMakeTemporary(1));
	co_return 0;
}

// Awaited early and wrapped, the cleanup runs while the variable is there.
TCFuture<int> fg_WrappedEarlyCleanup()
{
	int Value = 0;
	auto Cleanup = co_await fg_AsyncDestroy
		(
			[&Value]() -> TCFuture<int>
			{
				++Value;
				co_return 0;
			}
		)
	;
	co_await fg_Move(Cleanup).f_Wrap();
	co_return Value;
}

// A test category runs its lambda to completion before it returns, while the
// locals it captures are there.
struct CTestCategoryScope
{
	template <typename tf_CFunction>
	void operator % (tf_CFunction &&_fCategory)
	{
	}
};
void fg_TestCategoryCaptures()
{
	for (int iSize = 0; iSize < 2; ++iSize)
	{
		CTestCategoryScope() % [&]() -> TCFuture<int>
			{
				co_await fg_Suspend();
				co_return iSize;
			}
		;
		// A category in a category runs while the locals around both are there.
		CTestCategoryScope() % [&]()
			{
				for (int iThreads = 0; iThreads < 2; ++iThreads)
				{
					CTestCategoryScope() % [&]() -> TCFuture<int>
						{
							co_await fg_Suspend();
							co_return iSize + iThreads;
						}
					;
				}
			}
		;
	}
}

// A run loop that can't be had throws before the wait.
struct CEdgeRunLoop;
TCSharedPointer<CEdgeRunLoop> fg_GetEdgeRunLoop();
int fg_ThrowingRunLoop()
{
	int Value = 1;
	return (g_ConcurrentDispatch / [&Value]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:35: warning: a TCFuture coroutine lambda captures 'Value' by reference but is not called directly where 'Value' is, so it can run after 'Value' is gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
		{
			co_await fg_Suspend();
			co_return Value;
		}
	).f_CallSync(fg_GetEdgeRunLoop());
}

// A cleanup chosen by a conditional, and a reference init capture of what has
// static storage.
TCFuture<int> fg_ChosenCleanup(bool _bChoose)
{
	int Value = 0;
	auto Cleanup = _bChoose
		? co_await fg_AsyncDestroy
			(
				[&Value]() -> TCFuture<int>
				{
					++Value;
					co_return 0;
				}
			)
		: co_await fg_AsyncDestroy
			(
				[&Value]() -> TCFuture<int>
				{
					++Value;
					co_return 0;
				}
			)
	;
	co_return 0;
}
TCFuture<int> fg_StaticAlias()
{
	static int const s_Value = 42;
	co_return co_await fg_CallSafe
		(
			[&Value = s_Value]() -> TCFuture<int>
			{
				co_await fg_Suspend();
				co_return Value;
			}
		)
	;
}

// A cleanup awaited early through a reference to its guard.
TCFuture<int> fg_CleanupAwaitedThroughAlias()
{
	int *pValue = nullptr;
	auto Cleanup = co_await fg_AsyncDestroy
		(
			[&pValue]() -> TCFuture<int>
			{
				co_return pValue ? *pValue : 0;
			}
		)
	;
	auto &Alias = Cleanup;
	co_await fg_Move(Alias);
	co_return 0;
}

// A static reference lives only as long as what it refers to.
TCFuture<int> fg_StaticReferenceCapture(int &_Value)
{
	static int &s_Saved = _Value;
	return fg_CallSafe
		(
			[&Alias = s_Saved]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: a TCFuture coroutine lambda uses 'Alias', which it captures by reference, after a suspension, {{.*}}
			{
				co_await fg_Suspend();
				co_return Alias;
			}
		)
	;
}

// A copy of a pointer into a local borrows the local like a capture of it by
// reference.
void fg_KeepCallback(TCFunction<TCFuture<int> ()> _fCallback);
TCFuture<int> fg_PointerCaptures()
{
	int Value = 0;
	int Values[2] = {};
	int *pValue = &Value;

	fg_KeepCallback
		(
			[pValue = &Value]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: a TCFuture coroutine lambda captures 'pValue', which points to 'Value', but is not called directly where 'Value' is, so it can run after 'Value' is gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
			{
				co_return *pValue;
			}
		)
	;
	fg_KeepCallback
		(
			[pValue]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: a TCFuture coroutine lambda captures 'pValue', which points to 'Value', but is not called directly where 'Value' is, so it can run after 'Value' is gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
			{
				co_return *pValue;
			}
		)
	;
	fg_KeepCallback
		(
			[pElement = &Values[1]]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: a TCFuture coroutine lambda captures 'pElement', which points to 'Values', but is not called directly where 'Values' is, so it can run after 'Values' is gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
			{
				co_return *pElement;
			}
		)
	;

	// Called directly, it may use it until it first suspends.
	co_await [pValue = &Value]() -> TCFuture<int>
		{
			co_return *pValue;
		}
		()
	;
	co_await [pValue = &Value]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: a TCFuture coroutine lambda uses 'pValue', which points to 'Value', after a suspension, when 'Value' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
		{
			co_await fg_Suspend();
			co_return *pValue;
		}
		()
	;
	co_return 0;
}

// The address of a local through a reference to it, and a view made of a
// local array, borrow the local too.
struct CRound54StrSpan
{
	char const *m_pData;
	int m_Length;
};
TCFuture<int> fg_Round54Borrows()
{
	int Value = 42;
	int &Alias = Value;
	fg_KeepCallback
		(
			[pValue = &Alias]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: a TCFuture coroutine lambda captures 'pValue', which points to 'Value', but is not called directly where 'Value' is, so it can run after 'Value' is gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
			{
				co_return *pValue;
			}
		)
	;
	char Buffer[8] = {};
	fg_KeepCallback
		(
			[Span = CRound54StrSpan{Buffer, 8}]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: a TCFuture coroutine lambda captures 'Span', which points to 'Buffer', but is not called directly where 'Buffer' is, so it can run after 'Buffer' is gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
			{
				co_return Span.m_pData[0];
			}
		)
	;
	co_return 0;
}

// A pointer moved along a local array, a pointer capture of a coroutine that
// is joined, and a copy of a captured pointer.
TCFuture<int> fg_Round59Moved()
{
	int Values[2] = {1, 2};
	return fg_CallSafe
		(
			[pValue = Values + 1]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: a TCFuture coroutine lambda {{.*}}
			{
				co_await fg_Suspend();
				co_return *pValue;
			}
		)
	;
}
auto fg_Round59Copied()
{
	int Local = 1;
	return fg_CallSafe
		(
			[pLocal = &Local]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: a TCFuture coroutine lambda {{.*}}
			{
				int *pSaved = pLocal;
				co_await fg_Suspend();
				co_return *pSaved;
			}
		)
	;
}

// A pointer to either of two locals, `bChoose ? &First : &Second`, points to
// a local.
TCFuture<int> fg_Round67Conditional(bool _bChoose)
{
	int First = 1;
	int Second = 2;
	fg_KeepCallback
		(
			[pValue = _bChoose ? &First : &Second]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: a TCFuture coroutine lambda captures 'pValue', which points to 'Second', but is not called directly where 'Second' is, so it can run after 'Second' is gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
			{
				co_return *pValue;
			}
		)
	;
	co_return 0;
}

// A pointer captured after it is assigned the address of a local points to
// the local.
TCFuture<int> fg_Round68Assigned()
{
	int Value = 42;
	int *pValue = nullptr;
	pValue = &Value;
	fg_KeepCallback
		(
			[pValue]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: a TCFuture coroutine lambda captures 'pValue', which points to 'Value', but is not called directly where 'Value' is, so it can run after 'Value' is gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
			{
				co_return *pValue;
			}
		)
	;
	co_return 0;
}

// A pointer to a local cast to another pointer still points to the local.
void fg_Round74Cast()
{
	int Value = 7;
	fg_CallSafe
		(
			[pValue = static_cast<int const *>(&Value)]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: a TCFuture coroutine lambda uses 'pValue', which points to 'Value', after a suspension, when 'Value' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
			{
				co_await fg_Suspend();
				co_return *pValue;
			}
		)
	;
}

// A pointer made from another on either side of conditionals is followed
// once per local.
void fg_Round75Chain(bool _bChoose)
{
	int Value = 1;
	int *pAlias0 = &Value;
	int *pAlias1 = _bChoose ? pAlias0 : pAlias0;
	int *pAlias2 = _bChoose ? pAlias1 : pAlias1;
	int *pAlias3 = _bChoose ? pAlias2 : pAlias2;
	int *pAlias4 = _bChoose ? pAlias3 : pAlias3;
	int *pAlias5 = _bChoose ? pAlias4 : pAlias4;
	int *pAlias6 = _bChoose ? pAlias5 : pAlias5;
	int *pAlias7 = _bChoose ? pAlias6 : pAlias6;
	int *pAlias8 = _bChoose ? pAlias7 : pAlias7;
	int *pAlias9 = _bChoose ? pAlias8 : pAlias8;
	int *pAlias10 = _bChoose ? pAlias9 : pAlias9;
	int *pAlias11 = _bChoose ? pAlias10 : pAlias10;
	int *pAlias12 = _bChoose ? pAlias11 : pAlias11;
	int *pAlias13 = _bChoose ? pAlias12 : pAlias12;
	int *pAlias14 = _bChoose ? pAlias13 : pAlias13;
	int *pAlias15 = _bChoose ? pAlias14 : pAlias14;
	int *pAlias16 = _bChoose ? pAlias15 : pAlias15;
	int *pAlias17 = _bChoose ? pAlias16 : pAlias16;
	int *pAlias18 = _bChoose ? pAlias17 : pAlias17;
	int *pAlias19 = _bChoose ? pAlias18 : pAlias18;
	int *pAlias20 = _bChoose ? pAlias19 : pAlias19;
	int *pAlias21 = _bChoose ? pAlias20 : pAlias20;
	int *pAlias22 = _bChoose ? pAlias21 : pAlias21;
	int *pAlias23 = _bChoose ? pAlias22 : pAlias22;
	fg_CallSafe
		(
			[pValue = pAlias23]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: a TCFuture coroutine lambda uses 'pValue', which points to 'Value', after a suspension, when 'Value' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
			{
				co_await fg_Suspend();
				co_return *pValue;
			}
		)
	;
}

// The address of a local taken with a builtin.
TCFuture<int> fg_Round78AddressOf()
{
	int Value = 1;
	return fg_CallSafe
		(
			[pValue = __builtin_addressof(Value)]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: a TCFuture coroutine lambda uses 'pValue', which points to 'Value', after a suspension, when 'Value' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
			{
				co_await fg_Suspend();
				co_return *pValue;
			}
		)
	;
}

// A coroutine lambda made by another one before it suspends, which is called
// right away where the variable is, runs while it is there.
TCFuture<int> fg_Round79Nested()
{
	int Value = 1;
	co_return co_await fg_CallSafe
		(
			[&]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: a TCFuture coroutine lambda uses 'Value', which it captures by reference, after a suspension, when 'Value' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
			{
				co_return co_await fg_CallSafe
					(
						[&]() -> TCFuture<int>
						{
							co_return Value;
						}
					)
				;
			}
		)
	;
}

// A pointer to a temporary that a local reference extends, a constructor
// that can throw before the join, a default argument that can throw, and a
// negative timeout, which waits without one.
TCFuture<int> fg_Round82Extended()
{
	int const &Value = 42;
	return fg_CallSafe
		(
			[pValue = &Value]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: a TCFuture coroutine lambda uses 'pValue', which points to 'Value', after a suspension, when 'Value' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
			{
				co_await fg_Suspend();
				co_return *pValue;
			}
		)
	;
}
struct CRound82Context
{
	CRound82Context()
	{
		throw 1;
	}
};
int fg_Round82Context()
{
	int Value = 42;
	return
		(
			(
				g_ConcurrentDispatch / [&Value]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:30: warning: a TCFuture coroutine lambda captures 'Value' by reference but is not called directly where 'Value' is, so it can run after 'Value' is gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
				{
					co_await fg_Suspend();
					co_return Value;
				}
			)
			% CRound82Context{}
		)
		.f_Dispatch()
		.f_CallSync()
	;
}
int fg_Round82Fail();
TCSharedPointer<CRunLoop> fg_Round82RunLoop(int _Value = fg_Round82Fail()) noexcept;
int fg_Round82Default()
{
	int Value = 0;
	return
		(
			g_ConcurrentDispatch / [&Value]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:29: warning: a TCFuture coroutine lambda captures 'Value' by reference but is not called directly where 'Value' is, so it can run after 'Value' is gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
			{
				co_await fg_Suspend();
				co_return Value;
			}
		)
		.f_CallSync(fg_Round82RunLoop())
	;
}
// A cast to a reference that checks the type can throw before the join, and
// a pack of futures that is joined joins the dispatch.
struct CRound88Base
{
	virtual ~CRound88Base() = default;
};
struct CRound88Derived : CRound88Base
{
	char const *m_pMessage;
};
struct TCFuturePack
{
	int f_CallSync(double _Timeout = -1.0) &&;
};
TCFuturePack operator + (TCFuture<int> &&_Left, TCFuture<int> &&_Right);
TCFuture<int> fg_Round88Work();
void fg_Round88Cast(CRound88Base &_Base)
{
	int Local = 42;
	(
		(
			g_ConcurrentDispatch / [&Local]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:29: warning: a TCFuture coroutine lambda captures 'Local' by reference but is not called directly where 'Local' is, so it can run after 'Local' is gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
			{
				co_await fg_Round88Work();
				co_return Local;
			}
		)
		% dynamic_cast<CRound88Derived &>(_Base).m_pMessage
	).f_CallSync();
}
// A reference to a pointer captured by copy copies the pointer.
TCFuture<int> fg_Round89Capture()
{
	int Value = 42;
	int *pValue = &Value;
	int *&pAlias = pValue;
	return fg_CallSafe
		(
			[pAlias]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: a TCFuture coroutine lambda uses 'pAlias', which points to 'Value', after a suspension, when 'Value' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
			{
				co_await fg_Round88Work();
				co_return *pAlias;
			}
		)
	;
}

// Guards held in an array, and a guard awaited early through a cast.
TCFuture<int> fg_Round90Array()
{
	int Value = 0;
	auto fCleanup = [&Value]() -> TCFuture<int>
		{
			co_return Value;
		}
	;
	using CGuard = decltype(fg_AsyncDestroy(fCleanup).await_resume());
	CGuard Guards[] = {co_await fg_AsyncDestroy(fCleanup)};
	co_return 0;
}
TCFuture<int> fg_Round90Cast()
{
	int Value = 0;
	auto Guard = co_await fg_AsyncDestroy
		(
			[&Value]() -> TCFuture<int>
			{
				co_return Value;
			}
		)
	;
	co_await static_cast<decltype(Guard) &&>(Guard);
	co_return 0;
}

// A pack joined with a future of a function that throws before it gives it.
TCFuture<int> fg_Round90Work();
TCFuture<int> fg_Round90Throws()
{
	throw 1;
}
void fg_Round90Pack()
{
	int Value = 42;
	(
		(
			g_ConcurrentDispatch / [&Value]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:29: warning: a TCFuture coroutine lambda captures 'Value' by reference but is not called directly where 'Value' is, so it can run after 'Value' is gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
			{
				co_await fg_Round90Work();
				co_return Value;
			}
		)
		+ fg_Round90Throws()
	).f_CallSync();
}

// A generic lambda whose result depends on what it is called with is
// checked where it is instantiated.
template <typename t_FCallable>
TCFuture<int> fg_Round90CallWithWork(t_FCallable &&_fCallable)
{
	return _fCallable(fg_Round90Work());
}
TCFuture<int> fg_Round90Generic()
{
	int Value = 42;
	return fg_Round90CallWithWork
		(
			[&Value](auto _Future) -> decltype(_Future)
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: a TCFuture coroutine lambda captures 'Value' by reference but is not called directly where 'Value' is, so it can run after 'Value' is gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
			{
				co_await fg_Move(_Future);
				co_return Value;
			}
		)
	;
}

// What a thread owns is gone when the thread ends.
struct CRound90Data
{
	int m_Value;
	~CRound90Data();
};
thread_local CRound90Data g_Round90Data;
TCFuture<int> fg_Round90ThreadLocal()
{
	return fg_ConcurrentDispatch
		(
			[&Data = g_Round90Data]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: a TCFuture coroutine lambda captures 'Data' by reference but is not called directly where 'Data' is, so it can run after 'Data' is gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
			{
				co_return Data.m_Value;
			}
		)
	;
}

// A copy made in a template copies in each instantiation.
template <typename t_CType>
TCFuture<int> fg_Round90Copy(t_CType &_Value)
{
	return fg_CallSafe
		(
			[&_Value]() -> TCFuture<int>
			{
				t_CType Copy = _Value;
				co_await fg_Round90Work();
				co_return Copy;
			}
		)
	;
}
void fg_Round90UseCopy()
{
	int Value = 1;
	fg_Round90Copy(Value);
}

// A copy of a view refers where it does, and a pointer into what a thread
// owns is gone when the thread ends.
struct CRound92StrSpan
{
	char const *m_pData;
	int m_Size;
	CRound92StrSpan(char const *_pData, int _Size) : m_pData(_pData), m_Size(_Size) {}
	char f_Read() const
	{
		return *m_pData;
	}
};
TCFuture<int> fg_Round92Work();
TCFuture<int> fg_Round92Copy()
{
	char Buffer[1] = {'x'};
	CRound92StrSpan Span(Buffer, 1);
	CRound92StrSpan Copy = Span;
	return fg_CallSafe
		(
			[Copy]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: a TCFuture coroutine lambda uses 'Copy', which points to 'Buffer', after a suspension, when 'Buffer' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
			{
				co_await fg_Round92Work();
				co_return Copy.f_Read();
			}
		)
	;
}
thread_local int g_Round92ThreadData = 42;
TCFuture<int> fg_Round92ThreadLocal()
{
	return fg_CallSafe
		(
			[pData = &g_Round92ThreadData]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: a TCFuture coroutine lambda captures 'pData', which points to 'g_Round92ThreadData', but is not called directly where 'g_Round92ThreadData' is, so it can run after 'g_Round92ThreadData' is gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
			{
				co_await fg_Round92Work();
				co_return *pData;
			}
		)
	;
}

TCFuture<int> fg_Round93Suspend();

// A copy of a pointer through a reference to it is what the pointer is where
// the lambda captures it.
void fg_Round93ReferenceToPointer()
{
	int *pValue = nullptr;
	int *&pAlias = pValue;
	int Value = 1;
	pValue = &Value;

	fg_CallSafe
		(
			[pAlias]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: a TCFuture coroutine lambda uses 'pAlias', which points to 'Value', after a suspension, when 'Value' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
			{
				co_await fg_Round93Suspend();
				co_return *pAlias;
			}
		)
	;
}

// A `%` that can throw before the call is joined.
struct CRound93Context
{
};
TCFuture<int> operator % (TCFuture<int> &&_Future, CRound93Context _Context)
{
	throw 1;
}

void fg_Round93ThrowingOperator()
{
	int Value = 1;
	(
		(
			g_Dispatch / [&Value]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: a TCFuture coroutine lambda captures 'Value' by reference but is not called directly where 'Value' is, so it can run after 'Value' is gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
			{
				co_await fg_Round93Suspend();
				co_return Value;
			}
		)
		% CRound93Context{}
	).f_CallSync();
}

// A pointer moved into a capture points where it did.
TCFuture<int> fg_Round96Suspend();

TCFuture<int> fg_Round96Moved()
{
	int Value = 1;
	int *pValue = &Value;
	co_return co_await fg_CallSafe
		(
			[pCopy = fg_Move(pValue)]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: a TCFuture coroutine lambda uses 'pCopy', which points to 'Value', after a suspension, when 'Value' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
			{
				co_await fg_Round96Suspend();
				co_return *pCopy;
			}
		)
	;
}

// A pointer to a local captured by reference, copied before a suspension.
TCFuture<int> fg_Round97Copied()
{
	int Value = 42;
	int *pValue = &Value;
	return fg_CallSafe
		(
			[&pValue]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: a TCFuture coroutine lambda uses 'pValue', which it captures by reference, after a suspension, when 'pValue' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
			{
				int *pSaved = pValue;
				co_await fg_Suspend();
				co_return *pSaved;
			}
		)
	;
}

// A pointer assigned a local in a branch.
TCFuture<int> fg_Round100Branch(bool _bChoose)
{
	int Local = 42;
	int *pValue = nullptr;
	if (_bChoose)
		pValue = &Local;
	return fg_CallSafe
		(
			[pValue]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: a TCFuture coroutine lambda uses 'pValue', which points to 'Local', after a suspension, when 'Local' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
			{
				co_await fg_Suspend();
				co_return pValue ? *pValue : 0;
			}
		)
	;
}

// A pointer made in a comma.
void fg_Round102Prepare();

TCFuture<int> fg_Round102Comma()
{
	int Value = 1;
	return fg_CallSafe
		(
			[pValue = (fg_Round102Prepare(), &Value)]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: a TCFuture coroutine lambda uses 'pValue', which points to 'Value', after a suspension, when 'Value' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
			{
				co_await fg_Suspend();
				co_return *pValue;
			}
		)
	;
}

// A copy of a pointer that was assigned, and a view assigned in a branch.
TCFuture<int> fg_Round104Copy()
{
	int Local = 42;
	int *pValue = nullptr;
	pValue = &Local;
	int *pCopy = pValue;
	return fg_CallSafe
		(
			[pCopy]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: a TCFuture coroutine lambda uses 'pCopy', which points to 'Local', after a suspension, when 'Local' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
			{
				co_await fg_Suspend();
				co_return *pCopy;
			}
		)
	;
}

struct CRound104StrSpan
{
	char const *m_pData;
	CRound104StrSpan(char const *_pData = nullptr)
		: m_pData(_pData)
	{
	}
	char f_Read() const
	{
		return *m_pData;
	}
};

TCFuture<int> fg_Round104View(bool _bChoose)
{
	char Local[] = "x";
	CRound104StrSpan View;
	if (_bChoose)
		View = CRound104StrSpan(Local);
	return fg_CallSafe
		(
			[View]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: a TCFuture coroutine lambda uses 'View', which points to 'Local', after a suspension, when 'Local' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
			{
				co_await fg_Suspend();
				co_return View.f_Read();
			}
		)
	;
}

// A pointer into a local through a pointer to it, captured by copy.
struct CRound109Value
{
	int m_Value = 1;
};

TCFuture<int> fg_Round109ThroughPointer()
{
	CRound109Value Value;
	auto *pValue = &Value;
	return fg_CallSafe
		(
			[pInner = &pValue->m_Value]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: a TCFuture coroutine lambda uses 'pInner', which points to 'Value', after a suspension, when 'Value' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
			{
				co_await fg_Suspend();
				co_return *pInner;
			}
		)
	;
}

TCFuture<int> fg_Round109ThroughDereference()
{
	CRound109Value Value;
	auto *pValue = &Value;
	return fg_CallSafe
		(
			[pInner = &*pValue]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: a TCFuture coroutine lambda uses 'pInner', which points to 'Value', after a suspension, when 'Value' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
			{
				co_await fg_Suspend();
				co_return pInner->m_Value;
			}
		)
	;
}

// What is assigned after a lambda copies a pointer changes the variable, not
// the copy.
int g_Round109Value = 1;

TCFuture<int> fg_Round109AssignedAfter(bool _bNull)
{
	int Local = 1;
	int *pValue = &g_Round109Value;
	if (_bNull)
		pValue = nullptr;
	auto Future = fg_CallSafe
		(
			[pValue]() -> TCFuture<int>
			{
				co_await fg_Suspend();
				co_return pValue ? *pValue : 0;
			}
		)
	;
	pValue = &Local;
	return Future;
}

// A pointer to a structured binding, to an element through a pointer, and
// one an accessor gives back.
struct CRound111Pair
{
	int m_Value;
};
int *fg_Round111Data(int (&_Values)[2])
{
	return _Values;
}
TCFuture<int> fg_Round111Binding()
{
	auto [Value] = CRound111Pair{1};
	return fg_CallSafe
		(
			[pValue = &Value]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: a TCFuture coroutine lambda uses 'pValue', which points to '[Value]', after a suspension, when '[Value]' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
			{
				co_await fg_Suspend();
				co_return *pValue;
			}
		)
	;
}
TCFuture<int> fg_Round111Element()
{
	int Values[2] = {};
	int *pBase = Values;
	return fg_CallSafe
		(
			[pValue = &pBase[1]]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: a TCFuture coroutine lambda uses 'pValue', which points to 'Values', after a suspension, when 'Values' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
			{
				co_await fg_Suspend();
				co_return *pValue;
			}
		)
	;
}
TCFuture<int> fg_Round111Accessor()
{
	int Values[2] = {};
	return fg_CallSafe
		(
			[pValue = fg_Round111Data(Values)]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: a TCFuture coroutine lambda uses 'pValue', which points to 'Values', after a suspension, when 'Values' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
			{
				co_await fg_Suspend();
				co_return *pValue;
			}
		)
	;
}

// A long chain of assigned pointers is followed in linear time.
namespace NRound115AssignedChain {
#define STEP(N, P) \
  int *p##N = nullptr; p##N = b ? p##P : p##P;
void f(bool b) {
  int value = 0;
  int *p0 = &value;
  STEP(1,0) STEP(2,1) STEP(3,2) STEP(4,3) STEP(5,4)
  STEP(6,5) STEP(7,6) STEP(8,7) STEP(9,8) STEP(10,9)
  STEP(11,10) STEP(12,11) STEP(13,12) STEP(14,13) STEP(15,14)
  STEP(16,15) STEP(17,16) STEP(18,17) STEP(19,18) STEP(20,19)
  auto result = fg_CallSafe(
    [p20]() -> TCFuture<int> { co_return *p20; });
}
}

// A captured reference to a pointer that points to a local borrows the local.
namespace NRound117ReferenceToPointer {
TCFuture<int> fg_Suspend();
void unsafe() {
  int value = 42;
  int *pointer = &value;
  int *&alias = pointer;
  (void)fg_CallSafe([&alias]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a TCFuture coroutine lambda uses 'alias', which it captures by reference, after a suspension, when 'alias' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
    int *saved = alias;
    co_await fg_Suspend();
    co_return *saved;
  });
}
}

// A reference to a pointer bound through a conditional is followed through both arms.
namespace NRound117ConditionalReference {
TCFuture<int> fg_Suspend();
void unsafe(bool choose) {
  int value = 42;
  int *p = &value;
  int *q = &value;
  int *&alias = choose ? p : q;
  (void)(g_Dispatch / [alias]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda captures 'alias', which points to 'value', but is not called directly where 'value' is, so it can run after 'value' is gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
    co_await fg_Suspend();
    co_return *alias;
  });
}
}

// Round 119: the address of a local that a lambda called in place returns, or
// the last of a comma.
namespace NRound119LambdaReturnedLocal {
TCFuture<int> fg_Suspend();
TCFuture<int> fg_Run() {
  int Local = 42;
  return fg_CallSafe([pLocal = [&] { return &Local; }()]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a TCFuture coroutine lambda uses 'pLocal', which points to 'Local', after a suspension, when 'Local' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
    co_await fg_Suspend();
    co_return *pLocal;
  });
}
TCFuture<int> fg_Comma() {
  int Local = 42;
  return fg_CallSafe([pLocal = (void(0), &Local)]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a TCFuture coroutine lambda uses 'pLocal', which points to 'Local', after a suspension, when 'Local' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
    co_await fg_Suspend();
    co_return *pLocal;
  });
}
}

// Round 127: the value a constant condition selects is what a pointer points
// to, and a reference to a member of a static is to what lives as long as the
// program.
namespace NRound127
{
TCFuture<int> fg_Suspend();
const int g_Global = 7;
struct CData {
  int m_Value;
};
CData const g_Data{42};

void fg_Constant() {
  int Local = 1;
  fg_CallSafe([pValue = false ? &Local : &g_Global]() -> TCFuture<int> {
    co_await fg_Suspend();
    co_return *pValue;
  });
}

void fg_StaticMember() {
  fg_CallSafe([&Value = g_Data.m_Value]() -> TCFuture<int> {
    co_await fg_Suspend();
    co_return Value;
  });
}

void fg_Selected() {
  int Local = 1;
  fg_CallSafe([pValue = true ? &Local : &g_Global]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: a TCFuture coroutine lambda uses 'pValue', which points to 'Local', after a suspension, when 'Local' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
    co_await fg_Suspend();
    co_return *pValue;
  });
}
}

// Round 129: the first value of `?:` is its condition.
namespace NRound129
{
TCFuture<int> fg_Work();
int g_Global;

void fg_Elvis() {
  int Value = 7;
  int *pValue = &Value;
  fg_CallSafe([pCopy = pValue ?: &g_Global]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: a TCFuture coroutine lambda uses 'pCopy', which points to 'Value', after a suspension, when 'Value' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
    co_await fg_Work();
    co_return *pCopy;
  });
}
}

// Round 134: what a helper returns moved along what it is given is into it.
namespace NRound134
{
TCFuture<int> fg_Pause();
int *fg_Offset(int *_pValue) { return _pValue + 0; }

void fg_Run() {
  int Value = 1;
  fg_CallSafe([pValue = fg_Offset(&Value)]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: a TCFuture coroutine lambda uses 'pValue', which points to 'Value', after a suspension, when 'Value' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
    co_await fg_Pause();
    co_return *pValue;
  });
}
}

namespace NRound137 {
int g_Global;
TCFuture<int> fg_Suspend();

// What a conditional reference's pointers point to when it is captured, not
// when it is bound.
TCFuture<int> fg_Later(bool _bChoose) {
  int Local = 42;
  int *pFirst = &g_Global, *pSecond = &g_Global;
  int *&pAlias = _bChoose ? pFirst : pSecond;
  pFirst = &Local;
  pSecond = &Local;
  return fg_CallSafe([pAlias]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a TCFuture coroutine lambda uses 'pAlias', which points to 'Local', after a suspension, when 'Local' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
    co_await fg_Suspend();
    co_return *pAlias;
  });
}

// But not what they point to after it.
TCFuture<int> fg_After(bool _bChoose) {
  int Local = 42;
  int *pFirst = &g_Global, *pSecond = &g_Global;
  int *&pAlias = _bChoose ? pFirst : pSecond;
  auto Future = fg_CallSafe([pAlias]() -> TCFuture<int> {
    co_await fg_Suspend();
    co_return *pAlias;
  });
  pFirst = &Local;
  pSecond = &Local;
  return Future;
}
}

namespace NRound138 {
TCFuture<int> fg_Work();

// Through any number of references to the pointer, `auto &pE = pD;`.
TCFuture<int> fg_Aliases() {
  int Value = 1;
  int *pValue = &Value;
  auto &pA = pValue;
  auto &pB = pA;
  auto &pC = pB;
  auto &pD = pC;
  auto &pE = pD;
  return fg_CallSafe([pE]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a TCFuture coroutine lambda uses 'pE', which points to 'Value', after a suspension, when 'Value' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
    co_await fg_Work();
    co_return *pE;
  });
}
}

namespace NRound147 {
TCFuture<int> fg_Work();

// A reference to a pointer bound through a cast to a reference.
void fg_Cast()
{
	int Value = 1;
	int *pValue = &Value;
	int *&pAlias = static_cast<int *&>(pValue);
	fg_CallSafe([pAlias]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: a TCFuture coroutine lambda uses 'pAlias', which points to 'Value', after a suspension, when 'Value' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
		co_await fg_Work();
		co_return *pAlias;
	});
}

// A reference to a global through a cast lives as long as the global.
int g_Global;

void fg_Global()
{
	g_Dispatch / [&Value = static_cast<int &>(g_Global)]() -> TCFuture<int>
		{
			co_return Value;
		}
	;
}
}

namespace NRound148 {
TCFuture<int> fg_Work();

struct CData
{
	int m_Value = 42;
	int *f_Get()
	{
		return &m_Value;
	}
};

// What a member function of a local gives that points into it.
TCFuture<int> fg_Getter()
{
	CData Local;
	return fg_CallSafe([pValue = Local.f_Get()]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: a TCFuture coroutine lambda uses 'pValue', which points to 'Local', after a suspension, when 'Local' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
		co_await fg_Work();
		co_return *pValue;
	});
}
}

namespace std
{
	template <typename t_CType>
	struct initializer_list
	{
		const t_CType *m_pData;
		__SIZE_TYPE__ m_Size;
		const t_CType *begin() const
		{
			return m_pData;
		}
	};
}

namespace NRound149 {
// The array of a `std::initializer_list` lives as long as the local it
// initializes.
TCFuture<int> fg_InitializerList()
{
	std::initializer_list<int> Values{1, 2, 3};
	return fg_CallSafe([Values]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: a TCFuture coroutine lambda uses 'Values', which points to 'Values', after a suspension, when 'Values' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
		co_await CAwaiter{};
		co_return *Values.begin();
	});
}
}

namespace NRound150 {
int g_Global;
struct CBase
{
	virtual int *f_Get()
	{
		return &g_Global;
	}
};
struct CDerived : CBase
{
	int m_Value = 0;
	int *f_Get() override
	{
		return &m_Value;
	}
};

// An override that the call can run returns what is in the local.
TCFuture<int> fg_Virtual()
{
	CDerived Object;
	CBase &Base = Object;
	return fg_CallSafe([pValue = Base.f_Get()]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: a TCFuture coroutine lambda uses 'pValue', which points to 'Object', after a suspension, when 'Object' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
		co_await CAwaiter{};
		co_return *pValue;
	});
}

// The array of an init capture's `std::initializer_list` lives with the
// closure it is made in, not a copy of it.
TCFuture<int> fg_InitCapture()
{
	return fg_CallSafe([Values = {1, 2}]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: a TCFuture coroutine lambda uses 'Values', which points to 'Values', after a suspension, when 'Values' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
		co_await CAwaiter{};
		co_return *Values.begin();
	});
}
}

namespace NRound151 {
int g_Global;

// Either of a conditional through a local reference to a pointer.
void fg_ConditionalAlias(bool _bChoose)
{
	int Local = 42;
	int *pSafe = &g_Global;
	int *pLocal = &Local;
	int *&pFirst = pLocal;
	int *&pAlias = _bChoose ? pFirst : pSafe;
	auto Future = fg_ConcurrentDispatch([pAlias]() -> TCFuture<int> { co_return *pAlias; });
// CHECK-MESSAGES: :[[@LINE-1]]:39: warning: a TCFuture coroutine lambda captures 'pAlias', which points to 'Local', but is not called directly where 'Local' is, so it can run after 'Local' is gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}

// Only the one a constant condition chooses.
void fg_ConstantCondition()
{
	int Local = 42;
	int *pSafe = &g_Global;
	int *pLocal = &Local;
	int *&pAlias = true ? pSafe : pLocal;
	auto Future = fg_ConcurrentDispatch([pAlias]() -> TCFuture<int> { co_return *pAlias; });
}
}

namespace NRound152 {
CAwaiter fg_Suspend();

struct CLocal
{
	int m_Value;
	int &f_Get()
	{
		return m_Value;
	}
};

// The address of what a member function of a local returns a reference to.
TCFuture<int> fg_ReferenceGetter()
{
	CLocal Data;
	return fg_CallSafe([pValue = &Data.f_Get()]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: a TCFuture coroutine lambda uses 'pValue', which points to 'Data', after a suspension, when 'Data' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
		co_await fg_Suspend();
		co_return *pValue;
	});
}
}

namespace NRound156 {
int g_Global;
TCFuture<int> fg_Pause();
// A helper that returns a parameter it assigned another one.
int *fg_Choose(int *_pFallback, int *_pEntry) {
  _pFallback = _pEntry;
  return _pFallback;
}
TCFuture<int> fg_Chosen() {
  int Local = 1;
  return fg_CallSafe([pValue = fg_Choose(&g_Global, &Local)]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a TCFuture coroutine lambda uses 'pValue', which points to 'Local', after a suspension, when 'Local' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
    co_await fg_Pause();
    co_return *pValue;
  });
}
// One that replaces what it is given with a global.
int *fg_Replace(int *_pValue) {
  _pValue = &g_Global;
  return _pValue;
}
TCFuture<int> fg_Replaced() {
  int Local = 1;
  return fg_CallSafe([pValue = fg_Replace(&Local)]() -> TCFuture<int> {
    co_await fg_Pause();
    co_return *pValue;
  });
}
}

namespace NRound158 {
// A reference init capture of a local of the factory.
TCFuture<int> fg_FactoryLocal() {
  auto fFactory = [] {
    int Value = 42;
    return [&Alias = Value]() -> TCFuture<int> { co_return Alias; };
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a TCFuture coroutine lambda captures 'Alias' by reference but is not called directly where 'Alias' is, so it can run after 'Alias' is gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
  };
  auto Guard = co_await fg_AsyncDestroy(fFactory());
  co_return 0;
}
// A local reference of the factory to what it is given by reference.
TCFuture<int> fg_FactoryAlias() {
  int Value = 42;
  auto fFactory = [](int &_Input) {
    int &Alias = _Input;
    return [&Alias]() -> TCFuture<int> { co_return Alias; };
  };
  auto Guard = co_await fg_AsyncDestroy(fFactory(Value));
  co_return 0;
}
}

namespace NRound162 {
// The array of an init capture, used before any suspension of a lambda
// called where it is made.
TCFuture<int> fg_Direct() {
  return [Values = {1, 2}]() -> TCFuture<int> { co_return *Values.begin(); }();
}
}

namespace NRound163 {
// Into a local through a pointer to a member.
struct CState {
  int m_Value = 1;
};
TCFuture<int> fg_Run() {
  CState State;
  auto pMember = &CState::m_Value;
  return fg_CallSafe([pValue = &(State.*pMember)]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a TCFuture coroutine lambda uses 'pValue', which points to 'State', after a suspension, when 'State' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
    co_await CAwaiter{};
    co_return *pValue;
  });
}
}

namespace NRound167 {
TCFuture<int> fg_Suspend();
// Through many members, `&Local.m_Child...m_Value`.
template <unsigned t_Depth> struct TCNest {
  TCNest<t_Depth - 1> m_Child;
};
template <> struct TCNest<0> {
  int m_Value;
};
TCFuture<int> fg_Nested() {
  TCNest<7> Local{};
  return fg_CallSafe(
      [pValue = &Local.m_Child.m_Child.m_Child.m_Child.m_Child.m_Child.m_Child
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: a TCFuture coroutine lambda uses 'pValue', which points to 'Local', after a suspension, when 'Local' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
                     .m_Value]() -> TCFuture<int> {
        co_await fg_Suspend();
        co_return *pValue;
      });
}
// Pointed at a local by a function it is given to by reference.
void fg_Set(int *&o_pValue, int &_Value) { o_pValue = &_Value; }
TCFuture<int> fg_SetThrough() {
  int Value = 42;
  int *pValue = nullptr;
  fg_Set(pValue, Value);
  return fg_CallSafe([pValue]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a TCFuture coroutine lambda uses 'pValue', which points to 'Value', after a suspension, when 'Value' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
    co_await fg_Suspend();
    co_return *pValue;
  });
}
}

namespace NRound170 {
TCFuture<int> fg_Suspend();
// A copy of a pointer stepped into a local.
auto fg_Stepped() {
  int Values[2];
  int *pValue = Values;
  int *pNext = pValue++;
  return fg_CallSafe([pNext]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a TCFuture coroutine lambda uses 'pNext', which points to 'Values', after a suspension, when 'Values' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
    co_await fg_Suspend();
    co_return *pNext;
  });
}
}

namespace NRound171 {
// Through many references to the pointer.
TCFuture<int> fg_Start() {
  int Value = 42;
  int *pValue = &Value;
  auto const &pAlias0 = pValue, &pAlias1 = pAlias0, &pAlias2 = pAlias1,
             &pAlias3 = pAlias2, &pAlias4 = pAlias3, &pAlias5 = pAlias4,
             &pAlias6 = pAlias5, &pAlias7 = pAlias6, &pAlias8 = pAlias7;
  return fg_CallSafe([&pAlias8]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'pAlias8', which it captures by reference, after a suspension, when 'pAlias8' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
    int *pSaved = pAlias8;
    co_await CAwaiter{};
    co_return *pSaved;
  });
}
}

namespace NRound173 {
// What a getter gives back moved along, or either of a conditional.
struct CBuffer {
  int m_Data[2]{1, 2};
  int *f_Get() { return m_Data + 1; }
  int *f_Pick(bool _bFirst) { return _bFirst ? &m_Data[0] : &m_Data[1]; }
};
TCFuture<int> fg_Offset() {
  CBuffer Local;
  return fg_CallSafe([pValue = Local.f_Get()]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a TCFuture coroutine lambda uses 'pValue', which points to 'Local', after a suspension, when 'Local' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
    co_await CAwaiter{};
    co_return *pValue;
  });
}
TCFuture<int> fg_Conditional(bool _bFirst) {
  CBuffer Local;
  return fg_CallSafe([pValue = Local.f_Pick(_bFirst)]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a TCFuture coroutine lambda uses 'pValue', which points to 'Local', after a suspension, when 'Local' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
    co_await CAwaiter{};
    co_return *pValue;
  });
}
}

namespace NRound176 {
// The array of a list a cleanup captures, which its guard gets a moved copy
// of, without the array.
struct CValue {
  int m_Value = 42;
  ~CValue();
};
TCFuture<int> fg_Suspend();
TCFuture<int> fg_Run() {
  auto Guard = co_await fg_AsyncDestroy(
      [Values = {CValue{}}]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: a TCFuture coroutine lambda captures the list 'Values', whose array is a temporary of the expression that makes the closure, so it can be gone when the closure runs; capture an array or a container by value [malterlib-coroutine-reference-capture]
        co_return Values.begin()->m_Value;
      });
  co_await fg_Suspend();
  co_return 0;
}
}

namespace NRound182 {
// Set through a helper that gives it to another that sets it.
void fg_Set(int *&o_pValue, int &_Value) { o_pValue = &_Value; }
void fg_Relay(int *&o_pValue, int &_Value) { fg_Set(o_pValue, _Value); }
TCFuture<int> fg_Start() {
  int Value = 42;
  int *pValue = nullptr;
  fg_Relay(pValue, Value);
  return fg_CallSafe([pValue]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a TCFuture coroutine lambda uses 'pValue', which points to 'Value', after a suspension, when 'Value' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
    co_await CAwaiter{};
    co_return *pValue;
  });
}
}

namespace NRound194 {
// A pointer to an element of a local container, through its `operator[]`,
// which returns an element of an array member.
template <typename t_CValue>
struct TCVector {
  t_CValue m_Values[1]{};
  t_CValue &operator[](int _Index) { return m_Values[_Index]; }
};
TCFuture<int> fg_Suspend();
TCFuture<int> fg_Read() {
  TCVector<int> Values;
  co_return co_await fg_CallSafe([pValue = &Values[0]]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:35: warning: a TCFuture coroutine lambda uses 'pValue', which points to 'Values', after a suspension, when 'Values' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
    co_await fg_Suspend();
    co_return *pValue;
  });
}
}

namespace NRound200 {
TCFuture<int> fg_Suspend();
// Pointed at an offset into a local by a function it is given to by
// reference.
void fg_Next(int *&o_pValue, int *_pBase) { o_pValue = _pBase + 1; }
TCFuture<int> fg_SetThrough() {
  int Values[2] = {1, 2};
  int *pValue = nullptr;
  fg_Next(pValue, Values);
  return fg_CallSafe([pValue]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a TCFuture coroutine lambda uses 'pValue', which points to 'Values', after a suspension, when 'Values' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
    co_await fg_Suspend();
    co_return *pValue;
  });
}
}

namespace NRound202 {
TCFuture<int> fg_Suspend();
// What a pointer member of a local that releases it points to, through a
// getter, is of the local, but not of one that doesn't, like a view.
template <class t_CType>
struct TCUniquePointer {
  t_CType *m_pValue;
  explicit TCUniquePointer(t_CType *_pValue) : m_pValue(_pValue) {}
  ~TCUniquePointer() { delete m_pValue; }
  t_CType *f_Get() const { return m_pValue; }
};
template <class t_CType>
struct TCVector {
  t_CType *m_pData;
  ~TCVector() { delete[] m_pData; }
  t_CType &operator[](int _Index) { return m_pData[_Index]; }
};
template <class t_CType>
struct TCView {
  t_CType *m_pValue;
  t_CType *f_Get() const { return m_pValue; }
};
int g_Value;
TCFuture<int> fg_Owned() {
  TCUniquePointer<int> pValue(new int(3));
  co_return co_await fg_CallSafe([pData = pValue.f_Get()]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:35: warning: a TCFuture coroutine lambda uses 'pData', which points to 'pValue', after a suspension, when 'pValue' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
    co_await fg_Suspend();
    co_return *pData;
  });
}
TCFuture<int> fg_Element() {
  TCVector<int> Values{new int[1]{3}};
  co_return co_await fg_CallSafe([pData = &Values[0]]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:35: warning: a TCFuture coroutine lambda uses 'pData', which points to 'Values', after a suspension, when 'Values' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
    co_await fg_Suspend();
    co_return *pData;
  });
}
TCFuture<int> fg_Viewed() {
  TCView<int> View{&g_Value};
  co_return co_await fg_CallSafe([pData = View.f_Get()]() -> TCFuture<int> {
    co_await fg_Suspend();
    co_return *pData;
  });
}
// The address of what a function gives back a reference to of what it is
// given is of that, a local or a global.
template <class t_CType>
t_CType const &fg_Const(t_CType const &_Value) {
  return _Value;
}
TCFuture<int> fg_AddressOfConst() {
  int Value = 1;
  co_return co_await fg_CallSafe(
      [pValue = &fg_Const(Value)]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: a TCFuture coroutine lambda uses 'pValue', which points to 'Value', after a suspension, when 'Value' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
        co_await fg_Suspend();
        co_return *pValue;
      });
}
TCFuture<int> fg_GlobalThroughConst() {
  co_return co_await fg_CallSafe(
      [&Value = fg_Const(g_Value)]() -> TCFuture<int> {
        co_await fg_Suspend();
        co_return Value;
      });
}
}

namespace NRound203 {
// A cleanup a generic factory makes, given a variable that outlives the
// guard.
TCFuture<int> fg_Generic() {
  int Value = 42;
  auto fMakeCleanup = [](auto const &_Value) {
    return [&_Value]() -> TCFuture<int> { co_return _Value; };
  };
  auto Cleanup = co_await fg_AsyncDestroy(fMakeCleanup(Value));
  co_return 0;
}
}

namespace NRound207 {
TCFuture<int> fg_Suspend();
// What a getter gives back of what another getter of the object gives.
struct CValue {
  int m_Value;
  int *fp_Get() { return &m_Value; }
  int *f_Get() { return fp_Get(); }
};
TCFuture<int> fg_Getter() {
  CValue Value;
  co_return co_await fg_CallSafe([pValue = Value.f_Get()]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:35: warning: a TCFuture coroutine lambda uses 'pValue', which points to 'Value', after a suspension, when 'Value' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
    co_await fg_Suspend();
    co_return *pValue;
  });
}
// A static member named through a local has static storage.
struct CDigest {
  static constexpr int mc_Size = 32;
};
TCFuture<int> fg_Static() {
  CDigest Digest;
  co_return co_await fg_CallSafe(
      [&Size = Digest.mc_Size, pSize = &Digest.mc_Size]() -> TCFuture<int> {
        co_await fg_Suspend();
        co_return Size + *pSize;
      });
}
}

namespace NRound211 {
TCFuture<int> fg_Work();
// What a getter gives of a member that is an object owning its storage, which
// is the local's.
template <typename t_CType>
struct TCVector
{
	TCVector();
	~TCVector();
	t_CType *m_pData;
	t_CType *f_GetArray()
	{
		return m_pData;
	}
};
struct CBuffer
{
	TCVector<int> m_Values;
	int *f_GetArray()
	{
		return m_Values.f_GetArray();
	}
};
TCFuture<int> fg_Run()
{
	CBuffer Buffer;
	return fg_CallSafe([pData = Buffer.f_GetArray()]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: a TCFuture coroutine lambda uses 'pData', which points to 'Buffer', after a suspension, when 'Buffer' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
		co_await fg_Work();
		co_return *pData;
	});
}
// Not of a member that refers to what the local doesn't own.
struct CBorrowedBuffer
{
	TCVector<int> &m_Values;
	int *f_GetArray()
	{
		return m_Values.f_GetArray();
	}
};
TCVector<int> g_Values;
TCFuture<int> fg_RunBorrowed()
{
	CBorrowedBuffer Buffer{g_Values};
	return fg_CallSafe([pData = Buffer.f_GetArray()]() -> TCFuture<int> {
		co_await fg_Work();
		co_return *pData;
	});
}
// What the owner member owns, also named directly, is kept by the record
// moved into the closure, whose member is that owner.
TCFuture<int> fg_RunKept()
{
	CBuffer Buffer;
	int *pData = Buffer.f_GetArray();
	return fg_CallSafe([pData, Buffer = fg_Move(Buffer)]() -> TCFuture<int> {
		co_await fg_Work();
		co_return *pData;
	});
}
TCFuture<int> fg_RunMember()
{
	CBuffer Buffer;
	return fg_CallSafe([pData = Buffer.m_Values.f_GetArray()]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: a TCFuture coroutine lambda uses 'pData', which points to 'Buffer', after a suspension, when 'Buffer' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
		co_await fg_Work();
		co_return *pData;
	});
}
TCFuture<int> fg_RunMemberKept()
{
	CBuffer Buffer;
	int *pData = Buffer.m_Values.f_GetArray();
	return fg_CallSafe([pData, Kept = fg_Move(Buffer)]() -> TCFuture<int> {
		co_await fg_Work();
		co_return *pData;
	});
}
}

namespace NRound213 {
TCFuture<int> fg_Suspend();
// What an owner gives of the pointer it keeps in a struct member, like
// `TCUniquePointer`, which deletes it with the local.
template <typename t_CType>
struct TCUniquePointer
{
	struct CData
	{
		t_CType *m_pPointTo;
	};
	CData m_Data;
	TCUniquePointer(t_CType *_pPointTo);
	~TCUniquePointer();
	t_CType *f_Get() const
	{
		return m_Data.m_pPointTo;
	}
};
TCFuture<int> fg_Start()
{
	TCUniquePointer<int> pValue(new int(3));
	return fg_CallSafe([pData = pValue.f_Get()]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: a TCFuture coroutine lambda uses 'pData', which points to 'pValue', after a suspension, when 'pValue' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
		co_await fg_Suspend();
		co_return *pData;
	});
}
}

namespace NRound215 {
TCFuture<int> fg_Work();
// Assigning a pointer the closure has changes only the closure.
void fg_Run()
{
	int Local = 1;
	fg_CallSafe([pValue = &Local]() mutable -> TCFuture<int> {
		co_await fg_Work();
		pValue = nullptr;
		co_return 0;
	});
}
}

namespace NRound220 {
TCFuture<int> fg_Suspend();
// A pointer to what a reference parameter refers to borrows like a capture of
// it by reference.
void fg_StartPointer(int &_Value) {
  (void)(g_Dispatch / [pValue = &_Value]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda captures 'pValue', which points to '_Value', but is not called directly where '_Value' is, so it can run after '_Value' is gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
    co_await fg_Suspend();
    co_return *pValue;
  });
}
void fg_StartReference(int &_Value) {
  (void)(g_Dispatch / [&_Value]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:25: warning: a TCFuture coroutine lambda captures '_Value' by reference but is not called directly where '_Value' is, so it can run after '_Value' is gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
    co_await fg_Suspend();
    co_return _Value;
  });
}
}

namespace NRound221 {
TCFuture<int> fg_Suspend();
// A lambda that is never made, in a branch that never runs or what isn't
// evaluated, captures nothing.
struct CHelper {
  int m_Value = 0;
  void f_Run() {
    int Value = 1;
    if constexpr (false) {
      fg_CallSafe([this, &Value]() -> TCFuture<int> {
        co_await fg_Suspend();
        co_return m_Value + Value;
      });
    }
    if (false) {
      fg_CallSafe([&Value]() -> TCFuture<int> {
        co_await fg_Suspend();
        co_return Value;
      });
    }
    (void)sizeof(fg_CallSafe([&Value]() -> TCFuture<int> {
      co_await fg_Suspend();
      co_return Value;
    }));
  }
  void f_Taken() {
    int Value = 1;
    if (true) {
      (void)(g_Dispatch / [&Value]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:29: warning: a TCFuture coroutine lambda captures 'Value' by reference but is not called directly where 'Value' is, so it can run after 'Value' is gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
        co_await fg_Suspend();
        co_return Value;
      });
    }
  }
};
}

namespace NRound223 {
TCFuture<int> fg_Work();
// Comparing a copied pointer reads the closure's copy.
TCFuture<int> fg_CheckPresence() {
  int Value = 1;
  co_return co_await fg_CallSafe([pValue = &Value]() -> TCFuture<int> {
    co_await fg_Work();
    co_return pValue != nullptr;
  });
}
// What a free helper gives of what a reference parameter refers to points
// into what it is given.
template <class t_CType>
struct TCVector {
  TCVector();
  ~TCVector();
  t_CType *m_pData;
  t_CType *f_GetArray() { return m_pData; }
};
template <class t_CType>
t_CType *begin(TCVector<t_CType> &_Values) {
  return _Values.f_GetArray();
}
TCFuture<int> fg_Begin() {
  TCVector<int> Values;
  co_await fg_CallSafe([p = begin(Values)]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:25: warning: a TCFuture coroutine lambda uses 'p', which points to 'Values', after a suspension, when 'Values' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
    co_await fg_Work();
    co_return *p;
  });
  co_return 0;
}
}

namespace NRound227
{
	// What a local lambda points a pointer to, also given a reference to it,
	// and not what a later assignment points it away from; and what moves
	// with its owner into the closure stays.
	TCFuture<int> fg_Work();
	int g_Value;

	void fg_Set(int *&o_pValue, int &_Value)
	{
		o_pValue = &_Value;
	}

	TCFuture<int> fg_LocalLambda()
	{
		int Value = 1;
		int *pValue = &g_Value;
		auto fSet = [](int *&o_pValue, int &_Value)
		{
			o_pValue = &_Value;
		};
		fSet(pValue, Value);
		return fg_CallSafe
			(
				[pValue]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: a TCFuture coroutine lambda uses 'pValue', which points to 'Value', after a suspension, when 'Value' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
				{
					co_await fg_Work();
					co_return *pValue;
				}
			)
		;
	}

	TCFuture<int> fg_Alias()
	{
		int Value = 1;
		int *pValue = &g_Value;
		int *&pAlias = pValue;
		fg_Set(pAlias, Value);
		return fg_CallSafe
			(
				[pValue]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: a TCFuture coroutine lambda uses 'pValue', which points to 'Value', after a suspension, when 'Value' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
				{
					co_await fg_Work();
					co_return *pValue;
				}
			)
		;
	}

	TCFuture<int> fg_Overwritten()
	{
		int Value = 1;
		int *pValue = &g_Value;
		fg_Set(pValue, Value);
		pValue = &g_Value;
		return fg_CallSafe
			(
				[pValue]() -> TCFuture<int>
				{
					co_await fg_Work();
					co_return *pValue;
				}
			)
		;
	}

	template <typename t_CType>
	struct TCUniquePointer
	{
		t_CType *m_pPointTo;
		explicit TCUniquePointer(t_CType *_pPointTo)
			: m_pPointTo(_pPointTo)
		{
		}
		TCUniquePointer(TCUniquePointer &&_Other)
			: m_pPointTo(_Other.m_pPointTo)
		{
			_Other.m_pPointTo = nullptr;
		}
		~TCUniquePointer()
		{
			delete m_pPointTo;
		}
		t_CType *f_Get() const
		{
			return m_pPointTo;
		}
	};

	TCFuture<int> fg_MovedOwner()
	{
		TCUniquePointer<int> Owner(new int(7));
		int *pValue = Owner.f_Get();
		co_return co_await fg_CallSafe
			(
				[pValue, Owner = fg_Move(Owner)]() -> TCFuture<int>
				{
					co_await fg_Work();
					co_return *pValue;
				}
			)
		;
	}

	TCFuture<int> fg_KeptOwner()
	{
		TCUniquePointer<int> Owner(new int(7));
		int *pValue = Owner.f_Get();
		co_return co_await fg_CallSafe
			(
				[pValue]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: a TCFuture coroutine lambda uses 'pValue', which points to 'Owner', after a suspension, when 'Owner' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
				{
					co_await fg_Work();
					co_return *pValue;
				}
			)
		;
	}
}

namespace NRound228
{
	// What a static member function returns that points into what it is given
	// by reference, as of a free function.
	template <typename t_CValue>
	struct TCVector
	{
		t_CValue m_Data[2];
		t_CValue *f_GetArray()
		{
			return m_Data;
		}
	};

	TCFuture<int> fg_Work();

	struct CHelpers
	{
		static int *fs_Data(TCVector<int> &_Values)
		{
			return _Values.f_GetArray();
		}
		static int *fs_Global(TCVector<int> &_Values);
	};

	int g_Value;
	int *CHelpers::fs_Global(TCVector<int> &_Values)
	{
		return &g_Value;
	}

	void fg_Run()
	{
		TCVector<int> Values;
		auto Future = fg_CallSafe
			(
				[pData = CHelpers::fs_Data(Values)]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: a TCFuture coroutine lambda uses 'pData', which points to 'Values', after a suspension, when 'Values' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
				{
					co_await fg_Work();
					co_return pData[0];
				}
			)
		;
		auto Global = fg_CallSafe
			(
				[pData = CHelpers::fs_Global(Values)]() -> TCFuture<int>
				{
					co_await fg_Work();
					co_return pData[0];
				}
			)
		;
	}
}

namespace NRound232
{
	// What a getter gives of the object through a local reference to a
	// member is of the object; a pointer member an object's destructor
	// doesn't release isn't of what it owns.
	TCFuture<int> fg_Work();
	int g_Value = 1;

	struct CState
	{
		int m_Value;
		int *f_Get()
		{
			auto &Value = m_Value;
			return &Value;
		}
	};

	struct COwning
	{
		TCSharedPointer<int> m_pOwned;
		int *m_pExternal = &g_Value;
		int *f_Get() const
		{
			return m_pExternal;
		}
	};

	void fg_Test()
	{
		CState State{};
		fg_CallSafe
			(
				[pValue = State.f_Get()]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: a TCFuture coroutine lambda uses 'pValue', which points to 'State', after a suspension, when 'State' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
				{
					co_await fg_Work();
					co_return *pValue;
				}
			)
		;
		COwning Owning;
		fg_CallSafe
			(
				[pValue = Owning.f_Get()]() -> TCFuture<int>
				{
					co_await fg_Work();
					co_return *pValue;
				}
			)
		;
	}
}

namespace NRound233
{
	// Also moved by a cast to an rvalue reference.
	TCFuture<int> fg_Work();

	template <typename t_CType>
	struct TCUniquePointer
	{
		t_CType *m_pPointTo;
		explicit TCUniquePointer(t_CType *_pPointTo)
			: m_pPointTo(_pPointTo)
		{
		}
		TCUniquePointer(TCUniquePointer &&_Other)
			: m_pPointTo(_Other.m_pPointTo)
		{
			_Other.m_pPointTo = nullptr;
		}
		~TCUniquePointer()
		{
			delete m_pPointTo;
		}
		t_CType *f_Get() const
		{
			return m_pPointTo;
		}
	};

	TCFuture<int> fg_MovedByCast()
	{
		TCUniquePointer<int> Owner(new int(7));
		int *pValue = Owner.f_Get();
		co_return co_await fg_CallSafe
			(
				[pValue, Owner = static_cast<TCUniquePointer<int> &&>(Owner)]() -> TCFuture<int>
				{
					co_await fg_Work();
					co_return *pValue;
				}
			)
		;
	}
}

namespace NRound235 {
TCFuture<int> fg_Suspend();
// A pointer of the closure pointed into its frame before it suspends.
TCFuture<int> fg_Redirected() {
  int Value = 1;
  return fg_CallSafe([pValue = &Value]() mutable -> TCFuture<int> {
    int Owned = *pValue;
    pValue = &Owned;
    co_await fg_Suspend();
    co_return *pValue;
  });
}
// Also one captured by copy.
TCFuture<int> fg_Copied() {
  int Value = 1;
  int *pValue = &Value;
  return fg_CallSafe([pValue]() mutable -> TCFuture<int> {
    int Owned = *pValue;
    pValue = &Owned;
    co_await fg_Suspend();
    co_return *pValue;
  });
}
// Not one pointed elsewhere only after it suspends.
TCFuture<int> fg_Late() {
  int Value = 1;
  return fg_CallSafe([pValue = &Value]() mutable -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a TCFuture coroutine lambda uses 'pValue', which points to 'Value', after a suspension, when 'Value' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
    int Owned = 0;
    co_await fg_Suspend();
    Owned = *pValue;
    pValue = &Owned;
    co_return *pValue;
  });
}
}

namespace NRound240 {
int g_Value;
TCFuture<int> fg_Work();
// What a lambda called in place points at of what it is given by reference.
TCFuture<int> fg_Run() {
  co_await fg_CallSafe([pValue = [](int &_Value) { return &_Value; }(g_Value)]()
                           -> TCFuture<int> {
    co_await fg_Work();
    co_return *pValue;
  });
  co_return 0;
}
// But of a local it is given, which is gone.
TCFuture<int> fg_Local() {
  int Value = 0;
  return fg_CallSafe([pValue = [](int &_Value) { return &_Value; }(Value)]()
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a TCFuture coroutine lambda uses 'pValue', which points to 'Value', after a suspension, when 'Value' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
                         -> TCFuture<int> {
    co_await fg_Work();
    co_return *pValue;
  });
}
}

namespace NRound242 {
TCFuture<int> fg_Work();
// An owner that moves its pointer in the body of its move constructor, like
// TCUniquePointer.
template <typename t_CType>
struct TCUniquePointer {
  struct CData {
    t_CType *m_pPointTo;
  };
  CData m_Data;
  explicit TCUniquePointer(t_CType *_pPointTo) { m_Data.m_pPointTo = _pPointTo; }
  TCUniquePointer(TCUniquePointer &&_Other) {
    m_Data.m_pPointTo = _Other.m_Data.m_pPointTo;
    _Other.m_Data.m_pPointTo = nullptr;
  }
  ~TCUniquePointer() { delete m_Data.m_pPointTo; }
  t_CType *f_Get() const { return m_Data.m_pPointTo; }
};
TCFuture<int> fg_Moved() {
  TCUniquePointer<int> Owner(new int(7));
  int *pValue = Owner.f_Get();
  co_return co_await fg_CallSafe([pValue, Owner = fg_Move(Owner)]() -> TCFuture<int> {
    co_await fg_Work();
    co_return *pValue;
  });
}
// But not for a result that keeps it, which fg_CallSafe gives as its own.
template <typename tf_FFunction>
auto fg_CallSafe(tf_FFunction &&_fFunction) -> decltype(_fFunction()) {
  return {};
}
TCFuture<int *> fg_Returned() {
  TCUniquePointer<int> Owner(new int(7));
  int *pValue = Owner.f_Get();
  co_return co_await fg_CallSafe([pValue, Owner = fg_Move(Owner)]() -> TCFuture<int *> {
// CHECK-MESSAGES: :[[@LINE-1]]:35: warning: a TCFuture coroutine lambda uses 'pValue', which points to 'Owner', after a suspension, when 'Owner' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
    co_await fg_Work();
    co_return pValue;
  });
}
template <typename t_CType>
struct TCFlatPointer {
  t_CType *m_pPointTo;
  explicit TCFlatPointer(t_CType *_pPointTo) : m_pPointTo(_pPointTo) {}
  TCFlatPointer(TCFlatPointer &&_Other) : m_pPointTo(_Other.m_pPointTo) {
    _Other.m_pPointTo = nullptr;
  }
  ~TCFlatPointer() { delete m_pPointTo; }
  t_CType *f_Get() const { return m_pPointTo; }
};
TCFuture<int *> fg_ReturnedFlat() {
  TCFlatPointer<int> Owner(new int(7));
  int *pValue = Owner.f_Get();
  co_return co_await fg_CallSafe([pValue, Owner = fg_Move(Owner)]() -> TCFuture<int *> {
// CHECK-MESSAGES: :[[@LINE-1]]:35: warning: a TCFuture coroutine lambda uses 'pValue', which points to 'Owner', after a suspension, when 'Owner' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
    co_await fg_Work();
    co_return pValue;
  });
}
template <typename t_CKey, typename t_CValue>
struct TCMap {
  t_CValue &operator[](t_CKey const &_Key);
};
// An entry of a local container, read before anything suspends.
TCFuture<int> fg_Entry() {
  TCMap<int, int> Items;
  return fg_CallSafe([&Value = Items[1]]() -> TCFuture<int> { co_return Value; });
}
// A member of the object of the actor lives with it.
struct CApp : CActor {
  int m_Value = 0;
  TCFuture<int> f_Read() {
    co_return co_await fg_CallSafe([&Value = m_Value]() -> TCFuture<int> {
      co_await fg_Work();
      co_return Value;
    });
  }
  TCFuture<int> f_Alias() {
    auto &Value = m_Value;
    co_return co_await fg_CallSafe([&Value]() -> TCFuture<int> {
      co_await fg_Work();
      co_return Value;
    });
  }
};
}

// A pointer into what an owner moved into the closure owns, whatever it is
// made with, also through a local copy: the closure keeps it while it runs.
// Not a pointer into the owner itself, which stays where it is when it is
// moved from, nor one whose owner the closure doesn't take.
namespace NReview2Round145
{
	TCFuture<int> fg_Work();
	template <typename t_CType>
	struct TCUniquePointer
	{
		t_CType *m_pPointTo;
		explicit TCUniquePointer(t_CType *_pPointTo)
			: m_pPointTo(_pPointTo)
		{
		}
		TCUniquePointer(TCUniquePointer &&_Other)
			: m_pPointTo(_Other.m_pPointTo)
		{
			_Other.m_pPointTo = nullptr;
		}
		~TCUniquePointer()
		{
			delete m_pPointTo;
		}
		t_CType *f_Get() const
		{
			return m_pPointTo;
		}
		t_CType *operator->() const
		{
			return m_pPointTo;
		}
	};
	struct CJobData
	{
		int m_Result = 42;
	};
	struct CJob
	{
		CJobData m_Data;
	};

	TCFuture<int> fg_ProcessJob()
	{
		TCUniquePointer<CJobData> pJob(new CJobData);
		auto *pResult = &pJob.f_Get()->m_Result;
		return fg_CallSafe
			(
				[pResult, pJob = fg_Move(pJob)]() -> TCFuture<int>
				{
					co_await fg_Work();
					co_return *pResult;
				}
			)
		;
	}
	TCFuture<int> fg_ProcessJobCopy()
	{
		TCUniquePointer<CJobData> pJob(new CJobData);
		int *pResult = &pJob->m_Result;
		int *pCopy = pResult;
		return fg_CallSafe
			(
				[pCopy, pJob = fg_Move(pJob)]() -> TCFuture<int>
				{
					co_await fg_Work();
					co_return *pCopy;
				}
			)
		;
	}
	TCFuture<int> fg_InlineControl()
	{
		CJob Job;
		int *pResult = &Job.m_Data.m_Result;
		return fg_CallSafe
			(
				[pResult, Job = fg_Move(Job)]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: a TCFuture coroutine lambda uses 'pResult', which points to 'Job', after a suspension, when 'Job' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
				{
					co_await fg_Work();
					co_return *pResult;
				}
			)
		;
	}
	TCFuture<int> fg_NotMovedControl()
	{
		TCUniquePointer<CJobData> pJob(new CJobData);
		int *pResult = &pJob->m_Result;
		return fg_CallSafe
			(
				[pResult]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: a TCFuture coroutine lambda uses 'pResult', which points to 'pJob', after a suspension, when 'pJob' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
				{
					co_await fg_Work();
					co_return *pResult;
				}
			)
		;
	}
}

namespace NBufferOwning
{
	// A pointer into the buffer of an owner that keeps it when it is moved
	// (OwnerTypes) stays valid in the closure the owner is moved to,
	// whatever its move does; not of one with an allocator that can keep the
	// data in it, nor of one that isn't listed.
	TCFuture<int> fg_Work();
	template <typename t_CData, typename t_CNew>
	t_CData fg_Exchange(t_CData &_Value, t_CNew &&_New);
	struct CAllocator_Heap
	{
	};
	struct CAllocator_Inline
	{
		char m_Storage[64];
	};
	template <typename t_CData, typename t_CAllocator = CAllocator_Heap>
	struct TCVector
	{
		TCVector();
		TCVector(TCVector &&_Other);
		~TCVector();
		t_CData *f_GetArray() { return mp_pData; }
		t_CData *mp_pData;
		t_CAllocator mp_Allocator;
	};
	struct CStrTraits_CStr
	{
	};
	template <typename t_CTraits>
	struct TCStrImpl
	{
		TCStrImpl(TCStrImpl &&_Other) : mp_pStr(fg_Exchange(_Other.mp_pStr, nullptr)) {}
		TCStrImpl();
		~TCStrImpl();
		char const *mp_pStr;
	};
	template <typename t_CTraits>
	struct TCStr : TCStrImpl<t_CTraits>
	{
		TCStr();
		TCStr(TCStr &&_Other) : TCStrImpl<t_CTraits>(fg_Move(_Other)) {}
		char const *f_GetStr() const { return this->mp_pStr; }
	};
	using CStr = TCStr<CStrTraits_CStr>;
	template <typename t_CData>
	struct TCList
	{
		TCList();
		TCList(TCList &&_Other);
		~TCList();
		t_CData *f_GetFirst() { return mp_pFirst; }
		t_CData *mp_pFirst;
	};
	CStr fg_Text();

	TCFuture<int> fg_String()
	{
		CStr Text = fg_Text();
		auto const *pText = Text.f_GetStr();
		return fg_CallSafe([pText, Text = fg_Move(Text)]() -> TCFuture<int> {
			co_await fg_Work();
			co_return *pText;
		});
	}
	TCFuture<int> fg_Vector()
	{
		TCVector<int> Values;
		auto *pValue = Values.f_GetArray();
		return fg_CallSafe([pValue, Values = fg_Move(Values)]() -> TCFuture<int> {
			co_await fg_Work();
			co_return *pValue;
		});
	}
	// Whether the data can be in the object is read from its layout, not
	// from its template arguments: an allocator with no array member, also
	// one that isn't the default, keeps the buffer on the heap; one with an
	// array in a member held by value, through a union, keeps it inside.
	struct CAllocator_Growing : CAllocator_Heap
	{
		int m_MinSize = 16;
	};
	struct CStaticStorage
	{
		union
		{
			char m_Bytes[32];
			long m_Align;
		};
	};
	struct CAllocator_Static : CAllocator_Heap
	{
		CStaticStorage m_Storage;
	};
	TCFuture<int> fg_GrowingVector()
	{
		TCVector<int, CAllocator_Growing> Values;
		auto *pValue = Values.f_GetArray();
		return fg_CallSafe([pValue, Values = fg_Move(Values)]() -> TCFuture<int> {
			co_await fg_Work();
			co_return *pValue;
		});
	}
	TCFuture<int> fg_StaticVector()
	{
		TCVector<int, CAllocator_Static> Values;
		auto *pValue = Values.f_GetArray();
		return fg_CallSafe([pValue, Values = fg_Move(Values)]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a TCFuture coroutine lambda uses 'pValue', which points to 'Values', after a suspension, when 'Values' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
			co_await fg_Work();
			co_return *pValue;
		});
	}
	TCFuture<int> fg_InlineVector()
	{
		TCVector<int, CAllocator_Inline> Values;
		auto *pValue = Values.f_GetArray();
		return fg_CallSafe([pValue, Values = fg_Move(Values)]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a TCFuture coroutine lambda uses 'pValue', which points to 'Values', after a suspension, when 'Values' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
			co_await fg_Work();
			co_return *pValue;
		});
	}
	TCFuture<int> fg_List()
	{
		TCList<int> Values;
		auto *pValue = Values.f_GetFirst();
		return fg_CallSafe([pValue, Values = fg_Move(Values)]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a TCFuture coroutine lambda uses 'pValue', which points to 'Values', after a suspension, when 'Values' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
			co_await fg_Work();
			co_return *pValue;
		});
	}
}

namespace NRound152ConvertingMove {
// A converting move of the same owner template, `TCUniquePointer<CBase>(
// fg_Move(pJob))`, keeps what the owner owns, as its move does.
TCFuture<int> fg_Work();
template <typename t_CType>
struct TCUniquePointer {
  t_CType *m_pPointTo;
  explicit TCUniquePointer(t_CType *_pPointTo) : m_pPointTo(_pPointTo) {}
  TCUniquePointer(TCUniquePointer &&_Other) : m_pPointTo(_Other.m_pPointTo) { _Other.m_pPointTo = nullptr; }
  template <typename t_COther>
  TCUniquePointer(TCUniquePointer<t_COther> &&_Other) : m_pPointTo(_Other.f_Detach()) {}
  ~TCUniquePointer() { delete m_pPointTo; }
  t_CType *f_Detach() { auto *pPointTo = m_pPointTo; m_pPointTo = nullptr; return pPointTo; }
  t_CType *operator->() const { return m_pPointTo; }
};
struct CBase { virtual ~CBase() = default; };
struct CJob : CBase { int m_Result = 0; };
TCFuture<int> fg_Converted() {
  TCUniquePointer<CJob> pJob(new CJob);
  auto *pResult = &pJob->m_Result;
  return fg_CallSafe([pResult, pKeep = TCUniquePointer<CBase>(fg_Move(pJob))]() -> TCFuture<int> {
    co_await fg_Work();
    co_return *pResult;
  });
}
TCFuture<int> fg_NotMoved() {
  TCUniquePointer<CJob> pJob(new CJob);
  auto *pResult = &pJob->m_Result;
  return fg_CallSafe([pResult]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a TCFuture coroutine lambda uses 'pResult', which points to 'pJob', after a suspension, when 'pJob' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
    co_await fg_Work();
    co_return *pResult;
  });
}
}


// What a reference or a pointer derived from what an owner owns refers to,
// `*pJob`, `pJob->m_Result` or `&*pJob`, lives while the closure that the
// owner is moved into runs; not when the closure doesn't take the owner, nor
// when the owner releases it before it is moved. A release after the move
// releases only the source.
namespace NReview2Round155
{
	TCFuture<int> fg_Work();
	template <typename t_CType>
	struct TCUniquePointer
	{
		t_CType *m_pPointTo;
		explicit TCUniquePointer(t_CType *_pPointTo) : m_pPointTo(_pPointTo) {}
		TCUniquePointer(TCUniquePointer &&_Other) : m_pPointTo(_Other.m_pPointTo) { _Other.m_pPointTo = nullptr; }
		~TCUniquePointer() { delete m_pPointTo; }
		t_CType *operator->() const { return m_pPointTo; }
		t_CType &operator*() const { return *m_pPointTo; }
		void f_Clear() { delete m_pPointTo; m_pPointTo = nullptr; }
	};
	struct CJob
	{
		int m_Result = 0;
	};
	TCFuture<int> fg_Reference()
	{
		TCUniquePointer<CJob> pJob(new CJob);
		CJob &Job = *pJob;
		return fg_CallSafe([&Job, pJob = fg_Move(pJob)]() -> TCFuture<int> { co_await fg_Work(); co_return Job.m_Result; });
	}
	TCFuture<int> fg_Member()
	{
		TCUniquePointer<CJob> pJob(new CJob);
		int &Result = pJob->m_Result;
		return fg_CallSafe([&Result, pJob = fg_Move(pJob)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
	}
	TCFuture<int> fg_Address()
	{
		TCUniquePointer<CJob> pJob(new CJob);
		CJob *pRaw = &*pJob;
		return fg_CallSafe([pRaw, pJob = fg_Move(pJob)]() -> TCFuture<int> { co_await fg_Work(); co_return pRaw->m_Result; });
	}
	TCFuture<int> fg_ClearedAfter()
	{
		TCUniquePointer<CJob> pJob(new CJob);
		CJob &Job = *pJob;
		auto Result = fg_CallSafe([&Job, pKeep = fg_Move(pJob)]() -> TCFuture<int> { co_await fg_Work(); co_return Job.m_Result; });
		pJob.f_Clear();
		return Result;
	}
	TCFuture<int> fg_ClearedBefore()
	{
		TCUniquePointer<CJob> pJob(new CJob);
		CJob *pRaw = &*pJob;
		pJob.f_Clear();
		return fg_CallSafe([pRaw, pJob = fg_Move(pJob)]() -> TCFuture<int> { co_await fg_Work(); co_return pRaw->m_Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a TCFuture coroutine lambda uses 'pRaw', which points to 'pJob', after a suspension, when 'pJob' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
	}
	TCFuture<int> fg_NotTaken()
	{
		TCUniquePointer<CJob> pJob(new CJob);
		CJob &Job = *pJob;
		return fg_CallSafe([&Job]() -> TCFuture<int> { co_await fg_Work(); co_return Job.m_Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Job', which it captures by reference, after a suspension, when 'Job' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
	}
	TCFuture<int> fg_TakenByReference()
	{
		TCUniquePointer<CJob> pJob(new CJob);
		CJob &Job = *pJob;
		return fg_CallSafe([&Job, &pJob]() -> TCFuture<int> { co_await fg_Work(); co_return Job.m_Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Job', which it captures by reference, after a suspension, when 'Job' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
	}

	// What a getter gives of a constant condition, `t_bDefault ? "Default" :
	// m_Text`, is only the arm it chooses (keptArms).
	template <bool t_bDefault>
	struct TCBuffer
	{
		char m_Text[8];
		char const *f_GetStr() const { return t_bDefault ? "Default" : m_Text; }
	};
	TCFuture<int> fg_DefaultText()
	{
		TCBuffer<true> Buffer;
		return fg_CallSafe([pText = Buffer.f_GetStr()]() -> TCFuture<int> { co_await fg_Work(); co_return *pText; });
	}
	TCFuture<int> fg_OwnText()
	{
		TCBuffer<false> Buffer;
		return fg_CallSafe([pText = Buffer.f_GetStr()]() -> TCFuture<int> { co_await fg_Work(); co_return *pText; });
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a TCFuture coroutine lambda uses 'pText', which points to 'Buffer', after a suspension, when 'Buffer' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
	}

	// A call on a local value, not a reference, runs what its class has: an
	// override of a class derived from it can't.
	int g_Value;
	struct CSource
	{
		virtual ~CSource() = default;
		virtual int *f_Get() { return &g_Value; }
	};
	struct CLocalSource : CSource
	{
		int m_Value = 0;
		int *f_Get() override { return &m_Value; }
	};
	TCFuture<int> fg_ExactValue()
	{
		CSource Source;
		return fg_CallSafe([pValue = Source.f_Get()]() -> TCFuture<int> { co_await fg_Work(); co_return *pValue; });
	}
	TCFuture<int> fg_DerivedValue()
	{
		CLocalSource Source;
		return fg_CallSafe([pValue = Source.f_Get()]() -> TCFuture<int> { co_await fg_Work(); co_return *pValue; });
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a TCFuture coroutine lambda uses 'pValue', which points to 'Source', after a suspension, when 'Source' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
	}
	TCFuture<int> fg_ThroughReference(CSource &_Source)
	{
		CSource &Source = _Source;
		return fg_CallSafe([pValue = Source.f_Get()]() -> TCFuture<int> { co_await fg_Work(); co_return *pValue; });
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a TCFuture coroutine lambda uses 'pValue', which points to '_Source', after a suspension, when '_Source' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
	}

	// What has static storage lives while the closure runs, also through an
	// unchanged pointer to it, `pConfig->m_Timeout` of `auto const *pConfig =
	// &g_Config`; not through one to a local.
	struct CConfig
	{
		int m_Timeout;
	};
	CConfig const g_Config{10};
	TCFuture<int> fg_StaticConfig()
	{
		auto const *pConfig = &g_Config;
		co_return co_await fg_CallSafe([&Timeout = pConfig->m_Timeout]() -> TCFuture<int> { co_await fg_Work(); co_return Timeout; });
	}
	TCFuture<int> fg_LocalConfig()
	{
		CConfig Config{1};
		auto const *pConfig = &Config;
		return fg_CallSafe([&Timeout = pConfig->m_Timeout]() -> TCFuture<int> { co_await fg_Work(); co_return Timeout; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Timeout', which it captures by reference, after a suspension, when 'Timeout' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
	}

	// An element, or what a getter gives of a member held by value, of the
	// object of an actor lives with the actor, which runs its callbacks.
	struct CApp : CActor
	{
		int m_Values[4];
		int m_Value;
		int &f_Value() { return m_Value; }
		void f_Run()
		{
			self / [&Value = m_Values[0]]() -> TCFuture<int> { co_await fg_Work(); co_return Value; };
			self / [&Value = f_Value()]() -> TCFuture<int> { co_await fg_Work(); co_return Value; };
		}
	};
}

// Rule K for captures: a keeper in the closure or held by value by a local of
// its body in scope at the use, one declared after it reported; the owners dereferenced on
// the way also before a pointer they don't own, and the referent of what a
// getter whose body can't be seen gives; the object of the actor a free
// function is given at each call, as `this`, in a lambda it makes.
namespace NReview2Round177Capture {
using namespace NMib::NStorage;
TCFuture<int> fg_Work();
struct CJob { int m_Value = 0; };
struct CContext { CJob *m_pRawJob = nullptr; CJob *f_GetJob() const; };
TCSharedPointer<CJob> fg_FindJob();
TCFuture<int> fg_LocalKeeper(CJob &_Job) {
  return fg_CallSafe([&Value = _Job.m_Value]() -> TCFuture<int> { auto pKeep = fg_FindJob(); co_await fg_Work(); co_return Value; });
}
TCFuture<int> fg_LocalKeeperAfter(CJob &_Job) {
  return fg_CallSafe([&Value = _Job.m_Value]() -> TCFuture<int> { co_await fg_Work(); int Read = Value; auto pKeep = fg_FindJob(); co_return Read; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Value', which it captures by reference, after a suspension, when 'Value' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
TCFuture<int> fg_RawHop(TCSharedPointer<CContext> pContext) {
  return fg_CallSafe([pContext, &Value = pContext->m_pRawJob->m_Value]() -> TCFuture<int> { co_await fg_Work(); co_return Value; });
}
TCFuture<int> fg_RawHopWeak(TCSharedPointer<CContext> pContext, TCWeakPointer<CContext> pWeak) {
  return fg_CallSafe([pWeak, &Value = pContext->m_pRawJob->m_Value]() -> TCFuture<int> { co_await fg_Work(); co_return Value; });
// CHECK-MESSAGES: :[[@LINE-1]]:31: warning: a TCFuture coroutine lambda uses 'Value', which it captures by reference, after a suspension, when 'Value' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
TCFuture<int> fg_UnseenGetter(TCSharedPointer<CContext> pContext, TCSharedPointer<CJob> pJob) {
  return fg_CallSafe([pJob, &Value = pContext->f_GetJob()->m_Value]() -> TCFuture<int> { co_await fg_Work(); co_return Value; });
}
TCFuture<int> fg_UnseenGetterUnkept(TCSharedPointer<CContext> pContext) {
  return fg_CallSafe([&Value = pContext->f_GetJob()->m_Value]() -> TCFuture<int> { co_await fg_Work(); co_return Value; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Value', which it captures by reference, after a suspension, when 'Value' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
struct CApp : public NMib::NConcurrency::CActor { int m_Value = 0; TCFuture<int> f_Read(); };
TCFuture<int> fg_ReadApp(CApp *_pThis) {
  return fg_CallSafe([&Value = _pThis->m_Value]() -> TCFuture<int> { co_await fg_Work(); co_return Value; });
}
TCFuture<int> CApp::f_Read() { return fg_ReadApp(this); }
} // namespace NReview2Round177Capture

// Round 183: a structured binding of a record held by value names a member
// of the variable it decomposes, and the value of a pointer member stored in
// it is a value, also as a member read: clean. The record's own storage,
// through a binding by value or by reference, is the frame's: reported.
namespace NReview2Round183Capture {
TCFuture<int> fg_Work();
struct CReadContext { int *m_pValue; };
struct CCount { int m_Count; };
TCFuture<int> fg_Binding() {
  int Value = 1;
  CReadContext Context{&Value};
  auto [pValue] = Context;
  return fg_CallSafe([pValue]() -> TCFuture<int> { co_await fg_Work(); co_return *pValue; });
// CHECK-MESSAGES: :[[@LINE-1]]:{{[0-9]+}}: warning: a TCFuture coroutine lambda uses 'pValue', which points to 'Value', after a suspension, when 'Value' may be gone{{.*}} [malterlib-coroutine-reference-capture]
}
TCFuture<int> fg_BindingByReference() {
  int Value = 1;
  CReadContext Context{&Value};
  auto &[pValue] = Context;
  return fg_CallSafe([pValue]() -> TCFuture<int> { co_await fg_Work(); co_return *pValue; });
// CHECK-MESSAGES: :[[@LINE-1]]:{{[0-9]+}}: warning: a TCFuture coroutine lambda uses 'pValue', which points to 'Value', after a suspension, when 'Value' may be gone{{.*}} [malterlib-coroutine-reference-capture]
}
TCFuture<int> fg_Member() {
  int Value = 1;
  CReadContext Context{&Value};
  return fg_CallSafe([&Read = *Context.m_pValue]() -> TCFuture<int> { co_await fg_Work(); co_return Read; });
// CHECK-MESSAGES: :[[@LINE-1]]:{{[0-9]+}}: warning: a TCFuture coroutine lambda uses 'Read', which it captures by reference, after a suspension, when 'Read' may be gone{{.*}} [malterlib-coroutine-reference-capture]
}
TCFuture<int> fg_OwnStorage() {
  CCount Context{1};
  auto &[Count] = Context;
  return fg_CallSafe([&Count]() -> TCFuture<int> { co_await fg_Work(); co_return Count; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Count', which it captures by reference, after a suspension, when 'Count' may be gone; {{.*}}
}
TCFuture<int> fg_OwnStorageCopy() {
  CCount Context{1};
  auto [Count] = Context;
  return fg_CallSafe([&Count]() -> TCFuture<int> { co_await fg_Work(); co_return Count; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Count', which it captures by reference, after a suspension, when 'Count' may be gone; {{.*}}
}
} // namespace NReview2Round183Capture

// Round 184: a reference member of a local record is what it is bound to,
// also as a getter returns it or a structured binding names it: reported; of
// a parameter held by value, what the caller bound it to can't be told
// (DMibSetByOwnClass), reported. An init capture of a record carries what it is made with, like a
// capture of a named one: reported; what reads only a captured view's own storage,
// `f_GetLen()`, isn't a use of what it refers to. The record's own storage,
// also through a pointer to it, stays reported.
namespace NReview2Round184Capture {
TCFuture<int> fg_Work();
struct CJob { int m_Value; };
struct CReadContext { int *m_pValue; };
struct CJobContext { CJob &m_Job; CJob &f_Job() { return m_Job; } };
struct CRequestContext { int const &m_RequestID; };
struct CStrSpan { CStrSpan(char *_pData, int _Len) : m_pData(_pData), m_Len(_Len) {} int f_GetLen() const { return m_Len; } char *m_pData; int m_Len; };
TCFuture<int> fg_ReferenceMember(CJob &_Job) {
  CJobContext Context{_Job};
  return fg_CallSafe([&Job = Context.m_Job]() -> TCFuture<int> { co_await fg_Work(); co_return Job.m_Value; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Job', which it captures by reference, after a suspension, when 'Job' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
TCFuture<int> fg_ReferenceMemberGetter(CJob &_Job) {
  CJobContext Context{_Job};
  return fg_CallSafe([&Job = Context.f_Job()]() -> TCFuture<int> { co_await fg_Work(); co_return Job.m_Value; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Job', which it captures by reference, after a suspension, when 'Job' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
TCFuture<int> fg_ReferenceMemberBinding(CJob &_Job) {
  CJobContext Context{_Job};
  auto &[Job] = Context;
  return fg_CallSafe([&Job]() -> TCFuture<int> { co_await fg_Work(); co_return Job.m_Value; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'Job', which it captures by reference, after a suspension, when 'Job' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
TCFuture<int> fg_ReferenceMemberParameter(CRequestContext _Context) {
  return fg_CallSafe([&RequestID = _Context.m_RequestID]() -> TCFuture<int> { co_await fg_Work(); co_return RequestID; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'RequestID', which it captures by reference, after a suspension, when 'RequestID' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
TCFuture<int> fg_InitCapture() {
  int Value = 1;
  return fg_CallSafe([Context = CReadContext{&Value}]() -> TCFuture<int> { co_await fg_Work(); co_return *Context.m_pValue; });
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a TCFuture coroutine lambda uses 'Context', which points to 'Value', after a suspension, when 'Value' may be gone; {{.*}}
}
TCFuture<int> fg_ViewLength() {
  char Buffer[8]{};
  CStrSpan Text(Buffer, 8);
  return fg_CallSafe([Text]() -> TCFuture<int> { co_await fg_Work(); co_return Text.f_GetLen(); });
}
TCFuture<int> fg_ViewData() {
  char Buffer[8]{};
  CStrSpan Text(Buffer, 8);
  return fg_CallSafe([Text]() -> TCFuture<int> { co_await fg_Work(); co_return *Text.m_pData; });
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a TCFuture coroutine lambda uses 'Text', which points to 'Buffer', after a suspension, when 'Buffer' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
TCFuture<int> fg_OwnStorage() {
  CReadContext Context{nullptr};
  return fg_CallSafe([&pValue = Context.m_pValue]() -> TCFuture<int> { co_await fg_Work(); co_return *pValue; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'pValue', which it captures by reference, after a suspension, when 'pValue' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
TCFuture<int> fg_OwnStorageThroughPointer() {
  CReadContext Context{nullptr};
  auto *pContext = &Context;
  return fg_CallSafe([&pValue = pContext->m_pValue]() -> TCFuture<int> { co_await fg_Work(); co_return *pValue; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda uses 'pValue', which it captures by reference, after a suspension, when 'pValue' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
} // namespace NReview2Round184Capture

// Round 192 llvm-coroutine-5: a closure assigned to a local function wrapper
// that captures a local by reference is reported when the wrapper is returned,
// stored in a member or dispatched.
namespace NReview2Round192Holder {
TCFuture<int> fg_Work();
struct CHolder { TCFunction<TCFuture<int> ()> m_fRead; };
TCFunction<TCFuture<int> ()> fg_Returned() {
  int Local = 0;
  TCFunction<TCFuture<int> ()> fRead;
  fRead = [&Local]() -> TCFuture<int> { co_await fg_Work(); co_return Local; };
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: a TCFuture coroutine lambda captures 'Local' by reference but is not called directly where 'Local' is, so it can run after 'Local' is gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
  return fRead;
}
void fg_Member(CHolder &o_Holder) {
  int Local = 0;
  TCFunction<TCFuture<int> ()> fRead;
  fRead = [&Local]() -> TCFuture<int> { co_await fg_Work(); co_return Local; };
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: a TCFuture coroutine lambda captures 'Local' by reference but is not called directly where 'Local' is, so it can run after 'Local' is gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
  o_Holder.m_fRead = fg_Move(fRead);
}
void fg_Dispatched() {
  int Local = 0;
  TCFunction<TCFuture<int> ()> fRead;
  fRead = [&Local]() -> TCFuture<int> { co_await fg_Work(); co_return Local; };
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: a TCFuture coroutine lambda captures 'Local' by reference but is not called directly where 'Local' is, so it can run after 'Local' is gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
  fg_Dispatch(fg_Move(fRead));
}
} // namespace NReview2Round192Holder

namespace NReview2Round193Wrapper {
// A local function wrapper is the closure at the uses the definition that
// gives it the closure reaches; assigned again, cleared or assigned nullptr,
// what it holds after is another value. A lambda that captures the wrapper
// and is only called in the scope runs here.
TCFuture<int> fg_Work();
TCFuture<int> fg_CallBefore() {
  TCFunction<TCFuture<int> ()> fRead = []() -> TCFuture<int> { co_return 7; };
  int Value = co_await fRead();
  fRead = [Value]() -> TCFuture<int> { co_await fg_Work(); co_return Value; };
  co_return co_await fg_CallSafe(fg_Move(fRead));
}
TCFuture<int> fg_Replaced() {
  TCFunction<TCFuture<int> ()> fRead = [Value = 1]() -> TCFuture<int> { co_await fg_Work(); co_return Value; };
  fRead = []() -> TCFuture<int> { co_return 7; };
  co_return co_await fRead();
}
TCFuture<int> fg_Cancel(bool _bCancel) {
  TCFunction<TCFuture<int> ()> fRead = [Value = 42]() -> TCFuture<int> { co_await fg_Work(); co_return Value; };
  if (_bCancel) {
    fRead.f_Clear();
    co_return 0;
  }
  co_return co_await fg_CallSafe(fg_Move(fRead));
}
TCFuture<int> fg_ClearAfter() {
  TCFunction<TCFuture<int> ()> fRead = [Value = 42]() -> TCFuture<int> { co_await fg_Work(); co_return Value; };
  int Result = co_await fg_CallSafe(fRead);
  fRead.f_Clear();
  co_return Result;
}
TCFunction<TCFuture<int> ()> fg_NullReturn() {
  int Local = 0;
  TCFunction<TCFuture<int> ()> fRead = [&Local]() -> TCFuture<int> { co_await fg_Work(); co_return Local; };
  fRead = nullptr;
  return fRead;
}
TCFuture<int> fg_MovedToCopy() {
  TCFunction<TCFuture<int> ()> fRead;
  fRead = [Value = 42]() -> TCFuture<int> { co_await fg_Work(); co_return Value; };
  auto fCopy = fg_Move(fRead);
  co_return co_await fg_CallSafe(fg_Move(fCopy));
}
TCFuture<int> fg_ThroughAlias() {
  TCFunction<TCFuture<int> ()> fRead;
  auto &fAlias = fRead;
  fAlias = [Value = 42]() -> TCFuture<int> { co_await fg_Work(); co_return Value; };
  co_return co_await fg_CallSafe(fg_Move(fRead));
}
TCFuture<int> fg_Forwarded() {
  int Value = 0;
  TCFunction<TCFuture<int> ()> fRead = [&Value]() -> TCFuture<int> { co_return Value; };
  co_return co_await fg_CallSafe([&fRead] { return fRead(); });
}
TCFuture<int> fg_AssignedInCalledLambda() {
  TCFunction<TCFuture<int> ()> fRead;
  [&] { fRead = [Value = 1]() -> TCFuture<int> { co_await fg_Work(); co_return Value; }; }();
  co_return co_await fg_CallSafe(fg_Move(fRead));
}
// Initialized once with a coroutine lambda that captures a local by
// reference, and not written otherwise: where the wrapper goes is where the
// closure goes.
struct CHolder { TCFunction<TCFuture<int> ()> m_fRead; };
TCFunction<TCFuture<int> ()> fg_InitReturned() {
  int Local = 0;
  TCFunction<TCFuture<int> ()> fRead = [&Local]() -> TCFuture<int> { co_await fg_Work(); co_return Local; };
// CHECK-MESSAGES: :[[@LINE-1]]:42: warning: a TCFuture coroutine lambda captures 'Local' by reference but is not called directly where 'Local' is, so it can run after 'Local' is gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
  return fRead;
}
void fg_InitMember(CHolder &o_Holder) {
  int Local = 0;
  TCFunction<TCFuture<int> ()> fRead = [&Local]() -> TCFuture<int> { co_await fg_Work(); co_return Local; };
// CHECK-MESSAGES: :[[@LINE-1]]:42: warning: a TCFuture coroutine lambda captures 'Local' by reference but is not called directly where 'Local' is, so it can run after 'Local' is gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
  o_Holder.m_fRead = fg_Move(fRead);
}
void fg_InitDispatched() {
  int Local = 0;
  TCFunction<TCFuture<int> ()> fRead = [&Local]() -> TCFuture<int> { co_await fg_Work(); co_return Local; };
// CHECK-MESSAGES: :[[@LINE-1]]:42: warning: a TCFuture coroutine lambda captures 'Local' by reference but is not called directly where 'Local' is, so it can run after 'Local' is gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
  fg_Dispatch(fg_Move(fRead));
}
} // namespace NReview2Round193Wrapper

namespace NMib::NSys {
struct CIoSpan { void const *m_pData; decltype(sizeof(0)) m_nBytes; };
} // namespace NMib::NSys
namespace NReview2Round190Part {
// A part of a view the closure holds, written in it, `Span.m_pData =
// g_Data`, holds what it is written, before or after a suspension, like the
// whole of it written; the view as captured still points to the local.
int const g_Data[] = {42};
TCFuture<int> fg_Work();
TCFuture<int> fg_PartBefore() {
  int Local = 0;
  NMib::NSys::CIoSpan Span{&Local, sizeof(Local)};
  return fg_CallSafe([Span]() mutable -> TCFuture<int> {
    Span.m_pData = g_Data;
    co_await fg_Work();
    co_return static_cast<int const *>(Span.m_pData)[0];
  });
}
TCFuture<int> fg_PartAfter() {
  int Local = 0;
  NMib::NSys::CIoSpan Span{&Local, sizeof(Local)};
  return fg_CallSafe([Span]() mutable -> TCFuture<int> {
    co_await fg_Work();
    Span.m_pData = g_Data;
    co_return static_cast<int const *>(Span.m_pData)[0];
  });
}
TCFuture<int> fg_Whole() {
  int Local = 0;
  NMib::NSys::CIoSpan Span{&Local, sizeof(Local)};
  return fg_CallSafe([Span]() mutable -> TCFuture<int> {
    co_await fg_Work();
    Span = {g_Data, sizeof(g_Data)};
    co_return static_cast<int const *>(Span.m_pData)[0];
  });
}
TCFuture<int> fg_NotWritten() {
  int Local = 0;
  NMib::NSys::CIoSpan Span{&Local, sizeof(Local)};
  return fg_CallSafe([Span]() mutable -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a TCFuture coroutine lambda uses 'Span', which points to 'Local', after a suspension, when 'Local' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
    co_await fg_Work();
    co_return static_cast<int const *>(Span.m_pData)[0];
  });
}
} // namespace NReview2Round190Part

namespace NReview2Round193Walker {
// Where a copy points is where provenance says it does: a raw pointer into
// what a local owner owns, with no keeper in the closure, is reported, also
// when the closure is stored in that allocation; what a reference parameter to
// a const character array designates is a string literal, also deduced from
// `auto const &`; a pointer into a temporary made in an init capture's
// initializer is reported as that init capture, one made in another statement
// isn't; the array of a named closure's list lives with that local.
using namespace NMib::NStorage;
TCFuture<int> fg_Suspend();
struct CGate {
  int m_Value = 0;
};
struct CStorage {
  CGate m_Gate;
  TCFunction<TCFuture<int>()> m_fRun;
};
void fg_Stored() {
  TCSharedPointer<CStorage> pStorage = new CStorage;
  pStorage->m_fRun = [pGate = &pStorage->m_Gate]() mutable -> TCFuture<int> { co_await fg_Suspend(); co_return pGate->m_Value; };
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a TCFuture coroutine lambda captures 'pGate', which points to 'pStorage', but is not called directly where 'pStorage' is, so it can run after 'pStorage' is gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
void fg_Dispatched() {
  TCSharedPointer<CStorage> pStorage = new CStorage;
  (void)(g_Dispatch / [pGate = &pStorage->m_Gate]() mutable -> TCFuture<int> { co_await fg_Suspend(); co_return pGate->m_Value; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda captures 'pGate', which points to 'pStorage', but is not called directly where 'pStorage' is, so it can run after 'pStorage' is gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
void fg_Kept() {
  TCSharedPointer<CStorage> pStorage = new CStorage;
  (void)(g_Dispatch / [pGate = &pStorage->m_Gate, pStorage]() mutable -> TCFuture<int> { co_await fg_Suspend(); co_return pGate->m_Value; });
}
void fg_String(char const (&_Error)[6]) {
  (void)(g_Dispatch / [Error = _Error]() -> TCFuture<int> { co_await fg_Suspend(); co_return *Error; });
}
void fg_WideString(wchar_t const (&_Error)[6]) {
  (void)(g_Dispatch / [Error = _Error]() -> TCFuture<int> { co_await fg_Suspend(); co_return *Error; });
}
void fg_GenericString() {
  auto fCall = [](auto const &_Error) {
    (void)(g_Dispatch / [Error = _Error]() -> TCFuture<int> { co_await fg_Suspend(); co_return *Error; });
  };
  fCall("Error");
}
void fg_Integers(int const (&_Values)[6]) {
  (void)(g_Dispatch / [pValues = _Values]() -> TCFuture<int> { co_await fg_Suspend(); co_return *pValues; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda captures 'pValues', which points to '_Values', but is not called directly where '_Values' is, so it can run after '_Values' is gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
void fg_Address(int const &_Value) {
  (void)(g_Dispatch / [pValue = &_Value]() -> TCFuture<int> { co_await fg_Suspend(); co_return *pValue; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: a TCFuture coroutine lambda captures 'pValue', which points to '_Value', but is not called directly where '_Value' is, so it can run after '_Value' is gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
struct CValue {
  int m_Value = 0;
  int *f_Get() { return &m_Value; }
};
TCFuture<int> fg_TemporaryInInit() {
  return fg_CallSafe([pValue = CValue{}.f_Get()]() -> TCFuture<int> { co_await fg_Suspend(); co_return *pValue; });
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a TCFuture coroutine lambda uses 'pValue', which points to 'pValue', after a suspension, when 'pValue' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
TCFuture<int> fg_TemporaryBefore() {
  int *pTemporary = CValue{}.f_Get();
  return fg_CallSafe([pValue = pTemporary]() -> TCFuture<int> { co_await fg_Suspend(); co_return *pValue; });
}
} // namespace NReview2Round193Walker

#include "malterlib-runs-to-completion.h"
namespace NReview2Round194ListArray {
// The array of a named closure's list is extended by that local, the
// language's rule, and lives while a joined call runs; the array of a
// closure that is itself a temporary is a temporary of the full expression.
TCFuture<int> fg_Work();
DMibRunsToCompletion TCFuture<int> fg_Named() {
  auto fRead = [Ids = {1, 2}]() -> TCFuture<int> { co_await fg_Work(); co_return *Ids.begin(); };
  co_return co_await fg_CallSafe(fRead);
}
TCFuture<int> fg_Temporary() {
  co_return co_await fg_CallSafe([Ids = {1, 2}]() -> TCFuture<int> { co_await fg_Work(); co_return *Ids.begin(); });
// CHECK-MESSAGES: :[[@LINE-1]]:35: warning: a TCFuture coroutine lambda uses 'Ids', which points to 'Ids', after a suspension, when 'Ids' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
} // namespace NReview2Round194ListArray

namespace NReview2Round195Joined {
// A temporary of the full expression that awaits a callee lives until the
// callee is joined there, in code that runs to completion, and so does one
// given to a callee that runs it to completion before it returns
// (BlockingCallees): the array of the list a temporary closure captures.
TCFuture<int> fg_Work();
DMibRunsToCompletion TCFuture<int> fg_Joined() {
  co_return co_await fg_CallSafe([Ids = {1, 2}]() -> TCFuture<int> { co_await fg_Work(); co_return *Ids.begin(); });
}
DMibRunsToCompletion void fg_Blocking() {
  CTestCategoryScope() % [Ids = {1, 2}]() -> TCFuture<int> { co_await fg_Work(); co_return *Ids.begin(); };
}
// Stored and joined in a later statement, after the array is gone, and the
// same where the code doesn't run to completion.
DMibRunsToCompletion TCFuture<int> fg_Stored() {
  auto Future = fg_CallSafe([Ids = {1, 2}]() -> TCFuture<int> { co_await fg_Work(); co_return *Ids.begin(); });
// CHECK-MESSAGES: :[[@LINE-1]]:30: warning: a TCFuture coroutine lambda uses 'Ids', which points to 'Ids', after a suspension, when 'Ids' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
  co_return co_await fg_Move(Future);
}
TCFuture<int> fg_NotRunToCompletion() {
  co_return co_await fg_CallSafe([Ids = {1, 2}]() -> TCFuture<int> { co_await fg_Work(); co_return *Ids.begin(); });
// CHECK-MESSAGES: :[[@LINE-1]]:35: warning: a TCFuture coroutine lambda uses 'Ids', which points to 'Ids', after a suspension, when 'Ids' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
} // namespace NReview2Round195Joined

namespace NReview2Round200Nested {
// Round 200 (llvm-coroutine-1): an owner reached only through a unique owner
// the closure takes is changed only by the code of the function and of the
// closure, so the unique owner keeps what it owns. Reported: the same through
// a shared owner, which others can change, and with the closure resetting the
// nested owner itself.
TCFuture<int> fg_Work();
struct CJob { int m_Result = 7; };
template <typename T> struct TCUniquePointer {
 TCUniquePointer(T*); TCUniquePointer(TCUniquePointer&&); ~TCUniquePointer();
 TCUniquePointer &operator=(TCUniquePointer &&);
 T* operator->() const; T& operator*() const;
};
struct CContext { TCUniquePointer<CJob> m_pJob{new CJob}; };
TCFuture<int> f_Nested() {
 TCUniquePointer<CContext> pContext(new CContext);
 int &Result = pContext->m_pJob->m_Result;
 co_await fg_Work();
 co_return Result;
}
TCFuture<int> f_CaptureNested() {
 TCUniquePointer<CContext> pContext(new CContext);
 int &Result = pContext->m_pJob->m_Result;
 return fg_CallSafe([&Result, pKeep = fg_Move(pContext)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
}
TCSharedPointer<CContext> fg_ShareContext();
TCFuture<int> f_CaptureNestedShared() {
 TCSharedPointer<CContext> pContext = fg_ShareContext();
 int &Result = pContext->m_pJob->m_Result;
 return fg_CallSafe([&Result, pKeep = fg_Move(pContext)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
TCFuture<int> f_CaptureNestedReset() {
 TCUniquePointer<CContext> pContext(new CContext);
 int &Result = pContext->m_pJob->m_Result;
 return fg_CallSafe([&Result, pKeep = fg_Move(pContext)]() -> TCFuture<int> { pKeep->m_pJob = TCUniquePointer<CJob>(new CJob); co_await fg_Work(); co_return Result; });
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
} // namespace NReview2Round200Nested

namespace NReview2Round202CalledOnce {
// Round 202 (llvm-coroutine-3): a mutable closure given to a function that
// runs what it is given (DMibRunsGivenFunction) runs once, so what its body
// assigns after a use doesn't reach that use. Reported: the keeper cleared
// before the use, and a closure kept in a local and called twice.
TCFuture<int> fg_Work();
struct CJob { int m_Value = 0; };
TCFuture<int> fg_Read(TCSharedPointer<CJob> _pJob)
{
	int *pValue = &_pJob->m_Value;
	co_return co_await fg_CallSafe([pValue, pKeep = _pJob]() mutable -> TCFuture<int> { co_await fg_Work(); int Value = *pValue; pKeep.f_Clear(); co_return Value; });
}
TCFuture<int> fg_ReadCleared(TCSharedPointer<CJob> _pJob)
{
	int *pValue = &_pJob->m_Value;
	co_return co_await fg_CallSafe([pValue, pKeep = _pJob]() mutable -> TCFuture<int> { co_await fg_Work(); pKeep.f_Clear(); co_return *pValue; });
// CHECK-MESSAGES: :[[@LINE-1]]:34: warning: a TCFuture coroutine lambda uses 'pValue', which points to '_pJob', after a suspension, when '_pJob' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
}
TCFuture<int> fg_ReadTwice(TCSharedPointer<CJob> _pJob)
{
	int *pValue = &_pJob->m_Value;
	auto fRun = [pValue, pKeep = _pJob]() mutable -> TCFuture<int> { co_await fg_Work(); int Value = *pValue; pKeep.f_Clear(); co_return Value; };
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: a TCFuture coroutine lambda uses 'pValue', which points to '_pJob', after a suspension, when '_pJob' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
	co_await fRun();
	co_return co_await fRun();
}
} // namespace NReview2Round202CalledOnce

namespace NReview2Round202Constants {
// Round 202 (llvm-coroutine-10): a name that isn't an odr-use, the value of a
// constant read as a constant, touches no storage, also in a `[&]` lambda.
// Reported: a constant initialized at run time, and the address of the
// constant.
TCFuture<int> fg_Work();
int fg_GetSize();
TCFuture<int> fg_Run()
{
	constexpr int c_ChunkSize = 4096;
	auto fRead = [&](auto _Extra) -> TCFuture<int> { co_await fg_Work(); co_return c_ChunkSize + _Extra; };
	return fRead(1);
}
TCFuture<int> fg_RunRuntime()
{
	int const ChunkSize = fg_GetSize();
	auto fRead = [&](auto _Extra) -> TCFuture<int> { co_await fg_Work(); co_return ChunkSize + _Extra; };
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: a TCFuture coroutine lambda uses 'ChunkSize', which it captures by reference, after a suspension, when 'ChunkSize' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
// The generic lambda's invented template head is at its introducer, so the
// report on it names the instantiation of its call operator.
// CHECK-MESSAGES: :[[@LINE+1]]:14: note: in instantiation of function template specialization 'NReview2Round202Constants::fg_RunRuntime()::(lambda)::operator()<int>' requested here
	return fRead(1);
}
TCFuture<int> fg_RunAddress()
{
	constexpr int c_ChunkSize = 4096;
	auto fRead = [&](auto _Extra) -> TCFuture<int> { co_await fg_Work(); co_return *&c_ChunkSize + _Extra; };
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: a TCFuture coroutine lambda uses 'c_ChunkSize', which it captures by reference, after a suspension, when 'c_ChunkSize' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
	return fRead(1);
}
} // namespace NReview2Round202Constants

// A copy of a captured record made after a suspension, of a record given the
// address of a local of the function that is gone by then.
namespace NUnreportedShapesRecordCopy
{
struct CPlain { int const *m_pValue; };
TCFuture<int> fg_Copy()
{
  int Local = 0;
  CPlain Record{&Local};
  auto Future = fg_CallSafe([Record]() -> TCFuture<CPlain> { co_await fg_Suspend(); CPlain Copy = Record; co_return Copy; });
// CHECK-MESSAGES: :[[@LINE-1]]:30: warning: a TCFuture coroutine lambda uses 'Record', which points to 'Local', after a suspension, when 'Local' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
  co_return 0;
}
} // namespace NUnreportedShapesRecordCopy

namespace NReview2Round203Identity {
// Round 203 (llvm-coroutine-4, -8): whether a keeper owns what a borrow is
// into is the same, another, or can't be told. Two results of calls that
// can't be seen can't be told, so the type decides; an owner made from a raw
// pointer to an owner's object owns that object. Reported: the result of a
// function that states it gives back a new object (DMibReturnsNew) and of a
// factory whose body is seen, no keeper, and an owner of a new object.
TCFuture<int> fg_Work();
struct CJob { int m_Value = 0; int m_Result = 0; };
TCSharedPointer<CJob> fg_CurrentJob();
DMibReturnsNew TCSharedPointer<CJob> fg_MakeJob();
TCSharedPointer<CJob> fg_SeenFactory() { return TCSharedPointer<CJob>(new CJob); }
TCFuture<int> fg_Read() { auto pJob = fg_CurrentJob(); auto *pValue = &pJob->m_Value; return fg_CallSafe([pValue, pKeep = fg_CurrentJob()]() -> TCFuture<int> { co_await fg_Work(); co_return *pValue; }); }
TCFuture<int> fg_ReadNoKeeper() { auto pJob = fg_CurrentJob(); auto *pValue = &pJob->m_Value; return fg_CallSafe([pValue]() -> TCFuture<int> { co_await fg_Work(); co_return *pValue; }); }
// CHECK-MESSAGES: :[[@LINE-1]]:115: warning: a TCFuture coroutine lambda uses 'pValue', which points to 'pJob', after a suspension, when 'pJob' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
TCFuture<int> fg_ReadStatedNew() { auto pJob = fg_MakeJob(); auto *pValue = &pJob->m_Value; return fg_CallSafe([pValue, pKeep = fg_MakeJob()]() -> TCFuture<int> { co_await fg_Work(); co_return *pValue; }); }
// CHECK-MESSAGES: :[[@LINE-1]]:113: warning: a TCFuture coroutine lambda uses 'pValue', which points to 'pJob', after a suspension, when 'pJob' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
TCFuture<int> fg_ReadSeenFactory() { auto pJob = fg_SeenFactory(); auto *pValue = &pJob->m_Value; return fg_CallSafe([pValue, pKeep = fg_SeenFactory()]() -> TCFuture<int> { co_await fg_Work(); co_return *pValue; }); }
// CHECK-MESSAGES: :[[@LINE-1]]:119: warning: a TCFuture coroutine lambda uses 'pValue', which points to 'pJob', after a suspension, when 'pJob' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
struct CStyle { bool m_bAlignTrailingComments = false; };
TCSharedPointer<CStyle> fg_MalterlibStyle();
TCFuture<int> fg_Style() { auto pStyle = fg_MalterlibStyle(); auto const &Style = *pStyle; return fg_CallSafe([&Style, pKeep = fg_MalterlibStyle()]() -> TCFuture<int> { co_await fg_Work(); co_return Style.m_bAlignTrailingComments; }); }
TCFuture<int> fg_Rebuilt(TCSharedPointer<CJob> const &_pJob) { int &Result = _pJob->m_Result; return fg_CallSafe([&Result, pKeep = TCSharedPointer<CJob>(_pJob.f_Get())]() -> TCFuture<int> { co_await fg_Work(); co_return Result; }); }
TCFuture<int> fg_RebuiltNew(TCSharedPointer<CJob> const &_pJob) { int &Result = _pJob->m_Result; return fg_CallSafe([&Result, pKeep = TCSharedPointer<CJob>(new CJob)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; }); }
// CHECK-MESSAGES: :[[@LINE-1]]:119: warning: a TCFuture coroutine lambda uses 'Result', which it captures by reference, after a suspension, when 'Result' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
} // namespace NReview2Round203Identity
