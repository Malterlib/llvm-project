// RUN: %check_clang_tidy -std=c++20-or-later %s malterlib-type-naming %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}"
// A size is a size_t of the target, which wraps at 32 bits on a 32-bit one.
// RUN: printf 'template <typename t_CType, auto t_Value>\nstruct TCChoice {};\ntemplate <typename t_CType, auto t_Value>\n\trequires ((sizeof(t_CType) + t_Value) == 0)\nstruct TCChoice<t_CType, t_Value> { void operator()(); };\nusing FCallback = TCChoice<int, 0xfffffffcU>;\nFCallback g_Callback;\n' > %t.32.cpp
// RUN: clang-tidy --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" --checks='-*,malterlib-type-naming' %t.32.cpp -- -std=c++20 -target i386-unknown-linux-gnu 2>&1 | FileCheck --check-prefix=CHECK-32 --allow-empty %s
// CHECK-32-NOT: warning:
// RUN: clang-tidy --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" --checks='-*,malterlib-type-naming' %t.32.cpp -- -std=c++20 -target x86_64-unknown-linux-gnu 2>&1 | FileCheck --check-prefix=CHECK-64 %s
// CHECK-64: warning: type alias 'FCallback' has the 'F' prefix of function types, but is not a function type or a function object [malterlib-type-naming]

namespace NNamespace
{
	struct CStruct;
	struct CStruct
	{
		struct CNested;
	};

	struct ICInterface
	{
		virtual void f_Function() = 0;
	};

	template <typename t_CType>
	struct TCTemplate
	{
		struct CNested;
		// Only a template is named as one, also nested in another.
		struct TCNestedInTemplate;
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: class 'TCNestedInTemplate' should have the 'C' or 'IC' prefix followed by upper camel case [malterlib-type-naming]
	};

	template <typename t_CType>
	struct TICInterface;

	template <typename t_CType>
	struct TCTemplate<t_CType *>;

	template <>
	struct TCTemplate<int>;

	enum EEnum
	{
		EEnum_Value
		, EEnum_Other
	};

	enum class EScoped
	{
		mc_Value
		, mc_bOther
	};

	typedef enum
	{
		ETypedef_Value
	} ETypedef;

	using CAlias = int;
	using FFunction = void (int);
	using EAlias = EEnum;
	typedef int CTypedef;

	template <typename t_CType>
	using TCAliasTemplate = TCTemplate<t_CType>;

	template <typename t_CType>
	using TFFunctionTemplate = t_CType (int);

	template <typename t_CType>
	concept cConcept = true;

	// Lower case names emulate language features.
	using uint32 = unsigned int;
}

namespace Namespace
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: namespace 'Namespace' should have the 'N' prefix followed by upper camel case [malterlib-type-naming]
{
	struct Struct;
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: class 'Struct' should have the 'C' or 'IC' prefix followed by upper camel case [malterlib-type-naming]
	struct Cstruct
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: class 'Cstruct' should have the 'C' or 'IC' prefix followed by upper camel case [malterlib-type-naming]
	{
	};
	union UUnion
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: union 'UUnion' should have the 'C' or 'IC' prefix followed by upper camel case [malterlib-type-naming]
	{
	};

	template <typename t_CType>
	struct CTemplate;
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: class template 'CTemplate' should have the 'TC' or 'TIC' prefix followed by upper camel case [malterlib-type-naming]

	enum Enum
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: enum 'Enum' should have the 'E' prefix followed by upper camel case [malterlib-type-naming]
	{
		Value
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: enumerator 'Value' should have the name of its enum followed by an underscore, 'Enum_', and upper camel case [malterlib-type-naming]
	};

	// An enum's name has no underscore, which separates its enumerators' names
	// from it, and they have its name.
	enum EMode_Type
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: enum 'EMode_Type' should have the 'E' prefix followed by upper camel case without underscores, which separate the names of its enumerators from its own [malterlib-type-naming]
	{
		EMode_Type_Value
	};

	enum EFormat
	{
		EFormat_Html
		, EHtml
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: enumerator 'EHtml' should have the name of its enum followed by an underscore, 'EFormat_', and upper camel case [malterlib-type-naming]
		, EOther_Value
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: enumerator 'EOther_Value' should have the name of its enum followed by an underscore, 'EFormat_', and upper camel case [malterlib-type-naming]
		, EFormat_lower
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: enumerator 'EFormat_lower' should have the name of its enum followed by an underscore, 'EFormat_', and upper camel case [malterlib-type-naming]
		, EFormat_8Bit
		, EFormat_Group_Value
	};

	typedef enum
	{
		ENamed_Value
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: enumerator 'ENamed_Value' should have the name of its enum followed by an underscore, 'ETypedefWrong_', and upper camel case [malterlib-type-naming]
	} ETypedefWrong;

	enum class EScoped
	{
		EScoped_Value
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: enumerator 'EScoped_Value' of a scoped enum should have the 'mc_' prefix followed by upper camel case [malterlib-type-naming]
	};

	// An unnamed enum is what constants were before `constexpr`, and its
	// enumerators belong in a named enum or are constexpr variables.
	enum
	{
		gc_Global
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: enumerator 'gc_Global' of an unnamed enum is a constant; declare it as a 'constexpr' variable instead [malterlib-type-naming]
		, EUnnamed_Value
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: enumerator 'EUnnamed_Value' of an unnamed enum should be in a named enum, or be a 'constexpr' variable [malterlib-type-naming]
	};

	struct CRecord
	{
		enum : unsigned
		{
			mc_InRecord
// CHECK-MESSAGES: :[[@LINE-1]]:4: warning: enumerator 'mc_InRecord' of an unnamed enum is a constant; declare it as a 'constexpr' variable instead [malterlib-type-naming]
			, ENumBits = 32
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: enumerator 'ENumBits' of an unnamed enum should be in a named enum, or be a 'constexpr' variable [malterlib-type-naming]
		};
	private:
		enum
		{
			mcp_PrivateInRecord
// CHECK-MESSAGES: :[[@LINE-1]]:4: warning: enumerator 'mcp_PrivateInRecord' of an unnamed enum is a constant; declare it as a 'constexpr' variable instead [malterlib-type-naming]
		};
	};

	using Alias = int;
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: type alias 'Alias' should have the 'C', 'IC', 'F' or 'E' prefix followed by upper camel case [malterlib-type-naming]

	template <typename t_CType>
	using CAliasTemplate = t_CType;
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: alias template 'CAliasTemplate' should have the 'TC', 'TIC', 'TF' or 'E' prefix followed by upper camel case [malterlib-type-naming]

	template <typename t_CType>
	concept Concept = true;
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: concept 'Concept' should have the 'c' prefix followed by upper camel case [malterlib-type-naming]
}

namespace NNamespace
{
	// Reopened namespaces are checked where they are first declared.
}

namespace
{
	struct CAnonymous;
}

void fg_Function()
{
	auto fLambda = []
		{
		}
	;
	struct CLocal
	{
	};
}

namespace std
{
	template <typename t_CType>
	struct hash;
}

namespace NNamespace
{
	struct CHashed;
}

template <>
struct std::hash<NNamespace::CHashed>
{
};

struct NoLint; // NOLINT(malterlib-type-naming)

// `F` is for function types.
using FCount = int;
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: type alias 'FCount' has the 'F' prefix of function types, but is not a function type or a function object [malterlib-type-naming]
using CCallback = void ();
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: function type alias 'CCallback' should have the 'F' prefix followed by upper camel case [malterlib-type-naming]
using FCallback = void ();
using FCallbackPointer = void (*)();

// Namespace aliases are named like namespaces.
namespace NGood
{
}
namespace Wrong = NGood;
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: namespace alias 'Wrong' should have the 'N' prefix followed by upper camel case [malterlib-type-naming]
namespace NAlias = NGood;

struct CGood_bad
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: class 'CGood_bad' should have the 'C' or 'IC' prefix followed by upper camel case [malterlib-type-naming]
{
};

// Lower case names are the built-in types and the protocols of the language
// and the standard library, not legacy spellings or other types.
using umint = unsigned long;
using zuint32 = unsigned int;
using mint = long;
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: type alias 'mint' should have the 'C', 'IC', 'F' or 'E' prefix followed by upper camel case [malterlib-type-naming]
struct widget
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: class 'widget' should have the 'C' or 'IC' prefix followed by upper camel case [malterlib-type-naming]
{
	using value_type = int;
};
namespace details
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: namespace 'details' should have the 'N' prefix followed by upper camel case [malterlib-type-naming]
{
}
template <typename t_CType>
concept comparable = true;
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: concept 'comparable' should have the 'c' prefix followed by upper camel case [malterlib-type-naming]

// The built-in types of Malterlib, as it defines them.
using pfp32 = float;
using int96 = long;
using zuint96 = unsigned long;
using zbint = bool;
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: type alias 'zbint' should have the 'C', 'IC', 'F' or 'E' prefix followed by upper camel case [malterlib-type-naming]

// `F` is for function objects and member function pointers too.
template <typename t_CSignature>
struct TCFunctionObject
{
	void operator()();
};
using FFunctionObject = TCFunctionObject<void ()>;
struct CWidget
{
	void f_Run();
};
using FMember = void (CWidget::*)();

// The protocol of an allocator.
struct CAllocator
{
	using value_type = int;
	template <typename t_CType>
	struct rebind
	{
		using other = CAllocator;
	};
};

// Lower case names are of classes and aliases.
namespace iterator
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: namespace 'iterator' should have the 'N' prefix followed by upper camel case [malterlib-type-naming]
{
}
template <typename t_CType>
concept value_type = true;
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: concept 'value_type' should have the 'c' prefix followed by upper camel case [malterlib-type-naming]

// `E` is for enums.
using ECount = int;
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: type alias 'ECount' has the 'E' prefix of enums, but is not an enum [malterlib-type-naming]
using EOther = NNamespace::EEnum;

// A name doesn't end with an underscore.
struct CWidget_
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: class 'CWidget_' should have the 'C' or 'IC' prefix followed by upper camel case [malterlib-type-naming]
{
};

// `TF` is for function types too.
template <typename t_CType>
using TFCount = int;
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: type alias 'TFCount' has the 'F' prefix of function types, but is not a function type or a function object [malterlib-type-naming]

// An enum defined in a system header is checked where it is defined.
# 1 "external.hpp" 1 3
enum class external_enum : int;
enum class external_enum : int
{
	value
};
# 1 "type-naming.cpp" 2

// Partial specializations that share a base with a call operator.
struct CCallableBase
{
	void operator()();
};
template <typename t_CType>
struct TCPartiallyCallable;
template <typename t_CType>
struct TCPartiallyCallable<t_CType *> : CCallableBase
{
};
template <typename t_CType>
struct TCPartiallyCallable<t_CType &> : CCallableBase
{
};
using FPartiallyCallable = TCPartiallyCallable<int *>;

// The iterator protocol name of C++20.
struct CProtocolIterator
{
	using iterator_concept = int;
};

// The call operator of a base of a templated base, with the arguments given on.
struct CCallableInner { void operator()(); };
template <typename t_CBase>
struct TCInnerBase : t_CBase {};
template <typename t_CBase>
struct TCOuterBase : TCInnerBase<t_CBase> {};
using FWrappedInner = TCOuterBase<CCallableInner>;

// The qualifiers of a member function, a value computed from a parameter,
// and constraints that choose between partial specializations.
struct CQualifiedOwner
{
	void f_Run();
};
template <typename t_CType>
struct TCQualifiedSelect
{
	void operator()();
};
template <typename t_COwner>
struct TCQualifiedSelect<void (t_COwner::*)()> {};
using FConstMember = TCQualifiedSelect<void (CQualifiedOwner::*)() const>;
template <typename t_CType, int t_Size>
struct TCSizeChoice
{
	void operator()();
};
template <typename t_CType>
struct TCSizeChoice<t_CType, sizeof(t_CType)> {};
using FSizeMismatch = TCSizeChoice<char, 2>;
template <typename t_CType>
struct TCTwoConstrained {};
template <typename t_CType>
	requires (sizeof(t_CType) > 1)
struct TCTwoConstrained<t_CType> {};
template <typename t_CType>
	requires (sizeof(t_CType) == 1)
struct TCTwoConstrained<t_CType>
{
	void operator()();
};
using FSecondConstraint = TCTwoConstrained<char>;

// A dependent function type, constraints whose order can't be told, a pack
// that a partial specialization deduces, and a base that is a member type of a
// parameter.
template <typename t_CResult>
using TCCallbackType = t_CResult ();
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: function type alias 'TCCallbackType' {{.*}}
template <typename t_CType>
concept cSmallType = sizeof(t_CType) == 1;
template <typename t_CType>
struct TCSubsumed {};
template <typename t_CType>
	requires cSmallType<t_CType>
struct TCSubsumed<t_CType> {};
template <typename t_CType>
	requires (cSmallType<t_CType> && sizeof(t_CType) < 2)
struct TCSubsumed<t_CType>
{
	void operator()();
};
using FSubsumed = TCSubsumed<char>;
struct CCallableFromPack { void operator()(); };
template <typename ...tp_CTypes>
struct TCTypeTuple {};
template <typename t_CType>
struct TCTupleWrapper {};
template <typename ...tp_CBases>
struct TCTupleWrapper<TCTypeTuple<tp_CBases...>> : tp_CBases... {};
using FFromTuple = TCTupleWrapper<TCTypeTuple<CCallableFromPack>>;
struct CBaseOptions
{
	using CBase = CCallableFromPack;
};
template <typename t_COptions>
struct TCOptionsBase : t_COptions::CBase {};
using FFromOptions = TCOptionsBase<CBaseOptions>;

// A specialization pattern doesn't match what isn't a specialization, packs
// match element by element, a template base selects its specialization, and a
// member type of a parameter is given on as an argument.
template <typename t_CType>
struct TCPatternBox {};
template <typename t_CType>
struct TCPatternChoice
{
	void operator()();
};
template <typename t_CType>
struct TCPatternChoice<TCPatternBox<t_CType>> {};
using FPatternPrimary = TCPatternChoice<int>;
template <typename ...tp_CTypes>
struct TCPackPatternChoice
{
	void operator()();
};
template <typename ...tp_CTypes>
struct TCPackPatternChoice<tp_CTypes *...> {};
using FPackPatternPrimary = TCPackPatternChoice<int>;
template <typename t_CType>
struct TCSelectedBase {};
template <typename t_CType>
struct TCSelectedBase<t_CType *>
{
	void operator()();
};
template <typename t_CType>
struct TCSelectingWrapper : TCSelectedBase<t_CType> {};
using FSelectedBase = TCSelectingWrapper<int *>;
template <typename t_CBase>
struct TCMemberInner : t_CBase {};
template <typename t_COptions>
struct TCMemberOuter : TCMemberInner<typename t_COptions::CBase> {};
using FMemberOuter = TCMemberOuter<CBaseOptions>;

// The elements of a pack, and the address of a declaration.
template <typename ...tp_CTypes>
struct TCVariadicChoice
{
	void operator()();
};
template <typename t_CType>
struct TCVariadicChoice<t_CType, int> {};
using FVariadicPrimary = TCVariadicChoice<double, float>;
int g_FirstAddress;
int g_SecondAddress;
template <typename t_CType, int *t_pValue>
struct TCAddressChoice
{
	void operator()();
};
template <typename t_CType>
struct TCAddressChoice<t_CType, &g_FirstAddress> {};
using FAddressPrimary = TCAddressChoice<int, &g_SecondAddress>;

// A base made of a parameter selects a partial specialization, and `nullptr`
// is another value than an address.
template <typename t_CType>
struct TCRound51Box
{
};
template <typename t_CType>
struct TCRound51Box<t_CType *>
{
	void operator ()();
};
template <typename t_CType>
struct TCRound51Derived : TCRound51Box<t_CType *>
{
};
using FRound51Callback = TCRound51Derived<int>;

int g_Round51Value;
template <typename t_CType, int *t_pAddress>
struct TCRound51Fixed
{
	void operator ()();
};
template <typename t_CType>
struct TCRound51Fixed<t_CType, nullptr>
{
};
using FRound51Fixed = TCRound51Fixed<int, &g_Round51Value>;

// A nested class of an argument as a base, and a negative value compared with
// a size, which C++ converts, can't be told.
struct CRound53Options
{
	struct CBase
	{
		void operator ()();
	};
};
template <typename t_COptions>
struct TCRound53Wrapper : t_COptions::CBase
{
};
using FRound53Callback = TCRound53Wrapper<CRound53Options>;

template <typename t_CType>
struct TCRound53Select
{
};
template <typename t_CType>
	requires (sizeof(t_CType) < -1)
struct TCRound53Select<t_CType>
{
	void operator ()();
};
using FRound53Selected = TCRound53Select<int>;

// Also in a function.
void fg_Round54Local()
{
	enum
	{
		c_Local = 1
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: enumerator 'c_Local' of an unnamed enum is a constant; declare it as a 'constexpr' variable instead [malterlib-type-naming]
	};
}

// A base made of a value parameter, and a member template of a specialization
// that isn't instantiated yet.
template <typename t_CType, int t_Value>
struct TCRound54Base
{
};
template <typename t_CType>
struct TCRound54Base<t_CType, 1>
{
	void operator ()();
};
template <int t_Value>
struct TCRound54Wrapper : TCRound54Base<int, t_Value>
{
};
using FRound54Callback = TCRound54Wrapper<1>;

struct CRound54Callable
{
	void operator ()();
};
struct CRound54Plain
{
};
template <typename t_COuter>
struct TCRound54Outer
{
	template <typename t_CInner>
	struct TCInner : t_COuter
	{
	};
};
using FRound54Nested = TCRound54Outer<CRound54Callable>::TCInner<CRound54Plain>;

// A base that is a member type of a specialization made of the parameters.
struct CRound55Callable
{
	void operator()();
};
template <typename t_CType>
struct TCRound55Outer
{
	using CBase = t_CType;
};
template <typename t_CType>
struct TCRound55Forward : TCRound55Outer<t_CType>::CBase
{
};
using FRound55Forward = TCRound55Forward<CRound55Callable>;

// A partial specialization whose array bound doesn't match the value given
// for it isn't selected.
template <typename t_CType, int t_Count>
struct TCRound55Choice
{
	void operator()();
};
template <typename t_CType, int t_Count>
struct TCRound55Choice<t_CType[t_Count], t_Count>
{
};
using FRound55Choice = TCRound55Choice<int[3], 4>;
using CRound55Choice = TCRound55Choice<int[3], 3>;

// A partial specialization deduces a value parameter and a pack that its
// bases are made of.
template <int t_Count>
struct TCRound56Base
{
};
template <>
struct TCRound56Base<1>
{
	void operator()();
};
template <typename t_CType, int t_Count>
struct TCRound56Wrapper
{
};
template <typename t_CType, int t_Count>
struct TCRound56Wrapper<t_CType *, t_Count> : TCRound56Base<t_Count>
{
};
using FRound56Value = TCRound56Wrapper<int *, 1>;

struct CRound56Callable
{
	void operator()();
};
struct CRound56Tag
{
};
template <typename ...tp_CTypes>
struct TCRound56Pack
{
};
template <typename ...tp_CBases>
struct TCRound56Pack<CRound56Tag, tp_CBases...> : tp_CBases...
{
};
using FRound56Pack = TCRound56Pack<CRound56Tag, CRound56Callable>;

// A template template parameter, a nested pack with more elements and a pack
// of function parameters.
template <typename t_CType>
struct TCRound57Slot
{
};
template <typename t_CType>
struct TCRound57Choice
{
};
template <template <typename> class t_TCTemplate, typename t_CType>
struct TCRound57Choice<t_TCTemplate<t_CType>>
{
	void operator()();
};
using FRound57Template = TCRound57Choice<TCRound57Slot<int>>;

struct CRound57Callable
{
	void operator()();
};
template <typename t_CSignature>
struct TCRound57Wrapper
{
};
template <typename t_CResult, typename ...tp_CArgs>
struct TCRound57Wrapper<t_CResult (tp_CArgs ...p_Args)> : tp_CArgs...
{
};
using FRound57Wrapper = TCRound57Wrapper<void (CRound57Callable _Callable)>;

// A class that declares a call operator is meant to be a function object,
// whatever the operator is, also one brought in by `using` that is deleted.
struct CRound73DeletedCall
{
	void operator()() = delete;
};
struct CRound73FunctorDeleted : CRound73DeletedCall
{
	using CRound73DeletedCall::operator();
};
using FRound73Callback = CRound73FunctorDeleted;

// Also with a deleted call operator that hides that of a base.
struct CRound74Call
{
	void operator()();
};
struct CRound74DisabledFunctor : CRound74Call
{
	void operator()() = delete;
};
using FRound74Callback = CRound74DisabledFunctor;

// A default argument that is an earlier parameter, `t_CBase = t_CType`, is
// what that one is given.
struct CRound78Callable
{
	void operator()();
};
template <typename t_CType, typename t_CBase = t_CType>
struct TCRound78Base : t_CBase
{
};
template <typename t_CType>
struct TCRound78Wrapper : TCRound78Base<t_CType>
{
};
using FRound78Callback = TCRound78Wrapper<CRound78Callable>;
FRound78Callback g_Round78Callback;
struct CRound90NotCallable
{
};
using FRound90NotCallable = TCRound78Wrapper<CRound90NotCallable>;
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: type alias 'FRound90NotCallable' has the 'F' prefix of function types, but is not a function type or a function object [malterlib-type-naming]
FRound90NotCallable g_Round90NotCallable;

// The result of `!` is a bool, promoted to an int in arithmetic.
template <auto t_Value, int t_Tag>
struct TCRound80Choice
{
};
template <auto t_Value>
	requires ((!t_Value) + ~0U == 0)
struct TCRound80Choice<t_Value, 1>
{
	void operator()();
};
using FRound80Choice = TCRound80Choice<false, 1>;
FRound80Choice g_Round80Choice;
using FRound90Choice = TCRound80Choice<true, 1>;
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: type alias 'FRound90Choice' has the 'F' prefix of function types, but is not a function type or a function object [malterlib-type-naming]
FRound90Choice g_Round90Choice;

// A call operator of a template whose constraint can't hold is still
// declared, which makes it meant to be a function object.
struct CRound83Never
{
	template <typename tf_CType>
		requires false
	void operator()(tf_CType _Value);
};
using FRound83Never = CRound83Never;

// A value given on to a base is as the type of the parameter of the base.
template <unsigned t_Count>
struct TCRound85Base
{
};
template <>
struct TCRound85Base<1>
{
	void operator()();
};
template <int t_Count>
struct TCRound85Wrapper : TCRound85Base<t_Count>
{
};
using FRound85Callback = TCRound85Wrapper<1>;
FRound85Callback g_Round85Callback;
using FRound90Wrapper = TCRound85Wrapper<2>;
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: type alias 'FRound90Wrapper' has the 'F' prefix of function types, but is not a function type or a function object [malterlib-type-naming]
TCRound85Wrapper<2> g_Round90Wrapper;

// A conversion to a pointer to a function, which a call goes through.
using FRound90Function = void ();
struct CRound90Converts
{
	operator FRound90Function *() const;
};
using FRound90Converts = CRound90Converts;

// A partial specialization whose exception specification or array bound
// depends on an argument after it is told after all arguments are deduced.
template <typename t_CType, bool t_bNoexcept>
struct TCRound87Noexcept
{
	void operator()() {}
};
template <typename t_CResult, bool t_bNoexcept>
struct TCRound87Noexcept<t_CResult () noexcept(t_bNoexcept), t_bNoexcept>
{
};
using FRound87Noexcept = TCRound87Noexcept<void (), true>;

template <typename t_CArray, typename t_CSize>
struct TCRound87Bound
{
	void operator()() {}
};
template <typename t_CElement, typename t_CSize>
struct TCRound87Bound<t_CElement[sizeof(t_CSize)], t_CSize>
{
};
using FRound87Bound = TCRound87Bound<int[4], char>;

// What a specialization of a template is, is told as it is instantiated in the
// translation unit, also after the alias, and not where it isn't: which
// partial specialization its arguments select isn't told otherwise.
template <typename t_CType>
struct TCRound89Choice
{
};
template <typename t_CType>
struct TCRound89Choice<t_CType *>
{
	void operator()();
};
using FRound89Uninstantiated = TCRound89Choice<int>;
using FRound89Instantiated = TCRound89Choice<char>;
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: type alias 'FRound89Instantiated' has the 'F' prefix of function types, but is not a function type or a function object [malterlib-type-naming]
using FRound89Callable = TCRound89Choice<char *>;
TCRound89Choice<char> g_Round89Instance;
TCRound89Choice<char *> g_fRound89Callable;

// A call operator that is declared, whatever its constraint, makes a function
// object.
template <bool t_bEnable>
struct TCRound89Functor
{
	void operator()() requires t_bEnable;
};
TCRound89Functor<false> g_Round89Functor;
using FRound89Constrained = TCRound89Functor<false>;
struct CRound89Base
{
	void operator()();
};
template <typename t_CType>
struct TCRound89Hidden : CRound89Base
{
	void operator()() requires false;
};
TCRound89Hidden<int> g_Round89Hidden;
using FRound89Hidden = TCRound89Hidden<int>;

// So does a conversion to a pointer to a function, whatever its constraint.
using FRound91Function = void ();
template <typename t_CType>
struct TCRound91Disabled
{
	operator FRound91Function *() requires false;
};
TCRound91Disabled<int> g_Round91Disabled;
using FRound91Disabled = TCRound91Disabled<int>;

// A conversion template doesn't make a class a function object, which a call
// never goes through.
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
using FRound94Info = CRound94FunctorInfo;
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: type alias 'FRound94Info' has the 'F' prefix of function types, but is not a function type or a function object [malterlib-type-naming]

template <typename t_CType>
using TFRound98Array = t_CType[2];
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: type alias 'TFRound98Array' has the 'F' prefix of function types, but is not a function type or a function object [malterlib-type-naming]

template <typename t_CType>
using TFRound99ArrayPointer = t_CType (*)[2];
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: type alias 'TFRound99ArrayPointer' has the 'F' prefix of function types, but is not a function type or a function object [malterlib-type-naming]

// Member aliases of a class template, told by its instantiations.
template <typename t_CType>
struct TCRound103Container
{
	using FValue = t_CType;
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: type alias 'FValue' has the 'F' prefix of function types, but is not a function type or a function object [malterlib-type-naming]
	using EValue = t_CType;
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: type alias 'EValue' has the 'E' prefix of enums, but is not an enum [malterlib-type-naming]
};
TCRound103Container<int> g_Round103Container;

// Member aliases of a partial specialization, told by its instantiations.
template <typename t_CType>
struct TCRound104Box;
template <typename t_CType>
struct TCRound104Box<t_CType *>
{
	using FCallback = t_CType;
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: type alias 'FCallback' has the 'F' prefix of function types, but is not a function type or a function object [malterlib-type-naming]
	using EValue = t_CType;
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: type alias 'EValue' has the 'E' prefix of enums, but is not an enum [malterlib-type-naming]
};
TCRound104Box<int *> g_Round104Box;

// An alias template of a type that isn't a function, which is instantiated.
template <typename t_CType>
using TFRound105Value = t_CType;
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: type alias 'TFRound105Value' has the 'F' prefix of function types, but is not a function type or a function object [malterlib-type-naming]

TFRound105Value<int> g_Round105Value = 42;

// A namespace a system header declares too is named by the library, also
// where it is declared first.
namespace tinyxml2
{
	class XMLNode;
}
# 1 "vendor.hpp" 1 3
namespace tinyxml2
{
	class XMLNode
	{
	};
}
# 888 "type-naming.cpp" 2
using CXMLNode = tinyxml2::XMLNode;

// An alias in a class template in a class template, instantiated.
template <typename t_CType>
struct TCRound107Outer
{
	template <typename t_COther>
	struct TCInner
	{
		using FCallback = t_COther;
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: type alias 'FCallback' has the 'F' prefix of function types, but is not a function type or a function object [malterlib-type-naming]
	};
};

TCRound107Outer<int>::TCInner<int> g_Round107Inner;

// An alias of a type of the traits a template is given that its
// instantiations make a function type is named as one.
struct CRound108Traits
{
	using FSignature = void ();
};

template <typename t_CTraits>
struct TCRound108Box
{
	using CCallback = typename t_CTraits::FSignature;
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: function type alias 'CCallback' should have the 'F' prefix followed by upper camel case [malterlib-type-naming]
};

TCRound108Box<CRound108Traits>::CCallback *g_pRound108Callback = nullptr;

// An alias template made concrete in the instantiation of a template.
template <typename t_CType>
using TFRound109Value = t_CType;
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: type alias 'TFRound109Value' has the 'F' prefix of function types, but is not a function type or a function object [malterlib-type-naming]

template <typename t_CType>
struct TCRound109Box
{
	TFRound109Value<t_CType> m_Value;
};

TCRound109Box<int> g_Round109Box;

// Local aliases of a function template are what its instantiations make them.
template <typename tf_CType>
void fg_Round110Aliases()
{
	using FValue = tf_CType;
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: type alias 'FValue' has the 'F' prefix of function types, but is not a function type or a function object [malterlib-type-naming]
	using EValue = tf_CType;
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: type alias 'EValue' has the 'E' prefix of enums, but is not an enum [malterlib-type-naming]
}

void fg_Round110UseAliases()
{
	fg_Round110Aliases<int>();
}

// A local alias of a function template that each instantiation makes a
// function type.
struct CRound111Traits
{
	using FSignature = void ();
};
template <typename tf_CTraits>
void fg_Round111Run()
{
	using CCallback = typename tf_CTraits::FSignature;
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: function type alias 'CCallback' should have the 'F' prefix followed by upper camel case [malterlib-type-naming]
	using FCallback = typename tf_CTraits::FSignature;
}
void fg_Round111Use()
{
	fg_Round111Run<CRound111Traits>();
}

// A generic lambda in a function template is checked through its specializations.
namespace NRound115GenericLambda {
template <typename tf_CType>
void fg_Run() {
  auto fAction = []<typename tf_CInner>(tf_CInner *_pValue) {
    auto Value = _pValue;
    using FWrong = tf_CInner;
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: type alias 'FWrong' has the 'F' prefix of function types, but is not a function type or a function object [malterlib-type-naming]
  };
  fAction(static_cast<tf_CType *>(nullptr));
}
void fg_Use() { fg_Run<int>(); }
}

// An alias template of a dependent member that is a function type is a function type alias.
namespace NRound115MemberFunctionType {
struct CTraits { using FSignature = void (); };
template <typename t_CTraits>
using TCCallback = typename t_CTraits::FSignature;
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: function type alias 'TCCallback' should have the 'TF' prefix followed by upper camel case [malterlib-type-naming]
TCCallback<CTraits> *g_fCallback = nullptr;
}

// A member of a class template defined outside of it is checked through its instantiations.
namespace NRound117OutOfLine {
template <typename t_CType>
struct TCBox { void f_Run(); };
template <typename t_CType>
void TCBox<t_CType>::f_Run() {
  auto Value = static_cast<t_CType *>(nullptr);
  using FValue = t_CType;
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: type alias 'FValue' has the 'F' prefix of function types, but is not a function type or a function object [malterlib-type-naming]
}
void fg_Use() { TCBox<int>{}.f_Run(); }
}

// Round 119: an instance of a partial specialization isn't one of the primary
// template, and a member alias template of a class template is named by what
// its uses make of it.
namespace NRound119Partial {
struct CTraits { using FSignature = void(); };

template <typename t_CTraits>
struct TCBox {
  using FCallback = typename t_CTraits::FSignature;
};

template <typename t_CTraits>
struct TCBox<t_CTraits *> {
  using FCallback = int;
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: type alias 'FCallback' has the 'F' prefix of function types, but is not a function type or a function object [malterlib-type-naming]
};

TCBox<CTraits> g_Box;
TCBox<int *> g_Other;
}

namespace NRound119MemberAliasTemplate {
struct CTraits { using FSignature = void(); };

template <typename t_CTraits>
struct TCWrap {
  template <typename t_COther>
  using TCCallback = typename t_CTraits::FSignature;
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: function type alias 'TCCallback' should have the 'TF' prefix followed by upper camel case [malterlib-type-naming]
};

TCWrap<CTraits>::TCCallback<int> *g_fCallback;
}

// Round 123: an alias of an enum is named like an enum.
namespace NRound123
{
enum EState { EState_Ready };
using CState = EState;
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: enum type alias 'CState' should have the 'E' prefix followed by upper camel case [malterlib-type-naming]
using EStateAlias = EState;
}

// Round 126: an alias of what depends on the parameters computed by decltype
// is a function type when each instantiation makes one.
namespace NRound126
{
struct CTraits {
  static void fs_Run();
};

template <typename t_CTraits>
struct TCBox {
  using CCallback = decltype(t_CTraits::fs_Run);
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: function type alias 'CCallback' should have the 'F' prefix followed by upper camel case [malterlib-type-naming]
};

TCBox<CTraits>::CCallback *g_fCallback;
}

// Round 128: an alias template of what an expression that depends on its
// parameters is, `decltype(t_CTraits::fs_Run)`, is a function type when each
// use makes one.
namespace NRound128
{
struct CTraits {
  static void fs_Run();
};

template <typename t_CTraits>
using TCCallback = decltype(t_CTraits::fs_Run);
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: function type alias 'TCCallback' should have the 'TF' prefix followed by upper camel case [malterlib-type-naming]

TCCallback<CTraits> *g_fCallback = nullptr;
}

// Round 129: an alias of a type a using declaration brings in from a base
// that depends on the parameters is what its instantiations make it.
namespace NRound129
{
template <typename t_CType>
struct TCParent {
  using FSignature = void(t_CType _Value);
};

template <typename t_CType>
struct TCChild : TCParent<t_CType> {
  using typename TCParent<t_CType>::FSignature;
  using CCallback = FSignature;
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: function type alias 'CCallback' should have the 'F' prefix followed by upper camel case [malterlib-type-naming]
};

TCChild<int>::CCallback *g_fCallback = nullptr;
}

// Round 131: an alias template of an enum takes `E`.
namespace NRound131
{
enum EState { EState_Ready };

template <typename t_CType>
struct TCStateTraits {
  using EResult = EState;
};

template <typename t_CType>
using EResult = typename TCStateTraits<t_CType>::EResult;

EResult<int> g_Result = EState_Ready;
}

// Round 132: an alias through an alias template of a type that depends on
// the parameters is what its instantiations make it.
namespace NRound132
{
template <typename t_CType>
using TCIdentity = t_CType;

struct CTraits {
  using FSignature = void();
};

template <typename t_CTraits>
struct TCBox {
  using CCallback = TCIdentity<typename t_CTraits::FSignature>;
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: function type alias 'CCallback' should have the 'F' prefix followed by upper camel case [malterlib-type-naming]
};

TCBox<CTraits> g_Box;
}

// Round 133: an alias template known to be of an enum is named like one.
namespace NRound133
{
enum EStatus { EStatus_Ready };

template <typename t_CType>
using TCStatus = EStatus;
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: enum type alias 'TCStatus' should have the 'E' prefix followed by upper camel case [malterlib-type-naming]

TCStatus<int> g_Status = EStatus_Ready;
}

// Round 134: an alias template through another of a function type is one,
// and an alias that each instantiation makes an enum is one.
namespace NRound134
{
template <typename t_CType>
using TCIdentity = t_CType;

struct CTraits {
  using FSignature = void();
  enum EState { EState_Ready };
};

template <typename t_CTraits>
using TCSignature = TCIdentity<typename t_CTraits::FSignature>;
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: function type alias 'TCSignature' should have the 'TF' prefix followed by upper camel case [malterlib-type-naming]

using FUse = TCSignature<CTraits>;

template <typename t_CTraits>
struct TCBox {
  using CState = typename t_CTraits::EState;
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: enum type alias 'CState' should have the 'E' prefix followed by upper camel case [malterlib-type-naming]
};

TCBox<CTraits> g_Box;
}

// Round 135: an alias template that each use makes an enum is one, and one of
// a specialization of a template of another library keeps its name.
// RUN: rm -rf %t.external135 && mkdir -p %t.external135/system
// RUN: printf 'namespace NExternal { template <class T> struct Box { template <class U> using FBuffer = U; }; }\n' > %t.external135/system/external.hpp
// RUN: printf '#include <external.hpp>\nstruct CValue {};\ntemplate <> struct NExternal::Box<CValue> { template <class t_CType> using FBuffer = t_CType; };\nNExternal::Box<CValue>::FBuffer<int> g_Value;\n' > %t.external135/main.cpp
// RUN: clang-tidy --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" --checks='-*,malterlib-type-naming' %t.external135/main.cpp -- -std=c++20 -isystem %t.external135/system 2>&1 | FileCheck --check-prefix=CHECK-EXTERNAL135 --allow-empty %s
// CHECK-EXTERNAL135-NOT: warning:
// Also of one redeclared before it is specialized.
// RUN: printf 'namespace NExternal { template <class T> struct Size {}; }\n' > %t.external135/system/size.hpp
// RUN: printf '#include <size.hpp>\nnamespace NExternal { template <class T> struct Size; }\nstruct CValue {};\ntemplate <> struct NExternal::Size<CValue> { using type = int; static constexpr int value = 1; };\n' > %t.external135/redeclared.cpp
// RUN: clang-tidy --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" --checks='-*,malterlib-type-naming,malterlib-variable-naming' %t.external135/redeclared.cpp -- -std=c++20 -isystem %t.external135/system 2>&1 | FileCheck --check-prefix=CHECK-EXTERNAL135 --allow-empty %s
namespace NRound135
{
struct CTraits {
  enum EState { EState_Ready };
};

template <typename t_CTraits>
using TCState = typename t_CTraits::EState;
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: enum type alias 'TCState' should have the 'E' prefix followed by upper camel case [malterlib-type-naming]

TCState<CTraits> g_State = CTraits::EState_Ready;
}

namespace NRound137 {
struct CTraits { using FSignature = void (); };

// Through another alias that depends on the parameters, `using CCallback =
// FSignature;`.
template <typename t_CTraits>
struct TCBox {
  using FSignature = typename t_CTraits::FSignature;
  using CCallback = FSignature;
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: function type alias 'CCallback' should have the 'F' prefix followed by upper camel case [malterlib-type-naming]
  using FCallback = FSignature;
};

TCBox<CTraits> g_Box;
}

namespace NRound139 {
struct CTraits {
  using FSignature = void ();
};

// A local alias of a friend defined in a class template.
template <typename t_CTraits>
struct TCBox {
  friend void fg_Use(TCBox _Box) {
    using CSignature = typename t_CTraits::FSignature;
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: function type alias 'CSignature' should have the 'F' prefix followed by upper camel case [malterlib-type-naming]
    using FSignature = typename t_CTraits::FSignature;
  }
};

void fg_Run() { fg_Use(TCBox<CTraits>{}); }
}

namespace NRound140 {
// A local alias of a lambda in a static assertion of a class template.
template <typename t_CType>
struct TCProbe {
  static_assert([] {
    using FValue = t_CType;
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: type alias 'FValue' has the 'F' prefix of function types, but is not a function type or a function object [malterlib-type-naming]
    using CValue = t_CType;
    return sizeof(FValue) == sizeof(CValue);
  }());
};

TCProbe<int> g_Probe;
}

namespace NRound143 {
struct CTraits {
  using FSignature = void ();
};

// A transformation of what depends on the parameters, `__remove_reference_t(
// typename t_CTraits::FSignature)`, but not of a parameter, which is generic.
template <typename t_CTraits>
struct TCBox {
  using CCallback = __remove_reference_t(typename t_CTraits::FSignature);
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: function type alias 'CCallback' should have the 'F' prefix followed by upper camel case [malterlib-type-naming]
};

template <typename t_CType>
struct TCGeneric {
  using CValue = __remove_reference_t(t_CType);
};

TCBox<CTraits> g_Box;
TCGeneric<void ()> g_Generic;
}

namespace NRound144 {
struct CTraits {
  using FSignature = void ();
};

// An alias template of a transformation of what depends on its parameters.
template <typename t_CTraits>
using TCSignature = __remove_reference_t(typename t_CTraits::FSignature);
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: function type alias 'TCSignature' should have the 'TF' prefix followed by upper camel case [malterlib-type-naming]

TCSignature<CTraits> fg_Run;
}

namespace NRound145 {
struct CTraits {
  using FSignature = void ();
};

// An alias template of another alias that depends on the parameters.
template <typename t_CTraits>
struct TCBox {
  using FSignature = typename t_CTraits::FSignature;
  template <typename t_CUnused>
  using TCCallback = FSignature;
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: function type alias 'TCCallback' should have the 'TF' prefix followed by upper camel case [malterlib-type-naming]
};

using FUse = TCBox<CTraits>::TCCallback<int>;
}

namespace NRound146 {
struct CTraits
{
	using FSignature = void ();
};

// A dependent alias in parentheses that each instantiation makes a function
// type.
template <typename t_CTraits>
struct TCBox
{
	typedef typename t_CTraits::FSignature (CCallback);
// CHECK-MESSAGES: :[[@LINE-1]]:42: warning: function type alias 'CCallback' should have the 'F' prefix followed by upper camel case [malterlib-type-naming]
};

TCBox<CTraits> g_Box;
}

namespace NRound151 {
// A member class template declared ahead of its definition.
template <typename t_COuter>
struct TCOuter
{
	template <typename t_CValue>
	struct TCInner;
	template <typename t_CValue>
	struct TCInner
	{
		using FValue = t_CValue;
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: type alias 'FValue' has the 'F' prefix of function types, but is not a function type or a function object [malterlib-type-naming]
	};
};
TCOuter<int>::TCInner<int> g_Inner;
}

namespace NRound173 {
// Not a generic alias that gives the type of a template given one of its
// parameters, `typename TCEnable<t_bEnable, t_CType>::type`, also when its
// uses make function types.
template <bool t_bEnable, typename t_CType> struct TCEnable {};
template <typename t_CType> struct TCEnable<true, t_CType> {
  using type = t_CType;
};
template <bool t_bEnable, typename t_CType>
using TCEnableIf = typename TCEnable<t_bEnable, t_CType>::type;
using FSignature = void();
TCEnableIf<true, FSignature> *g_fCallback;
}

namespace NRound174 {
// A type alias that isn't a template, nor nested in one.
using TCCount = int;
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: type alias 'TCCount' should have the 'C', 'IC', 'F' or 'E' prefix followed by upper camel case [malterlib-type-naming]
using TICFlag = bool;
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: type alias 'TICFlag' should have the 'C', 'IC', 'F' or 'E' prefix followed by upper camel case [malterlib-type-naming]
using CCount = int;
template <typename t_CType>
struct TCBox {
  // One nested in a template, which only a template is named as.
  using TCValue = t_CType;
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: type alias 'TCValue' should have the 'C', 'IC', 'F' or 'E' prefix followed by upper camel case [malterlib-type-naming]
};
TCBox<int>::TCValue g_Value;
}

namespace std {
template <bool t_bEnable, typename t_CType = void> struct enable_if {};
template <typename t_CType> struct enable_if<true, t_CType> {
  using type = t_CType;
};
}

namespace NRound177 {
// A member alias of the `type` of a template given a parameter, which gives
// what it is given.
template <typename t_CType>
struct TCBox {
  using CValue = typename std::enable_if<true, t_CType>::type;
};
TCBox<void ()> g_Box;
// Not one whose `type` is the same whatever it is given.
template <typename t_CType>
struct TCTraits {
  using type = void ();
};
template <typename t_CType>
using TCCallback = typename TCTraits<t_CType>::type;
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: function type alias 'TCCallback' should have the 'TF' prefix followed by upper camel case [malterlib-type-naming]
TCCallback<int> *g_fCallback;
}

namespace NRound179 {
// Forwarding through the `CType` of a template given a parameter.
template <typename t_CType>
struct TCIdentity {
  using CType = t_CType;
};
template <typename t_CType>
using TCForward = typename TCIdentity<t_CType>::CType;
using FSignature = void ();
TCForward<FSignature> *g_fCallback = nullptr;
}

namespace NRound182 {
// A `type` that a base which doesn't depend on the parameters has, which isn't
// what it is given.
struct CSignature {
  using type = void ();
};
template <typename t_CUnused>
struct TCTraits : CSignature {};
template <typename t_CUnused>
using TCCallback = typename TCTraits<t_CUnused>::type;
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: function type alias 'TCCallback' should have the 'TF' prefix followed by upper camel case [malterlib-type-naming]
TCCallback<int> *g_fCallback = nullptr;
}

namespace NRound184 {
// Transformations of a parameter, one of another, which are generic.
template <typename t_CType>
using TCRemoveReference = __remove_reference_t(t_CType);
template <typename t_CType>
using TCRemoveQualifiers = __remove_cv(t_CType);
enum EMode { EMode_One };
template <typename t_CType>
struct TCBox {
  using CValue = TCRemoveQualifiers<TCRemoveReference<t_CType>>;
};
TCBox<EMode const &> g_Box;
}

namespace NRound185 {
// The `CType` of a template given its parameters in another type, which
// forwards what it is given, also recursively.
template <typename... tp_CTypes>
struct TCTypeList {};
template <unsigned t_iIndex, typename t_CList>
struct TCGet;
template <typename t_CFirst, typename... tp_CRest>
struct TCGet<0, TCTypeList<t_CFirst, tp_CRest...>> {
  using CType = t_CFirst;
};
template <unsigned t_iIndex, typename t_CFirst, typename... tp_CRest>
struct TCGet<t_iIndex, TCTypeList<t_CFirst, tp_CRest...>> {
  using CType = typename TCGet<t_iIndex - 1, TCTypeList<tp_CRest...>>::CType;
};
using FSignature = void ();
using FSelected = TCGet<1, TCTypeList<int, FSignature>>::CType;
FSelected *g_fCallback = nullptr;
enum EMode { EMode_One };
using ESelected = TCGet<1, TCTypeList<int, EMode>>::CType;
ESelected g_Mode = EMode_One;
}

namespace NRound192 {
// A member that gives a parameter, whatever its name, of a template given a
// parameter, is generic; given a member of a type, it is what that is.
template <typename t_CKey, typename t_CValue>
struct TCMap {
  using CValue = t_CValue;
};
enum EState { EState_Ready };
template <typename t_CValue>
struct TCStorage {
  using CValue = typename TCMap<int, t_CValue>::CValue;
};
TCStorage<EState> g_Storage;
struct CStateTraits {
  using EValue = EState;
};
template <typename t_CTraits>
struct TCTraitsStorage {
  using CValue = typename TCMap<int, typename t_CTraits::EValue>::CValue;
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: enum type alias 'CValue' should have the 'E' prefix followed by upper camel case [malterlib-type-naming]
};
TCTraitsStorage<CStateTraits> g_TraitsStorage;
template <typename t_CType>
struct TCIdentity {
  using CType = t_CType;
};
struct CSignatureTraits {
  using FSignature = void();
};
template <typename t_CTraits>
struct TCBox {
  using CCallback = typename TCIdentity<typename t_CTraits::FSignature>::CType;
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: function type alias 'CCallback' should have the 'F' prefix followed by upper camel case [malterlib-type-naming]
};
TCBox<CSignatureTraits> g_Box;
template <typename t_CType>
struct TCNestedBox {
  using CValue = typename TCIdentity<typename TCIdentity<t_CType>::CType>::CType;
};
TCNestedBox<void()> g_NestedBox;
}

namespace NRound193Member {
// A lambda in the initializer of a member variable template of a class
// template, of the member templates of the instantiations of the class.
struct CTraits {
  using CBool = bool;
};
template <typename t_CTraits>
struct TCTraits {
  template <typename t_CValue>
  static constexpr bool mc_bSupports = [] {
    typename t_CTraits::CBool Ready = true;
    using FValue = typename t_CTraits::CBool;
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: type alias 'FValue' has the 'F' prefix of function types, but is not a function type or a function object [malterlib-type-naming]
    return Ready;
  }();
};
constexpr bool gc_bSupports = TCTraits<CTraits>::mc_bSupports<int>;
}

namespace NRound199 {
// Not of a partial specialization that what is written can't be.
template <typename t_CType, bool t_bKeep>
struct TCSelect {
  using CType = t_CType;
};
template <typename t_CType>
struct TCSelect<t_CType, false> {
  using CType = bool;
};
template <typename tf_CType>
void fg_Run() {
  using CValue = typename TCSelect<tf_CType, true>::CType;
}
void fg_Use() { fg_Run<void()>(); }
}

namespace NRound200 {
// A member of what a parameter is given that gives an argument of that as it
// is, also through another, is generic; one that its traits define is what
// they define it as.
enum EMode { EMode_Value };
template <typename t_CKey, typename t_CValue>
struct TCMap {
  using CKey = t_CKey;
};
template <typename t_CMap>
struct TCMapIterator {
  using CKey = typename t_CMap::CKey;
};
template <typename t_CIterator>
struct TCReverseIterator {
  using CKey = typename t_CIterator::CKey;
};
TCMapIterator<TCMap<EMode, int>> g_iEntries;
TCReverseIterator<TCMapIterator<TCMap<EMode, int>>> g_iReverse;
struct CTraits {
  enum EState { EState_Ready };
};
template <typename t_CTraits>
struct TCUser {
  using CState = typename t_CTraits::EState;
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: enum type alias 'CState' should have the 'E' prefix followed by upper camel case [malterlib-type-naming]
};
TCUser<CTraits> g_User;
// What the type of a parameter is made into, `decltype(_Value)`, is generic.
template <typename t_CType>
struct TCRemoveReference {
  using CType = t_CType;
};
template <typename t_CType>
struct TCRemoveReference<t_CType &> {
  using CType = t_CType;
};
template <typename t_CType>
using TCRemoveReferenceAndQualifiers = typename TCRemoveReference<t_CType>::CType;
template <typename tf_CValue>
void fg_Value(tf_CValue &&_Value) {
  using CType = TCRemoveReferenceAndQualifiers<decltype(_Value)>;
  CType Copy = _Value;
  (void)Copy;
}
void fg_Use() {
  EMode Mode = EMode_Value;
  fg_Value(Mode);
}
}

namespace NRound202 {
// A member of what a parameter is given that a trait makes of an argument
// of that, `TCRemoveReference<t_CValue>`, is generic.
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
template <typename t_CKey, typename t_CValue>
struct TCMap {
  using CValueNoRef = TCRemoveReference<t_CValue>;
};
enum EMode { EMode_First };
template <typename t_CMap>
struct TCProcessor {
  using CValue = typename t_CMap::CValueNoRef;
};
TCProcessor<TCMap<int, EMode &>> g_Processor;
}

namespace NRound205 {
// A specialization that what is written can't be, of a list of another
// number of types, doesn't keep a member that forwards from being generic.
template <typename... tp_CTypes>
struct TCTypeList {};
template <unsigned t_iIndex, typename t_CList>
struct TCGetOrVoid;
template <unsigned t_iIndex>
struct TCGetOrVoid<t_iIndex, TCTypeList<>> {
  using CType = void;
};
template <typename t_CFirst, typename... tp_CRest>
struct TCGetOrVoid<0, TCTypeList<t_CFirst, tp_CRest...>> {
  using CType = t_CFirst;
};
enum EMode { EMode_Ready };
template <typename t_CType>
struct TCProcessor {
  using CValue = typename TCGetOrVoid<0, TCTypeList<t_CType>>::CType;
};
TCProcessor<EMode> g_Processor;
// What a dependent base defines is what its template writes.
template <typename t_CType>
struct TCProvider {
  using FSignature = void(t_CType _Value);
};
template <typename t_CType>
struct TCTraits : TCProvider<t_CType> {};
template <typename t_CType>
using TCCallback = typename TCTraits<t_CType>::FSignature;
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: function type alias 'TCCallback' should have the 'TF' prefix followed by upper camel case [malterlib-type-naming]
TCCallback<int> fg_Run;
}

namespace NRound207 {
// An alias template of a member of what its parameter is given that gives
// what that is given, which is generic.
enum class EMode { mc_First };
template <class t_CKey, class t_CValue>
struct TCMap {
  using CKey = t_CKey;
};
template <class t_CMap>
using TCMapKey = typename t_CMap::CKey;
TCMapKey<TCMap<EMode, int>> g_Mode = EMode::mc_First;
}

namespace NRound208
{
	// A member template of what depends on the parameters is what each
	// instantiation makes it.
	struct CTraits
	{
		template <typename t_CValue>
		struct TCResult
		{
			using CType = bool;
			using FSignature = void(t_CValue _Value);
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
		using CSignature = typename t_CTraits::template TCResult<t_CValue>::FSignature;
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: function type alias 'CSignature' should have the 'F' prefix followed by upper camel case [malterlib-type-naming]
		using CValue = typename t_CTraits::template TCIdentity<t_CValue>::CType;
	};
	TCBox<CTraits, int> g_Box;
	TCBox<CTraits, void ()> g_FunctionBox;
}

namespace NRound212
{
	// A member of what a parameter is given through an adapter that forwards
	// it is generic, whatever it is given.
	enum EState
	{
		EState_Ready
	};
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
	template <typename t_CContainer>
	struct TCRead
	{
		using CValue = typename TCAdapter<t_CContainer>::CValue;
	};
	TCRead<TCVector<EState>> g_Value;
}

namespace NRound220
{
	// A member of a base that is a parameter is a member of what is given.
	struct CTraits
	{
		using FSignature = void ();
	};
	template <typename t_CTraits>
	struct TCAdapter : t_CTraits
	{
	};
	template <typename tf_CTraits>
	void fg_Test()
	{
		using CCallback = typename TCAdapter<tf_CTraits>::FSignature;
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: function type alias 'CCallback' should have the 'F' prefix followed by upper camel case [malterlib-type-naming]
	}
	void fg_Run()
	{
		fg_Test<CTraits>();
	}
}

namespace NRound221
{
	// A member of a member of what is given forwards a generic type too.
	enum EMode
	{
		EMode_Run
	};
	template <typename t_CValue>
	struct TCMapNode
	{
		using CValue = t_CValue;
	};
	template <typename t_CKey, typename t_CValue>
	struct TCMap
	{
		using CNode = TCMapNode<t_CValue>;
	};
	template <typename t_CMap>
	struct TCMapIterator
	{
		using CNodePublic = typename t_CMap::CNode;
		using CValue = typename CNodePublic::CValue;
	};
	TCMapIterator<TCMap<int, EMode>> g_iPosition;
}

namespace NRound225
{
	// An alias used through another alias.
	struct CTraits
	{
		using CType = int;
	};
	template <typename t_CTraits>
	using TFCallback = typename t_CTraits::CType;
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: type alias 'TFCallback' has the 'F' prefix of function types, but is not a function type or a function object [malterlib-type-naming]
	template <typename t_CTraits>
	using TCWrapped = TFCallback<t_CTraits>;
	using CResult = TCWrapped<CTraits>;
}

namespace NRound228
{
	// What a parameter of a generic type is declared as is generic like it,
	// but not what an expression in parentheses or a member of it is.
	enum EMode
	{
		EMode_Default
	};
	struct CTraits
	{
		using EState = EMode;
	};
	template <typename tf_CType>
	void fg_Copy(tf_CType _Value)
	{
		using CType = decltype(_Value);
		CType Copy = _Value;
		(void)Copy;
	}
	template <typename tf_CTraits>
	void fg_State(typename tf_CTraits::EState _State)
	{
		using CState = decltype(_State);
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: enum type alias 'CState' should have the 'E' prefix followed by upper camel case [malterlib-type-naming]
		CState Copy = _State;
		(void)Copy;
	}
	void fg_Use()
	{
		fg_Copy(EMode_Default);
		fg_State<CTraits>(EMode_Default);
	}
}

namespace NRound229
{
	// One that a use gives what it is given in turn is generic, also when the
	// other uses give an enum, of a member alias and of an alias template.
	enum class EMode
	{
		mc_Ready
	};
	struct CEnumProvider
	{
		static EMode fs_Get();
	};
	struct CIntProvider
	{
		static int fs_Get();
	};
	template <typename t_CProvider>
	struct TCTraits
	{
		using CType = decltype(t_CProvider::fs_Get());
	};
	TCTraits<CIntProvider> g_IntTraits;
	template <typename t_CType>
	struct TCIdentity
	{
		using CType = t_CType;
	};
	template <typename t_CTraits>
	struct TCBox
	{
		using CValue = typename t_CTraits::CType;
	};
	TCBox<TCIdentity<int>> g_Value;
	TCBox<TCTraits<CEnumProvider>> g_Mode;
	template <typename t_CTraits>
	using TCValue = typename t_CTraits::CType;
	TCValue<TCIdentity<int>> g_AliasValue;
	TCValue<TCTraits<CEnumProvider>> g_AliasMode;
}

namespace NRound236
{
	struct CTraits
	{
		using CType = int;
	};
	// An alias that is a default argument, of what a use that leaves it out
	// makes of it.
	template <typename t_CTraits>
	using TFCallback = typename t_CTraits::CType;
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: type alias 'TFCallback' has the 'F' prefix of function types, but is not a function type or a function object [malterlib-type-naming]
	template <typename t_CTraits, typename t_FCallback = TFCallback<t_CTraits>>
	struct TCHandler
	{
	};
	TCHandler<CTraits> g_Handler;
}

namespace NRound243
{
	struct CTraits
	{
		using FSignature = void(int _Value);
	};
	// An alias that is a default argument of a function template, of what a
	// call that leaves it out makes of it.
	template <typename t_CTraits>
	using TCSignature = typename t_CTraits::FSignature;
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: function type alias 'TCSignature' should have the 'TF' prefix followed by upper camel case [malterlib-type-naming]
	template <typename tf_CTraits, typename tf_CSignature = TCSignature<tf_CTraits>>
	void fg_Run()
	{
	}
	void fg_Test()
	{
		fg_Run<CTraits>();
	}
}

namespace NRound331
{
	struct CTraits
	{
		using FSignature = void(int _Value);
		using CType = int;
	};
	// Also of one whose parameters don't name the one the default is for,
	// `tf_CData &_Value`.
	template <typename t_CTraits>
	using TCSignature = typename t_CTraits::FSignature;
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: function type alias 'TCSignature' should have the 'TF' prefix followed by upper camel case [malterlib-type-naming]
	template <typename tf_CTraits, typename tf_CData, typename tf_CSignature = TCSignature<tf_CTraits>>
	void fg_Run(tf_CData &_Value)
	{
	}
	// Also of a member function template, `Runner.f_Run<CTraits>()`.
	template <typename t_CTraits>
	using TFValue = typename t_CTraits::CType;
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: type alias 'TFValue' has the 'F' prefix of function types, but is not a function type or a function object [malterlib-type-naming]
	struct CRunner
	{
		template <typename tf_CTraits, typename tf_CValue = TFValue<tf_CTraits>>
		void f_Run()
		{
		}
	};
	// But not of one that a parameter names, which the call deduces.
	template <typename t_CTraits>
	using TFDeduced = typename t_CTraits::FSignature;
	template <typename tf_CTraits, typename tf_FDeduced = TFDeduced<tf_CTraits>>
	void fg_Take(tf_FDeduced const &_Value)
	{
	}
	void fg_Test(int &_Value)
	{
		fg_Run<CTraits>(_Value);
		CRunner Runner;
		Runner.f_Run<CTraits>();
		fg_Take<CTraits>(5);
	}
}

namespace NRound332
{
	struct CTraits
	{
		using FSignature = void();
	};
	// Also where the call leaves the argument of the parameter that names it
	// to its default, which deduces nothing.
	template <typename t_CTraits>
	using TCSignature = typename t_CTraits::FSignature;
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: function type alias 'TCSignature' should have the 'TF' prefix followed by upper camel case [malterlib-type-naming]
	template <typename tf_CTraits, typename tf_FSignature = TCSignature<tf_CTraits>>
	void fg_Run(tf_FSignature *_fCallback = nullptr)
	{
	}
	void fg_Test()
	{
		fg_Run<CTraits>();
	}
}

namespace NRound332Length
{
	// A name is at least as long as the minimum after its prefix, but not the
	// value an enumerator gives after its enum's name.
	struct CX;
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: class 'CX' should have at least 2 characters after its prefix [malterlib-type-naming]
	template <typename t_CType>
	struct TCZ;
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: class template 'TCZ' should have at least 2 characters after its prefix [malterlib-type-naming]
	using F1 = void ();
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: function type alias 'F1' should have at least 2 characters after its prefix [malterlib-type-naming]
	namespace NX
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: namespace 'NX' should have at least 2 characters after its prefix [malterlib-type-naming]
	{
	}
	enum EA
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: enum 'EA' should have at least 2 characters after its prefix [malterlib-type-naming]
	{
		EA_Value
	};
	enum ELevel
	{
		ELevel_2
		, ELevel_A
	};
	template <typename t_CType>
	concept cQ = true;
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: concept 'cQ' should have at least 2 characters after its prefix [malterlib-type-naming]
}

namespace NRound336 {
// A parameter of a defaulted one only in a qualifier, which isn't deduced,
// leaves the default, which is checked where it is used.
template <typename t_CType> struct TCIdentity {
  using CType = t_CType;
};
struct CTraits {
  using FSignature = void();
};
template <typename t_CTraits> using TCCallback = typename t_CTraits::FSignature;
// CHECK-MESSAGES: :[[@LINE-1]]:37: warning: function type alias 'TCCallback' should have the 'TF' prefix followed by upper camel case [malterlib-type-naming]
template <typename tf_CTraits, typename tf_FCallback = TCCallback<tf_CTraits>>
void fg_Run(typename TCIdentity<tf_FCallback>::CType *_fCallback) {}
void fg_Test() { fg_Run<CTraits>(nullptr); }
}

namespace NRound336Shared {
// A type that names another twice in each of many levels is looked through
// once for each type it is made of, not each path to it.
using FLeft0 = void (*)();
using FRight0 = void (*)();
#define DLevel(N, P)                                                          \
  using FLeft##N = FLeft##P (*)(FLeft##P, FLeft##P);                          \
  using FRight##N = FRight##P (*)(FRight##P, FRight##P);
DLevel(1, 0)
DLevel(2, 1)
DLevel(3, 2)
DLevel(4, 3)
DLevel(5, 4)
DLevel(6, 5)
DLevel(7, 6)
DLevel(8, 7)
DLevel(9, 8)
DLevel(10, 9)
DLevel(11, 10)
DLevel(12, 11)
DLevel(13, 12)
DLevel(14, 13)
DLevel(15, 14)
DLevel(16, 15)
DLevel(17, 16)
DLevel(18, 17)
DLevel(19, 18)
DLevel(20, 19)
DLevel(21, 20)
DLevel(22, 21)
DLevel(23, 22)
DLevel(24, 23)
DLevel(25, 24)
DLevel(26, 25)
DLevel(27, 26)
DLevel(28, 27)
DLevel(29, 28)
DLevel(30, 29)
DLevel(31, 30)
DLevel(32, 31)
DLevel(33, 32)
DLevel(34, 33)
DLevel(35, 34)
DLevel(36, 35)
DLevel(37, 36)
DLevel(38, 37)
DLevel(39, 38)
DLevel(40, 39)
template <typename t_FSignature> struct TCFunction {};
template <typename t_CType> struct TCHolder {
  template <typename tf_FOnResult = TCFunction<void()>>
  void f_Test(t_CType _Value, tf_FOnResult _fOnResult = {}) {}
};
template <typename tf_CType> void fg_Both(tf_CType _First, tf_CType _Second) {
  TCHolder<tf_CType> Holder;
  Holder.f_Test(_First);
}
void fg_Use() { fg_Both(FLeft40{}, FRight40{}); }
}

namespace NRound340NonDeduced {
// A parameter named only through an alias that stands for a qualified type
// isn't deduced, so its default is used.
template<typename t_CType> struct TCIdentityHelper { using CType = t_CType; };
template<typename t_CType> using TCIdentity = typename TCIdentityHelper<t_CType>::CType;
struct CTraits { using CType = int; };
template<typename t_CTraits> using TFValue = typename t_CTraits::CType;
// CHECK-MESSAGES: :[[@LINE-1]]:36: warning: type alias 'TFValue' has the 'F' prefix of function types, but is not a function type or a function object [malterlib-type-naming]
template<typename tf_CTraits, typename tf_CValue = TFValue<tf_CTraits>>
void fg_Run(TCIdentity<tf_CValue> _Value) {}
void fg_Test() { fg_Run<CTraits>(42); }
}

namespace NRound340NestedDefault {
// An alias nested in a default.
struct CTraits { using FSignature = void(); };
template <typename t_CTraits> using TCSignature = typename t_CTraits::FSignature;
// CHECK-MESSAGES: :[[@LINE-1]]:37: warning: function type alias 'TCSignature' should have the 'TF' prefix followed by upper camel case [malterlib-type-naming]
template <typename t_FSignature> struct TCFunction {};
template <typename tf_CTraits, typename tf_FCallback = TCFunction<TCSignature<tf_CTraits>>>
void fg_Run(tf_FCallback _fCallback = {}) {}
void fg_Test() { fg_Run<CTraits>(); }
}

namespace NRound340NestedAlias {
// An alias nested in what another stands for.
struct CTraits { using FSignature = void (); };
template<typename t_FSignature> struct TCFunction {};
template<typename t_CTraits> using TCSignature = typename t_CTraits::FSignature;
// CHECK-MESSAGES: :[[@LINE-1]]:36: warning: function type alias 'TCSignature' should have the 'TF' prefix followed by upper camel case [malterlib-type-naming]
template<typename t_CTraits> using TCCallback = TCFunction<TCSignature<t_CTraits>>;
TCCallback<CTraits> g_fCallback;
}

// Each alias in a written type once for what is at its place, also where the
// same arguments are given on many paths, whose number grows twelvefold with
// each level.
namespace NRound362SharedArguments {
template <typename, typename, typename, typename, typename, typename, typename, typename, typename, typename, typename, typename>
struct TCPart {};
template <typename t_CType> using TCZero = t_CType;
#define DTwelve(_Previous) _Previous<t_CType>, _Previous<t_CType>, _Previous<t_CType>, _Previous<t_CType>, _Previous<t_CType>, _Previous<t_CType>, _Previous<t_CType>, _Previous<t_CType>, _Previous<t_CType>, _Previous<t_CType>, _Previous<t_CType>, _Previous<t_CType>
#define DLevel(_Level, _Previous) template <typename t_CType> using TC##_Level = TCPart<DTwelve(_Previous)>;
DLevel(One, TCZero)
DLevel(Two, TCOne)
DLevel(Three, TCTwo)
DLevel(Four, TCThree)
DLevel(Five, TCFour)
DLevel(Six, TCFive)
DLevel(Seven, TCSix)
DLevel(Eight, TCSeven)
#undef DLevel
#undef DTwelve
TCEight<int> *g_pValue;
}

// A type of a C library that this code only declares, in `extern "C"`, and
// the name a typedef there gives it, have the library's names, whichever
// header declares them first.
extern "C" {
typedef struct x509_store_st X509_STORE;
struct evp_pkey_st;
}
// But not one this code defines there, nor one it declares outside it.
extern "C" {
struct own_c_struct { int m_Value; };
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: class 'own_c_struct' should have the 'C' or 'IC' prefix followed by upper camel case [malterlib-type-naming]
}
struct opaque_cpp_st;
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: class 'opaque_cpp_st' should have the 'C' or 'IC' prefix followed by upper camel case [malterlib-type-naming]
