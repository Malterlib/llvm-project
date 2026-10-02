// RUN: clang-tidy --checks='-*,malterlib-conceptual-prefix,malterlib-type-naming,malterlib-variable-naming,malterlib-parameter-naming,malterlib-function-naming,malterlib-struct-not-class,malterlib-member-order' --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" %s -- -std=c++20 -fms-extensions -fno-delayed-template-parsing 2>&1 | FileCheck --allow-empty --implicit-check-not='{{warning|error}}:' %s

// What the naming checks read of a qualifier, a name and a template argument
// is read by its kind: Microsoft's `__super`, which names no type a check can
// tell, an operator, a conversion function, a constructor and a destructor,
// whose names aren't identifiers, anonymous classes, and non-type and
// template template arguments where a type argument can be.

struct CBase
{
	using CValue = int *;
	using FCallback = void ();
	static constexpr bool mc_bValue = true;
};

// A member whose type `__super` qualifies, in each place a qualifier is read:
// a member type, a value given as a template argument, the type of a
// function-like member.
template <bool t_bValue>
struct TCFlag
{
};

template <typename t_CBase>
struct TCBox : t_CBase
{
	typename __super::CValue m_pValue;
	typename __super::FCallback *m_fCallback;
	TCFlag<__super::mc_bValue> m_Flag;
};

TCBox<CBase> g_Box;

// Names that aren't identifiers where a name is read: an operator, a
// conversion function, a constructor and a destructor, called and named.
struct CCallable
{
	CCallable();
	~CCallable();
	int *operator()() const;
	operator int *() const;
	CCallable &operator=(CCallable const &_Other);
};

void fg_Use()
{
	CCallable Callable;
	int *pCalled = Callable();
	int *pConverted = Callable;
	int *pExplicit = Callable.operator int *();
	CCallable Copy;
	Copy = Callable;
	(void)pCalled;
	(void)pConverted;
	(void)pExplicit;
}

// Anonymous classes.
struct CAnonymous
{
	union
	{
		int *m_pValue;
		float m_Value;
	};
	struct
	{
		int m_Inner;
	} m_Named;
};

CAnonymous g_Anonymous;

// Non-type and template template arguments where a type argument can be.
template <typename t_CType>
struct TCHolder
{
	t_CType m_Value;
};

template <int t_Value>
struct TCNumber
{
	int *m_pValue;
};

template <template <typename> class t_TCTemplate>
struct TCTemplated
{
	t_TCTemplate<int *> m_Value;
};

template <typename t_CType, int t_Value>
struct TCMixed
{
	t_CType m_Value;
};

template <typename t_CType>
struct TCMixed<t_CType *, 0>
{
	t_CType *m_pValue;
};

TCNumber<3> g_Number;
TCTemplated<TCHolder> g_Templated;
TCMixed<int *, 0> g_Mixed;
TCMixed<int, 1> g_Other;
