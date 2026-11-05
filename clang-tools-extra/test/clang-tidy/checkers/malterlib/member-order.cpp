// RUN: %check_clang_tidy -std=c++20-or-later %s malterlib-member-order %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}"
// The begin of a NOLINT block that isn't ended covers nothing, as clang-tidy
// reads it.
// RUN: printf '// NOLINT%%sBEGIN(malterlib-member-order)\nstruct CUnmatched\n{\n\tint m_Value;\n\tvoid f_Run();\n};\n' '' > %t.unmatched.cpp
// RUN: not clang-tidy --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" --checks='-*,malterlib-member-order' %t.unmatched.cpp -- -std=c++20 2>&1 | FileCheck --check-prefix=CHECK-UNMATCHED %s
// CHECK-UNMATCHED-DAG: unmatched.cpp:5:7: warning: function 'f_Run' should come before the variables of its section
// CHECK-UNMATCHED-DAG: unmatched 'NOLINT{{B}}EGIN'

// A class has its public, protected and private sections in that order, and
// each section types, constructors and destructor, functions, constants and
// variables.
struct CInOrder
{
	using CType = int;
	struct CNested
	{
	};
	enum EKind
	{
		EKind_First
	};
	template <typename t_CType>
	using TCAlias = t_CType;
	CInOrder();
	template <typename tf_CType>
	CInOrder(tf_CType _Value);
	~CInOrder();
	void f_Function();
	template <typename tf_CType>
	void f_Template();
	static constexpr int mc_Constant = 1;
	static int const mc_DefinedElsewhere;
	int m_Variable;
	static int ms_Static;
	union
	{
		int m_First;
		float m_Second;
	};
	// An unnamed struct is the type of the variable it is written in.
	struct
	{
		int m_Low;
		int m_High;
	} m_Range;
	static_assert(true);

protected:
	void fp_Protected();
	int mp_Protected;

private:
	friend struct CFriend;
	using CPrivateType = int;
	void fp_Private();
	int mp_Private;
};

struct COutOfOrder
{
	int m_Variable;
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: variable 'm_Variable' should come after the constants of its section; a section has types, constructors and destructor, functions, constants and variables, in that order [malterlib-member-order]
	void f_Function();
	using CType = int;
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: type 'CType' should come before the functions of its section; a section has types, constructors and destructor, functions, constants and variables, in that order [malterlib-member-order]
	static constexpr int mc_Constant = 1;
	COutOfOrder();
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: constructor or destructor 'COutOfOrder' should come before the functions of its section; a section has types, constructors and destructor, functions, constants and variables, in that order [malterlib-member-order]

private:
	int mp_Private;

protected:
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: the protected section should come before the private section; a class has its public, protected and private sections in that order [malterlib-member-order]
	int mp_Protected;

public:
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: the public section should come before the private section; a class has its public, protected and private sections in that order [malterlib-member-order]
	void f_Public();
};

// The order starts again in each section, and a constant comes after the
// functions.
struct CPerSection
{
	int m_Variable;

private:
	using CPrivateType = int;
	void fp_Function();
	static constexpr int mcp_Constant = 1;
	void fp_Late();
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: function 'fp_Late' should come before the constants of its section; a section has types, constructors and destructor, functions, constants and variables, in that order [malterlib-member-order]
};

// A class template is checked as written, also a using declaration by what
// it brings in, and a section kept out of order for what the public one needs
// has a NOLINT.
struct CBase
{
	CBase(int _Value);
	void f_Run();
};

struct CDerived : CBase
{
	int m_Value;
	using CBase::CBase;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: constructor or destructor 'CDerived' should come before the variables of its section; a section has types, constructors and destructor, functions, constants and variables, in that order [malterlib-member-order]
};

template <typename t_CType>
struct TCDerived : CBase
{
	t_CType m_Value;
	void f_Late();
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: function 'f_Late' should come before the variables of its section; a section has types, constructors and destructor, functions, constants and variables, in that order [malterlib-member-order]
};

struct CNeedsPrivateFirst
{
private:
	struct CInternal
	{
	};

public: // NOLINT(malterlib-member-order): the public declarations use CInternal
	CInternal f_Internal();
};

// What a macro writes is ordered where the macro is used.
#define DDeclareRun void f_Run();
struct CMacro
{
	int m_Value;
	DDeclareRun
};


// Only the fewest members that have to move are reported: one kept early for
// what needs it first is the one reported, so one NOLINT covers it.
struct CKeptEarly
{
	static constexpr int mc_Size = 4;
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: constant 'mc_Size' should come after the functions of its section; a section has types, constructors and destructor, functions, constants and variables, in that order [malterlib-member-order]
	using CArray = int[mc_Size];
	void f_First(CArray const &_Array);
	void f_Second();
	void f_Third();
};

struct CKeptEarlyExcused
{
	static constexpr int mc_Size = 4; // NOLINT(malterlib-member-order): CArray uses it
	using CArray = int[mc_Size];
	void f_First(CArray const &_Array);
	void f_Second();
};

// A member that only starts with a macro, like an attribute, is ordered too.
#define DMibAttribute [[nodiscard]]
struct CAttributeMacro
{
	int m_Value;
	DMibAttribute int f_Value() const;
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: function 'f_Value' should come before the variables of its section; a section has types, constructors and destructor, functions, constants and variables, in that order [malterlib-member-order]
};

// A label that repeats the access of its section continues the section.
struct CRepeatedLabel
{
	int m_Value;

public:
	CRepeatedLabel();
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: constructor or destructor 'CRepeatedLabel' should come before the variables of its section; a section has types, constructors and destructor, functions, constants and variables, in that order [malterlib-member-order]
};

// A class's implicit private section that has no members isn't a section
// before its first label.
class CClassPublicFirst
{
public:
	CClassPublicFirst();

protected:
	void fp_Protected();

private:
	int mp_Value;
};

class CClassProtectedFirst
{
protected:
	void fp_Protected();

private:
	int mp_Value;
};

// One with members, or a label, is.
class CClassPrivateMembersFirst
{
	int mp_Value;

public:
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: the public section should come before the private section; a class has its public, protected and private sections in that order [malterlib-member-order]
	void f_Run();
};

class CClassPrivateLabelFirst
{
private:

public:
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: the public section should come before the private section; a class has its public, protected and private sections in that order [malterlib-member-order]
	void f_Run();
};

// A static member declared const without its value is a constant when its
// definition is constexpr, or isn't in the translation unit, and a variable
// when its definition isn't constexpr.
int fg_Read();
struct CConstDefinitions
{
	static int const mc_Constexpr;
	static int const mc_Elsewhere;
	static int ms_First;
	static int const ms_Second;
	static int const mc_Late;
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: constant 'mc_Late' should come before the variables of its section; a section has types, constructors and destructor, functions, constants and variables, in that order [malterlib-member-order]
};
constexpr int CConstDefinitions::mc_Constexpr = 1;
int CConstDefinitions::ms_First = 0;
int const CConstDefinitions::ms_Second = fg_Read();
constexpr int CConstDefinitions::mc_Late = 2;

// A using declaration of a type of what depends on the parameters is a type.
template <typename t_CBase>
struct TCUsingTypename : t_CBase
{
	int m_Value;
	using typename t_CBase::CType;
// CHECK-MESSAGES: :[[@LINE-1]]:26: warning: type 'CType' should come before the variables of its section; a section has types, constructors and destructor, functions, constants and variables, in that order [malterlib-member-order]
};

// So is a using declaration of the constructors of a base a class template is
// given, `using t_CBase::t_CBase;`, a constructor.
template <typename t_CBase>
struct TCInheritsConstructors : t_CBase
{
	int m_Value;
	using t_CBase::t_CBase;
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: constructor or destructor should come before the variables of its section; a section has types, constructors and destructor, functions, constants and variables, in that order [malterlib-member-order]
};

// An anonymous union is a variable of the section it is in.
struct CAnonymousUnion
{
	union
	{
		int m_Value;
	};
	void f_Run();
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: function 'f_Run' should come before the variables of its section; a section has types, constructors and destructor, functions, constants and variables, in that order [malterlib-member-order]
};

struct CAnonymousUnionLast
{
	void f_Run();
	union
	{
		int m_Value;
	};
};

// What a use of a member template instantiates is ordered as the template.
struct CMemberTemplates
{
	template <typename t_CType>
	struct TCInner
	{
	};
	template <typename t_CType>
	static constexpr int mc_Value = sizeof(t_CType);
	int m_Value = mc_Value<int>;
	TCInner<int> m_Inner;
};

// A member of an anonymous union of a base a using declaration brings in is a
// variable.
struct CUnionBase
{
	union
	{
		int m_Value;
	};
};
struct CUsesUnionMember : CUnionBase
{
	using CUnionBase::m_Value;
	void f_Run();
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: function 'f_Run' should come before the variables of its section; a section has types, constructors and destructor, functions, constants and variables, in that order [malterlib-member-order]
};

// A member a NOLINT keeps early for what needs it is left out of the order, so
// it doesn't make another the one reported.
struct CNoLintPrerequisite
{
	static constexpr int mc_Count = 4; // NOLINT(malterlib-member-order): CArray uses it
	using CArray = int[mc_Count];
	CArray m_Value;
};
struct CNoLintNextLinePrerequisite
{
	// NOLINTNEXTLINE(malterlib-*): CArray uses it
	static constexpr int mc_Count = 4;
	using CArray = int[mc_Count];
	CArray m_Value;
};

struct CNoLintBlockPrerequisite
{
	// NOLINTBEGIN(malterlib-member-order): CArray uses it
	static constexpr int mc_Count = 4;
	// NOLINTEND(malterlib-member-order)
	using CArray = int[mc_Count];
	CArray m_Value;
};
// But not one a block of another check covers.
struct COtherCheckBlock
{
	// NOLINTBEGIN(misc-other)
	static constexpr int mc_Count = 4;
	// NOLINTEND(misc-other)
	using CArray = int[mc_Count];
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: type 'CArray' should come before the constants of its section; a section has types, constructors and destructor, functions, constants and variables, in that order [malterlib-member-order]
	CArray m_Value;
};

// An unnamed bit-field is no member.
class CUnnamedBitField
{
	unsigned : 0;

public:
	CUnnamedBitField();
};
struct CUnnamedBitFieldFirst
{
	int : 0;
	void f_Run();
};

// A NOLINT of a glob of clang-tidy covers it too.
struct CNoLintGlobPrerequisite
{
	static constexpr int mc_Count = 4; // NOLINT(*-member-order): CArray uses it
	using CArray = int[mc_Count];
	CArray m_Value;
};
struct CNoLintInfixGlobBlock
{
	// NOLINTBEGIN(malterlib-*-order): CArray uses it
	static constexpr int mc_Count = 4;
	// NOLINTEND(malterlib-*-order)
	using CArray = int[mc_Count];
	CArray m_Value;
};

// A static member whose type depends on the parameters of its template can be
// a constant or a variable as it is instantiated, and isn't ordered.
template <typename t_CType>
struct TCDependentStatic
{
	int m_Value;
	static t_CType const ms_Maybe;
	static t_CType ms_Other;
	static constexpr int mc_Value = 1;
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: constant 'mc_Value' should come before the variables of its section; a section has types, constructors and destructor, functions, constants and variables, in that order [malterlib-member-order]
};
template struct TCDependentStatic<int &>;

// A using declaration of what depends on the parameters is what the
// instantiations make of it, not ordered when there is none.
struct CRunBase
{
	void f_Run();
};
template <typename t_CBase>
struct TCUsesBaseRun : t_CBase
{
	int m_Value;
	using t_CBase::f_Run;
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: function 'f_Run' should come before the variables of its section; a section has types, constructors and destructor, functions, constants and variables, in that order [malterlib-member-order]
};
template struct TCUsesBaseRun<CRunBase>;
template <typename t_CBase>
struct TCUninstantiatedUsesBase : t_CBase
{
	int m_Value;
	using t_CBase::f_Run;
};

// A static member of a class template declared without its value, or a member
// variable template, isn't ordered: only what the template declares decides,
// not its definitions or specializations.
int fg_ReadValue();
template <typename t_CType>
struct TCSpecializedStatic
{
	int m_Value;
	static int const ms_Value;
};
template <>
int const TCSpecializedStatic<int>::ms_Value = fg_ReadValue();
template struct TCSpecializedStatic<int>;

// Also one a using declaration brings in from a specialization that doesn't
// depend on the parameters, as its declaration in the class template says.
template <typename t_CType>
struct TCImportedStaticBase
{
	static int const ms_Value;
	static constexpr int mc_Value = 1;
};
struct CImportsTemplateStatic : TCImportedStaticBase<int>
{
	int m_Value;
	using TCImportedStaticBase<int>::ms_Value;
	using TCImportedStaticBase<int>::mc_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:35: warning: constant 'mc_Value' should come before the variables of its section; a section has types, constructors and destructor, functions, constants and variables, in that order [malterlib-member-order]
};

// A member class of an instantiation of its class, also an explicit one, is
// checked as written, where a using declaration of what depends on the
// parameters isn't ordered: only in a class template's own pattern is it what
// the instantiations agree on.
struct CItemFunction
{
	void f_Item();
};
struct CItemField
{
	int f_Item;
};
template <typename t_CType>
struct TCOuterOfInner
{
	struct CInner : t_CType
	{
		int m_Value;
		using t_CType::f_Item;
	};
};
template struct TCOuterOfInner<CItemFunction>;
template struct TCOuterOfInner<CItemField>;
template <typename t_CType>
struct TCOuterOfAgreeing
{
	struct CInner : t_CType
	{
		int m_Value;
		using t_CType::f_Item;
	};
};
template struct TCOuterOfAgreeing<CItemFunction>;

// An enumerator a using declaration brings in, or a using enum declaration,
// is a constant.
enum class EUsingKind
{
	EUsingKind_First
};
struct CUsingEnumerator
{
	int m_Value;
	using EUsingKind::EUsingKind_First;
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: constant 'EUsingKind_First' should come before the variables of its section; a section has types, constructors and destructor, functions, constants and variables, in that order [malterlib-member-order]
};
struct CUsingEnum
{
	int m_Value;
	using enum EUsingKind;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: constant 'EUsingKind' should come before the variables of its section; a section has types, constructors and destructor, functions, constants and variables, in that order [malterlib-member-order]
};
template <typename t_CType>
struct TCConstexprPrimary
{
	int m_Value;
	static int const ms_Value;
};
template <typename t_CType>
constexpr int TCConstexprPrimary<t_CType>::ms_Value = 1;
template <>
int const TCConstexprPrimary<int>::ms_Value = fg_ReadValue();
template struct TCConstexprPrimary<int>;
struct CMemberVariableTemplate
{
	int m_Value;
	template <typename t_CType>
	static int const ms_Value;
	template <typename t_CType>
	static constexpr int mc_Value = 1;
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: constant 'mc_Value' should come before the variables of its section; a section has types, constructors and destructor, functions, constants and variables, in that order [malterlib-member-order]
};
template <>
int const CMemberVariableTemplate::ms_Value<int> = fg_ReadValue();
// An out-of-class constexpr definition doesn't make one given a value that
// isn't constexpr in the class a constant.
template <typename t_CType>
struct TCInitializedStatic
{
	int m_Value;
	static int const ms_Value = 1;
};
template <typename t_CType>
constexpr int TCInitializedStatic<t_CType>::ms_Value;
struct CInitializedVariableTemplate
{
	static int ms_First;
	template <typename t_CType>
	static int const ms_Value = 1;
};
template <typename t_CType>
constexpr int CInitializedVariableTemplate::ms_Value;
// Nor one a using declaration brings in, which is as where it is declared.
struct CVariableTemplateBase
{
	template <typename t_CType>
	static int const ms_Value;
};
struct CUsesVariableTemplate : CVariableTemplateBase
{
	int m_Value;
	using CVariableTemplateBase::ms_Value;
};

// A friend that only declares, also a function or a template, goes first in the
// private section, before its types; of a class, also in the private section
// it starts with.
template <typename t_CType>
struct TCFriend;
void fg_Friend();
struct CFriendFirst
{
	void f_Run();

private:
	friend struct CFriend;
	friend void fg_Friend();
	template <typename t_CType>
	friend struct TCFriend;
	using CType = int;
	int mp_Value;
};
class CFriendLeading
{
	friend struct CFriend;

public:
	void f_Run();

private:
	int mp_Value;
};
// Reported elsewhere.
struct CFriendMisplaced
{
	friend struct CFriend;
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: friend declaration should come first in the private section, before its types; a friend that only declares goes first in the private section, or the protected one when there are no private members [malterlib-member-order]
	void f_Run();

private:
	using CType = int;
	friend void fg_Friend();
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: friend declaration should come first in the private section, before its types; a friend that only declares goes first in the private section, or the protected one when there are no private members [malterlib-member-order]
	int mp_Value;
};
// In the protected section when there are no private members.
struct CFriendProtected
{
	void f_Run();

protected:
	friend struct CFriend;
	int mp_Value;
};
struct CFriendProtectedMisplaced
{
	friend struct CFriend;
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: friend declaration should come first in the protected section, before its types; a friend that only declares goes first in the private section, or the protected one when there are no private members [malterlib-member-order]

protected:
	int mp_Value;
};
// A friend a macro writes, or one that defines a function in the class, a
// hidden friend, isn't ordered.
#define DMacroFriend friend struct CMacroFriend;
struct CFriendsUnordered
{
	DMacroFriend
	friend bool operator==(CFriendsUnordered const &, CFriendsUnordered const &) = default;
	friend void fg_Defined(CFriendsUnordered &) {}
	void f_Run();

private:
	int mp_Value;
};
// A friend class, also a template, in a class with no private or protected
// members of its own, grants nothing. Not a friend function, which can be
// what makes the function found, by argument-dependent lookup, and is left
// unordered there.
struct CFriendNothing
{
	friend struct CFriend;
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: friend declaration grants nothing; 'CFriendNothing' has no private or protected members [malterlib-member-order]
	template <typename t_CType>
	friend struct TCFriend;
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: friend declaration grants nothing; 'CFriendNothing' has no private or protected members [malterlib-member-order]
	void f_Run();
	friend void fg_Friend();
	friend void fg_FoundByLookup(CFriendNothing const &_Value);
	template <typename tf_CType>
	friend void fg_FoundByLookupTemplate(CFriendNothing const &_Value, tf_CType _Other);
};
// Only of a class with no bases at all: any base, also an empty public one,
// leaves its friends unreported.
struct CProtectedBase
{
protected:
	int mp_Value;
};
struct CFriendThroughBase : CProtectedBase
{
	friend struct CFriend;
};
class CFriendPrivateBase : CRunBase
{
	friend struct CFriend;

public:
	void f_Other();
};
template <typename t_CBase>
struct TCFriendDependentBase : t_CBase
{
	friend struct CFriend;
};
struct CEmptyPublicBase
{
};
struct CFriendEmptyBase : CEmptyPublicBase
{
	friend struct CFriend;
	void f_Run();
};
// A partial or explicit specialization of a member template has no access of
// its own: it doesn't make a class have private members, and a friend first
// in its protected section is in place.
struct CFriendProtectedTemplates
{
	void f_Run();

protected:
	friend struct CFriend;
	template <typename t_CType>
	struct TCInner;
	template <typename t_CType>
	struct TCInner<t_CType *>
	{
	};
	template <>
	struct TCInner<int>
	{
	};
};

// A static member a using declaration brings in is classified by its
// declaration in its class, also when what it leads to is the definition out
// of it, of an instantiation used before, or of a member variable template.
template <typename t_CType>
struct TCDefinedStaticBase
{
	static int const ms_Value;
};
template <typename t_CType>
constexpr int TCDefinedStaticBase<t_CType>::ms_Value = 1;
int g_ReadDefinedStatic = TCDefinedStaticBase<int>::ms_Value;
struct CImportsDefinedStatic : TCDefinedStaticBase<int>
{
	int m_Value;
	using TCDefinedStaticBase<int>::ms_Value;
};
struct CVariableTemplateDefinedBase
{
	template <typename t_CType>
	static int const ms_Value;
};
template <typename t_CType>
constexpr int CVariableTemplateDefinedBase::ms_Value = 1;
struct CImportsDefinedVariableTemplate : CVariableTemplateDefinedBase
{
	int m_Value;
	using CVariableTemplateDefinedBase::ms_Value;
};
// A partial specialization of a member variable template is classified as the
// template is: declared without its value, it isn't ordered.
struct CPartialVariableTemplate
{
	int m_Value;
	template <typename t_CType>
	static int const ms_Value;
	template <typename t_CType>
	static int const ms_Value<t_CType *>;
	template <typename t_CType>
	static int ms_Other;
	template <typename t_CType>
	static int ms_Other<t_CType *>;
};

// A NOLINTNEXTLINE covers the line after it, not the member after a blank
// line, and one before a member written in the first column.
struct CNoLintBlankLine
{
	// NOLINTNEXTLINE(malterlib-member-order)

	static constexpr int mc_Early = 1;
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: constant 'mc_Early' should come after the functions of its section; a section has types, constructors and destructor, functions, constants and variables, in that order [malterlib-member-order]
	using CType = int;
	void f_Run();
	int m_Value;
};
struct CNoLintColumnOne
{
	int
// NOLINTNEXTLINE(malterlib-member-order)
m_Value;
	static constexpr int mc_After = 1;
};

// An explicit specialization of a member function template doesn't make a
// class have private members, where it is written.
struct CFriendProtectedFunctionTemplate
{
	void f_Run();

protected:
	friend struct CFriend;
	template <typename t_CType>
	void fp_Run();

private:
	template <>
	void fp_Run<int>();
};

// Nor a partial or explicit specialization of a member variable template.
struct CFriendProtectedVariableTemplate
{
	void f_Run();

protected:
	friend struct CFriend;
	template <typename t_CType>
	static int const ms_Value;
	template <typename t_CType>
	static int const ms_Partial;

private:
	template <>
	constexpr int ms_Value<int> = 2;
	template <typename t_CType>
	static int const ms_Partial<t_CType *>;
};

// The copy an instantiation of its class makes of an explicit specialization
// written in a class template isn't checked: the specialization is, as
// written.
template <typename t_CType>
struct TCOuterOfSpecialized
{
	template <typename t_CInner>
	struct TCInner;
	template <>
	struct TCInner<int> : t_CType
	{
		int m_Value;
		using t_CType::f_Item;
	};
};
template struct TCOuterOfSpecialized<CItemFunction>;
template struct TCOuterOfSpecialized<CItemField>;

// A member variable template a using declaration brings in from an
// instantiation of a class template is classified as its class template
// declares it.
template <typename t_CType>
struct TCVariableTemplateBase
{
	template <typename t_CValue>
	static int const ms_Value = 1;
};
struct CImportsInstantiatedVariableTemplate : TCVariableTemplateBase<int>
{
	using TCVariableTemplateBase<int>::ms_Value;
	void f_Run();
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: function 'f_Run' should come before the variables of its section; a section has types, constructors and destructor, functions, constants and variables, in that order [malterlib-member-order]
};

// Also the copies of one written in another written in a class template,
// whose copy is in a copy.
template <typename t_CType>
struct TCOuterOfDeep
{
	template <typename t_CMiddle>
	struct TCMiddle;
	template <>
	struct TCMiddle<int>
	{
		template <typename t_CDeep>
		struct TCDeep;
		template <>
		struct TCDeep<int> : t_CType
		{
			int m_Value;
			using t_CType::f_Item;
		};
	};
};
template struct TCOuterOfDeep<CItemFunction>;
template struct TCOuterOfDeep<CItemField>;

// Nor in an explicit specialization written in a class template, whose copies
// agree here.
template <typename t_CType>
struct TCOuterOfCollected
{
	template <typename t_CInner>
	struct TCInner;
	template <>
	struct TCInner<int> : t_CType
	{
		int m_Value;
		using t_CType::f_Item;
	};
};
template struct TCOuterOfCollected<CItemFunction>;

// A tag that an elaborated type specifier in a member introduces, `struct
// COther *m_pOther;`, is of the namespace around the class, not a member.
struct CElaboratedMember
{
	void f_Run();
	struct CElaboratedOther *m_pOther;
};

namespace NReview2Round203ReferenceConstants {
// Round 203 (llvm-naming-struct-4, -6, -7): a static reference to const
// declared without its value may be a constant, defined constexpr in another
// source, as variable-naming takes it. Reported: a reference to what isn't
// const, a variable.
struct CStr { int m_Len; };
struct CLookup { static CStr const &mc_Empty; static constexpr unsigned mc_MaxLength = 256; unsigned m_nEntries; };
struct CText { static CStr const &mc_Empty; static constexpr int mc_Count = 2; };
struct CMutable { static CStr &ms_Shared; static constexpr unsigned mc_MaxLength = 256; unsigned m_nEntries; };
// CHECK-MESSAGES: :[[@LINE-1]]:69: warning: constant 'mc_MaxLength' should come before the variables of its section; a section has types, constructors and destructor, functions, constants and variables, in that order [malterlib-member-order]
} // namespace NReview2Round203ReferenceConstants

// Round 218: an order decided by what the instantiations make of a using
// declaration names the first of them.
namespace NRound218Order
{
	struct CBase
	{
		int m_Value;
	};
	template <typename t_CBase>
	struct TCDerived : t_CBase
	{
		using t_CBase::m_Value;
		void f_Run();
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: function 'f_Run' should come before the variables of its section; a section has types, constructors and destructor, functions, constants and variables, in that order [malterlib-member-order]
// CHECK-MESSAGES: :{{[0-9]+}}:{{[0-9]+}}: note: in instantiation of template class 'NRound218Order::TCDerived<NRound218Order::CBase>' requested here
	};
	TCDerived<CBase> g_Derived;
}

// Round 219: the order of sections is the order of the labels written in the
// class; a label or member a macro writes, like Qt's `Q_OBJECT`, which ends in
// `private:`, is neither reported nor what the next written one is compared
// with. A label written as an argument of a macro is written.
namespace NRound219Macro
{
#define DQObject                                                               \
public:                                                                        \
	static int const ms_Meta;                                                  \
	virtual int f_Meta() const;                                                \
                                                                               \
private:                                                                       \
	struct CPrivateSignal                                                      \
	{                                                                          \
	};
#define DPrivateCount int m_Count;
#define DSection(d_Label) d_Label
#define DPublicLabel public:

	struct CWidget
	{
		DQObject
	public:
		CWidget();

	protected:
		void fp_Run();

	private:
		int m_Value;
	};
	struct CWrong
	{
	private:
		int m_Value;
		DQObject
	public:
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: the public section should come before the private section; a class has its public, protected and private sections in that order [malterlib-member-order]
		CWrong();
	};
	class CView
	{
		DQObject
	public:
		CView();
	};
	class CView2
	{
		DQObject
	private:
		int m_Value;

	public:
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: the public section should come before the private section; a class has its public, protected and private sections in that order [malterlib-member-order]
		CView2();
	};
	struct CWrittenAfterMacro
	{
		DQObject
		int m_Value;

	public:
		CWrittenAfterMacro();
	};
	class CCounter
	{
		DPrivateCount
	public:
		CCounter();
	};
	class CCounter2
	{
		int m_Count;

	public:
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: the public section should come before the private section; a class has its public, protected and private sections in that order [malterlib-member-order]
		CCounter2();
	};
	struct CDialog
	{
		DQObject
		friend struct CDialogTester;

	private:
		int m_Value;
	};
	struct CDialog2
	{
		DQObject
	private:
		int m_Value;
		friend struct CDialogTester;
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: friend declaration should come first in the private section, before its types; a friend that only declares goes first in the private section, or the protected one when there are no private members [malterlib-member-order]
	};
	struct CArgument
	{
	private:
		int m_Value;
		DSection(public):
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: the public section should come before the private section; a class has its public, protected and private sections in that order [malterlib-member-order]
		CArgument();
	};
	struct CBody
	{
	private:
		int m_Value;
		DPublicLabel
		CBody();
	};
}
