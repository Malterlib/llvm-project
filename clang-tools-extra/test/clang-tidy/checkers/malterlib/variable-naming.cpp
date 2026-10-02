// RUN: %check_clang_tidy -std=c++20-or-later %s malterlib-variable-naming %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -I %S/Inputs

int g_Global;
int const gc_Constant = 1;
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: global variable 'gc_Constant' has the constant prefix 'gc_' but isn't constexpr; declare it constexpr, or use the 'g_' prefix [malterlib-variable-naming]
constexpr int gc_ConstExpr = 2;
static int gs_Static;
static int const gc_StaticConstant = 3;
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: static global variable 'gc_StaticConstant' has the constant prefix 'gc_' but isn't constexpr; declare it constexpr, or use the 'gs_' prefix [malterlib-variable-naming]
// A const one defined in another translation unit is constexpr if its
// definition is, which is checked there.
extern int const gc_ExternConstant;
extern int const g_ExternConstantRunTime;
int fg_RunTime();
int const gc_RunTimeConstant = fg_RunTime();
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: global variable 'gc_RunTimeConstant' has the constant prefix 'gc_' but isn't constexpr; declare it constexpr, or use the 'g_' prefix [malterlib-variable-naming]
int const g_RunTimeConstant = fg_RunTime();
double const gc_Double = 3.14;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: global variable 'gc_Double' has the constant prefix 'gc_' but isn't constexpr; declare it constexpr, or use the 'g_' prefix [malterlib-variable-naming]

namespace
{
	int g_Anonymous;
}

int Global;
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: global variable 'Global' should have the 'g_' prefix [malterlib-variable-naming]
static int g_Static;
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: static global variable 'g_Static' should have the 'gs_' prefix [malterlib-variable-naming]
constexpr int g_ConstExpr = 1;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: global constant 'g_ConstExpr' should have the 'gc_' prefix [malterlib-variable-naming]
int g_lower;
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: global variable 'g_lower' should be upper camel case after its prefix [malterlib-variable-naming]

struct CStruct
{
	int m_Member;
	bool m_bFlag;
	static int ms_Static;
	static constexpr int mc_Constant = 1;
	static int const mc_ConstantDefinedLater;
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: static member variable 'mc_ConstantDefinedLater' has the constant prefix 'mc_' but isn't constexpr; declare it constexpr, or use the 'ms_' prefix [malterlib-variable-naming]
	static int const ms_ConstantDefinedLater;
	int mp_Public;
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: member variable 'mp_Public' should have the 'm_' prefix [malterlib-variable-naming]
	int Member;
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: member variable 'Member' should have the 'm_' prefix [malterlib-variable-naming]
	static constexpr int ms_Constant = 1;
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: static member variable 'ms_Constant' should have the 'mc_' prefix [malterlib-variable-naming]

	union
	{
		int m_UnionMember;
	};

private:
	int mp_Private;
	static int msp_Static;
	static constexpr int mcp_Constant = 1;
	int m_Private;
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: private or protected member variable 'm_Private' should have the 'mp_' prefix [malterlib-variable-naming]
	static int ms_Private;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: private or protected static member variable 'ms_Private' should have the 'msp_' prefix [malterlib-variable-naming]

	union
	{
		int mp_PrivateUnionMember;
	};
};

int CStruct::ms_Static = 0;

void fg_Function(int _Value)
{
	int Local = 0;
	bool bLocal = false;
	int *pLocal = nullptr;
	int const RunTime = _Value;
	int const c_Constant = 1;
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: local variable 'c_Constant' has the constant prefix 'c_' but isn't constexpr; declare it constexpr, or name it without a storage prefix [malterlib-variable-naming]
	int const NotMarkedConstant = 1;
	constexpr int c_ConstExpr = 1;
	static int s_Static = 0;
	static constexpr int c_StaticConstExpr = 1;
	static int const c_StaticConstant = 1;
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: static local variable 'c_StaticConstant' has the constant prefix 'c_' but isn't constexpr; declare it constexpr, or use the 's_' prefix [malterlib-variable-naming]
	static int const s_StaticConstant = 1;

	int local = 0;
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: local variable 'local' should be upper camel case [malterlib-variable-naming]
	int m_Local = 0;
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: local variable 'm_Local' should not have a storage prefix [malterlib-variable-naming]
	constexpr int ConstExpr = 1;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: local variable 'ConstExpr' should have the 'c_' prefix [malterlib-variable-naming]
	static int Static = 0;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: static local variable 'Static' should have the 's_' prefix [malterlib-variable-naming]
	int const c_RunTime = _Value;
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: local variable 'c_RunTime' has the constant prefix 'c_' but isn't constexpr; declare it constexpr, or name it without a storage prefix [malterlib-variable-naming]

	for (int iValue = 0; iValue < 10; ++iValue)
	{
	}
	for (int i = 0; i < 10; ++i)
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: local variable 'i' should be upper camel case [malterlib-variable-naming]
	{
	}

	int Array[2] = {1, 2};
	auto [First, Second] = Array;
	auto [first, second] = Array;
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: structured binding 'first' should be upper camel case [malterlib-variable-naming]
// CHECK-MESSAGES: :[[@LINE-2]]:15: warning: structured binding 'second' should be upper camel case [malterlib-variable-naming]

	try
	{
	}
	catch (int const &_Exception)
	{
	}
	catch (long const &Exception)
// CHECK-MESSAGES: :[[@LINE-1]]:21: warning: exception variable 'Exception' should have the '_' prefix [malterlib-variable-naming]
	{
	}

	auto fLambda = [Captured = _Value, m_Bad = _Value]
// CHECK-MESSAGES: :[[@LINE-1]]:37: warning: local variable 'm_Bad' should not have a storage prefix [malterlib-variable-naming]
		{
			return Captured + m_Bad;
		}
	;
	fLambda();
}

template <typename ...tfp_CParams>
void fg_Pack(tfp_CParams ...p_Params)
{
	// The captures of a pack are locals, not parameters.
	auto fLambda = [...p_Captured = p_Params]
// CHECK-MESSAGES: :[[@LINE-1]]:21: warning: local variable 'p_Captured' should not have a storage prefix [malterlib-variable-naming]
		{
		}
	;
}

template <typename t_CType>
struct TCTemplate
{
	t_CType m_Value;
	static constexpr bool mc_bValue = true;
};

template <typename t_CType>
constexpr bool gc_bTemplate = true;

TCTemplate<int> g_Instance;

enum
{
	mc_NotAVariable
};

extern "C" int c_variable;

constexpr int &gc_Reference = g_Global;

void fg_ConstexprReference()
{
	static constexpr int &c_StaticReference = g_Global;
	constexpr int &c_Reference = g_Global;
	try
	{
	}
	catch (int const _ConstException)
	{
	}
}

int NoLint; // NOLINT(malterlib-variable-naming)

struct CBindingPair
{
	int m_First;
	int m_Second;
};
CBindingPair g_BindingPair;

// Structured bindings are named for the storage of what they decompose.
auto [g_First, Second] = g_BindingPair;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: global structured binding 'Second' should have the 'g_' prefix [malterlib-variable-naming]

void fg_StaticBindings()
{
	static auto [s_First, s_Second] = g_BindingPair;
	auto [First, Second2] = g_BindingPair;
	(void)First;
	(void)Second2;
}

int g_Group_Value;
int g_Group_value;
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: global variable 'g_Group_value' should be upper camel case after its prefix [malterlib-variable-naming]

// Only the outer concept is a prefix.
bool *g_pbOuter = nullptr;
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: global variable 'g_pbOuter' has the concept prefixes 'pb', of which only the outermost, 'p', is written [malterlib-variable-naming]
// A lower case letter that isn't a concept prefix is told as such.
struct CRound345Parser
{
private:
	int mp_lParses = 0;
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: private or protected member variable 'mp_lParses' has the prefix 'l', which isn't a concept prefix; remove it or use 'b', 'i', 'n', 'p', 'f' or 'r' [malterlib-variable-naming]
};

// A block scope `extern` declares a global.
void fg_Externs()
{
	extern int g_ExternGlobal;
	extern int Bad;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: global variable 'Bad' should have the 'g_' prefix [malterlib-variable-naming]
}

// A name doesn't end with an underscore.
int g_Value_;
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: global variable 'g_Value_' should be upper camel case after its prefix [malterlib-variable-naming]

// A name has at least two characters after its prefixes.
void fg_ShortNames()
{
	[[maybe_unused]] int iP = 0;
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: local variable 'iP' should have at least 2 characters after its prefixes [malterlib-variable-naming]
	[[maybe_unused]] int X = 0;
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: local variable 'X' should have at least 2 characters after its prefixes [malterlib-variable-naming]
	[[maybe_unused]] int iByte = 0;
	[[maybe_unused]] int ID = 0;
}


// A structured binding that is only const isn't a constant.
struct CRuntimePair
{
	int m_First;
	int m_Second;
};
CRuntimePair fg_LoadRuntimePair();
void fg_RuntimeBindings()
{
	const auto [c_First, c_Second] = fg_LoadRuntimePair();
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: structured binding 'c_First' has the constant prefix 'c_' but isn't constexpr; declare it constexpr, or name it without a storage prefix [malterlib-variable-naming]
// CHECK-MESSAGES: :[[@LINE-2]]:23: warning: structured binding 'c_Second' has the constant prefix 'c_' but isn't constexpr; declare it constexpr, or name it without a storage prefix [malterlib-variable-naming]
	const auto [First, Second] = fg_LoadRuntimePair();
	(void)First;
	(void)Second;
	(void)c_First;
	(void)c_Second;
}

// A constexpr definition after a declaration that isn't is a constant,
// checked where it is defined.
extern const int g_LaterConstExpr;
constexpr int g_LaterConstExpr = 1;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: global constant 'g_LaterConstExpr' should have the 'gc_' prefix [malterlib-variable-naming]

// A reference declared before its constexpr definition.
int g_Referenced;
extern int &gc_LaterReference;
constexpr int &gc_LaterReference = g_Referenced;

// The members of an anonymous union outside of a class are variables of the
// scope it is in.
void fg_LocalUnion()
{
	union
	{
		int Value;
		bool bFlag;
	};
	Value = 1;
}
static union
{
	int gs_UnionValue;
};

// The members of a static anonymous union in a function are static locals.
void fg_StaticAnonymousUnion() {
	static union {
		int s_Value;
		int Other;
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: static local variable 'Other' should have the 's_' prefix [malterlib-variable-naming]
	};
	s_Value = Other;
}

// A name given to a macro in the body of another is reported in that body.
#define DRound92Declare(d_Name) int d_Name;
#define DRound92Fields DRound92Declare(Round92Wrong)
// CHECK-MESSAGES: :[[@LINE-1]]:40: warning: member variable 'Round92Wrong' should have the 'm_' prefix [malterlib-variable-naming]
struct CRound92First
{
	DRound92Fields
};
struct CRound92Second
{
	DRound92Fields
};

// Explicit specializations of variable templates are checked, and a constant
// prefix needs constexpr there too.
template <typename t_CType>
inline constexpr int gc_Round93Value = 42;

template <>
inline int gc_Round93Value<void> = 42;
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: global variable 'gc_Round93Value<void>' has the constant prefix 'gc_' but isn't constexpr; declare it constexpr, or use the 'g_' prefix [malterlib-variable-naming]

template <>
inline constexpr int gc_Round93Value<int> = 42;

// A specialization of a variable template of a system header is named by it.
# 1 "ranges" 1 3
namespace std::ranges
{
	template <typename t_CRange>
	inline constexpr bool enable_borrowed_range = false;
}
# 316 "variable-naming.cpp" 2
struct CRound107Range
{
};
template <>
inline constexpr bool std::ranges::enable_borrowed_range<CRound107Range> = true;

// An anonymous union in an anonymous namespace isn't static.
namespace
{
	union
	{
		int g_Round109Value;
	};
}

static union
{
	int gs_Round109Value;
};

// Constexpr on the definition of a static member initialized in its class,
// and a const global declared before it is defined, which is checked where
// it is defined.
struct CRound111Values
{
	static const int mc_Value = 42;
	static const int ms_Value = 42;
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: static member variable 'ms_Value' should have the 'mc_' prefix [malterlib-variable-naming]
};
constexpr int CRound111Values::mc_Value;
constexpr int CRound111Values::ms_Value;
extern const int gc_Round111Declared;
const int gc_Round111Declared = 42;
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: global variable 'gc_Round111Declared' has the constant prefix 'gc_' but isn't constexpr; declare it constexpr, or use the 'g_' prefix [malterlib-variable-naming]
extern const int gc_Round111Constexpr;
constexpr int gc_Round111Constexpr = 42;
extern const int g_Round111Plain;
const int g_Round111Plain = 42;

// An explicit specialization of a variable template has the storage of the template.
namespace NRound115Specialization {
template <typename t_CType>
static int gs_Value = 0;
template <>
int gs_Value<int> = 1;
}

// Round 120: the identifier of a locale facet, which the standard library
// looks up by its name.
namespace std {
struct locale {
  struct id {};
  struct facet {};
};
}

namespace NRound120Facet {
struct CFacet : std::locale::facet {
  static std::locale::id id;
};
std::locale::id CFacet::id;
}

// Round 121: a constexpr definition of what a header that isn't checked
// declares const is checked where it is defined.
#include "variable-naming-extern.h"
constexpr int g_Round121Value = 42;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: global constant 'g_Round121Value' should have the 'gc_' prefix [malterlib-variable-naming]

// Round 126: a name made by token pasting isn't checked, like the names macros
// make for themselves.
#define DRound126Declare(d_Name) int Global##d_Name;
DRound126Declare(Value)

// Round 134: a reference declared here and defined elsewhere is named where
// it is defined, which can be constexpr.
namespace NRound134
{
extern int g_Value;
extern int &gc_Reference;
int fg_Read() { return gc_Reference; }
}

namespace NRound160 {
// A specialization of a static member that is constexpr where the member
// isn't, and the other way.
template <typename t_CType>
struct TCBox {
  static const int ms_Value;
  static constexpr int mc_Other = 1;
};
template <> constexpr int TCBox<int>::ms_Value = 2;
// CHECK-MESSAGES: :[[@LINE-1]]:39: warning: static member variable 'ms_Value' should have the 'mc_' prefix [malterlib-variable-naming]
template <> const int TCBox<int>::mc_Other = 2;
// CHECK-MESSAGES: :[[@LINE-1]]:35: warning: static member variable 'mc_Other' has the constant prefix 'mc_' but isn't constexpr; declare it constexpr, or use the 'ms_' prefix [malterlib-variable-naming]
}
