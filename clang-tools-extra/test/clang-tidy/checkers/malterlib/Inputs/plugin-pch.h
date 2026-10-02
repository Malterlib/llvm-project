#pragma once
#include "malterlib-concurrency.h"

TCFuture<int> fg_PchSuspend();

template <typename tf_CType>
TCFuture<int> fg_PchRead(tf_CType _Value)
{
	co_await fg_PchSuspend();
	co_return _Value;
}

template <typename t_CType>
struct TCPchBox
{
	TCFuture<int> f_InClass(t_CType _Value)
	{
		co_await fg_PchSuspend();
		co_return _Value;
	}
	TCFuture<int> f_OutOfLine(t_CType _Value);
};

template <typename t_CType>
TCFuture<int> TCPchBox<t_CType>::f_OutOfLine(t_CType _Value)
{
	co_await fg_PchSuspend();
	co_return _Value;
}

template <typename t_CType>
struct TCPchExtern
{
	TCFuture<int> f_Read(t_CType _Value)
	{
		co_await fg_PchSuspend();
		co_return _Value;
	}
	template <typename tf_CType>
	TCFuture<int> f_ReadAs(tf_CType _Value)
	{
		co_await fg_PchSuspend();
		co_return _Value;
	}
};

extern template struct TCPchExtern<int &>;

template <typename t_CType>
inline auto g_pPchValue = t_CType{};

// A member whose prefix each specialization this compile makes excludes.
struct CPchNumberTraits
{
	using CNumber = int;
};
template <typename t_CTraits>
struct TCPchField
{
	typename t_CTraits::CNumber m_pValue;
};

// Also one of an anonymous member union, and a non-type template parameter.
struct CPchBoolTraits
{
	using CBool = bool;
};
template <typename t_CTraits>
struct TCPchUnion
{
	union
	{
		typename t_CTraits::CNumber m_pValue;
	};
};
template <typename t_CTraits, typename t_CTraits::CBool t_fFlag>
struct TCPchFlag
{
};

// Not one of what it is given, `t_CType m_Value`, which is generic.
template <typename t_CType>
struct TCPchGeneric
{
	t_CType m_Value;
};

// And a static member of such a type.
template <typename t_CTraits>
struct TCPchStatic
{
	static inline typename t_CTraits::CNumber ms_pValue = 0;
};

// And a member of a class in one.
template <typename t_CTraits>
struct TCPchOuter
{
	struct CInner
	{
		typename t_CTraits::CNumber m_pValue;
	};
};

// And a parameter of a member function declared in one, and a static member
// of a class in one.
template <typename t_CTraits>
struct TCPchMethod
{
	void f_Run(typename t_CTraits::CNumber _pValue);
};
template <typename t_CTraits>
struct TCPchNested
{
	struct CInner
	{
		static inline typename t_CTraits::CNumber ms_pValue = 0;
	};
};

// And a lambda in the default initializer of a member that depends on them.
template <typename t_CTraits>
struct TCPchInitializer
{
	int m_Value = [](typename t_CTraits::CNumber _pValue) { return 0; }(0);
};

template <typename t_FSignature>
struct TCPchFunction
{
};

template <typename t_CTraits>
struct TCPchReturn
{
	TCPchFunction<void (typename t_CTraits::CBool _fReady)> f_Make();
};

template <typename t_CTraits>
struct TCPchDeclared
{
	typename t_CTraits::CNumber m_pValue;
};

TCPchDeclared<CPchNumberTraits> *fg_PchDeclared();

// And the signature of a function template that a requirement instantiates,
// without its body.
template <typename t_CTraits>
void fg_PchSignature(typename t_CTraits::CNumber _pValue)
{
}

// And the non-type parameters of function and variable templates whose types
// depend on the others.
template <typename tf_CTraits, typename tf_CTraits::CBool tf_fFlag>
void fg_PchFlag()
{
}

template <typename t_CTraits, typename t_CTraits::CBool t_fFlag>
inline int g_PchFlagValue = 0;

// And the default arguments of type parameters of class templates that depend
// on the others.
template <typename t_CTraits,
          typename t_FCallback =
              TCPchFunction<void (typename t_CTraits::CBool _fDone)>>
struct TCPchDefaulted
{
};

// And the members of a class that an explicit instantiation declaration of the
// compile instantiates, whose types depend on what it is made with.
template <typename t_CTraits>
struct TCPchExternField
{
	typename t_CTraits::CNumber m_pValue;
};
