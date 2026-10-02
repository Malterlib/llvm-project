// RUN: %check_clang_tidy -std=c++20-or-later %s malterlib-conceptual-prefix %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}"

namespace NMib::NStorage
{
	template <typename t_CType>
	struct TCSharedPointer
	{
		t_CType *m_pData;
	};
}

namespace NMib::NFunction
{
	template <typename t_CSignature>
	struct TCFunction;

	template <typename t_CReturn, typename ...tp_CParams>
	struct TCFunction<t_CReturn (tp_CParams...)>
	{
	};

	template <typename t_CSignature>
	struct TCFunctionMovable : TCFunction<t_CSignature>
	{
	};
}

struct CListIterator
{
	int &operator*() const;
};

using namespace NMib;
using FFunction = void (int);
using CSubscription = NStorage::TCSharedPointer<int>;

bool g_bGlobal;
int *g_pGlobal;
FFunction *g_fGlobal;
NFunction::TCFunction<void ()> g_fFunctor;
NStorage::TCSharedPointer<int> g_pShared;
// An alias of a smart pointer is a pointer too, unless it is listed as a
// handle, SmartPointerHandleTypes.
CSubscription g_Subscription;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'g_Subscription' is a pointer and should have the 'p' concept prefix after its storage prefix [malterlib-conceptual-prefix]

bool g_Global;
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: 'g_Global' is a boolean and should have the 'b' concept prefix after its storage prefix [malterlib-conceptual-prefix]
int *g_Pointer;
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: 'g_Pointer' is a pointer and should have the 'p' concept prefix after its storage prefix [malterlib-conceptual-prefix]
FFunction *g_pFunction;
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: 'g_pFunction' is a function object and should have the 'f' concept prefix after its storage prefix [malterlib-conceptual-prefix]
NFunction::TCFunctionMovable<void ()> g_Functor;
// CHECK-MESSAGES: :[[@LINE-1]]:39: warning: 'g_Functor' is a function object and should have the 'f' concept prefix after its storage prefix [malterlib-conceptual-prefix]
NStorage::TCSharedPointer<int> g_Shared;
// CHECK-MESSAGES: :[[@LINE-1]]:32: warning: 'g_Shared' is a pointer and should have the 'p' concept prefix after its storage prefix [malterlib-conceptual-prefix]

struct CStruct
{
	bool m_bFlag;
	unsigned m_bBitField : 1;
	static constexpr bool mc_Value = true;
	bool *m_pFlag;
	bool m_Flag;
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: 'm_Flag' is a boolean and should have the 'b' concept prefix after its storage prefix [malterlib-conceptual-prefix]
};

void fg_Function(bool _bFlag, bool const &_bReference, bool &o_bOutput, int *_pValue, int *&o_pValue, char const _String[4], CListIterator _iList, FFunction _fFunction)
{
	bool bLocal = _bFlag;
	auto fLambda = []
		{
		}
	;
	auto *pLocal = _pValue;
	auto pNull = nullptr;
	CListIterator iIterator;

	bool Local = true;
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: 'Local' is a boolean and should have the 'b' concept prefix [malterlib-conceptual-prefix]
	auto Lambda = []
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: 'Lambda' is a function object and should have the 'f' concept prefix [malterlib-conceptual-prefix]
		{
		}
	;
	CListIterator Iterator;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'Iterator' is an iterator and should have the 'i' concept prefix [malterlib-conceptual-prefix]
}

void fg_Bad(bool _Flag, int *_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: '_Flag' is a boolean and should have the 'b' concept prefix after its storage prefix [malterlib-conceptual-prefix]
// CHECK-MESSAGES: :[[@LINE-2]]:30: warning: '_Value' is a pointer and should have the 'p' concept prefix after its storage prefix [malterlib-conceptual-prefix]

template <typename tf_CType, bool tf_bFlag, bool tf_Flag>
// CHECK-MESSAGES: :[[@LINE-1]]:50: warning: 'tf_Flag' is a boolean and should have the 'b' concept prefix after its storage prefix [malterlib-conceptual-prefix]
void fg_Template(tf_CType _Value, tf_CType *_pValue)
{
	tf_CType Value = _Value;
}

struct CPair
{
	bool m_bFirst;
	int *m_pSecond;
};

void fg_Bindings(CPair const &_Pair)
{
	auto [bFirst, pSecond] = _Pair;
	auto [First, Second] = _Pair;
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: 'First' is a boolean and should have the 'b' concept prefix [malterlib-conceptual-prefix]
// CHECK-MESSAGES: :[[@LINE-2]]:15: warning: 'Second' is a pointer and should have the 'p' concept prefix [malterlib-conceptual-prefix]
}

// An alias of a raw pointer named as an iterator is still a pointer.
template <typename t_CType>
using TCAliasIterator = t_CType *;
void fg_AliasTemplateIterator(TCAliasIterator<int> _iValue);
// CHECK-MESSAGES: :[[@LINE-1]]:52: warning: '_iValue' is a pointer and should have the 'p' concept prefix after its storage prefix [malterlib-conceptual-prefix]

using CPointerIterator = int *;
void fg_PointerIterator(CPointerIterator _iValue, CPointerIterator *_pIterator);
// CHECK-MESSAGES: :[[@LINE-1]]:42: warning: '_iValue' is a pointer and should have the 'p' concept prefix after its storage prefix [malterlib-conceptual-prefix]
void fg_PointerIteratorAsPointer(CPointerIterator _pValue);

extern "C" void c_function(bool flag, int *value);

bool NoLint; // NOLINT(malterlib-conceptual-prefix)

// A raw pointer that walks over elements is a pointer, and a pointer to a
// dependent type.
void fg_Iterate(int *_pBegin, int *_pEnd)
{
	for (int *pValue = _pBegin; pValue != _pEnd; ++pValue)
	{
	}
	for (int *iValue = _pBegin; iValue != _pEnd; ++iValue)
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: 'iValue' is a pointer and should have the 'p' concept prefix [malterlib-conceptual-prefix]
	{
	}
}

template <typename tf_CType>
void fg_Visit(tf_CType *_Value)
// CHECK-MESSAGES: :[[@LINE-1]]:25: warning: '_Value' is a pointer and should have the 'p' concept prefix after its storage prefix [malterlib-conceptual-prefix]
{
}

// Only the outer concept is a prefix: a pointer to a boolean is `p`.
bool *g_pbFlag = nullptr;
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: 'g_pbFlag' is a pointer and should have the 'p' concept prefix after its storage prefix [malterlib-conceptual-prefix]
bool *g_pFlag = nullptr;
int **g_ppValue = nullptr;
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: 'g_ppValue' is a pointer and should have the 'p' concept prefix after its storage prefix [malterlib-conceptual-prefix]

// A pointer to a template parameter named as a function type is a function,
// which makes it a function object, or a pointer to a function object.
template <typename tf_FFunction>
void fg_Use(tf_FFunction *_fCallback)
{
}
template <typename tf_FFunction>
void fg_UsePointerToFunctor(tf_FFunction *_pCallback)
{
}

// The function objects of Malterlib.
template <typename t_CSignature>
struct TCActorFunctor
{
	void operator()();
};
template <typename t_CSignature>
struct TCFunctionSmall
{
	void operator()();
};
TCActorFunctor<void ()> g_Handler;
// CHECK-MESSAGES: :[[@LINE-1]]:25: warning: 'g_Handler' is a function object and should have the 'f' concept prefix after its storage prefix [malterlib-conceptual-prefix]
TCFunctionSmall<void ()> g_fCallback;

// Iterators and functors by their names.
struct CIteratorConst
{
	int &operator*() const;
};
void fg_VisitIterator(CIteratorConst _Iterator);
// CHECK-MESSAGES: :[[@LINE-1]]:38: warning: '_Iterator' is an iterator and should have the 'i' concept prefix after its storage prefix [malterlib-conceptual-prefix]
struct COperatorFunctor_Comma
{
	void operator()() const;
};
COperatorFunctor_Comma g_Comma;
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: 'g_Comma' is a function object and should have the 'f' concept prefix after its storage prefix [malterlib-conceptual-prefix]

// A name that only contains what a concept is named for isn't of the concept
// unless it does what the concept does.
struct CIteratorEndSentinel
{
};
void fg_End(CIteratorEndSentinel const &_EndSentinel)
{
}
struct CActorFunctorInfo
{
	int m_Value;
};
void fg_Info(CActorFunctorInfo const &_Info)
{
}
struct CNodeIterator
{
	int &operator*() const;
};
void fg_Node(CNodeIterator const &_Node)
// CHECK-MESSAGES: :[[@LINE-1]]:35: warning: '_Node' is an iterator and should have the 'i' concept prefix after its storage prefix [malterlib-conceptual-prefix]
{
}

// A pack is named for each of its elements.
template <typename ...tfp_CTypes>
void fg_PointerPack(tfp_CTypes *...p_Values)
// CHECK-MESSAGES: :[[@LINE-1]]:36: warning: 'p_Values' is a pointer and should have the 'p' concept prefix after its storage prefix [malterlib-conceptual-prefix]
{
}

// A pointer to a function is a function object, also when its signature
// depends on a template parameter.
template <typename tf_CType>
void fg_DependentCallback(void (*_Callback)(tf_CType))
// CHECK-MESSAGES: :[[@LINE-1]]:34: warning: '_Callback' is a function object and should have the 'f' concept prefix after its storage prefix [malterlib-conceptual-prefix]
{
}

// A function referred to, with a signature that depends on a template
// parameter.
template <typename tf_CValue>
void fg_DependentFunctionReference(void (&_Callback)(tf_CValue));
// CHECK-MESSAGES: :[[@LINE-1]]:43: warning: '_Callback' is a function object and should have the 'f' concept prefix after its storage prefix [malterlib-conceptual-prefix]

// An alias whose name contains Iterator, of a class that isn't one.
using CIteratorEnd = CIteratorEndSentinel;
void fg_StopAt(CIteratorEnd _End);

// A pointer to a data member that depends on a template parameter, and
// Malterlib's zbool.
template <typename t_COwner>
struct TCMemberBox
{
	int t_COwner::*m_Member;
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: 'm_Member' is a pointer and should have the 'p' concept prefix after its storage prefix [malterlib-conceptual-prefix]
};
template <typename t_CType>
struct TCAutoClear
{
	t_CType m_Value{};
	operator t_CType &()
	{
		return m_Value;
	}
};
using zbool = TCAutoClear<bool>;
void fg_AutoClearFlag()
{
	zbool Flag;
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: 'Flag' is a boolean and should have the 'b' concept prefix [malterlib-conceptual-prefix]
	zbool bFlag;
}

// An atomic boolean, an iterator that is a range, and a pack of raw pointers
// that walk over elements, which are pointers.
namespace NMib::NAtomic
{
	template <typename t_CType>
	struct TCAtomic
	{
		t_CType m_Value;
	};
}
NMib::NAtomic::TCAtomic<bool> g_AtomicFlag;
// CHECK-MESSAGES: :[[@LINE-1]]:31: warning: 'g_AtomicFlag' is a boolean and should have the 'b' concept prefix after its storage prefix [malterlib-conceptual-prefix]
NMib::NAtomic::TCAtomic<bool> g_bAtomicFlag;
struct CRangeIterator
{
	int *m_pBegin;
	int *m_pEnd;
	explicit operator bool() const;
	int &operator * () const;
	CRangeIterator &operator ++ ();
};
void fg_RangeIterator(CRangeIterator _rValues)
{
	for (auto rValue = _rValues; rValue; ++rValue)
		(void)*rValue;
}
template <typename ...tfp_CTypes>
void fg_AdvancePack(tfp_CTypes *...p_pValues)
{
	(++p_pValues, ...);
}
template <typename ...tfp_CTypes>
void fg_AdvancePackAsIterators(tfp_CTypes *...p_iValues)
// CHECK-MESSAGES: :[[@LINE-1]]:47: warning: 'p_iValues' is a pointer and should have the 'p' concept prefix after its storage prefix [malterlib-conceptual-prefix]
{
	(++p_iValues, ...);
}

// A pointer to a dependent member type named as a function type can be a
// function.
struct CCallbackTypes
{
	using FCallback = void ();
};
template <typename tf_CCallbacks>
void fg_RunCallback(typename tf_CCallbacks::FCallback *_fCallback);
template <typename tf_CCallbacks>
void fg_RunCallbackPointer(typename tf_CCallbacks::FCallback *_pCallback);

// A pointer to what is named as a function type is either, and Malterlib's
// pointer wrappers are pointers.
template <typename tf_FFunction>
void fg_CallEither(tf_FFunction *_Callback);
// CHECK-MESSAGES: :[[@LINE-1]]:34: warning: '_Callback' is a function or a pointer to a function object and should have the 'f' or 'p' concept prefix after its storage prefix [malterlib-conceptual-prefix]
template <typename tf_FFunction>
void fg_CallFunction(tf_FFunction *_fCallback);
template <typename t_CType>
struct TCAutoClearPtr
{
	t_CType *m_pPointTo;
};
void fg_UseAutoClear(TCAutoClearPtr<int> _Object);
// CHECK-MESSAGES: :[[@LINE-1]]:42: warning: '_Object' is a pointer and should have the 'p' concept prefix after its storage prefix [malterlib-conceptual-prefix]

// A smart pointer or function object whose arguments are template parameters.
template <typename tf_CType>
void fg_Round58Pointer(NMib::NStorage::TCSharedPointer<tf_CType> _Value)
// CHECK-MESSAGES: :[[@LINE-1]]:66: warning: '_Value' is a pointer and should have the 'p' concept prefix after its storage prefix [malterlib-conceptual-prefix]
{
}
template <typename tf_FSignature>
void fg_Round58Function(NMib::NFunction::TCFunction<tf_FSignature> _Value)
// CHECK-MESSAGES: :[[@LINE-1]]:68: warning: '_Value' is a function object and should have the 'f' concept prefix after its storage prefix [malterlib-conceptual-prefix]
{
}

// A deleted operator doesn't make it what the concept needs.
struct CRound72IteratorEndSentinel
{
	void operator++() = delete;
};
void fg_Round72End(CRound72IteratorEndSentinel const &_EndSentinel);

// The partial specialization the arguments of a base select, also when it
// isn't made yet, and an operator brought in by `using`, also deleted, which
// declares one.
namespace NRound73
{
	template <typename t_CType>
	struct TCPointer
	{
		t_CType *m_pValue;
	};
}
template <typename t_CType>
struct TCRound73Base : NRound73::TCPointer<t_CType>
{
};
template <typename t_CType>
struct TCRound73Base<t_CType *>
{
};
template <typename t_CType>
struct TCRound73Wrapper : TCRound73Base<t_CType>
{
};
void fg_Round73Wrapper(TCRound73Wrapper<int *> const &_Value);
struct CRound73DeletedCall
{
	void operator()() = delete;
};
struct CRound73FunctorDeleted : CRound73DeletedCall
{
	using CRound73DeletedCall::operator();
};
struct CRound73FunctorCall : CRound73DeletedCall
{
	void operator()(int _Value);
	using CRound73DeletedCall::operator();
};
void fg_Round73Using(CRound73FunctorDeleted const &_Info, CRound73FunctorCall const &_Call);
// CHECK-MESSAGES: :[[@LINE-1]]:52: warning: '_Info' is a function object and should have the 'f' concept prefix after its storage prefix [malterlib-conceptual-prefix]
// CHECK-MESSAGES: :[[@LINE-2]]:86: warning: '_Call' is a function object and should have the 'f' concept prefix after its storage prefix [malterlib-conceptual-prefix]

// A specialization of a class named like a function object that isn't
// instantiated can't be told.
template <typename t_CType>
struct TCRound76Functor
{
	void operator()() requires false;
};
extern TCRound76Functor<int> g_Round76Value;

// A class without a name is what its bases are.
struct : NFunction::TCFunction<void ()> {} g_Round77Callback;
// CHECK-MESSAGES: :[[@LINE-1]]:44: warning: 'g_Round77Callback' is a function object and should have the 'f' concept prefix after its storage prefix [malterlib-conceptual-prefix]

// A debug auto clear pointer is a smart pointer.
template <typename t_CType>
struct TCAutoClearPtrDebug
{
	t_CType *m_pPointTo;
	operator t_CType *() const
	{
		return m_pPointTo;
	}
	t_CType *operator->() const
	{
		return m_pPointTo;
	}
};
TCAutoClearPtrDebug<int> g_Round81Value;
// CHECK-MESSAGES: :[[@LINE-1]]:26: warning: 'g_Round81Value' is a pointer and should have the 'p' concept prefix after its storage prefix [malterlib-conceptual-prefix]

// A function object type with a call operator of a template, also one whose
// constraint can't hold, which is declared, is one.
struct CRound83NeverFunctor
{
	template <typename tf_CType>
		requires false
	void operator()(tf_CType _Value);
};
CRound83NeverFunctor g_Round83Never;
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: 'g_Round83Never' is a function object and should have the 'f' concept prefix after its storage prefix [malterlib-conceptual-prefix]

// A pointer that keeps bits in its value is a pointer.
template <typename t_CType, unsigned t_nBits = 2>
struct TCBitStorePointer
{
	t_CType *m_pValue;
};
struct CRound86Box
{
	TCBitStorePointer<int> m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:25: warning: 'm_Value' is a pointer and should have the 'p' concept prefix after its storage prefix [malterlib-conceptual-prefix]
	TCBitStorePointer<int> m_pOther;
};

// A dependent specialization of a template whose partial specializations
// aren't function objects can be either.
struct CRound87Functor
{
	void operator()() {}
};
template <typename t_CType>
struct TCRound87Dispatch : CRound87Functor
{
};
template <typename t_CType>
struct TCRound87Dispatch<t_CType *>
{
};
template <typename tf_CType>
void fg_Round87Dispatch(TCRound87Dispatch<tf_CType *> _Value)
{
}

// A class derived from a listed function object is one, also with a deleted
// call operator, and so is one named like one that declares a call operator,
// whatever its constraint, and a base made of a parameter is the
// specialization its argument selects.
template <typename t_CType>
struct TCFunction
{
	void operator()();
};
struct CRound88Task : TCFunction<void ()>
{
	void operator()() = delete;
};
CRound88Task g_Round88Task;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'g_Round88Task' is a function object and should have the 'f' concept prefix after its storage prefix [malterlib-conceptual-prefix]
template <bool t_bEnable>
struct TCRound88Functor
{
	void operator()() requires t_bEnable;
};
TCRound88Functor<false> g_Round88Object;
// CHECK-MESSAGES: :[[@LINE-1]]:25: warning: 'g_Round88Object' is a function object and should have the 'f' concept prefix after its storage prefix [malterlib-conceptual-prefix]
template <typename t_CType>
struct TCUniquePointer
{
	t_CType *operator->() const;
};
template <typename t_CType>
struct TCRound88Base : TCUniquePointer<int>
{
};
template <typename t_CType>
struct TCRound88Base<t_CType *>
{
};
template <typename t_CType>
struct TCRound88Wrapper : TCRound88Base<t_CType *>
{
};
extern TCRound88Wrapper<int> g_Round88Wrapper;

// A specialization that isn't instantiated is only told by the name of its
// template, and one derived from a listed function object is one, whatever
// call operator it declares.
template <typename t_CType>
struct TCRound89Wrapper : TCFunction<void ()>
{
	void operator()() requires false;
};
TCRound89Wrapper<int> g_Round89Object;
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: 'g_Round89Object' is a function object and should have the 'f' concept prefix after its storage prefix [malterlib-conceptual-prefix]
template <typename t_CType>
struct TCRound89Choice
{
};
template <typename t_CType>
struct TCRound89Choice<t_CType *> : TCFunction<void ()>
{
};
void fg_Round89Uninstantiated(TCRound89Choice<int *> const &_Value);
TCRound89Choice<char *> g_Round89Instance;
// CHECK-MESSAGES: :[[@LINE-1]]:25: warning: 'g_Round89Instance' is a function object and should have the 'f' concept prefix after its storage prefix [malterlib-conceptual-prefix]

// A pointer to a member alias template named as a function type.
template <typename t_CType>
struct TCRound91Signatures
{
	template <typename t_CArgument>
	using FCallback = void (t_CArgument _Value);
};
template <typename tf_CType>
void fg_Round91Run(typename TCRound91Signatures<tf_CType>::template FCallback<int> *_fCallback)
{
}
void fg_Round91Use()
{
	fg_Round91Run<int>(nullptr);
}

// The parameters of a specialization of an external template are named by it.
// RUN: rm -rf %t.external && mkdir -p %t.external && printf 'template <typename t_CType> struct TCExternal;\n' > %t.external/external.h
// RUN: printf '#include <external.h>\ntemplate <> struct TCExternal<int> { void f_Run(bool _Flag); };\n' > %t.external/main.cpp
// RUN: clang-tidy --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" --checks='-*,malterlib-conceptual-prefix' %t.external/main.cpp -- -std=c++20 -isystem %t.external 2>&1 | FileCheck --check-prefix=CHECK-EXTERNAL %s
// CHECK-EXTERNAL: main.cpp:2:54: warning: '_Flag' is a boolean and should have the 'b' concept prefix after its storage prefix [malterlib-conceptual-prefix]

// A type named as a function type is a function object, and one named as a
// function object has a type named as a function type, when it is a template
// parameter.
template <typename t_CType>
struct TCRound92Signatures
{
	using FCallback = void ();
};
template <typename tf_FFunction, typename tf_CType>
void fg_Round92Named(tf_FFunction &&_Callback, tf_FFunction &&_fGood, tf_CType &&_fValue, tf_CType &&_Value, typename TCRound92Signatures<tf_CType>::FCallback &_Member)
// CHECK-MESSAGES: :[[@LINE-1]]:37: warning: '_Callback' is a function object and should have the 'f' concept prefix after its storage prefix [malterlib-conceptual-prefix]
// CHECK-MESSAGES: :[[@LINE-2]]:82: warning: '_fValue' is named as a function object, so its type 'tf_CType' should be named as a function type, with the 'F' type prefix [malterlib-conceptual-prefix]
// CHECK-MESSAGES: :[[@LINE-3]]:161: warning: '_Member' is a function object and should have the 'f' concept prefix after its storage prefix [malterlib-conceptual-prefix]
{
}
template <typename t_FCallback>
struct TCRound92Store
{
	t_FCallback m_Callback;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'm_Callback' is a function object and should have the 'f' concept prefix after its storage prefix [malterlib-conceptual-prefix]
	t_FCallback m_fGood;
};

// Function types through references to them.
using FRound93Signature = void ();
using FRound93CallbackReference = FRound93Signature *&;

struct CRound93Functor
{
	void operator ()();
};
using FRound93FunctorReference = CRound93Functor &;

// A conversion template doesn't make a class a function object.
template <typename t_CType>
struct TCRound94Value
{
};
struct CRound94FunctorInfo
{
	template <typename tf_CType>
	operator TCRound94Value<tf_CType>() const
	{
		return {};
	}
};
CRound94FunctorInfo g_Round94Info;

// An alias of a reference named as a function type, and an explicit
// specialization of a variable template, are checked like others.
struct CRound95Action
{
	void operator ()();
};
using FRound95CallbackReference = CRound95Action &;

void fg_Round95Run(FRound95CallbackReference _Callback);
// CHECK-MESSAGES: :[[@LINE-1]]:46: warning: '_Callback' is a function object and should have the 'f' concept prefix after its storage prefix [malterlib-conceptual-prefix]

template <typename t_CType>
inline auto g_Round95Value = 0;

template <>
inline auto g_Round95Value<void> = static_cast<int *>(nullptr);
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'g_Round95Value<void>' is a pointer and should have the 'p' concept prefix after its storage prefix [malterlib-conceptual-prefix]

// A conversion to a type that depends on template parameters doesn't make a
// function object, when the instantiation tells it converts to an `int`.
template <typename t_CType>
struct TCRound99Info
{
	operator t_CType() const;
};

template <typename tf_CType>
void fg_Round99Read(TCRound99Info<tf_CType> const &_Info)
{
}

void fg_Round99UseRead()
{
	TCRound99Info<int> Info;
	fg_Round99Read(Info);
}

// A callable parameter of a generic lambda has no type name to prefix.
void fg_Round100Generic()
{
	auto fCall = [](auto &&_fCallback)
		{
			_fCallback();
		}
	;
	fCall([] {});
}

// An alias template of a function wrapper is one too.
template <typename t_CSignature>
using TCFunctionSmallMovable = TCFunctionSmall<t_CSignature>;

template <typename tf_CSignature>
void fg_Round100Run(TCFunctionSmallMovable<tf_CSignature> _Callback)
// CHECK-MESSAGES: :[[@LINE-1]]:59: warning: '_Callback' is a function object and should have the 'f' concept prefix after its storage prefix [malterlib-conceptual-prefix]
{
}

// An `F` alias brought in with `using`.
namespace NRound101
{
	struct CCallable
	{
		void operator()();
	};
	using FCallback = CCallable;
}
using NRound101::FCallback;
FCallback g_Round101Callback;
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: 'g_Round101Callback' is a function object and should have the 'f' concept prefix after its storage prefix [malterlib-conceptual-prefix]

// Deduced from what is named as a function type.
struct CRound102Action
{
	void operator()();
};
using FRound102Action = CRound102Action;

void fg_Round102Deduced()
{
	FRound102Action fOriginal;
	auto Copy = fOriginal;
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: 'Copy' is a function object and should have the 'f' concept prefix [malterlib-conceptual-prefix]
	auto &Reference = fOriginal;
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: 'Reference' is a function object and should have the 'f' concept prefix [malterlib-conceptual-prefix]
}

// A variable template whose deduced type depends on template parameters.
template <typename t_CType>
auto g_Round103Value = static_cast<t_CType *>(nullptr);
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: 'g_Round103Value' is a pointer and should have the 'p' concept prefix after its storage prefix [malterlib-conceptual-prefix]

int *g_pRound103Use = g_Round103Value<int>;

// A type named with `decltype` of what is named as a function type.
void fg_Round104Copy(FRound102Action _fAction)
{
	decltype(_fAction) Copy = _fAction;
// CHECK-MESSAGES: :[[@LINE-1]]:21: warning: 'Copy' is a function object and should have the 'f' concept prefix [malterlib-conceptual-prefix]
}

// A specialization of a variable template of a system header is named by it.
# 1 "ranges" 1 3
namespace std::ranges
{
	template <typename t_CRange>
	inline constexpr bool enable_borrowed_range = false;
}
# 707 "conceptual-prefix.cpp" 2
struct CRound107Range
{
};
template <>
inline constexpr bool std::ranges::enable_borrowed_range<CRound107Range> = true;

// So is an alias template of one, also with arguments that depend on template
// parameters.
template <typename t_CType>
using TCRound107Subscription = NMib::NStorage::TCSharedPointer<t_CType>;

void fg_Round107Concrete(TCRound107Subscription<int> _Subscription);
// CHECK-MESSAGES: :[[@LINE-1]]:54: warning: '_Subscription' is a pointer and should have the 'p' concept prefix after its storage prefix [malterlib-conceptual-prefix]

template <typename tf_CType>
void fg_Round107Generic(TCRound107Subscription<tf_CType> _Subscription)
// CHECK-MESSAGES: :[[@LINE-1]]:58: warning: '_Subscription' is a pointer and should have the 'p' concept prefix after its storage prefix [malterlib-conceptual-prefix]
{
}

// A member of a type of the traits a template is given takes no concept prefix
// that its instantiations would require, which an argument can change, only
// none that each of them excludes.
struct CRound108Traits
{
	using CPointer = int *;
};

template <typename t_CTraits>
struct TCRound108Box
{
	typename t_CTraits::CPointer m_Value = nullptr;
};

TCRound108Box<CRound108Traits> g_Round108Box;

// A variable template whose instantiations disagree keeps the generic name.
template <typename t_CType>
inline auto g_Round109Value = t_CType{};

int g_Round109Integer = g_Round109Value<int>;
bool g_bRound109Flag = g_Round109Value<bool>;

// A local of a function template deduced from a pointer to its parameter,
// which no argument can make other than a pointer, is a pointer.
template <typename tf_CType>
void fg_Round110Deduced()
{
	auto Value = static_cast<tf_CType *>(nullptr);
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: 'Value' is a pointer and should have the 'p' concept prefix [malterlib-conceptual-prefix]
	(void)Value;
}

void fg_Round110UseDeduced()
{
	fg_Round110Deduced<int>();
}

// A parameter and a local of a function template of a type that is a member
// of what depends on its parameters take no concept prefix that its
// instantiations would require, which an argument can change.
struct CRound111Traits
{
	using CPointer = int *;
};
template <typename tf_CTraits>
void fg_Round111Use(typename tf_CTraits::CPointer _Value)
{
	typename tf_CTraits::CPointer Value = _Value;
	(void)Value;
}
void fg_Round111Run()
{
	fg_Round111Use<CRound111Traits>(nullptr);
}

// A type computed from template parameters, `decltype(_pValue)`, takes no
// concept prefix that its instantiations would require, only none that each of
// them excludes.
template <typename tf_CType>
void fg_Round112Decltype(tf_CType *_pValue)
{
	decltype(_pValue) Value = nullptr;
}
void fg_Round112UseDecltype()
{
	int Value;
	fg_Round112Decltype(&Value);
}

// Round 113: an init capture of a lambda in a template is checked in the instantiations
namespace NRound113Capture
{
template <typename tf_CType>
void fg_Capture(tf_CType *_pValue) {
  auto fAction = [Value = _pValue] { return Value; };
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: 'Value' is a pointer and should have the 'p' concept prefix [malterlib-conceptual-prefix]
  fAction();
}
void fg_Use() { int Value; fg_Capture(&Value); }
}

// Round 113: a structured binding in a template takes no concept prefix that
// its instantiations would require
namespace NRound113Bind
{
template <typename t_CType>
struct TCPair { t_CType m_First, m_Second; };
template <typename tf_CType>
void fg_Bind(tf_CType *_pValue) {
  auto [First, Second] = TCPair<tf_CType *>{_pValue, _pValue};
}
void fg_Use() { int Value; fg_Bind(&Value); }
}

// Round 113: a deduced static member of a class template is what its pattern
// makes it
namespace NRound113Static
{
template <typename t_CType>
struct TCBox {
  static inline auto ms_Value = static_cast<t_CType *>(nullptr);
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: 'ms_Value' is a pointer and should have the 'p' concept prefix after its storage prefix [malterlib-conceptual-prefix]
};
void fg_Use() { (void)TCBox<int>::ms_Value; }
}

// Round 114: a reference to a deduced local in a template is checked in the instantiations
namespace NRound114RefAuto
{
template <typename tf_CType>
void fg_Run(tf_CType *_pValue) {
  auto &Value = _pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: 'Value' is a pointer and should have the 'p' concept prefix [malterlib-conceptual-prefix]
  auto &&Copy = _pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: 'Copy' is a pointer and should have the 'p' concept prefix [malterlib-conceptual-prefix]
}
void fg_Use() { fg_Run<int>(nullptr); }
}

// Round 114: a pack of a dependent member type takes no concept prefix that its
// instantiations would require
namespace NRound114Pack
{
struct CTraits { using CPointer = int *; };
template <typename ...tfp_CTraits>
void fg_Run(typename tfp_CTraits::CPointer ...p_Values) {}
void fg_Use() { fg_Run<CTraits>(nullptr); }
}

// Round 114: a local of a lambda in a constructor initializer is checked in the instantiations
namespace NRound114Initializer
{
template <typename t_CType>
struct TCBox {
  int m_Value;
  TCBox() : m_Value([] {
    auto Value = static_cast<t_CType *>(nullptr);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: 'Value' is a pointer and should have the 'p' concept prefix [malterlib-conceptual-prefix]
    return 0;
  }()) {}
};
TCBox<int> g_Box;
}

// Round 114: a parenthesized declarator is named by its type
namespace NRound114Paren
{
struct CAction { void operator()() {} };
using FAction = CAction;
void fg_Run(FAction (_Action)) {}
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: '_Action' is a function object and should have the 'f' concept prefix after its storage prefix [malterlib-conceptual-prefix]
}

// A generic lambda in a function template is checked through its specializations.
namespace NRound115GenericLambda {
template <typename tf_CType>
void fg_Run() {
  auto fAction = []<typename tf_CInner>(tf_CInner *_pValue) {
    auto Value = _pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: 'Value' is a pointer and should have the 'p' concept prefix [malterlib-conceptual-prefix]
    using FWrong = tf_CInner;
  };
  fAction(static_cast<tf_CType *>(nullptr));
}
void fg_Use() { fg_Run<int>(); }
}

// A parenthesized parameter of a dependent member type, and a local of it, take
// no concept prefix that the instantiations would require.
namespace NRound115ParenParameter {
struct CTraits { using CPointer = int *; };
template <typename tf_CTraits>
void fg_Run(typename tf_CTraits::CPointer (_Value)) {
  using CPointer = typename tf_CTraits::CPointer;
  CPointer Value = _Value;
}
void fg_Use() { fg_Run<CTraits>(nullptr); }
}

// An alias of a reference to a smart pointer is named like an alias of the smart pointer.
namespace NRound116ReferenceAlias {
template <typename t_CType> struct TCSharedPointer {};
using CSubscription = TCSharedPointer<int> &;
TCSharedPointer<int> g_pHandle;
CSubscription g_Subscription = g_pHandle;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'g_Subscription' is a pointer and should have the 'p' concept prefix after its storage prefix [malterlib-conceptual-prefix]
}

// A member whose type is what it is given, through a template, is generic.
namespace NRound116GenericSpecialization {
template <typename t_CType> struct TCAtomic {};
template <typename t_CType>
struct TCBox {
  TCAtomic<t_CType> m_Flag;
};
TCBox<bool> g_Box;
}

// A member of a class template defined outside of it is checked through its instantiations.
namespace NRound117OutOfLine {
template <typename t_CType>
struct TCBox { void f_Run(); };
template <typename t_CType>
void TCBox<t_CType>::f_Run() {
  auto Value = static_cast<t_CType *>(nullptr);
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: 'Value' is a pointer and should have the 'p' concept prefix [malterlib-conceptual-prefix]
  using FValue = t_CType;
}
void fg_Use() { TCBox<int>{}.f_Run(); }
}

// A lambda in a member initializer of a class template is checked through the instantiations of the class.
namespace NRound118MemberInitializerLambda {
template <typename t_CType>
struct TCBox {
  static constexpr int mc_Value = [] {
    auto Value = static_cast<t_CType *>(nullptr);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: 'Value' is a pointer and should have the 'p' concept prefix [malterlib-conceptual-prefix]
    return Value == nullptr;
  }();
};
constexpr int gc_Value = TCBox<int>::mc_Value;
}

// An annotated type is named as the type it annotates.
namespace NRound118AttributedType {
struct CAction { void operator()(); };
using FCallback = CAction;

FCallback [[clang::annotate_type("tag")]] g_Callback;
// CHECK-MESSAGES: :[[@LINE-1]]:43: warning: 'g_Callback' is a function object and should have the 'f' concept prefix after its storage prefix [malterlib-conceptual-prefix]
}

// Round 119: a variable template of a member type of its parameters takes no
// concept prefix that its instantiations would require, but a partial
// specialization deduced from a pointer to its parameter is a pointer.
namespace NRound119VariableTemplate {
struct CTraits { using CPointer = int *; };

template <typename t_CTraits>
inline typename t_CTraits::CPointer g_Value = nullptr;

int *g_pUse = g_Value<CTraits>;

template <typename t_CType>
inline auto g_Partial = 0;

template <typename t_CType>
inline auto g_Partial<t_CType *> = static_cast<t_CType *>(nullptr);
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'g_Partial<t_CType *>' is a pointer and should have the 'p' concept prefix after its storage prefix [malterlib-conceptual-prefix]

int *g_pPartialUse = g_Partial<int *>;
}

// Round 120: a local of a lambda in the initializer of a variable template,
// whose type is a parameter of it, is generic.
namespace NRound120VariableTemplateLambda {
template <typename t_CType>
inline t_CType g_Value = [] {
  t_CType Value{};
  return Value;
}();

bool g_bResult = g_Value<bool>;
}

// Round 122: a pointer local of a lambda in the initializer of a variable
// template, a generic local of a lambda in a default argument, a dependent
// alias of a function object, and a multiplication that isn't a dereference.
namespace NRound122 {
template <typename t_CType>
int g_Result = [] {
  auto Value = static_cast<t_CType *>(nullptr);
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: 'Value' is a pointer and should have the 'p' concept prefix [malterlib-conceptual-prefix]
  return Value ? 1 : 0;
}();

int g_Use = g_Result<int>;

template <typename tf_CType>
void fg_Default(int _Value = [] {
  tf_CType Value{};
  return 0;
}()) {}

void fg_UseDefault() { fg_Default<bool>(); }

template <typename t_FSignature> struct TCFunction {};

template <typename t_CType>
struct TCBox {
  using CCallback = TCFunction<void (t_CType _Value)>;
  CCallback m_Callback;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'm_Callback' is a function object and should have the 'f' concept prefix after its storage prefix [malterlib-conceptual-prefix]
  void f_Run(CCallback _Callback) {}
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: '_Callback' is a function object and should have the 'f' concept prefix after its storage prefix [malterlib-conceptual-prefix]
};

TCBox<int> g_Box;

struct CIteratorScale {
  int operator*(int _Value) const { return _Value * 2; }
};

void fg_Scale(CIteratorScale const &_Scale);
}

// Round 123: what `__typeof__` names is the type it is of.
namespace NRound123 {
struct CAction {
  void operator()();
};
using FAction = CAction;
__typeof__(FAction) g_Action;
// CHECK-MESSAGES: :[[@LINE-1]]:21: warning: 'g_Action' is a function object and should have the 'f' concept prefix after its storage prefix [malterlib-conceptual-prefix]
__typeof__(FAction) g_fAction;
FAction g_fDirect;
__typeof__(g_fDirect) g_TypeOfExpr;
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: 'g_TypeOfExpr' is a function object and should have the 'f' concept prefix after its storage prefix [malterlib-conceptual-prefix]
}

// Round 124: a local of a type `__typeof__` computes takes no concept prefix
// that its instantiations would require, but one deduced from a pointer to a
// parameter is a pointer, also in a lambda of a default argument of a function
// template.
namespace NRound124 {
template <typename tf_CType>
void fg_TypeOf(tf_CType *_pValue) {
  __typeof__(_pValue) Value = _pValue;
}

void fg_UseTypeOf() { fg_TypeOf<int>(nullptr); }

template <typename tf_CType>
void fg_Default(int _Value = [] {
  auto Value = static_cast<tf_CType *>(nullptr);
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: 'Value' is a pointer and should have the 'p' concept prefix [malterlib-conceptual-prefix]
  return 0;
}()) {}

void fg_UseDefault() { fg_Default<int>(); }
}

// Round 125: an annotated template parameter is the parameter.
namespace NRound125 {
template <typename tf_CType>
void fg_Annotated(tf_CType [[clang::annotate_type("callback")]] &&_fValue);
// CHECK-MESSAGES: :[[@LINE-1]]:67: warning: '_fValue' is named as a function object, so its type 'tf_CType' should be named as a function type, with the 'F' type prefix [malterlib-conceptual-prefix]
}

// Round 126: an annotated member of what depends on the parameters takes no
// concept prefix that its instantiations would require.
namespace NRound126 {
struct CTraits {
  using CValue = bool;
};

template <typename tf_CTraits>
void fg_Run(typename tf_CTraits::CValue [[clang::annotate_type("tag")]] _Value) {}

void fg_Use() { fg_Run<CTraits>(true); }
}

// Round 127: a type brought in from what depends on the parameters by a using
// declaration is named as it is.
namespace NRound127 {
template <typename t_CType>
struct TCParent {
  using FCallback = void(t_CType _Value);
};
template <typename t_CType>
struct TCChild : TCParent<t_CType> {
  using typename TCParent<t_CType>::FCallback;
  FCallback *m_fCallback = nullptr;
};
TCChild<int> g_Child;
}

// Round 129: a type a using declaration brings in from a base that depends on
// the parameters takes no concept prefix that its instantiations would
// require, and an iterator can have non-member operators.
namespace NRound129
{
template <typename t_CType>
struct TCParent {
  using CPointer = t_CType *;
};

template <typename t_CType>
struct TCChild : TCParent<t_CType> {
  using typename TCParent<t_CType>::CPointer;
  CPointer m_Value;
};

TCChild<int> g_Child;

struct CIterator {
  int *m_pValue;
};

int &operator*(CIterator const &_iIterator) { return *_iIterator.m_pValue; }

CIterator &operator++(CIterator &_iIterator) {
  ++_iIterator.m_pValue;
  return _iIterator;
}

CIterator g_Value{};
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: 'g_Value' is an iterator and should have the 'i' concept prefix after its storage prefix [malterlib-conceptual-prefix]

struct CFriendIterator {
  int *m_pValue;
  friend int &operator*(CFriendIterator const &_iIterator) {
    return *_iIterator.m_pValue;
  }
  friend CFriendIterator &operator++(CFriendIterator &_iIterator) {
    ++_iIterator.m_pValue;
    return _iIterator;
  }
};

CFriendIterator g_Friend{};
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: 'g_Friend' is an iterator and should have the 'i' concept prefix after its storage prefix [malterlib-conceptual-prefix]
}

// Round 130: a type in parentheses or annotated is what it is written as, and
// a reference to an alias of a reference is the alias.
namespace NRound130
{
template <typename tf_CType>
void fg_Store(NMib::NStorage::TCSharedPointer<tf_CType>(_Value)) {}
// CHECK-MESSAGES: :[[@LINE-1]]:57: warning: '_Value' is a pointer and should have the 'p' concept prefix after its storage prefix [malterlib-conceptual-prefix]

template <typename tf_CType>
void fg_Call(NMib::NFunction::TCFunction<void(tf_CType)>
                 [[clang::annotate_type("callback")]] _Callback) {}
// CHECK-MESSAGES: :[[@LINE-1]]:55: warning: '_Callback' is a function object and should have the 'f' concept prefix after its storage prefix [malterlib-conceptual-prefix]

void fg_Use() {
  fg_Store(NMib::NStorage::TCSharedPointer<int>{});
  fg_Call(NMib::NFunction::TCFunction<void(int)>{});
}

using CSubscription = NMib::NStorage::TCSharedPointer<int> &;
void fg_Read(CSubscription &_Subscription);
// CHECK-MESSAGES: :[[@LINE-1]]:29: warning: '_Subscription' is a pointer and should have the 'p' concept prefix after its storage prefix [malterlib-conceptual-prefix]

struct CAction {
  void operator()();
};
using FCallbackReference = CAction &;
void fg_Call(FCallbackReference &_Callback);
// CHECK-MESSAGES: :[[@LINE-1]]:34: warning: '_Callback' is a function object and should have the 'f' concept prefix after its storage prefix [malterlib-conceptual-prefix]
}

// Round 131: an iterator can have non-member operator templates, which are
// instantiated for it.
namespace NRound131
{
template <typename t_CType>
struct TCIterator {
  t_CType *m_pValue;
};

template <typename tf_CType>
tf_CType &operator*(TCIterator<tf_CType> const &_iIterator) {
  return *_iIterator.m_pValue;
}

template <typename tf_CType>
TCIterator<tf_CType> &operator++(TCIterator<tf_CType> &_iIterator) {
  ++_iIterator.m_pValue;
  return _iIterator;
}

void fg_Test() {
  int Values[2]{};
  TCIterator<int> Walk{Values};
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: 'Walk' is an iterator and should have the 'i' concept prefix [malterlib-conceptual-prefix]
  *Walk = 42;
  ++Walk;
}
}

// Round 132: a type through an alias template of a member type that depends
// on the parameters takes no concept prefix that its instantiations would
// require, and an iterator can have operators in the parent of an inline
// namespace.
namespace NRound132
{
template <typename t_CType>
using TCIdentity = t_CType;

struct CTraits {
  using CValue = bool;
};

template <typename t_CTraits>
struct TCBox {
  TCIdentity<typename t_CTraits::CValue> m_Flag;
};

TCBox<CTraits> g_Box;

namespace NRange
{
inline namespace NImplementation
{
struct CIterator {
  int *m_pValue;
};
}

int &operator*(CIterator const &_iValue) { return *_iValue.m_pValue; }

CIterator &operator++(CIterator &_iValue) {
  ++_iValue.m_pValue;
  return _iValue;
}
}

NRange::CIterator g_Position;
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: 'g_Position' is an iterator and should have the 'i' concept prefix after its storage prefix [malterlib-conceptual-prefix]
}

// Round 133: a pointer to an alias template named as a function type can be
// a function.
namespace NRound133
{
template <typename t_CType>
using FCallback = t_CType;

template <typename tf_CType>
void fg_Run(FCallback<tf_CType> *_fCallback) {}

void fg_Test() { fg_Run<void()>(nullptr); }
}

// Round 136: the variable of a catch in a template, of a type `decltype`
// computes, takes no concept prefix that its instantiations would require.
namespace NRound136
{
template <typename tf_CType>
void fg_Run(tf_CType *_pValue) {
  try {
    throw _pValue;
  } catch (decltype(_pValue) _Value) {
  }
}

void fg_Use() { fg_Run(static_cast<int *>(nullptr)); }
}

namespace NRound137 {
// A generic lambda in the initializer of a variable template.
template <typename t_CType>
inline auto g_fCallback = []<typename tf_CType>() {
  auto Value = static_cast<tf_CType *>(nullptr);
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: 'Value' is a pointer and should have the 'p' concept prefix [malterlib-conceptual-prefix]
  auto pValue = static_cast<tf_CType *>(nullptr);
};

void fg_Use() { g_fCallback<int>.template operator()<int>(); }

// A lambda in the initializer of a partial specialization of one.
template <typename t_CType>
inline int g_Value = 0;

template <typename t_CType>
inline int g_Value<t_CType *> = [] {
  auto Value = static_cast<t_CType *>(nullptr);
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: 'Value' is a pointer and should have the 'p' concept prefix [malterlib-conceptual-prefix]
  auto pValue = static_cast<t_CType *>(nullptr);
  return Value != pValue;
}();

int g_Use = g_Value<int *>;
}

namespace NRound138 {
// A constrained member operator template that is instantiated, which its
// constraints allow.
struct CIterator {
  template <typename tf_CType = int>
    requires(sizeof(tf_CType) > 0)
  CIterator &operator++() { return *this; }
};

CIterator g_Begin;
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: 'g_Begin' is an iterator and should have the 'i' concept prefix after its storage prefix [malterlib-conceptual-prefix]
CIterator g_iEnd;
void fg_Test() {
  ++g_Begin;
  ++g_iEnd;
}
}

namespace NRound139 {
struct CTraits {
  using CPointer = int *;
};

// A parameter of a lambda in a function template, of a member of traits, takes
// no concept prefix that the lambda's instantiations would require.
template <typename tf_CTraits>
void fg_Lambda() {
  auto fUse = [](typename tf_CTraits::CPointer _Value) {};
  auto fGood = [](typename tf_CTraits::CPointer _pValue) {};
  fUse(nullptr);
  fGood(nullptr);
}

void fg_RunLambda() { fg_Lambda<CTraits>(); }

// A parameter of a friend defined in a class template takes none either.
template <typename t_CTraits>
struct TCBox {
  friend void fg_Use(TCBox _Box, typename t_CTraits::CPointer _Value) {}
  friend void fg_Good(TCBox _Box, typename t_CTraits::CPointer _pValue) {}
};

void fg_RunFriend() {
  fg_Use(TCBox<CTraits>{}, nullptr);
  fg_Good(TCBox<CTraits>{}, nullptr);
}
}

namespace NRound140 {
// A lambda in a static assertion of a class template.
template <typename t_CType>
struct TCProbe {
  static_assert([] {
    auto Value = static_cast<t_CType *>(nullptr);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: 'Value' is a pointer and should have the 'p' concept prefix [malterlib-conceptual-prefix]
    auto pValue = static_cast<t_CType *>(nullptr);
    return Value == pValue;
  }());
};

TCProbe<int> g_Probe;

// A boolean that clears itself, `TCAutoClearInt<bool>`.
template <typename t_CInt, t_CInt t_ClearValue = 0>
struct TCAutoClearInt {
  t_CInt m_Value = t_ClearValue;
  operator t_CInt &() { return m_Value; }
};

TCAutoClearInt<bool> g_Enabled;
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: 'g_Enabled' is a boolean and should have the 'b' concept prefix after its storage prefix [malterlib-conceptual-prefix]
TCAutoClearInt<bool> g_bEnabled;
TCAutoClearInt<int> g_Count;
}

namespace NRound141 {
struct CTraits {
  using CPointer = int *;
};

// A local of a friend template defined in a class template, of a member of
// traits, takes no concept prefix that its instantiations would require.
template <typename t_CTraits>
struct TCBox {
  template <typename tf_CValue>
  friend void fg_Run(TCBox _Box, tf_CValue _Value) {
    typename t_CTraits::CPointer Value = nullptr;
    typename t_CTraits::CPointer pValue = nullptr;
  }
};

void fg_Use() { fg_Run(TCBox<CTraits>{}, 0); }

// A member of a partial specialization of a member template of a class
// template takes none either.
template <typename t_CTraits>
struct TCOuter {
  template <typename t_CType>
  struct TCInner;
  template <typename t_CType>
  struct TCInner<t_CType *> {
    typename t_CTraits::CPointer m_Value;
    typename t_CTraits::CPointer m_pValue;
  };
};

TCOuter<CTraits>::TCInner<int *> g_Inner;
}

namespace NRound145 {
struct CTraits {
  using CPointer = int *;
};

// A parameter of a member function defined outside of its class template, of a
// member of traits, takes no concept prefix that its instantiations would
// require.
template <typename t_CTraits>
struct TCBox {
  void f_Run(typename t_CTraits::CPointer _pValue);
  void f_Good(typename t_CTraits::CPointer _pValue);
};

template <typename t_CTraits>
void TCBox<t_CTraits>::f_Run(typename t_CTraits::CPointer _Value) {}

template <typename t_CTraits>
void TCBox<t_CTraits>::f_Good(typename t_CTraits::CPointer _pValue) {}

void fg_Use() {
  TCBox<CTraits>{}.f_Run(nullptr);
  TCBox<CTraits>{}.f_Good(nullptr);
}

// A lambda in the bound of a member array of a class template.
template <typename t_CType>
struct TCBuffer {
  int m_Data[([] {
    auto Value = static_cast<t_CType *>(nullptr);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: 'Value' is a pointer and should have the 'p' concept prefix [malterlib-conceptual-prefix]
    return 1;
  }())];
};

TCBuffer<int> g_Buffer;
}

namespace NRound146 {
struct CTraits
{
	using CPointer = int *;
};

// A parameter of a lambda in a base of a class template takes no concept prefix
// that its instantiations would require.
template <typename t_CTraits>
struct TCBox : decltype([](typename t_CTraits::CPointer _Value) {})
{
};

TCBox<CTraits> g_Box;
}

namespace NRound149 {
template <typename t_FSignature>
struct TCFunction
{
	void operator()();
};
template <typename t_CType>
struct TCWrapper : TCFunction<void()>
{
};
// An explicit specialization that can't be what is written.
template <>
struct TCWrapper<int>
{
};

template <typename tf_CType>
void fg_Run(TCWrapper<tf_CType *> _Callback) {}
// CHECK-MESSAGES: :[[@LINE-1]]:35: warning: '_Callback' is a function object and should have the 'f' concept prefix after its storage prefix [malterlib-conceptual-prefix]

void fg_Use()
{
	fg_Run(TCWrapper<int *>{});
}
}

namespace NRound150 {
template <typename t_CType>
struct TCVectorIterator
{
	t_CType *operator->() const;
};

// An iterator of a class template written with parameters.
template <typename tf_CType>
void fg_Iterate(TCVectorIterator<tf_CType> _Cursor) {}
// CHECK-MESSAGES: :[[@LINE-1]]:44: warning: '_Cursor' is an iterator and should have the 'i' concept prefix after its storage prefix [malterlib-conceptual-prefix]

// A pointer to what an alias names a function pointer.
template <typename t_CType>
using FCallback = void (*)(t_CType _Value);

template <typename tf_CType>
void fg_Callback(FCallback<tf_CType> *_fCallback) {}
// CHECK-MESSAGES: :[[@LINE-1]]:39: warning: '_fCallback' is a pointer and should have the 'p' concept prefix after its storage prefix [malterlib-conceptual-prefix]

void fg_Use()
{
	fg_Iterate(TCVectorIterator<int>{});
	fg_Callback<int>(nullptr);
}
}

namespace NRound153 {
template <typename t_CInt, t_CInt t_ClearValue = 0>
struct TCAutoClearInt {
  t_CInt m_Value = t_ClearValue;
  operator t_CInt() const { return m_Value; }
};
template <bool t_bInitial>
struct TCFlags {
  TCAutoClearInt<bool, t_bInitial> m_Enabled;
// CHECK-MESSAGES: :[[@LINE-1]]:36: warning: 'm_Enabled' is a boolean and should have the 'b' concept prefix after its storage prefix [malterlib-conceptual-prefix]
};
TCFlags<true> g_Flags;

struct CTraits {
  using CBoolean = bool;
};
// A parameter of a lambda in the exception specification takes no concept
// prefix that its instantiations would require.
template <typename tf_CTraits>
void fg_Noexcept() noexcept([](typename tf_CTraits::CBoolean _Value) { return _Value; }(true)) {}
void fg_UseNoexcept() { fg_Noexcept<CTraits>(); }
}

namespace NRound154 {
// Through an alias of a parameter named as a function type.
template <typename tf_FCallback>
void fg_Run(tf_FCallback const &_fCallback) {
  using CCallback = tf_FCallback;
  CCallback const &Callback = _fCallback;
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: 'Callback' is a function object and should have the 'f' concept prefix [malterlib-conceptual-prefix]
  Callback();
}
void fg_Test() {
  auto fCallback = [] {};
  fg_Run(fCallback);
}
}

namespace NRound155 {
// An atomic pointer and an atomic function.
NMib::NAtomic::TCAtomic<int *> g_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:32: warning: 'g_Value' is a pointer and should have the 'p' concept prefix after its storage prefix [malterlib-conceptual-prefix]
NMib::NAtomic::TCAtomic<void (*)()> g_Callback;
// CHECK-MESSAGES: :[[@LINE-1]]:37: warning: 'g_Callback' is a function object and should have the 'f' concept prefix after its storage prefix [malterlib-conceptual-prefix]
NMib::NAtomic::TCAtomic<int *> g_pValue;
NMib::NAtomic::TCAtomic<void (*)()> g_fCallback;
}
namespace NRound155Types {
struct CIterator {};
}
namespace NRound155Operators {
int &operator*(NRound155Types::CIterator const &_iValue);
}
namespace NRound155Types {
using NRound155Operators::operator*;
}
namespace NRound155 {
// An iterator by an operator brought in by a using declaration.
NRound155Types::CIterator g_Iterator;
// CHECK-MESSAGES: :[[@LINE-1]]:27: warning: 'g_Iterator' is an iterator and should have the 'i' concept prefix after its storage prefix [malterlib-conceptual-prefix]
void fg_Use() { *g_Iterator = 1; }
}

namespace NRound156 {
// A pointer to what is named as a function type can be a function.
template <typename t_CType> struct TCAtomic {};
template <typename t_FCallback>
struct TCBox {
  TCAtomic<t_FCallback *> m_fCallback;
};
TCBox<void()> g_Box;
}
namespace NRound156Tag {
struct CTag {};
}
namespace NRound156Range {
template <typename t_CTag> struct TCIterator {};
}
namespace NRound156Tag {
NRound156Range::TCIterator<CTag> &operator++(NRound156Range::TCIterator<CTag> &_iIterator);
int &operator*(NRound156Range::TCIterator<CTag> const &_iIterator);
}
namespace NRound156 {
// Iterator operators in the namespace of a template argument.
void fg_Use() {
  NRound156Range::TCIterator<NRound156Tag::CTag> Value;
// CHECK-MESSAGES: :[[@LINE-1]]:50: warning: 'Value' is an iterator and should have the 'i' concept prefix [malterlib-conceptual-prefix]
  ++Value;
  (void)*Value;
}
}

namespace NRound157Range {
template <typename... tp_CTags> struct TCIterator {};
}
namespace NRound157Tag {
struct CTag {};
int operator*(NRound157Range::TCIterator<CTag> const &_iIterator);
}
namespace NRound157 {
// Iterator operators in the namespace of an argument of a pack.
void fg_Test(NRound157Range::TCIterator<NRound157Tag::CTag> _Iterator) { (void)*_Iterator; }
// CHECK-MESSAGES: :[[@LINE-1]]:61: warning: '_Iterator' is an iterator and should have the 'i' concept prefix after its storage prefix [malterlib-conceptual-prefix]
}

namespace NRound158 {
// Through an alias template of a parameter named as a function type.
template <typename t_CType>
using TCIdentity = t_CType;
template <typename tf_FCallback>
void fg_Run(TCIdentity<tf_FCallback> _Callback) {
// CHECK-MESSAGES: :[[@LINE-1]]:38: warning: '_Callback' is a function object and should have the 'f' concept prefix after its storage prefix [malterlib-conceptual-prefix]
  _Callback();
}
void fg_Use() { fg_Run([] {}); }
}

namespace NRound159 {
// A specialization for another value can't be what is written.
template <typename t_CType, bool t_bEnd>
struct TCIterator {
  t_CType &operator*();
};
template <typename t_CType>
struct TCIterator<t_CType, true> {};
template <typename tf_CType>
void fg_Run(TCIterator<tf_CType, false> _Wrong) {}
// CHECK-MESSAGES: :[[@LINE-1]]:41: warning: '_Wrong' is an iterator and should have the 'i' concept prefix after its storage prefix [malterlib-conceptual-prefix]
void fg_Use() { fg_Run(TCIterator<int, false>{}); }

// An operator whose constraint holds where it is used.
template <bool t_bEnabled>
struct TCConstrainedIterator {
  TCConstrainedIterator &operator++()
    requires t_bEnabled;
};
TCConstrainedIterator<true> g_Position;
// CHECK-MESSAGES: :[[@LINE-1]]:29: warning: 'g_Position' is an iterator and should have the 'i' concept prefix after its storage prefix [malterlib-conceptual-prefix]
void fg_Step() { ++g_Position; }
}

namespace NRound160 {
// An atomic pointer to what is named as a function type needs either.
template <typename t_CType> struct TCAtomic {};
template <typename tf_FCallback>
void fg_Test(TCAtomic<tf_FCallback *> _Callback) {}
// CHECK-MESSAGES: :[[@LINE-1]]:39: warning: '_Callback' is a function or a pointer to a function object and should have the 'f' or 'p' concept prefix after its storage prefix [malterlib-conceptual-prefix]
using FCallback = void();
void fg_Use() { fg_Test(TCAtomic<FCallback *>{}); }
}

namespace NRound162 {
// A template template parameter named as a function type.
template <typename t_CType>
using TFCallback = void(t_CType _Value);
template <template <typename> typename tf_TFCallback>
void fg_Test(tf_TFCallback<int> *_fCallback) {}
void fg_Invoke(int _Value) {}
void fg_Use() { fg_Test<TFCallback>(&fg_Invoke); }
}

namespace NRound163 {
// A parameter of a requires expression isn't one of the function.
struct CTraits {
  using CPointer = int *;
};
template <typename tf_CTraits>
void fg_Requires(bool _bFlag) {
  static_assert(requires(typename tf_CTraits::CPointer _pValue) { _pValue; });
}
void fg_Use() { fg_Requires<CTraits>(true); }
}

namespace NRound164 {
struct CTraits {
  using CPointer = int *;
  using CBool = bool;
};
// A dependent field of an anonymous member union takes no concept prefix that
// its instantiations would require.
template <typename t_CTraits> struct TCBox {
  union {
    typename t_CTraits::CPointer m_Value;
  };
};
TCBox<CTraits> g_Box;
// A non-type template parameter of a dependent type takes none either.
template <typename t_CTraits, typename t_CTraits::CBool t_Flag>
struct TCFlag {};
TCFlag<CTraits, true> g_Flag;
template <typename tf_CTraits, typename tf_CTraits::CBool tf_Flag>
int fg_Flag() {
  return tf_Flag;
}
int g_Value = fg_Flag<CTraits, true>();
}

namespace NRound166 {
// Copies of what is named as a function type.
struct CAction {
  void operator()() const {}
};
template <typename tf_FAction> void fg_Use(tf_FAction _fAction) {
  auto Copy = _fAction;
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: 'Copy' is a function object and should have the 'f' concept prefix [malterlib-conceptual-prefix]
  decltype(_fAction) Other = _fAction;
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: 'Other' is a function object and should have the 'f' concept prefix [malterlib-conceptual-prefix]
  Copy();
  Other();
}
void fg_Test() { fg_Use(CAction{}); }
// A pack of non-type parameters of a dependent type, and one of a variable
// template, take no concept prefix that their instantiations would require.
struct CTraits {
  using CBool = bool;
};
template <typename t_CTraits, typename t_CTraits::CBool... tp_Flags>
struct TCFlags {};
TCFlags<CTraits, true, false> g_Flags;
template <typename t_CTraits, typename t_CTraits::CBool t_Flag>
int g_Selected = 0;
int g_Used = g_Selected<CTraits, true>;
}

namespace NRound167 {
// An iterator whose operators are found through its base.
namespace NBase {
struct CBase {};
} // namespace NBase
namespace NIterators {
struct CIterator : NBase::CBase {};
} // namespace NIterators
namespace NBase {
int &operator*(NIterators::CIterator const &_iIterator);
NIterators::CIterator &operator++(NIterators::CIterator &_iIterator);
} // namespace NBase
void fg_Run() {
  NIterators::CIterator Iterator;
// CHECK-MESSAGES: :[[@LINE-1]]:25: warning: 'Iterator' is an iterator and should have the 'i' concept prefix [malterlib-conceptual-prefix]
  (void)*Iterator;
  ++Iterator;
}
}

namespace NRound168 {
// Dependent parameters and members of member templates of a class template,
// and of a partial specialization of a variable template, take no concept
// prefix that their instantiations would require.
struct CTraits {
  using CBool = bool;
  using CPointer = int *;
};
template <class t_CTraits> struct TCBox {
  template <typename t_CTraits::CBool tf_Flag> static void fs_Run() {}
  template <class t_CUnused>
  static inline typename t_CTraits::CPointer ms_Value = nullptr;
};
void fg_Test() { TCBox<CTraits>::fs_Run<true>(); }
int *g_pUsed = TCBox<CTraits>::ms_Value<int>;
template <class t_CTraits, typename t_CTraits::CBool t_bFlag, class t_CType>
int g_Value = 0;
template <class t_CTraits, typename t_CTraits::CBool t_Flag>
int g_Value<t_CTraits, t_Flag, int> = 1;
int g_Used = g_Value<CTraits, true, int>;
}

namespace NRound169 {
// A parameter of a lambda in the return type takes no concept prefix that its
// instantiations would require.
struct CTraits {
  using CPointer = int *;
};
template <typename tf_CTraits>
auto fg_Make() -> decltype([](typename tf_CTraits::CPointer _Value) {}) {
  return {};
}
void fg_Use() {
  auto fMade = fg_Make<CTraits>();
  fMade(nullptr);
}
}

namespace NRound170 {
// Not of a partial specialization a dependent member type can select.
template <typename t_FSignature> struct TCFunction {
  void operator()() {}
};
template <typename t_CType> struct TCWrapper : TCFunction<void()> {};
template <typename t_CType> struct TCWrapper<t_CType *> {};
struct CTraits {
  using CValue = int *;
};
template <typename tf_CTraits>
void fg_Run(TCWrapper<typename tf_CTraits::CValue> const &_Value) {}
void fg_Use() { fg_Run<CTraits>({}); }
// A copy of a forwarding parameter named as a function type.
struct CAction {
  void operator()() {}
};
template <typename tf_FAction> void fg_Forward(tf_FAction &&_fAction) {
  auto Copy = _fAction;
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: 'Copy' is a function object and should have the 'f' concept prefix [malterlib-conceptual-prefix]
  Copy();
}
void fg_UseForward() {
  CAction Action;
  fg_Forward(Action);
}
}

namespace NRound171 {
// A pack of parameters of a definition outside of its class takes no concept
// prefix that its instantiations would require.
struct CTraits {
  using CBool = bool;
};
template <typename... tp_CTraits> struct TCBox {
  void f_Run(typename tp_CTraits::CBool... p_bFlags);
};
template <typename... tp_CTraits>
void TCBox<tp_CTraits...>::f_Run(typename tp_CTraits::CBool... p_Flags) {}
void fg_Test() { TCBox<CTraits>{}.f_Run(true); }
}

namespace NRound172 {
// A template template parameter named as a function type.
template <typename t_CType> using TFCallback = void (*)();
template <template <typename t_CType> class tf_TFCallback>
void fg_Invoke(tf_TFCallback<int> _Callback) {
// CHECK-MESSAGES: :[[@LINE-1]]:35: warning: '_Callback' is a function object and should have the 'f' concept prefix after its storage prefix [malterlib-conceptual-prefix]
  _Callback();
}
void fg_Action() {}
void fg_Use() { fg_Invoke<TFCallback>(&fg_Action); }
}

namespace NRound175 {
// An iterator whose dereference ADL finds in the namespace of an enum it is
// of.
namespace NData {
enum EItem { EItem_Value };
}
namespace NRange {
template <typename t_CData> struct TCIterator {};
}
namespace NData {
EItem operator*(NRange::TCIterator<EItem> const &_iIterator) { return EItem_Value; }
}
void fg_Use() {
  NRange::TCIterator<NData::EItem> Value;
// CHECK-MESSAGES: :[[@LINE-1]]:36: warning: 'Value' is an iterator and should have the 'i' concept prefix [malterlib-conceptual-prefix]
  (void)*Value;
  NRange::TCIterator<NData::EItem> iValue;
  (void)*iValue;
}
}

namespace NRound178 {
// A lambda in a default argument of a declaration whose definition is apart.
template <typename tf_CType>
void fg_Default(int _Value = [] {
  auto Value = static_cast<tf_CType *>(nullptr);
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: 'Value' is a pointer and should have the 'p' concept prefix [malterlib-conceptual-prefix]
  return 0;
}());
template <typename tf_CType>
void fg_Default(int _Value) {}
// A lambda in the initializer of a member of a local class.
template <typename tf_CType>
void fg_Local() {
  struct CLocal {
    int m_Value = [] {
      auto Value = static_cast<tf_CType *>(nullptr);
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: 'Value' is a pointer and should have the 'p' concept prefix [malterlib-conceptual-prefix]
      return 0;
    }();
  };
  CLocal Local;
}
void fg_Use() {
  fg_Default<int>();
  fg_Local<int>();
}
}

namespace NRound179 {
// A parameter of a member variable template, not of the class.
struct CTraits {
  using CPointer = int *;
};
template <bool t_bEnabled, typename t_CTraits>
struct TCBox {
  template <typename t_CTraits::CPointer t_pAddress>
  static constexpr int mc_Value = 1;
};
constexpr int gc_Result = TCBox<true, CTraits>::mc_Value<nullptr>;
}

namespace NRound180 {
struct CTraits {
  using CBool = bool;
};
// A dependent parameter of a member variable template takes no concept prefix
// that its instantiations would require.
template <typename t_CTraits>
struct TCBox {
  template <typename t_CTraits::CBool t_Flag>
  static constexpr int mc_Value = 1;
};
constexpr int gc_Result = TCBox<CTraits>::mc_Value<true>;
// Also one of a generic lambda in a template.
template <typename tf_CTraits>
void fg_Run() {
  auto fCallback = []<typename tf_CTraits::CBool tf_Flag>() {};
  fCallback.template operator()<true>();
}
void fg_Test() { fg_Run<CTraits>(); }
}

namespace NRound181 {
struct CTraits {
  using CBool = bool;
};
// A dependent parameter of a friend function template defined in a class
// template takes no concept prefix that its instantiations would require.
template <typename t_CTraits>
struct TCBox {
  template <typename t_CTraits::CBool tf_Flag>
  friend void fg_Run(TCBox _Box) {}
};
void fg_Use() { fg_Run<true>(TCBox<CTraits>{}); }
// An iterator whose dereference ADL finds through a pointer to a member.
namespace NData {
struct CNode {};
}
namespace NRange {
template <typename t_CMember> struct TCIterator {};
}
namespace NData {
int &operator*(NRange::TCIterator<int CNode::*> const &_iIterator);
}
void fg_Iterate(NRange::TCIterator<int NData::CNode::*> _Iterator) { (void)*_Iterator; }
// CHECK-MESSAGES: :[[@LINE-1]]:57: warning: '_Iterator' is an iterator and should have the 'i' concept prefix after its storage prefix [malterlib-conceptual-prefix]
}

namespace NRound184 {
// A wrapper that is a function object through a base that depends on what it
// is given takes no concept prefix that its instantiations would require,
// which an argument can change.
template <typename t_FSignature> struct TCFunction;
template <typename t_CResult>
struct TCFunction<t_CResult()> {
  t_CResult operator()() const { return {}; }
};
template <typename t_CResult>
struct TCDeferred : TCFunction<t_CResult()> {};
template <typename tf_CResult>
void fg_Use(TCDeferred<tf_CResult> _Callback) { _Callback(); }
void fg_Run() { fg_Use(TCDeferred<int>{}); }
}

namespace NRound185 {
// A member of a class template given its parameters, whose instantiations
// derive from a function object, takes no concept prefix that they would
// require, which an argument can change.
template <typename t_FSignature>
struct TCStorage : NFunction::TCFunctionMovable<t_FSignature> {};
template <typename t_FSignature>
struct TCOwner {
  TCStorage<t_FSignature> m_Callback;
};
TCOwner<void ()> g_Owner;
// A parameter of a function type in the type of a member, of a member of
// traits, takes none either.
struct CTraits {
  using CPointer = int *;
};
template <typename t_CTraits>
struct TCBox {
  NFunction::TCFunction<void (typename t_CTraits::CPointer _Value)> m_fCallback;
  NFunction::TCFunction<void (typename t_CTraits::CPointer _pValue)> m_fGood;
};
TCBox<CTraits> g_Box;
// Also of a local of a function template.
template <typename tf_CTraits>
void fg_Local() {
  NFunction::TCFunction<void (typename tf_CTraits::CPointer _Value)> fCallback;
}
void fg_CallLocal() { fg_Local<CTraits>(); }
}

namespace NRound186 {
// What only forwards a parameter, through the `CType` of a template given it
// or a transformation of it, is generic like it.
template <typename t_CType>
struct TCIdentity {
  using CType = t_CType;
};
template <typename tf_CType>
void fg_Run(typename TCIdentity<tf_CType>::CType &_Value) {}
template <typename tf_CType>
void fg_Forward(__remove_reference_t(tf_CType) &_Value) {}
void fg_Use() {
  bool bValue{};
  fg_Run<bool>(bValue);
  fg_Forward<bool>(bValue);
}
// A parameter pack of a function type in the type of a member, of a member of
// traits, takes no concept prefix that its instantiations would require.
struct CTraits {
  using CBool = bool;
};
template <typename... tp_CTraits>
struct TCBox {
  NFunction::TCFunction<void (typename tp_CTraits::CBool... p_Flags)> m_fCallback;
  NFunction::TCFunction<void (int _Count, typename tp_CTraits::CBool... p_Flags, int *_pLast)> m_fAround;
};
TCBox<CTraits, CTraits> g_Box;
}

namespace NRound187 {
// What a trait makes of what it is given takes no concept prefix that its
// instantiations would require, which a specialization of the trait can
// change.
template <typename t_CType>
struct TCAddressTraits {
  using CType = t_CType *;
};
template <typename t_CType>
struct TCBox {
  typename TCAddressTraits<t_CType>::CType m_Value = nullptr;
};
TCBox<int> g_Box;
}

namespace NRound188 {
// A pointer to a type that depends on the parameters, which can be a function
// type, takes no concept prefix that its instantiations would require, only
// none that each of them excludes.
template <typename t_CType>
struct TCIdentity {
  using CType = t_CType;
};
using FSignature = void ();
template <typename tf_FSignature>
void fg_Call(typename TCIdentity<tf_FSignature>::CType *_fCallback) {
  _fCallback();
}
template <typename tf_CType>
void fg_Data(typename TCIdentity<tf_CType>::CType *_Value) {}
struct CTraits {
  static void fs_Run();
  using CPointer = int *;
};
template <typename tf_CTraits>
void fg_Run(decltype(tf_CTraits::fs_Run) *_fRun) {}
void fg_Target() {}
void fg_Use() {
  fg_Call<FSignature>(&fg_Target);
  int Value = 0;
  fg_Data<int>(&Value);
  fg_Run<CTraits>(&CTraits::fs_Run);
}
// A parameter of a function type in the type of a variable template, of a
// member of traits, takes none either.
template <typename t_CTraits>
inline NFunction::TCFunction<void (typename t_CTraits::CPointer _Value)> g_fCallback;
void fg_UseCallback() { (void)g_fCallback<CTraits>; }
}

namespace NRound189 {
// Packs of several function types in the type of a member, each matched in
// its function type.
template <typename... tp_CTypes>
struct TCTypeList {};
template <typename... tp_FSignatures>
struct TCFunctions {
  void operator()();
};
struct CTraits {
  using CBool = bool;
  using CPointer = int *;
};
template <typename t_CFlags, typename t_CValues>
struct TCBox;
template <typename... tp_CFlags, typename... tp_CValues>
struct TCBox<TCTypeList<tp_CFlags...>, TCTypeList<tp_CValues...>> {
  TCFunctions<void (typename tp_CFlags::CBool... p_bFlags),
              int (typename tp_CValues::CPointer... p_pValues)> m_fCallbacks;
};
TCBox<TCTypeList<>, TCTypeList<CTraits, CTraits>> g_Box;
// A trait defined only by partial specializations, which make another type
// of what they are given, takes no concept prefix that its instantiations
// would require.
template <typename t_CType>
struct TCVector {};
template <typename t_CType>
struct TCResult;
template <typename t_CType>
struct TCResult<TCVector<t_CType>> {
  using CType = t_CType *;
};
template <typename tf_CType>
void fg_Run(typename TCResult<tf_CType>::CType _Value) {}
void fg_Test() { fg_Run<TCVector<int>>(nullptr); }
}

namespace NRound190 {
// A trait that adds a reference to what it is given forwards it.
template <typename t_CType>
struct TCGetRefType {
  using CType = t_CType const &;
};
template <typename tf_CType>
void fg_Use(typename TCGetRefType<tf_CType>::CType _Value) {}
void fg_Run() { fg_Use<bool>(true); }
// A callback named as a function type in a template, moved into a capture.
template <typename tf_CType>
tf_CType &&fg_Move(tf_CType &_Value) {
  return static_cast<tf_CType &&>(_Value);
}
struct CCallback {
  void operator()() const {}
};
template <typename tf_FCallback>
void fg_Moved(tf_FCallback &&_fCallback) {
  auto fTask = [Callback = fg_Move(_fCallback)] { Callback(); };
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: 'Callback' is a function object and should have the 'f' concept prefix [malterlib-conceptual-prefix]
  fTask();
}
void fg_RunMoved() { fg_Moved(CCallback{}); }
}

namespace NRound191 {
// A pointer to what an alias template makes of a parameter, which can be a
// function, takes no concept prefix that its instantiations would require.
template <typename t_CType>
struct TCRemoveReferenceImpl {
  using CType = t_CType;
};
template <typename t_CType>
struct TCRemoveReferenceImpl<t_CType &> {
  using CType = t_CType;
};
template <typename t_CType>
using TCRemoveReference = typename TCRemoveReferenceImpl<t_CType>::CType;
void fg_Callback() {}
template <typename tf_FAction>
void fg_Run(tf_FAction &&_fAction) {
  TCRemoveReference<tf_FAction> *fCallback = &_fAction;
  fCallback();
}
template <typename tf_CType>
void fg_Data(tf_CType &&_Value) {
  TCRemoveReference<tf_CType> *Value = &_Value;
}
void fg_Use() {
  fg_Run(fg_Callback);
  int Value = 0;
  fg_Data(Value);
}
// The parameters of a requires expression in a function template, of a member
// of traits, take none either.
struct CTraits {
  using CPointer = int *;
};
template <typename tf_CTraits>
void fg_Require() {
  static_assert(requires(typename tf_CTraits::CPointer _Value) { *_Value; });
}
void fg_UseRequire() { fg_Require<CTraits>(); }
}

namespace NRound192 {
// A parameter of a function type in the return type of a function template, of
// a member of traits, takes no concept prefix that its instantiations would
// require.
template <typename t_FSignature>
struct TCFunction {};
struct CTraits {
  using CBool = bool;
};
template <typename tf_CTraits>
TCFunction<void(typename tf_CTraits::CBool o_Ready)> fg_Make() { return {}; }
auto g_fCallback = fg_Make<CTraits>();
}

namespace NRound193 {
// Also of a function type in the return type of a member, beside another
// overload of it.
template <typename t_FSignature>
struct TCFunction {};
struct CTraits {
  using CBool = bool;
};
template <typename t_CTraits>
struct TCListener {
  TCFunction<void(typename t_CTraits::CBool o_Ready)> f_Make(int _Tag);
  TCFunction<void(int &o_Value)> f_Make();
};
TCListener<CTraits> g_Listener;
}

namespace NRound193Member {
// A local of a lambda in the initializer of a member variable template of a
// class template, of a member of traits, takes no concept prefix that the
// instantiations of the class would require.
struct CTraits {
  using CBool = bool;
};
template <typename t_CTraits>
struct TCTraits {
  template <typename t_CValue>
  static constexpr bool mc_bSupports = [] {
    typename t_CTraits::CBool Ready = true;
    using FValue = typename t_CTraits::CBool;
    return Ready;
  }();
};
constexpr bool gc_bSupports = TCTraits<CTraits>::mc_bSupports<int>;
}

namespace NRound194 {
// A type that only forwards a function type parameter is named as it.
template <typename t_CType>
struct TCRemoveReferenceImpl {
  using CType = t_CType;
};
template <typename t_CType>
struct TCRemoveReferenceImpl<t_CType &> {
  using CType = t_CType;
};
template <typename t_CType>
using TCRemoveReference = typename TCRemoveReferenceImpl<t_CType>::CType;
struct CAction {
  void operator()() const {}
};
template <typename tf_FAction>
void fg_Run(tf_FAction &&_fAction) {
  TCRemoveReference<tf_FAction> Action = _fAction;
// CHECK-MESSAGES: :[[@LINE-1]]:33: warning: 'Action' is a function object and should have the 'f' concept prefix [malterlib-conceptual-prefix]
  Action();
}
template <typename tf_CType>
void fg_Keep(tf_CType &&_Value) {
  TCRemoveReference<tf_CType> Value = _Value;
}
void fg_Use() {
  CAction fAction;
  fg_Run(fAction);
  fg_Keep(fAction);
}
}

namespace NRound195 {
// Of what is given several arguments, which it gives can depend on the
// others, so it isn't named by any of them.
template <bool t_bSelect, typename t_CTrue, typename t_CFalse>
struct TCConditionalHelper {
  using CType = t_CTrue;
};
template <typename t_CTrue, typename t_CFalse>
struct TCConditionalHelper<false, t_CTrue, t_CFalse> {
  using CType = t_CFalse;
};
template <bool t_bSelect, typename t_CTrue, typename t_CFalse>
using TCConditional = typename TCConditionalHelper<t_bSelect, t_CTrue, t_CFalse>::CType;
struct CAction {
  void operator()() {}
};
template <typename tf_FCallback>
void fg_Run(TCConditional<true, bool, tf_FCallback> _bReady) {}
void fg_Test() { fg_Run<CAction>(true); }
}

namespace NRound196 {
// The name of a class template in itself is its specialization.
template <typename t_CType>
struct TCVectorIterator {
  void operator++() {}
  void f_Copy(TCVectorIterator const &_Other) const {
// CHECK-MESSAGES: :[[@LINE-1]]:39: warning: '_Other' is an iterator and should have the 'i' concept prefix after its storage prefix [malterlib-conceptual-prefix]
    TCVectorIterator Copy = _Other;
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: 'Copy' is an iterator and should have the 'i' concept prefix [malterlib-conceptual-prefix]
    ++Copy;
  }
};
TCVectorIterator<int> g_iIterator;
void fg_Run() { g_iIterator.f_Copy(g_iIterator); }
}

namespace NRound197 {
// A transformation that makes a pointer of a parameter, `__add_pointer`, takes
// no concept prefix that its instantiations would require, only none that each
// of them excludes.
template <typename t_CType>
using TCAddPointer = __add_pointer(t_CType);
struct CAction {
  void operator()() const {}
};
template <typename tf_FAction, typename tf_CValue>
void fg_Run(TCAddPointer<tf_FAction> _pAction) {
  TCAddPointer<tf_CValue> Value = nullptr;
}
void fg_Test(CAction *_pAction) { fg_Run<CAction, int>(_pAction); }
}

namespace NRound197Crash {
// A local copied from a call through what depends on the parameters, which
// has no name yet.
template <typename t_CMantissa>
struct TCFloat {
  struct CInternalFloat {
    t_CMantissa f_ToInt(bool &o_bOverflow) const;
  };
  TCFloat f_Pow() const {
    CInternalFloat Power;
    bool bOverflow = false;
    auto Integer = Power.f_ToInt(bOverflow);
    (void)Integer;
    return *this;
  }
};
TCFloat<long> g_Float;
void fg_UseFloat() { g_Float.f_Pow(); }
// A friend function template of a class template, also of a specialization
// only declared.
template <typename t_CActor>
struct TCActorHandle;
namespace NPrivate {
template <typename tf_CActor>
tf_CActor *fg_GetInternalActor(TCActorHandle<tf_CActor> const &_Actor);
}
template <typename t_CActor>
struct TCActorHandle {
  template <typename tf_CActor>
  friend tf_CActor *NPrivate::fg_GetInternalActor(TCActorHandle<tf_CActor> const &_Actor);
  t_CActor *m_pActor;
};
struct CActorA;
struct CActorB {};
TCActorHandle<CActorA> *g_pActorA;
TCActorHandle<CActorB> g_ActorB;
namespace NPrivate {
template <typename tf_CActor>
tf_CActor *fg_GetInternalActor(TCActorHandle<tf_CActor> const &_Actor) { return _Actor.m_pActor; }
}
CActorB *fg_UseActor() { return NPrivate::fg_GetInternalActor(g_ActorB); }
}

namespace NRound198 {
// An atomic pointer to a member of what depends on the parameters takes no
// concept prefix that the instantiations would require.
template <typename t_CType>
struct TCAtomic {
  t_CType m_Value{};
};
template <typename t_CType>
struct TCTraits {
  using CType = t_CType;
};
template <typename t_CTraits>
struct TCStorage {
  TCAtomic<typename t_CTraits::CType *> m_fCallback;
  TCAtomic<typename t_CTraits::CType *> m_Value;
};
TCStorage<TCTraits<void()>> g_Storage;
template <typename t_CTraits>
struct TCPointerStorage {
  TCAtomic<typename t_CTraits::CType *> m_Value;
};
TCPointerStorage<TCTraits<int>> g_PointerStorage;
}

namespace NRound199 {
// A parameter of a function type in a base of a class template, of a member of
// traits, takes no concept prefix that its instantiations would require.
template <typename t_FSignature>
struct TCFunction {};
struct CTraits {
  using CBool = bool;
};
template <typename t_CTraits>
struct TCHandler : TCFunction<void(typename t_CTraits::CBool _Ready, typename t_CTraits::CBool o_bDone)> {};
TCHandler<CTraits> g_fHandler;
}

namespace NRound199Copy {
// A copy made with `fg_TempCopy` is what it copies.
struct CAction {
  void operator()() const {}
};
template <typename tf_CType>
constexpr tf_CType fg_TempCopy(tf_CType const &_Value) { return _Value; }
template <typename tf_FAction>
void fg_Run(tf_FAction &&_fAction) {
  auto Copy = fg_TempCopy(_fAction);
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: 'Copy' is a function object and should have the 'f' concept prefix [malterlib-conceptual-prefix]
  Copy();
}
void fg_Use() {
  CAction fAction;
  fg_Run(fAction);
}
}

namespace NRound200 {
// The argument that the member of a template given several gives, which is
// named as a function type, or not.
template <typename t_FSignature>
struct TCFunction {
  void operator()() const {}
};
template <typename t_CKey, typename t_CValue>
struct TCMap {
  using CValue = t_CValue;
};
template <typename tf_FCallback>
void fg_Run(typename TCMap<int, tf_FCallback>::CValue const &_Callback) {
// CHECK-MESSAGES: :[[@LINE-1]]:62: warning: '_Callback' is a function object and should have the 'f' concept prefix after its storage prefix [malterlib-conceptual-prefix]
  _Callback();
}
template <typename tf_CValue>
void fg_Store(typename TCMap<tf_CValue, int>::CValue const &_Value) {}
template <typename tf_CValue>
void fg_Keep(typename TCMap<int, tf_CValue>::CValue const &_Value) {}
void fg_Test() {
  TCFunction<void()> fCallback;
  fg_Run<TCFunction<void()>>(fCallback);
  fg_Store<TCFunction<void()>>(1);
  fg_Keep<TCFunction<void()>>(fCallback);
}
}

namespace NRound201 {
// A member of an explicit instantiation is checked in the template.
template <typename t_CType>
struct TCStorage {
  t_CType m_Value;
};
template struct TCStorage<bool>;
// A trait an explicit specialization of which defines its member otherwise
// doesn't only forward what it is given: what it gives takes no concept prefix
// that the instantiations would require, also of the template itself.
template <typename t_CType>
struct TCTraits {
  using CType = t_CType;
};
template <>
struct TCTraits<int> {
  using CType = bool;
};
template <typename t_CType>
struct TCTraitsStorage {
  typename TCTraits<t_CType>::CType m_Value;
};
TCTraitsStorage<int> g_TraitsStorage;
template <typename t_CType>
struct TCPrimaryStorage {
  typename TCTraits<t_CType>::CType m_Value;
};
TCPrimaryStorage<bool *> g_PrimaryStorage;
// A non-type parameter whose type is deduced takes no concept prefix that its
// arguments would require.
void fg_Destroy() {}
template <auto t_Deleter>
struct TCDeleter {
  void f_Run() { t_Deleter(); }
};
TCDeleter<&fg_Destroy> g_Deleter;
template <auto t_Value>
struct TCValue {};
TCValue<1> g_One;
TCValue<2> g_Two;
}

namespace NRound202 {
// A parameter of a function type in an alias template takes no concept prefix
// that the uses of the alias template would require.
struct CTraits {
  using CBool = bool;
};
template <typename t_FSignature>
struct TCFunction {};
template <typename t_CTraits>
using TCCallback = TCFunction<void(typename t_CTraits::CBool _Ready)>;
TCCallback<CTraits> g_fCallback;
template <typename t_CTraits>
using TCUnused = TCFunction<void(typename t_CTraits::CBool _Ready)>;
}

namespace NRound203 {
// The parameters of an alias template's function type before and after a
// pack, which the uses match by their place from the start and the end, take
// no concept prefix that the uses would require.
struct CTraits {
  using CPointer = int *;
};
template <typename t_CTraits, typename... tp_CParams>
using TFCallback = void(typename t_CTraits::CPointer _Context,
                        tp_CParams... p_Params,
                        typename t_CTraits::CPointer _Last);
TFCallback<CTraits, int, bool> *g_fCallback;
// Also those of a member alias template of a class template.
template <typename t_CTraits>
struct TCBox {
  template <typename t_CTag>
  using TFTagged = void(typename t_CTraits::CPointer _Value, t_CTag _Tag);
};
TCBox<CTraits>::TFTagged<int> *g_fTagged;
// A parameter of a class template repeated for a member defined out of it, of
// a deduced type, takes none either.
void fg_Delete(int *_pValue) {}
template <auto t_fDelete>
struct TCDeleter {
  void f_Run();
};
template <auto t_Delete>
void TCDeleter<t_Delete>::f_Run() {}
void fg_Test() { TCDeleter<&fg_Delete>{}.f_Run(); }
}

namespace NRound204 {
// A pack in an alias template's function type takes no concept prefix that the
// uses would require, also in each function type a pack of signatures expands
// to.
struct CTraits {
  using CBool = bool;
};
struct CReferenceTraits {
  using CBool = bool &;
};
template <typename... tp_CTraits>
using TFFlags = void(typename tp_CTraits::CBool... p_Flags);
TFFlags<CTraits, CTraits> *g_fFlags;
template <typename... tp_FSignatures>
struct TCFunction {};
template <typename... tp_CTraits>
using TCCallbacks = TCFunction<void(typename tp_CTraits::CBool _Ready)...>;
TCCallbacks<CTraits, CReferenceTraits> g_fCallbacks;
// What forwards one of several type arguments that are each named as a
// function type.
template <bool t_bReference, typename t_CFirst, typename t_CSecond>
struct TCConditionalHelper {
  using CType = t_CFirst;
};
template <typename t_CFirst, typename t_CSecond>
struct TCConditionalHelper<false, t_CFirst, t_CSecond> {
  using CType = t_CSecond;
};
template <bool t_bReference, typename t_CFirst, typename t_CSecond>
using TCConditional =
    typename TCConditionalHelper<t_bReference, t_CFirst, t_CSecond>::CType;
template <typename t_FCallback, bool t_bReference>
struct TCStorage {
  TCConditional<t_bReference, t_FCallback const &, t_FCallback> m_Callback;
// CHECK-MESSAGES: :[[@LINE-1]]:65: warning: 'm_Callback' is a function object and should have the 'f' concept prefix after its storage prefix [malterlib-conceptual-prefix]
  TCConditional<t_bReference, bool, t_FCallback> m_Mixed;
};
struct CAction {
  void operator()() {}
};
TCStorage<CAction, false> g_Copy;
}

namespace NRound206 {
// What a dependent base is given at the place of the parameter its member
// is, `bool`, takes no concept prefix that the instantiations would require,
// which a specialization for an argument can change.
template <class t_CType, t_CType t_Value>
struct TCCompileTimeConstant {
  using CType = t_CType;
};
template <class t_CType>
struct TCTraits : TCCompileTimeConstant<bool, (sizeof(t_CType) > 0)> {};
template <class t_CType>
struct TCForward : TCCompileTimeConstant<t_CType, t_CType{}> {};
template <class tf_CType>
void fg_Run() {
  typename TCTraits<tf_CType>::CType Ready = true;
  typename TCForward<tf_CType>::CType Value = {};
  (void)Ready;
  (void)Value;
}
void fg_Use() { fg_Run<int>(); }
}

namespace NRound207 {
// A copy of a member named as a function type, also one instantiated as a
// reference.
struct CAction {
  void operator()() const {}
};
template <class t_FAction>
struct TCWrapper {
  t_FAction m_fAction;
  void f_Run() {
    auto Copy = m_fAction;
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: 'Copy' is a function object and should have the 'f' concept prefix [malterlib-conceptual-prefix]
    Copy();
  }
};
void fg_Run() {
  CAction fAction;
  TCWrapper<CAction &> Wrapper{fAction};
  Wrapper.f_Run();
}
}

namespace NRound208
{
	// A member template of what depends on the parameters takes no concept
	// prefix that each instantiation would require.
	struct CTraits
	{
		template <typename t_CValue>
		struct TCResult
		{
			using CType = bool;
		};
		template <typename t_CValue>
		struct TCIdentity
		{
			using CType = t_CValue;
		};
	};
	template <typename t_CTraits, typename t_CValue>
	struct TCBox
	{
		typename t_CTraits::template TCResult<t_CValue>::CType m_Ready;
		typename t_CTraits::template TCIdentity<t_CValue>::CType m_Value;
	};
	TCBox<CTraits, int> g_Box;
	TCBox<CTraits, bool> g_BoolBox;
}

namespace NRound209
{
	// A member made of a parameter, a reference to it, takes no concept prefix
	// that the argument at its place would require, which a specialization for
	// an argument can change.
	template <typename t_CKey, typename t_CValue>
	struct TCMap
	{
		using CValueRef = t_CValue const &;
	};
	template <typename tf_CKey>
	void fg_Run(typename TCMap<tf_CKey, bool>::CValueRef _Ready)
	{
	}
	template <typename tf_CKey, typename tf_CValue>
	void fg_RunGeneric(typename TCMap<tf_CKey, tf_CValue>::CValueRef _Value)
	{
	}
	void fg_Use()
	{
		fg_Run<int>(true);
		fg_RunGeneric<int, bool>(true);
		fg_RunGeneric<int, int>(1);
	}
}

namespace NRound210
{
	// A member of what a parameter is given that gives an argument of that as
	// it is is generic, whatever it is given.
	template <typename t_CKey, typename t_CValue>
	struct TCMap
	{
		using CValue = t_CValue;
	};
	template <typename t_CMap>
	struct TCEntry
	{
		typename t_CMap::CValue m_Value;
	};
	TCEntry<TCMap<int, bool>> g_Entry;

	// The parameters of a signature of a partial specialization of a variable
	// template, like those of the primary template, take no concept prefix that
	// its instantiations would require.
	template <typename t_FSignature>
	struct TCFunction
	{
	};
	struct CTraits
	{
		using CValue = int;
		using CBool = bool;
	};
	template <typename t_CTraits>
	TCFunction<void (typename t_CTraits::CValue _Value)> g_fCallback;
	template <typename t_CTraits>
	TCFunction<void (typename t_CTraits::CBool _Flag)> g_fCallback<t_CTraits *>;
	void fg_Use()
	{
		(void)g_fCallback<CTraits *>;
	}
}

namespace NRound211
{
	// A parameter or local of a member of what a parameter is given that gives
	// an argument of that as it is is generic, whatever it is given.
	template <typename t_CKey, typename t_CValue>
	struct TCMap
	{
		using CValue = t_CValue;
	};
	template <typename tf_CMap>
	void fg_Read(typename tf_CMap::CValue _Value)
	{
		typename tf_CMap::CValue Value{};
		(void)Value;
	}
	void fg_Test()
	{
		fg_Read<TCMap<int, bool>>(false);
	}

	// A wrapper whose concept is that of what it holds, of a member of what
	// depends on the parameters, takes no concept prefix that the
	// instantiations would require.
	template <typename t_CType>
	struct TCAtomic
	{
		t_CType m_Value{};
	};
	struct CTraits
	{
		using CBool = bool;
		using CPointer = int *;
	};
	template <typename t_CTraits>
	struct TCState
	{
		TCAtomic<typename t_CTraits::CBool> m_Ready{};
		TCAtomic<typename t_CTraits::CPointer> m_Head{};
		TCAtomic<t_CTraits> m_Generic{};
	};
	TCState<CTraits> g_State;
}

namespace NRound212
{
	// A local of a member of what a parameter is given through an adapter
	// that forwards it is generic, whatever it is given.
	template <typename t_CType>
	struct TCVector
	{
		using CValue = t_CType;
	};
	template <typename t_CContainer>
	struct TCAdapter
	{
		using CValue = typename t_CContainer::CValue;
	};
	template <typename tf_CContainer>
	void fg_Read()
	{
		typename TCAdapter<tf_CContainer>::CValue Value{};
		(void)Value;
	}
	void fg_Use()
	{
		fg_Read<TCVector<bool>>();
	}

	// A variable template that holds a member of what depends on the
	// parameters takes no concept prefix that the instantiations would require.
	template <typename t_CType>
	struct TCAtomic
	{
		t_CType m_Value{};
	};
	struct CTraits
	{
		using CBool = bool;
	};
	template <typename t_CTraits>
	inline TCAtomic<typename t_CTraits::CBool> g_Ready;
	void fg_UseReady()
	{
		(void)g_Ready<CTraits>;
	}
}

namespace NRound213
{
	// Each of a pack of init captures is what it is deduced as.
	template <typename... tfp_CTypes>
	void fg_Dispatch(tfp_CTypes *...p_pValues)
	{
		auto fTask = [...p_Values = p_pValues]
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: 'p_Values' is a pointer and should have the 'p' concept prefix after its storage prefix [malterlib-conceptual-prefix]
			{
				((void)p_Values, ...);
			}
		;
		fTask();
	}
	void fg_Test()
	{
		int Value = 0;
		fg_Dispatch(&Value);
	}
}

namespace NRound214
{
	// A member that is a parameter without its reference is what is given at
	// its place.
	template <typename t_CType>
	using TCRemoveReference = __remove_reference_t(t_CType);
	template <typename t_CKey, typename t_CValue>
	struct TCMap
	{
		using CValueNoRef = TCRemoveReference<t_CValue>;
	};
	template <typename t_FSignature>
	struct TCFunction
	{
		void operator()() const
		{
		}
	};
	template <typename tf_FCallback>
	void fg_Run(tf_FCallback &&_fCallback)
	{
		typename TCMap<int, tf_FCallback>::CValueNoRef Callback = _fCallback;
// CHECK-MESSAGES: :[[@LINE-1]]:50: warning: 'Callback' is a function object and should have the 'f' concept prefix [malterlib-conceptual-prefix]
		Callback();
	}
	void fg_Use()
	{
		TCFunction<void ()> fCallback;
		fg_Run(fCallback);
	}
}

namespace NRound215
{
	template <typename t_CType>
	using TCRemoveReference = __remove_reference_t(t_CType);
	// A member of what a parameter is given without its reference is generic,
	// whatever it is given.
	template <typename t_CKey, typename t_CValue>
	struct TCMap
	{
		using CValue = t_CValue;
	};
	template <typename tf_CMap>
	void fg_Read()
	{
		TCRemoveReference<typename tf_CMap::CValue> Value{};
		(void)Value;
	}
	void fg_Use()
	{
		fg_Read<TCMap<int, bool &>>();
	}

	// What a partial specialization takes out of a signature is what it is,
	// not the signature.
	template <typename t_FSignature>
	struct TCSignature;
	template <typename t_CReturn, typename... tp_CParams>
	struct TCSignature<t_CReturn (tp_CParams... p_Params)>
	{
		using CReturn = t_CReturn;
	};
	template <typename tf_FSignature>
	void fg_Run()
	{
		typename TCSignature<tf_FSignature>::CReturn bResult{};
		(void)bResult;
	}
	void fg_UseSignature()
	{
		fg_Run<bool ()>();
	}
}

namespace NRound216
{
	// A member of a template template parameter takes no concept prefix that
	// each argument would require.
	template <typename t_CType>
	struct TCAddPointer
	{
		using CType = t_CType *;
	};
	template <template <typename> class tf_TCTransform, typename tf_CType>
	void fg_Test()
	{
		typename tf_TCTransform<tf_CType>::CType Value = nullptr;
		(void)Value;
	}
	void fg_Run()
	{
		fg_Test<TCAddPointer, int>();
	}

	// What a base of a base is given, as what the one between is given.
	template <typename t_CType>
	struct TCBase
	{
		using CType = t_CType;
	};
	template <typename t_CUnused, typename t_CType>
	struct TCIntermediate : TCBase<t_CType>
	{
	};
	template <typename t_CType>
	struct TCTraits : TCIntermediate<t_CType, bool>
	{
	};
	template <typename tf_FAction>
	void fg_Inherited()
	{
		typename TCTraits<tf_FAction>::CType bReady = false;
		(void)bReady;
	}
	void fg_RunInherited()
	{
		fg_Inherited<void ()>();
	}
}

namespace NRound217
{
	// What the partial specialization the written arguments select gives takes
	// no concept prefix that the instantiations would require, also where it
	// gives a type that is written, which a specialization can change.
	template <bool t_bFirst, typename t_CFirst, typename t_CSecond>
	struct TCConditionalHelper
	{
		using CType = t_CFirst;
	};
	template <typename t_CFirst, typename t_CSecond>
	struct TCConditionalHelper<false, t_CFirst, t_CSecond>
	{
		using CType = t_CSecond;
	};
	template <bool t_bFirst, typename t_CFirst, typename t_CSecond>
	using TCConditional = typename TCConditionalHelper<t_bFirst, t_CFirst, t_CSecond>::CType;
	template <typename tf_CType>
	void fg_Selected()
	{
		TCConditional<false, int, tf_CType> Value{};
		TCConditional<true, tf_CType, int> Primary{};
		TCConditional<true, bool, tf_CType> Fixed{};
		(void)Value;
		(void)Primary;
		(void)Fixed;
	}
	void fg_RunSelected()
	{
		fg_Selected<bool>();
	}

	// What an alias of a dependent base forwards, at its place in what is
	// written.
	template <typename t_CKey, typename t_CValue>
	struct TCMap
	{
		using CValue = t_CValue;
	};
	template <typename t_CKey, typename t_CValue>
	struct TCMapWithPool : TCMap<t_CKey, t_CValue>
	{
	};
	template <typename tf_FCallback>
	void fg_Call(typename TCMapWithPool<int, tf_FCallback>::CValue &_Callback)
// CHECK-MESSAGES: :[[@LINE-1]]:66: warning: '_Callback' is a function object and should have the 'f' concept prefix after its storage prefix [malterlib-conceptual-prefix]
	{
	}
	template <typename tf_CKey>
	void fg_Key(typename TCMapWithPool<tf_CKey, int>::CValue &_Value)
	{
	}
	void fg_Target()
	{
	}
	void fg_RunInherited()
	{
		fg_Call<void ()>(fg_Target);
		int Value = 0;
		fg_Key<void ()>(Value);
	}
}

namespace NRound218
{
	// What the partial specialization the written arguments select gives at
	// the place of its parameters takes no concept prefix that the
	// instantiations would require, also where `bool` is written there.
	template <bool t_bSelect, typename t_CFirst, typename t_CSecond>
	struct TCConditionalHelper
	{
		using CType = t_CFirst;
	};
	template <typename t_CFirst, typename t_CSecond>
	struct TCConditionalHelper<false, t_CFirst, t_CSecond>
	{
		using CType = t_CSecond;
	};
	template <typename tf_CType>
	void fg_Run()
	{
		typename TCConditionalHelper<false, tf_CType, bool>::CType Ready = true;
		typename TCConditionalHelper<false, bool, tf_CType>::CType Value{};
		typename TCConditionalHelper<true, tf_CType, bool>::CType Other{};
		(void)Ready;
		(void)Value;
		(void)Other;
	}
	void fg_Use()
	{
		fg_Run<int>();
	}
}

namespace NRound219
{
	// A class of a template's own takes no concept prefix that the
	// instantiations would require, which a specialization can change.
	template <typename t_CData>
	struct TCDLinkListAggregate
	{
		struct CIterator
		{
			CIterator &operator ++ ()
			{
				return *this;
			}
			t_CData &operator * ();
		};
	};
	template <typename tf_CData>
	void fg_Run()
	{
		typename TCDLinkListAggregate<tf_CData>::CIterator Iterator;
		++Iterator;
	}
	void fg_Use()
	{
		fg_Run<int>();
	}

	// A reference to a value a container forwards is as generic as it.
	template <typename t_CKey, typename t_CValue>
	struct TCMap
	{
		using CValue = t_CValue;
	};
	template <typename t_CMap>
	struct TCValueReference
	{
		typename t_CMap::CValue &m_Value;
	};
	bool g_bReady;
	TCValueReference<TCMap<int, bool>> g_Reference{g_bReady};
}

namespace NRound220
{
	// A member of a base that is a parameter, a member of what is given, takes
	// no concept prefix that the instantiations would require.
	struct CTraits
	{
		using CType = bool;
	};
	template <typename t_CTraits>
	struct TCAdapter : t_CTraits
	{
	};
	template <typename tf_CTraits>
	void fg_Test()
	{
		typename TCAdapter<tf_CTraits>::CType Ready = false;
		(void)Ready;
	}
	void fg_Run()
	{
		fg_Test<CTraits>();
	}
}

namespace NRound221Alias
{
	// The parameters of a function type an alias gives through another
	// alias take no concept prefix that the uses would require.
	struct CTraits
	{
		using CType = bool;
	};
	template <typename t_CTraits>
	using TFCallback = void (typename t_CTraits::CType o_Value);
	template <typename t_CTraits>
	using TFAlias = TFCallback<t_CTraits>;
	TFAlias<CTraits> *g_fCallback;
}

namespace NRound221
{
	// The argument a conditional selects is what it is named as.
	template <bool t_bFirst, typename t_CFirst, typename t_CSecond>
	struct TCConditionalHelper
	{
		using CType = t_CFirst;
	};
	template <typename t_CFirst, typename t_CSecond>
	struct TCConditionalHelper<false, t_CFirst, t_CSecond>
	{
		using CType = t_CSecond;
	};
	template <bool t_bFirst, typename t_CFirst, typename t_CSecond>
	using TCConditional = typename TCConditionalHelper<t_bFirst, t_CFirst, t_CSecond>::CType;
	struct CAction
	{
		void operator () ()
		{
		}
	};
	template <typename t_FCallback>
	struct TCBox
	{
		TCConditional<true, t_FCallback, int> m_Callback;
// CHECK-MESSAGES: :[[@LINE-1]]:41: warning: 'm_Callback' is a function object and should have the 'f' concept prefix after its storage prefix [malterlib-conceptual-prefix]
		TCConditional<false, t_FCallback, int> m_Value;
	};
	TCBox<CAction> g_Box;
}

namespace NRound222
{
	// What a base that only partial specializations define gives takes no
	// concept prefix that the instantiations would require.
	template <typename t_CType>
	struct TCBase;
	template <typename t_CType>
	struct TCBase<t_CType *>
	{
		using CType = bool;
	};
	template <typename t_CType>
	struct TCTraits : TCBase<t_CType *>
	{
	};
	template <typename tf_CType>
	void fg_Run(typename TCTraits<tf_CType>::CType _Ready)
	{
	}
	void fg_Use()
	{
		fg_Run<int>(true);
	}
}

namespace NRound223Default
{
	// The parameters of a function type in the default argument of a template
	// parameter take no concept prefix that the specializations would require.
	template <typename t_FSignature>
	struct TCFunction
	{
	};
	struct CTraits
	{
		using CBool = bool;
	};
	template <typename tf_CTraits, typename tf_FCallback = TCFunction<void (typename tf_CTraits::CBool _Ready, typename tf_CTraits::CBool o_bResult)>>
	void fg_Run()
	{
	}
	void fg_Use()
	{
		fg_Run<CTraits>();
	}
}

namespace NRound224
{
	// A parameter selected from a type list is generic like it, and what the
	// list gives where `bool` is written takes no concept prefix that the
	// instantiations would require.
	template <typename... tp_CTypes>
	struct TCTypeList
	{
	};
	template <typename t_CList>
	struct TCFirst;
	template <typename t_CHead, typename... tp_CTail>
	struct TCFirst<TCTypeList<t_CHead, tp_CTail...>>
	{
		using CType = t_CHead;
	};
	template <typename t_CType>
	struct TCStorage
	{
		typename TCFirst<TCTypeList<t_CType>>::CType m_Value;
		typename TCFirst<TCTypeList<bool, t_CType>>::CType m_Fixed;
	};
	TCStorage<bool> g_Storage;
}

namespace NRound226Default
{
	// The parameters of a default callback signature take no concept prefix
	// that the instantiations would require, whether the call or class uses the
	// default, also through a member of a specialization, or is given another.
	template <typename t_FSignature>
	struct TCFunction
	{
	};
	struct CTraits
	{
		using CType = bool;
	};
	struct CPointerTraits
	{
		using CType = int *;
	};
	template <typename t_CType>
	struct TCWrap
	{
		using CType = typename t_CType::CType;
	};
	template <typename tf_CTraits, typename tf_FCallback = TCFunction<void (typename tf_CTraits::CType _Ready)>>
	void fg_Run(tf_FCallback _fCallback = {})
	{
	}
	template <typename tf_CTraits, typename tf_FCallback = TCFunction<void (typename TCWrap<tf_CTraits>::CType _Wrapped)>>
	void fg_RunWrapped(tf_FCallback _fCallback = {})
	{
	}
	template <typename tf_CTraits, typename tf_FCallback = TCFunction<void (typename tf_CTraits::CType _Given)>>
	void fg_RunGiven(tf_FCallback _fCallback)
	{
	}
	template <typename t_CTraits, typename t_FCallback = TCFunction<void (typename t_CTraits::CType _pValue)>>
	struct TCBox
	{
	};
	template <typename t_CTraits, typename t_FCallback = TCFunction<void (typename t_CTraits::CType _Boxed)>>
	struct TCDefaultBox
	{
	};
	TCBox<CPointerTraits, TCFunction<void (bool _bReady)>> g_Box;
	TCDefaultBox<CTraits> g_DefaultBox;
	void fg_Use()
	{
		fg_Run<CTraits>();
		fg_RunWrapped<CTraits>();
		fg_RunGiven<CPointerTraits>(TCFunction<void (bool _bReady)>{});
	}
}

namespace NRound227MemberDefault
{
	// The parameters of the default of a member template of a class template,
	// of the parameters of the class, take no concept prefix that the
	// specializations of the member template would require.
	template <typename t_FSignature>
	struct TCFunction
	{
	};
	struct CTraits
	{
		using CBool = bool;
	};
	template <typename t_CTraits>
	struct TCBox
	{
		template <typename tf_FCallback = TCFunction<void (typename t_CTraits::CBool _Ready, typename t_CTraits::CBool o_bResult)>>
		void f_Run()
		{
		}
		template <typename tf_FCallback = TCFunction<void (typename t_CTraits::CBool _Given, typename t_CTraits::CBool o_bGiven)>>
		void f_RunGiven()
		{
		}
	};
	void fg_Test()
	{
		TCBox<CTraits>{}.f_Run();
		TCBox<CTraits>{}.f_RunGiven<TCFunction<void (int _Value, int &o_Value)>>();
	}
}

namespace NRound230
{
	// A local alias of a member of what a parameter is given is generic, like
	// the member.
	template <typename t_CKey, typename t_CValue>
	struct TCMap
	{
		using CValue = t_CValue;
	};
	template <typename tf_CMap>
	void fg_Test(typename tf_CMap::CValue _Value)
	{
		using CValue = typename tf_CMap::CValue;
		CValue Value = _Value;
		(void)Value;
	}
	void fg_Run()
	{
		fg_Test<TCMap<int, bool>>(true);
	}
}

namespace NRound231
{
	// A member of a base that a specialization defines otherwise takes no
	// concept prefix that the instantiations would require, and one of a class
	// nested in a template that gives what the template is given is generic.
	template <typename t_CType>
	struct TCDecayBase
	{
		using CType = t_CType;
	};
	template <typename t_CType>
	struct TCDecayBase<t_CType[]>
	{
		using CType = t_CType *;
	};
	template <typename t_CType>
	struct TCDecay : TCDecayBase<t_CType>
	{
	};
	template <typename tf_CType>
	void fg_Run(typename TCDecay<tf_CType>::CType _Value)
	{
	}
	void fg_Use()
	{
		fg_Run<int[]>(nullptr);
	}

	template <typename t_CValue>
	struct TCMap
	{
		struct CNode
		{
			using CValue = t_CValue;
		};
	};
	template <typename t_CMap>
	struct TCStorage
	{
		using CValue = typename t_CMap::CNode::CValue;
		CValue m_Value;
	};
	TCStorage<TCMap<bool>> g_Storage;
}

namespace NRound232
{
	// What a partial specialization with constraints may give, and a parameter
	// of a callback signature in a template argument of a call, take no concept
	// prefix that the instantiations would require.
	template <typename t_CType>
	struct TCTraits
	{
		using CType = bool;
	};
	template <typename t_CType>
		requires (sizeof(t_CType) > sizeof(int))
	struct TCTraits<t_CType>
	{
		using CType = t_CType;
	};
	template <typename tf_CType>
	void fg_Run(typename TCTraits<tf_CType>::CType _Ready)
	{
	}
	void fg_Test()
	{
		fg_Run<int>(true);
	}

	template <typename t_FSignature>
	struct TCFunction
	{
	};
	struct CBoolTraits
	{
		using CBool = bool;
	};
	template <typename tf_CType>
	tf_CType fg_Construct()
	{
		return {};
	}
	template <typename tf_CTraits>
	auto fg_Make()
	{
		return fg_Construct<TCFunction<void (typename tf_CTraits::CBool _Ready)>>();
	}
	void fg_UseMake()
	{
		auto fCallback = fg_Make<CBoolTraits>();
		(void)fCallback;
	}
}

namespace NRound233
{
	// A default callback signature of a member that gives what it is given is
	// generic, and one inherited through one of several bases, and a callback
	// in a generic lambda, take no concept prefix that the instantiations would
	// require.
	template <typename t_FSignature>
	struct TCFunction
	{
	};
	template <typename t_CKey, typename t_CValue>
	struct TCMap
	{
		using CValue = t_CValue;
	};
	template <typename tf_CMap, typename tf_FCallback = TCFunction<void (typename tf_CMap::CValue _Value)>>
	void fg_Visit(tf_FCallback _fCallback = {})
	{
	}
	struct CBoolTraits
	{
		using CBool = bool;
	};
	struct CPointerTraits
	{
		using CPointer = int *;
	};
	struct CTraits : CBoolTraits, CPointerTraits
	{
	};
	template <typename tf_CTraits, typename tf_FCallback = TCFunction<void (typename tf_CTraits::CBool _Ready)>>
	void fg_Run(tf_FCallback _fCallback = {})
	{
	}
	void fg_Use()
	{
		fg_Visit<TCMap<int, bool>>();
		fg_Run<CTraits>();
		auto fDispatch = []<typename tf_CTraits>()
			{
				auto fCallback = [](typename tf_CTraits::CPointer _Value)
					{
					}
				;
				fCallback(nullptr);
			}
		;
		fDispatch.template operator()<CTraits>();
	}
}

namespace NRound234
{
	template <typename t_CValue>
	struct TCMap
	{
		using CValueRef = t_CValue const &;
		typename TCMap::CValueRef m_Value;
	};
	// A reference to what the map is given is generic.
	template <typename tf_CMap>
	void fg_Read(typename tf_CMap::CValueRef _Value)
	{
	}
	template <typename t_CMap>
	struct TCHolder
	{
		typename t_CMap::CValueRef m_Value;
	};
	template <typename t_FSignature>
	struct TCFunction
	{
	};
	struct CTraits
	{
		using CBool = bool;
	};
	// The parameters of the type of a temporary take no concept prefix that the
	// instantiations would require.
	template <typename tf_CTraits>
	void fg_Temporary()
	{
		auto fCallback = TCFunction<void (typename tf_CTraits::CBool _Ready)>{};
		auto fParens = TCFunction<void (typename tf_CTraits::CBool _Done)>();
	}
	// Also around a pack it expands.
	template <typename tf_CTraits, typename ...tfp_CArgs>
	void fg_Pack(void *_pContext)
	{
		(void)reinterpret_cast<TCFunction<void (typename tf_CTraits::CBool _Ready, tfp_CArgs ...p_Args, typename tf_CTraits::CBool _Last)> *>(_pContext);
	}
	void fg_Use()
	{
		fg_Read<TCMap<bool>>(true);
		TCHolder<TCMap<bool>> Holder{true};
		(void)Holder;
		TCMap<bool> Map{true};
		(void)Map;
		fg_Temporary<CTraits>();
		fg_Pack<CTraits, int, int>(nullptr);
	}
}

namespace NRound235
{
	template <typename ...tp_CTypes>
	struct TCTypeList
	{
	};
	struct CPointerTraits
	{
		using CType = int *;
	};
	struct CBoolTraits
	{
		using CType = bool;
	};
	template <typename t_CLeft, typename t_CRight>
	struct TCJoin;
	// What is between two packs has no place that can be told.
	template <typename ...tp_CLeft, typename ...tp_CRight>
	struct TCJoin<TCTypeList<tp_CLeft...>, TCTypeList<tp_CRight...>>
	{
		using FSignature = void (typename tp_CLeft::CType ...p_bLeft, typename tp_CRight::CType ...p_pRight);
	};
	TCJoin<TCTypeList<>, TCTypeList<CPointerTraits, CPointerTraits>>::FSignature *g_fRight;
	template <typename t_FSignature>
	struct TCFunction
	{
	};
	struct CTraits
	{
		using CBool = bool;
	};
	// The parameters of a callback signature take no concept prefix that the
	// instantiations would require, in a member template of a class template.
	template <typename t_CTraits>
	struct TCBox
	{
		template <typename tf_CType>
		void f_Run()
		{
			(void)TCFunction<void (typename t_CTraits::CBool _Ready)>{};
		}
	};
	// Also in a generic lambda in a function template.
	template <typename tf_CTraits>
	void fg_Generic()
	{
		auto fRun = [](auto _Value)
			{
				(void)TCFunction<void (typename tf_CTraits::CBool _Ready)>{};
			}
		;
		fRun(1);
	}
	// Also in what a constructor initializes.
	template <typename t_CTraits>
	struct TCHolder
	{
		TCFunction<void (bool _bReady)> m_fCallback;
		TCHolder()
			: m_fCallback(TCFunction<void (typename t_CTraits::CBool _Ready)>{})
		{
		}
	};
	// Also a pack expanded to many.
	template <typename tf_CTraits, typename ...tfp_CArgs>
	void fg_Pack(void *_pContext)
	{
		(void)reinterpret_cast<TCFunction<void (typename tfp_CArgs::CType ...p_Args)> *>(_pContext);
	}
	void fg_Use()
	{
		TCBox<CTraits>{}.f_Run<int>();
		fg_Generic<CTraits>();
		TCHolder<CTraits> Holder;
		(void)Holder;
		fg_Pack<CTraits, CBoolTraits, CBoolTraits>(nullptr);
	}
}

namespace NRound237
{
	struct CAction
	{
		void operator () () const
		{
		}
	};
	using FAction = CAction;
	template <typename tf_CType>
	tf_CType &&fg_Move(tf_CType &_Value)
	{
		return static_cast<tf_CType &&>(_Value);
	}
	// A callback moved into a local, which loses the name of its type.
	void fg_Run(FAction _fAction)
	{
		auto Copy = fg_Move(_fAction);
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: 'Copy' is a function object and should have the 'f' concept prefix [malterlib-conceptual-prefix]
		Copy();
		auto fTask = [Callback = fg_Move(_fAction)]
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: 'Callback' is a function object and should have the 'f' concept prefix [malterlib-conceptual-prefix]
			{
				Callback();
			}
		;
		fTask();
	}
}

namespace NRound238
{
	template <typename t_CType>
	struct TCIdentity
	{
		using CType = t_CType;
	};
	// A reference to what a parameter is given is generic.
	template <typename tf_CType>
	void fg_Run(typename TCIdentity<tf_CType const &>::CType _Value)
	{
	}
	void fg_Test()
	{
		fg_Run<bool>(true);
	}
}

namespace NRound239
{
	template <typename t_CValue>
	struct TCMap
	{
		using CValue = t_CValue;
	};
	// A type imported from what a parameter is given is generic.
	template <typename t_CMap>
	struct TCAdapter : t_CMap
	{
		using typename t_CMap::CValue;
		CValue m_Value;
		void f_Set(CValue _Value)
		{
			m_Value = _Value;
		}
	};
	TCAdapter<TCMap<bool>> g_Adapter;
	void fg_Test()
	{
		g_Adapter.f_Set(true);
	}
	struct CTraits
	{
		using CBool = bool;
	};
	template <typename t_FSignature>
	struct TCFunction
	{
	};
	// The parameters of what a `new` makes take no concept prefix that the
	// instantiations would require.
	template <typename tf_CTraits>
	void fg_Make()
	{
		delete new TCFunction<void (typename tf_CTraits::CBool _Ready)>;
	}
	void fg_UseMake()
	{
		fg_Make<CTraits>();
	}
}

namespace NRound240
{
	template <typename t_CType>
	struct TCSharedPointer
	{
	};
	struct CExit
	{
	};
	using COnScopeExitShared = TCSharedPointer<CExit>;
	template <typename tf_CType>
	tf_CType fg_TempCopy(tf_CType const &_Value)
	{
		return _Value;
	}
	// A copy of what is named with an alias for what it is.
	void fg_Run(COnScopeExitShared const &_Scope)
	{
		auto Scope = fg_TempCopy(_Scope);
		auto fTask = [Scope = fg_TempCopy(_Scope)]
			{
			}
		;
		fTask();
	}
	struct CAction
	{
		void operator () () const
		{
		}
	};
	// A pack of callbacks copied into captures.
	template <typename... tfp_FActions>
	void fg_RunAll(tfp_FActions... p_fActions)
	{
		auto fTask = [...p_Actions = fg_TempCopy(p_fActions)]
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: 'p_Actions' is a function object and should have the 'f' concept prefix after its storage prefix [malterlib-conceptual-prefix]
			{
				(p_Actions(), ...);
			}
		;
		fTask();
	}
	void fg_Test()
	{
		fg_RunAll(CAction{}, CAction{});
	}
}

namespace NRound241
{
	// An element of what a parameter points to is generic.
	template <typename tf_CData>
	void fg_Visit(tf_CData *_pArray)
	{
		auto &Value = _pArray[0];
		tf_CData &Other = _pArray[0];
		(void)Value;
		(void)Other;
	}
	void fg_Use(bool *_pArray)
	{
		fg_Visit(_pArray);
	}
	template <typename t_CType>
	struct TCUniquePointer
	{
	};
	using CActorSubscription = TCUniquePointer<int>;
	template <typename tf_CType>
	tf_CType &&fg_Move(tf_CType &_Value)
	{
		return static_cast<tf_CType &&>(_Value);
	}
	CActorSubscription &fg_GetSubscription();
	// What a getter gives moved, named as the getter declares it.
	void fg_Moved()
	{
		auto Subscription = fg_Move(fg_GetSubscription());
		CActorSubscription Other = fg_Move(fg_GetSubscription());
		(void)Subscription;
		(void)Other;
	}
}

namespace NRound242
{
	template <typename t_FSignature, bool t_bCoalesced = false>
	struct TCActorFunctor
	{
	};
	struct CTraits
	{
		using CBool = bool;
		static constexpr bool mc_bCoalesced = false;
	};
	// The parameters of a default given a value of what the parameter is given
	// take no concept prefix that the instantiations would require.
	template
	<
		typename tf_CTraits
		, typename tf_FCallback = TCActorFunctor<void (typename tf_CTraits::CBool _Ready), tf_CTraits::mc_bCoalesced>
	>
	void fg_Run()
	{
	}
	template <typename t_FSignature>
	struct TCFunction
	{
	};
	// Also those of the type `sizeof` is of.
	template <typename tf_CTraits>
	void fg_Size()
	{
		(void)sizeof(TCFunction<void (typename tf_CTraits::CBool _Done)>);
	}
	void fg_Use()
	{
		fg_Run<CTraits>();
		fg_Size<CTraits>();
	}
}

namespace NRound243
{
	// A member of a class template read in a template, whose type is what the
	// template is given.
	template <typename t_CValue>
	struct TCEntry
	{
		t_CValue m_Value;
	};

	template <typename tf_CValue>
	void fg_Visit(TCEntry<tf_CValue> const &_Entry)
	{
		auto const &Value = _Entry.m_Value;
		(void)Value;
	}

	void fg_Use()
	{
		fg_Visit(TCEntry<bool>{true});
	}
}

namespace NRound244
{
	// What a getter of a class template gives, as it declares it, and an
	// element of a container given a parameter.
	template <typename t_CType>
	struct TCUniquePointer
	{
	};
	using CActorSubscription = TCUniquePointer<int>;

	template <typename tf_CType>
	tf_CType &&fg_Move(tf_CType &_Value)
	{
		return static_cast<tf_CType &&>(_Value);
	}

	template <typename t_CType>
	struct TCActorFunctor
	{
		CActorSubscription m_Subscription;
		CActorSubscription &f_GetSubscription()
		{
			return m_Subscription;
		}
		void f_Destroy()
		{
			auto Subscription = fg_Move(f_GetSubscription());
			(void)Subscription;
		}
	};

	template <typename t_CData>
	struct TCVector
	{
		t_CData m_Value{};
		t_CData &operator [] (int _Index)
		{
			return m_Value;
		}
		t_CData &f_GetFirst()
		{
			return m_Value;
		}
	};

	template <typename tf_CData>
	void fg_Visit(TCVector<tf_CData> &_Vector)
	{
		auto &Value = _Vector[0];
		auto &First = _Vector.f_GetFirst();
		(void)Value;
		(void)First;
	}

	void fg_Use()
	{
		TCActorFunctor<int> fFunctor;
		fFunctor.f_Destroy();
		TCVector<bool> Vector;
		fg_Visit(Vector);
	}
}

namespace NRound245
{
	// An element of a range of a parameter, and a binding of a member that is
	// one, are generic like it, and one of a pointer to it takes no concept
	// prefix that the instantiations would require.
	template <typename t_CData>
	struct TCVector
	{
		t_CData *begin();
		t_CData *end();
	};

	template <typename t_CValue>
	struct TCEntry
	{
		int m_Key;
		t_CValue m_Value;
	};

	template <typename tf_CData>
	void fg_Visit(TCVector<tf_CData> &_Vector, TCVector<tf_CData *> &_Pointers)
	{
		for (auto &Value : _Vector)
			(void)Value;
		for (auto &Value : _Pointers)
			(void)Value;
	}

	template <typename tf_CValue>
	void fg_Bind(TCEntry<tf_CValue> const &_Entry, TCEntry<tf_CValue *> const &_Pointer)
	{
		auto const &[Key, Value] = _Entry;
		(void)Key;
		(void)Value;
		auto const &[OtherKey, OtherValue] = _Pointer;
		(void)OtherKey;
		(void)OtherValue;
	}

	void fg_Run(TCVector<bool> &_Vector, TCVector<bool *> &_Pointers)
	{
		fg_Visit(_Vector, _Pointers);
		bool bValue = true;
		fg_Bind(TCEntry<bool>{1, true}, TCEntry<bool *>{1, &bValue});
	}
}

// The qualified names of the options are matched past the inline namespaces
// they leave out, and without printing the arguments of the templates the
// type is in, which can be exponentially long.
namespace std
{
	inline namespace __1
	{
		template <typename t_CSignature>
		struct function
		{
		};
	}
}
std::function<void ()> g_Callback;
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: 'g_Callback' is a function object and should have the 'f' concept prefix after its storage prefix [malterlib-conceptual-prefix]
std::function<void ()> g_fCallbackQualified;

namespace NRound269
{
	template <int t_N>
	struct TCSignature
	{
		using CPrevious = typename TCSignature<t_N - 1>::CType;
		using CType = std::function<CPrevious (CPrevious)>;
	};
	template <>
	struct TCSignature<0>
	{
		using CType = int;
	};
	template <typename t_CType>
	struct TCList
	{
		struct CIterator
		{
			t_CType &operator * () const;
			t_CType *operator -> () const;
			CIterator &operator ++ ();
		};
	};
	TCList<TCSignature<40>::CType>::CIterator g_iIterator;
}

namespace NRound331 {
// Also copies initialized in parentheses, `auto Copy(_fAction)`.
template <typename tf_CType>
tf_CType &&fg_Move(tf_CType &_Value) {
  return static_cast<tf_CType &&>(_Value);
}
struct CAction {
  void operator()() const {}
};
template <typename tf_FAction> void fg_Use(tf_FAction _fAction) {
  auto Copy(_fAction);
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: 'Copy' is a function object and should have the 'f' concept prefix [malterlib-conceptual-prefix]
  auto Moved(fg_Move(_fAction));
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: 'Moved' is a function object and should have the 'f' concept prefix [malterlib-conceptual-prefix]
  Copy();
  Moved();
}
void fg_Test() { fg_Use(CAction{}); }
// Also what a template gives that it is given, a member of what a parameter
// is given, `typename TCIdentity<typename tf_CTraits::FSignature>::CType`.
struct CTraits {
  using FSignature = CAction;
};
template <typename t_CType> struct TCIdentity {
  using CType = t_CType;
};
template <typename tf_CTraits>
void fg_Run(typename TCIdentity<typename tf_CTraits::FSignature>::CType _Callback) {}
// CHECK-MESSAGES: :[[@LINE-1]]:73: warning: '_Callback' is a function object and should have the 'f' concept prefix after its storage prefix [malterlib-conceptual-prefix]
void fg_RunTest() { fg_Run<CTraits>(CAction{}); }
}

namespace NRound332 {
// Moved from either of a conditional of what is written as the same.
template <typename tf_CType>
tf_CType &&fg_Move(tf_CType &_Value) {
  return static_cast<tf_CType &&>(_Value);
}
struct CAction {
  void operator()() const {}
};
using FAction = CAction;
template <typename t_CType> struct TCUniquePointer {
  t_CType *operator->() const;
};
using CActorSubscription = TCUniquePointer<int>;
void fg_Run(bool _bFirst, FAction &_fFirst, FAction &_fSecond,
            CActorSubscription &_First, CActorSubscription &_Second) {
  auto Action = fg_Move(_bFirst ? _fFirst : _fSecond);
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: 'Action' is a function object and should have the 'f' concept prefix [malterlib-conceptual-prefix]
  auto Subscription = fg_Move(_bFirst ? _First : _Second);
  Action();
  (void)Subscription;
}
// Also what a template gives that it is given referred to, `tf_FCallback const
// &`.
template <typename t_CType> struct TCIdentity {
  using CType = t_CType;
};
template <typename tf_FCallback>
void fg_Forward(tf_FCallback &_fCallback) {
  typename TCIdentity<tf_FCallback const &>::CType Callback = _fCallback;
// CHECK-MESSAGES: :[[@LINE-1]]:52: warning: 'Callback' is a function object and should have the 'f' concept prefix [malterlib-conceptual-prefix]
  Callback();
}
void fg_ForwardTest() {
  CAction fCallback;
  fg_Forward(fCallback);
}
}

namespace NRound332Contradicts {
// A concept prefix that the type can't be, and `c`, which isn't one.
struct CPlain {
  int m_Value;
};
double g_fRatio = 1.0;
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: 'g_fRatio' has the 'f' concept prefix of a function object, but is not one [malterlib-conceptual-prefix]
int g_pCount = 0;
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: 'g_pCount' has the 'p' concept prefix of a pointer, but is not one [malterlib-conceptual-prefix]
float g_bScale = 0.0f;
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: 'g_bScale' has the 'b' concept prefix of a boolean, but is not one [malterlib-conceptual-prefix]
CPlain g_fPlain;
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: 'g_fPlain' has the 'f' concept prefix of a function object, but is not one [malterlib-conceptual-prefix]
int g_bFlags = 0;
}

namespace NRound333 {
// An array has no concept prefix, whatever its elements are.
const char *g_pWeekDays[7] = {};
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'g_pWeekDays' is an array, which has no concept prefix [malterlib-conceptual-prefix]
bool g_bFlags[4] = {};
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: 'g_bFlags' is an array, which has no concept prefix [malterlib-conceptual-prefix]
const char *g_WeekDays[7] = {};
bool g_Flags[4] = {};
// A concept prefix the type can't be, also deduced and in a template, and of a
// value of a parameter where each instantiation excludes it.
void fg_Deduced() {
  auto fRatio = 0.5;
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: 'fRatio' has the 'f' concept prefix of a function object, but is not one [malterlib-conceptual-prefix]
  auto pCount = 42;
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: 'pCount' has the 'p' concept prefix of a pointer, but is not one [malterlib-conceptual-prefix]
  auto bRatio = 0.5;
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: 'bRatio' has the 'b' concept prefix of a boolean, but is not one [malterlib-conceptual-prefix]
  (void)fRatio;
  (void)pCount;
  (void)bRatio;
}
template <typename tf_CType>
int fg_Count(tf_CType const &_Value) { return 1; }
template <typename tf_CType>
void fg_InTemplate(tf_CType _Value) {
  int fCount = 42;
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: 'fCount' has the 'f' concept prefix of a function object, but is not one [malterlib-conceptual-prefix]
  auto pNumber = fg_Count(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: 'pNumber' has the 'p' concept prefix of a pointer, but is not one [malterlib-conceptual-prefix]
  auto pValue = _Value;
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: 'pValue' has the 'p' concept prefix of a pointer, but is not one [malterlib-conceptual-prefix]
  (void)fCount;
  (void)pNumber;
  (void)pValue;
}
void fg_UseTemplate() { fg_InTemplate(1); }
}

namespace NRound334 {
// A smart pointer through an alias is a pointer, also of a reference to it and
// through an alias template, but not through an alias listed as a handle, which
// is named for what it gives.
template <typename t_CType> struct TCSharedPointer {
  t_CType *operator->() const;
};
struct CNode {};
using CNodePtr = TCSharedPointer<CNode>;
using CNodeReference = TCSharedPointer<CNode> &;
template <typename t_CType> using TCNodePtr = TCSharedPointer<t_CType>;
using CActorSubscription = TCSharedPointer<CNode>;
using CSubscriptionAlias = CActorSubscription;
CNodePtr g_Node;
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: 'g_Node' is a pointer and should have the 'p' concept prefix after its storage prefix [malterlib-conceptual-prefix]
CNodePtr g_pNode;
TCNodePtr<CNode> g_Other;
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: 'g_Other' is a pointer and should have the 'p' concept prefix after its storage prefix [malterlib-conceptual-prefix]
CActorSubscription g_Subscription;
CSubscriptionAlias g_AliasedSubscription;
void fg_Take(CNodeReference _Node) {}
// CHECK-MESSAGES: :[[@LINE-1]]:29: warning: '_Node' is a pointer and should have the 'p' concept prefix after its storage prefix [malterlib-conceptual-prefix]
template <typename tf_CType>
void fg_TakeDependent(TCNodePtr<tf_CType> _Node) {}
// CHECK-MESSAGES: :[[@LINE-1]]:43: warning: '_Node' is a pointer and should have the 'p' concept prefix after its storage prefix [malterlib-conceptual-prefix]
}

namespace NRound334Generic {
// A copy of a value whose type is a member alias that forwards a template
// argument, also imported with `using typename`, or through a trait that
// removes a reference, and an element through an `auto` iterator, are named
// for the generic type.
template <typename t_CKey, typename t_CValue> struct TCMap {
  using CValue = t_CValue;
};
template <typename tf_CMap>
void fg_Process(typename tf_CMap::CValue const &_Value) {
  auto Value = _Value;
  (void)Value;
}
void fg_UseProcess() { fg_Process<TCMap<int, bool>>(true); }
template <typename t_CValue> struct TCAdapter : TCMap<int, t_CValue> {
  using typename TCMap<int, t_CValue>::CValue;
  CValue m_Value;
  void f_Set(CValue _Value) { m_Value = _Value; }
};
void fg_UseAdapter() {
  TCAdapter<bool> Adapter{};
  Adapter.f_Set(true);
}
template <typename t_CType>
using TCRemoveReference = __remove_reference_t(t_CType);
template <typename t_CKey, typename t_CValue> struct TCUserMap {
  using CUserData = TCRemoveReference<t_CValue>;
  CUserData &operator[](t_CKey const &_Key);
};
template <typename tf_CValue> void fg_Run(TCUserMap<int, tf_CValue> &_Map) {
  auto &Value = _Map[0];
  (void)Value;
}
void fg_UseUserMap(TCUserMap<int, bool> &_Map) { fg_Run(_Map); }
template <typename t_CType>
struct TCRemoveReferenceHelper { using CType = t_CType; };
template <typename t_CType>
struct TCRemoveReferenceHelper<t_CType &> { using CType = t_CType; };
template <typename t_CType>
using TCOwnRemoveReference = typename TCRemoveReferenceHelper<t_CType>::CType;
template <typename t_CType> struct TCValueMap {
  using CValue = TCOwnRemoveReference<t_CType>;
  CValue &operator[](int _Index);
};
template <typename tf_CType> void fg_Visit(TCValueMap<tf_CType> &_Map) {
  auto &Value = _Map[0];
  (void)Value;
}
void fg_UseValueMap(TCValueMap<bool> &_Map) { fg_Visit(_Map); }
template <typename t_CData> struct TCVectorIterator {
  operator t_CData *() const;
  TCVectorIterator &operator++();
};
template <typename t_CData> struct TCVector {
  TCVectorIterator<t_CData> f_GetIterator();
};
template <typename tf_CData> void fg_Iterate(TCVector<tf_CData> &_Vector) {
  auto iValue = _Vector.f_GetIterator();
  auto &Value = *iValue;
  (void)Value;
}
void fg_UseVector(TCVector<bool> &_Vector) { fg_Iterate(_Vector); }
// Also through a specialization that gives another type, and an iterator that
// gives one of its own, which take no concept prefix that the instantiations
// would require.
template <typename t_CType> struct TCSelectHelper { using CType = t_CType; };
template <typename t_CType> struct TCSelectHelper<t_CType *> {
  using CType = bool;
};
template <typename t_CType>
using TCSelect = typename TCSelectHelper<t_CType>::CType;
template <typename t_CType> struct TCSelectMap {
  using CValue = TCSelect<t_CType>;
  CValue &operator[](int _Index);
};
template <typename tf_CType> void fg_Select(TCSelectMap<tf_CType> &_Map) {
  auto &Value = _Map[0];
  (void)Value;
}
void fg_UseSelectMap(TCSelectMap<int *> &_Map) { fg_Select(_Map); }
template <typename t_CData> struct TCFixedIterator {
  operator bool *() const;
  TCFixedIterator &operator++();
};
template <typename t_CData> struct TCFixedVector {
  TCFixedIterator<t_CData> f_GetIterator();
};
template <typename tf_CData>
void fg_IterateFixed(TCFixedVector<tf_CData> &_Vector) {
  auto iValue = _Vector.f_GetIterator();
  auto &Value = *iValue;
  (void)Value;
}
void fg_UseFixed(TCFixedVector<int> &_Vector) { fg_IterateFixed(_Vector); }
}
namespace NRound334Contradiction {
// What each instantiation makes a traits member is what it can't be named as.
struct CTraits {
  using CType = int;
  using CReal = double;
};
template <typename t_CTraits> struct TCStorage {
  typename t_CTraits::CType m_pCount = 0;
// CHECK-MESSAGES: :[[@LINE-1]]:29: warning: 'm_pCount' has the 'p' concept prefix of a pointer, but is not one [malterlib-conceptual-prefix]
  typename t_CTraits::CType m_fValue = 0;
// CHECK-MESSAGES: :[[@LINE-1]]:29: warning: 'm_fValue' has the 'f' concept prefix of a function object, but is not one [malterlib-conceptual-prefix]
  typename t_CTraits::CReal m_bRatio = 0;
// CHECK-MESSAGES: :[[@LINE-1]]:29: warning: 'm_bRatio' has the 'b' concept prefix of a boolean, but is not one [malterlib-conceptual-prefix]
};
TCStorage<CTraits> g_Storage;
template <typename tf_CTraits> void fg_Run(typename tf_CTraits::CType _fValue) {}
// CHECK-MESSAGES: :[[@LINE-1]]:71: warning: '_fValue' has the 'f' concept prefix of a function object, but is not one [malterlib-conceptual-prefix]
void fg_Use() { fg_Run<CTraits>(42); }
// Dereferencing an `F` template parameter gives a function.
void fg_Callback() {}
template <typename tf_FAction> void fg_Call(tf_FAction *_pAction) {
  auto Copy = *_pAction;
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: 'Copy' is a function object and should have the 'f' concept prefix [malterlib-conceptual-prefix]
  Copy();
}
void fg_Test() { fg_Call(&fg_Callback); }
}
namespace NRound334Arrays {
// An array parameter pack has no concept prefix either.
template <unsigned... tfp_nChars>
void fg_Append(char const (&...p_pParts)[tfp_nChars]) {}
// CHECK-MESSAGES: :[[@LINE-1]]:32: warning: 'p_pParts' is an array, which has no concept prefix [malterlib-conceptual-prefix]
void fg_Run() { fg_Append("Alpha", "Beta"); }
}
namespace NRound334Handles {
// A copy of a handle listed in the options is a handle also outside a
// function.
template <typename t_CType> struct TCSharedPointer {};
struct CScope {};
using COnScopeExitShared = TCSharedPointer<CScope>;
template <typename tf_CType> tf_CType fg_TempCopy(tf_CType const &_Value) {
  return _Value;
}
COnScopeExitShared g_Scope;
auto g_Copy = fg_TempCopy(g_Scope);
struct CHolder {
  static inline auto ms_Copy = fg_TempCopy(g_Scope);
};
void fg_Run() { auto Copy = fg_TempCopy(g_Scope); }
}

namespace NRound335Copied {
// An element of a local copy of a container given a parameter, also what a
// copy of a function object gives, is generic.
template <typename t_CValue> struct TCVector {
  t_CValue &operator[](int _iIndex);
};
template <typename tf_CValue> void fg_Test(TCVector<tf_CValue> const &_Input) {
  auto Copy = _Input;
  auto Value = Copy[0];
  (void)Value;
}
void fg_Use(TCVector<bool> const &_Input) { fg_Test(_Input); }
}
namespace NRound335Arrays {
// What each instantiation makes an array has no concept prefix.
struct CTraits {
  using CStorage = unsigned char[8];
  using CPointers = int *[4];
};
template <typename t_CTraits> struct TCStorage {
  typename t_CTraits::CStorage m_pBuffer;
// CHECK-MESSAGES: :[[@LINE-1]]:32: warning: 'm_pBuffer' is an array, which has no concept prefix [malterlib-conceptual-prefix]
  typename t_CTraits::CPointers m_pStorage{};
// CHECK-MESSAGES: :[[@LINE-1]]:33: warning: 'm_pStorage' is an array, which has no concept prefix [malterlib-conceptual-prefix]
};
TCStorage<CTraits> g_Storage;
}
namespace NRound335Members {
// What a member function of the class itself gives in it is of its
// parameters, also an iterator.
template <typename t_CType> struct TCVectorIterator {
  operator t_CType *() const;
  void operator++();
};
template <typename t_CType> struct TCVector {
  using CIterator = TCVectorIterator<t_CType>;
  CIterator f_GetIterator() const;
  t_CType m_Value;
  t_CType &f_GetFirst() { return m_Value; }
  void f_Run() const {
    auto iValue = f_GetIterator();
    auto const &Value = *iValue;
    (void)Value;
  }
  void f_Visit() {
    auto &Value = f_GetFirst();
    (void)Value;
  }
};
void fg_Run(TCVector<bool> &_Vector) {
  _Vector.f_Run();
  _Vector.f_Visit();
}
}
namespace NRound335MapIterator {
// An iterator given its map gives the member of that it names.
template <typename t_CMap> struct TCMapIterator {
  using CValue = typename t_CMap::CValue;
  operator CValue *() const;
  void operator++();
};
template <typename t_CKey, typename t_CValue> struct TCMap {
  using CValue = t_CValue;
  using CIterator = TCMapIterator<TCMap>;
  CIterator f_GetIterator() const;
};
template <typename tf_CValue> void fg_Visit(TCMap<int, tf_CValue> const &_Map) {
  auto iValue = _Map.f_GetIterator();
  auto const &Value = *iValue;
  (void)Value;
}
void fg_Test(TCMap<int, bool> const &_Map) { fg_Visit(_Map); }
}
namespace NRound335Bindings {
// A structured binding of a class with an empty base binds its members.
struct CEmpty {};
template <typename t_CValue> struct TCEntry : CEmpty {
  t_CValue m_Value{};
};
template <typename tf_CValue> void fg_Visit(TCEntry<tf_CValue> const &_Entry) {
  auto const &[Value] = _Entry;
  (void)Value;
}
void fg_Run() { fg_Visit(TCEntry<bool>{}); }
}
namespace NRound335Aliases {
// An alias template names what it is an alias of: an atomic of a member of
// traits takes no concept prefix that the instantiations would require, through
// it or directly.
template <typename t_CType> struct TCAtomic { t_CType m_Value; };
template <typename t_CType> using TCWrapped = TCAtomic<t_CType>;
struct CTraits { using CBool = bool; };
template <typename t_CTraits> struct TCState {
  TCWrapped<typename t_CTraits::CBool> m_Ready;
  TCAtomic<typename t_CTraits::CBool> m_Direct;
};
TCState<CTraits> g_State;
}
namespace NRound335Functions {
// An element of a parameter named as a function takes no concept prefix that
// the instantiations would require, which a specialization of the container
// can change.
template <typename t_CData> struct TCVector {
  t_CData m_Data;
  t_CData &operator[](int _iIndex) { return m_Data; }
};
template <typename t_FSignature> struct TCFunction { void operator()() {} };
template <typename tf_FCallback> void fg_Test(TCVector<tf_FCallback> &_Vector) {
  auto &Callback = _Vector[0];
  (void)Callback;
}
void fg_Use() {
  TCVector<TCFunction<void()>> Vector;
  fg_Test(Vector);
}
}
namespace NRound335CopiedFunction {
template <typename t_FSignature> struct TCFunction;
template <typename t_CReturn> struct TCFunction<t_CReturn()> {
  t_CReturn operator()();
};
template <typename tf_CReturn> void fg_Call(TCFunction<tf_CReturn()> &_fCallback) {
  auto fCopy = _fCallback;
  auto Result = fCopy();
  (void)Result;
}
void fg_Use(TCFunction<bool()> &_fCallback) { fg_Call(_fCallback); }
}
namespace NRound335OwnFixed {
// Also what a member function gives of a type of its own takes no concept
// prefix that the instantiations would require, which a specialization can
// change.
template <typename t_CType> struct TCVector {
  bool f_IsValid() const;
  void f_Run() const {
    auto Valid = f_IsValid();
    (void)Valid;
  }
};
void fg_Run(TCVector<int> const &_Vector) { _Vector.f_Run(); }
}
namespace NRound335Values {
// And an element of another parameter is generic.
template <typename t_CData> struct TCVector {
  t_CData m_Data;
  t_CData &operator[](int _iIndex) { return m_Data; }
};
template <typename tf_CValue> void fg_Test(TCVector<tf_CValue> &_Vector) {
  auto &Value = _Vector[0];
  (void)Value;
}
void fg_Use() {
  TCVector<bool> Vector;
  fg_Test(Vector);
}
}

namespace NRound336Functions {
// An element of a parameter named as a function type takes no concept prefix
// that the instantiations would require, also of a class not listed as one,
// and a copy of it, but can have the `f` one.
struct CAction {
  void operator()() const {}
};
template <typename t_CValue> struct TCVector {
  t_CValue m_Value;
  t_CValue &operator[](int _iIndex) { return m_Value; }
};
template <typename tf_FCallback>
void fg_Visit(TCVector<tf_FCallback> &_Callbacks) {
  auto &Callback = _Callbacks[0];
  auto Copy = _Callbacks[0];
  auto &fCallback = _Callbacks[0];
  (void)Callback;
  (void)Copy;
  (void)fCallback;
}
void fg_Run(TCVector<CAction> &_Callbacks) { fg_Visit(_Callbacks); }
}
namespace NRound336Converted {
// What a function template gives of a parameter it is given explicitly is
// generic.
template <typename tf_CTo, typename tf_CFrom>
tf_CTo fg_Convert(tf_CFrom const &_From) {
  return tf_CTo(_From);
}
template <typename tf_CType> void fg_Run(int _Value) {
  auto Converted = fg_Convert<tf_CType>(_Value);
  auto Fixed = fg_Convert<bool>(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: 'Fixed' is a boolean and should have the 'b' concept prefix [malterlib-conceptual-prefix]
  (void)Converted;
  (void)Fixed;
}
void fg_Test() { fg_Run<bool>(1); }
}
namespace NRound336Copies {
// A copy of an element of a range, also by reference and in an init capture,
// and of a structured binding, is generic like it.
template <typename t_CValue> struct TCVector {
  t_CValue *begin();
  t_CValue *end();
};
template <typename t_CValue> struct TCEntry {
  t_CValue m_Value;
};
template <typename tf_CValue>
void fg_Copy(TCVector<tf_CValue> &_Values, TCEntry<tf_CValue> &_Entry) {
  for (auto &Value : _Values) {
    auto Copy = Value;
    auto &Reference = Value;
    auto fCapture = [Captured = Value] {};
    (void)Copy;
    (void)Reference;
    (void)fCapture;
  }
  auto &[Bound] = _Entry;
  auto BoundCopy = Bound;
  (void)BoundCopy;
}
void fg_Use(TCVector<bool> &_Values, TCEntry<bool> &_Entry) {
  fg_Copy(_Values, _Entry);
}
}
namespace NRound336Handles {
// What a member function gives of a parameter of its class template is named
// as the argument is written, also a handle, and so is a deduced value of the
// type of a handle where that spells the smart pointer.
template <typename t_CValue> struct TCUniquePointer {};
using CActorSubscription = TCUniquePointer<int>;
template <typename t_CValue> struct TCFuture {
  t_CValue f_Get();
};
void fg_Run(TCFuture<CActorSubscription> &_Result,
            TCFuture<TCUniquePointer<int>> &_Other) {
  auto Subscription = _Result.f_Get();
  auto Spelled = _Other.f_Get();
  (void)Subscription;
  (void)Spelled;
}
}
namespace NRound336NestedIterator {
// An iterator given its map gives what a class in that names.
template <typename t_CMap> struct TCMapIterator {
  using CValue = typename t_CMap::CNode::CValue;
  operator CValue *() const;
  void operator++();
};
template <typename t_CValue> struct TCMap {
  struct CNode {
    using CValue = t_CValue;
  };
  TCMapIterator<TCMap> f_GetIterator();
};
template <typename tf_CValue> void fg_Visit(TCMap<tf_CValue> &_Map) {
  auto iValues = _Map.f_GetIterator();
  auto &Value = *iValues;
  (void)Value;
}
void fg_Run(TCMap<bool> &_Map) { fg_Visit(_Map); }
}
namespace NRound336Arrow {
// What a member of what an iterator's `operator->` gives is of the
// parameters of the template its class is in, and one of its own takes no
// concept prefix that the instantiations would require.
template <typename t_CKey, typename t_CValue> struct TCMapNode {
  t_CValue &f_Value();
  bool f_IsValid();
};
template <typename t_CKey, typename t_CValue> struct TCMap {
  using CNode = TCMapNode<t_CKey, t_CValue>;
  struct CIterator {
    CNode *operator->();
    void operator++();
  };
  CIterator f_GetIterator();
};
template <typename tf_CValue> void fg_Visit(TCMap<int, tf_CValue> &_Map) {
  auto iEntry = _Map.f_GetIterator();
  auto &Value = iEntry->f_Value();
  auto Valid = iEntry->f_IsValid();
  (void)Value;
  (void)Valid;
}
void fg_Test(TCMap<int, bool> &_Map) { fg_Visit(_Map); }
}

namespace NRound337Handles {
// A handle is named as one, also where the type it is deduced as doesn't
// keep its alias: moved in either result of a conditional, an element of a
// range or by index, copied from what a getter gives, and awaited, also where
// that spells the smart pointer; and takes no concept prefix.
template <typename t_CType> struct TCUniquePointer {};
using CActorSubscription = TCUniquePointer<int>;
using COnScopeExitShared = TCUniquePointer<char>;
template <typename tf_CType> tf_CType &&fg_Move(tf_CType &_Value) {
  return static_cast<tf_CType &&>(_Value);
}
template <typename t_CValue> struct TCVector {
  t_CValue *begin();
  t_CValue *end();
  t_CValue &operator[](int _iIndex);
  t_CValue &f_GetFirst();
};
void fg_Test(bool _bFirst, CActorSubscription &_First,
             CActorSubscription &_Second,
             TCVector<CActorSubscription> &_Subscriptions,
             TCVector<COnScopeExitShared> &_Scopes,
             TCVector<TCUniquePointer<int>> &_Pointers) {
  auto Subscription = _bFirst ? fg_Move(_First) : fg_Move(_Second);
  for (auto &Element : _Subscriptions)
    (void)Element;
  auto &Indexed = _Subscriptions[0];
  auto Scope = _Scopes.f_GetFirst();
  auto &Spelled = _Pointers[0];
  COnScopeExitShared pScope;
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: 'pScope' has the 'p' concept prefix of a pointer, but is not one [malterlib-conceptual-prefix]
  (void)Subscription;
  (void)Indexed;
  (void)Scope;
  (void)Spelled;
  (void)pScope;
}
}
namespace NRound337Tuple {
// A structured binding through `get` of an element its `std::tuple_element`
// gives as it is given is generic.
}
namespace std {
template <class> struct tuple_size;
template <unsigned long, class> struct tuple_element;
}
namespace NRound337Tuple {
template <class t_CValue> struct TCMapNode {
  t_CValue m_Value;
  template <unsigned long tf_iValue> t_CValue &get() { return m_Value; }
};
}
namespace std {
template <class t_CValue> struct tuple_size<NRound337Tuple::TCMapNode<t_CValue>> {
  static constexpr int value = 1;
};
template <unsigned long t_iValue, class t_CValue>
struct tuple_element<t_iValue, NRound337Tuple::TCMapNode<t_CValue>> {
  using type = t_CValue;
};
}
namespace NRound337Tuple {
template <class tf_CValue> void fg_Visit(TCMapNode<tf_CValue> &_Node) {
  auto &[Value] = _Node;
  (void)Value;
}
void fg_Use(TCMapNode<bool> &_Node) { fg_Visit(_Node); }
}


namespace NRound338Handles {
// A handle is named as one also as a member of what a class template is
// given, through a structured binding of it, and what a function object
// whose signature a partial specialization takes apart gives, also where that
// spells the smart pointer.
template <typename t_CType> struct TCSharedPointer {};
using CActorSubscription = TCSharedPointer<int>;
template <typename t_CValue> struct TCEntry {
  t_CValue &m_Value;
};
template <typename t_CValue> struct TCPair {
  int m_Key;
  t_CValue m_Value;
};
template <typename t_FSignature> struct TCFunction;
template <typename t_CResult, typename... tp_CParams>
struct TCFunction<t_CResult(tp_CParams... p_Params)> {
  t_CResult operator()(tp_CParams... p_Params);
};
void fg_Run(TCEntry<CActorSubscription> const &_Entry,
            TCPair<CActorSubscription> &_Pair,
            TCFunction<CActorSubscription()> &_fSubscribe,
            TCFunction<TCSharedPointer<int>()> &_fPointer) {
  auto Subscription = _Entry.m_Value;
  auto &[Key, BoundSubscription] = _Pair;
  auto Subscribed = _fSubscribe();
  auto pSubscribed = _fSubscribe();
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: 'pSubscribed' has the 'p' concept prefix of a pointer, but is not one [malterlib-conceptual-prefix]
  auto Spelled = _fPointer();
  (void)Subscription;
  (void)Key;
  (void)BoundSubscription;
  (void)Subscribed;
  (void)pSubscribed;
  (void)Spelled;
}
}

namespace NRound338Helper {
// What a free helper gives of a parameter it deduces from a specialization
// given one is generic.
template <class t_CValue> struct TCVector {
  t_CValue &operator[](int _iIndex);
};
template <class tf_CValue> tf_CValue fg_First(TCVector<tf_CValue> &_Values) {
  return _Values[0];
}
template <class tf_CValue> void fg_Run(TCVector<tf_CValue> &_Values) {
  auto Value = fg_First(_Values);
  (void)Value;
}
void fg_Use(TCVector<bool> &_Values) { fg_Run(_Values); }
}
namespace NRound338Iterator {
// What a member of what an iterator of its own template gives through its
// `operator->`, a member of the map it is given, is of the map's parameters.
template <typename t_CKey, typename t_CValue> struct TCMapNode {
  t_CValue &f_Value();
};
template <typename t_CMap> struct TCMapIterator {
  using CNode = typename t_CMap::CNode;
  CNode *operator->() const;
  void operator++();
};
template <typename t_CKey, typename t_CValue> struct TCMap {
  using CNode = TCMapNode<t_CKey, t_CValue>;
  TCMapIterator<TCMap> f_GetIterator();
};
template <typename tf_CValue> void fg_Visit(TCMap<int, tf_CValue> &_Map) {
  auto iEntry = _Map.f_GetIterator();
  auto &Value = iEntry->f_Value();
  (void)Value;
}
void fg_Test(TCMap<int, bool> &_Map) { fg_Visit(_Map); }
}
namespace NRound338Inherited {
// A member of a base that doesn't depend on the parameters, read through a
// specialization given them, takes no concept prefix that the instantiations
// would require, which a specialization can change.
struct CFutureCoroutineContext {
  bool m_bIsCalledSafely;
};
template <typename t_CResult>
struct TCFutureCoroutineContextShared : CFutureCoroutineContext {};
template <typename tf_CResult>
void fg_Inspect(TCFutureCoroutineContextShared<tf_CResult> const &_Context) {
  auto CalledSafely = _Context.m_bIsCalledSafely;
  (void)CalledSafely;
}
void fg_Test(TCFutureCoroutineContextShared<bool> const &_Context) {
  fg_Inspect(_Context);
}
}

namespace NRound339InheritedGetter {
// A handle given to the base of a class that isn't a template is a handle as
// what a getter of the base gives, as is any deduced value of its type.
template <typename t_CType> struct TCUniquePointer {};
using CActorSubscription = TCUniquePointer<int>;
template <typename t_CValue> struct TCVector {
  t_CValue &f_GetFirst();
};
struct CSubscriptions : TCVector<CActorSubscription> {};
void fg_Test(CSubscriptions &_Subscriptions) {
  auto Subscription = _Subscriptions.f_GetFirst();
  (void)Subscription;
}
}

namespace NRound339CallOperator {
// What the call operator of a class template gives, `_Map(0)`, is written
// with its parameters, as a member it gives is.
template<typename t_CReturnType> struct TCMapResult {
  t_CReturnType f_GetResult();
};
template<typename t_CKey, typename t_CValue> struct TCMap {
  TCMapResult<t_CValue &> operator()(t_CKey const &_Key);
};
template<typename tf_CValue>
void fg_Run(TCMap<int, tf_CValue> &_Map) {
  auto &Value = _Map(0).f_GetResult();
  (void)Value;
}
void fg_Test(TCMap<int, bool> &_Map) { fg_Run(_Map); }
}

namespace NRound339ConstexprReturn {
// What a member function template given its arguments returns is what the
// branches of an `if constexpr` they choose return.
template<typename t_CKey, typename t_CValue> struct TCMapNode {
  t_CKey &f_Key();
  t_CValue &f_Value();
  template<unsigned tf_iValue> decltype(auto) get() {
    if constexpr (tf_iValue == 0) return f_Key();
    else return f_Value();
  }
};
template<typename tf_CValue>
void fg_Run(TCMapNode<int, tf_CValue> &_Node) {
  auto &Value = _Node.template get<1>();
  (void)Value;
}
void fg_Test(TCMapNode<int, bool> &_Node) { fg_Run(_Node); }
}

namespace NRound339InheritedCall {
// A handle is one also through a call operator of a base specialized
// otherwise.
template<class t_CType> struct TCUniquePointer {};
using CActorSubscription = TCUniquePointer<int>;
struct CStorage {};
template<int t_iCall, class t_CBase, class t_FSignature, int t_Qualifiers>
struct TCFunctionImplementation0;
template<class t_CBase, class t_CReturn>
struct TCFunctionImplementation0<0, t_CBase, t_CReturn(), 0> {
  t_CReturn operator()() const;
};
template<class t_FSignature>
struct TCFunction : TCFunctionImplementation0<0, CStorage, t_FSignature, 0> {};
void fg_Run(TCFunction<CActorSubscription()> const &_fMake) {
  auto Subscription = _fMake();
  (void)Subscription;
}
}

namespace NRound339RemoveReference {
// An element of a map whose alias of it takes off what its parameter refers
// to is of that parameter.
template<class t_CType> struct TCStrip { using CType = t_CType; };
template<class t_CType> struct TCStrip<t_CType &> { using CType = t_CType; };
template<class t_CType> using TCRemoveReference = typename TCStrip<t_CType>::CType;
template<class t_CMap> struct TCMapIterator {
  using CUserData = typename t_CMap::CUserData;
  operator CUserData *() const;
  void operator++();
};
template<class t_CKey, class t_CValue> struct TCMap {
  using CUserData = TCRemoveReference<t_CValue>;
  TCMapIterator<TCMap> f_GetIterator();
};
template<class tf_CValue>
void fg_Visit(TCMap<int, tf_CValue> &_Map) {
  auto iValue = _Map.f_GetIterator();
  auto &Value = *iValue;
  (void)Value;
}
void fg_Test(TCMap<int, bool> &_Map) { fg_Visit(_Map); }
}

namespace NRound339SignatureTrait {
// A handle is one also through a trait of a signature, and so is not a
// pointer.
template <typename t_CType> struct TCSharedPointer {};
using CActorSubscription = TCSharedPointer<int>;
template <typename t_FSignature> struct TCFunctionTraits;
template <typename t_CReturn> struct TCFunctionTraits<t_CReturn()> {
  using CReturn = t_CReturn;
};
template <typename t_FSignature> struct TCFunction {
  using CReturn = typename TCFunctionTraits<t_FSignature>::CReturn;
  CReturn operator()();
};
void fg_Run(TCFunction<CActorSubscription()> &_fSubscribe) {
  auto Subscription = _fSubscribe();
  auto pSubscription = _fSubscribe();
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: 'pSubscription' has the 'p' concept prefix of a pointer, but is not one [malterlib-conceptual-prefix]
  (void)Subscription;
  (void)pSubscription;
}
}

namespace NRound339PointerArithmetic {
// What a pointer to a parameter moved along points to is of it.
template <typename tf_CValue> void fg_Read(tf_CValue *_pValues) {
  auto First = _pValues[0];
  auto Next = *(_pValues + 1);
  (void)First;
  (void)Next;
}
void fg_Run(bool *_pValues) { fg_Read(_pValues); }
}

namespace NRound339NonTypeArguments {
// A handle is one also through a partial specialization that takes values
// too.
template <typename t_CType> struct TCSharedPointer {};
struct CNode {};
using CActorSubscription = TCSharedPointer<CNode>;
template <int t_Index, typename t_FSignature> struct TCCall;
template <int t_Index, typename t_CReturn> struct TCCall<t_Index, t_CReturn ()> {
  t_CReturn operator()() const;
};
void fg_Use(TCCall<0, CActorSubscription ()> const &_fCall) {
  auto Subscription = _fCall();
  (void)Subscription;
}
}

namespace NRound339BindingInstances {
// A structured binding in a template can't have a concept prefix that what
// it is in each instantiation can't have.
struct CTraits { using CValue = int; };
template <typename t_CTraits> struct TCEntry {
  typename t_CTraits::CValue m_Value;
};
template <typename tf_CTraits> void fg_Run(TCEntry<tf_CTraits> &_Entry) {
  auto [fValue] = _Entry;
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: 'fValue' has the 'f' concept prefix of a function object, but is not one [malterlib-conceptual-prefix]
  (void)fValue;
}
void fg_Use(TCEntry<CTraits> &_Entry) { fg_Run(_Entry); }
}

namespace NRound340InstanceHandle {
// A handle deduced in a template is one in each instantiation too.
template<typename t_CType> struct TCUniquePointer {};
struct CActorSubscriptionReference {};
using CActorSubscription = TCUniquePointer<CActorSubscriptionReference>;
template<typename tf_CType> TCUniquePointer<tf_CType> fg_Make() { return {}; }
template<typename tf_CType> void fg_Run() {
  auto Subscription = fg_Make<tf_CType>();
  (void)Subscription;
}
void fg_Test() { fg_Run<CActorSubscriptionReference>(); }
}

namespace NRound340LambdaReturn {
// What a local lambda written to return a parameter gives is of it.
template<typename t_CValue> struct TCVector { t_CValue &f_GetFirst(); };
template<typename tf_CValue> void fg_Read(TCVector<tf_CValue> &_Values) {
  auto fFirst = [&]() -> tf_CValue { return _Values.f_GetFirst(); };
  auto First = fFirst();
  auto Direct = _Values.f_GetFirst();
  (void)First;
  (void)Direct;
}
void fg_Use(TCVector<bool> &_Values) { fg_Read(_Values); }
}

namespace NRound340ArrayBinding {
// A structured binding of an array of a parameter is of it.
template<class tf_CType> void fg_Run(tf_CType (&_Values)[2]) {
  auto Value = _Values[0];
  auto &[First, Second] = _Values;
  (void)Value;
  (void)First;
  (void)Second;
}
void fg_Test() {
  bool Values[2]{};
  fg_Run(Values);
}
}

namespace NRound340DecltypeReturn {
// What a function template gives back as `decltype` of its parameter, also
// deduced, is of what it is given.
template<class tf_CType> auto fg_Identity(tf_CType &_Value) -> decltype(_Value) { return _Value; }
template<class tf_CType> decltype(auto) fg_Forward(tf_CType &_Value) { return (_Value); }
template<class tf_CType> void fg_Run(tf_CType &_Value) {
  auto Copy = fg_Identity(_Value);
  auto Forwarded = fg_Forward(_Value);
  (void)Copy;
  (void)Forwarded;
}
void fg_Test() {
  bool bValue{};
  fg_Run(bValue);
}
}

namespace NRound340MovedBase {
// A member of what is moved is of what it is moved from.
template <typename tf_CType> tf_CType &&fg_Move(tf_CType &_Value);
template <typename t_CValue> struct TCMapNode { t_CValue &&f_Value() &&; };
template <typename tf_CValue> void fg_Extract(TCMapNode<tf_CValue> &_Node) {
  auto Value = fg_Move(_Node).f_Value();
  (void)Value;
}
void fg_Test(TCMapNode<bool> &_Node) { fg_Extract(_Node); }
}

namespace NRound340Overloads {
// Only the overloads a call can select tell what it gives.
template<typename t_CValue> struct TCMap {
  t_CValue &f_Get(int _Key);
  bool f_Get(int _Key, t_CValue &o_Value);
};
template<typename tf_CValue> void fg_Visit(TCMap<tf_CValue> &_Map) {
  auto &Value = _Map.f_Get(0);
  (void)Value;
}
void fg_Test(TCMap<int *> &_Map) { fg_Visit(_Map); }
}

namespace NRound340Partial {
// What a member of the partial specialization it is given gives.
template<typename t_CValue, bool t_bHasValue> struct TCResult { bool f_Value(); };
template<typename t_CValue> struct TCResult<t_CValue, true> { t_CValue &f_Value(); };
template<typename tf_CValue> void fg_Visit(TCResult<tf_CValue, true> &_Result) {
  auto Value = _Result.f_Value();
  (void)Value;
}
void fg_Test(TCResult<int *, true> &_Result) { fg_Visit(_Result); }
}

namespace NRound341ConditionalElement {
// An element of an iterator whose alias of it a trait chooses by what the
// iterator is given is what that chooses.
template<bool t_bSelect, class t_CFirst, class t_CSecond>
struct TCConditionalHelper { using CType = t_CFirst; };
template<class t_CFirst, class t_CSecond>
struct TCConditionalHelper<false, t_CFirst, t_CSecond> { using CType = t_CSecond; };
template<bool t_bSelect, class t_CFirst, class t_CSecond>
using TCConditional = typename TCConditionalHelper<t_bSelect, t_CFirst, t_CSecond>::CType;
template<class t_CMap, bool t_bValue> struct TCMapIterator {
  using CUserData = TCConditional<t_bValue, typename t_CMap::CUserData, typename t_CMap::CKey>;
  operator CUserData *() const;
  TCMapIterator &operator++();
};
template<class t_CValue> struct TCMap {
  using CUserData = t_CValue;
  using CKey = int;
  TCMapIterator<TCMap, true> f_GetIterator();
};
template<class tf_CValue> void fg_Run(TCMap<tf_CValue> &_Map) {
  auto iEntry = _Map.f_GetIterator();
  auto Value = *iEntry;
  (void)Value;
}
void fg_Use(TCMap<bool> &_Map) { fg_Run(_Map); }
}

namespace NRound341MemberPartial {
// A partial specialization of a member variable template takes no concept
// prefix that its instantiations would require.
struct CTraits { using CPointer = int *; };
template<typename t_CTraits> struct TCStorage {
  template<typename t_CValue>
  static inline typename t_CTraits::CPointer ms_Empty = nullptr;
  template<typename t_CValue>
  static inline typename t_CTraits::CPointer ms_Empty<t_CValue *> = nullptr;
};
int *fg_Test() { return TCStorage<CTraits>::ms_Empty<int *>; }
}

namespace NRound341SpelledHandle {
// A deduced value of the type of a handle is one also where what it is
// deduced from spells the smart pointer.
template<typename t_CType> struct TCUniquePointer {
  TCUniquePointer(TCUniquePointer const &) = delete;
};
struct CActorSubscriptionReference {};
using CActorSubscription = TCUniquePointer<CActorSubscriptionReference>;
void fg_Test(TCUniquePointer<CActorSubscriptionReference> &_pOriginal) {
  auto &Subscription = _pOriginal;
  (void)Subscription;
}
}

namespace NRound341BoundFunction {
// A structured binding of a parameter named as a function type takes no
// concept prefix that the instantiations would require.
struct CAction { void operator()() {} };
template<typename t_CValue> struct TCEntry { t_CValue m_Value; };
template<typename tf_FAction> void fg_Run(TCEntry<tf_FAction> &_Entry) {
  auto [Action] = _Entry;
  Action();
}
void fg_Test() {
  TCEntry<CAction> Entry;
  fg_Run(Entry);
}
}

namespace NRound341RemoveReference {
// What a function template gives back taking off what its parameter
// refers to is of what it is given.
template <typename t_CType> struct TCRemoveReferenceHelper { using CType = t_CType; };
template <typename t_CType> struct TCRemoveReferenceHelper<t_CType &> { using CType = t_CType; };
template <typename t_CType> using TCRemoveReference = typename TCRemoveReferenceHelper<t_CType>::CType;
template <typename tf_CFirst, typename tf_CSecond>
TCRemoveReference<tf_CFirst> fg_Min(tf_CFirst &&_First, tf_CSecond &&_Second) {
  return _First < _Second ? _First : _Second;
}
template <typename tf_CType> void fg_Test(tf_CType _First, tf_CType _Second) {
  auto Value = fg_Min(_First, _Second);
  (void)Value;
}
void fg_Use() { fg_Test(true, false); }
}

namespace NRound341DeducedLambda {
// What a lambda without a written return type gives is what it returns.
template <typename t_CType> struct TCVector { t_CType &operator[](int _iIndex); };
template <typename tf_CType> void fg_Test(TCVector<tf_CType> &_Values) {
  auto Direct = _Values[0];
  auto Value = [&] { return _Values[0]; }();
  (void)Direct;
  (void)Value;
}
void fg_Use(TCVector<bool> &_Values) { fg_Test(_Values); }
}

namespace NRound341WrapperCopy {
// A copy of a specialization given a parameter whose concept follows it is
// generic.
template <typename t_CType> struct TCAutoClear { t_CType m_Value; };
template <typename tf_CType> void fg_Test(TCAutoClear<tf_CType> const &_Value) {
  TCAutoClear<tf_CType> Explicit = _Value;
  auto Copy = _Value;
  (void)Explicit;
  (void)Copy;
}
void fg_Use(TCAutoClear<bool> const &_bValue) { fg_Test(_bValue); }
}

namespace NRound343DependentLambda {
// A lambda declared in a template is a function object whatever the arguments
// are, also capturing what depends on them.
template <typename tf_CInteger> void fg_Test() {
  tf_CInteger Number = 0x50;
  unsigned Result = 0;
  auto Lambda = [&]() { Result += Number; };
  // CHECK-MESSAGES: :[[@LINE-1]]:8: warning: 'Lambda' is a function object and should have the 'f' concept prefix [malterlib-conceptual-prefix]
  auto fLambda = [&]() { Result += Number; };
  Lambda();
  fLambda();
}
void fg_Use() { fg_Test<unsigned>(); }
}

namespace NRound344InstantiatedPointer {
// A prefix of another concept than an instantiation makes it, a pointer of
// `f_GetArray()`, is wrong, as where it doesn't depend on parameters, but `i`,
// which generic code walks a pointer as; no prefix is always allowed.
template <typename t_CData, typename t_CAllocator, typename t_COptions> struct TCVector {
  t_CData const *f_GetArray() const;
};
template <typename tf_CData, typename tf_CAllocator, typename tf_COptions>
void fg_Test(TCVector<tf_CData, tf_CAllocator, tf_COptions> const &_Other) {
  auto iOtherArray = _Other.f_GetArray();
  auto fSource = _Other.f_GetArray();
  // CHECK-MESSAGES: :[[@LINE-1]]:8: warning: 'fSource' has the 'f' concept prefix of a function object, but is not one [malterlib-conceptual-prefix]
  auto pOtherArray = _Other.f_GetArray();
  auto OtherArray = _Other.f_GetArray();
  (void)iOtherArray;
  (void)fSource;
  (void)pOtherArray;
  (void)OtherArray;
}
struct CAllocator {};
struct COptions {};
void fg_Use(TCVector<int, CAllocator, COptions> const &_Values) { fg_Test(_Values); }
}

namespace NRound345PlainParameter {
// A parameter of a template parameter as it is takes no prefix that each
// instantiation makes wrong, like `p` on a pointer to a member function, which
// is a function object; no prefix is always allowed.
struct CDummy { virtual void f_Dummy(); };
template <typename tf_CMemberFunctionPointer>
bool fg_IsVirtualCall(tf_CMemberFunctionPointer _pFunctionPointer) {
// CHECK-MESSAGES: :[[@LINE-1]]:49: warning: '_pFunctionPointer' has the 'p' concept prefix of a pointer, but is not one [malterlib-conceptual-prefix]
  return _pFunctionPointer != nullptr;
}
template <typename tf_CMemberFunctionPointer>
bool fg_IsVirtualCallGeneric(tf_CMemberFunctionPointer _FunctionPointer) {
  return _FunctionPointer != nullptr;
}
template <typename tf_CValue>
bool fg_IsSet(tf_CValue const &_pValue) {
  return _pValue != nullptr;
}
void fg_Use(int *_pValue) {
  (void)fg_IsVirtualCall(&CDummy::f_Dummy);
  (void)fg_IsVirtualCallGeneric(&CDummy::f_Dummy);
  (void)fg_IsSet(_pValue);
}
}

namespace NRound346PlainMember {
// So is a member of a template parameter as it is, and a non-type template
// parameter of one.
struct CDummy { virtual void f_Dummy(); };
template <typename t_CFunctionMemberPointer> struct TCBinder {
  t_CFunctionMemberPointer mp_pFunction;
  // CHECK-MESSAGES: :[[@LINE-1]]:28: warning: 'mp_pFunction' has the 'p' concept prefix of a pointer, but is not one [malterlib-conceptual-prefix]
  t_CFunctionMemberPointer mp_Function;
};
template <typename t_CType, t_CType t_pValue> struct TCConstant {};
// CHECK-MESSAGES: :[[@LINE-1]]:37: warning: 't_pValue' has the 'p' concept prefix of a pointer, but is not one [malterlib-conceptual-prefix]
TCBinder<void (CDummy::*)()> g_Binder;
TCConstant<int, 1> g_Constant;
}

namespace NRound348Atomic {
// So is one that holds what is given, `TCAtomic<tf_CType>`, which is generic
// but takes no prefix each instantiation excludes.
template <typename t_CType> struct TCAtomic { t_CType m_Value; };
template <typename tf_CType> void fg_Probe() {
  TCAtomic<tf_CType> fAtomic;
  // CHECK-MESSAGES: :[[@LINE-1]]:22: warning: 'fAtomic' has the 'f' concept prefix of a function object, but is not one [malterlib-conceptual-prefix]
  TCAtomic<tf_CType> Atomic;
  (void)fAtomic;
  (void)Atomic;
}
void fg_Use() { fg_Probe<int>(); }
}

namespace NRound349AnyInstantiation {
// A prefix wrong in one instantiation is wrong, as it claims the concept of
// every use; no prefix is always allowed. But `i` is allowed on what an
// instantiation makes a pointer, which generic code walks like any iterator.
struct CIterator { int &operator*(); CIterator &operator++(); bool operator!=(CIterator const &) const; };
template <typename tf_CType> bool fg_IsSet(tf_CType const &_pValue) {
  // CHECK-MESSAGES: :[[@LINE-1]]:60: warning: '_pValue' has the 'p' concept prefix of a pointer, but is not one [malterlib-conceptual-prefix]
  return !!_pValue;
}
template <typename tf_CType> bool fg_IsSetGeneric(tf_CType const &_Value) { return !!_Value; }
template <typename tf_CIterator> int fg_Count(tf_CIterator _iBegin, tf_CIterator _iEnd) {
  int Count = 0;
  for (; _iBegin != _iEnd; ++_iBegin)
    ++Count;
  return Count;
}
template <typename tf_CType> void fg_Deduced(tf_CType _Value) {
  auto pCopy = _Value;
  // CHECK-MESSAGES: :[[@LINE-1]]:8: warning: 'pCopy' has the 'p' concept prefix of a pointer, but is not one [malterlib-conceptual-prefix]
  (void)pCopy;
}
void fg_Use(int *_pValue, int _Value, CIterator _iBegin) {
  (void)fg_IsSet(_pValue);
  (void)fg_IsSet(_Value);
  (void)fg_IsSetGeneric(_pValue);
  (void)fg_IsSetGeneric(_Value);
  (void)fg_Count(_pValue, _pValue + 1);
  (void)fg_Count(_iBegin, _iBegin);
  fg_Deduced(_pValue);
  fg_Deduced(_Value);
}
}

namespace NRound349BuiltinClass {
// A built-in type of Malterlib that is a class, `fp64`, is a number, also a
// floating point one.
template <int t_Bits> struct TCFloat { double m_Value; TCFloat(double _Value); };
using fp64 = TCFloat<64>;
void fg_Test() {
  fp64 bRunTime = 0.0;
  // CHECK-MESSAGES: :[[@LINE-1]]:8: warning: 'bRunTime' has the 'b' concept prefix of a boolean, but is not one [malterlib-conceptual-prefix]
  fp64 pRunTime = 0.0;
  // CHECK-MESSAGES: :[[@LINE-1]]:8: warning: 'pRunTime' has the 'p' concept prefix of a pointer, but is not one [malterlib-conceptual-prefix]
  fp64 fRunTime = 0.0;
  // CHECK-MESSAGES: :[[@LINE-1]]:8: warning: 'fRunTime' has the 'f' concept prefix of a function object, but is not one [malterlib-conceptual-prefix]
  fp64 RunTime = 0.0;
  (void)bRunTime;
  (void)pRunTime;
  (void)fRunTime;
  (void)RunTime;
}
}

namespace NRound349ClassConcepts {
// A boolean is an integer, a boolean or a wrapper of one, ValueWrapperTypes;
// an iterator is one of IteratorTypes, a pointer, in generic code, or an
// index; a pointer is a pointer or one of SmartPointerTypes. A class is none
// of them otherwise; `n` and `r` can be any.
struct CTesting {};
template <typename t_CType> struct TCAtomic { t_CType m_Value; };
enum EFlags { EFlags_None };
CTesting g_bTesting;
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: 'g_bTesting' has the 'b' concept prefix of a boolean, but is not one [malterlib-conceptual-prefix]
CTesting g_pTesting;
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: 'g_pTesting' has the 'p' concept prefix of a pointer, but is not one [malterlib-conceptual-prefix]
CTesting g_iTesting;
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: 'g_iTesting' has the 'i' concept prefix of an iterator, but is not one [malterlib-conceptual-prefix]
CTesting g_nTesting;
CTesting g_rTesting;
double g_iRatio;
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: 'g_iRatio' has the 'i' concept prefix of an iterator, but is not one [malterlib-conceptual-prefix]
TCAtomic<CTesting> g_bWrapped;
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: 'g_bWrapped' has the 'b' concept prefix of a boolean, but is not one [malterlib-conceptual-prefix]
int g_bFlags;
EFlags g_bEnumFlags;
TCAtomic<unsigned> g_bAtomicFlags;
int g_iIndex;
TCAtomic<unsigned> g_iAtomicIndex;
}

namespace NRound349ListedConcepts {
// A class named as a pointer, `*Pointer`, that has the operators of one or
// tells whether it points to anything, also through an alias named so, is a
// smart pointer; a cursor, `*Cursor*`, with the operators of an iterator is
// an iterator; a flag, FlagTypes, can be named as a boolean.
template <typename t_CPointer> struct TCSSLPointer {
  t_CPointer operator->() const;
  explicit operator bool() const;
};
struct CExceptionPtr {
  explicit operator bool() const;
};
using CExceptionPointer = CExceptionPtr;
struct CNamedPointer {};
struct CDatabaseCursor {
  CDatabaseCursor &operator++();
  explicit operator bool() const;
};
struct CAtomicFlag {
  bool f_Test() const;
};
struct CKey;
TCSSLPointer<CKey *> g_pKey;
TCSSLPointer<CKey *> g_Key;
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: 'g_Key' is a pointer and should have the 'p' concept prefix after its storage prefix [malterlib-conceptual-prefix]
CExceptionPointer g_pException;
CExceptionPtr g_pPlain;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'g_pPlain' has the 'p' concept prefix of a pointer, but is not one [malterlib-conceptual-prefix]
CNamedPointer g_pNamed;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'g_pNamed' has the 'p' concept prefix of a pointer, but is not one [malterlib-conceptual-prefix]
CDatabaseCursor g_iCursor;
CAtomicFlag g_bReplied;
CAtomicFlag g_Replied;
}

namespace NRound349NoUniqueAddress {
// A member that takes no space where it is empty, `[[no_unique_address]]`,
// can be an empty stand-in in an instantiation of generic code, whatever its
// concept prefix; not one that takes space.
template <bool t_bCondition, typename t_CTrue, typename t_CFalse> struct TCConditionalImp { using CType = t_CTrue; };
template <typename t_CTrue, typename t_CFalse> struct TCConditionalImp<false, t_CTrue, t_CFalse> { using CType = t_CFalse; };
template <bool t_bCondition, typename t_CTrue, typename t_CFalse>
using TCConditional = typename TCConditionalImp<t_bCondition, t_CTrue, t_CFalse>::CType;
struct CStore {};
struct CStoreEmpty {};
template <bool t_bStore> struct TCSlab {
  [[no_unique_address]] TCConditional<t_bStore, CStore *, CStoreEmpty> m_pStore = {};
  TCConditional<t_bStore, CStore *, CStoreEmpty> m_pOther = {};
  // CHECK-MESSAGES: :[[@LINE-1]]:50: warning: 'm_pOther' has the 'p' concept prefix of a pointer, but is not one [malterlib-conceptual-prefix]
};
TCSlab<true> g_Stored;
TCSlab<false> g_Unstored;
}

namespace NRound350 {
// So is a variable template of a parameter as it is, `t_CType gc_Max`.
template <typename t_CType, t_CType... tp_Values>
inline constexpr t_CType gc_pConstantMax = t_CType(0);
// CHECK-MESSAGES: :[[@LINE-1]]:26: warning: 'gc_pConstantMax' has the 'p' concept prefix of a pointer, but is not one [malterlib-conceptual-prefix]
template <typename t_CType, t_CType... tp_Values>
inline constexpr t_CType gc_ConstantMax = t_CType(0);
static_assert(gc_pConstantMax<unsigned, 1u, 2u> == 0);
static_assert(gc_ConstantMax<unsigned, 1u, 2u> == 0);
}

namespace std {
struct exception_ptr {
  explicit operator bool() const;
};
} // namespace std

namespace NRound350ExceptionPointer {
// A listed smart pointer as it is, also where an alias of it is lost, like
// through a function object's call, `std::exception_ptr` of
// `CExceptionPointer`.
template <typename t_CResult> struct TCCaller {
  t_CResult operator()() const;
};
struct CTransformer : TCCaller<std::exception_ptr> {};
void fg_Use(CTransformer const &_fTransformer) {
  auto pException = _fTransformer();
  (void)pException;
}
}

namespace NRound351Forwarded {
// So is a parameter of what forwards a parameter, `TCRemoveReference<tf_CType>
// &_Value`, of what each instantiation makes it.
template <typename t_CType> struct TCRemoveReferenceImp { using CType = t_CType; };
template <typename t_CType> struct TCRemoveReferenceImp<t_CType &> { using CType = t_CType; };
template <typename t_CType> using TCRemoveReference = typename TCRemoveReferenceImp<t_CType>::CType;
template <typename tf_CType>
constexpr tf_CType &&fg_ForwardChecked(TCRemoveReference<tf_CType> &_pValue) noexcept {
  // CHECK-MESSAGES: :[[@LINE-1]]:69: warning: '_pValue' has the 'p' concept prefix of a pointer, but is not one [malterlib-conceptual-prefix]
  return static_cast<tf_CType &&>(_pValue);
}
template <typename tf_CType>
constexpr tf_CType &&fg_Forward(TCRemoveReference<tf_CType> &_Value) noexcept {
  return static_cast<tf_CType &&>(_Value);
}
void fg_Test() {
  int Value = 42;
  fg_ForwardChecked<int &>(Value);
  fg_Forward<int &>(Value);
}
}

namespace NRound352Floating {
// A built-in floating point type is one also unsigned or cleared, `ufp64`,
// `zfp64` or `zufp64`.
template <int t_Bits, bool t_bUnsigned> struct TCFloatType { double m_Value; TCFloatType(double _Value); };
template <typename t_CType> struct TCAutoClear { t_CType m_Value; TCAutoClear(double _Value); };
using ufp64 = TCFloatType<64, true>;
using zfp64 = TCAutoClear<TCFloatType<64, false>>;
struct CExample {
  zfp64 m_bLastUpdate = 0.0;
  // CHECK-MESSAGES: :[[@LINE-1]]:9: warning: 'm_bLastUpdate' has the 'b' concept prefix of a boolean, but is not one [malterlib-conceptual-prefix]
  zfp64 m_iLastUpdate = 0.0;
  // CHECK-MESSAGES: :[[@LINE-1]]:9: warning: 'm_iLastUpdate' has the 'i' concept prefix of an iterator, but is not one [malterlib-conceptual-prefix]
  ufp64 m_LastUpdate = 0.0;
};
void fg_Test() {
  ufp64 bInfinity = 0.0;
  // CHECK-MESSAGES: :[[@LINE-1]]:9: warning: 'bInfinity' has the 'b' concept prefix of a boolean, but is not one [malterlib-conceptual-prefix]
  (void)bInfinity;
}
}

// A deduced local of pointer arithmetic in a template, whose type is
// dependent there but which steps the pointer it is given, is a pointer
// whatever the arguments are.
namespace NRound378 {
using umint = unsigned long;
template <typename tf_CData>
void fg_WalkBack(tf_CData *_pDest, umint _Len) {
  auto End = _pDest - 1;
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: 'End' is a pointer and should have the 'p' concept prefix [malterlib-conceptual-prefix]
  for (auto Dest = _pDest + (_Len - 1); Dest != End; --Dest)
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'Dest' is a pointer and should have the 'p' concept prefix [malterlib-conceptual-prefix]
    (void)*Dest;
  auto pFirst = 1 + _pDest;
  auto nCount = _pDest - _pDest;
}
void fg_Test(int *_pValues) { fg_WalkBack(_pValues, 2); }
}
