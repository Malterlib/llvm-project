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

// An awaiter that keeps a pointer to what made it, the reader of f_Read.
struct CBorrowingAwaiter
{
	bool await_ready() const noexcept;
	void await_suspend(std::coroutine_handle<>) const noexcept;
	int await_resume() const noexcept;
	void const *m_pReader = nullptr;
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

// Before the first suspension a parameter may only be used as a value: a
// pointer to it is followed where it still points to it.
TCFuture<int> fg_Repointed(CStr const &_Value)
// Clean: pLength is pointed at the local Local before it is dereferenced after the suspension.
// Clean: a pointer to it is followed only where it still points to it, as of a pointer parameter.
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
// An integer made of the address is the address: converted back and read, it is used.
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

// A copy constructor's other arguments are not copied: one it states it
// keeps (DMibKeepsArgument) is kept by the copy, which its destructor uses;
// one it doesn't state is kept by nothing, the class has no member that can
// hold it.
struct CCopied
{
	CCopied(CCopied const &_Other, DMibKeepsArgument int const *_pExtra = nullptr);
	~CCopied();
};

struct CCopiedUnstated
{
	CCopiedUnstated(CCopiedUnstated const &_Other, int const *_pExtra = nullptr);
	~CCopiedUnstated();
};

TCFuture<int> fg_CopyExtraUnstated(CCopiedUnstated const &_Copied, int const &_Extra)
{
	CCopiedUnstated Copy(_Copied, &_Extra);
	co_await fg_Suspend();
	co_return 0;
}

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
// Clean: Alias is never used after the suspension.
// Clean: a reference bound to it that is never used does nothing with it, as a pointer to it.
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
// An integer made of the address is the address: converted back and read, it is used.
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
	CPointerGuard(DMibKeepsArgument int *_pValue);
	~CPointerGuard();
};

struct CPointerGuardUnstated
{
	CPointerGuardUnstated(int *_pValue);
	~CPointerGuardUnstated();
};

template <typename t_CAwaitable>
TCFuture<int> fg_GuardUnstatedThenAwait(int *_pValue, t_CAwaitable _Awaitable)
{
	CPointerGuardUnstated Guard(_pValue);
	co_return co_await fg_Move(_Awaitable);
}

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
	fg_GuardUnstatedThenAwait(&Value, fg_Suspend());
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

// Nor after a statement that always returns in the instantiation, `if
// constexpr (!t_bEnabled) co_return 0;` of `t_bEnabled` false, which nothing
// after runs.
struct CRound150EarlyReturn : CActor
{
	template <bool t_bEnabled>
	TCFuture<int> f_Read(int const *_pValue)
	{
		if constexpr (!t_bEnabled)
			co_return 0;
		co_return *_pValue;
	}
};
void fg_Round150EarlyReturn(CRound150EarlyReturn &_Actor)
{
	_Actor.f_Read<false>(nullptr);
}

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
// One an alias points at another parameter after refers to what that one
// does, not to what it was given.
TCFuture<int> fg_PointedBack(int const *_pValue, int const *_pOther) {
// CHECK-MESSAGES: :[[@LINE-1]]:61: warning: TCFuture coroutine 'fg_PointedBack' uses '_pOther' after a suspension, but it takes it as 'const int *', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
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
// Nor without a default, of a switch that has a case for each enumerator:
// a value no enumerator names leaves it as it is.
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
// CHECK-MESSAGES: :[[@LINE-1]]:39: warning: TCFuture coroutine 'fg_Read' uses '_Callback' after a suspension, but it takes it as 'const NMib::NFunction::TCFunction<...> &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
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
// CHECK-MESSAGES: :[[@LINE-1]]:61: warning: TCFuture coroutine 'fg_ReadNested' uses '_Value' after a suspension, but it takes it as 'const {{[A-Za-z0-9_:]*}}TCBox<...>::CValue &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
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

// A scalar member of a view taken by value is in the frame, also given by
// reference after a suspension, to a function or an operator, `CFormat("{}")
// << _Span.m_nBytes`; what the view refers to isn't.
namespace NRound129
{
	using umint = decltype(sizeof(0));
	struct CIoSpan
	{
		void const *m_pData;
		umint m_nBytes;
	};
	template <typename t_CValue>
	struct TCFormat
	{
		TCFormat &operator << (t_CValue const &_Value);
	};
	void fg_LogBytes(umint const &_nBytes);

	TCFuture<int> fg_LogRead(CIoSpan _Span)
	{
		co_await fg_Suspend();
		fg_LogBytes(_Span.m_nBytes);
		TCFormat<umint>{} << _Span.m_nBytes;
		co_return 0;
	}

	TCFuture<int> fg_LogPointee(CIoSpan _Span)
// CHECK-MESSAGES: :[[@LINE-1]]:38: warning: TCFuture coroutine 'fg_LogPointee' uses '_Span' after a suspension, {{.*}}
	{
		co_await fg_Suspend();
		co_return *static_cast<char const *>(_Span.m_pData);
	}
}

// A pointer a getter of a view taken by value gives, also one it forwards from
// the getter of a base, `return CImp::f_GetStr();` of `TCStr`, compared after
// a suspension, is in the frame; what it points to isn't.
namespace NReview2Round130
{
	struct CStrTraitsPtr_UTF8;
	template <typename t_CTraits>
	struct TCStrImp_Ptr
	{
		using CChar = char;
		CChar const *f_GetStr() const
		{
			return m_pData;
		}
		CChar *m_pData;
	};
	struct CStrTraitsPtr_UTF8
	{
		using CImp = TCStrImp_Ptr<CStrTraitsPtr_UTF8>;
	};
	template <typename t_CTraits>
	struct TCStr : t_CTraits::CImp
	{
		using CImp = typename t_CTraits::CImp;
		char const *f_GetStr() const
		{
			return CImp::f_GetStr();
		}
	};
	using CStrPtr = TCStr<CStrTraitsPtr_UTF8>;

	TCFuture<bool> fg_HasBuffer(CStrPtr _Buffer)
	{
		co_await fg_Suspend();
		co_return _Buffer.f_GetStr() != nullptr;
	}

	TCFuture<char> fg_First(CStrPtr _Buffer)
// CHECK-MESSAGES: :[[@LINE-1]]:34: warning: TCFuture coroutine 'fg_First' uses '_Buffer' after a suspension, {{.*}}
	{
		co_await fg_Suspend();
		co_return *_Buffer.f_GetStr();
	}
}

// Pre-review 131: an aggregate made of it, by what is done with it, only read
// as a value or kept.
namespace NPreReview131
{
	TCFuture<int> fg_Suspend();

	struct CView
	{
		int const *m_pValue;
		int f_Len() const
		{
			return 1;
		}
	};

	TCFuture<int> fg_Read(int const &_Value)
	{
		int Length = CView{&_Value}.f_Len();
		co_await fg_Suspend();
		co_return Length;
	}

	TCFuture<int> fg_Kept(int const &_Value)
// CHECK-MESSAGES: :[[@LINE-1]]:35: warning: TCFuture coroutine 'fg_Kept' uses '_Value' after a suspension, {{.*}}
	{
		CView View{&_Value};
		co_await fg_Suspend();
		co_return *View.m_pValue;
	}
}

// Round 136: what the result keeps where provenance is cut is only what
// can give it what refers to the data, not a number read of it or a
// condition; a member assigned on its own replaces what the whole was given;
// assigning a view held by value, or a member of it, writes the frame; a
// getter of only values may give its own scalar to a helper by reference.
namespace NReview2Round136
{
	TCFuture<int> fg_Work();
	using umint = unsigned long;

	struct CText
	{
		char const *m_pData;
		umint f_GetLen() const;
	};
	struct CDescription
	{
		CDescription(umint _Length, CText _Payload);
		umint m_Length;
		CText m_Payload;
	};
	CText fg_Payload();
	TCFuture<CDescription> fg_Describe(CText const &_Text)
	{
		co_return CDescription(_Text.f_GetLen(), fg_Payload());
	}

	struct CConfig
	{
		int m_Value;
	};
	extern CConfig g_Config;
	char const *fg_Name(CConfig const &_Config);
	TCFuture<char const *> fg_Fallback(bool const &_bFallback)
	{
		co_return _bFallback ? fg_Name(g_Config) : nullptr;
	}

	struct CIoSpan
	{
		void const *m_pData;
		umint m_nBytes;
	};
	static char const g_Output[] = "output";
	TCFuture<CIoSpan> fg_Retarget(CIoSpan _Span)
	{
		CIoSpan Result = _Span;
		Result.m_pData = g_Output;
		co_return Result;
	}
	TCFuture<CIoSpan> fg_RetargetMaybe(CIoSpan _Span, bool _bRetarget)
// CHECK-MESSAGES: :[[@LINE-1]]:45: warning: TCFuture coroutine 'fg_RetargetMaybe' uses '_Span' after a suspension, {{.*}}
	{
		CIoSpan Result = _Span;
		if (_bRetarget)
			Result.m_pData = g_Output;
		co_return Result;
	}
	TCFuture<CIoSpan> fg_Resized(CIoSpan _Span)
// CHECK-MESSAGES: :[[@LINE-1]]:39: warning: TCFuture coroutine 'fg_Resized' uses '_Span' after a suspension, {{.*}}
	{
		CIoSpan Result = _Span;
		Result.m_nBytes = 0;
		co_return Result;
	}

	TCFuture<umint> fg_GetSize(CIoSpan _Span, bool _bDefault)
	{
		co_await fg_Work();
		if (_bDefault)
			_Span = {g_Output, sizeof(g_Output)};
		co_return _Span.m_nBytes;
	}
	TCFuture<umint> fg_ClearData(CIoSpan _Span)
	{
		co_await fg_Work();
		_Span.m_pData = nullptr;
		co_return _Span.m_nBytes;
	}
	TCFuture<char> fg_ReadData(CIoSpan _Span)
// CHECK-MESSAGES: :[[@LINE-1]]:37: warning: TCFuture coroutine 'fg_ReadData' uses '_Span' after a suspension, {{.*}}
	{
		co_await fg_Work();
		co_return *static_cast<char const *>(_Span.m_pData);
	}

	template <typename tf_CFirst, typename tf_CSecond>
	constexpr umint fg_Min(tf_CFirst &&_First, tf_CSecond &&_Second)
	{
		return _First < _Second ? _First : _Second;
	}
	struct CStrSpan
	{
		char const *m_pStr;
		umint m_Length;
		umint f_GetChunkSize() const
		{
			return fg_Min(m_Length, umint(4096));
		}
		umint f_GetFirstChunk() const
		{
			return fg_Min(*m_pStr, umint(4096));
		}
	};
	TCFuture<umint> fg_ChunkSize(CStrSpan _Text)
	{
		co_await fg_Work();
		co_return _Text.f_GetChunkSize();
	}
	TCFuture<umint> fg_FirstChunk(CStrSpan _Text)
// CHECK-MESSAGES: :[[@LINE-1]]:41: warning: TCFuture coroutine 'fg_FirstChunk' uses '_Text' after a suspension, {{.*}}
	{
		co_await fg_Work();
		co_return _Text.f_GetFirstChunk();
	}
}

// A view parameter held by value is read by the slot analysis, as a local is,
// also of a member of a member: a member that can hold a reference replaced
// on every path holds what replaced it, not what the caller gave.
namespace NReview2Round139
{
	TCFuture<int> fg_Work();
	using umint = decltype(sizeof(0));
	namespace NMib::NSys
	{
		struct CIoSpan
		{
			void const *m_pData;
			umint m_nBytes;
		};
	}
	using NMib::NSys::CIoSpan;
	static char const gc_Output[] = "output";

	TCFuture<CIoSpan> fg_Output(CIoSpan _Span)
	{
		_Span.m_pData = gc_Output;
		_Span.m_nBytes = sizeof(gc_Output);
		co_return _Span;
	}
	TCFuture<CIoSpan> fg_Empty(CIoSpan _Span)
	{
		_Span.m_pData = nullptr;
		co_return _Span;
	}
	TCFuture<CIoSpan> fg_EmptyMaybe(CIoSpan _Span, bool _bEmpty)
// CHECK-MESSAGES: :[[@LINE-1]]:42: warning: TCFuture coroutine 'fg_EmptyMaybe' uses '_Span' after a suspension, but it takes it as 'CIoSpan', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
	{
		if (_bEmpty)
			_Span.m_pData = nullptr;
		co_return _Span;
	}
	TCFuture<CIoSpan> fg_Resized(CIoSpan _Span)
// CHECK-MESSAGES: :[[@LINE-1]]:39: warning: TCFuture coroutine 'fg_Resized' uses '_Span' after a suspension, but it takes it as 'CIoSpan', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
	{
		_Span.m_nBytes = 0;
		co_return _Span;
	}
	TCFuture<void const *> fg_Data(CIoSpan _Span)
// CHECK-MESSAGES: :[[@LINE-1]]:41: warning: TCFuture coroutine 'fg_Data' uses '_Span' after a suspension, but it takes it as 'CIoSpan', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
	{
		co_return _Span.m_pData;
	}

	TCFuture<int> fg_Process(CIoSpan _Span)
	{
		char Buffer[256]{};
		_Span.m_pData = Buffer;
		co_await fg_Work();
		co_return static_cast<char const *>(_Span.m_pData)[0];
	}
	TCFuture<int> fg_ProcessMaybe(CIoSpan _Span, bool _bCopy)
// CHECK-MESSAGES: :[[@LINE-1]]:40: warning: TCFuture coroutine 'fg_ProcessMaybe' uses '_Span' after a suspension, but it takes it as 'CIoSpan', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
	{
		char Buffer[256]{};
		if (_bCopy)
			_Span.m_pData = Buffer;
		co_await fg_Work();
		co_return static_cast<char const *>(_Span.m_pData)[0];
	}
	// What the new value is made of only as a value, the length, isn't
	// borrowed.
	TCFuture<int> fg_Rebind(CIoSpan _Span)
	{
		char Buffer[256]{};
		if (_Span.m_nBytes > sizeof(Buffer))
			co_return 0;
		for (umint iByte = 0; iByte != _Span.m_nBytes; ++iByte)
			Buffer[iByte] = static_cast<char const *>(_Span.m_pData)[iByte];
		_Span = {Buffer, _Span.m_nBytes};
		co_await fg_Work();
		co_return static_cast<char const *>(_Span.m_pData)[0];
	}
	TCFuture<int> fg_RebindKept(CIoSpan _Span)
// CHECK-MESSAGES: :[[@LINE-1]]:38: warning: TCFuture coroutine 'fg_RebindKept' uses '_Span' after a suspension, but it takes it as 'CIoSpan', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
	{
		_Span = {_Span.m_pData, 4};
		co_await fg_Work();
		co_return static_cast<char const *>(_Span.m_pData)[0];
	}

	struct CReadResult
	{
		CIoSpan m_Data;
	};
	TCFuture<CReadResult> fg_EmptyResult(CIoSpan _Input)
	{
		CReadResult Result{_Input};
		Result.m_Data.m_pData = nullptr;
		Result.m_Data.m_nBytes = 0;
		co_return Result;
	}
	struct CSendResult
	{
		CIoSpan m_Remaining;
		int m_Error;
	};
	TCFuture<CSendResult> fg_CompleteSend(CIoSpan _Span)
	{
		CSendResult Result{_Span, 0};
		Result.m_Remaining.m_pData = nullptr;
		co_return Result;
	}
	TCFuture<CSendResult> fg_CompleteSendKept(CIoSpan _Span)
// CHECK-MESSAGES: :[[@LINE-1]]:52: warning: TCFuture coroutine 'fg_CompleteSendKept' uses '_Span' after a suspension, but it takes it as 'CIoSpan', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
	{
		CSendResult Result{_Span, 0};
		Result.m_Remaining.m_nBytes = 0;
		co_return Result;
	}
	TCFuture<CReadResult> fg_ResultBefore(CIoSpan _Span)
// CHECK-MESSAGES: :[[@LINE-1]]:48: warning: TCFuture coroutine 'fg_ResultBefore' uses '_Span' after a suspension, but it takes it as 'CIoSpan', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
	{
		CReadResult Result{_Span};
		_Span.m_pData = nullptr;
		co_return Result;
	}
	struct CBase
	{
		CIoSpan m_Data;
	};
	struct CDerived : CBase
	{
		int m_Status;
	};
	TCFuture<CDerived> fg_RetargetBase(CIoSpan _Span)
	{
		CDerived Result{{_Span}, 0};
		Result.m_Data = {gc_Output, sizeof(gc_Output)};
		co_return Result;
	}
}

// A view-like holder taken by value: only the parts of it that hold a
// pointer, a reference or a view lead to the caller's data, also of a member
// of it by value; a member of only values, or what a getter of a nested view
// gives of its own storage, is in the frame.
namespace NReview2Round145
{
	TCFuture<int> fg_Work();
	using umint = decltype(sizeof(0));
	struct CStrSpan
	{
		char const *m_pData;
		umint m_Length;
		umint f_GetLen() const { return m_Length; }
		char f_GetFirst() const { return m_pData[0]; }
	};
	struct CInfo
	{
		umint m_nBytes;
	};
	struct CSourceStrSpan
	{
		CStrSpan m_Text;
		umint m_Line;
		CInfo m_Info;
	};

	TCFuture<umint> fg_Line(CSourceStrSpan _Source)
	{
		co_await fg_Work();
		co_return _Source.m_Line;
	}
	TCFuture<umint> fg_Length(CSourceStrSpan _Source)
	{
		co_await fg_Work();
		co_return _Source.m_Text.f_GetLen();
	}
	TCFuture<umint> fg_TextLength(CSourceStrSpan _Source)
	{
		co_await fg_Work();
		co_return _Source.m_Text.m_Length;
	}
	TCFuture<umint> fg_Bytes(CSourceStrSpan _Source)
	{
		co_await fg_Work();
		co_return _Source.m_Info.m_nBytes;
	}
	TCFuture<bool> fg_HasText(CSourceStrSpan _Source)
	{
		co_await fg_Work();
		co_return _Source.m_Text.m_pData != nullptr;
	}
	TCFuture<char> fg_First(CSourceStrSpan _Source)
// CHECK-MESSAGES: :[[@LINE-1]]:41: warning: TCFuture coroutine 'fg_First' uses '_Source' after a suspension, {{.*}}
	{
		co_await fg_Work();
		co_return _Source.m_Text.f_GetFirst();
	}
	TCFuture<CStrSpan> fg_Text(CSourceStrSpan _Source)
// CHECK-MESSAGES: :[[@LINE-1]]:44: warning: TCFuture coroutine 'fg_Text' uses '_Source' after a suspension, {{.*}}
	{
		co_await fg_Work();
		co_return _Source.m_Text;
	}
}

namespace NBufferOwningConversion
{
	// One that owns a buffer (OwnerTypes) made of a view copies what
	// it refers to; one that isn't listed can keep it.
	TCFuture<int> fg_Work();
	struct CStrPtr
	{
		char const *m_pStr;
	};
	struct CStrTraits_CStr
	{
	};
	template <typename t_CTraits>
	struct TCStr
	{
		TCStr(CStrPtr const &_Text);
		~TCStr();
		int f_GetLen() const;
		char *mp_pStr;
	};
	using CStr = TCStr<CStrTraits_CStr>;
	struct CHolder
	{
		CHolder(CStrPtr const &_Text);
		int f_GetLen() const;
		char const *mp_pStr;
	};
	TCFuture<int> fg_Copied(CStrPtr _Text)
	{
		CStr Text(_Text);
		co_await fg_Work();
		co_return Text.f_GetLen();
	}
	TCFuture<int> fg_Held(CStrPtr _Text)
// CHECK-MESSAGES: :[[@LINE-1]]:32: warning: TCFuture coroutine 'fg_Held' uses '_Text' after a suspension, but it takes it as 'CStrPtr', which refers to data the caller owns, which can be gone by then; {{.*}} [malterlib-coroutine-reference-parameter]
	{
		CHolder Text(_Text);
		co_await fg_Work();
		co_return Text.f_GetLen();
	}
}


// Round 157: a member function of a view taken by value that reads only its
// own storage, also given arguments, reads the frame's copy.
namespace NReview2Round157 {
TCFuture<int> fg_Work();
struct CPacketStrSpan {
  char const *m_pData;
  unsigned m_nBytes;
  bool f_HasBytes(unsigned _nRequired) const { return m_nBytes >= _nRequired; }
};
TCFuture<int> fg_Check(CPacketStrSpan _Packet) { co_await fg_Work(); co_return _Packet.f_HasBytes(4); }
TCFuture<int> fg_CheckData(CPacketStrSpan _Packet) { co_await fg_Work(); co_return *_Packet.m_pData; }
// CHECK-MESSAGES: :[[@LINE-1]]:43: warning: TCFuture coroutine 'fg_CheckData' uses '_Packet' after a suspension, but it takes it as 'CPacketStrSpan', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
}
// Round 173: a result that keeps an owner of the allocation it points into,
// made of an owner parameter. Reported: one that keeps another owner, one
// that points to the parameter itself, and one that uses the parameter after
// a suspension.
namespace NReview2Round173ResultOwner {
using namespace NMib::NStorage;
TCFuture<int> fg_Work();
struct CJob { int m_Value = 0; };
struct CReadResult { TCSharedPointer<CJob> m_pJob; int *m_pValue = nullptr; };
struct CSlotResult { TCSharedPointer<CJob> m_pJob; TCSharedPointer<CJob> const *m_ppJob = nullptr; };
TCFuture<CReadResult> fg_Read(TCSharedPointer<CJob> const &_pJob) {
  co_return CReadResult{_pJob, &_pJob->m_Value};
}
// The keeper parameter states that it owns no object another parameter owns
// (DMibDistinct): reported.
TCFuture<CReadResult> fg_ReadOther(TCSharedPointer<CJob> const &_pJob, DMibDistinct TCSharedPointer<CJob> const &_pOther) {
// CHECK-MESSAGES: :[[@LINE-1]]:65: warning: TCFuture coroutine 'fg_ReadOther' uses '_pJob' after a suspension, but it takes it as 'const TCSharedPointer<CJob> &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
  co_return CReadResult{_pOther, &_pJob->m_Value};
}
// Clean: unstated, the caller may give the same owner twice.
TCFuture<CReadResult> fg_ReadOtherUnstated(TCSharedPointer<CJob> const &_pJob, TCSharedPointer<CJob> const &_pOther) {
  co_return CReadResult{_pOther, &_pJob->m_Value};
}
TCFuture<CSlotResult> fg_ReadSlot(TCSharedPointer<CJob> const &_pJob) {
// CHECK-MESSAGES: :[[@LINE-1]]:64: warning: TCFuture coroutine 'fg_ReadSlot' uses '_pJob' after a suspension, but it takes it as 'const TCSharedPointer<CJob> &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
  co_return CSlotResult{_pJob, &_pJob};
}
TCFuture<CReadResult> fg_ReadAfter(TCSharedPointer<CJob> const &_pJob) {
// CHECK-MESSAGES: :[[@LINE-1]]:65: warning: TCFuture coroutine 'fg_ReadAfter' uses '_pJob' after a suspension, but it takes it as 'const TCSharedPointer<CJob> &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
  co_await fg_Work();
  co_return CReadResult{_pJob, &_pJob->m_Value};
}
} // namespace NReview2Round173ResultOwner

// Rule K in the frame: a keeper held by value in a parameter or a local in
// scope at the use, one declared after it or in a sibling scope reported; a
// result that keeps it with its own
// keeper, also of a raw pointer parameter. Rule R before the first
// suspension: what an unseen constructor or function keeps, by type.
namespace NReview2Round177 {
using namespace NMib::NStorage;
TCFuture<int> fg_Work();
struct CJob { int m_Result = 0; int m_Value = 0; };
TCSharedPointer<CJob> fg_FindJob();
TCFuture<int> fg_FrameParameter(CJob const &_Job, TCSharedPointer<CJob> _pKeep) { co_await fg_Work(); co_return _Job.m_Value; }
TCFuture<int> fg_FramePointer(CJob const *_pJob, TCSharedPointer<CJob> _pKeep) { co_await fg_Work(); co_return _pJob->m_Value; }
TCFuture<int> fg_FrameLocal(CJob const &_Job, TCSharedPointer<CJob> const &_pJob) { auto pKeep = _pJob; co_await fg_Work(); co_return _Job.m_Value; }
TCFuture<int> fg_FrameByReference(CJob const &_Job, TCSharedPointer<CJob> const &_pKeep) { co_await fg_Work(); co_return _Job.m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:47: warning: TCFuture coroutine 'fg_FrameByReference' uses '_Job' after a suspension, but it takes it as 'const CJob &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
TCFuture<int> fg_FrameLocalAfter(CJob const &_Job) { co_await fg_Work(); int Value = _Job.m_Value; auto pKeep = fg_FindJob(); co_return Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:46: warning: TCFuture coroutine 'fg_FrameLocalAfter' uses '_Job' after a suspension, but it takes it as 'const CJob &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
TCFuture<int> fg_FrameLocalSibling(CJob const &_Job) { { auto pKeep = fg_FindJob(); } co_await fg_Work(); co_return _Job.m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:48: warning: TCFuture coroutine 'fg_FrameLocalSibling' uses '_Job' after a suspension, but it takes it as 'const CJob &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
TCFuture<int> fg_FrameWeak(CJob const &_Job, TCWeakPointer<CJob> _pKeep) { co_await fg_Work(); co_return _Job.m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:40: warning: TCFuture coroutine 'fg_FrameWeak' uses '_Job' after a suspension, but it takes it as 'const CJob &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
struct CResult { TCSharedPointer<CJob> m_pKeep; CJob const *m_pJob; };
struct CReadResult { TCSharedPointer<CJob> m_pKeep; int const *m_pResult; };
struct CBare { CJob const *m_pJob; };
TCFuture<CResult> fg_ResultMoved(CJob const &_Job, TCSharedPointer<CJob> _pKeep) { co_return CResult{fg_Move(_pKeep), &_Job}; }
TCFuture<CReadResult> fg_ResultCopied(CJob const &_Job, TCSharedPointer<CJob> const &_pKeep) { co_return CReadResult{_pKeep, &_Job.m_Result}; }
TCFuture<CResult> fg_ResultPointer(CJob const *_pJob, TCSharedPointer<CJob> _pKeep) { co_return CResult{_pKeep, _pJob}; }
TCFuture<CBare> fg_ResultBare(CJob const &_Job, TCSharedPointer<CJob> _pKeep) { co_return CBare{&_Job}; }
// CHECK-MESSAGES: :[[@LINE-1]]:43: warning: TCFuture coroutine 'fg_ResultBare' uses '_Job' after a suspension, but it takes it as 'const CJob &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
TCFuture<CBare> fg_ResultEscapes(CJob const &_Job, TCSharedPointer<CJob> _pKeep) { co_await fg_Work(); co_return CBare{&_Job}; }
// CHECK-MESSAGES: :[[@LINE-1]]:46: warning: TCFuture coroutine 'fg_ResultEscapes' uses '_Job' after a suspension, but it takes it as 'const CJob &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
struct CConnection { CConnection(TCSharedPointer<CJob> const &_pJob); ~CConnection(); void *m_pAddress; };
struct CValues { CValues(int const &_Value); ~CValues(); int m_Value; };
struct CHolder { CHolder(int const &_Value); ~CHolder(); int const *m_pValue; };
void fg_UseJob(TCSharedPointer<CJob> const &_pJob);
void fg_Inspect(int const &_Value);
int const &fg_Identity(int const &_Value);
TCFuture<int> fg_ConstructShared(TCSharedPointer<CJob> const &_pJob) { CConnection Connection(_pJob); co_await fg_Work(); co_return 0; }
TCFuture<int> fg_ConstructValues(int const &_Value) { CValues Values(_Value); co_await fg_Work(); co_return 0; }
TCFuture<int> fg_CallShared(TCSharedPointer<CJob> const &_pJob) { fg_UseJob(_pJob); co_return co_await fg_Work(); }
TCFuture<int> fg_CallInspect(int const &_Value) { fg_Inspect(_Value); co_return co_await fg_Work(); }
TCFuture<int> fg_ConstructHolder(int const &_Value) { CHolder Holder(_Value); co_await fg_Work(); co_return 0; }
// CHECK-MESSAGES: :[[@LINE-1]]:45: warning: TCFuture coroutine 'fg_ConstructHolder' uses '_Value' after a suspension, but it takes it as 'const int &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
TCFuture<int> fg_CallIdentity(int const &_Value) { int const &Kept = fg_Identity(_Value); co_await fg_Work(); co_return Kept; }
// CHECK-MESSAGES: :[[@LINE-1]]:42: warning: TCFuture coroutine 'fg_CallIdentity' uses '_Value' after a suspension, but it takes it as 'const int &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
// A pointer parameter of an actor's member coroutine, kept from the call on
// by a keeper given by value.
struct CApp : public NMib::NConcurrency::CActor {
  TCFuture<int> f_Kept(CJob *_pJob, TCSharedPointer<CJob> _pKeep) { co_await fg_Work(); co_return _pJob->m_Value; }
  TCFuture<int> f_Unkept(CJob *_pJob, TCWeakPointer<CJob> _pKeep) { co_await fg_Work(); co_return _pJob->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:32: warning: TCFuture coroutine 'f_Unkept' is a member function of actor 'CApp' and takes 'CJob *', whose data can be gone when an actor call starts the coroutine, after the call returns; take the data by value [malterlib-coroutine-reference-parameter]
};
} // namespace NReview2Round177

// Round 184: a pointer member of a local record read through its name is
// what it was given, reported; read through a pointer to the record, or
// through a dereference of one, it is a value, a known miss.
namespace NReview2Round184Parameter {
struct CReadContext { int *m_pValue; };
TCFuture<int *> fg_ThroughPointer(int &_Value) {
  CReadContext Context{&_Value};
  auto *pContext = &Context;
  co_return pContext->m_pValue;
}
TCFuture<int *> fg_ThroughDereference(int &_Value) {
  CReadContext Context{&_Value};
  auto *pContext = &Context;
  co_return (*pContext).m_pValue;
}
TCFuture<int *> fg_Direct(int &_Value) {
// CHECK-MESSAGES: :[[@LINE-1]]:32: warning: TCFuture coroutine 'fg_Direct' uses '_Value' after a suspension, {{.*}}
  CReadContext Context{&_Value};
  co_return Context.m_pValue;
}
TCFuture<int *> fg_Address(int &_Value) {
// CHECK-MESSAGES: :[[@LINE-1]]:33: warning: TCFuture coroutine 'fg_Address' uses '_Value' after a suspension, but it takes it as 'int &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
  co_return &_Value;
}
} // namespace NReview2Round184Parameter
// Round 188: a getter that returns a stored pointer member or `nullptr`
// gives a stored member of its object (callResultOf): `nullptr` refers to
// nothing, in any spelling, and merges as the identity, so the getter is
// told as the unconditional one is. The member states that only its class
// points it (DMibSetByOwnClass), which it never does; without that, what it
// points to can't be told, reported.
namespace NReview2Round188Parameter {
struct CJob { int m_Value = 0; };
struct CJobSlot {
  DMibSetByOwnClass CJob *m_pJob = nullptr;
  CJob *f_TryGet() const { return m_pJob ? m_pJob : nullptr; }
  CJob *f_TryGetZero() const { return m_pJob ? m_pJob : 0; }
  CJob *f_TryGetIf() const {
    if (!m_pJob)
      return {};
    return m_pJob;
  }
  CJob *f_TryGetCast() const { return m_pJob ? m_pJob : static_cast<CJob *>(nullptr); }
};
TCFuture<int> fg_Work();
TCFuture<int> fg_Read(CJobSlot const &_Slot) {
  auto *pJob = _Slot.f_TryGet();
  co_await fg_Work();
  co_return pJob ? pJob->m_Value : 0;
}
TCFuture<int> fg_ReadZero(CJobSlot const &_Slot) {
  auto *pJob = _Slot.f_TryGetZero();
  co_await fg_Work();
  co_return pJob ? pJob->m_Value : 0;
}
TCFuture<int> fg_ReadIf(CJobSlot const &_Slot) {
  auto *pJob = _Slot.f_TryGetIf();
  co_await fg_Work();
  co_return pJob ? pJob->m_Value : 0;
}
TCFuture<int> fg_ReadCast(CJobSlot const &_Slot) {
  auto *pJob = _Slot.f_TryGetCast();
  co_await fg_Work();
  co_return pJob ? pJob->m_Value : 0;
}
struct CUntoldSlot {
  CJob *m_pJob = nullptr;
  CJob *f_TryGet() const { return m_pJob ? m_pJob : nullptr; }
};
TCFuture<int> fg_ReadUntold(CUntoldSlot const &_Slot) {
// CHECK-MESSAGES: :[[@LINE-1]]:48: warning: TCFuture coroutine 'fg_ReadUntold' uses '_Slot' after a suspension
  auto *pJob = _Slot.f_TryGet();
  co_await fg_Work();
  co_return pJob ? pJob->m_Value : 0;
}
// Reported: a getter of a pointer into the slot itself, or nullptr.
struct CValueSlot {
  CJob m_Job;
  CJob *f_TryGet(bool _bHas) { return _bHas ? &m_Job : nullptr; }
};
TCFuture<int> fg_ReadOwn(CValueSlot &_Slot) {
// CHECK-MESSAGES: :[[@LINE-1]]:38: warning: TCFuture coroutine 'fg_ReadOwn' uses '_Slot' after a suspension
  auto *pJob = _Slot.f_TryGet(true);
  co_await fg_Work();
  co_return pJob ? pJob->m_Value : 0;
}
} // namespace NReview2Round188Parameter
// Round 188: a use after a suspension is asked at its access path, the cast
// on it included (reportLifetime): `static_cast<CJob *>(_pContext)` anchors
// CJob, which the frame's keeper fits. A keeper declared after the borrow,
// in scope at the use, keeps it too.
namespace NReview2Round188Cast {
struct CJob { int m_Result = 0; };
TCFuture<int> fg_Work();
TCFuture<int> fg_Read(void *_pContext, NMib::NStorage::TCSharedPointer<CJob> _pKeep) {
  co_await fg_Work();
  co_return static_cast<CJob *>(_pContext)->m_Result;
}
TCFuture<int> fg_ReadNoKeep(void *_pContext) {
// CHECK-MESSAGES: :[[@LINE-1]]:35: warning: TCFuture coroutine 'fg_ReadNoKeep' uses '_pContext' after a suspension
  co_await fg_Work();
  co_return static_cast<CJob *>(_pContext)->m_Result;
}
TCFuture<int> fg_ReadKeepAfter(CJob &_Job, NMib::NStorage::TCSharedPointer<CJob> const &_pJob) {
  int &Result = _Job.m_Result;
  NMib::NStorage::TCSharedPointer<CJob> pKeep = _pJob;
  co_await fg_Work();
  co_return Result;
}
} // namespace NReview2Round188Cast

namespace NHeldInTemporary {
// What a call keeps in the object it is called on, or in what another
// argument refers to, lasts as long as that object: a temporary of the full
// expression ends there, unless the expression gives it on, and a value made
// of it that can't hold a pointer, a reference or a view keeps nothing. A
// named one keeps it when it is a view (ViewTypes), a pointer or an owner; a
// named record that carries it in a member keeps it too.
struct CStrPtr { char const *m_pStr = nullptr; };
struct CFormat {
  CFormat(char const *_pFormat);
  CFormat &&operator<<(CStrPtr const &_Text) && {
    m_Text = _Text;
    return static_cast<CFormat &&>(*this);
  }
  CStrPtr m_Text;
};
struct CFormatStrSpan {
  CFormatStrSpan(char const *_pFormat);
  static void fs_Add(CFormatStrSpan &o_Format, CStrPtr const &_Text) { o_Format.m_Text = _Text; }
  CFormatStrSpan &&operator<<(CStrPtr const &_Text) && {
    fs_Add(*this, _Text);
    return static_cast<CFormatStrSpan &&>(*this);
  }
  CStrPtr m_Text;
};
struct CText {
  CText(CFormat &&_Format);
  CText(CFormatStrSpan &&_Format);
  int m_Length = 0;
};
TCFuture<int> fg_Work();
TCFuture<int> fg_ReadLength(CStrPtr _Text) {
  CText Copy = CFormat("{}") << _Text;
  co_await fg_Work();
  co_return Copy.m_Length;
}
// Kept by a helper in what another reference argument refers to, the object
// of the call, `fs_Add(*this, _Text)`: the temporary it is called on.
TCFuture<int> fg_ReadLengthAdd(CStrPtr _Text) {
  CText Copy = CFormatStrSpan("{}") << _Text;
  co_await fg_Work();
  co_return Copy.m_Length;
}
// A named record that isn't a view keeps it in its member, which the
// temporary it is moved from was given.
TCFuture<int> fg_NamedRecord(CStrPtr _Text) {
// CHECK-MESSAGES: :[[@LINE-1]]:{{[0-9]+}}: warning: TCFuture coroutine 'fg_NamedRecord' uses '_Text' after a suspension{{.*}} [malterlib-coroutine-reference-parameter]
  CFormat Format = CFormat("{}") << _Text;
  co_await fg_Work();
  CText Copy(static_cast<CFormat &&>(Format));
  co_return Copy.m_Length;
}
TCFuture<int> fg_NamedView(CStrPtr _Text) {
// CHECK-MESSAGES: :[[@LINE-1]]:36: warning: TCFuture coroutine 'fg_NamedView' uses '_Text' after a suspension
  CFormatStrSpan Format = CFormatStrSpan("{}") << _Text;
  co_await fg_Work();
  CText Copy(static_cast<CFormatStrSpan &&>(Format));
  co_return Copy.m_Length;
}
TCFuture<int> fg_AddToNamedView(CStrPtr _Text) {
// CHECK-MESSAGES: :[[@LINE-1]]:41: warning: TCFuture coroutine 'fg_AddToNamedView' uses '_Text' after a suspension
  CFormatStrSpan Format("{}");
  CFormatStrSpan::fs_Add(Format, _Text);
  co_await fg_Work();
  CText Copy(static_cast<CFormatStrSpan &&>(Format));
  co_return Copy.m_Length;
}
CFormatStrSpan fg_MakeFormat(CStrPtr _Text) { return CFormatStrSpan("{}") << _Text; }
TCFuture<int> fg_ReturnedView(CStrPtr _Text) {
// CHECK-MESSAGES: :[[@LINE-1]]:39: warning: TCFuture coroutine 'fg_ReturnedView' uses '_Text' after a suspension
  CFormatStrSpan Format = fg_MakeFormat(_Text);
  co_await fg_Work();
  CText Copy(static_cast<CFormatStrSpan &&>(Format));
  co_return Copy.m_Length;
}
// A copy of a record that isn't a view is a value (Rule V): its pointer
// member is copied, nothing of the parameter's storage is kept.
struct CReadOptions { char const *m_pPrefix; };
TCFuture<int> fg_ReadCopy(CReadOptions const &_Options) {
  CReadOptions Options = _Options;
  co_await fg_Work();
  co_return Options.m_pPrefix[0];
}
} // namespace NHeldInTemporary

// Round 190 llvm-coroutine-1, round 192 llvm-coroutine-10: the context of a use
// is where it is written: in a coroutine lambda, its frame (the parameters,
// `_pJob` itself and the keeper `_pKeep`) and its closure.
namespace NReview2Round192Context {
TCFuture<int> fg_Work();
struct CJob { int m_Value = 0; };

TCFuture<int> fg_CalledHere(TCSharedPointer<CJob> pKeep) {
  CJob *pJob = pKeep.operator->();
  co_return co_await [](CJob *_pJob, TCSharedPointer<CJob> _pKeep) -> TCFuture<int> {
    co_await fg_Work();
    co_return _pJob->m_Value;
  }(pJob, fg_Move(pKeep));
}

TCFuture<int> fg_Stored(TCSharedPointer<CJob> pJob) {
  auto fRead = [](CJob const *_pJob, TCSharedPointer<CJob> _pKeep) -> TCFuture<int> {
    co_await fg_Work();
    co_return _pJob->m_Value;
  };
  co_return co_await fRead(pJob.operator->(), pJob);
}

template <typename t_CJob>
TCFuture<int> fg_Generic(TCSharedPointer<t_CJob> pJob) {
  auto fRead = [](auto const *_pJob, TCSharedPointer<t_CJob> _pKeep) -> TCFuture<int> {
    co_await fg_Work();
    co_return _pJob->m_Value;
  };
  co_return co_await fRead(pJob.operator->(), pJob);
}
template TCFuture<int> fg_Generic<CJob>(TCSharedPointer<CJob> pJob);

// Without a keeper it is reported.
TCFuture<int> fg_NoKeeper(TCSharedPointer<CJob> pJob) {
  auto fRead = [](CJob const *_pJob) -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:31: warning: TCFuture coroutine 'operator()' uses '_pJob' after a suspension, but it takes it as 'const CJob *', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
    co_await fg_Work();
    co_return _pJob->m_Value;
  };
  co_return co_await fRead(pJob.operator->());
}
} // namespace NReview2Round192Context

// Round 192 llvm-coroutine-2 and -9: what a use touches is read off its access
// path, the expressions around it whose provenance derives from it: the
// address of a pointer parameter is its own storage, and a function that
// gives back what it is given is on the path, with the anchors of its type.
namespace NReview2Round192Path {
TCFuture<int> fg_Work();
struct CJob { int m_Value = 0; int m_Result = 0; };
void fg_Reset(CJob **o_ppJob) { *o_ppJob = nullptr; }
CJob *fg_Job(void *_pContext) { return static_cast<CJob *>(_pContext); }

TCFuture<int> fg_Run(CJob *_pJob) {
  co_await fg_Work();
  fg_Reset(&_pJob);
  co_return 0;
}
TCFuture<int> fg_Read(void *_pContext, TCSharedPointer<CJob> _pKeep) {
  co_await fg_Work();
  co_return fg_Job(_pContext)->m_Result;
}
// Reported: no keeper; and what the pointer points to read after the
// suspension.
TCFuture<int> fg_ReadNoKeeper(void *_pContext) {
// CHECK-MESSAGES: :[[@LINE-1]]:37: warning: TCFuture coroutine 'fg_ReadNoKeeper' uses '_pContext' after a suspension, but it takes it as 'void *', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
  co_await fg_Work();
  co_return fg_Job(_pContext)->m_Result;
}
TCFuture<int> fg_Deref(CJob *_pJob) {
// CHECK-MESSAGES: :[[@LINE-1]]:30: warning: TCFuture coroutine 'fg_Deref' uses '_pJob' after a suspension, but it takes it as 'CJob *', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
  co_await fg_Work();
  co_return (*_pJob).m_Value;
}
} // namespace NReview2Round192Path

// Round 196 (llvm-coroutine-3): what is done with a pointer's value is read
// where the steps that hand it on unchanged end, past a conditional: only
// compared, clean; dereferenced through the conditional, reported.
namespace NReview2Round196Conditional {
TCFuture<int> fg_Work();
struct CJob { int m_Value = 0; };
TCFuture<bool> fg_HasTarget(CJob *_pJob, bool _bEnabled) { co_await fg_Work(); co_return (_bEnabled ? _pJob : nullptr) != nullptr; }
TCFuture<int> fg_ReadTarget(CJob *_pJob, bool _bEnabled) { co_await fg_Work(); co_return (_bEnabled ? _pJob : nullptr)->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:35: warning: TCFuture coroutine 'fg_ReadTarget' uses '_pJob' after a suspension, but it takes it as 'CJob *', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
TCFuture<CJob> fg_CopyTarget(CJob *_pJob, CJob *_pOther, bool _bEnabled) { co_await fg_Work(); co_return *(_bEnabled ? _pJob : _pOther); }
// CHECK-MESSAGES: :[[@LINE-1]]:36: warning: TCFuture coroutine 'fg_CopyTarget' uses '_pJob' after a suspension, but it takes it as 'CJob *', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
// CHECK-MESSAGES: :[[@LINE-2]]:49: warning: TCFuture coroutine 'fg_CopyTarget' uses '_pOther' after a suspension, but it takes it as 'CJob *', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
} // namespace NReview2Round196Conditional

// What a use does to what a pointer points to is told also in a body it is
// given to that is seen: a comparison there, an address only compared and an
// unevaluated operand touch nothing; a seen body that dereferences or keeps it,
// and one that isn't seen, use it.
namespace NReview2Round197Storage {
TCFuture<int> fg_Work();
struct CJob { int m_Value = 0; };
template <typename t_CType>
struct TCWeakPointer
{
	t_CType *m_pObject = nullptr;
	t_CType *fp_GetContained() const { return m_pObject; }
	bool operator == (t_CType const *_pOther) const { return fp_GetContained() == _pOther; }
};
template <typename t_CType>
bool fg_Compare(t_CType const *_pLeft, t_CType const *_pRight) { return _pLeft == _pRight; }
int fg_Read(CJob const *_pJob) { return _pJob->m_Value; }
CJob const *g_pKept = nullptr;
void fg_KeepGlobal(CJob const *_pJob) { g_pKept = _pJob; }
struct CHolder
{
	CJob const *m_pJob = nullptr;
	void f_Keep(CJob const *_pJob) { m_pJob = _pJob; }
};
bool fg_Unseen(CJob const *_pJob);

TCFuture<bool> fg_IsSame(CJob const *_pJob, TCWeakPointer<CJob> _pExpected) { co_await fg_Work(); co_return _pExpected == _pJob; }
TCFuture<bool> fg_CompareRaw(CJob const *_pJob, CJob const *_pOther) { co_await fg_Work(); co_return fg_Compare(_pJob, _pOther); }
TCFuture<bool> fg_SameAddress(CJob const &_Job, CJob const *_pOther) { co_await fg_Work(); co_return &_Job == _pOther; }
TCFuture<int> fg_Size(CJob const *_pJob) { co_await fg_Work(); co_return int(sizeof(*_pJob)); }
TCFuture<int> fg_SeenRead(CJob const *_pJob) { co_await fg_Work(); co_return fg_Read(_pJob); }
// CHECK-MESSAGES: :[[@LINE-1]]:39: warning: TCFuture coroutine 'fg_SeenRead' uses '_pJob' after a suspension, {{.*}}
TCFuture<int> fg_SeenKeepGlobal(CJob const *_pJob) { co_await fg_Work(); fg_KeepGlobal(_pJob); co_return 0; }
// CHECK-MESSAGES: :[[@LINE-1]]:45: warning: TCFuture coroutine 'fg_SeenKeepGlobal' uses '_pJob' after a suspension, {{.*}}
TCFuture<int> fg_SeenKeepMember(CJob const *_pJob) { co_await fg_Work(); CHolder Holder; Holder.f_Keep(_pJob); co_return Holder.m_pJob ? 1 : 0; }
// Clean: Holder.m_pJob is only tested against null after the suspension, never dereferenced.
TCFuture<bool> fg_UnseenCallee(CJob const *_pJob) { co_await fg_Work(); co_return fg_Unseen(_pJob); }
// CHECK-MESSAGES: :[[@LINE-1]]:44: warning: TCFuture coroutine 'fg_UnseenCallee' uses '_pJob' after a suspension, {{.*}}
TCFuture<bool> fg_UnseenAddress(CJob const &_Job) { co_await fg_Work(); co_return fg_Unseen(&_Job); }
// CHECK-MESSAGES: :[[@LINE-1]]:45: warning: TCFuture coroutine 'fg_UnseenAddress' uses '_Job' after a suspension, {{.*}}
TCFuture<int> fg_ReadAfter(CJob const &_Job) { co_await fg_Work(); co_return _Job.m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:40: warning: TCFuture coroutine 'fg_ReadAfter' uses '_Job' after a suspension, {{.*}}
} // namespace NReview2Round197Storage

// What a body gives back goes on in the caller with the call's value; a name
// of a local the value was copied into is followed only where the value
// reaches it; a pointer converted to an integer is over; a level past what a
// parameter's type has is a body that can't be seen, also of a recursive
// helper that passes its own parameter's address on.
namespace NReview2Round198Forward {
TCFuture<int> fg_Work();
struct CJob { int m_Result = 0; };
using umint = unsigned long;
template <typename t_CType>
t_CType const &fg_Const(t_CType const &_In) { return (t_CType const &)_In; }
template <typename t_CPointer>
bool fg_HasJob(t_CPointer &&_pJob) { return NMib::fg_Forward<t_CPointer>(_pJob) != nullptr; }
void fg_Visit(void *_pState, int _Depth) { if (_Depth > 0) fg_Visit(&_pState, _Depth - 1); }
void fg_Log(umint _Value);
CJob g_DefaultJob;

TCFuture<bool> fg_IsExpected(CJob *_pJob, CJob const *_pExpected) { co_await fg_Work(); co_return fg_Const(_pJob) == _pExpected; }
TCFuture<bool> fg_Check(CJob *_pJob) { co_await fg_Work(); co_return fg_HasJob(_pJob); }
TCFuture<int> fg_ConstRead(CJob *_pJob) { co_await fg_Work(); co_return fg_Const(_pJob)->m_Result; }
// CHECK-MESSAGES: :[[@LINE-1]]:34: warning: TCFuture coroutine 'fg_ConstRead' uses '_pJob' after a suspension, {{.*}}
TCFuture<int> fg_Recursive(void *_pState) { co_await fg_Work(); fg_Visit(_pState, 2); co_return 0; }
// CHECK-MESSAGES: :[[@LINE-1]]:34: warning: TCFuture coroutine 'fg_Recursive' uses '_pState' after a suspension, {{.*}}
TCFuture<int> fg_Overwritten(CJob *_pPrevious, CJob *_pExpected) { co_await fg_Work(); CJob *pCurrent = _pPrevious; bool bSame = pCurrent == _pExpected; pCurrent = &g_DefaultJob; co_return pCurrent->m_Result + bSame; }
TCFuture<int> fg_NotOverwritten(CJob *_pPrevious, CJob *_pExpected) { co_await fg_Work(); CJob *pCurrent = _pPrevious; bool bSame = pCurrent == _pExpected; co_return pCurrent->m_Result + bSame; }
// CHECK-MESSAGES: :[[@LINE-1]]:39: warning: TCFuture coroutine 'fg_NotOverwritten' uses '_pPrevious' after a suspension, {{.*}}
TCFuture<int> fg_OverwrittenOnBranch(CJob *_pPrevious, CJob *_pExpected, bool _bReset) { co_await fg_Work(); CJob *pCurrent = _pPrevious; bool bSame = pCurrent == _pExpected; if (_bReset) pCurrent = &g_DefaultJob; co_return pCurrent->m_Result + bSame; }
// CHECK-MESSAGES: :[[@LINE-1]]:44: warning: TCFuture coroutine 'fg_OverwrittenOnBranch' uses '_pPrevious' after a suspension, {{.*}}
TCFuture<umint> fg_JobID(CJob *_pJob) { co_await fg_Work(); co_return reinterpret_cast<umint>(_pJob); }
TCFuture<int> fg_JobLogged(CJob *_pJob) { co_await fg_Work(); fg_Log(reinterpret_cast<umint>(_pJob)); co_return 0; }
TCFuture<umint> fg_JobStored(CJob *_pJob) { co_await fg_Work(); umint n = reinterpret_cast<umint>(_pJob); co_return n; }
} // namespace NReview2Round198Forward

// The address of a reference follows the rules of a pointer: compared,
// converted to an integer or kept in a local that is only compared, it
// touches nothing; read through, given to a body that can't be seen or made
// the result, it is used. Put in a member of a record held by value, by an
// aggregate, a constructor that is seen or a copy assignment the compiler
// defines, a load of that member is it again; a constructor that only
// compares it keeps nothing; a member function that gives back its object,
// `return *this;`, hands it on.
namespace NReview2Round198Rulings {
TCFuture<int> fg_Work();
using umint = unsigned long;
struct CJob { int m_Result = 0; CJob &f_Self() { return *this; } };
struct CPair { CJob *m_pJob; int m_Count; };
struct CStored { CJob *m_pJob; CStored(CJob *_pJob) : m_pJob(_pJob) {} };
struct CCompared { bool m_bSame; CCompared(CJob *_pJob, CJob *_pOther) : m_bSame(_pJob == _pOther) {} };
struct CUnseen { CUnseen(CJob *_pJob); CJob *m_pJob; };
void fg_UseJob(CJob *_pJob);
TCFuture<bool> fg_LocalCompared(CJob &_Job) { co_await fg_Work(); CJob *pLocal = &_Job; co_return pLocal == nullptr; }
TCFuture<bool> fg_AsInteger(CJob &_Job) { co_await fg_Work(); co_return reinterpret_cast<umint>(&_Job) != 0; }
TCFuture<bool> fg_LocalComparedLater(CJob &_Job) { CJob *pLocal = &_Job; co_await fg_Work(); co_return pLocal == nullptr; }
TCFuture<int> fg_LocalRead(CJob &_Job) { co_await fg_Work(); CJob *pLocal = &_Job; co_return pLocal->m_Result; }
// CHECK-MESSAGES: :[[@LINE-1]]:34: warning: TCFuture coroutine 'fg_LocalRead' uses '_Job' after a suspension, {{.*}}
TCFuture<int> fg_LocalReadLater(CJob &_Job) { CJob *pLocal = &_Job; co_await fg_Work(); co_return pLocal->m_Result; }
// CHECK-MESSAGES: :[[@LINE-1]]:39: warning: TCFuture coroutine 'fg_LocalReadLater' uses '_Job' after a suspension, {{.*}}
TCFuture<int> fg_Given(CJob &_Job) { co_await fg_Work(); fg_UseJob(&_Job); co_return 0; }
// CHECK-MESSAGES: :[[@LINE-1]]:30: warning: TCFuture coroutine 'fg_Given' uses '_Job' after a suspension, {{.*}}
TCFuture<int> fg_Aggregate(CJob *_pJob) { co_await fg_Work(); CPair Pair{_pJob, 1}; co_return Pair.m_pJob->m_Result; }
// CHECK-MESSAGES: :[[@LINE-1]]:34: warning: TCFuture coroutine 'fg_Aggregate' uses '_pJob' after a suspension, {{.*}}
TCFuture<bool> fg_ConstructorStores(CJob *_pJob) { CStored Stored(_pJob); co_await fg_Work(); co_return Stored.m_pJob == nullptr; }
TCFuture<bool> fg_ConstructorCompares(CJob *_pJob, CJob *_pOther) { co_await fg_Work(); CCompared Compared(_pJob, _pOther); co_return Compared.m_bSame; }
TCFuture<int> fg_ConstructorUnseenBefore(CJob *_pJob) { CUnseen Unseen(_pJob); co_await fg_Work(); co_return 0; }
TCFuture<int> fg_ConstructorUnseen(CJob *_pJob) { co_await fg_Work(); CUnseen Unseen(_pJob); co_return 0; }
// CHECK-MESSAGES: :[[@LINE-1]]:42: warning: TCFuture coroutine 'fg_ConstructorUnseen' uses '_pJob' after a suspension, {{.*}}
TCFuture<int> fg_RecordAssigned(CJob *_pJob) { co_await fg_Work(); CPair Pair{}; Pair = CPair{_pJob, 1}; co_return Pair.m_pJob->m_Result; }
// CHECK-MESSAGES: :[[@LINE-1]]:39: warning: TCFuture coroutine 'fg_RecordAssigned' uses '_pJob' after a suspension, {{.*}}
TCFuture<bool> fg_SelfCompared(CJob *_pJob, CJob *_pOther) { co_await fg_Work(); co_return &_pJob->f_Self() == _pOther; }
TCFuture<int> fg_SelfRead(CJob *_pJob) { co_await fg_Work(); co_return _pJob->f_Self().m_Result; }
// CHECK-MESSAGES: :[[@LINE-1]]:33: warning: TCFuture coroutine 'fg_SelfRead' uses '_pJob' after a suspension, {{.*}}
} // namespace NReview2Round198Rulings

namespace NReview2Round200Setter {
// Round 200 (llvm-coroutine-3): a setter whose body is seen and sets the
// member on every way through it (setMembers) assigns that member, so the
// value it held no longer reaches a later read. Reported: a setter that sets
// it on one way only, which adds a value without removing the old one, one
// that names its object after the assignment, which can change it again,
// and the setter given the parameter after a local.
TCFuture<int> fg_Suspend();
struct CJob { int m_Value = 0; };
struct CJobView {
 CJob const *m_pJob;
 void f_Set(CJob const *_pJob) { m_pJob = _pJob; }
 void f_SetMaybe(CJob const *_pJob, bool _bSet) { if (_bSet) m_pJob = _pJob; }
 void f_SetLogged(CJob const *_pJob) { m_pJob = _pJob; f_Log(); }
 void f_Log();
};
TCFuture<int> fg_Read(CJob const &_Job)
{
 CJob Local;
 CJobView View{&_Job};
 View.f_Set(&Local);
 co_await fg_Suspend();
 co_return View.m_pJob->m_Value;
}
TCFuture<int> fg_ReadDirect(CJob const &_Job)
{
 CJob Local;
 CJobView View{&_Job};
 View.m_pJob = &Local;
 co_await fg_Suspend();
 co_return View.m_pJob->m_Value;
}
TCFuture<int> fg_ReadMaybe(CJob const &_Job, bool _bSet)
// CHECK-MESSAGES: :[[@LINE-1]]:40: warning: TCFuture coroutine 'fg_ReadMaybe' uses '_Job' after a suspension, {{.*}}
{
 CJob Local;
 CJobView View{&_Job};
 View.f_SetMaybe(&Local, _bSet);
 co_await fg_Suspend();
 co_return View.m_pJob->m_Value;
}
TCFuture<int> fg_ReadLogged(CJob const &_Job)
// CHECK-MESSAGES: :[[@LINE-1]]:41: warning: TCFuture coroutine 'fg_ReadLogged' uses '_Job' after a suspension, {{.*}}
{
 CJob Local;
 CJobView View{&_Job};
 View.f_SetLogged(&Local);
 co_await fg_Suspend();
 co_return View.m_pJob->m_Value;
}
TCFuture<int> fg_ReadSetAfter(CJob const &_Job)
// CHECK-MESSAGES: :[[@LINE-1]]:43: warning: TCFuture coroutine 'fg_ReadSetAfter' uses '_Job' after a suspension, {{.*}}
{
 CJob Local;
 CJobView View{&Local};
 View.f_Set(&_Job);
 co_await fg_Suspend();
 co_return View.m_pJob->m_Value;
}
} // namespace NReview2Round200Setter

namespace NReview2Round202Places {
// Round 202 (llvm-coroutine-4, -5, -6, -7, -9): a place is a variable and a
// member path. A constructor whose body is seen keeps each parameter in the
// members it initializes or assigns with it; a local reference to a record is
// the record, with the same members; a setter whose body is seen called
// through a pointer to the record is a definition of the member, as through
// the record. Reported: the member that got the borrow read after the
// suspension, and a setter that sets it on one way only.
TCFuture<int> fg_Work();
TCFuture<int> fg_Suspend();
int g_Default = 0;
int g_Output = 0;
namespace NConstructor {
struct CReadContext {
	CReadContext(int *_pValue) : m_pInput(_pValue), m_pFallback(&g_Default) {}
	int *m_pInput, *m_pFallback;
};
TCFuture<int> fg_Read(int &_Value) { CReadContext Context(&_Value); co_await fg_Work(); co_return *Context.m_pFallback; }
TCFuture<int> fg_ReadInput(int &_Value) { CReadContext Context(&_Value); co_await fg_Work(); co_return *Context.m_pInput; }
// CHECK-MESSAGES: :[[@LINE-1]]:33: warning: TCFuture coroutine 'fg_ReadInput' uses '_Value' after a suspension, but it takes it as 'int &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
}
namespace NBuffers {
struct CBuffers {
	CBuffers(int *_pInput, int *_pOutput) : m_pInput(_pInput), m_pOutput(_pOutput) {}
	int *m_pInput;
	int *m_pOutput;
};
TCFuture<int> fg_Read(int &_Input) { CBuffers Buffers(&_Input, &g_Output); co_await fg_Work(); co_return *Buffers.m_pOutput; }
TCFuture<int> fg_ReadInput(int &_Input) { CBuffers Buffers(&_Input, &g_Output); co_await fg_Work(); co_return *Buffers.m_pInput; }
// CHECK-MESSAGES: :[[@LINE-1]]:33: warning: TCFuture coroutine 'fg_ReadInput' uses '_Input' after a suspension, but it takes it as 'int &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
}
namespace NBraced {
struct CReadContext {
	CReadContext(int const *_pInput) : m_pInput(_pInput), m_pDefault(&g_Default) {}
	int const *m_pInput;
	int const *m_pDefault;
};
TCFuture<int> fg_Read(int const &_Input) { CReadContext Context{&_Input}; co_await fg_Work(); co_return *Context.m_pDefault; }
TCFuture<int> fg_ReadInput(int const &_Input) { CReadContext Context{&_Input}; co_await fg_Work(); co_return *Context.m_pInput; }
// CHECK-MESSAGES: :[[@LINE-1]]:39: warning: TCFuture coroutine 'fg_ReadInput' uses '_Input' after a suspension, but it takes it as 'const int &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
}
namespace NAlias {
struct CReadContext {
	int const *m_pInput;
	int const *m_pDefault;
};
TCFuture<int> fg_Read(int const &_Input) { CReadContext Context{&_Input, &g_Default}; auto &Alias = Context; co_await fg_Work(); co_return *Alias.m_pDefault; }
TCFuture<int> fg_ReadInput(int const &_Input) { CReadContext Context{&_Input, &g_Default}; auto &Alias = Context; co_await fg_Work(); co_return *Alias.m_pInput; }
// CHECK-MESSAGES: :[[@LINE-1]]:39: warning: TCFuture coroutine 'fg_ReadInput' uses '_Input' after a suspension, but it takes it as 'const int &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
}
namespace NAliasAssigned {
struct CJob { int m_Value = 0; };
struct CReadContext { CJob *m_pJob; };
TCFuture<int> fg_Read(CJob *_pJob) { CJob Local{}; CReadContext Context{_pJob}; auto &Current = Context; Current.m_pJob = &Local; co_await fg_Suspend(); co_return Current.m_pJob->m_Value; }
TCFuture<int> fg_ReadKept(CJob *_pJob) { CReadContext Context{_pJob}; auto &Current = Context; co_await fg_Suspend(); co_return Current.m_pJob->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:33: warning: TCFuture coroutine 'fg_ReadKept' uses '_pJob' after a suspension, but it takes it as 'CJob *', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
TCFuture<int> fg_ReadAssignedAfter(CJob *_pJob) { CJob Local{}; CReadContext Context{_pJob}; auto &Current = Context; co_await fg_Suspend(); int Value = Current.m_pJob->m_Value; Current.m_pJob = &Local; co_return Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:42: warning: TCFuture coroutine 'fg_ReadAssignedAfter' uses '_pJob' after a suspension, but it takes it as 'CJob *', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
}
namespace NSetterThroughPointer {
struct CJob { int m_Value = 0; };
struct CJobView {
	CJob const *m_pJob;
	void f_Set(CJob const *_pJob) { m_pJob = _pJob; }
	void f_SetMaybe(CJob const *_pJob, bool _bSet) { if (_bSet) m_pJob = _pJob; }
};
TCFuture<int> fg_Read(CJob const &_Job) { CJob Local{}; CJobView View{&_Job}; auto *pView = &View; pView->f_Set(&Local); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
TCFuture<int> fg_ReadMaybe(CJob const &_Job, bool _bSet) { CJob Local{}; CJobView View{&_Job}; auto *pView = &View; pView->f_SetMaybe(&Local, _bSet); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:40: warning: TCFuture coroutine 'fg_ReadMaybe' uses '_Job' after a suspension, but it takes it as 'const CJob &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
}
} // namespace NReview2Round202Places

namespace NSummarySetters {
// A member function whose body is seen leaves in the members of its object
// what its flow has there where it returns, also through the member
// functions it calls: a member set on every way through it holds that value
// after the call, so what it held before no longer reaches a later read; also
// where another member is changed after the set, where both branches of an
// `if` set it, of a view member, and through `(*this)`. Reported: the same
// setters setting it on one way only, which adds a value without removing the
// old one.
TCFuture<int> fg_Suspend();
struct CJob { int m_Value = 0; };
struct CJobView {
	CJob const *m_pJob;
	int m_nSets = 0;
	void f_SetCounted(CJob const *_pJob) { m_pJob = _pJob; ++m_nSets; }
	void f_SetCountedMaybe(CJob const *_pJob, bool _bSet) { if (_bSet) m_pJob = _pJob; ++m_nSets; }
	void f_SetEither(CJob const *_pFirst, CJob const *_pSecond, bool _bFirst) { if (_bFirst) m_pJob = _pFirst; else m_pJob = _pSecond; }
	void f_SetEitherMaybe(CJob const *_pFirst, CJob const *_pSecond, bool _bFirst, bool _bSet) { if (_bFirst) m_pJob = _pFirst; else if (_bSet) m_pJob = _pSecond; }
	void f_SetThis(CJob const *_pJob) { (*this).m_pJob = _pJob; }
	void f_SetThisMaybe(CJob const *_pJob, bool _bSet) { if (_bSet) (*this).m_pJob = _pJob; }
};
TCFuture<int> fg_ReadCounted(CJob const &_Job) { CJob Local; CJobView View{&_Job}; View.f_SetCounted(&Local); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
TCFuture<int> fg_ReadCountedMaybe(CJob const &_Job, bool _bSet) { CJob Local; CJobView View{&_Job}; View.f_SetCountedMaybe(&Local, _bSet); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:47: warning: TCFuture coroutine 'fg_ReadCountedMaybe' uses '_Job' after a suspension, {{.*}}
TCFuture<int> fg_ReadEither(CJob const &_Job, bool _bFirst) { CJob First, Second; CJobView View{&_Job}; View.f_SetEither(&First, &Second, _bFirst); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
TCFuture<int> fg_ReadEitherMaybe(CJob const &_Job, bool _bFirst, bool _bSet) { CJob First, Second; CJobView View{&_Job}; View.f_SetEitherMaybe(&First, &Second, _bFirst, _bSet); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:46: warning: TCFuture coroutine 'fg_ReadEitherMaybe' uses '_Job' after a suspension, {{.*}}
TCFuture<int> fg_ReadThis(CJob const &_Job) { CJob Local; CJobView View{&_Job}; View.f_SetThis(&Local); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
TCFuture<int> fg_ReadThisMaybe(CJob const &_Job, bool _bSet) { CJob Local; CJobView View{&_Job}; View.f_SetThisMaybe(&Local, _bSet); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:44: warning: TCFuture coroutine 'fg_ReadThisMaybe' uses '_Job' after a suspension, {{.*}}
struct CTextHolder {
	CStrSpan m_Text;
	void f_Set(CStrSpan _Text) { m_Text = _Text; }
	void f_SetMaybe(CStrSpan _Text, bool _bSet) { if (_bSet) m_Text = _Text; }
};
char const g_Default[] = "Default";
TCFuture<int> fg_ReadText(CStrSpan _Text) { CTextHolder Holder{_Text}; Holder.f_Set(CStrSpan{g_Default}); co_await fg_Suspend(); co_return Holder.m_Text.m_pData[0]; }
TCFuture<int> fg_ReadTextMaybe(CStrSpan _Text, bool _bSet) { CTextHolder Holder{_Text}; Holder.f_SetMaybe(CStrSpan{g_Default}, _bSet); co_await fg_Suspend(); co_return Holder.m_Text.m_pData[0]; }
// CHECK-MESSAGES: :[[@LINE-1]]:41: warning: TCFuture coroutine 'fg_ReadTextMaybe' uses '_Text' after a suspension, {{.*}}
} // namespace NSummarySetters

namespace NSummaryRecursion {
// A member function that calls itself, or another that calls it back, sets a
// member on every way through it where each way that doesn't call it again
// sets it: a way through the call of itself is one it doesn't take, by
// induction. That holds whichever is asked first, its summary (fg_Rec) or its
// flow (fg_First asks the flow of f_SetRecGet through what fg_Swap gives
// back, before fg_RecGet asks its summary). Reported: the same with a way
// that leaves without setting it. A recursion whose values only grow each
// pass settles, however many passes that takes (f_Rotate10, a new value each
// pass, and f_Rotate3 are clean); reported: the same rotation with a way that
// leaves without setting it (f_Rotate10Maybe).
TCFuture<int> fg_Suspend();
struct CJob { int m_Value = 0; };
struct CJobView {
	CJob const *m_pJob;
	void f_SetRec(CJob const *_pJob, int _nDepth) { if (_nDepth == 0) { m_pJob = _pJob; return; } f_SetRec(_pJob, _nDepth - 1); }
	void f_SetRecMaybe(CJob const *_pJob, int _nDepth) { if (_nDepth == 0) return; if (_nDepth == 1) { m_pJob = _pJob; return; } f_SetRecMaybe(_pJob, _nDepth - 1); }
	void f_SetA(CJob const *_pJob, int _nDepth);
	void f_SetB(CJob const *_pJob, int _nDepth) { if (_nDepth == 0) { m_pJob = _pJob; return; } f_SetA(_pJob, _nDepth - 1); }
	void f_SetMaybeA(CJob const *_pJob, int _nDepth);
	void f_SetMaybeB(CJob const *_pJob, int _nDepth) { if (_nDepth == 0) return; f_SetMaybeA(_pJob, _nDepth - 1); }
	CJob const *f_SetRecGet(CJob const *_pJob, int _nDepth) { CJob const *pOld = m_pJob; if (_nDepth == 0) { m_pJob = _pJob; return pOld; } return f_SetRecGet(_pJob, _nDepth - 1); }
	CJob const *f_SetRecGetMaybe(CJob const *_pJob, int _nDepth) { CJob const *pOld = m_pJob; if (_nDepth == 0) return pOld; if (_nDepth == 1) { m_pJob = _pJob; return pOld; } return f_SetRecGetMaybe(_pJob, _nDepth - 1); }
	void f_Rotate3(CJob const *_pA, CJob const *_pB, CJob const *_pC, int _nDepth) { if (_nDepth == 0) { m_pJob = _pA; return; } f_Rotate3(_pB, _pC, _pA, _nDepth - 1); }
	void f_Rotate10(CJob const *_p0, CJob const *_p1, CJob const *_p2, CJob const *_p3, CJob const *_p4, CJob const *_p5, CJob const *_p6, CJob const *_p7, CJob const *_p8, CJob const *_p9, int _nDepth) {
		if (_nDepth == 0) { m_pJob = _p0; return; }
		f_Rotate10(_p1, _p2, _p3, _p4, _p5, _p6, _p7, _p8, _p9, _p0, _nDepth - 1);
	}
	void f_Rotate10Maybe(CJob const *_p0, CJob const *_p1, CJob const *_p2, CJob const *_p3, CJob const *_p4, CJob const *_p5, CJob const *_p6, CJob const *_p7, CJob const *_p8, CJob const *_p9, int _nDepth) {
		if (_nDepth == 0) { m_pJob = _p0; return; }
		if (_nDepth == 1) return;
		f_Rotate10Maybe(_p1, _p2, _p3, _p4, _p5, _p6, _p7, _p8, _p9, _p0, _nDepth - 1);
	}
};
void CJobView::f_SetA(CJob const *_pJob, int _nDepth) { if (_nDepth == 0) { m_pJob = _pJob; return; } f_SetB(_pJob, _nDepth - 1); }
void CJobView::f_SetMaybeA(CJob const *_pJob, int _nDepth) { if (_nDepth == 0) { m_pJob = _pJob; return; } f_SetMaybeB(_pJob, _nDepth - 1); }
CJob const *fg_Swap(CJobView &_View, CJob const *_pJob) { return _View.f_SetRecGet(_pJob, 3); }
CJob const *fg_SwapMaybe(CJobView &_View, CJob const *_pJob) { return _View.f_SetRecGetMaybe(_pJob, 3); }
TCFuture<int> fg_First(CJob const &_Job) { CJob Local; CJobView View{&_Job}; CJob const *pOld = fg_Swap(View, &Local); co_await fg_Suspend(); co_return pOld->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:36: warning: TCFuture coroutine 'fg_First' uses '_Job' after a suspension, {{.*}}
TCFuture<int> fg_FirstMaybe(CJob const &_Job) { CJob Local; CJobView View{&_Job}; CJob const *pOld = fg_SwapMaybe(View, &Local); co_await fg_Suspend(); co_return pOld->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:41: warning: TCFuture coroutine 'fg_FirstMaybe' uses '_Job' after a suspension, {{.*}}
TCFuture<int> fg_RecGet(CJob const &_Job) { CJob Local; CJobView View{&_Job}; View.f_SetRecGet(&Local, 3); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
TCFuture<int> fg_RecGetMaybe(CJob const &_Job) { CJob Local; CJobView View{&_Job}; View.f_SetRecGetMaybe(&Local, 3); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:42: warning: TCFuture coroutine 'fg_RecGetMaybe' uses '_Job' after a suspension, {{.*}}
TCFuture<int> fg_Rec(CJob const &_Job) { CJob Local; CJobView View{&_Job}; View.f_SetRec(&Local, 3); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
TCFuture<int> fg_RecMaybe(CJob const &_Job) { CJob Local; CJobView View{&_Job}; View.f_SetRecMaybe(&Local, 3); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:39: warning: TCFuture coroutine 'fg_RecMaybe' uses '_Job' after a suspension, {{.*}}
TCFuture<int> fg_Mutual(CJob const &_Job) { CJob Local; CJobView View{&_Job}; View.f_SetA(&Local, 3); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
TCFuture<int> fg_MutualMaybe(CJob const &_Job) { CJob Local; CJobView View{&_Job}; View.f_SetMaybeA(&Local, 3); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:42: warning: TCFuture coroutine 'fg_MutualMaybe' uses '_Job' after a suspension, {{.*}}
TCFuture<int> fg_Rotate3(CJob const &_Job) { CJob A, B, C; CJobView View{&_Job}; View.f_Rotate3(&A, &B, &C, 5); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
TCFuture<int> fg_Rotate10(CJob const &_Job) { CJob A[10]; CJobView View{&_Job}; View.f_Rotate10(&A[0], &A[1], &A[2], &A[3], &A[4], &A[5], &A[6], &A[7], &A[8], &A[9], 5); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
TCFuture<int> fg_Rotate10Maybe(CJob const &_Job) { CJob A[10]; CJobView View{&_Job}; View.f_Rotate10Maybe(&A[0], &A[1], &A[2], &A[3], &A[4], &A[5], &A[6], &A[7], &A[8], &A[9], 5); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:44: warning: TCFuture coroutine 'fg_Rotate10Maybe' uses '_Job' after a suspension, {{.*}}
} // namespace NSummaryRecursion

namespace NSummaryRecursionMutual {
// Two member functions that call each other with their parameters in another
// order, of different counts, settle where the values only grow each pass,
// however many passes that takes: set on every way. Reported: the same with a
// way that leaves without setting it.
TCFuture<int> fg_Suspend();
struct CJob { int m_Value = 0; };
struct CJobView {
	CJob const *m_pJob;
	void f_RotA(CJob const *_p0, CJob const *_p1, CJob const *_p2, CJob const *_p3, CJob const *_p4, CJob const *_p5, CJob const *_p6, CJob const *_p7, CJob const *_p8, CJob const *_p9, CJob const *_p10, CJob const *_p11, int _nDepth);
	void f_RotB(CJob const *_q0, CJob const *_q1, CJob const *_q2, CJob const *_q3, CJob const *_q4, CJob const *_q5, CJob const *_q6, CJob const *_q7, CJob const *_q8, CJob const *_q9, CJob const *_q10, CJob const *_q11, CJob const *_q12, int _nDepth, bool _bFlag);
	void f_RotMaybeA(CJob const *_p0, CJob const *_p1, CJob const *_p2, CJob const *_p3, CJob const *_p4, CJob const *_p5, CJob const *_p6, CJob const *_p7, CJob const *_p8, CJob const *_p9, CJob const *_p10, CJob const *_p11, int _nDepth);
	void f_RotMaybeB(CJob const *_q0, CJob const *_q1, CJob const *_q2, CJob const *_q3, CJob const *_q4, CJob const *_q5, CJob const *_q6, CJob const *_q7, CJob const *_q8, CJob const *_q9, CJob const *_q10, CJob const *_q11, CJob const *_q12, int _nDepth, bool _bFlag);
};
void CJobView::f_RotA(CJob const *_p0, CJob const *_p1, CJob const *_p2, CJob const *_p3, CJob const *_p4, CJob const *_p5, CJob const *_p6, CJob const *_p7, CJob const *_p8, CJob const *_p9, CJob const *_p10, CJob const *_p11, int _nDepth) {
	if (_nDepth == 0) { m_pJob = _p0; return; }
	f_RotB(_p1, _p2, _p3, _p4, _p5, _p6, _p7, _p8, _p9, _p10, _p11, _p0, _p0, _nDepth - 1, true);
}
void CJobView::f_RotB(CJob const *_q0, CJob const *_q1, CJob const *_q2, CJob const *_q3, CJob const *_q4, CJob const *_q5, CJob const *_q6, CJob const *_q7, CJob const *_q8, CJob const *_q9, CJob const *_q10, CJob const *_q11, CJob const *_q12, int _nDepth, bool _bFlag) {
	if (_nDepth == 0) { m_pJob = _q0; return; }
	f_RotA(_q1, _q2, _q3, _q4, _q5, _q6, _q7, _q8, _q9, _q10, _q11, _q12, _nDepth - 1);
}
void CJobView::f_RotMaybeA(CJob const *_p0, CJob const *_p1, CJob const *_p2, CJob const *_p3, CJob const *_p4, CJob const *_p5, CJob const *_p6, CJob const *_p7, CJob const *_p8, CJob const *_p9, CJob const *_p10, CJob const *_p11, int _nDepth) {
	if (_nDepth == 0) { m_pJob = _p0; return; }
	f_RotMaybeB(_p1, _p2, _p3, _p4, _p5, _p6, _p7, _p8, _p9, _p10, _p11, _p0, _p0, _nDepth - 1, true);
}
void CJobView::f_RotMaybeB(CJob const *_q0, CJob const *_q1, CJob const *_q2, CJob const *_q3, CJob const *_q4, CJob const *_q5, CJob const *_q6, CJob const *_q7, CJob const *_q8, CJob const *_q9, CJob const *_q10, CJob const *_q11, CJob const *_q12, int _nDepth, bool _bFlag) {
	if (_nDepth == 0) { m_pJob = _q0; return; }
	if (_nDepth == 1) return;
	f_RotMaybeA(_q1, _q2, _q3, _q4, _q5, _q6, _q7, _q8, _q9, _q10, _q11, _q12, _nDepth - 1);
}
TCFuture<int> fg_RotMutual(CJob const &_Job) { CJob A[12]; CJobView View{&_Job}; View.f_RotA(&A[0], &A[1], &A[2], &A[3], &A[4], &A[5], &A[6], &A[7], &A[8], &A[9], &A[10], &A[11], 5); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
TCFuture<int> fg_RotMutualMaybe(CJob const &_Job) { CJob A[12]; CJobView View{&_Job}; View.f_RotMaybeA(&A[0], &A[1], &A[2], &A[3], &A[4], &A[5], &A[6], &A[7], &A[8], &A[9], &A[10], &A[11], 5); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:45: warning: TCFuture coroutine 'fg_RotMutualMaybe' uses '_Job' after a suspension, {{.*}}
} // namespace NSummaryRecursionMutual

namespace NSummaryAlias {
// A write through what another parameter points to, of the same member of an
// object of the same type, can be a write of the object itself: what it
// writes there is what the member can hold after the call, only maybe, so a
// setter that sets the member and then writes it through such a pointer
// doesn't set it on every way to its own value. So does a write of a whole
// object of the type that holds the member (f_ResetWhole), and a write whose
// place can't be told, through a pointer an unseen function gives
// (f_ResetUnseen), of an object whose address unseen code can reach.
// Reported: f_Reset given the object itself puts back the saved borrow,
// f_ResetWhole given it puts back a copy of the object made before the call,
// f_ResetUnseen's function can give the object itself where its address is
// in a global (fg_AliasUnseenEscaped), which gets back what that copy held.
// Clean: the same with the parameter stated distinct (DMibDistinct) given
// another object, a write through a local pointer to a local view
// (f_ResetLocal), and f_ResetUnseen of a local whose address doesn't escape,
// which no unseen function can give (fg_AliasUnseen).
TCFuture<int> fg_Suspend();
struct CJob { int m_Value = 0; };
CJob g_Job;
struct CView {
	CJob const *m_pJob = nullptr;
	CJob const *m_pSaved = nullptr;
	void f_Reset(CJob const *_pJob, CView *_pOther) { m_pJob = _pJob; _pOther->m_pJob = _pOther->m_pSaved; }
	void f_ResetDistinct(CJob const *_pJob, DMibDistinct CView *_pOther) { m_pJob = _pJob; _pOther->m_pJob = _pOther->m_pSaved; }
	void f_ResetWhole(CJob const *_pJob, CView *_pOther, CView _Old) { m_pJob = _pJob; *_pOther = _Old; }
	void f_ResetWholeDistinct(CJob const *_pJob, DMibDistinct CView *_pOther, CView _Old) { m_pJob = _pJob; *_pOther = _Old; }
	void f_ResetUnseen(CJob const *_pJob, CView _Old);
	void f_ResetLocal(CJob const *_pJob, CView _Old) { m_pJob = _pJob; CView Other; CView *pOther = &Other; pOther->m_pJob = _Old.m_pJob; }
};
CView *fg_OtherView();
void CView::f_ResetUnseen(CJob const *_pJob, CView _Old) { m_pJob = _pJob; fg_OtherView()->m_pJob = _Old.m_pJob; }
TCFuture<int> fg_Alias(CJob const &_Job) { CView View; View.m_pJob = &_Job; View.m_pSaved = &_Job; View.f_Reset(&g_Job, &View); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:36: warning: TCFuture coroutine 'fg_Alias' uses '_Job' after a suspension, {{.*}}
TCFuture<int> fg_AliasDistinct(CJob const &_Job) { CView View, Other; View.m_pJob = &_Job; View.f_ResetDistinct(&g_Job, &Other); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
TCFuture<int> fg_AliasWhole(CJob const &_Job) { CView View; View.m_pJob = &_Job; View.f_ResetWhole(&g_Job, &View, View); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:41: warning: TCFuture coroutine 'fg_AliasWhole' uses '_Job' after a suspension, {{.*}}
TCFuture<int> fg_AliasWholeDistinct(CJob const &_Job) { CView View, Other; View.m_pJob = &_Job; View.f_ResetWholeDistinct(&g_Job, &Other, View); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
TCFuture<int> fg_AliasUnseen(CJob const &_Job) { CView View; View.m_pJob = &_Job; View.f_ResetUnseen(&g_Job, View); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
CView *g_pView;
TCFuture<int> fg_AliasUnseenEscaped(CJob const &_Job) { CView View; g_pView = &View; View.m_pJob = &_Job; View.f_ResetUnseen(&g_Job, View); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:49: warning: TCFuture coroutine 'fg_AliasUnseenEscaped' uses '_Job' after a suspension, {{.*}}
TCFuture<int> fg_AliasLocal(CJob const &_Job) { CView View; View.m_pJob = &_Job; View.f_ResetLocal(&g_Job, View); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
} // namespace NSummaryAlias

namespace NSummarySetParameters {
// What a function whose body is seen sets a pointer it is given by reference
// or by its address to is what its flow has there where it returns: set on
// every way, on both branches of an `if`, through a cast, through a local
// reference to it, or through another such function, it replaces what the
// pointer held. Reported: the same set on one way only, which adds a value.
TCFuture<int> fg_Suspend();
struct CJob { int m_Value = 0; };
void fg_SetEither(CJob const *&o_pJob, CJob const *_pFirst, CJob const *_pSecond, bool _bFirst) { if (_bFirst) o_pJob = _pFirst; else o_pJob = _pSecond; }
void fg_SetOne(CJob const *&o_pJob, CJob const *_pFirst, bool _bFirst) { if (_bFirst) o_pJob = _pFirst; }
void fg_SetCast(CJob const *&o_pJob, void const *_pJob) { o_pJob = static_cast<CJob const *>(_pJob); }
void fg_SetAlias(CJob const **o_ppJob, CJob const *_pJob) { CJob const *&rpJob = *o_ppJob; rpJob = _pJob; }
void fg_SetThrough(CJob const *&o_pJob, CJob const *_pJob) { fg_SetEither(o_pJob, _pJob, _pJob, true); }
TCFuture<int> fg_ReadEither(CJob const &_Job, bool _bFirst) { CJob First, Second; CJob const *pJob = &_Job; fg_SetEither(pJob, &First, &Second, _bFirst); co_await fg_Suspend(); co_return pJob->m_Value; }
TCFuture<int> fg_ReadOne(CJob const &_Job, bool _bFirst) { CJob First; CJob const *pJob = &_Job; fg_SetOne(pJob, &First, _bFirst); co_await fg_Suspend(); co_return pJob->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:38: warning: TCFuture coroutine 'fg_ReadOne' uses '_Job' after a suspension, {{.*}}
TCFuture<int> fg_ReadCast(CJob const &_Job) { CJob Local; CJob const *pJob = &_Job; fg_SetCast(pJob, &Local); co_await fg_Suspend(); co_return pJob->m_Value; }
TCFuture<int> fg_ReadAlias(CJob const &_Job) { CJob Local; CJob const *pJob = &_Job; fg_SetAlias(&pJob, &Local); co_await fg_Suspend(); co_return pJob->m_Value; }
TCFuture<int> fg_ReadThrough(CJob const &_Job) { CJob Local; CJob const *pJob = &_Job; fg_SetThrough(pJob, &Local); co_await fg_Suspend(); co_return pJob->m_Value; }
} // namespace NSummarySetParameters

namespace NSummaryParameterRepointed {
// What a pointer parameter points to is a place of its function only where
// no use of a name of it gives it another value, also through parentheses, a
// cast to a reference or a conditional, which the uses of the name lead to.
// Reported: the parameter repointed to a local before what it points to is
// set, so the caller's pointer keeps the borrow. Clean: a cast that only
// reads it.
TCFuture<int> fg_Suspend();
struct CJob { int m_Value = 0; };
CJob g_Job;
void fg_SetParen(CJob const **o_ppJob, CJob const *_pJob) { CJob const *pLocal = nullptr; (o_ppJob) = &pLocal; *o_ppJob = _pJob; }
void fg_SetCastRef(CJob const **o_ppJob, CJob const *_pJob) { CJob const *pLocal = nullptr; static_cast<CJob const **&>(o_ppJob) = &pLocal; *o_ppJob = _pJob; }
void fg_SetChoice(CJob const **o_ppJob, CJob const **_ppOther, CJob const *_pJob, bool _b) { CJob const *pLocal = nullptr; (_b ? o_ppJob : _ppOther) = &pLocal; *o_ppJob = _pJob; }
void fg_SetCastRead(CJob const **o_ppJob, CJob const *_pJob) { *static_cast<CJob const **>((o_ppJob)) = _pJob; }
TCFuture<int> fg_ReadParen(CJob const &_Job) { CJob const *pJob = &_Job; fg_SetParen(&pJob, &g_Job); co_await fg_Suspend(); co_return pJob->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:40: warning: TCFuture coroutine 'fg_ReadParen' uses '_Job' after a suspension, {{.*}}
TCFuture<int> fg_ReadCastRef(CJob const &_Job) { CJob const *pJob = &_Job; fg_SetCastRef(&pJob, &g_Job); co_await fg_Suspend(); co_return pJob->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:42: warning: TCFuture coroutine 'fg_ReadCastRef' uses '_Job' after a suspension, {{.*}}
TCFuture<int> fg_ReadChoice(CJob const &_Job, bool _b) { CJob const *pJob = &_Job; CJob const *pOther = nullptr; fg_SetChoice(&pJob, &pOther, &g_Job, _b); co_await fg_Suspend(); co_return pJob->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:41: warning: TCFuture coroutine 'fg_ReadChoice' uses '_Job' after a suspension, {{.*}}
TCFuture<int> fg_ReadCastRead(CJob const &_Job) { CJob const *pJob = &_Job; fg_SetCastRead(&pJob, &g_Job); co_await fg_Suspend(); co_return pJob->m_Value; }
} // namespace NSummaryParameterRepointed

namespace NSummaryAliasTypes {
// A write of another place that can be the same object, or one that holds
// it, gives the member what it writes there, only maybe: an object of a base
// class of the one that holds the member (f_ResetBase), of a class derived
// from it (f_ResetDerived), one that holds it by value (f_ResetOuter), and
// what can be any object's bytes, through a `void` pointer an unseen function
// gives (f_ResetBytes), of an object whose address unseen code can reach.
// Reported: each given the object itself, or what can be it, which puts back
// the borrow a copy made before held (f_ResetBytes where the object's address
// is in a global, fg_BytesEscaped). Clean: the base stated distinct
// (DMibDistinct), writes of what can't hold the member, an `int` through a
// member pointer (f_ResetCount) and through what an unseen function gives
// (f_ResetInts), and f_ResetBytes of a local whose address doesn't escape,
// which no unseen function can give (fg_Bytes). A member that points to the object itself,
// `m_pSelf->m_pJob`, can be the member as well (f_ResetSelf, reported; the
// object's address in its own member is also one the flow can't follow).
TCFuture<int> fg_Suspend();
struct CJob { int m_Value = 0; };
CJob g_Job;
struct COuter;
struct CBase { CJob const *m_pJob = nullptr; };
struct CView : CBase {
	CJob const *m_pSaved = nullptr;
	CView *m_pSelf = this;
	int *m_pCount = nullptr;
	void f_ResetBase(CJob const *_pJob, CBase *_pOther, CBase _Old) { m_pJob = _pJob; *_pOther = _Old; }
	void f_ResetBaseDistinct(CJob const *_pJob, DMibDistinct CBase *_pOther, CBase _Old) { m_pJob = _pJob; *_pOther = _Old; }
	void f_ResetOuter(CJob const *_pJob, COuter *_pOuter, COuter const &_Old);
	void f_ResetSelf(CJob const *_pJob) { m_pJob = _pJob; m_pSelf->m_pJob = m_pSaved; }
	void f_ResetCount(CJob const *_pJob) { m_pJob = _pJob; *m_pCount = 0; }
	void f_ResetBytes(CJob const *_pJob, CView const *_pOld);
	void f_ResetInts(CJob const *_pJob);
};
struct COuter { CView m_Inner; };
void CView::f_ResetOuter(CJob const *_pJob, COuter *_pOuter, COuter const &_Old) { m_pJob = _pJob; *_pOuter = _Old; }
void fg_CopyBytes(void *_pTo, void const *_pFrom, unsigned long _nBytes);
void *fg_OtherBytes();
int *fg_OtherInts();
void CView::f_ResetBytes(CJob const *_pJob, CView const *_pOld) { m_pJob = _pJob; fg_CopyBytes(fg_OtherBytes(), _pOld, sizeof(CView)); }
void CView::f_ResetInts(CJob const *_pJob) { m_pJob = _pJob; *fg_OtherInts() = 0; }
struct CDerivedView;
struct CBaseSetter {
	CJob const *m_pJob = nullptr;
	void f_ResetDerived(CJob const *_pJob, CDerivedView *_pOther, CDerivedView const &_Old);
};
struct CDerivedView : CBaseSetter { CJob const *m_pSaved = nullptr; };
void CBaseSetter::f_ResetDerived(CJob const *_pJob, CDerivedView *_pOther, CDerivedView const &_Old) { m_pJob = _pJob; *_pOther = _Old; }
TCFuture<int> fg_Base(CJob const &_Job) { CView View; View.m_pJob = &_Job; View.f_ResetBase(&g_Job, &View, View); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:35: warning: TCFuture coroutine 'fg_Base' uses '_Job' after a suspension, {{.*}}
TCFuture<int> fg_BaseDistinct(CJob const &_Job) { CView View; CBase Other; View.m_pJob = &_Job; View.f_ResetBaseDistinct(&g_Job, &Other, View); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
TCFuture<int> fg_Outer(CJob const &_Job) { COuter Outer; Outer.m_Inner.m_pJob = &_Job; COuter Saved = Outer; Outer.m_Inner.f_ResetOuter(&g_Job, &Outer, Saved); co_await fg_Suspend(); co_return Outer.m_Inner.m_pJob->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:36: warning: TCFuture coroutine 'fg_Outer' uses '_Job' after a suspension, {{.*}}
TCFuture<int> fg_Self(CJob const &_Job) { CView View; View.m_pJob = &_Job; View.m_pSaved = &_Job; View.f_ResetSelf(&g_Job); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:35: warning: TCFuture coroutine 'fg_Self' uses '_Job' after a suspension, {{.*}}
TCFuture<int> fg_Count(CJob const &_Job) { int nCount = 0; CView View; View.m_pCount = &nCount; View.m_pJob = &_Job; View.f_ResetCount(&g_Job); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
TCFuture<int> fg_Bytes(CJob const &_Job) { CView View; View.m_pJob = &_Job; CView Old = View; View.f_ResetBytes(&g_Job, &Old); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
CView *g_pView;
TCFuture<int> fg_BytesEscaped(CJob const &_Job) { CView View; g_pView = &View; View.m_pJob = &_Job; CView Old = View; View.f_ResetBytes(&g_Job, &Old); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:43: warning: TCFuture coroutine 'fg_BytesEscaped' uses '_Job' after a suspension, {{.*}}
TCFuture<int> fg_Ints(CJob const &_Job) { CView View; View.m_pJob = &_Job; View.f_ResetInts(&g_Job); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
TCFuture<int> fg_Derived(CJob const &_Job) { CDerivedView View; View.m_pJob = &_Job; CDerivedView Old = View; View.f_ResetDerived(&g_Job, &View, Old); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:38: warning: TCFuture coroutine 'fg_Derived' uses '_Job' after a suspension, {{.*}}
} // namespace NSummaryAliasTypes

namespace NSummaryReferent {
// What a function whose body is seen leaves in a member of what a reference
// or a pointer parameter refers to is what its flow has there where it
// returns, as of the members of its object: set on every way it replaces
// what the member held (fg_Set, fg_SetPointer, fg_SetEither, fg_SetRec), on
// one way only it adds a value (fg_SetMaybe, fg_SetRecMaybe, reported). Of
// two parameters given the same object, what each leaves is what the other
// writes too (fg_Same, reported; fg_SameOther, clean). What a pointer
// parameter points to whose value goes where the flow can't follow it can
// be written by code that can't be seen after it is set (fg_SetEscaped,
// reported), and a pointer parameter given another value first sets another
// object (fg_SetMoved, reported).
TCFuture<int> fg_Suspend();
struct CJob { int m_Value = 0; };
CJob g_Job;
struct CView { CJob const *m_pJob = nullptr; CJob const *m_pSaved = nullptr; };
CView *g_pSaved;
void fg_Fire();
void fg_Set(CView &_View, CJob const *_pJob) { _View.m_pJob = _pJob; }
void fg_SetPointer(CView *_pView, CJob const *_pJob) { _pView->m_pJob = _pJob; }
void fg_SetEither(CView &_View, CJob const *_pFirst, CJob const *_pSecond, bool _bFirst) { if (_bFirst) _View.m_pJob = _pFirst; else _View.m_pJob = _pSecond; }
void fg_SetMaybe(CView &_View, CJob const *_pJob, bool _bSet) { if (_bSet) _View.m_pJob = _pJob; }
void fg_SetRec(CView &_View, CJob const *_pJob, int _nDepth) { if (_nDepth == 0) { _View.m_pJob = _pJob; return; } fg_SetRec(_View, _pJob, _nDepth - 1); }
void fg_SetRecMaybe(CView &_View, CJob const *_pJob, int _nDepth) { if (_nDepth > 0) fg_SetRecMaybe(_View, _pJob, _nDepth - 1); else if (_nDepth == 0) _View.m_pJob = _pJob; }
void fg_SetBoth(CView &_First, CView &_Second, CJob const *_pJob, CJob const *_pOld) { _First.m_pJob = _pJob; _Second.m_pJob = _pOld; }
void fg_SetEscaped(CView *_pView, CJob const *_pJob) { g_pSaved = _pView; _pView->m_pJob = _pJob; fg_Fire(); }
void fg_SetMoved(CView *_pView, CView *_pOther, CJob const *_pJob) { _pView = _pOther; _pView->m_pJob = _pJob; }
TCFuture<int> fg_Run(CJob const &_Job) { CView View; View.m_pJob = &_Job; fg_Set(View, &g_Job); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
TCFuture<int> fg_RunPointer(CJob const &_Job) { CView View; View.m_pJob = &_Job; fg_SetPointer(&View, &g_Job); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
TCFuture<int> fg_RunEither(CJob const &_Job, bool _bFirst) { CView View; View.m_pJob = &_Job; fg_SetEither(View, &g_Job, &g_Job, _bFirst); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
TCFuture<int> fg_RunMaybe(CJob const &_Job, bool _bSet) { CView View; View.m_pJob = &_Job; fg_SetMaybe(View, &g_Job, _bSet); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:39: warning: TCFuture coroutine 'fg_RunMaybe' uses '_Job' after a suspension, {{.*}}
TCFuture<int> fg_RunRec(CJob const &_Job) { CView View; View.m_pJob = &_Job; fg_SetRec(View, &g_Job, 3); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
TCFuture<int> fg_RunRecMaybe(CJob const &_Job) { CView View; View.m_pJob = &_Job; fg_SetRecMaybe(View, &g_Job, 3); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:42: warning: TCFuture coroutine 'fg_RunRecMaybe' uses '_Job' after a suspension, {{.*}}
TCFuture<int> fg_Same(CJob const &_Job) { CView View; View.m_pJob = &g_Job; fg_SetBoth(View, View, &g_Job, &_Job); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:35: warning: TCFuture coroutine 'fg_Same' uses '_Job' after a suspension, {{.*}}
TCFuture<int> fg_SameOther(CJob const &_Job) { CView View, Other; View.m_pJob = &_Job; fg_SetBoth(View, Other, &g_Job, &_Job); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
TCFuture<int> fg_RunEscaped(CJob const &_Job) { CView View; View.m_pJob = &_Job; fg_SetEscaped(&View, &g_Job); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:41: warning: TCFuture coroutine 'fg_RunEscaped' uses '_Job' after a suspension, {{.*}}
TCFuture<int> fg_RunMoved(CJob const &_Job) { CView View, Other; View.m_pJob = &_Job; fg_SetMoved(&View, &Other, &g_Job); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:39: warning: TCFuture coroutine 'fg_RunMoved' uses '_Job' after a suspension, {{.*}}
} // namespace NSummaryReferent

namespace NSummaryEscapeThroughCall {
// What a call keeps beyond the call, where code that can't be seen reaches
// it, escapes: of a body that is seen, where it keeps it, static storage
// (fg_Register, fg_RegisterRef), and of one that can't be seen, only what its
// parameter states that it keeps (DMibKeepsArgument, fg_RegisterKept); an
// unannotated one keeps nothing (Rule R, fg_RegisterUnseen). A write whose
// place can't be told, through what an unseen function gives, can then be
// of the object (reported: fg_Registered, fg_RegisteredRef,
// fg_RegisteredKept), and what a pointer or reference parameter refers to
// can be written by unseen code after it is set (reported: fg_RunRegistered,
// fg_RunRegisteredRef). Clean: a helper that reads the pointer and sets the
// global to another object (fg_Note), and the unannotated unseen call. What
// a body keeps through a call of itself is kept (fg_RegisterRec of its first
// pointer, reported), not what it only passes on to itself (its second,
// clean); a helper that registers what a pointer read out of the object
// points to keeps that, not the object (fg_RegisterStored, clean).
TCFuture<int> fg_Suspend();
struct CJob { int m_Value = 0; };
CJob g_Job;
struct CView { CJob const *m_pJob = nullptr; void f_ResetUnseen(CJob const *_pJob, CJob const *_pOld); };
CView *fg_OtherView();
CView *g_pRegistered;
CView g_Default;
int g_nSeen;
void fg_Register(CView *_pView) { g_pRegistered = _pView; }
void fg_RegisterRef(CView &_View) { g_pRegistered = &_View; }
void fg_Note(CView *_pView) { g_nSeen += _pView != nullptr; g_pRegistered = &g_Default; }
void fg_RegisterUnseen(CView *_pView);
void fg_RegisterKept(DMibKeepsArgument CView *_pView);
void fg_Fire();
void CView::f_ResetUnseen(CJob const *_pJob, CJob const *_pOld) { m_pJob = _pJob; fg_OtherView()->m_pJob = _pOld; }
TCFuture<int> fg_Registered(CJob const &_Job) { CView View; fg_Register(&View); View.m_pJob = &_Job; View.f_ResetUnseen(&g_Job, &_Job); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:41: warning: TCFuture coroutine 'fg_Registered' uses '_Job' after a suspension, {{.*}}
TCFuture<int> fg_RegisteredRef(CJob const &_Job) { CView View; fg_RegisterRef(View); View.m_pJob = &_Job; View.f_ResetUnseen(&g_Job, &_Job); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:44: warning: TCFuture coroutine 'fg_RegisteredRef' uses '_Job' after a suspension, {{.*}}
TCFuture<int> fg_Noted(CJob const &_Job) { CView View; fg_Note(&View); View.m_pJob = &_Job; View.f_ResetUnseen(&g_Job, &_Job); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
TCFuture<int> fg_RegisteredUnseen(CJob const &_Job) { CView View; fg_RegisterUnseen(&View); View.m_pJob = &_Job; View.f_ResetUnseen(&g_Job, &_Job); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
TCFuture<int> fg_RegisteredKept(CJob const &_Job) { CView View; fg_RegisterKept(&View); View.m_pJob = &_Job; View.f_ResetUnseen(&g_Job, &_Job); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:45: warning: TCFuture coroutine 'fg_RegisteredKept' uses '_Job' after a suspension, {{.*}}
void fg_SetRegistered(CView *_pView, CJob const *_pJob) { fg_Register(_pView); _pView->m_pJob = _pJob; fg_Fire(); }
void fg_SetNoted(CView *_pView, CJob const *_pJob) { fg_Note(_pView); _pView->m_pJob = _pJob; fg_Fire(); }
void fg_SetRegisteredRef(CView &_View, CJob const *_pJob) { fg_Register(&_View); _View.m_pJob = _pJob; fg_Fire(); }
void fg_SetNotedRef(CView &_View, CJob const *_pJob) { fg_Note(&_View); _View.m_pJob = _pJob; fg_Fire(); }
TCFuture<int> fg_RunRegistered(CJob const &_Job) { CView View; View.m_pJob = &_Job; fg_SetRegistered(&View, &g_Job); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:44: warning: TCFuture coroutine 'fg_RunRegistered' uses '_Job' after a suspension, {{.*}}
TCFuture<int> fg_RunNoted(CJob const &_Job) { CView View; View.m_pJob = &_Job; fg_SetNoted(&View, &g_Job); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
TCFuture<int> fg_RunRegisteredRef(CJob const &_Job) { CView View; View.m_pJob = &_Job; fg_SetRegisteredRef(View, &g_Job); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:47: warning: TCFuture coroutine 'fg_RunRegisteredRef' uses '_Job' after a suspension, {{.*}}
TCFuture<int> fg_RunNotedRef(CJob const &_Job) { CView View; View.m_pJob = &_Job; fg_SetNotedRef(View, &g_Job); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
void fg_RegisterRec(CView *_pKeep, CView *_pOther, int _nDepth) { if (_nDepth == 0) { g_pRegistered = _pKeep; return; } fg_RegisterRec(_pKeep, _pOther, _nDepth - 1); }
TCFuture<int> fg_RegisteredRec(CJob const &_Job) { CView View; CView Other; fg_RegisterRec(&View, &Other, 3); View.m_pJob = &_Job; View.f_ResetUnseen(&g_Job, &_Job); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:44: warning: TCFuture coroutine 'fg_RegisteredRec' uses '_Job' after a suspension, {{.*}}
TCFuture<int> fg_PassedRec(CJob const &_Job) { CView View; CView Other; fg_RegisterRec(&Other, &View, 3); View.m_pJob = &_Job; View.f_ResetUnseen(&g_Job, &_Job); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
void fg_RegisterStored(CView &_View) { void *pStored = *((void **)&_View); fg_RegisterRef(*((CView *)pStored)); }
TCFuture<int> fg_RegisteredStored(CJob const &_Job) { CView View; fg_RegisterStored(View); View.m_pJob = &_Job; View.f_ResetUnseen(&g_Job, &_Job); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
} // namespace NSummaryEscapeThroughCall

namespace NSummaryEscapeThroughReceiver {
// What a member function whose body is seen keeps of its object beyond the
// call, `g_pRegistered = this;`, escapes too: called on a local, also
// through a pointer to it, or on what a parameter refers to, code that
// can't be seen can then reach the local (reported: fg_Registered,
// fg_RunRegistered, fg_RegisteredPointer, fg_RegisteredDereferenced,
// fg_RunRegisteredRef), also where
// it keeps its address in a member first and gives that away (fg_LinkedAway).
// Clean: one that only reads its object (fg_Noted, fg_RunNoted), one that
// only keeps its address in its own member (fg_LinkedSelf), and one that keeps
// what a pointer element of its member points to, not itself (fg_KeptFirst,
// fg_KeptFirstDirect). Pending (a place reached through a member, item 2):
// the address kept in its own member by one call and given away by another
// (fg_LinkedThenLeaked).
TCFuture<int> fg_Suspend();
struct CJob { int m_Value = 0; };
CJob g_Job;
struct CView;
CView *g_pRegistered;
CView *fg_OtherView();
void fg_Fire();
int g_nSeen;
CJob const *g_pKeptJob;
void fg_KeepJob(CJob const &_Job) { g_pKeptJob = &_Job; }
struct CView {
	CJob const *m_pJob = nullptr;
	CView *m_pSelf = nullptr;
	void f_Register() { g_pRegistered = this; }
	void f_Note() { g_nSeen += m_pJob != nullptr; }
	void f_LinkSelf() { m_pSelf = this; }
	void f_LinkAway() { m_pSelf = this; g_pRegistered = m_pSelf; }
	void f_Leak() { g_pRegistered = m_pSelf; }
	CJob const *m_pStack[2] = {};
	void f_KeepFirst() { CJob const *pFirst = m_pStack[0]; fg_KeepJob(*pFirst); }
	void f_KeepFirstDirect() { fg_KeepJob(*m_pStack[0]); }
	void f_ResetUnseen(CJob const *_pJob, CJob const *_pOld) { m_pJob = _pJob; fg_OtherView()->m_pJob = _pOld; }
	void f_SetRegistered(CJob const *_pJob) { f_Register(); m_pJob = _pJob; fg_Fire(); }
	void f_SetNoted(CJob const *_pJob) { f_Note(); m_pJob = _pJob; fg_Fire(); }
};
TCFuture<int> fg_Registered(CJob const &_Job) { CView View; View.f_Register(); View.m_pJob = &_Job; View.f_ResetUnseen(&g_Job, &_Job); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:41: warning: TCFuture coroutine 'fg_Registered' uses '_Job' after a suspension, {{.*}}
TCFuture<int> fg_Noted(CJob const &_Job) { CView View; View.f_Note(); View.m_pJob = &_Job; View.f_ResetUnseen(&g_Job, &_Job); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
TCFuture<int> fg_RunRegistered(CJob const &_Job) { CView View; View.m_pJob = &_Job; View.f_SetRegistered(&g_Job); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:44: warning: TCFuture coroutine 'fg_RunRegistered' uses '_Job' after a suspension, {{.*}}
TCFuture<int> fg_RunNoted(CJob const &_Job) { CView View; View.m_pJob = &_Job; View.f_SetNoted(&g_Job); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
TCFuture<int> fg_RegisteredPointer(CJob const &_Job) { CView View; CView *pView = &View; pView->f_Register(); View.m_pJob = &_Job; View.f_ResetUnseen(&g_Job, &_Job); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:48: warning: TCFuture coroutine 'fg_RegisteredPointer' uses '_Job' after a suspension, {{.*}}
TCFuture<int> fg_RegisteredDereferenced(CJob const &_Job) { CView View; CView *pView = &View; (*pView).f_Register(); View.m_pJob = &_Job; View.f_ResetUnseen(&g_Job, &_Job); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:53: warning: TCFuture coroutine 'fg_RegisteredDereferenced' uses '_Job' after a suspension, {{.*}}
TCFuture<int> fg_LinkedSelf(CJob const &_Job) { CView View; View.f_LinkSelf(); View.m_pJob = &_Job; View.f_ResetUnseen(&g_Job, &_Job); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
TCFuture<int> fg_LinkedAway(CJob const &_Job) { CView View; View.f_LinkAway(); View.m_pJob = &_Job; View.f_ResetUnseen(&g_Job, &_Job); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:41: warning: TCFuture coroutine 'fg_LinkedAway' uses '_Job' after a suspension, {{.*}}
TCFuture<int> fg_LinkedThenLeaked(CJob const &_Job) { CView View; View.f_LinkSelf(); View.f_Leak(); View.m_pJob = &_Job; View.f_ResetUnseen(&g_Job, &_Job); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
// RESTORE-MESSAGES: :[[@LINE-1]]:47: warning: TCFuture coroutine 'fg_LinkedThenLeaked' uses '_Job' after a suspension, {{.*}}
TCFuture<int> fg_KeptFirst(CJob const &_Job) { CView View; View.m_pStack[0] = &g_Job; View.f_KeepFirst(); View.m_pJob = &_Job; View.f_ResetUnseen(&g_Job, &_Job); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
TCFuture<int> fg_KeptFirstDirect(CJob const &_Job) { CView View; View.m_pStack[0] = &g_Job; View.f_KeepFirstDirect(); View.m_pJob = &_Job; View.f_ResetUnseen(&g_Job, &_Job); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
void fg_SetRegisteredRef(CView &_View, CJob const *_pJob) { _View.f_Register(); _View.m_pJob = _pJob; fg_Fire(); }
TCFuture<int> fg_RunRegisteredRef(CJob const &_Job) { CView View; View.m_pJob = &_Job; fg_SetRegisteredRef(View, &g_Job); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:47: warning: TCFuture coroutine 'fg_RunRegisteredRef' uses '_Job' after a suspension, {{.*}}
} // namespace NSummaryEscapeThroughReceiver

namespace NSummaryHolders {
// What a call keeps in what a pointer it is given points to, or in what a
// reference it is given is bound to, a local of the frame whose address
// doesn't escape, a pointer or a record, the frame holds there: its names
// after the call go on (reported: fg_ReadHeld, fg_ReadHeldRef,
// fg_ReadMember, fg_ReadHolder), and what isn't read after the suspension,
// or is read before it, uses nothing (fg_NotRead, fg_ReadBefore,
// fg_NotReadRef, fg_ReadOtherHolder). A local whose address escapes keeps it
// beyond the frame (reported: fg_HeldEscaped).
TCFuture<int> fg_Suspend();
struct CJob { int m_Value = 0; };
CJob g_Job;
struct CHolder { CJob const *m_pJob = nullptr; CJob const *m_pSaved = nullptr; };
CJob const **g_ppSaved;
void fg_Keep(CJob const *_pJob, CJob const **o_ppJob) { *o_ppJob = _pJob; }
void fg_KeepRef(CJob const *_pJob, CJob const *&o_pJob) { o_pJob = _pJob; }
void fg_KeepHolder(CJob const *_pJob, CHolder *o_pHolder) { o_pHolder->m_pJob = _pJob; }
TCFuture<int> fg_ReadHeld(CJob const &_Job) { CJob const *pHeld = nullptr; fg_Keep(&_Job, &pHeld); co_await fg_Suspend(); co_return pHeld->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:39: warning: TCFuture coroutine 'fg_ReadHeld' uses '_Job' after a suspension, {{.*}}
TCFuture<int> fg_NotRead(CJob const &_Job) { CJob const *pHeld = nullptr; fg_Keep(&_Job, &pHeld); co_await fg_Suspend(); co_return g_Job.m_Value; }
TCFuture<int> fg_ReadBefore(CJob const &_Job) { CJob const *pHeld = nullptr; fg_Keep(&_Job, &pHeld); int Value = pHeld->m_Value; co_await fg_Suspend(); co_return Value; }
TCFuture<int> fg_ReadHeldRef(CJob const &_Job) { CJob const *pHeld = nullptr; fg_KeepRef(&_Job, pHeld); co_await fg_Suspend(); co_return pHeld->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:42: warning: TCFuture coroutine 'fg_ReadHeldRef' uses '_Job' after a suspension, {{.*}}
TCFuture<int> fg_NotReadRef(CJob const &_Job) { CJob const *pHeld = nullptr; fg_KeepRef(&_Job, pHeld); co_await fg_Suspend(); co_return g_Job.m_Value; }
TCFuture<int> fg_ReadMember(CJob const &_Job) { CHolder Holder; fg_Keep(&_Job, &Holder.m_pJob); co_await fg_Suspend(); co_return Holder.m_pJob->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:41: warning: TCFuture coroutine 'fg_ReadMember' uses '_Job' after a suspension, {{.*}}
TCFuture<int> fg_ReadOtherHolder(CJob const &_Job) { CHolder Holder, Other; fg_Keep(&_Job, &Other.m_pJob); co_await fg_Suspend(); co_return Holder.m_pJob ? 0 : g_Job.m_Value; }
TCFuture<int> fg_ReadHolder(CJob const &_Job) { CHolder Holder; fg_KeepHolder(&_Job, &Holder); co_await fg_Suspend(); co_return Holder.m_pJob->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:41: warning: TCFuture coroutine 'fg_ReadHolder' uses '_Job' after a suspension, {{.*}}
TCFuture<int> fg_HeldEscaped(CJob const &_Job) { CJob const *pHeld = nullptr; g_ppSaved = &pHeld; fg_Keep(&_Job, &pHeld); co_await fg_Suspend(); co_return g_Job.m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:42: warning: TCFuture coroutine 'fg_HeldEscaped' uses '_Job' after a suspension, {{.*}}
} // namespace NSummaryHolders

namespace NSummaryClosureWrites {
// What a closure made and called in the function writes through `this` or a
// pointer parameter it captures by reference is where it writes: a member
// it doesn't write keeps what the function sets (fg_Read, fg_ReadPointer,
// clean); the member it writes is reported (fg_ReadSaved,
// fg_ReadPointerSaved), and so is the object the closure gives away
// (fg_ReadRegistered, fg_ReadPointerRegistered).
TCFuture<int> fg_Suspend();
struct CJob { int m_Value = 0; };
CJob g_Job;
struct CView;
CView *g_pView;
struct CView {
	CJob const *m_pJob = nullptr;
	CJob const *m_pSaved = nullptr;
	void f_Set(CJob const *_pJob, CJob const *_pOld) { m_pJob = _pJob; auto fWrite = [&] { m_pSaved = _pOld; }; fWrite(); }
	void f_SetRegistering(CJob const *_pJob, CJob const *_pOld) { m_pJob = _pJob; auto fWrite = [&] { g_pView = this; m_pSaved = _pOld; }; fWrite(); }
};
CView g_Other;
void fg_Set(CView *_pView, CJob const *_pJob, CJob const *_pOld) { _pView->m_pJob = _pJob; auto fWrite = [&] { _pView->m_pSaved = _pOld; }; fWrite(); }
void fg_SetRegistering(CView *_pView, CJob const *_pJob, CJob const *_pOld) { _pView->m_pJob = _pJob; auto fWrite = [&] { g_pView = _pView; _pView->m_pSaved = _pOld; }; fWrite(); }
TCFuture<int> fg_Read(CJob const &_Job) { CView View; View.m_pJob = &_Job; View.f_Set(&g_Job, &_Job); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
TCFuture<int> fg_ReadSaved(CJob const &_Job) { CView View; View.m_pJob = &_Job; View.f_Set(&g_Job, &_Job); co_await fg_Suspend(); co_return View.m_pSaved->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:40: warning: TCFuture coroutine 'fg_ReadSaved' uses '_Job' after a suspension, {{.*}}
TCFuture<int> fg_ReadRegistered(CJob const &_Job) { CView View; View.m_pJob = &_Job; View.f_SetRegistering(&g_Job, &_Job); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:45: warning: TCFuture coroutine 'fg_ReadRegistered' uses '_Job' after a suspension, {{.*}}
TCFuture<int> fg_ReadPointer(CJob const &_Job) { CView View; View.m_pJob = &_Job; fg_Set(&View, &g_Job, &_Job); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
TCFuture<int> fg_ReadPointerSaved(CJob const &_Job) { CView View; View.m_pJob = &_Job; fg_Set(&View, &g_Job, &_Job); co_await fg_Suspend(); co_return View.m_pSaved->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:47: warning: TCFuture coroutine 'fg_ReadPointerSaved' uses '_Job' after a suspension, {{.*}}
TCFuture<int> fg_ReadPointerRegistered(CJob const &_Job) { CView View; View.m_pJob = &_Job; fg_SetRegistering(&View, &g_Job, &_Job); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:52: warning: TCFuture coroutine 'fg_ReadPointerRegistered' uses '_Job' after a suspension, {{.*}}
} // namespace NSummaryClosureWrites

namespace NSummaryClosureLimit {
// Past 64 closures made of a parameter, where the function keeps it can't
// be told and it is kept as a body that can't be seen keeps it (keptByType):
// in the object as a whole, of which a member that can't hold it there still
// doesn't (fg_Read64, fg_Read65, clean); a member that can is reported
// (fg_Read64Read, fg_Read65Read).
TCFuture<int> fg_Suspend();
struct CJob { int m_Value = 0; };
CJob g_Job;
#define DClosure(n) auto f##n = [&] { (void)_pOld; }; f##n();
#define DClosures8(n) DClosure(n##0) DClosure(n##1) DClosure(n##2) DClosure(n##3) DClosure(n##4) DClosure(n##5) DClosure(n##6) DClosure(n##7)
#define DClosures63 DClosures8(a) DClosures8(b) DClosures8(c) DClosures8(d) DClosures8(e) DClosures8(f) DClosures8(g) DClosure(h0) DClosure(h1) DClosure(h2) DClosure(h3) DClosure(h4) DClosure(h5) DClosure(h6)
struct CView {
	CJob const *m_pJob = nullptr;
	CJob const *m_pSaved = nullptr;
	void f_Set64(CJob const *_pJob, CJob const *_pOld) { m_pJob = _pJob; DClosures63 auto fLast = [&] { m_pSaved = _pOld; }; fLast(); }
	void f_Set65(CJob const *_pJob, CJob const *_pOld) { m_pJob = _pJob; DClosures63 DClosure(h7) auto fLast = [&] { m_pSaved = _pOld; }; fLast(); }
};
TCFuture<int> fg_Read64(CJob const &_Job) { CView View; View.m_pJob = &_Job; View.f_Set64(&g_Job, &_Job); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
TCFuture<int> fg_Read65(CJob const &_Job) { CView View; View.m_pJob = &_Job; View.f_Set65(&g_Job, &_Job); co_await fg_Suspend(); co_return View.m_pJob->m_Value;  }
struct CViewRead {
	CJob const *m_pJob = nullptr;
	CJob const *m_pSaved = nullptr;
	void f_Set64Read(CJob const *_pJob, CJob const *_pOld) { m_pJob = _pJob; DClosures63 auto fLast = [&] { m_pJob = _pOld; }; fLast(); }
	void f_Set65Read(CJob const *_pJob, CJob const *_pOld) { m_pJob = _pJob; DClosures63 DClosure(h7) auto fLast = [&] { m_pJob = _pOld; }; fLast(); }
};
TCFuture<int> fg_Read64Read(CJob const &_Job) { CViewRead View; View.m_pJob = &_Job; View.f_Set64Read(&g_Job, &_Job); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:41: warning: TCFuture coroutine 'fg_Read64Read' uses '_Job' after a suspension, {{.*}}
TCFuture<int> fg_Read65Read(CJob const &_Job) { CViewRead View; View.m_pJob = &_Job; View.f_Set65Read(&g_Job, &_Job); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:41: warning: TCFuture coroutine 'fg_Read65Read' uses '_Job' after a suspension, {{.*}}
} // namespace NSummaryClosureLimit

namespace NSummaryConstructorParts {
// A member a constructor's initializer makes as an aggregate,
// `m_Read{_p, nullptr}`, holds what its body then gives a part of it too,
// `m_Read.m_pB = _p;`, in whichever order the body and the initializer keep
// it (reported: fg_PartReadA, fg_PartReadB).
TCFuture<int> fg_Suspend();
struct CJob { int m_Value = 0; };
CJob g_Job;
struct CRead { CJob const *m_pA = nullptr; CJob const *m_pB = nullptr; };
struct CKeepPart {
	CRead m_Read;
	CKeepPart(CJob const *_p) : m_Read{_p, nullptr} { m_Read.m_pB = _p; }
};
TCFuture<int> fg_PartReadB(CJob const &_Job) { CKeepPart Keep(&_Job); co_await fg_Suspend(); co_return Keep.m_Read.m_pB->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:40: warning: TCFuture coroutine 'fg_PartReadB' uses '_Job' after a suspension, {{.*}}
TCFuture<int> fg_PartReadA(CJob const &_Job) { CKeepPart Keep(&_Job); co_await fg_Suspend(); co_return Keep.m_Read.m_pA->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:40: warning: TCFuture coroutine 'fg_PartReadA' uses '_Job' after a suspension, {{.*}}
} // namespace NSummaryConstructorParts

namespace NSummaryElementForms {
// An element written after an insert at the front of a container is
// another than the one the insert moved (reported: fg_InsertedBetween); a
// write through a pointer to the second element, also by a subscript of an
// offset, is of that element (reported: fg_ThroughSecond,
// fg_OffsetSubscript) and not of the first (fg_ThroughSecondReadFirst,
// fg_OffsetSubscriptReadFirst).
TCFuture<int> fg_Suspend();
struct CJob { int m_Value = 0; };
CJob g_Job;
struct CView { CJob const *m_pJobs[2] = {}; };
struct CJobs {
	CJob const *m_pData[4] = {};
	CJob const *&operator[](int _iJob) { return m_pData[_iJob]; }
	void f_InsertFirst(CJob const *_pJob);
};
TCFuture<int> fg_InsertedBetween(CJob const &_Job) { CJobs Jobs; Jobs[0] = &_Job; Jobs.f_InsertFirst(&g_Job); Jobs[0] = &g_Job; co_await fg_Suspend(); co_return Jobs[1]->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:46: warning: TCFuture coroutine 'fg_InsertedBetween' uses '_Job' after a suspension, {{.*}}
TCFuture<int> fg_ThroughSecond(CJob const &_Job) { CView View; CJob const **ppJob = &View.m_pJobs[1]; ppJob[0] = &_Job; co_await fg_Suspend(); co_return View.m_pJobs[1]->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:44: warning: TCFuture coroutine 'fg_ThroughSecond' uses '_Job' after a suspension, {{.*}}
TCFuture<int> fg_ThroughSecondReadFirst(CJob const &_Job) { CView View; View.m_pJobs[0] = &g_Job; CJob const **ppJob = &View.m_pJobs[1]; ppJob[0] = &_Job; co_await fg_Suspend(); co_return View.m_pJobs[0]->m_Value; }
TCFuture<int> fg_OffsetSubscript(CJob const &_Job) { CView View; (View.m_pJobs + 1)[0] = &_Job; co_await fg_Suspend(); co_return View.m_pJobs[1]->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:46: warning: TCFuture coroutine 'fg_OffsetSubscript' uses '_Job' after a suspension, {{.*}}
TCFuture<int> fg_OffsetSubscriptReadFirst(CJob const &_Job) { CView View; View.m_pJobs[0] = &g_Job; (View.m_pJobs + 1)[0] = &_Job; co_await fg_Suspend(); co_return View.m_pJobs[0]->m_Value; }
} // namespace NSummaryElementForms

namespace NSummaryElements {
// An element of a constant index of an array member is a place of its own:
// a write of it at that index replaces what it held (fg_Setter of f_Set,
// fg_Referent of fg_Set, fg_Twice, clean), and a write at another constant
// index doesn't touch it. Reported: an element set on one way only
// (fg_SetterMaybe), at an index that isn't a constant (fg_SetterAt,
// fg_Index), at another index than the one read (fg_SetterOther,
// fg_SetterReadOther), and through a pointer to an element, also stepped,
// which is the element it steps to (fg_Through, fg_Offset, fg_StepAfter).
TCFuture<int> fg_Suspend();
struct CJob { int m_Value = 0; };
CJob g_Job;
struct CView {
	CJob const *m_pJobs[2] = {};
	void f_Set(CJob const *_pJob) { m_pJobs[0] = _pJob; }
	void f_SetMaybe(CJob const *_pJob, bool _bSet) { if (_bSet) m_pJobs[0] = _pJob; }
	void f_SetAt(CJob const *_pJob, int _iJob) { m_pJobs[_iJob] = _pJob; }
	void f_SetOther(CJob const *_pJob) { m_pJobs[1] = _pJob; }
};
void fg_Set(CView &_View, CJob const *_pJob) { _View.m_pJobs[0] = _pJob; }
TCFuture<int> fg_Setter(CJob const &_Job) { CView View; View.m_pJobs[0] = &_Job; View.f_Set(&g_Job); co_await fg_Suspend(); co_return View.m_pJobs[0]->m_Value; }
TCFuture<int> fg_SetterMaybe(CJob const &_Job, bool _bSet) { CView View; View.m_pJobs[0] = &_Job; View.f_SetMaybe(&g_Job, _bSet); co_await fg_Suspend(); co_return View.m_pJobs[0]->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:42: warning: TCFuture coroutine 'fg_SetterMaybe' uses '_Job' after a suspension, {{.*}}
TCFuture<int> fg_SetterAt(CJob const &_Job) { CView View; View.m_pJobs[0] = &_Job; View.f_SetAt(&g_Job, 0); co_await fg_Suspend(); co_return View.m_pJobs[0]->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:39: warning: TCFuture coroutine 'fg_SetterAt' uses '_Job' after a suspension, {{.*}}
TCFuture<int> fg_SetterOther(CJob const &_Job) { CView View; View.m_pJobs[0] = &_Job; View.f_SetOther(&g_Job); co_await fg_Suspend(); co_return View.m_pJobs[0]->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:42: warning: TCFuture coroutine 'fg_SetterOther' uses '_Job' after a suspension, {{.*}}
TCFuture<int> fg_SetterReadOther(CJob const &_Job) { CView View; View.m_pJobs[1] = &_Job; View.f_Set(&g_Job); co_await fg_Suspend(); co_return View.m_pJobs[1]->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:46: warning: TCFuture coroutine 'fg_SetterReadOther' uses '_Job' after a suspension, {{.*}}
TCFuture<int> fg_Referent(CJob const &_Job) { CView View; View.m_pJobs[0] = &_Job; fg_Set(View, &g_Job); co_await fg_Suspend(); co_return View.m_pJobs[0]->m_Value; }
TCFuture<int> fg_Twice(CJob const &_Job) { CView View; View.m_pJobs[0] = &_Job; View.m_pJobs[0] = &g_Job; co_await fg_Suspend(); co_return View.m_pJobs[0]->m_Value; }
TCFuture<int> fg_Index(CJob const &_Job, int _iJob) { CView View; View.m_pJobs[0] = &g_Job; View.m_pJobs[_iJob] = &_Job; co_await fg_Suspend(); co_return View.m_pJobs[0]->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:36: warning: TCFuture coroutine 'fg_Index' uses '_Job' after a suspension, {{.*}}
TCFuture<int> fg_Through(CJob const &_Job) { CView View; CJob const **ppJob = &View.m_pJobs[1]; *ppJob = &_Job; co_await fg_Suspend(); co_return View.m_pJobs[1]->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:38: warning: TCFuture coroutine 'fg_Through' uses '_Job' after a suspension, {{.*}}
TCFuture<int> fg_Offset(CJob const &_Job) { CView View; *(View.m_pJobs + 1) = &_Job; co_await fg_Suspend(); co_return View.m_pJobs[1]->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:37: warning: TCFuture coroutine 'fg_Offset' uses '_Job' after a suspension, {{.*}}
TCFuture<int> fg_StepAfter(CJob const &_Job) { CView View; CJob const **ppJob = &View.m_pJobs[0]; ppJob[1] = &_Job; co_await fg_Suspend(); co_return View.m_pJobs[1]->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:40: warning: TCFuture coroutine 'fg_StepAfter' uses '_Job' after a suspension, {{.*}}
} // namespace NSummaryElements

namespace NSummaryRecursionSets {
// A function that calls itself sets the pointer it is given on every way
// where each way that doesn't call it again does, as of the members of an
// object (NSummaryRecursion), by reference and by its address. Reported: the
// same with a way that leaves without setting it.
TCFuture<int> fg_Suspend();
struct CJob { int m_Value = 0; };
CJob g_Job;
void fg_SetRec(CJob const *&o_pJob, CJob const *_pJob, int _nDepth) { if (_nDepth == 0) { o_pJob = _pJob; return; } fg_SetRec(o_pJob, _pJob, _nDepth - 1); }
void fg_SetRecMaybe(CJob const *&o_pJob, CJob const *_pJob, int _nDepth) { if (_nDepth == 0) return; if (_nDepth == 1) { o_pJob = _pJob; return; } fg_SetRecMaybe(o_pJob, _pJob, _nDepth - 1); }
void fg_SetPointerRec(CJob const **o_ppJob, CJob const *_pJob, int _nDepth) { if (_nDepth == 0) { *o_ppJob = _pJob; return; } fg_SetPointerRec(o_ppJob, _pJob, _nDepth - 1); }
TCFuture<int> fg_Rec(CJob const &_Job) { CJob const *pJob = &_Job; fg_SetRec(pJob, &g_Job, 3); co_await fg_Suspend(); co_return pJob->m_Value; }
TCFuture<int> fg_RecMaybe(CJob const &_Job) { CJob const *pJob = &_Job; fg_SetRecMaybe(pJob, &g_Job, 3); co_await fg_Suspend(); co_return pJob->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:39: warning: TCFuture coroutine 'fg_RecMaybe' uses '_Job' after a suspension, {{.*}}
TCFuture<int> fg_PointerRec(CJob const &_Job) { CJob const *pJob = &_Job; fg_SetPointerRec(&pJob, &g_Job, 3); co_await fg_Suspend(); co_return pJob->m_Value; }
// Two that call each other, each setting it to nothing on one way: each
// value once, however many passes the recursion takes (a value for each
// pass, kept again on each, made boringssl's ASN.1 decoders take minutes).
// Reported: the same with a way that leaves without setting it.
void fg_ParseB(CJob const **o_ppJob, CJob const *_pJob, int _nDepth);
void fg_ParseA(CJob const **o_ppJob, CJob const *_pJob, int _nDepth) { if (_nDepth == 0) { *o_ppJob = nullptr; return; } if (_nDepth == 1) { *o_ppJob = _pJob; return; } fg_ParseB(o_ppJob, _pJob, _nDepth - 1); }
void fg_ParseB(CJob const **o_ppJob, CJob const *_pJob, int _nDepth) { if (_nDepth == 0) { *o_ppJob = nullptr; return; } if (_nDepth == 2) { fg_ParseA(o_ppJob, _pJob, _nDepth - 2); return; } fg_ParseA(o_ppJob, _pJob, _nDepth - 1); }
void fg_ParseMaybeB(CJob const **o_ppJob, CJob const *_pJob, int _nDepth);
void fg_ParseMaybeA(CJob const **o_ppJob, CJob const *_pJob, int _nDepth) { if (_nDepth == 0) { *o_ppJob = nullptr; return; } if (_nDepth == 1) return; fg_ParseMaybeB(o_ppJob, _pJob, _nDepth - 1); }
void fg_ParseMaybeB(CJob const **o_ppJob, CJob const *_pJob, int _nDepth) { if (_nDepth == 0) { *o_ppJob = nullptr; return; } if (_nDepth == 2) { fg_ParseMaybeA(o_ppJob, _pJob, _nDepth - 2); return; } fg_ParseMaybeA(o_ppJob, _pJob, _nDepth - 1); }
TCFuture<int> fg_Parse(CJob const &_Job) { CJob const *pJob = &_Job; fg_ParseA(&pJob, &g_Job, 5); co_await fg_Suspend(); co_return pJob ? pJob->m_Value : 0; }
TCFuture<int> fg_ParseMaybe(CJob const &_Job) { CJob const *pJob = &_Job; fg_ParseMaybeA(&pJob, &g_Job, 5); co_await fg_Suspend(); co_return pJob ? pJob->m_Value : 0; }
// CHECK-MESSAGES: :[[@LINE-1]]:41: warning: TCFuture coroutine 'fg_ParseMaybe' uses '_Job' after a suspension, {{.*}}
} // namespace NSummaryRecursionSets

namespace NSummaryThrows {
// What a setter sets on every way it returns by holds after the call where it
// returns; in a handler of a `try` around the call, where it threw before it
// set, what was held before still is. Reported: read in the handler, of a
// pointer and of a member. Clean: read after the `try`.
TCFuture<int> fg_Suspend();
struct CJob { int m_Value = 0; };
CJob g_Job;
void fg_Check(bool _b);
void fg_SetChecked(CJob const *&o_pJob, CJob const *_pJob, bool _b) { fg_Check(_b); o_pJob = _pJob; }
struct CView { CJob const *m_pJob = nullptr; void f_SetChecked(CJob const *_pJob, bool _b) { fg_Check(_b); m_pJob = _pJob; } };
TCFuture<int> fg_ReadHandler(CJob const &_Job, bool _b) { CJob const *pJob = &_Job; co_await fg_Suspend(); try { fg_SetChecked(pJob, &g_Job, _b); } catch (...) { co_return pJob->m_Value; } co_return 0; }
// CHECK-MESSAGES: :[[@LINE-1]]:42: warning: TCFuture coroutine 'fg_ReadHandler' uses '_Job' after a suspension, {{.*}}
TCFuture<int> fg_ReadAfter(CJob const &_Job, bool _b) { CJob const *pJob = &_Job; co_await fg_Suspend(); try { fg_SetChecked(pJob, &g_Job, _b); } catch (...) { co_return 0; } co_return pJob->m_Value; }
TCFuture<int> fg_MemberHandler(CJob const &_Job, bool _b) { CView View; View.m_pJob = &_Job; co_await fg_Suspend(); try { View.f_SetChecked(&g_Job, _b); } catch (...) { co_return View.m_pJob->m_Value; } co_return 0; }
// CHECK-MESSAGES: :[[@LINE-1]]:44: warning: TCFuture coroutine 'fg_MemberHandler' uses '_Job' after a suspension, {{.*}}
TCFuture<int> fg_MemberAfter(CJob const &_Job, bool _b) { CView View; View.m_pJob = &_Job; co_await fg_Suspend(); try { View.f_SetChecked(&g_Job, _b); } catch (...) { co_return 0; } co_return View.m_pJob->m_Value; }
} // namespace NSummaryThrows

namespace NReview2Round202PointerOutput {
// Round 202 (llvm-coroutine-7, -10): a setter whose body is seen and assigns
// through a pointer parameter on every way through it is a definition of what
// the argument points to, as one through a reference parameter. Reported: a
// setter that assigns on one way only, and a read before the setter.
TCFuture<int> fg_Work();
struct CJob { int m_Value = 0; };
void fg_SetJob(CJob **o_ppJob, CJob *_pJob) { *o_ppJob = _pJob; }
void fg_SetJobMaybe(CJob **o_ppJob, CJob *_pJob, bool _bSet) { if (_bSet) *o_ppJob = _pJob; }
TCFuture<int> fg_Read(CJob *_pJob) { CJob Local{42}; fg_SetJob(&_pJob, &Local); co_await fg_Work(); co_return _pJob->m_Value; }
TCFuture<int> fg_ReadMaybe(CJob *_pJob, bool _bSet) { CJob Local{42}; fg_SetJobMaybe(&_pJob, &Local, _bSet); co_await fg_Work(); co_return _pJob->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:34: warning: TCFuture coroutine 'fg_ReadMaybe' uses '_pJob' after a suspension, but it takes it as 'CJob *', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
void fg_Set(CJob const **o_ppJob, CJob const *_pJob) { *o_ppJob = _pJob; }
TCFuture<int> fg_ReadConst(CJob const *_pJob) { CJob Local; fg_Set(&_pJob, &Local); co_await fg_Work(); co_return _pJob->m_Value; }
TCFuture<int> fg_ReadBefore(CJob const *_pJob) { CJob Local; co_await fg_Work(); int Value = _pJob->m_Value; fg_Set(&_pJob, &Local); co_return Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:41: warning: TCFuture coroutine 'fg_ReadBefore' uses '_pJob' after a suspension, but it takes it as 'const CJob *', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
} // namespace NReview2Round202PointerOutput

namespace NReview2Round202Holders {
// Round 202 (llvm-coroutine-2, -4, -5): a holder counts for what can run after
// the suspension. A record whose scope ends before the first suspension, and
// its destructor there, use nothing afterwards; an integer that carries a
// pointer's value is followed by the definitions that reach its names.
// Reported: the suspension in the holder's scope, the record read after it,
// and the integer not given another value.
TCFuture<int> fg_Work();
TCFuture<int> fg_Suspend();
void fg_Consume(int _Value);
using umint = unsigned long;
struct CJob { int m_Value = 0; };
struct CReadScope {
	int *m_pValue;
	~CReadScope() { fg_Consume(*m_pValue); }
};
TCFuture<int> fg_ReadScoped(int *_pValue) { { CReadScope Scope{_pValue}; } co_await fg_Work(); co_return 0; }
TCFuture<int> fg_ReadScopedAcross(int *_pValue) { { CReadScope Scope{_pValue}; co_await fg_Work(); } co_return 0; }
// CHECK-MESSAGES: :[[@LINE-1]]:40: warning: TCFuture coroutine 'fg_ReadScopedAcross' uses '_pValue' after a suspension, but it takes it as 'int *', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
struct CJobView { CJob const *m_pJob; };
TCFuture<int> fg_ReadView(CJob const &_Job) { int Value; { CJobView View{&_Job}; Value = View.m_pJob->m_Value; } co_await fg_Suspend(); co_return Value; }
TCFuture<int> fg_ReadViewAfter(CJob const &_Job) { int Value; { CJobView View{&_Job}; co_await fg_Suspend(); Value = View.m_pJob->m_Value; } co_return Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:44: warning: TCFuture coroutine 'fg_ReadViewAfter' uses '_Job' after a suspension, but it takes it as 'const CJob &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
TCFuture<int> fg_ReadAddress(CJob *_pJob) { CJob Local{}; umint Address = reinterpret_cast<umint>(_pJob); Address = reinterpret_cast<umint>(&Local); co_await fg_Suspend(); co_return reinterpret_cast<CJob *>(Address)->m_Value; }
TCFuture<int> fg_ReadAddressKept(CJob *_pJob) { umint Address = reinterpret_cast<umint>(_pJob); co_await fg_Suspend(); co_return reinterpret_cast<CJob *>(Address)->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:40: warning: TCFuture coroutine 'fg_ReadAddressKept' uses '_pJob' after a suspension, but it takes it as 'CJob *', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
TCFuture<int> fg_ReadAddressMaybe(CJob *_pJob, bool _bLocal) { CJob Local{}; umint Address = reinterpret_cast<umint>(_pJob); if (_bLocal) Address = reinterpret_cast<umint>(&Local); co_await fg_Suspend(); co_return reinterpret_cast<CJob *>(Address)->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:41: warning: TCFuture coroutine 'fg_ReadAddressMaybe' uses '_pJob' after a suspension, but it takes it as 'CJob *', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
} // namespace NReview2Round202Holders

namespace NReview2Round202Closures {
// Round 202 (llvm-coroutine-5): a lambda that isn't a coroutine reads its
// captures where it is called, with the definitions of a function object it
// is in that reach the call. Reported: the closure called after the
// suspension, and the function object not given another value.
TCFuture<int> fg_Suspend();
struct CJob { int m_Value = 0; };
TCFuture<int> fg_Read(CJob const &_Job) { TCFunction<int()> fRead = [&_Job] { return _Job.m_Value; }; fRead = [] { return 42; }; co_await fg_Suspend(); co_return fRead(); }
TCFuture<int> fg_ReadKept(CJob const &_Job) { TCFunction<int()> fRead = [&_Job] { return _Job.m_Value; }; co_await fg_Suspend(); co_return fRead(); }
// CHECK-MESSAGES: :[[@LINE-1]]:39: warning: TCFuture coroutine 'fg_ReadKept' uses '_Job' after a suspension, but it takes it as 'const CJob &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
TCFuture<int> fg_ReadBefore(CJob const &_Job) { auto fRead = [&_Job] { return _Job.m_Value; }; int Value = fRead(); co_await fg_Suspend(); co_return Value; }
TCFuture<int> fg_ReadAfter(CJob const &_Job) { auto fRead = [&_Job] { return _Job.m_Value; }; int Value = fRead(); co_await fg_Suspend(); co_return Value + fRead(); }
// CHECK-MESSAGES: :[[@LINE-1]]:40: warning: TCFuture coroutine 'fg_ReadAfter' uses '_Job' after a suspension, but it takes it as 'const CJob &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
} // namespace NReview2Round202Closures

namespace NReview2Round203Places {
// Round 203 (llvm-coroutine-1, -2, -3, -7, -9): what a member holds after the
// suspension is what the definitions of that member that reach there give,
// the same for a read of it, a nested member, and the destructor where the
// scope ends; a constructor whose body is seen keeps nested members of an
// aggregate member apart, and a setter that calls a setter that assigns on
// every way assigns on every way. Reported: the member that still holds the
// borrow, the refresh on one way only, the destructor without the overwrite.
TCFuture<int> fg_Work();
TCFuture<int> fg_Suspend();
void fg_Consume(int _Value);
int g_Value = 0;
namespace NNestedConstructor {
struct CJob { int m_Value = 0; };
struct CRead { CJob const *m_pInput; CJob const *m_pFallback; };
struct CContext { CContext(CJob const *_pInput, CJob const *_pFallback) : m_Read{_pInput, _pFallback} {} CRead m_Read; };
TCFuture<int> fg_Read(CJob const &_Job) { CJob Local{}; CContext Context{&_Job, &Local}; co_await fg_Work(); co_return Context.m_Read.m_pFallback->m_Value; }
TCFuture<int> fg_ReadInput(CJob const &_Job) { CJob Local{}; CContext Context{&_Job, &Local}; co_await fg_Work(); co_return Context.m_Read.m_pInput->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:40: warning: TCFuture coroutine 'fg_ReadInput' uses '_Job' after a suspension, but it takes it as 'const CJob &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
}
namespace NRefreshed {
struct CJob { int m_Value = 0; };
struct CReadPair { CJob const *m_pInput; CJob const *m_pFallback; };
TCFuture<int> fg_Read(CJob const &_Job) { CJob Local{}; CReadPair Context{&_Job, &Local}; Context.m_pInput = &Local; co_await fg_Work(); co_return Context.m_pInput->m_Value; }
TCFuture<int> fg_ReadKept(CJob const &_Job) { CJob Local{}; CReadPair Context{&_Job, &Local}; co_await fg_Work(); co_return Context.m_pInput->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:39: warning: TCFuture coroutine 'fg_ReadKept' uses '_Job' after a suspension, but it takes it as 'const CJob &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
TCFuture<int> fg_ReadMaybe(CJob const &_Job, bool _bRefresh) { CJob Local{}; CReadPair Context{&_Job, &Local}; if (_bRefresh) Context.m_pInput = &Local; co_await fg_Work(); co_return Context.m_pInput->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:40: warning: TCFuture coroutine 'fg_ReadMaybe' uses '_Job' after a suspension, but it takes it as 'const CJob &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
struct CReadContext { int const *m_pInput; int const *m_pFallback; };
TCFuture<int> fg_ReadInt(int const &_Value) { int Local = 42; CReadContext Context{&_Value, &Local}; Context.m_pInput = &Local; co_await fg_Work(); co_return *Context.m_pInput; }
TCFuture<int> fg_ReadIntKept(int const &_Value) { int Local = 42; CReadContext Context{&_Value, &Local}; co_await fg_Work(); co_return *Context.m_pInput; }
// CHECK-MESSAGES: :[[@LINE-1]]:41: warning: TCFuture coroutine 'fg_ReadIntKept' uses '_Value' after a suspension, but it takes it as 'const int &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
}
namespace NNestedSpan {
struct CIoSpan { void const *m_pData; unsigned long m_Size; };
struct CRequest { CIoSpan m_Input, m_Fallback; };
TCFuture<int> fg_Read(int const &_Value) { int Local = 5; CRequest Request{{&_Value, sizeof _Value}, {nullptr, 0}}; Request.m_Input.m_pData = &Local; co_await fg_Work(); co_return *static_cast<int const *>(Request.m_Input.m_pData); }
TCFuture<int> fg_ReadMaybe(int const &_Value, bool _bRefresh) { int Local = 5; CRequest Request{{&_Value, sizeof _Value}, {nullptr, 0}}; if (_bRefresh) Request.m_Input.m_pData = &Local; co_await fg_Work(); co_return *static_cast<int const *>(Request.m_Input.m_pData); }
// CHECK-MESSAGES: :[[@LINE-1]]:39: warning: TCFuture coroutine 'fg_ReadMaybe' uses '_Value' after a suspension, but it takes it as 'const int &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
}
namespace NDestroyed {
struct CWriteBack { int *m_pValue; ~CWriteBack() { ++*m_pValue; } };
TCFuture<int> fg_Run(int &_Value) { CWriteBack Slot{&_Value}; Slot.m_pValue = &g_Value; co_await fg_Work(); co_return 0; }
TCFuture<int> fg_RunKept(int &_Value) { CWriteBack Slot{&_Value}; co_await fg_Work(); co_return 0; }
// CHECK-MESSAGES: :[[@LINE-1]]:31: warning: TCFuture coroutine 'fg_RunKept' uses '_Value' after a suspension, but it takes it as 'int &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
struct CReadScope { int const *m_pValue; ~CReadScope() { if (m_pValue) fg_Consume(*m_pValue); } };
TCFuture<int> fg_Read(int const &_Value) { CReadScope Scope{&_Value}; Scope.m_pValue = nullptr; co_await fg_Work(); co_return 0; }
TCFuture<int> fg_ReadKept(int const &_Value) { CReadScope Scope{&_Value}; co_await fg_Work(); co_return 0; }
// CHECK-MESSAGES: :[[@LINE-1]]:38: warning: TCFuture coroutine 'fg_ReadKept' uses '_Value' after a suspension, but it takes it as 'const int &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
}
namespace NForwardingSetter {
struct CJob { int m_Value = 0; };
CJob g_Job;
struct CJobView {
	CJob const *m_pJob;
	void fp_Set(CJob const *_pJob) { m_pJob = _pJob; }
	void f_Set(CJob const *_pJob) { fp_Set(_pJob); }
	void fp_SetMaybe(CJob const *_pJob, bool _bSet) { if (_bSet) m_pJob = _pJob; }
	void f_SetMaybe(CJob const *_pJob, bool _bSet) { fp_SetMaybe(_pJob, _bSet); }
};
TCFuture<int> fg_Read(CJob const &_Job) { CJobView View{&_Job}; View.f_Set(&g_Job); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
TCFuture<int> fg_ReadMaybe(CJob const &_Job, bool _bSet) { CJobView View{&_Job}; View.f_SetMaybe(&g_Job, _bSet); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:40: warning: TCFuture coroutine 'fg_ReadMaybe' uses '_Job' after a suspension, but it takes it as 'const CJob &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
}
} // namespace NReview2Round203Places

void *operator new(decltype(sizeof(0)), void *_pPlace) noexcept;
namespace NSummaryPlaces {
// Where a write lands. A subscript of a pointer whose value is the address of
// an object that isn't an array is the object itself: `ppJob[0]` of `auto
// ppJob = &m_pJob;` replaces m_pJob (fg_SubLast, fg_LocalSubLast clean; the
// same setting the borrow, fg_Sub, fg_LocalSub, reported). Another member of a
// union the member is in is the same storage (fg_Union reported; a struct,
// fg_NoUnion, clean). What a placement gives is where it is placed, not new
// storage (fg_Placed reported; a `new` that allocates, fg_New, clean). A local
// a closure refers to by reference is the function's own (fg_Captured
// reported; another local, fg_CapturedOther, clean).
TCFuture<int> fg_Suspend();
struct CJob { int m_Value = 0; };
CJob g_Job;
struct CView {
	CJob const *m_pJob = nullptr;
	void f_Sub(CJob const *_pJob, CJob const *_pOld) { m_pJob = _pJob; auto ppJob = &m_pJob; ppJob[0] = _pOld; }
	void f_SubLast(CJob const *_pJob, CJob const *_pOld) { m_pJob = _pOld; auto ppJob = &m_pJob; ppJob[0] = _pJob; }
	void f_Placed(CJob const *_pJob, CJob const *_pOld, CView *_pStorage) { m_pJob = _pJob; CView *pNew = new (_pStorage) CView; pNew->m_pJob = _pOld; }
	void f_New(CJob const *_pJob, CJob const *_pOld) { m_pJob = _pJob; CView *pNew = new CView; pNew->m_pJob = _pOld; }
};
struct CUnion {
	union { CJob const *m_pJob; CJob const *m_pOther; };
	void f_Reset(CJob const *_pJob, CJob const *_pOld) { m_pJob = _pJob; m_pOther = _pOld; }
};
struct CNoUnion {
	CJob const *m_pJob = nullptr;
	CJob const *m_pOther = nullptr;
	void f_Reset(CJob const *_pJob, CJob const *_pOld) { m_pJob = _pJob; m_pOther = _pOld; }
};
TCFuture<int> fg_Sub(CJob const &_Job) { CView View; View.m_pJob = &_Job; View.f_Sub(&g_Job, &_Job); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:34: warning: TCFuture coroutine 'fg_Sub' uses '_Job' after a suspension, but it takes it as 'const CJob &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
TCFuture<int> fg_SubLast(CJob const &_Job) { CView View; View.f_SubLast(&g_Job, &_Job); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
TCFuture<int> fg_LocalSub(CJob const &_Job) { CJob const *pJob = &g_Job; auto ppJob = &pJob; ppJob[0] = &_Job; co_await fg_Suspend(); co_return pJob->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:39: warning: TCFuture coroutine 'fg_LocalSub' uses '_Job' after a suspension, but it takes it as 'const CJob &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
TCFuture<int> fg_LocalSubLast(CJob const &_Job) { CJob const *pJob = &_Job; auto ppJob = &pJob; ppJob[0] = &g_Job; co_await fg_Suspend(); co_return pJob->m_Value; }
TCFuture<int> fg_Union(CJob const &_Job) { CUnion View; View.m_pJob = &_Job; View.f_Reset(&g_Job, &_Job); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:36: warning: TCFuture coroutine 'fg_Union' uses '_Job' after a suspension, but it takes it as 'const CJob &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
TCFuture<int> fg_NoUnion(CJob const &_Job) { CNoUnion View; View.m_pJob = &_Job; View.f_Reset(&g_Job, &_Job); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
TCFuture<int> fg_Placed(CJob const &_Job) { CView View; View.m_pJob = &_Job; View.f_Placed(&g_Job, &_Job, &View); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:37: warning: TCFuture coroutine 'fg_Placed' uses '_Job' after a suspension, but it takes it as 'const CJob &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
TCFuture<int> fg_New(CJob const &_Job) { CView View; View.m_pJob = &_Job; View.f_New(&g_Job, &_Job); co_await fg_Suspend(); co_return View.m_pJob->m_Value; }
TCFuture<int> fg_Captured(CJob const &_Job) {
// CHECK-MESSAGES: :[[@LINE-1]]:39: warning: TCFuture coroutine 'fg_Captured' uses '_Job' after a suspension, but it takes it as 'const CJob &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
	CJob const *pJob = &_Job; CJob const *pSaved = &_Job;
	auto fSet = [&](CJob const *&o_pJob) { o_pJob = &g_Job; pJob = pSaved; };
	fSet(pJob); co_await fg_Suspend(); co_return pJob->m_Value; }
TCFuture<int> fg_CapturedOther(CJob const &_Job) {
	CJob const *pJob = &_Job; CJob const *pOther = &_Job; CJob const *pSaved = &_Job;
	auto fSet = [&](CJob const *&o_pJob) { o_pJob = &g_Job; pOther = pSaved; };
	fSet(pJob); co_await fg_Suspend(); co_return pJob->m_Value; }
} // namespace NSummaryPlaces

namespace NLandingWideIndex {
TCFuture<int> fg_Work();
// A constant index past what 64 bits hold is an index that can't be told.
TCFuture<int> fg_Run(int *_pValues) {
// CHECK-MESSAGES: :[[@LINE-1]]:27: warning: TCFuture coroutine 'fg_Run' uses '_pValues' after a suspension, {{.*}}
  int &Value = _pValues[0xffffffffffffffffULL];
  co_await fg_Work();
  co_return Value;
}
} // namespace NLandingWideIndex
