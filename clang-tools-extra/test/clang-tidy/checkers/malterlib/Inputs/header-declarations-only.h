void fg_Declaration();

inline void fg_Inline()
{
}

struct CStruct
{
	CStruct() = default;
	void f_Declaration();
	void f_Defined()
	{
	}
	bool operator == (CStruct const &) const = default;
	void f_Deleted() = delete;

	int m_Value = []
		{
			return 1;
		}
		()
	;
};

template <typename t_CType>
constexpr t_CType fg_Template(t_CType _Value)
{
	return _Value;
}

#define DDefineFunction(d_Name) inline void d_Name() {}
DDefineFunction(fg_FromMacro)

void fg_NoLint() // NOLINT(malterlib-header-declarations-only)
{
}

// A body written in an argument of a macro is written here.
#define DFunctionWithBody(d_Name, d_Body) inline void d_Name() d_Body
DFunctionWithBody(fg_BodyArgument, {
	int Value = 1;
	(void)Value;
})

// A macro that gives only the start of a body leaves the rest written here.
#define DOpenBody {
inline int fg_OpenedByMacro() DOpenBody
	return 1;
}
#define DCloseBody }
inline int fg_BracedByMacros() DOpenBody
	return 1;
DCloseBody
#define DDefineWithCode(d_Name, d_Code) inline int d_Name() { d_Code }
DDefineWithCode(fg_CodeArgument, return 1;)
#define DTryBody(d_Body, d_Handler) try { d_Body } catch (...) { d_Handler }
inline int fg_TryArgument() DTryBody(return 1;, return 0;)
#include "malterlib-concurrency.h"
#define DDefineCoroutine(d_Name, d_Code) inline NMib::NConcurrency::TCFuture<int> d_Name() { d_Code }
DDefineCoroutine(fg_CoroutineArgument, co_return 1;)
#define DDefineNested(d_Name, d_Code) inline int d_Name() { if (true) { d_Code } return 0; }
DDefineNested(fg_NestedArgument, return 1;)
#define DEmptyBody {}
int fg_CreateValue();
struct CInitializedInHeader
{
	int m_Value;
	CInitializedInHeader() : m_Value(fg_CreateValue()) DEmptyBody
};
#define DCtor(d_Init) CInitializedInArgument() : d_Init {}
struct CInitializedInArgument
{
	int m_Value;
	DCtor(m_Value(fg_CreateValue()))
};
// A body that an argument forwards from a macro of a .hpp file.
#include "header-declarations-only-body.hpp"
DFunctionWithBody(fg_Round59Forwarded, DRound59Body)
// Only what initializes the member written in an argument of the macro.
#define DCtorValue(d_Value) CInitializedInValue() : m_Value(d_Value) {}
struct CInitializedInValue
{
	int m_Value;
	DCtorValue(fg_CreateValue())
};
// Also a part of what initializes the member, `m_Value(1 + d_Value)`.
#define DCtorPart(d_Value) CInitializedInPart() : m_Value(1 + d_Value) {}
struct CInitializedInPart
{
	int m_Value;
	DCtorPart(fg_CreateValue())
};
// Types that arguments of the macro name aren't implementation, the base of
// what it defines, also qualifying what its bodies call.
struct CRound343Parent
{
	CRound343Parent(int _Value);
	static int f_Value();
	int f_Member();
};
#define DRound343Derived(d_CClass, d_CParent) struct d_CClass : public d_CParent { d_CClass() : d_CParent(d_CParent::f_Value()) {} int f_Member() { return d_CParent::f_Member(); } };
DRound343Derived(CRound343Derived, CRound343Parent)
// Nor an operator an argument of the macro names, `==`, whose bodies the macro
// writes.
template <typename t_CType> struct TCRound354Reference
{
	t_CType *m_pValue;
};
#define DRound354Operator(d_Operator) \
	template <typename t_CType> bool operator d_Operator(TCRound354Reference<t_CType> const &_Left, TCRound354Reference<t_CType> const &_Right) { return *_Left.m_pValue d_Operator *_Right.m_pValue; }
DRound354Operator(==)
DRound354Operator(<)
