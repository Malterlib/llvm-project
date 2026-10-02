// RUN: %check_clang_tidy -check-suffixes=LAMBDA -std=c++23-or-later %s malterlib-coroutine-lambda-call %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -I %S/Inputs
// RUN: %check_clang_tidy -check-suffixes=CONTINUE -std=c++23-or-later %s malterlib-continue-running-on-actor %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -I %S/Inputs
// RUN: %check_clang_tidy -check-suffixes=DISPATCH -std=c++23-or-later %s malterlib-actor-dispatch-this %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -I %S/Inputs

// A member function with an explicit object parameter, `this CType &_This`,
// is told like one with an implicit object: what it keeps in its object
// through `_This`, what is called on `_This` or `this` of it, a lambda whose
// call operator has one given what it sets, and `_This` as the object of the
// current actor. Each form is beside its implicit twin,
// which gives the same.

#include "malterlib-concurrency.h"

using namespace NMib::NConcurrency;

namespace NKeptInObject {
// Kept in the object through the explicit object, `_This.m_pValue = _pValue`,
// by a constructor that calls it on its own object, `this->f_Keep(...)`.
struct CKeptExplicit
{
	int const *m_pValue = nullptr;
	CKeptExplicit(int const &_Value) { this->f_Keep(&_Value); }
	void f_Keep(this CKeptExplicit &_This, int const *_pValue) { _This.m_pValue = _pValue; }
};
struct CKeptImplicit
{
	int const *m_pValue = nullptr;
	CKeptImplicit(int const &_Value) { f_Keep(&_Value); }
	void f_Keep(int const *_pValue) { m_pValue = _pValue; }
};
// Kept by a member function with an implicit object called on the explicit
// object, `_This.f_Store(_pValue)`.
struct CForwardedExplicit
{
	int const *m_pValue = nullptr;
	CForwardedExplicit(int const &_Value) { this->f_Set(&_Value); }
	void f_Set(this CForwardedExplicit &_This, int const *_pValue) { _This.f_Store(_pValue); }
	void f_Store(int const *_pValue) { m_pValue = _pValue; }
};
struct CForwardedImplicit
{
	int const *m_pValue = nullptr;
	CForwardedImplicit(int const &_Value) { f_Set(&_Value); }
	void f_Set(int const *_pValue) { f_Store(_pValue); }
	void f_Store(int const *_pValue) { m_pValue = _pValue; }
};
void fg_Results()
{
	(void)(g_Dispatch / [Value = 42]() -> TCFuture<CKeptExplicit>
// CHECK-MESSAGES-LAMBDA: :[[@LINE-1]]:9: warning: a lambda coroutine returns what refers to its captures, which are gone once what it is given to has run it; return a copy [malterlib-coroutine-lambda-call]
		{
			co_return CKeptExplicit(Value);
		}
	);
	(void)(g_Dispatch / [Value = 42]() -> TCFuture<CKeptImplicit>
// CHECK-MESSAGES-LAMBDA: :[[@LINE-1]]:9: warning: a lambda coroutine returns what refers to its captures, which are gone once what it is given to has run it; return a copy [malterlib-coroutine-lambda-call]
		{
			co_return CKeptImplicit(Value);
		}
	);
	(void)(g_Dispatch / [Value = 42]() -> TCFuture<CForwardedExplicit>
// CHECK-MESSAGES-LAMBDA: :[[@LINE-1]]:9: warning: a lambda coroutine returns what refers to its captures, which are gone once what it is given to has run it; return a copy [malterlib-coroutine-lambda-call]
		{
			co_return CForwardedExplicit(Value);
		}
	);
	(void)(g_Dispatch / [Value = 42]() -> TCFuture<CForwardedImplicit>
// CHECK-MESSAGES-LAMBDA: :[[@LINE-1]]:9: warning: a lambda coroutine returns what refers to its captures, which are gone once what it is given to has run it; return a copy [malterlib-coroutine-lambda-call]
		{
			co_return CForwardedImplicit(Value);
		}
	);
}
}

namespace NSetterLambda {
// Pointed at the object by a lambda through what it is given, also one whose
// call operator has an explicit object, which the lambda is given first.
struct CMyActor : CActor
{
	int m_Value;
	TCFuture<int> f_Explicit(TCActor<> _Other)
	{
		int *pValue = nullptr;
		auto fSet = [](this auto const &, int *&o_pValue, int *_pValue) { o_pValue = _pValue; };
		fSet(pValue, &m_Value);
		co_await fg_ContinueRunningOnActor(_Other);
		co_return *pValue;
// CHECK-MESSAGES-CONTINUE: :[[@LINE-1]]:14: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}
	TCFuture<int> f_Implicit(TCActor<> _Other)
	{
		int *pValue = nullptr;
		auto fSet = [](int *&o_pValue, int *_pValue) { o_pValue = _pValue; };
		fSet(pValue, &m_Value);
		co_await fg_ContinueRunningOnActor(_Other);
		co_return *pValue;
// CHECK-MESSAGES-CONTINUE: :[[@LINE-1]]:14: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor, where using it races with its own actor [malterlib-continue-running-on-actor]
	}
};
}

namespace NOwnObject {
// The explicit object of a member function of the actor is the object of the
// current actor, like `this`.
struct CApp;
struct CReadValue
{
	int operator()(CApp *_pApp) const;
};
struct CApp : CActor
{
	int m_Value = 0;
	int f_ReadExplicit(this CApp &_This) { return CReadValue{}(&_This); }
	int f_ReadImplicit() { return CReadValue{}(this); }
	int f_ReadOther(CApp *_pOther) { return CReadValue{}(_pOther); }
// CHECK-MESSAGES-DISPATCH: :[[@LINE-1]]:55: warning: 'operator()' is given the object of an actor, which it uses like 'this' on the actor the call runs on, {{.*}}
};
int CReadValue::operator()(CApp *_pApp) const
{
	return _pApp->m_Value;
}
}
