// RUN: %check_clang_tidy -std=c++20-or-later %s malterlib-coroutine-reference-parameter %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -I %S/Inputs

#include "malterlib-concurrency.h"

template <typename t_CChar>
struct TCStrSpan
{
	t_CChar const *m_pData;
};
using CStrSpan = TCStrSpan<char>;

struct CStr
{
	int m_Length = 0;
};

TCFuture<int> fg_Suspend();
TCFuture<int> fg_Get(int _Value);

struct CBorrowingAwaiter
{
	bool await_ready() const noexcept;
	void await_suspend(std::coroutine_handle<>) const noexcept;
	int await_resume() const noexcept;
};

// A coroutine runs in its call until it first suspends, while the caller has
// the data.
TCFuture<int> fg_BeforeSuspension(int *_pValue, CStrSpan _Span, CStr const &_Value, void (*_fCallback)())
{
	int Copy = *_pValue + _Value.m_Length + (_Span.m_pData ? 1 : 0);
	co_await fg_Suspend();
	_fCallback();
	co_return Copy;
}

TCFuture<int> fg_AfterSuspension(int *_pValue, CStrSpan _Span, CStr const &_Value)
// CHECK-MESSAGES: :[[@LINE-1]]:39: warning: TCFuture coroutine 'fg_AfterSuspension' uses '_pValue' after a suspension, but it takes it as 'int *', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
// CHECK-MESSAGES: :[[@LINE-2]]:57: warning: TCFuture coroutine 'fg_AfterSuspension' uses '_Span' after a suspension, but it takes it as 'CStrSpan' (aka 'TCStrSpan<char>'), {{.*}}
// CHECK-MESSAGES: :[[@LINE-3]]:76: warning: TCFuture coroutine 'fg_AfterSuspension' uses '_Value' after a suspension, but it takes it as 'const CStr &', {{.*}}
{
	co_await fg_Suspend();
	co_return *_pValue + _Span.m_pData[0] + _Value.m_Length;
}

// A copy of a pointer, or a reference to what it refers to, is used like it.
TCFuture<int> fg_Alias(int *_pValue, CStr const &_Value)
// CHECK-MESSAGES: :[[@LINE-1]]:29: warning: TCFuture coroutine 'fg_Alias' uses '_pValue' after a suspension, {{.*}}
// CHECK-MESSAGES: :[[@LINE-2]]:50: warning: TCFuture coroutine 'fg_Alias' uses '_Value' after a suspension, {{.*}}
{
	int *pCopy = _pValue;
	int const &Length = _Value.m_Length;
	int Copy = _Value.m_Length;
	co_await fg_Suspend();
	co_return *pCopy + Length + Copy;
}

// Copies of views, moves and structured bindings borrow too.
struct CPair
{
	int m_First = 0;
	int m_Second = 0;
};

TCFuture<int> fg_ViewCopy(CStrSpan _Span, int *_pValue, CPair const &_Pair)
// CHECK-MESSAGES: :[[@LINE-1]]:36: warning: TCFuture coroutine 'fg_ViewCopy' uses '_Span' after a suspension, {{.*}}
// CHECK-MESSAGES: :[[@LINE-2]]:48: warning: TCFuture coroutine 'fg_ViewCopy' uses '_pValue' after a suspension, {{.*}}
// CHECK-MESSAGES: :[[@LINE-3]]:70: warning: TCFuture coroutine 'fg_ViewCopy' uses '_Pair' after a suspension, {{.*}}
{
	auto Copy = _Span;
	int *pMoved = fg_Move(_pValue);
	auto const &[First, Second] = _Pair;
	co_await fg_Suspend();
	co_return (Copy.m_pData ? 1 : 0) + *pMoved + First + Second;
}

// Before the first suspension a parameter may only be used as a value, so a
// pointer to it counts even if it is pointed elsewhere later.
TCFuture<int> fg_Repointed(CStr const &_Value)
// CHECK-MESSAGES: :[[@LINE-1]]:40: warning: TCFuture coroutine 'fg_Repointed' uses '_Value' after a suspension, {{.*}}
{
	int const *pLength = &_Value.m_Length;
	co_await fg_Suspend();
	int Local = 0;
	pLength = &Local;
	co_return *pLength;
}

// The result of `++` or an assignment is the data itself.
TCFuture<int> fg_ModifiedAlias(int &_Value)
// CHECK-MESSAGES: :[[@LINE-1]]:37: warning: TCFuture coroutine 'fg_ModifiedAlias' uses '_Value' after a suspension, {{.*}}
{
	int &Alias = ++_Value;
	co_await fg_Suspend();
	co_return Alias;
}

void fg_Consume(int const &_Value);

// A lambda that captures a copy owns it, also before the suspension.
TCFuture<int> fg_CopiedBeforeUse(int &_Value)
{
	auto fUse = [_Value]
		{
			fg_Consume(_Value);
		}
	;
	co_await fg_Suspend();
	fUse();
	co_return 0;
}

// Code that can't run keeps nothing.
TCFuture<int> fg_UnreachableBorrow(int &_Value)
{
	if (false)
	{
		int &Alias = _Value;
	}
	co_await fg_Suspend();
	co_return 0;
}

// A template whose awaiter keeps a reference to the data.
struct CReader
{
	CBorrowingAwaiter f_Read() const;
};

template <typename t_CReader>
TCFuture<int> fg_DependentAwait(t_CReader const *_pReader)
// CHECK-MESSAGES: :[[@LINE-1]]:50: warning: TCFuture coroutine 'fg_DependentAwait' uses '_pReader' after a suspension, {{.*}}
{
	co_return co_await _pReader->f_Read();
}

// What the parameter points to can be an actor, so the instantiations tell.
void fg_InstantiateReader()
{
	CReader Reader;
	fg_DependentAwait(&Reader);
}

// A lambda that captures a copy uses its own copy.
TCFuture<int> fg_CopiedIntoLambda(CStr const &_Value)
{
	auto fGet = [_Value]
		{
			return _Value.m_Length;
		}
	;
	co_await fg_Suspend();
	co_return fGet();
}

// An await that can't run doesn't suspend.
TCFuture<int> fg_Unreachable(CStr const &_Value)
{
	if (false)
		co_await fg_Suspend();
	int const *pLength = &_Value.m_Length;
	co_return *pLength;
}

// What the first `co_await` awaits runs before it suspends.
TCFuture<int> fg_InFirstAwait(CStr const &_Value)
{
	co_return co_await fg_Get(_Value.m_Length);
}

TCFuture<int> fg_InLoop(CStr const &_Value)
// CHECK-MESSAGES: :[[@LINE-1]]:37: warning: TCFuture coroutine 'fg_InLoop' uses '_Value' after a suspension, {{.*}}
{
	int Sum = 0;
	for (int iTry = 0; iTry < 2; ++iTry)
		Sum += co_await fg_Get(_Value.m_Length);
	co_return Sum;
}

// A lambda the coroutine makes can run whenever it is called, also after a
// suspension.
TCFuture<int> fg_InLambda(CStr const &_Value)
// CHECK-MESSAGES: :[[@LINE-1]]:39: warning: TCFuture coroutine 'fg_InLambda' uses '_Value' after a suspension, {{.*}}
{
	auto fGet = [&]
		{
			return _Value.m_Length;
		}
	;
	co_await fg_Get(0);
	co_return fGet();
}

TCFuture<int> fg_Unused(int *_pValue, CStr const &, CStrSpan _Span)
{
	co_await fg_Suspend();
	co_return 0;
}

TCFutureWithFlags<int, ECoroutineFlag_None> fg_Flags(int *_pValue)
// CHECK-MESSAGES: :[[@LINE-1]]:59: warning: TCFuture coroutine 'fg_Flags' uses '_pValue' after a suspension, {{.*}}
{
	co_await fg_Suspend();
	co_return *_pValue;
}

void fg_Lambda()
{
	auto fLambda = [](int *_pValue) -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:25: warning: TCFuture coroutine 'operator()' uses '_pValue' after a suspension, {{.*}}
		{
			co_await fg_Suspend();
			co_return *_pValue;
		}
	;
	auto fBefore = [](int *_pValue) -> TCFuture<int>
		{
			co_return *_pValue;
		}
	;
}

template <typename tf_CType>
TCFuture<int> fg_Template(tf_CType *_pValue)
// CHECK-MESSAGES: :[[@LINE-1]]:37: warning: TCFuture coroutine 'fg_Template' uses '_pValue' after a suspension, {{.*}}
{
	co_await fg_Suspend();
	co_return *_pValue;
}

void fg_InstantiateTemplate()
{
	int Value = 0;
	fg_Template(&Value);
}

template <typename tf_CChar>
TCFuture<int> fg_DependentView(TCStrSpan<tf_CChar> _Span, tf_CChar _Value)
// CHECK-MESSAGES: :[[@LINE-1]]:52: warning: TCFuture coroutine 'fg_DependentView' uses '_Span' after a suspension, {{.*}}
{
	co_await fg_Suspend();
	co_return _Span.m_pData[0];
}
template TCFuture<int> fg_DependentView<char>(TCStrSpan<char> _Span, char _Value);

TCUnsafeFuture<int> fg_Unsafe(int *_pValue)
{
	co_await fg_Suspend();
	co_return *_pValue;
}

TCFuture<int> fg_NotCoroutine(int *_pValue)
{
	return {};
}

struct CMyActor;

// Pointers and references to an actor, its internals or a class that allows
// it are used like `this`.
struct CInternal : CActorInternal
{
	int m_Value = 0;

	static TCFuture<int> fs_Cleanup(CInternal *_pThis)
	{
		co_await fg_Suspend();
		co_return _pThis->m_Value;
	}
};

struct CAllowed : CAllowUnsafeThis
{
	int m_Value = 0;
};

TCFuture<int> fg_Allowed(CAllowed &_Allowed, CInternal const *_pInternal)
{
	co_await fg_Suspend();
	co_return _Allowed.m_Value + _pInternal->m_Value;
}

struct CMyActor : CActor
{
	// Actor calls move the arguments into the coroutine once.
	TCFuture<int> f_Reference(CStr const &_Value, CStr &&_Moved)
// CHECK-MESSAGES: :[[@LINE-1]]:40: warning: TCFuture coroutine 'f_Reference' is a member function of actor 'CMyActor' and takes 'const CStr &'; actor calls move the arguments into the coroutine once, so take it by value [malterlib-coroutine-reference-parameter]
// CHECK-MESSAGES: :[[@LINE-2]]:55: warning: TCFuture coroutine 'f_Reference' is a member function of actor 'CMyActor' and takes 'CStr &&'; {{.*}}
	{
		co_return 0;
	}

	TCFuture<int> f_Pointer(int *_pValue)
// CHECK-MESSAGES: :[[@LINE-1]]:31: warning: TCFuture coroutine 'f_Pointer' is a member function of actor 'CMyActor' and takes 'int *', whose data can be gone when an actor call starts the coroutine, after the call returns; take the data by value [malterlib-coroutine-reference-parameter]
	{
		co_return *_pValue;
	}

	static TCFuture<int> fs_Reference(CStr const &_Value)
	{
		co_return _Value.m_Length;
	}
};

struct CHelper
{
	TCFuture<int> f_Reference(CStr const &_Value)
	{
		co_return _Value.m_Length;
	}
};

// Exchanging a pointer keeps where it points, and so does an address cast to
// an integer.
TCFuture<int> fg_ExchangedPointer(int *_pValue)
// CHECK-MESSAGES: :[[@LINE-1]]:40: warning: TCFuture coroutine 'fg_ExchangedPointer' uses '_pValue' after a suspension, {{.*}}
{
	int *pCopy = fg_Exchange(_pValue, nullptr);
	co_await fg_Suspend();
	co_return *pCopy;
}

TCFuture<int> fg_AddressAsInteger(int &_Value)
// CHECK-MESSAGES: :[[@LINE-1]]:40: warning: TCFuture coroutine 'fg_AddressAsInteger' uses '_Value' after a suspension, {{.*}}
{
	auto Address = reinterpret_cast<unsigned long>(&_Value);
	co_await fg_Suspend();
	co_return *reinterpret_cast<int *>(Address);
}

TCFuture<int> fg_ExchangedValue(int &_Value)
{
	int Old = fg_Exchange(_Value, 0);
	co_await fg_Suspend();
	co_return Old;
}

// A copy constructor's other arguments are not copied.
struct CCopied
{
	CCopied(CCopied const &_Other, int const *_pExtra = nullptr);
};

TCFuture<int> fg_CopyExtra(CCopied const &_Copied, int const &_Extra)
// CHECK-MESSAGES: :[[@LINE-1]]:63: warning: TCFuture coroutine 'fg_CopyExtra' uses '_Extra' after a suspension, {{.*}}
{
	CCopied Copy(_Copied, &_Extra);
	co_await fg_Suspend();
	co_return 0;
}

// An awaiter wrapped in fg_Move is awaited too.
TCFuture<int> fg_AwaitMoved(CReader const &_Reader)
// CHECK-MESSAGES: :[[@LINE-1]]:44: warning: TCFuture coroutine 'fg_AwaitMoved' uses '_Reader' after a suspension, {{.*}}
{
	co_return co_await fg_Move(_Reader.f_Read());
}

// A lambda in a lambda that captures a copy uses the copy.
TCFuture<int> fg_NestedCopy(int &_Value)
{
	auto fOuter = [_Value]
		{
			auto fInner = [&_Value]
				{
					return _Value;
				}
			;
			return fInner();
		}
	;
	co_await fg_Suspend();
	co_return fOuter();
}

// A lambda that is never made uses nothing.
TCFuture<int> fg_NeverMade(int &_Value)
{
	if constexpr (false)
	{
		auto fUse = [&_Value]
			{
				return _Value;
			}
		;
	}
	co_await fg_Suspend();
	co_return 0;
}

// A reference bound to a result of a dependent call.
template <typename t_CData>
TCFuture<int> fg_DependentReference(t_CData const *_pData)
// CHECK-MESSAGES: :[[@LINE-1]]:52: warning: TCFuture coroutine 'fg_DependentReference' uses '_pData' after a suspension, {{.*}}
{
	auto const &Result = _pData->f_Get();
	co_await fg_Suspend();
	co_return Result;
}

struct CGetter
{
	int const &f_Get() const;
};

void fg_InstantiateReference()
{
	CGetter Getter;
	fg_DependentReference(&Getter);
}

// A lambda's copy of a pointer still points to the caller's data.
TCFuture<int> fg_PointerInLambda(int *_pValue)
// CHECK-MESSAGES: :[[@LINE-1]]:39: warning: TCFuture coroutine 'fg_PointerInLambda' uses '_pValue' after a suspension, {{.*}}
{
	co_return co_await fg_CallSafe
		(
			[_pValue]() -> TCFuture<int>
			{
				co_await fg_Suspend();
				co_return *_pValue;
			}
		)
	;
}

// Values: whether a pointer is null, copy assignment, and calls of a function
// reference or a handle.
struct CAssigned
{
	int m_Value = 0;
};

TCFuture<int> fg_Values(int *_pValue, CAssigned const &_Assigned, int (&_fFunction)(), TCActor<> const &_Actor)
{
	bool bPresent = _pValue;
	CAssigned Copy;
	Copy = _Assigned;
	co_await fg_Suspend();
	co_return Copy.m_Value + (bPresent ? 1 : 0) + _fFunction();
}

template <typename t_CActor>
TCFuture<int> fg_DependentHandle(TCActor<t_CActor> const &_Actor)
{
	co_return co_await _Actor.f_Bind([] { return 0; });
}

// A pointer to member gives the data.
TCFuture<int> fg_MemberPointer(CAssigned const &_Assigned, int CAssigned::*_pMember)
// CHECK-MESSAGES: :[[@LINE-1]]:49: warning: TCFuture coroutine 'fg_MemberPointer' uses '_Assigned' after a suspension, {{.*}}
{
	int const &Value = _Assigned.*_pMember;
	co_await fg_Suspend();
	co_return Value;
}

// An awaiter in a comma expression is awaited.
TCFuture<int> fg_AwaitComma(CReader const &_Reader)
// CHECK-MESSAGES: :[[@LINE-1]]:44: warning: TCFuture coroutine 'fg_AwaitComma' uses '_Reader' after a suspension, {{.*}}
{
	co_return co_await (0, _Reader.f_Read());
}

// In a template, a copy into a local of a class type is a value, but a
// reference bound to a conditional isn't.
template <typename t_CType>
struct TCBox
{
	t_CType m_Value;
};

template <typename t_CType>
TCFuture<int> fg_DependentCopy(TCBox<t_CType> const &_Box)
{
	TCBox<t_CType> Copy = _Box;
	co_await fg_Suspend();
	co_return 0;
}

// A scalar initialized from it holds a value.
template <typename t_CType>
TCFuture<int> fg_DependentScalar(t_CType const &_Value)
{
	int Local = _Value;
	co_await fg_Suspend();
	co_return Local;
}

// A scalar from an overloaded member template, and a function that returns a
// TCFuture through a pointer.
struct COverloaded
{
	template <typename t_CType>
	int f_Get() const;
	template <typename t_CType>
	int f_Get(int _Index) const;
};

template <typename t_CType>
TCFuture<int> fg_DependentMember(COverloaded const &_Data)
{
	int Local = _Data.template f_Get<t_CType>();
	co_await fg_Suspend();
	co_return Local;
}

TCFuture<int> fg_IndirectFuture(int const &_Value, TCFuture<int> (*_fCallback)(int const &))
{
	co_return co_await _fCallback(_Value);
}

// In a template, a call of a function template that returns a TCFuture, and an
// `auto` copy.
template <typename t_CType>
TCFuture<int> fg_FutureOf(t_CType const &_Value);

template <typename t_CType>
TCFuture<int> fg_DependentFuture(t_CType const &_Value)
{
	co_return co_await fg_FutureOf(_Value);
}

template <typename t_CType>
TCFuture<int> fg_DependentAutoCopy(TCBox<t_CType> const &_Box)
{
	auto Copy = _Box;
	co_await fg_Suspend();
	co_return 0;
}

template <typename t_CType>
TCFuture<int> fg_DependentConditional(t_CType const &_Value, bool _bFirst)
// CHECK-MESSAGES: :[[@LINE-1]]:54: warning: TCFuture coroutine 'fg_DependentConditional' uses '_Value' after a suspension, {{.*}}
{
	auto const &Alias = _bFirst ? _Value : _Value;
	co_await fg_Suspend();
	co_return 0;
}

void fg_InstantiateConditional()
{
	fg_DependentConditional(1, true);
}

// A lambda's copy of an array owns its elements, but a reference parameter of
// pointer type still points to the caller's data.
TCFuture<int> fg_ArrayCopy(int const (&_Values)[2])
{
	auto fGet = [_Values]
		{
			return _Values[0];
		}
	;
	co_await fg_Suspend();
	co_return fGet();
}

TCFuture<int> fg_PointerReferenceInLambda(int *const &_pValue)
// CHECK-MESSAGES: :[[@LINE-1]]:55: warning: TCFuture coroutine 'fg_PointerReferenceInLambda' uses '_pValue' after a suspension, {{.*}}
{
	co_return co_await fg_CallSafe
		(
			[_pValue]() -> TCFuture<int>
			{
				co_await fg_Suspend();
				co_return *_pValue;
			}
		)
	;
}

// Without a suspension, what `co_return` gives must be a value, also in a
// template, where the result is given to the promise later.
struct CMemberData
{
	int f_Get();
};

template <typename t_CReturn, auto tf_pMemberFunction>
TCFuture<t_CReturn> fg_CallMember(CMemberData *_pData)
{
	return []<typename tf_CData>(tf_CData *_pThis) -> TCFuture<t_CReturn>
		{
			co_return (_pThis->*tf_pMemberFunction)();
		}
		(_pData)
	;
}

TCFuture<int const *> fg_ReturnsAddress(int const &_Value)
// CHECK-MESSAGES: :[[@LINE-1]]:52: warning: TCFuture coroutine 'fg_ReturnsAddress' uses '_Value' after a suspension, {{.*}}
{
	co_return &_Value;
}

// A copy of a template parameter is a value or a view depending on what it is
// instantiated with, and it is reported where it is instantiated with a view.
template <typename t_CType>
TCFuture<int> fg_CopyOfParameter(t_CType const *_pData)
{
	auto Copy = *_pData;
	co_await fg_Suspend();
	co_return sizeof(Copy);
}

template <typename t_CType>
TCFuture<int> fg_CopyOfView(t_CType const *_pData)
// CHECK-MESSAGES: :[[@LINE-1]]:44: warning: TCFuture coroutine 'fg_CopyOfView' uses '_pData' after a suspension, {{.*}}
{
	auto Copy = *_pData;
	co_await fg_Suspend();
	co_return sizeof(Copy);
}

void fg_InstantiateCopies()
{
	int Value = 0;
	CStrSpan Span;
	fg_CopyOfParameter(&Value);
	fg_CopyOfView(&Span);
}

// Through a pointer to a member function that returns a TCFuture, braced and
// `auto` copies in a template, and a handle converted to another.
struct CFutureMember
{
	TCFuture<int> f_Get() const;
};

TCFuture<int> fg_MemberPointerFuture(CFutureMember const &_Data, TCFuture<int> (CFutureMember::*_pGet)() const)
{
	co_return co_await (_Data.*_pGet)();
}

template <typename t_CType>
TCFuture<int> fg_DependentBracedCopy(TCBox<t_CType> const &_Box)
{
	TCBox<t_CType> Copy{_Box};
	auto Other = TCBox<t_CType>{_Box};
	auto Third(_Box);
	co_await fg_Suspend();
	co_return 0;
}

TCFuture<int> fg_ConvertedHandle(TCActor<CMyActor> const &_Actor)
{
	TCActor<> Copy = _Actor;
	co_await fg_Suspend();
	co_return 0;
}

// A call that is resolved later can take it by value, a reference to what can
// be an actor is used like `this`, and a parameter after a pack is found in
// the instantiations.
template <typename t_CType>
void fg_ConsumeByValue(t_CType _Value);

template <typename t_CType>
TCFuture<int> fg_ConsumeDependent(t_CType const &_Value)
{
	fg_ConsumeByValue(_Value);
	co_await fg_Suspend();
	co_return 0;
}

template <typename t_CActor>
TCFuture<int> fg_DependentActor(t_CActor const &_Actor)
{
	co_await fg_Suspend();
	co_return _Actor.m_Value;
}

struct CValueActor : CActor
{
	int m_Value = 0;
};

template <typename t_CType, typename ...tp_CParams>
TCFuture<int> fg_AfterPack(tp_CParams ...p_Params, t_CType const &_Value)
{
	t_CType Copy = _Value;
	co_await fg_Suspend();
	co_return sizeof(Copy);
}

void fg_InstantiateResolvedLater()
{
	fg_ConsumeDependent(1);
	CValueActor Actor;
	fg_DependentActor(Actor);
	fg_AfterPack<int, int, int>(1, 2, 3);
}

// In a template: a member that returns a TCFuture resolved later, a copy into a
// destination that can be anything, a reference pack, a member function of an
// actor through a dependent base, and an address cast to an integer.
struct CFutureGetter
{
	template <typename t_CType>
	TCFuture<t_CType> f_Get() const;

	int m_Value = 0;
};

template <typename t_CType>
TCFuture<int> fg_DependentFutureMember(CFutureGetter const &_Getter)
{
	co_return co_await _Getter.template f_Get<t_CType>();
}

template <typename t_CType>
TCFuture<int> fg_DependentDestination(CFutureGetter const &_Getter)
{
	t_CType Copy = _Getter.m_Value;
	co_await fg_Suspend();
	co_return Copy;
}

template <typename ...tp_CValues>
TCFuture<int> fg_ReferencePack(tp_CValues const &...p_Values)
// CHECK-MESSAGES: :[[@LINE-1]]:53: warning: TCFuture coroutine 'fg_ReferencePack' uses 'p_Values' after a suspension, {{.*}}
{
	co_await fg_Suspend();
	co_return (p_Values + ...);
}

template <typename t_CBase>
struct TCWrapper : t_CBase
{
	TCFuture<int> f_Run(int const &_Value)
// CHECK-MESSAGES: :[[@LINE-1]]:33: warning: TCFuture coroutine 'f_Run' is a member function of actor 'TCWrapper' and takes 'const int &'; {{.*}}
	{
		co_return _Value;
	}
};

template <typename t_CType>
TCFuture<int> fg_AddressAsInteger(t_CType const &_pValue)
// CHECK-MESSAGES: :[[@LINE-1]]:50: warning: TCFuture coroutine 'fg_AddressAsInteger' uses '_pValue' after a suspension, {{.*}}
{
	long Address = reinterpret_cast<long>(_pValue);
	co_await fg_Suspend();
	co_return *reinterpret_cast<int *>(Address);
}

void fg_InstantiateMore()
{
	CFutureGetter Getter;
	fg_DependentFutureMember<int>(Getter);
	fg_DependentDestination<int>(Getter);
	fg_ReferencePack(1, 2);
	TCWrapper<CActor> Wrapper;
	Wrapper.f_Run(1);
	int Value = 0;
	fg_AddressAsInteger(&Value);
}

// A template whose only suspension ends it, a generic lambda in an
// instantiated template, two packs, and a parameter by value of a type that
// can become a pointer.
struct CPointerGuard
{
	CPointerGuard(int *_pValue);
};

template <typename t_CAwaitable>
TCFuture<int> fg_GuardThenAwait(int *_pValue, t_CAwaitable _Awaitable)
// CHECK-MESSAGES: :[[@LINE-1]]:38: warning: TCFuture coroutine 'fg_GuardThenAwait' uses '_pValue' after a suspension, {{.*}}
{
	CPointerGuard Guard(_pValue);
	co_return co_await fg_Move(_Awaitable);
}

template <typename t_CType>
TCFuture<int> fg_OuterTemplate()
{
	int Value = 0;
	co_return co_await [](auto const *_pValue) -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:36: warning: TCFuture coroutine 'operator()' uses '_pValue' after a suspension, {{.*}}
		{
			co_await fg_Suspend();
			co_return *_pValue;
		}
		(&Value)
	;
}

template <typename ...tp_CFirst, typename ...tp_CSecond>
TCFuture<int> fg_TwoPacks(tp_CFirst const &...p_First, tp_CSecond const &...p_Second)
// CHECK-MESSAGES: :[[@LINE-1]]:77: warning: TCFuture coroutine 'fg_TwoPacks' uses 'p_Second' after a suspension, {{.*}}
{
	co_await fg_Suspend();
	co_return (0 + ... + p_Second);
}

template <typename t_CType>
TCFuture<int> fg_ByValue(t_CType _Value)
// CHECK-MESSAGES: :[[@LINE-1]]:34: warning: TCFuture coroutine 'fg_ByValue' uses '_Value' after a suspension, {{.*}}
{
	co_await fg_Suspend();
	co_return *_Value;
}

void fg_InstantiateLast()
{
	int Value = 0;
	fg_GuardThenAwait(&Value, fg_Suspend());
	fg_OuterTemplate<int>();
	fg_TwoPacks(1, 2);
	fg_ByValue(&Value);
}

// A member function template of an actor instantiated with a reference, and a
// fold that computes a value.
struct CTemplateActor : CActor
{
	template <typename t_CType>
	TCFuture<int> f_Take(t_CType _Value)
// CHECK-MESSAGES: :[[@LINE-1]]:31: warning: TCFuture coroutine 'f_Take' is a member function of actor 'CTemplateActor' and takes 'int &'; {{.*}}
	{
		co_return 0;
	}
};

template <int ...tp_Values>
TCFuture<int> fg_Fold(int const &_Base)
{
	int Sum = (_Base + ... + tp_Values);
	co_await fg_Suspend();
	co_return Sum;
}

void fg_InstantiateFinal()
{
	int Value = 0;
	CTemplateActor Actor;
	Actor.f_Take<int &>(Value);
	fg_Fold<1, 2>(Value);
}

// A copy whose template arguments are deduced, and an explicit copy assignment.
template <typename t_CType>
TCFuture<int> fg_DeducedCopy(TCBox<t_CType> const &_Box)
{
	TCBox Copy = _Box;
	co_await fg_Suspend();
	co_return 0;
}

TCFuture<int> fg_ExplicitAssignment(CAssigned const &_Assigned)
{
	CAssigned Copy;
	Copy.operator = (_Assigned);
	co_await fg_Suspend();
	co_return Copy.m_Value;
}

void fg_InstantiateDeduced()
{
	fg_DeducedCopy(TCBox<int>{});
}

// A lambda with a cleanup suspends where it joins the cleanup, when it
// finishes.
TCFuture<int> fg_CleanupSuspends(int const &_Value)
// CHECK-MESSAGES: :[[@LINE-1]]:45: warning: TCFuture coroutine 'fg_CleanupSuspends' uses '_Value' after a suspension, {{.*}}
{
	co_await []() -> TCFuture<int>
		{
			auto Cleanup = co_await fg_AsyncDestroyByValue
				(
					[]() -> TCFuture<int>
					{
						co_await fg_Suspend();
						co_return 0;
					}
				)
			;
			co_return 0;
		}
		()
	;
	co_return _Value;
}

// A result type that depends on template parameters is protected when an
// instantiation's is.
template <ECoroutineFlag t_Flags>
TCFutureWithFlags<int, t_Flags> fg_DependentFlags(int const &_Value)
// CHECK-MESSAGES: :[[@LINE-1]]:62: warning: TCFuture coroutine 'fg_DependentFlags' uses '_Value' after a suspension, {{.*}}
{
	co_await fg_Suspend();
	co_return _Value;
}

void fg_StartDependentFlags(int const &_Value)
{
	fg_DependentFlags<ECoroutineFlag_None>(_Value);
}

// The elements of an array member of the result keep what they point to.
struct CArrayResult
{
	int *m_pValues[1];
};
TCFuture<CArrayResult> fg_ReturnsArray(int &_Value)
// CHECK-MESSAGES: :[[@LINE-1]]:45: warning: TCFuture coroutine 'fg_ReturnsArray' uses '_Value' after a suspension, {{.*}}
{
	co_return CArrayResult{{&_Value}};
}

// A pointer parameter that the body points at its own data reads that after.
TCFuture<int> fg_Redirected(int *_pValue)
{
	int Local = *_pValue;
	_pValue = &Local;
	co_await fg_Suspend();
	co_return *_pValue;
}

// Not when it can point back at the borrowed data.
TCFuture<int> fg_RedirectedConditionally(int *_pValue, bool _bRedirect)
// CHECK-MESSAGES: :[[@LINE-1]]:47: warning: TCFuture coroutine 'fg_RedirectedConditionally' uses '_pValue' after a suspension, {{.*}}
{
	int Local = 1;
	if (_bRedirect)
		_pValue = &Local;
	co_await fg_Suspend();
	co_return *_pValue;
}

// An aggregate made with parentheses keeps what its members refer to, like one
// made with braces.
struct CReferenceResult
{
	int const &m_Value;
};
TCFuture<CReferenceResult> fg_ParenthesizedResult(int const &_Value)
// CHECK-MESSAGES: :[[@LINE-1]]:62: warning: TCFuture coroutine 'fg_ParenthesizedResult' uses '_Value' after a suspension, {{.*}}
{
	co_return CReferenceResult(_Value);
}

// A parameter pointed at a local first borrows nothing.
TCFuture<int> fg_RedirectedAlias(int *_pValue)
{
	int Local = 4;
	_pValue = &Local;
	co_await CThrowingAwaiter{};
	int &Alias = *_pValue;
	co_return Alias;
}

// An awaiter derived from one that never suspends, with an await_ready of its
// own.
struct CSuspendingAgain : CSuspendNever
{
	bool await_ready() const noexcept
	{
		return false;
	}
};
TCFuture<int> fg_DerivedAwaiter(int const &_Value)
// CHECK-MESSAGES: :[[@LINE-1]]:44: warning: TCFuture coroutine 'fg_DerivedAwaiter' uses '_Value' after a suspension, but it takes it as 'const int &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
{
	co_await CSuspendingAgain{};
	co_return _Value;
}

// A result that keeps a pointer cast from the address of a parameter.
struct CCastResult
{
	void const *m_pValue;
};
TCFuture<CCastResult> fg_CastResult(int const &_Value)
// CHECK-MESSAGES: :[[@LINE-1]]:48: warning: TCFuture coroutine 'fg_CastResult' uses '_Value' after a suspension, {{.*}}
{
	co_return CCastResult{static_cast<void const *>(&_Value)};
}

// A pointer parameter pointed at a local and then given to what can point it
// back.
int *g_pSavedValue;
void fg_ResetPointer(int *&_pValue);
TCFuture<int> fg_RedirectedThenReset(int *_pValue)
// CHECK-MESSAGES: :[[@LINE-1]]:43: warning: TCFuture coroutine 'fg_RedirectedThenReset' uses '_pValue' after a suspension, {{.*}}
{
	int Local = 0;
	_pValue = &Local;
	fg_ResetPointer(_pValue);
	co_await CThrowingAwaiter{};
	co_return *_pValue;
}

// All of the returned local assigned what keeps the parameter.
TCFuture<CCastResult> fg_AssignedWhole(int const &_Value)
// CHECK-MESSAGES: :[[@LINE-1]]:51: warning: TCFuture coroutine 'fg_AssignedWhole' uses '_Value' after a suspension, {{.*}}
{
	CCastResult Result{};
	Result = CCastResult{&_Value};
	co_return Result;
}

// An awaiter whose `await_ready` is of a class that suspends, whatever the
// order of its bases, and a result made of a local chosen by a conditional or
// assigned through a reference to it.
struct CRound53Suspends
{
	bool await_ready() const noexcept { return false; }
	void await_suspend(std::coroutine_handle<>) const noexcept {}
};
struct CRound53Mixed : CRound53Suspends, std::suspend_never
{
	using CRound53Suspends::await_ready;
	using CRound53Suspends::await_suspend;
};
TCFuture<int> fg_Round53Mixed(int &_Value)
// CHECK-MESSAGES: :[[@LINE-1]]:36: warning: TCFuture coroutine 'fg_Round53Mixed' uses '_Value' after a suspension, but it takes it as 'int &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
{
	co_await CRound53Mixed{};
	co_return _Value;
}
struct CRound53Result
{
	int const *m_pValue;
};
TCFuture<CRound53Result> fg_Round53Chosen(int const &_Value, bool _bChoose)
// CHECK-MESSAGES: :[[@LINE-1]]:54: warning: TCFuture coroutine 'fg_Round53Chosen' uses '_Value' after a suspension, but it takes it as 'const int &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
{
	CRound53Result Borrowed{&_Value};
	CRound53Result Empty{nullptr};
	co_return _bChoose ? Borrowed : Empty;
}
TCFuture<CRound53Result> fg_Round53ThroughAlias(int const &_Value)
// CHECK-MESSAGES: :[[@LINE-1]]:60: warning: TCFuture coroutine 'fg_Round53ThroughAlias' uses '_Value' after a suspension, but it takes it as 'const int &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
{
	CRound53Result Result{nullptr};
	CRound53Result &Alias = Result;
	Alias.m_pValue = &_Value;
	co_return Result;
}

// A lambda whose only suspension is in a discarded branch of an `if
// constexpr` runs to its end in the call, and awaiting it doesn't suspend.
TCFuture<int> fg_Round60Discarded(int const &_Value)
{
	co_await []() -> TCFuture<int>
		{
			if constexpr (false)
				co_await fg_Suspend();
			co_return 0;
		}
	();
	co_return _Value;
}

// A copy of a borrowed view kept in the result refers to the caller's data.
struct CRound61Result
{
	CStrSpan m_Text;
};
TCFuture<CRound61Result> fg_Round61Result(CStrSpan _Text)
// CHECK-MESSAGES: :[[@LINE-1]]:52: warning: TCFuture coroutine 'fg_Round61Result' uses '_Text' after a suspension, {{.*}}
{
	co_return CRound61Result{_Text};
}

// The object of a member function whose arguments suspend is used where the
// function runs, after them.
struct CRound63Value
{
	int m_Value = 0;
	int f_Read(int _Offset) const
	{
		return m_Value + _Offset;
	}
};
TCFuture<int> fg_Round63Receiver(CRound63Value const &_Value)
// CHECK-MESSAGES: :[[@LINE-1]]:55: warning: TCFuture coroutine 'fg_Round63Receiver' uses '_Value' after a suspension, but it takes it as 'const CRound63Value &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
{
	co_return _Value.f_Read(co_await fg_Suspend());
}

// What an argument by reference refers to is used when the call runs, after
// another argument suspends.
TCFuture<int> fg_Round67Pause();
TCFuture<int> fg_Round67Read(int const &_Value, int _Other)
{
	co_return _Value + _Other;
}
TCFuture<int> fg_Round67Borrowed(int const &_Value)
// CHECK-MESSAGES: :[[@LINE-1]]:45: warning: TCFuture coroutine 'fg_Round67Borrowed' uses '_Value' after a suspension, but it takes it as 'const int &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
{
	co_return co_await fg_Round67Read(_Value, co_await fg_Round67Pause());
}

// A constructor that only copies what its reference parameter refers to into
// a value doesn't keep it, and a `co_return` in a discarded branch of an `if
// constexpr` doesn't return.
struct CRound67Copy
{
	explicit CRound67Copy(int const &_Value)
		: m_Value(_Value)
	{
	}
	int m_Value;
};
TCFuture<CRound67Copy> fg_Round67Copy(int const &_Value)
{
	co_return CRound67Copy(_Value);
}
struct CRound67Ref
{
	int const &m_Value;
};
int g_Round67Global = 0;
TCFuture<CRound67Ref> fg_Round67Discarded(int const &_Value)
{
	if constexpr (false)
		co_return CRound67Ref{_Value};
	co_return CRound67Ref{g_Round67Global};
}

// Also through a pointer to a function.
TCFuture<int> fg_Round69Pointer(int const &_Value, TCFuture<int> (*_fRead)(int const &_Value, int _Other))
// CHECK-MESSAGES: :[[@LINE-1]]:44: warning: TCFuture coroutine 'fg_Round69Pointer' uses '_Value' after a suspension, but it takes it as 'const int &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
{
	co_return co_await _fRead(_Value, co_await fg_Round67Pause());
}

// Lambda coroutines that only forward to others that don't suspend, each
// looked into once, also when called many times.
#define DRound76Ten(d_Call) d_Call d_Call d_Call d_Call d_Call d_Call d_Call d_Call d_Call d_Call
#define DRound76Forty(d_Call) DRound76Ten(d_Call) DRound76Ten(d_Call) DRound76Ten(d_Call) DRound76Ten(d_Call)
TCFuture<int> fg_Round76Repeated(int &_Value)
{
	auto fA0 = []() -> TCFuture<int> { co_return 0; };
	auto fA1 = [&]() -> TCFuture<int> { DRound76Forty(co_await fA0();) co_return 0; };
	auto fA2 = [&]() -> TCFuture<int> { DRound76Forty(co_await fA1();) co_return 0; };
	auto fA3 = [&]() -> TCFuture<int> { DRound76Forty(co_await fA2();) co_return 0; };
	auto fA4 = [&]() -> TCFuture<int> { DRound76Forty(co_await fA3();) co_return 0; };
	co_await fA4();
	co_return _Value;
}
#define DRound76Forward(d_Next) [&]() -> TCFuture<int> { co_return co_await d_Next(); }
TCFuture<int> fg_Round76Forwarding(int &_Value)
{
	auto fA0 = []() -> TCFuture<int> { co_return 0; };
	auto fA1 = DRound76Forward(fA0);
	auto fA2 = DRound76Forward(fA1);
	auto fA3 = DRound76Forward(fA2);
	auto fA4 = DRound76Forward(fA3);
	auto fA5 = DRound76Forward(fA4);
	co_await fA5();
	co_return _Value;
}

// Given through a cast or fg_Move to what runs after a suspension, read as a
// scalar into a pointer result, and nested awaits.
TCFuture<int> fg_Round79Read(int const &_Value, int _Other);
TCFuture<int> fg_Round79Cast(int &_Value)
// CHECK-MESSAGES: :[[@LINE-1]]:35: warning: TCFuture coroutine 'fg_Round79Cast' uses '_Value' after a suspension, but it takes it as 'int &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
{
	co_return co_await fg_Round79Read(static_cast<int const &>(_Value), co_await fg_Suspend());
}
TCFuture<int> fg_Round79Move(int &_Value)
// CHECK-MESSAGES: :[[@LINE-1]]:35: warning: TCFuture coroutine 'fg_Round79Move' uses '_Value' after a suspension, but it takes it as 'int &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
{
	co_return co_await fg_Round79Read(NMib::fg_Move(_Value), co_await fg_Suspend());
}
int g_Round79Values[16];
TCFuture<int *> fg_Round79Choose(int &_Value)
{
	co_return _Value ? g_Round79Values : nullptr;
}
TCFuture<int *> fg_Round79Index(int &_Index)
{
	co_return &g_Round79Values[_Index];
}
#define DRound79Await(d_Value) (co_await fg_Get(d_Value))
#define DRound79Await4(d_Value) DRound79Await(DRound79Await(DRound79Await(DRound79Await(d_Value))))
TCFuture<int> fg_Round79Nested(int &_Value)
{
	co_return DRound79Await4(DRound79Await4(DRound79Await4(_Value)));
}

// Nested awaits are looked at once each, not once per path through the
// operands, which is exponential in their depth.
TCFuture<int> fg_Round82Work(int _Value);
#define DRound82Await(d_Value) co_await fg_Round82Work(d_Value)
#define DRound82Await4(d_Value) DRound82Await(DRound82Await(DRound82Await(DRound82Await(d_Value))))
#define DRound82Await32(d_Value) DRound82Await4(DRound82Await4(DRound82Await4(DRound82Await4(DRound82Await4(DRound82Await4(DRound82Await4(DRound82Await4(d_Value))))))))
template <typename t_CType>
TCFuture<int> fg_Round82Nested(int const &_Value)
// CHECK-MESSAGES: :[[@LINE-1]]:43: warning: TCFuture coroutine 'fg_Round82Nested' uses '_Value' after a suspension, but it takes it as 'const int &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
{
	int Value = DRound82Await32(0);
	co_return _Value + Value;
}
template TCFuture<int> fg_Round82Nested<int>(int const &_Value);

// Without a suspension, only what the result is made from outlives the call.
TCFuture<int *> fg_Round83Read(int &_Value)
{
	int *pValue = &_Value;
	int Copy = *pValue;
	(void)Copy;
	co_return nullptr;
}
TCFuture<int *> fg_Round83Returned(int &_Value)
// CHECK-MESSAGES: :[[@LINE-1]]:41: warning: TCFuture coroutine 'fg_Round83Returned' uses '_Value' after a suspension, but it takes it as 'int &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
{
	int *pValue = &_Value;
	co_return pValue;
}

// Returned through either of a conditional, through many copies, and given to
// what can point it elsewhere through a cast to a reference.
TCFuture<int *> fg_Round84Either(int &_Value, bool _bFirst)
// CHECK-MESSAGES: :[[@LINE-1]]:39: warning: TCFuture coroutine 'fg_Round84Either' uses '_Value' after a suspension, but it takes it as 'int &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
{
	int *pFirst = nullptr;
	int *pSecond = nullptr;
	(_bFirst ? pFirst : pSecond) = &_Value;
	co_return _bFirst ? pFirst : pSecond;
}
TCFuture<int *> fg_Round84Copies(int &_Value)
// CHECK-MESSAGES: :[[@LINE-1]]:39: warning: TCFuture coroutine 'fg_Round84Copies' uses '_Value' after a suspension, but it takes it as 'int &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
{
	int *p0 = &_Value;
	int *p1 = p0;
	int *p2 = p1;
	int *p3 = p2;
	int *p4 = p3;
	int *p5 = p4;
	int *p6 = p5;
	int *p7 = p6;
	int *p8 = p7;
	int *p9 = p8;
	co_return p9;
}
int *g_pRound84Data;
void fg_Round84Reset(int *&o_pValue)
{
	o_pValue = g_pRound84Data;
}
TCFuture<int> fg_Round84Redirected(int *_pValue)
// CHECK-MESSAGES: :[[@LINE-1]]:41: warning: TCFuture coroutine 'fg_Round84Redirected' uses '_pValue' after a suspension, but it takes it as 'int *', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
{
	int Local = 42;
	_pValue = &Local;
	fg_Round84Reset(static_cast<int *&>(_pValue));
	co_await fg_Suspend();
	co_return *_pValue;
}

// What a pointer points to accessed after a suspension in the same
// expression.
TCFuture<int> fg_Round85Subscript(int *_pValue)
// CHECK-MESSAGES: :[[@LINE-1]]:40: warning: TCFuture coroutine 'fg_Round85Subscript' uses '_pValue' after a suspension, but it takes it as 'int *', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
{
	co_return _pValue[(co_await fg_Suspend(), 0)];
}

// A constructor that reads a number in its body keeps nothing of it, but a
// local copy of a view kept in the result refers to the caller's data.
struct CRound86Copy
{
	explicit CRound86Copy(int const &_Input)
	{
		m_Value = _Input;
	}
	int m_Value;
};
TCFuture<CRound86Copy> fg_Round86Copy(int const &_Input)
{
	co_return CRound86Copy(_Input);
}
struct CRound86Text
{
	CStrSpan m_Text;
};
TCFuture<CRound86Text> fg_Round86View(CStrSpan _Text)
// CHECK-MESSAGES: :[[@LINE-1]]:48: warning: TCFuture coroutine 'fg_Round86View' uses '_Text' after a suspension, but it takes it as 'CStrSpan' (aka 'TCStrSpan<char>'), which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
{
	auto Copied = _Text;
	co_return CRound86Text{Copied};
}

// What a pointer points to is used after a suspension in the base of a
// subscript too.
TCFuture<int> fg_Round89Work();
TCFuture<int> fg_Round89Read(int *_pValue)
// CHECK-MESSAGES: :[[@LINE-1]]:35: warning: TCFuture coroutine 'fg_Round89Read' uses '_pValue' after a suspension, but it takes it as 'int *', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
{
	co_return (_pValue + co_await fg_Round89Work())[0];
}

// Overwriting a pointer it is given changes only the pointer.
TCFuture<int> fg_Round91Clear(int *_pValue, bool _bClear)
{
	co_await fg_Suspend();
	if (_bClear)
		_pValue = nullptr;
	co_return 0;
}

// What is assigned through a chain of references to a local, however long.
TCFuture<int const *> fg_Round95Aliases(int const &_Value)
// CHECK-MESSAGES: :[[@LINE-1]]:52: warning: TCFuture coroutine 'fg_Round95Aliases' uses '_Value' after a suspension, but it takes it as 'const int &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
{
	int const *pValue = nullptr;
	auto &pAlias1 = pValue;
	auto &pAlias2 = pAlias1;
	auto &pAlias3 = pAlias2;
	auto &pAlias4 = pAlias3;
	auto &pAlias5 = pAlias4;
	pAlias5 = &_Value;
	co_return pValue;
}

// What a builtin only looks at the constness of, and the arm of
// `__builtin_choose_expr` that isn't chosen, aren't uses.
TCFuture<int> fg_Round96Suspend();

TCFuture<int> fg_Round96Constant(int &o_Value)
{
	co_await fg_Round96Suspend();
	co_return __builtin_constant_p(o_Value);
}

TCFuture<int> fg_Round96Choose(int &o_Value)
{
	co_await fg_Round96Suspend();
	co_return __builtin_choose_expr(true, 0, o_Value);
}

// A template that isn't instantiated isn't checked: what it borrows is told
// by what it is instantiated with.
template <typename t_CChar>
struct TCRound99StrSpan
{
	t_CChar const *m_pData;
};

template <typename tf_CChar>
TCFuture<int> fg_Round99Uninstantiated(TCRound99StrSpan<tf_CChar> const &_Text)
{
	co_await fg_Suspend();
	co_return _Text.m_pData ? 1 : 0;
}

// The operand of `__builtin_dynamic_object_size` is evaluated.
char g_Round100Small[8];
char g_Round100Large[16];

TCFuture<unsigned long> fg_Round100DynamicSize(int *_pValue)
// CHECK-MESSAGES: :[[@LINE-1]]:53: warning: TCFuture coroutine 'fg_Round100DynamicSize' uses '_pValue' after a suspension, but it takes it as 'int *', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
{
	co_await fg_Suspend();
	co_return __builtin_dynamic_object_size(*_pValue ? g_Round100Small : g_Round100Large, 0);
}

// A cast of a pointer accessed after a suspension.
TCFuture<int> fg_Round102Cast(int *_pValue)
// CHECK-MESSAGES: :[[@LINE-1]]:36: warning: TCFuture coroutine 'fg_Round102Cast' uses '_pValue' after a suspension, but it takes it as 'int *', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
{
	co_return static_cast<int *>(_pValue)[co_await fg_Suspend()];
}

// A compound assignment whose right side suspends uses the left after it.
TCFuture<int> fg_Round103Compound(int &_Value)
// CHECK-MESSAGES: :[[@LINE-1]]:40: warning: TCFuture coroutine 'fg_Round103Compound' uses '_Value' after a suspension, but it takes it as 'int &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
{
	_Value += co_await fg_Suspend();
	co_return 0;
}

// An actor call starts the coroutine after the call returns, so pointers and
// views given to a member function of an actor can be gone before it starts,
// unless they point to an actor.
struct CRound105Reader : CActor
{
	TCFuture<int> f_ReadSpan(CStrSpan _Text)
// CHECK-MESSAGES: :[[@LINE-1]]:36: warning: TCFuture coroutine 'f_ReadSpan' is a member function of actor 'CRound105Reader' and takes 'CStrSpan' (aka 'TCStrSpan<char>'), whose data can be gone when an actor call starts the coroutine, after the call returns; take the data by value [malterlib-coroutine-reference-parameter]
	{
		co_return _Text.m_pData[0];
	}

	TCFuture<int> f_Unused(int const *_pValue)
	{
		co_return 0;
	}

	TCFuture<int> f_ReadActor(CRound105Reader *_pOther)
	{
		co_return _pOther == this;
	}
};

// What a pointer points to accessed through a pointer to a member after it
// is awaited.
struct CRound105Data
{
	int m_Value;
};

TCFuture<int CRound105Data::*> fg_Round105Member();

TCFuture<int> fg_Round105ReadMember(CRound105Data *_pData)
// CHECK-MESSAGES: :[[@LINE-1]]:52: warning: TCFuture coroutine 'fg_Round105ReadMember' uses '_pData' after a suspension, but it takes it as 'CRound105Data *', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
{
	co_return _pData->*(co_await fg_Round105Member());
}

// Not a use in a branch that an `if constexpr` discards.
struct CRound106Discarded : CActor
{
	TCFuture<int> f_Run(int *_pValue)
	{
		if constexpr (false)
			*_pValue = 1;
		co_return 0;
	}
};

// A template parameter of a member function of an actor that is instantiated
// with a pointer, also a pack.
struct CRound107Generic : CActor
{
	template <typename tf_CPointer>
	TCFuture<int> f_Read(tf_CPointer _pValue)
// CHECK-MESSAGES: :[[@LINE-1]]:35: warning: TCFuture coroutine 'f_Read' is a member function of actor 'CRound107Generic' and takes 'int *', whose data can be gone when an actor call starts the coroutine, after the call returns; take the data by value [malterlib-coroutine-reference-parameter]
	{
		co_return *_pValue;
	}

	template <typename... tfp_CPointers>
	TCFuture<int> f_ReadAll(tfp_CPointers... p_pValues)
// CHECK-MESSAGES: :[[@LINE-1]]:43: warning: TCFuture coroutine 'f_ReadAll' is a member function of actor 'CRound107Generic' and takes 'int *', whose data can be gone when an actor call starts the coroutine, after the call returns; take the data by value [malterlib-coroutine-reference-parameter]
	{
		co_return (*p_pValues + ...);
	}
};

void fg_Round107Instantiate(CRound107Generic &o_Generic, int *_pValue)
{
	o_Generic.f_Read(_pValue);
	o_Generic.f_ReadAll(_pValue);
}

// A parameter discarded with a cast to void keeps nothing.
TCFuture<int> fg_Round107Discarded(int *_pValue)
{
	static_cast<void>(_pValue);
	co_await fg_Suspend();
	co_return 0;
}

// Nested coroutine lambdas that return what a converting constructor with a
// default argument makes are looked into once.
struct CRound107Result
{
	CRound107Result(int _Value, int _Extra = 0);
};

struct CRound107Task
{
	struct promise_type
	{
		CRound107Task get_return_object();
		std::suspend_never initial_suspend();
		std::suspend_never final_suspend() noexcept;
		void return_value(CRound107Result _Result);
		void unhandled_exception();
	};
};

int fg_Round107Run(CRound107Task _Task);
int fg_Round107Value();

#define DRound107Level(d_Value) fg_Round107Run([]() -> CRound107Task { co_return d_Value; }())
#define DRound107Levels(d_Value) DRound107Level(DRound107Level(DRound107Level(DRound107Level(d_Value))))

TCFuture<int> fg_Round107Nested(int &_Value)
{
	co_return DRound107Levels(DRound107Levels(DRound107Levels(DRound107Levels(DRound107Levels(DRound107Level(DRound107Level(fg_Round107Value()))))))) + _Value;
}

// A lambda whose only await is in a branch that never runs doesn't suspend.
TCFuture<int> fg_Round108NeverAwaits(int &_Value)
{
	auto fImmediate = []() -> TCFuture<int>
		{
			if (false)
				co_await fg_Suspend();
			co_return 1;
		}
	;
	co_await fImmediate();
	co_return _Value;
}

TCFuture<int> fg_Round108AfterBreak(int &_Value, int _Count)
{
	auto fImmediate = [_Count]() -> TCFuture<int>
		{
			for (int iValue = 0; iValue < _Count; ++iValue)
			{
				break;
				co_await fg_Suspend();
			}
			co_return 1;
		}
	;
	co_await fImmediate();
	co_return _Value;
}

// A callable member await_ready is not the trusted suspend_never one.
namespace NRound115CallableReady {
struct CCallableReady : std::suspend_never {
  static inline std::coroutine_handle<> Pending;
  static constexpr auto await_ready =
    []() noexcept { return false; };
  static constexpr auto await_suspend =
    [](auto h) noexcept { Pending = h; };
};
TCFuture<int> f(int &value) {
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: TCFuture coroutine 'f' uses 'value' after a suspension, but it takes it as 'int &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
  co_await CCallableReady{};
  co_return value;
}
}

// A jump into a constant-false branch can reach a suspension in it.
namespace NRound116GotoIntoFalse {
TCFuture<int> fg_Suspend();
TCFuture<int> bad(int &value) {
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: TCFuture coroutine 'bad' uses 'value' after a suspension, but it takes it as 'int &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
  auto work = []() -> TCFuture<int> {
    goto target;
    if (false) {
    target:
      co_await fg_Suspend();
    }
    co_return 0;
  };
  co_await work();
  co_return value;
}
}

// What a builtin only looks at the constness of is not a use.
namespace NRound117Unevaluated {
struct CApp : CActor {
  TCFuture<int> f_Safe(int *p) {
    co_return __builtin_constant_p(p);
  }
};
}

// Round 121: a lambda called explicitly that never suspends, and a parameter
// given back through what it is assigned.
namespace NRound121
{
TCFuture<int> fg_Explicit(int &_Value) {
  auto fReady = []() -> TCFuture<int> { co_return 1; };
  co_await fReady.operator()();
  co_return _Value;
}

TCFuture<int *> fg_Escape(int *_pValue) {
// CHECK-MESSAGES: :[[@LINE-1]]:32: warning: TCFuture coroutine 'fg_Escape' uses '_pValue' after a suspension, but it takes it as 'int *', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
  int *pCopy = nullptr;
  pCopy = _pValue;
  _pValue = pCopy;
  co_return _pValue;
}
}

// Round 122: what the result keeps past an unnamed bit-field, a conditional
// of an alias, a comma and many copies, and not what never runs or a
// constant condition doesn't choose.
namespace NRound122Results
{
void fg_Log();
struct CBitField {
  int : 0;
  int const *m_pValue;
};
struct CResult { int const *m_pValue; };

TCFuture<CBitField> fg_BitField(int &_Value) {
// CHECK-MESSAGES: :[[@LINE-1]]:38: warning: TCFuture coroutine 'fg_BitField' uses '_Value' after a suspension, but it takes it as 'int &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
  co_return CBitField{&_Value};
}

TCFuture<CResult> fg_Conditional(int &_Value, bool _bChoose) {
// CHECK-MESSAGES: :[[@LINE-1]]:39: warning: TCFuture coroutine 'fg_Conditional' uses '_Value' after a suspension, but it takes it as 'int &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
  int const *pFirst = &_Value;
  int const *pSecond = _bChoose ? pFirst : pFirst;
  co_return CResult{pSecond};
}

TCFuture<CResult> fg_Comma(int &_Value) {
// CHECK-MESSAGES: :[[@LINE-1]]:33: warning: TCFuture coroutine 'fg_Comma' uses '_Value' after a suspension, but it takes it as 'int &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
  co_return CResult{(fg_Log(), &_Value)};
}

TCFuture<CResult> fg_Copies(int &_Value) {
// CHECK-MESSAGES: :[[@LINE-1]]:34: warning: TCFuture coroutine 'fg_Copies' uses '_Value' after a suspension, but it takes it as 'int &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
  CResult First{&_Value};
  CResult Second = First;
  CResult Third = Second;
  CResult Fourth = Third;
  CResult Fifth = Fourth;
  co_return Fifth;
}

TCFuture<CResult> fg_NeverRuns(int &_Value) {
  if (false)
    co_return CResult{&_Value};
  co_return CResult{nullptr};
}

TCFuture<CResult> fg_NotChosen(int &_Value) {
  co_return CResult{false ? &_Value : nullptr};
}
}

// Round 123: what a reference member of the result refers to, through a
// conditional, a comma or a cast, the value a constant condition selects, and
// pointers made from conditionals of others, each followed once.
namespace NRound123
{
int g_Value;
struct CResult {
  const int &m_Value;
};
struct CPointerResult {
  int *m_pValue;
};

TCFuture<CResult> fg_Conditional(const int &_Value, bool _bChoose) {
// CHECK-MESSAGES: :[[@LINE-1]]:45: warning: TCFuture coroutine 'fg_Conditional' uses '_Value' after a suspension, but it takes it as 'const int &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
  co_return CResult{_bChoose ? _Value : g_Value};
}

TCFuture<CResult> fg_Comma(const int &_Value) {
// CHECK-MESSAGES: :[[@LINE-1]]:39: warning: TCFuture coroutine 'fg_Comma' uses '_Value' after a suspension, but it takes it as 'const int &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
  co_return CResult{(0, _Value)};
}

TCFuture<CResult> fg_Cast(const int &_Value) {
// CHECK-MESSAGES: :[[@LINE-1]]:38: warning: TCFuture coroutine 'fg_Cast' uses '_Value' after a suspension, but it takes it as 'const int &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
  co_return CResult{static_cast<const int &>(_Value)};
}

TCFuture<CResult> fg_ConstantFalse(const int &_Value) {
  co_return false ? CResult{_Value} : CResult{g_Value};
}

TCFuture<CResult> fg_ConstantTrue(const int &_Value) {
// CHECK-MESSAGES: :[[@LINE-1]]:46: warning: TCFuture coroutine 'fg_ConstantTrue' uses '_Value' after a suspension, but it takes it as 'const int &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
  co_return true ? CResult{_Value} : CResult{g_Value};
}

#define DRound123Link(d_N, d_P) int *p##d_N = _bChoose ? p##d_P : p##d_P;
TCFuture<CPointerResult> fg_Chain(int &_Unused, bool _bChoose) {
  int *p0 = &g_Value;
  DRound123Link(1, 0) DRound123Link(2, 1) DRound123Link(3, 2) DRound123Link(4, 3)
  DRound123Link(5, 4) DRound123Link(6, 5) DRound123Link(7, 6) DRound123Link(8, 7)
  DRound123Link(9, 8) DRound123Link(10, 9) DRound123Link(11, 10) DRound123Link(12, 11)
  DRound123Link(13, 12) DRound123Link(14, 13) DRound123Link(15, 14) DRound123Link(16, 15)
  DRound123Link(17, 16) DRound123Link(18, 17) DRound123Link(19, 18) DRound123Link(20, 19)
  DRound123Link(21, 20) DRound123Link(22, 21) DRound123Link(23, 22) DRound123Link(24, 23)
  DRound123Link(25, 24) DRound123Link(26, 25) DRound123Link(27, 26) DRound123Link(28, 27)
  DRound123Link(29, 28) DRound123Link(30, 29) DRound123Link(31, 30) DRound123Link(32, 31)
  co_return CPointerResult{p32};
}
}

// Round 125: a view taken by value is in the frame, so reading only what is
// its own, like its length, after a suspension is fine, unlike what it views.
namespace NRound125
{
struct CStrSpan {
  char const *m_pData;
  unsigned long m_Length;
  unsigned long f_GetLen() const { return m_Length; }
  char f_GetFirst() const { return m_pData[0]; }
};
TCFuture<int> fg_Work();

TCFuture<unsigned long> fg_Length(CStrSpan _Text) {
  co_await fg_Work();
  co_return _Text.f_GetLen();
}

TCFuture<unsigned long> fg_Member(CStrSpan _Text) {
  co_await fg_Work();
  co_return _Text.m_Length;
}

TCFuture<char> fg_First(CStrSpan _Text) {
// CHECK-MESSAGES: :[[@LINE-1]]:34: warning: TCFuture coroutine 'fg_First' uses '_Text' after a suspension, but it takes it as 'CStrSpan', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
  co_await fg_Work();
  co_return _Text.f_GetFirst();
}
}

// Round 128: an await that a constant condition skips doesn't run, in an arm
// of a conditional or on the right side of `||`.
namespace NRound128
{
TCFuture<int> fg_Work();
struct CValue {
  int f_Read(int _Value) const;
};

TCFuture<int> fg_Unchosen(CValue const &_Value) {
  co_return _Value.f_Read(false ? co_await fg_Work() : 1);
}

TCFuture<int> fg_Skipped(CValue const &_Value) {
  co_return _Value.f_Read(true || (co_await fg_Work(), true));
}

TCFuture<int> fg_Chosen(CValue const &_Value) {
// CHECK-MESSAGES: :[[@LINE-1]]:39: warning: TCFuture coroutine 'fg_Chosen' uses '_Value' after a suspension, but it takes it as 'const CValue &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
  co_return _Value.f_Read(true ? co_await fg_Work() : 1);
}
}

// Round 129: assigning a pointer parameter doesn't use what it was given,
// nor does what reads it after, and a lambda whose branch that runs returns
// doesn't suspend after it.
namespace NRound129
{
TCFuture<int> fg_Work();

struct CApp : CActor {
  int m_Value;

  TCFuture<int> f_Assigned(int *_pValue) {
    _pValue = &m_Value;
    co_await fg_Work();
    co_return *_pValue;
  }

  TCFuture<int> f_Cleared(int *_pValue) {
    _pValue = nullptr;
    co_return 0;
  }
};

template <bool t_bSuspend>
TCFuture<int> fg_Run(int &_Value) {
  auto fReady = []() -> TCFuture<int> {
    if constexpr (!t_bSuspend)
      co_return 0;
    co_await fg_Work();
    co_return 0;
  };
  co_await fReady();
  co_return _Value;
}

TCFuture<int> fg_NeverRuns(int &_Value) {
  auto fReady = []() -> TCFuture<int> {
    while (false)
      co_await fg_Work();
    co_return 0;
  };
  co_await fReady();
  co_return _Value;
}

void fg_Use() {
  int Value = 7;
  fg_Run<false>(Value);
}
}

// Round 130: a pointer cast or `->` after an offset that suspends reads
// after it, a parameter that both branches of an `if` assign before
// doesn't use what it was given, and the body of a loop whose condition is
// false doesn't run.
namespace NRound130
{
TCFuture<int> fg_Offset();
TCFuture<int> fg_Work();
int g_First;
int g_Second;

TCFuture<int> fg_CastRead(int *_pValue) {
// CHECK-MESSAGES: :[[@LINE-1]]:32: warning: TCFuture coroutine 'fg_CastRead' uses '_pValue' after a suspension, but it takes it as 'int *', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
  co_return *static_cast<int *>(_pValue + co_await fg_Offset());
}

TCFuture<int> fg_Branches(int *_pValue, bool _bChoose) {
  if (_bChoose)
    _pValue = &g_First;
  else
    _pValue = &g_Second;
  co_await fg_Work();
  co_return *_pValue;
}

TCFuture<int> fg_ReadInBranch(int *_pValue, bool _bChoose) {
// CHECK-MESSAGES: :[[@LINE-1]]:36: warning: TCFuture coroutine 'fg_ReadInBranch' uses '_pValue' after a suspension, but it takes it as 'int *', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
  if (_bChoose) {
    co_await fg_Work();
    g_First = *_pValue;
    _pValue = &g_First;
  } else
    _pValue = &g_Second;
  co_return *_pValue;
}

TCFuture<int> fg_ForFalse(int &_Value) {
  co_await []() -> TCFuture<int> {
    for (; false;)
      co_await fg_Work();
    co_return 0;
  }();
  co_return _Value;
}
}

// Round 133: what a returned local is made of isn't returned when it is
// assigned something else before.
namespace NRound133
{
TCFuture<int *> fg_Overwritten(int &_Value) {
  int *pResult = &_Value;
  pResult = nullptr;
  co_return pResult;
}
}

// Round 135: a constructor that only reads through a local pointer to what it
// is given keeps nothing.
namespace NRound135
{
struct CResult {
  int m_Value;
  CResult(const int &_Value) {
    const int *pValue = &_Value;
    m_Value = *pValue;
  }
};

TCFuture<CResult> fg_Copy(const int &_Value) { co_return CResult(_Value); }
}

namespace NRound138 {
TCFuture<int> fg_Work();

// An awaiter whose await_ready only returns true doesn't suspend.
struct CReady {
  constexpr bool await_ready() const noexcept { return true; }
  void await_suspend(std::coroutine_handle<>) const noexcept {}
  int await_resume() const noexcept { return 0; }
};

TCFuture<int> fg_Ready(int const &_Value) {
  co_await CReady{};
  co_return _Value;
}

// But one that can return false does.
struct CMaybe {
  bool m_bReady;
  constexpr bool await_ready() const noexcept { return m_bReady; }
  void await_suspend(std::coroutine_handle<>) const noexcept {}
  int await_resume() const noexcept { return 0; }
};

TCFuture<int> fg_Maybe(int const &_Value) {
// CHECK-MESSAGES: :[[@LINE-1]]:35: warning: TCFuture coroutine 'fg_Maybe' uses '_Value' after a suspension, but it takes it as 'const int &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
  co_await CMaybe{false};
  co_return _Value;
}

// A static member named through what is borrowed, `_pState->ms_Value`,
// doesn't use it.
struct CState {
  static int ms_Value;
  static int fs_Get();
  int m_Value;
};

TCFuture<int> fg_Static(CState const *_pState) {
  co_await fg_Work();
  co_return _pState->ms_Value + _pState->fs_Get() + (*_pState).ms_Value;
}

TCFuture<int> fg_Member(CState const *_pState) {
// CHECK-MESSAGES: :[[@LINE-1]]:39: warning: TCFuture coroutine 'fg_Member' uses '_pState' after a suspension, but it takes it as 'const CState *', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
  co_await fg_Work();
  co_return _pState->m_Value;
}
}

namespace NRound140 {
// An await_ready that is virtual, which an override can make suspend.
struct CBase {
  virtual bool await_ready() const noexcept { return true; }
  void await_suspend(std::coroutine_handle<>) const noexcept;
  int await_resume() const noexcept;
};

struct CDerived : CBase {
  bool await_ready() const noexcept override { return false; }
};

TCFuture<int> fg_Virtual(int &_Value) {
// CHECK-MESSAGES: :[[@LINE-1]]:31: warning: TCFuture coroutine 'fg_Virtual' uses '_Value' after a suspension, but it takes it as 'int &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
  CDerived Awaiter;
  CBase &Base = Awaiter;
  co_await Base;
  co_return _Value;
}

// But not one that is final.
struct CFinal final : CBase {
  bool await_ready() const noexcept override { return true; }
};

TCFuture<int> fg_Final(int &_Value) {
  co_await CFinal{};
  co_return _Value;
}
}

namespace NRound141 {
// An assignment that a jump to a label in a block after it skips, `goto Skip;
// ... { Skip: ... }`, doesn't point it elsewhere.
struct CApp : CActor {
  TCFuture<int> f_Skipped(int *_pValue) {
// CHECK-MESSAGES: :[[@LINE-1]]:32: warning: TCFuture coroutine 'f_Skipped' is a member function of actor 'CApp' and takes 'int *', whose data can be gone when an actor call starts the coroutine, after the call returns; take the data by value [malterlib-coroutine-reference-parameter]
    int Local = 0;
    goto Skip;
    _pValue = &Local;
    {
    Skip:
      co_return *_pValue;
    }
  }
};

// A lambda called where it is made, which isn't a coroutine, runs there.
TCFuture<int> fg_Immediate(int const &_Value) {
  co_return [&_Value] { return _Value; }();
}

TCFuture<int> fg_ImmediateAfter(int const &_Value) {
// CHECK-MESSAGES: :[[@LINE-1]]:44: warning: TCFuture coroutine 'fg_ImmediateAfter' uses '_Value' after a suspension, but it takes it as 'const int &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
  co_await fg_Suspend();
  co_return [&_Value] { return _Value; }();
}

// The object of a call through what a pointer to it points to, `(*_pValue)
// .f_Read(...)`, is used where the call runs.
struct CValue {
  int m_Value;
  int f_Read(int _Add) const { return m_Value + _Add; }
};

TCFuture<int> fg_Dereferenced(CValue const *_pValue) {
// CHECK-MESSAGES: :[[@LINE-1]]:45: warning: TCFuture coroutine 'fg_Dereferenced' uses '_pValue' after a suspension, but it takes it as 'const CValue *', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
  co_return (*_pValue).f_Read(co_await fg_Suspend());
}

// A constructor that reads a value through a pointer it is given, `m_Value =
// *_pValue`, keeps nothing.
struct CResult {
  int m_Value;
  CResult(int const *_pValue) { m_Value = *_pValue; }
};

TCFuture<CResult> fg_Result(int const *_pValue) {
  co_return CResult(_pValue);
}

// The value of a statement expression, `({ &_Value; })`, refers where it does.
TCFuture<int> fg_Statement(int &_Value) {
// CHECK-MESSAGES: :[[@LINE-1]]:33: warning: TCFuture coroutine 'fg_Statement' uses '_Value' after a suspension, but it takes it as 'int &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
  int *pValue = ({ &_Value; });
  co_await fg_Suspend();
  co_return *pValue;
}
}

namespace NRound142 {
// What a pointer to a local pointer points to, `co_return *ppValue`, is what
// that pointer does.
TCFuture<int *> fg_PointerToPointer(int &_Value) {
// CHECK-MESSAGES: :[[@LINE-1]]:42: warning: TCFuture coroutine 'fg_PointerToPointer' uses '_Value' after a suspension, but it takes it as 'int &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
  int *pValue = &_Value;
  int **ppValue = &pValue;
  co_return *ppValue;
}

// A capture of a lambda called where it is made, `[&_Value] { return _Value;
// }()`, is gone after the call.
TCFuture<int> fg_ImmediateCapture(int const &_Value) {
  int Copy = [&_Value] { return _Value; }();
  co_await fg_Suspend();
  co_return Copy;
}

// An awaiter whose await_suspend only returns false resumes right away.
struct CInline {
  bool await_ready() const noexcept { return false; }
  bool await_suspend(std::coroutine_handle<>) const noexcept { return false; }
  int await_resume() const noexcept { return 0; }
};

TCFuture<int> fg_Inline(int const &_Value) {
  co_await CInline{};
  co_return _Value;
}

// But not one that can return true.
struct CMaybeSuspend {
  bool m_bSuspend;
  bool await_ready() const noexcept { return false; }
  bool await_suspend(std::coroutine_handle<>) const noexcept { return m_bSuspend; }
  int await_resume() const noexcept { return 0; }
};

TCFuture<int> fg_MaybeSuspend(int const &_Value) {
// CHECK-MESSAGES: :[[@LINE-1]]:42: warning: TCFuture coroutine 'fg_MaybeSuspend' uses '_Value' after a suspension, but it takes it as 'const int &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
  co_await CMaybeSuspend{true};
  co_return _Value;
}
}

namespace NRound143 {
// A constructor that keeps it only in a branch a constant condition never
// takes.
template <bool t_bKeep>
struct TCResult {
  int m_Value;
  const int *m_pValue = nullptr;
  TCResult(const int &_Value) {
    if (t_bKeep)
      m_pValue = &_Value;
    m_Value = _Value;
  }
};

TCFuture<TCResult<false>> fg_NotKept(const int &_Value) {
  co_return TCResult<false>{_Value};
}

TCFuture<TCResult<true>> fg_Kept(const int &_Value) {
// CHECK-MESSAGES: :[[@LINE-1]]:45: warning: TCFuture coroutine 'fg_Kept' uses '_Value' after a suspension, but it takes it as 'const int &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
  co_return TCResult<true>{_Value};
}
}

namespace NRound144 {
// A base given it whose constructor only copies it.
struct CBase {
  int m_Value;
  CBase(int const &_Value) : m_Value(_Value) {}
};
struct CValue : CBase {
  CValue(int const &_Value) : CBase(_Value) {}
};

TCFuture<CValue> fg_Copied(int const &_Value) { co_return CValue(_Value); }

// But one whose constructor keeps its address.
struct CPointerBase {
  int const *m_pValue;
  CPointerBase(int const &_Value) : m_pValue(&_Value) {}
};
struct CPointer : CPointerBase {
  CPointer(int const &_Value) : CPointerBase(_Value) {}
};

TCFuture<CPointer> fg_Kept(int const &_Value) { co_return CPointer(_Value); }
// CHECK-MESSAGES: :[[@LINE-1]]:39: warning: TCFuture coroutine 'fg_Kept' uses '_Value' after a suspension, but it takes it as 'const int &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
}

namespace NRound145 {
// An awaiter whose static await_ready only returns true.
struct CStatic {
  static bool await_ready() noexcept { return true; }
  static void await_suspend(std::coroutine_handle<>) noexcept {}
  int await_resume() noexcept { return 0; }
};

TCFuture<int> fg_Static(int const &_Value) {
  co_await CStatic{};
  co_return _Value;
}
}

namespace NRound147 {
CAwaiter fg_Suspend();

struct CActorHolder : CActor
{
	// Only the arm `__builtin_choose_expr` chooses is evaluated.
	TCFuture<int> f_Chosen(int *_pValue)
	{
		co_return __builtin_choose_expr(1, 0, *_pValue);
	}
};

// A lambda called right away through its call operator by name, or by
// `std::invoke`, runs before the suspension.
TCFuture<int> fg_Named(int &_Value)
{
	int Copy = [&] { return _Value; }.operator()();
	co_await fg_Suspend();
	co_return Copy;
}

// Nested coroutine lambdas that return through a promise with a default
// argument are looked at in time linear in their depth.
struct CTask
{
	struct promise_type
	{
		CTask get_return_object();
		std::suspend_never initial_suspend();
		std::suspend_never final_suspend() noexcept;
		void return_value(int _Value, int _Extra = 0);
		void unhandled_exception();
	};
};
int fg_Run(CTask _Task);
#define DRound147L(x) fg_Run([]() -> CTask { co_return x; }())
#define DRound147L4(x) DRound147L(DRound147L(DRound147L(DRound147L(x))))
#define DRound147L8(x) DRound147L4(DRound147L4(x))

TCFuture<int> fg_Nested(int _Value)
{
	co_return DRound147L8(DRound147L8(DRound147L8(DRound147L8(0)))) + _Value;
}
}

namespace NRound148 {
CAwaiter fg_Suspend();

// An explicit capture of a lambda called right away through its call operator
// by name, or by `std::invoke`.
TCFuture<int> fg_Named(int &_Value)
{
	int Copy = [&_Value] { return _Value; }.operator()();
	co_await fg_Suspend();
	co_return Copy;
}

// What a lambda called right away returns is the result.
TCFuture<int *> fg_Returned(int &_Value)
// CHECK-MESSAGES: :[[@LINE-1]]:34: warning: TCFuture coroutine 'fg_Returned' uses '_Value' after a suspension, but it takes it as 'int &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
{
	co_return [&_Value] { return &_Value; }();
}

struct CStrSpan
{
	const char *m_pData;
	int m_Length;
};

// A view's own scalar, updated after a suspension.
TCFuture<int> fg_Updated(CStrSpan _Text)
{
	co_await fg_Suspend();
	_Text.m_Length = 0;
	++_Text.m_Length;
	co_return _Text.m_Length;
}
}

namespace NRound149 {
// An awaiter of a final class whose `await_ready` is always ready.
struct CReadyBase
{
	virtual bool await_ready() const noexcept
	{
		return true;
	}
	void await_suspend(std::coroutine_handle<> _Handle) const noexcept {}
	void await_resume() const noexcept {}
};
struct CReadyFinal final : CReadyBase
{
};

TCFuture<int> fg_Final(int &_Value)
{
	co_await CReadyFinal{};
	co_return _Value;
}

// A result whose value is replaced before it is returned.
struct CResult
{
	int const *m_pValue;
};

TCFuture<CResult> fg_Replaced(int const &_Value)
{
	CResult Result{&_Value};
	Result = CResult{nullptr};
	co_return Result;
}
}

namespace NRound150 {
TCFuture<int> fg_Work();

// A lambda called right away runs after what it is given, which suspends.
TCFuture<int> fg_AfterArgument(int &_Value)
// CHECK-MESSAGES: :[[@LINE-1]]:37: warning: TCFuture coroutine 'fg_AfterArgument' uses '_Value' after a suspension, but it takes it as 'int &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
{
	co_return [&_Value](int _Unused) { return _Value; }(co_await fg_Work());
}

// An awaiter of a final class whose `await_suspend` resumes right away.
struct CBaseAwaiter
{
	bool await_ready() const noexcept
	{
		return false;
	}
	virtual bool await_suspend(std::coroutine_handle<> _Handle) noexcept
	{
		return false;
	}
	int await_resume() const noexcept
	{
		return 0;
	}
};
struct CFinalAwaiter final : CBaseAwaiter
{
};

TCFuture<int> fg_FinalSuspend(int &_Value)
{
	co_await CFinalAwaiter{};
	co_return _Value;
}
}

namespace NRound151 {
void fg_Prepare();

// An `fg_AsyncDestroy` awaited after a comma doesn't suspend.
TCFuture<int> fg_Comma(int &_Value)
{
	auto fCleanup = []() -> TCFuture<int> { co_return 0; };
	auto Guard = co_await (fg_Prepare(), fg_AsyncDestroy(fCleanup));
	co_return _Value;
}

// Pointed at static storage in the branch of an `if constexpr` it takes.
int g_Global;

template <bool t_bReset>
TCFuture<int> fg_Reset(int *_pValue)
{
	if constexpr (t_bReset)
		_pValue = &g_Global;
	co_await CAwaiter{};
	co_return *_pValue;
}

void fg_UseReset()
{
	fg_Reset<true>(nullptr);
}
}

namespace NRound152 {
TCFuture<int> fg_Suspend();

// An argument that a constant condition doesn't make suspend.
TCFuture<int> fg_NotSuspending(int &_Value)
{
	int Copy = [&_Value](int _Unused) { return _Value; }(false ? co_await fg_Suspend() : 0);
	co_await fg_Suspend();
	co_return Copy;
}
}

namespace NRound153 {
TCFuture<int> fg_Work();

// A reference to the own scalar member of a view.
struct CIoSpan {
  void const *m_pData;
  unsigned long m_nBytes;
};
TCFuture<unsigned long> fg_OwnMember(CIoSpan _Span) {
  auto &Bytes = _Span.m_nBytes;
  co_await CAwaiter{};
  co_return Bytes;
}

// A suspension that a short circuit skips.
TCFuture<int> fg_ShortCircuit(int &_Value) {
  int Copy = [&_Value](bool) { return _Value; }(true || (co_await CAwaiter{}, false));
  co_await CAwaiter{};
  co_return Copy;
}

// An overwrite through a cast to a reference.
TCFuture<int> fg_CastOverwrite(int *_pValue) {
  co_await CAwaiter{};
  static_cast<int *&>(_pValue) = nullptr;
  co_return 0;
}

// A virtual getter that isn't final can read what the view refers to.
struct CBase {
  int m_Length;
  virtual int f_Get() const { return m_Length; }
};
struct CLeft : virtual CBase {};
struct CRight : virtual CBase {
  int *m_pData;
  int f_Get() const override { return *m_pData; }
};
struct CMyStrSpan : CLeft, CRight {
  using CLeft::f_Get;
};
TCFuture<int> fg_VirtualGetter(CMyStrSpan _View) {
// CHECK-MESSAGES: :[[@LINE-1]]:43: warning: TCFuture coroutine 'fg_VirtualGetter' uses '_View' after a suspension, but it takes it as 'CMyStrSpan', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
  co_await fg_Work();
  co_return _View.f_Get();
}

// Many copies of a view.
struct CStrSpan {
  int *m_pData;
};
struct CResult {
  CStrSpan m_Span;
};
TCFuture<CResult> fg_ManyCopies(CStrSpan _Span) {
// CHECK-MESSAGES: :[[@LINE-1]]:42: warning: TCFuture coroutine 'fg_ManyCopies' uses '_Span' after a suspension, but it takes it as 'CStrSpan', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
  auto A = _Span, B = A, C = B, D = C, E = D, F = E, G = F, H = G, I = H;
  co_return CResult{I};
}

// A local lambda that is only called, before any suspension.
TCFuture<int> fg_LocalLambda(int &_Value) {
  auto fGet = [&_Value] { return _Value; };
  co_return fGet();
}
}

namespace NRound154 {
// Returned through a lambda held in a local that is only called.
TCFuture<int const *> fg_NamedHelper(int const &_Value) {
// CHECK-MESSAGES: :[[@LINE-1]]:49: warning: TCFuture coroutine 'fg_NamedHelper' uses '_Value' after a suspension, but it takes it as 'const int &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
  auto fGet = [&] { return &_Value; };
  co_return fGet();
}

// Only read through a chain of local references by a constructor.
struct CResult {
  int m_Value;
  CResult(int const &_Input) {
    int const &First = _Input;
    int const &Second = First;
    m_Value = Second;
  }
};
TCFuture<CResult> fg_Aliases(int const &_Input) { co_return CResult(_Input); }
}

namespace NRound156 {
TCFuture<int> fg_Suspend();
// A pointer compared with null reads only itself, which is in the frame.
TCFuture<int> fg_IsNull(int *_pValue) {
  co_await fg_Suspend();
  co_return _pValue == nullptr;
}
TCFuture<int> fg_Tested(int *_pValue) {
  co_await fg_Suspend();
  co_return !_pValue ? 0 : 1;
}
// What is read through it still uses what it points to.
TCFuture<int> fg_Read(int *_pValue) {
// CHECK-MESSAGES: :[[@LINE-1]]:28: warning: TCFuture coroutine 'fg_Read' uses '_pValue' after a suspension, but it takes it as 'int *', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
  co_await fg_Suspend();
  co_return _pValue ? *_pValue : 0;
}

// What an initializer kept, which the body resets first.
struct CReset {
  int const *m_pValue;
  CReset(int const &_Value) : m_pValue(&_Value) { m_pValue = nullptr; }
};
TCFuture<CReset> fg_Reset(int const &_Value) { co_return CReset(_Value); }
}

namespace NRound157 {
// Copied into an array of numbers, which keeps nothing.
struct CCopy {
  int m_Values[1];
  explicit CCopy(int const &_Value) : m_Values{_Value} {}
};
TCFuture<CCopy> fg_Copy(int const &_Value) { co_return CCopy(_Value); }

// Returned through a cast to a reference to it.
struct CKeep {
  int const &m_Value;
  explicit CKeep(int const &_Value) : m_Value(_Value) {}
};
TCFuture<CKeep> fg_Cast(int const &_Value) {
// CHECK-MESSAGES: :[[@LINE-1]]:36: warning: TCFuture coroutine 'fg_Cast' uses '_Value' after a suspension, but it takes it as 'const int &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
  CKeep Result(_Value);
  co_return static_cast<CKeep &&>(Result);
}

// Assigned what it returns.
TCFuture<int const *> fg_Overwritten(int const &_Value) {
  int const *pValue = &_Value;
  co_return (pValue = nullptr);
}

// A static pointer a getter returns can point into what it is called on.
struct CState {
  int m_Value;
  int *f_Get() {
    static int *s_pCached;
    s_pCached = &m_Value;
    return s_pCached;
  }
};
TCFuture<int> fg_Static(CState &_State) {
// CHECK-MESSAGES: :[[@LINE-1]]:33: warning: TCFuture coroutine 'fg_Static' uses '_State' after a suspension, but it takes it as 'CState &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
  int *pValue = _State.f_Get();
  co_await CAwaiter{};
  co_return *pValue;
}
}

namespace NRound158 {
int g_Values[64];
TCFuture<int> fg_Suspend();
// A large other value of a conditional.
TCFuture<int *> fg_Large(int &_Value, bool _bChoose) {
// CHECK-MESSAGES: :[[@LINE-1]]:31: warning: TCFuture coroutine 'fg_Large' uses '_Value' after a suspension, but it takes it as 'int &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
  int *pValue = &_Value;
  int *pResult = _bChoose ? pValue : g_Values + 1 + 2 + 3 + 4 + 5 + 6 + 7;
  co_return pResult;
}
// A lambda whose awaits can't run never suspends.
TCFuture<int> fg_NeverSuspends(int &_Value) {
  co_await []() -> TCFuture<int> { co_return false ? co_await fg_Suspend() : 0; }();
  co_await []() -> TCFuture<int> { co_return false && (co_await fg_Suspend(), true); }();
  co_return _Value;
}
// What a local is assigned before it is returned.
TCFuture<int *> fg_Reassigned(int &_Value) {
  int *pValue = &_Value;
  pValue = g_Values;
  co_return pValue + 1;
}
TCFuture<int *> fg_Comma(int &_Value) {
  int *pValue = &_Value;
  co_return (pValue = nullptr, pValue);
}
}

namespace NRound159 {
// Many reads after it is pointed at a member.
#define DRound159Eight(d_Code) d_Code d_Code d_Code d_Code d_Code d_Code d_Code d_Code
struct CApp : CActor {
  int m_Value;
  TCFuture<int> f_Reads(int *_pValue) {
    _pValue = &m_Value;
    int Sum = 0;
    DRound159Eight(DRound159Eight(DRound159Eight(Sum += *_pValue;)))
    co_return Sum;
  }
};
}
namespace std {
template <class t_FFunction> auto invoke(t_FFunction &_fFunction) -> decltype(_fFunction()) { return _fFunction(); }
}
namespace NRound159 {
// A lambda that never suspends, called through std::invoke.
TCFuture<int> fg_Invoked(int &_Value) {
  auto fReady = []() -> TCFuture<int> { co_return 1; };
  co_await std::invoke(fReady);
  co_return _Value;
}
}

namespace NRound161 {
// Discarded after a suspension, it isn't read.
TCFuture<int> fg_Discarded(int *_pValue, int &_Value) {
  co_await CAwaiter{};
  (void)_pValue;
  (void)_Value;
  co_return 0;
}
// A long chain of casts to a reference to it.
struct CResult {
  int const &m_Value;
};
#define DRound161Ref(d_Value) static_cast<int const &>(d_Value)
#define DRound161Ref8(d_Value) DRound161Ref(DRound161Ref(DRound161Ref(DRound161Ref(DRound161Ref(DRound161Ref(DRound161Ref(DRound161Ref(d_Value))))))))
#define DRound161Ref64(d_Value) DRound161Ref8(DRound161Ref8(DRound161Ref8(DRound161Ref8(DRound161Ref8(DRound161Ref8(DRound161Ref8(DRound161Ref8(d_Value))))))))
TCFuture<CResult> fg_Chain(int const &_Value) { co_return CResult{DRound161Ref64(_Value)}; }
// CHECK-MESSAGES: :[[@LINE-1]]:39: warning: TCFuture coroutine 'fg_Chain' uses '_Value' after a suspension, but it takes it as 'const int &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
}

namespace NRound162 {
// Uses that never run.
struct CApp : CActor {
  TCFuture<int> f_Run(int *_pValue) {
    while (false) {
      int Value = *_pValue;
    }
    for (; false;) {
      int Value = *_pValue;
    }
    co_return 0;
    int Value = *_pValue;
  }
};
}

namespace NRound164 {
int g_Value;
struct CObject {
  virtual int *f_Get() const final { return &g_Value; }
};
struct CBase {
  virtual int *f_Get() const { return &g_Value; }
};
// A getter that gives only what has static storage, that can't be
// overridden or is called by its name.
TCFuture<int *> fg_Final(CObject &_Object) { co_return _Object.f_Get(); }
TCFuture<int *> fg_Qualified(CBase &_Object) {
  co_return _Object.CBase::f_Get();
}
TCFuture<int *> fg_Virtual(CBase &_Object) { co_return _Object.f_Get(); }
// CHECK-MESSAGES: :[[@LINE-1]]:35: warning: TCFuture coroutine 'fg_Virtual' uses '_Object' after a suspension, but it takes it as 'CBase &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
// Many reference parameters of a coroutine with a scalar result are analyzed
// in linear time.
template <typename t_CType, t_CType... tp_Values> struct TCSequence {};
template <typename... tfp_CType>
TCFuture<int> fg_First(tfp_CType const &...p_Values) {
  int Copies[] = {int(p_Values)...};
  co_return Copies[0];
}
template <unsigned... tfp_Values>
void fg_Instantiate(TCSequence<unsigned, tfp_Values...>) {
  (void)fg_First(tfp_Values...);
}
void fg_Test() {
  fg_Instantiate(__make_integer_seq<TCSequence, unsigned, 3000>{});
}
}

namespace NRound166 {
int g_Value;
TCFuture<int> fg_Work();
// Not a pointer assigned something else after it, before the co_return.
TCFuture<int *> fg_Overwritten(int &_Value) {
  int *pResult = nullptr;
  pResult = &_Value;
  pResult = &g_Value;
  co_return pResult;
}
// Not a pointer parameter pointed elsewhere before its use, also before a
// comma, or in a block.
TCFuture<int> fg_Comma(int *_pValue) {
  co_await fg_Work();
  co_return (_pValue = &g_Value, *_pValue);
}
TCFuture<int> fg_Block(int *_pValue) {
  {
    _pValue = &g_Value;
  }
  co_await fg_Work();
  co_return *_pValue;
}
// Not a lambda that suspends only in what isn't evaluated.
TCFuture<int> fg_Unevaluated(int &_Value) {
  co_await []() -> TCFuture<int> {
    using CGuard = decltype(
        fg_AsyncDestroy([]() -> TCFuture<int> { co_return 0; }));
    co_return 0;
  }();
  co_return _Value;
}
// Not an assignment to a returned member that can't run before it.
struct CResult {
  int *m_pValue;
};
TCFuture<CResult> fg_NeverTaken(int &_Value) {
  CResult Result{nullptr};
  if (false)
    Result.m_pValue = &_Value;
  co_return Result;
}
TCFuture<CResult> fg_After(int &_Value) {
  CResult Result{nullptr};
  co_return Result;
  Result.m_pValue = &_Value;
}
}

namespace NRound169 {
int g_Value;
// Not a getter that returns a local made from what is static.
struct CData {
  int const *f_Get() const {
    int const *pValue = &g_Value;
    return pValue;
  }
};
TCFuture<int const *> fg_Static(CData const &_Data) { co_return _Data.f_Get(); }
}

namespace NRound170 {
// Not a member of the result assigned something else after.
struct CResult {
  int const *m_pValue;
};
TCFuture<CResult> fg_Overwritten(int const &_Value) {
  CResult Result{};
  Result.m_pValue = &_Value;
  Result.m_pValue = nullptr;
  co_return Result;
}
}

namespace NRound174 {
void fg_Set(int *&o_pValue, int &_Value) { o_pValue = &_Value; }
// Given back to the pointer after it is cleared.
TCFuture<int *> fg_SetAgain(int &_Value) {
// CHECK-MESSAGES: :[[@LINE-1]]:34: warning: TCFuture coroutine 'fg_SetAgain' uses '_Value' after a suspension, but it takes it as 'int &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
  int *pValue = &_Value;
  pValue = nullptr;
  fg_Set(pValue, _Value);
  co_return pValue;
}
// Cleared in both branches.
TCFuture<int *> fg_ClearedBoth(int &_Value, bool _bChoose) {
  int *pValue = &_Value;
  if (_bChoose)
    pValue = nullptr;
  else
    pValue = nullptr;
  co_return pValue;
}
// Kept in one of them.
TCFuture<int *> fg_KeptInOne(int &_Value, bool _bChoose) {
// CHECK-MESSAGES: :[[@LINE-1]]:35: warning: TCFuture coroutine 'fg_KeptInOne' uses '_Value' after a suspension, but it takes it as 'int &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
  int *pValue = nullptr;
  if (_bChoose)
    pValue = nullptr;
  else
    pValue = &_Value;
  co_return pValue;
}
int g_First, g_Second;
// A pointer that only chooses what it points to after.
TCFuture<int> fg_Chooses(int *_pValue) {
  _pValue = _pValue ? &g_First : &g_Second;
  co_await fg_Suspend();
  co_return *_pValue;
}
// But not one that is one of the values.
TCFuture<int> fg_ChoosesItself(int *_pValue, bool _bChoose) {
// CHECK-MESSAGES: :[[@LINE-1]]:37: warning: TCFuture coroutine 'fg_ChoosesItself' uses '_pValue' after a suspension, but it takes it as 'int *', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
  _pValue = _bChoose ? _pValue : &g_Second;
  co_await fg_Suspend();
  co_return *_pValue;
}
}

namespace NRound175 {
// A reference given back by a helper, kept in the result.
template <typename t_CType>
constexpr t_CType const &fg_Const(t_CType const &_Value) noexcept {
  return (t_CType const &)_Value;
}
struct CResult {
  int const &m_Value;
};
TCFuture<CResult> fg_Read(int const &_Value) { co_return CResult{fg_Const(_Value)}; }
// CHECK-MESSAGES: :[[@LINE-1]]:38: warning: TCFuture coroutine 'fg_Read' uses '_Value' after a suspension, but it takes it as 'const int &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
// Not a copy of what holds only values.
struct CValue {
  int m_Number;
};
struct COwning {
  CValue m_Value;
  COwning(CValue const &_Value) : m_Value(_Value) {}
};
TCFuture<COwning> fg_Copy(CValue const &_Value) { co_return COwning{_Value}; }
// But a copy of what refers to it.
struct CView {
  int const *m_pNumber;
};
struct CViewing {
  CView m_View;
  CViewing(CView const &_View) : m_View(_View) {}
};
TCFuture<CViewing> fg_CopyView(CView const &_View) { co_return CViewing{_View}; }
// CHECK-MESSAGES: :[[@LINE-1]]:45: warning: TCFuture coroutine 'fg_CopyView' uses '_View' after a suspension, but it takes it as 'const CView &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
}

namespace NRound179 {
// A result that holds a copy of a value of many members of the same types,
// each told once.
template <int t_Level> struct TCWide {
  TCWide<t_Level - 1> m_A, m_B, m_C, m_D, m_E, m_F, m_G, m_H;
};
template <> struct TCWide<0> { int m_Value; };
struct CWideResult {
  TCWide<9> m_Value;
  CWideResult(TCWide<9> const &_Value) : m_Value(_Value) {}
};
TCFuture<CWideResult> fg_Wide(TCWide<9> const &_Value) { co_return CWideResult(_Value); }
// And of a deep one.
template <int t_Level> struct TCDeep {
  TCDeep<t_Level - 1> m_Value;
};
template <> struct TCDeep<0> { int m_Value; };
struct CDeepResult {
  TCDeep<40> m_Value;
  CDeepResult(TCDeep<40> const &_Value) : m_Value(_Value) {}
};
TCFuture<CDeepResult> fg_Deep(TCDeep<40> const &_Value) { co_return CDeepResult(_Value); }
}

namespace NRound180 {
struct CRef {
  int const &m_Value;
};
// A reference member bound to what a local pointer to the parameter points
// to.
TCFuture<CRef> fg_Return(int const &_Value) {
// CHECK-MESSAGES: :[[@LINE-1]]:37: warning: TCFuture coroutine 'fg_Return' uses '_Value' after a suspension, but it takes it as 'const int &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
  auto *pValue = &_Value;
  co_return CRef{*pValue};
}
}

namespace NRound181 {
// A constructor that reads a member of what it is given as a value.
struct CData {
  int m_Value;
};
struct CResult {
  int m_Value;
  CResult(CData const &_Data) { m_Value = _Data.m_Value; }
};
TCFuture<CResult> fg_Copy(CData const &_Data) { co_return CResult(_Data); }
}

namespace NRound183 {
// A view type matched without printing the long name of what it is in.
template <typename t_CFirst, typename t_CSecond>
struct TCPair {
  struct CNested {};
};
template <int t_Level>
struct TCChain {
  using CType = TCPair<typename TCChain<t_Level - 1>::CType,
                       typename TCChain<t_Level - 1>::CType>;
};
template <> struct TCChain<0> {
  using CType = int;
};
TCFuture<int> fg_Probe(TCChain<24>::CType::CNested _Unused) { co_return 0; }
}

namespace NRound189 {
// A result that refers to what a pointer it is given points to, moved.
struct CResult {
  int const &m_Value;
  CResult(int const &_Value) : m_Value(_Value) {}
};
TCFuture<CResult> fg_Select(int const *_pValues) {
// CHECK-MESSAGES: :[[@LINE-1]]:40: warning: TCFuture coroutine 'fg_Select' uses '_pValues' after a suspension, but it takes it as 'const int *', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
  co_return CResult{*(_pValues + 1)};
}
}

namespace NRound194 {
// A local pointer in a returned aggregate holds what it is assigned last.
struct CResult {
  int const *m_pValue;
};
TCFuture<CResult> fg_Cleared(int const &_Value) {
  int const *pValue = &_Value;
  pValue = nullptr;
  co_return CResult{pValue};
}
TCFuture<CResult> fg_Kept(int const &_Value) {
// CHECK-MESSAGES: :[[@LINE-1]]:38: warning: TCFuture coroutine 'fg_Kept' uses '_Value' after a suspension, but it takes it as 'const int &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
  int const *pValue = nullptr;
  pValue = &_Value;
  co_return CResult{pValue};
}
// A view whose alias deduction drops, `CStrPtr`, told by its traits.
struct CStrTraitsPtr_CStr {};
template <typename t_CTraits>
struct TCStr {
  char const *m_pData;
  char const *f_GetStr() const { return m_pData; }
};
using CStrPtr = TCStr<CStrTraitsPtr_CStr>;
TCFuture<int> fg_Work();
template <typename tf_CText>
TCFuture<int> fg_Read(tf_CText _Text) {
// CHECK-MESSAGES: :[[@LINE-1]]:32: warning: TCFuture coroutine 'fg_Read' uses '_Text' after a suspension, but it takes it as 'NRound194::TCStr<NRound194::CStrTraitsPtr_CStr>', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
  co_await fg_Work();
  co_return *_Text.f_GetStr();
}
void fg_Start() {
  char Text[] = "name";
  fg_Read(CStrPtr{Text});
}
}

namespace NRound203 {
// What a local is assigned before a copy of it is returned, not what it was
// made with, also when another copy is.
TCFuture<int *> fg_Cleared(int &_Value) {
  int *pValue = &_Value;
  pValue = nullptr;
  int *pResult = pValue;
  co_return pResult;
}
TCFuture<int *> fg_Kept(int &_Value) {
// CHECK-MESSAGES: :[[@LINE-1]]:30: warning: TCFuture coroutine 'fg_Kept' uses '_Value' after a suspension, but it takes it as 'int &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
  int *pValue = &_Value;
  int *pResult = pValue;
  co_return pResult;
}
}

namespace NRound206 {
TCFuture<int> fg_Work();
// A number read through a conditional is copied.
int g_Default;
struct CResult {
  int m_Value;
  CResult(int const &_Value, bool _bUseValue) {
    m_Value = _bUseValue ? _Value : g_Default;
  }
};
TCFuture<CResult> fg_Copied(int const &_Value, bool _bUseValue) {
  co_return CResult(_Value, _bUseValue);
}
// A pointer given by value tested after a suspension reads only itself.
TCFuture<int> fg_Tested(int *_pValue) {
  co_await fg_Work();
  if (_pValue)
    co_return 1;
  co_return _pValue ? 2 : 0;
}
}

namespace NRound212 {
TCFuture<int> fg_Work();
// A lambda whose cleanup of fg_AsyncDestroy is cancelled before it can run
// doesn't suspend.
TCFuture<int> fg_Cancelled(int const &_Value) {
  auto fReady = []() -> TCFuture<int> {
    auto Cleanup = co_await fg_AsyncDestroy([]() -> TCFuture<int> { co_return 0; });
    Cleanup.f_Clear();
    co_return 1;
  };
  int n = co_await fg_CallSafe(fReady);
  co_return _Value + n;
}
// The type is named as it is written, not as the alias it is of, which can be
// exponentially longer.
template <class t_CKey, class t_CValue>
struct TCMap {
  int f_GetLen() const;
};
template <int t_Level>
struct TCNested {
  using CPrevious = typename TCNested<t_Level - 1>::CType;
  using CType = TCMap<CPrevious, CPrevious>;
};
template <>
struct TCNested<0> {
  using CType = int;
};
using CValues = TCNested<16>::CType;
TCFuture<int> fg_Read(CValues &_Values) {
// CHECK-MESSAGES: :[[@LINE-1]]:32: warning: TCFuture coroutine 'fg_Read' uses '_Values' after a suspension, but it takes it as 'CValues &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
  co_await fg_Work();
  co_return _Values.f_GetLen();
}
}

namespace NRound218 {
// A lambda that captures the pointer by reference reads it where it is
// called, after it is pointed elsewhere.
struct CApp : CActor {
  int m_Value = 42;
  TCFuture<int> f_ReadLater(int *_pValue) {
    auto fRead = [&_pValue] { return *_pValue; };
    _pValue = &m_Value;
    co_await CAwaiter{};
    co_return fRead();
  }
  TCFuture<int> f_ReadBefore(int *_pValue) {
// CHECK-MESSAGES: :[[@LINE-1]]:35: warning: TCFuture coroutine 'f_ReadBefore' is a member function of actor 'CApp' and takes 'int *', whose data can be gone when an actor call starts the coroutine, after the call returns; take the data by value [malterlib-coroutine-reference-parameter]
    auto fRead = [&_pValue] { return *_pValue; };
    int Value = fRead();
    _pValue = &m_Value;
    co_await CAwaiter{};
    co_return Value + fRead();
  }
  TCFuture<int> f_ReadCopy(int *_pValue) {
// CHECK-MESSAGES: :[[@LINE-1]]:33: warning: TCFuture coroutine 'f_ReadCopy' is a member function of actor 'CApp' and takes 'int *', whose data can be gone when an actor call starts the coroutine, after the call returns; take the data by value [malterlib-coroutine-reference-parameter]
    auto fRead = [_pValue] { return *_pValue; };
    _pValue = &m_Value;
    co_await CAwaiter{};
    co_return fRead();
  }
};
TCFuture<int> fg_ReadLocal(int *_pValue) {
  int Local = *_pValue;
  auto fRead = [&_pValue] { return *_pValue; };
  _pValue = &Local;
  co_await CAwaiter{};
  co_return fRead();
}
}

namespace NRound218Free {
// Called before it is pointed elsewhere, or never pointed elsewhere, the
// lambda reads what the caller owns.
TCFuture<int> fg_ReadCalledBefore(int *_pValue) {
// CHECK-MESSAGES: :[[@LINE-1]]:40: warning: TCFuture coroutine 'fg_ReadCalledBefore' uses '_pValue' after a suspension, but it takes it as 'int *', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
  int Local = *_pValue;
  auto fRead = [&_pValue] { return *_pValue; };
  co_await CAwaiter{};
  int Value = fRead();
  _pValue = &Local;
  co_return Value;
}
TCFuture<int> fg_ReadNotRedirected(int *_pValue) {
// CHECK-MESSAGES: :[[@LINE-1]]:41: warning: TCFuture coroutine 'fg_ReadNotRedirected' uses '_pValue' after a suspension, but it takes it as 'int *', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
  auto fRead = [&_pValue] { return *_pValue; };
  co_await CAwaiter{};
  co_return fRead();
}
}

namespace NRound219 {
TCFuture<int> fg_Work();
// Pointed elsewhere through a local reference to it, or a cast to one.
TCFuture<int> fg_ThroughAlias(int const *_pValue) {
  int Local = 42;
  auto &pCursor = _pValue;
  pCursor = &Local;
  co_await fg_Work();
  co_return *_pValue;
}
TCFuture<int> fg_ThroughCast(int const *_pValue) {
  int Local = 42;
  static_cast<int const *&>(_pValue) = &Local;
  co_await fg_Work();
  co_return *_pValue;
}
// Not one that the alias points elsewhere only on some paths.
TCFuture<int> fg_SomePaths(int const *_pValue, bool _bRedirect) {
// CHECK-MESSAGES: :[[@LINE-1]]:39: warning: TCFuture coroutine 'fg_SomePaths' uses '_pValue' after a suspension, but it takes it as 'const int *', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
  int Local = 42;
  auto &pCursor = _pValue;
  if (_bRedirect)
    pCursor = &Local;
  co_await fg_Work();
  co_return *_pValue;
}
// Nor one an alias points back after.
TCFuture<int> fg_PointedBack(int const *_pValue, int const *_pOther) {
// CHECK-MESSAGES: :[[@LINE-1]]:41: warning: TCFuture coroutine 'fg_PointedBack' uses '_pValue' after a suspension, but it takes it as 'const int *', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
// CHECK-MESSAGES: :[[@LINE-2]]:61: warning: TCFuture coroutine 'fg_PointedBack' uses '_pOther' after a suspension, but it takes it as 'const int *', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
  int Local = 42;
  auto &pCursor = _pValue;
  pCursor = &Local;
  if (_pOther)
    pCursor = _pOther;
  co_await fg_Work();
  co_return *_pValue;
}
}

namespace NRound219Read {
TCFuture<int> fg_Work();
// Read through the alias, it uses what the caller owns.
TCFuture<int> fg_ReadAlias(int const *_pValue) {
// CHECK-MESSAGES: :[[@LINE-1]]:39: warning: TCFuture coroutine 'fg_ReadAlias' uses '_pValue' after a suspension, but it takes it as 'const int *', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
  auto &pCursor = _pValue;
  co_await fg_Work();
  co_return *pCursor;
}
}

namespace NRound220 {
struct CEntry {
  int m_Value;
};
CEntry g_First, g_Second;
TCFuture<int> fg_Work();
// What the value of a redirection reads, before it assigns, is of what the
// caller owns.
TCFuture<int> fg_ReadInRedirect(CEntry const *_pEntry) {
// CHECK-MESSAGES: :[[@LINE-1]]:47: warning: TCFuture coroutine 'fg_ReadInRedirect' uses '_pEntry' after a suspension, but it takes it as 'const CEntry *', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
  co_await fg_Work();
  _pEntry = _pEntry->m_Value ? &g_First : &g_Second;
  co_return _pEntry->m_Value;
}
// Not what it reads after.
TCFuture<int> fg_ReadAfterRedirect(CEntry const *_pEntry, bool _bFirst) {
  co_await fg_Work();
  _pEntry = _bFirst ? &g_First : &g_Second;
  co_return _pEntry->m_Value;
}
// A coroutine called directly that never suspends gives a ready future.
TCFuture<int> fg_Ready() { co_return 0; }
TCFuture<int> fg_AfterReady(int const &_Value) {
  co_await fg_Ready();
  co_return _Value;
}
}

namespace NRound221 {
TCFuture<int> fg_Work();
enum EMode { EMode_First, EMode_Second };
// Each case of a switch points it elsewhere, or leaves.
TCFuture<int> fg_Switched(int const *_pValue, EMode _Mode) {
  int First = 1, Second = 2;
  switch (_Mode) {
  case EMode_First:
    _pValue = &First;
    break;
  case EMode_Second:
    _pValue = &Second;
    break;
  default:
    co_return 0;
  }
  co_await fg_Work();
  co_return *_pValue;
}
// Not one a case leaves as it is.
TCFuture<int> fg_SwitchedPartly(int const *_pValue, EMode _Mode, bool _bSkip) {
// CHECK-MESSAGES: :[[@LINE-1]]:44: warning: TCFuture coroutine 'fg_SwitchedPartly' uses '_pValue' after a suspension, but it takes it as 'const int *', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
  int First = 1, Second = 2;
  switch (_Mode) {
  case EMode_First:
    if (_bSkip)
      break;
    _pValue = &First;
    break;
  default:
    _pValue = &Second;
    break;
  }
  co_await fg_Work();
  co_return *_pValue;
}
// Nor without a default.
TCFuture<int> fg_SwitchedNoDefault(int const *_pValue, EMode _Mode) {
// CHECK-MESSAGES: :[[@LINE-1]]:47: warning: TCFuture coroutine 'fg_SwitchedNoDefault' uses '_pValue' after a suspension, but it takes it as 'const int *', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
  int First = 1, Second = 2;
  switch (_Mode) {
  case EMode_First:
    _pValue = &First;
    break;
  case EMode_Second:
    _pValue = &Second;
    break;
  }
  co_await fg_Work();
  co_return *_pValue;
}
}

namespace NRound223 {
TCFuture<int> fg_Work();
// Testing a pointer explicitly reads only the pointer.
TCFuture<bool> fg_HasValue(int const *_pValue) {
  co_await fg_Work();
  co_return bool(_pValue);
}
TCFuture<bool> fg_HasValueCast(int const *_pValue) {
  co_await fg_Work();
  co_return static_cast<bool>(_pValue);
}
// Comparing the pointers of views reads only what is in the frame.
struct CStrSpan {
  char const *m_pStr;
  char const *f_GetStr() const { return m_pStr; }
};
TCFuture<bool> fg_SameBuffer(CStrSpan _First, CStrSpan _Second) {
  co_await CAwaiter{};
  co_return _First.f_GetStr() == _Second.f_GetStr();
}
TCFuture<bool> fg_SameBufferMember(CStrSpan _First, CStrSpan _Second) {
  co_await CAwaiter{};
  co_return _First.m_pStr == _Second.m_pStr;
}
// Not what it points to.
TCFuture<char> fg_FirstChar(CStrSpan _Text) {
// CHECK-MESSAGES: :[[@LINE-1]]:38: warning: TCFuture coroutine 'fg_FirstChar' uses '_Text' after a suspension, but it takes it as 'CStrSpan', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
  co_await CAwaiter{};
  co_return *_Text.f_GetStr();
}
}

namespace NRound227 {
// What an assignment of a pointer in the result's initializer assigns is
// what the result holds, not what it assigns over.
struct CResult {
  int *m_pValue;
};
int g_Value;

TCFuture<CResult> fg_Assigned(int &_Value) {
// CHECK-MESSAGES: :[[@LINE-1]]:36: warning: TCFuture coroutine 'fg_Assigned' uses '_Value' after a suspension, but it takes it as 'int &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
  int *pValue = nullptr;
  co_return CResult{pValue = &_Value};
}

TCFuture<CResult> fg_AssignedOver(int &_Value) {
  int *pValue = &_Value;
  co_return CResult{pValue = &g_Value};
}
}

namespace NRound229 {
// A pointer parameter that the branch that suspends points elsewhere is
// redirected after it, when each suspension is in that branch; not when one
// is elsewhere.
TCFuture<int> fg_Read(CStr const *_pText, bool _bAsync) {
  CStr Copy;
  if (_bAsync) {
    Copy = *_pText;
    _pText = &Copy;
    co_await fg_Suspend();
  }
  co_return _pText->m_Length;
}
TCFuture<int> fg_ReadLater(CStr const *_pText, bool _bAsync) {
// CHECK-MESSAGES: :[[@LINE-1]]:40: warning: TCFuture coroutine 'fg_ReadLater' uses '_pText' after a suspension, but it takes it as 'const CStr *', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
  CStr Copy;
  if (_bAsync) {
    Copy = *_pText;
    _pText = &Copy;
    co_await fg_Suspend();
  }
  co_await fg_Suspend();
  co_return _pText->m_Length;
}
}

namespace NRound230 {
// Binding a reference to a pointer parameter doesn't read what it points to,
// nor does reading what it points to after that is pointed elsewhere.
struct CApp : CActor {
  int m_Value = 7;
  TCFuture<int> f_Read(int *_pValue) {
    auto &pCursor = _pValue;
    pCursor = &m_Value;
    co_return *pCursor;
  }
  TCFuture<int> f_ReadFirst(int *_pValue) {
// CHECK-MESSAGES: :[[@LINE-1]]:34: warning: TCFuture coroutine 'f_ReadFirst' is a member function of actor 'CApp' and takes 'int *', whose data can be gone when an actor call starts the coroutine, after the call returns; take the data by value [malterlib-coroutine-reference-parameter]
    auto &pCursor = _pValue;
    int Value = *pCursor;
    pCursor = &m_Value;
    co_return Value + *pCursor;
  }
};
}

namespace NRound235 {
struct CValue {
  int m_Value;
};
struct CMap {
  CValue *f_FindEqual(int _Key);
};
struct CMyActor : CActor {
  CMap mp_Items;
  // Replaced in what an `if` starts with, before it reads it.
  TCFuture<int> f_Process(CValue *_pValue) {
    if (_pValue = mp_Items.f_FindEqual(1); _pValue)
      co_return _pValue->m_Value;
    co_return 0;
  }
  // Not one that reads it first.
  TCFuture<int> f_Read(CValue *_pValue) {
// CHECK-MESSAGES: :[[@LINE-1]]:32: warning: TCFuture coroutine 'f_Read' is a member function of actor 'CMyActor' and takes 'CValue *', whose data can be gone when an actor call starts the coroutine, after the call returns; take the data by value [malterlib-coroutine-reference-parameter]
    int Value = _pValue->m_Value;
    if (_pValue = mp_Items.f_FindEqual(1); _pValue)
      co_return _pValue->m_Value + Value;
    co_return 0;
  }
};
}

namespace NRound239 {
int g_Value;
TCFuture<int> fg_Work();
// Pointed elsewhere in the branch that suspends, besides an await that doesn't
// suspend.
TCFuture<int> fg_Read(int const *_pValue, bool _bWait) {
  co_await ECoroutineFlag_CaptureExceptions;
  if (_bWait) {
    _pValue = &g_Value;
    co_await fg_Work();
  }
  co_return *_pValue;
}
}

namespace NRound241 {
struct CStr {
  int f_GetLen() const;
};
TCFuture<int> fg_Work();
// Read through a local reference that pointed it at a local first.
TCFuture<int> fg_ReadAlias(CStr *_pText) {
  CStr Local;
  auto &pText = _pText;
  pText = &Local;
  co_await fg_Work();
  co_return pText->f_GetLen();
}
struct CStrSpan {
  char const *m_pStr;
  unsigned m_Length;
};
// What the result keeps of what it is given, moved, assigned or set by a
// helper.
TCFuture<CStrSpan> fg_Moved(char const *_pText) {
// CHECK-MESSAGES: :[[@LINE-1]]:41: warning: TCFuture coroutine 'fg_Moved' uses '_pText' after a suspension, but it takes it as 'const char *', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
  CStrSpan First{_pText, 1};
  CStrSpan Result = fg_Move(First);
  co_return Result;
}
TCFuture<CStrSpan> fg_Assigned(CStrSpan _Text) {
// CHECK-MESSAGES: :[[@LINE-1]]:41: warning: TCFuture coroutine 'fg_Assigned' uses '_Text' after a suspension, but it takes it as 'CStrSpan', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
  CStrSpan Result{};
  Result = _Text;
  co_return Result;
}
void fg_SetResult(char const *&o_pResult, char const *_pText) {
  o_pResult = _pText;
}
TCFuture<char const *> fg_Set(char const *_pText) {
// CHECK-MESSAGES: :[[@LINE-1]]:43: warning: TCFuture coroutine 'fg_Set' uses '_pText' after a suspension, but it takes it as 'const char *', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
  char const *pResult = nullptr;
  fg_SetResult(pResult, _pText);
  co_return pResult;
}
// Types whose printed forms are too large are printed without their parts.
template <int t_N>
struct TCSignature {
  using CPrevious = typename TCSignature<t_N - 1>::CType;
  using CType = TCFunction<CPrevious(CPrevious)>;
};
template <>
struct TCSignature<0> {
  using CType = int;
};
using CCallback = typename TCSignature<24>::CType;
template <typename tf_CType>
int fg_ReadValue(tf_CType const &_Value);
template <typename tf_CType>
TCFuture<int> fg_Read(tf_CType const &_Callback) {
// CHECK-MESSAGES: :[[@LINE-1]]:39: warning: TCFuture coroutine 'fg_Read' uses '_Callback' after a suspension, but it takes it as 'const NMib::NFunction::TCFunction<...>', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
  co_await fg_Work();
  co_return fg_ReadValue(_Callback);
}
void fg_Use(CCallback const &_Callback) { fg_Read<CCallback>(_Callback); }
// Also those of a class nested in a specialization of a template whose
// arguments are too large, which are left out too.
template <typename t_CFirst, typename t_CSecond> struct TCPair {};
template <int t_N> struct TCBuild {
  using CType = TCPair<typename TCBuild<t_N - 1>::CType,
                       typename TCBuild<t_N - 1>::CType>;
};
template <> struct TCBuild<0> {
  using CType = int;
};
template <typename t_CType> struct TCBox {
  struct CValue {
    int m_Value;
  };
};
template <typename tf_CBox>
TCFuture<int> fg_ReadNested(typename tf_CBox::CValue const &_Value) {
// CHECK-MESSAGES: :[[@LINE-1]]:61: warning: TCFuture coroutine 'fg_ReadNested' uses '_Value' after a suspension, but it takes it as 'const {{[A-Za-z0-9_:]*}}TCBox<...>::CValue', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
  co_await fg_Work();
  co_return _Value.m_Value;
}
using CBox = TCBox<TCBuild<24>::CType>;
void fg_UseNested(CBox::CValue const &_Value) {
  fg_ReadNested<CBox>(_Value);
}
}

// A coroutine whose body can be seen not to suspend is ready also when its
// future allows references.
TCUnsafeFuture<int> fg_Round243Ready()
{
	co_return 1;
}

TCFuture<int> fg_Round243Read(int const &_Value)
{
	co_await fg_Round243Ready();
	co_return _Value;
}

// A helper whose cleanup of fg_AsyncDestroy can be seen not to suspend is
// ready, but not one whose cleanup suspends.
TCFuture<int> fg_Round245Prepare()
{
	auto Cleanup = co_await fg_AsyncDestroy
		(
			[]() -> TCFuture<int>
			{
				co_return 0;
			}
		)
	;
	co_return 0;
}

TCFuture<int> fg_Round245Run(int const &_Value)
{
	co_await fg_Round245Prepare();
	co_return _Value;
}

TCFuture<int> fg_Round245Suspend();

TCFuture<int> fg_Round245PrepareSuspending()
{
	auto Cleanup = co_await fg_AsyncDestroy
		(
			[]() -> TCFuture<int>
			{
				co_await fg_Round245Suspend();
				co_return 0;
			}
		)
	;
	co_return 0;
}

TCFuture<int> fg_Round245RunSuspending(int const &_Value)
// CHECK-MESSAGES: :[[@LINE-1]]:51: warning: TCFuture coroutine 'fg_Round245RunSuspending' uses '_Value' after a suspension, but it takes it as 'const int &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
{
	co_await fg_Round245PrepareSuspending();
	co_return _Value;
}

namespace NRound343PointerChain {
// What a chain of conditionals of local pointers to a pointer points to,
// `int **ppNext = bFirst ? ppPrevious : ppPrevious`, is looked at once for
// each link, not once for each path, whose number doubles with each.
TCFuture<int> fg_Chain(int *_pValue, bool _bFirst) {
// CHECK-MESSAGES: :[[@LINE-1]]:29: warning: TCFuture coroutine 'fg_Chain' uses '_pValue' after a suspension, but it takes it as 'int *', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
  int *pValue = _pValue;
  int **ppValue0 = &pValue;
  int **ppValue1 = _bFirst ? ppValue0 : ppValue0;
  int **ppValue2 = _bFirst ? ppValue1 : ppValue1;
  int **ppValue3 = _bFirst ? ppValue2 : ppValue2;
  int **ppValue4 = _bFirst ? &*ppValue3 : &*ppValue3;
  int **ppValue5 = _bFirst ? ppValue4 : ppValue4;
  int **ppValue6 = _bFirst ? ppValue5 : ppValue5;
  int **ppValue7 = _bFirst ? ppValue6 : ppValue6;
  int **ppValue8 = _bFirst ? &*ppValue7 : &*ppValue7;
  int **ppValue9 = _bFirst ? ppValue8 : ppValue8;
  int **ppValue10 = _bFirst ? ppValue9 : ppValue9;
  int **ppValue11 = _bFirst ? ppValue10 : ppValue10;
  int **ppValue12 = _bFirst ? &*ppValue11 : &*ppValue11;
  int **ppValue13 = _bFirst ? ppValue12 : ppValue12;
  int **ppValue14 = _bFirst ? ppValue13 : ppValue13;
  int **ppValue15 = _bFirst ? ppValue14 : ppValue14;
  int **ppValue16 = _bFirst ? &*ppValue15 : &*ppValue15;
  int **ppValue17 = _bFirst ? ppValue16 : ppValue16;
  int **ppValue18 = _bFirst ? ppValue17 : ppValue17;
  int **ppValue19 = _bFirst ? ppValue18 : ppValue18;
  int **ppValue20 = _bFirst ? &*ppValue19 : &*ppValue19;
  int **ppValue21 = _bFirst ? ppValue20 : ppValue20;
  int **ppValue22 = _bFirst ? ppValue21 : ppValue21;
  int **ppValue23 = _bFirst ? ppValue22 : ppValue22;
  int **ppValue24 = _bFirst ? &*ppValue23 : &*ppValue23;
  int **ppValue25 = _bFirst ? ppValue24 : ppValue24;
  int **ppValue26 = _bFirst ? ppValue25 : ppValue25;
  int **ppValue27 = _bFirst ? ppValue26 : ppValue26;
  int **ppValue28 = _bFirst ? &*ppValue27 : &*ppValue27;
  int **ppValue29 = _bFirst ? ppValue28 : ppValue28;
  int **ppValue30 = _bFirst ? ppValue29 : ppValue29;
  int **ppValue31 = _bFirst ? ppValue30 : ppValue30;
  int **ppValue32 = _bFirst ? &*ppValue31 : &*ppValue31;
  int **ppValue33 = _bFirst ? ppValue32 : ppValue32;
  int **ppValue34 = _bFirst ? ppValue33 : ppValue33;
  int **ppValue35 = _bFirst ? ppValue34 : ppValue34;
  int **ppValue36 = _bFirst ? &*ppValue35 : &*ppValue35;
  int **ppValue37 = _bFirst ? ppValue36 : ppValue36;
  int **ppValue38 = _bFirst ? ppValue37 : ppValue37;
  int **ppValue39 = _bFirst ? ppValue38 : ppValue38;
  int **ppValue40 = _bFirst ? &*ppValue39 : &*ppValue39;
  *ppValue40 = nullptr;
  co_await fg_Suspend();
  co_return pValue ? *pValue : 0;
}
}
