// RUN: %check_clang_tidy %s malterlib-unused-parameter-cast %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}"
// A cast written across files isn't removed by offsets of one of them.
// RUN: rm -rf %t.span && mkdir -p %t.span
// RUN: printf '//xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\n_Value;\n' > %t.span/param2.hpp
// RUN: printf 'void fg_Work();\nvoid fg_Run(int _Value) {\n  (void)\n#include "param2.hpp"\n  fg_Work();\n}\n' > %t.span/main.cpp
// RUN: clang-tidy --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" --checks='-*,malterlib-unused-parameter-cast' -fix %t.span/main.cpp -- -std=c++20 2>&1 | FileCheck --check-prefix=CHECK-SPAN-MESSAGES %s
// RUN: FileCheck --check-prefix=CHECK-SPAN --input-file=%t.span/main.cpp %s
// CHECK-SPAN-MESSAGES: main.cpp:3:3: warning: parameter '_Value' is silenced with a cast to void; leave an unused parameter named, without the cast [malterlib-unused-parameter-cast]
// CHECK-SPAN: {{^}}void fg_Run(int _Value) {
// CHECK-SPAN-NEXT: {{^}}  (void)
// CHECK-SPAN-NEXT: {{^}}#include "param2.hpp"
// CHECK-SPAN-NEXT: {{^}}  fg_Work();

void fg_Unused(int _Value)
{
	(void)_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: parameter '_Value' is silenced with a cast to void; leave an unused parameter named, without the cast [malterlib-unused-parameter-cast]
}
// CHECK-FIXES: void fg_Unused(int _Value)
// CHECK-FIXES-NEXT: {
// CHECK-FIXES-NEXT: //
// CHECK-FIXES-NEXT: }

void fg_Used(int _Value, int _Other)
{
	static_cast<void>(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: parameter '_Value' is silenced with a cast to void; leave an unused parameter named, without the cast [malterlib-unused-parameter-cast]
	int Copy = _Value + _Other;
	(void)Copy;
}
// CHECK-FIXES: void fg_Used(int _Value, int _Other)
// CHECK-FIXES-NEXT: {
// CHECK-FIXES-NEXT: //
// CHECK-FIXES-NEXT: int Copy = _Value + _Other;

void fg_Expression(int _Value)
{
	int Local = ((void)_Value, 5);
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: parameter '_Value' is silenced with a cast to void; leave an unused parameter named, without the cast [malterlib-unused-parameter-cast]
}

struct CMember
{
	CMember(int _Value)
		: m_Value(_Value)
	{
		(void)_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: parameter '_Value' is silenced with a cast to void; leave an unused parameter named, without the cast [malterlib-unused-parameter-cast]
	}
// CHECK-FIXES: CMember(int _Value)

	int m_Value;
};

void fg_Parenthesized(int (_Value))
{
	(void)_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: parameter '_Value' is silenced with a cast to void; leave an unused parameter named, without the cast [malterlib-unused-parameter-cast]
}
// CHECK-FIXES: void fg_Parenthesized(int (_Value))

void fg_Attribute(int _Value [[maybe_unused]])
{
	(void)_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: parameter '_Value' is silenced with a cast to void; leave an unused parameter named, without the cast [malterlib-unused-parameter-cast]
}
// CHECK-FIXES: void fg_Attribute(int _Value {{\[\[}}maybe_unused{{\]\]}})

void fg_Volatile(int volatile &_Register)
{
	(void)_Register;
}

void fg_NoLint(int _Value)
{
	(void)_Value; // NOLINT(malterlib-unused-parameter-cast)
}

// A cast that is the body of an `if` or a loop, or after a label or a statement
// attribute, has no fix, which would leave them without one.
void fg_Unbraced(int _Value, bool _bFlag)
{
	if (_bFlag)
		(void)_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: parameter '_Value' is silenced with a cast to void; {{.*}}
	switch (_Value)
	{
	case 0:
		(void)_bFlag;
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: parameter '_bFlag' is silenced with a cast to void; {{.*}}
		break;
	}
	if (_bFlag) [[likely]]
		(void)_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: parameter '_Value' is silenced with a cast to void; {{.*}}
}

// Naming a parameter of a function around a lambda captures it, like to keep
// it alive in the closure.
struct CSubscription
{
	~CSubscription();
};
void fg_KeepAlive(CSubscription *_pSubscription)
{
	auto fCallback = [=]
		{
			(void)_pSubscription;
		}
	;
	fCallback();
}

void fg_ReturnedCast(int _Value)
{
	return (void)_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: parameter '_Value' is silenced with a cast to void; leave an unused parameter named, without the cast [malterlib-unused-parameter-cast]
}

void fg_CommaCasts(int _Value, int _Other)
{
	(void)_Value, (void)_Other;
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: parameter '_Value' is silenced with a cast to void; leave an unused parameter named, without the cast [malterlib-unused-parameter-cast]
// CHECK-MESSAGES: :[[@LINE-2]]:16: warning: parameter '_Other' is silenced with a cast to void; leave an unused parameter named, without the cast [malterlib-unused-parameter-cast]
}

template <typename ...tp_CValues>
void fg_FoldedCasts(tp_CValues ..._Values)
{
	((void)_Values, ...);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: parameter '_Values' is silenced with a cast to void; leave an unused parameter named, without the cast [malterlib-unused-parameter-cast]
}

// Also in a conditional.
void fg_SilencedInConditional(int _Value, bool _bFlag)
{
	_bFlag ? (void)_Value : (void)0;
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: parameter '_Value' is silenced with a cast to void; leave an unused parameter named, without the cast [malterlib-unused-parameter-cast]
}

// A cast that isn't evaluated, which picks the built-in comma, isn't
// silencing.
struct CRound64Value
{
};
int operator,(CRound64Value const &_Left, int _Right) noexcept(false);
void fg_Round64Query(CRound64Value const &_Value)
{
	static_assert(noexcept(((void)_Value, 0)));
}

// A cast that picks the built-in comma over an overloaded one.
struct CRound74Value
{
};
int operator,(CRound74Value const &_Left, int _Right) = delete;
int fg_Round74Comma(CRound74Value const &_Value)
{
	return ((void)_Value, 42);
}

// A cast on the right of a comma that picks the built-in one.
struct CRound76Value
{
};
void operator,(int _Left, CRound76Value const &_Right) = delete;
int fg_Round76Work();
void fg_Round76Comma(CRound76Value const &_Value)
{
	fg_Round76Work(), (void)_Value;
}

// The cast picks the built-in comma also when only the other side is a class.
struct CRound83Value
{
};
void operator,(int _Left, CRound83Value const &_Right) = delete;
void fg_Round83Comma(int _Value, CRound83Value const &_Other)
{
	(void)_Value, _Other;
}

// A cast in a fold over a comma with a value picks the built-in comma.
struct CRound86Value
{
};
constexpr int operator , (CRound86Value const &_Left, int _Right)
{
	return -1;
}
template <typename ...tp_CTypes>
constexpr int fg_Round86Last(tp_CTypes const &..._Values)
{
	return ((void)_Values, ..., 42);
}
static_assert(fg_Round86Last(CRound86Value{}) == 42);

// Round 134: the last statement of a statement expression, which gives its
// type, isn't removed.
void fg_Round134StatementExpression(int _Value)
{
	return ({ 42; (void)_Value; });
// CHECK-FIXES: {{^}}	return ({ 42; (void)_Value; });
// CHECK-MESSAGES: :[[@LINE-2]]:16: warning: parameter '_Value' is silenced with a cast to void; leave an unused parameter named, without the cast [malterlib-unused-parameter-cast]
}

// Round 135: a cast to void of a cast to void silences it too.
void fg_Round135Twice(int _Value)
{
	(void)(void)_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: parameter '_Value' is silenced with a cast to void; leave an unused parameter named, without the cast [malterlib-unused-parameter-cast]
}

// Round 136: any number of casts to void silence it.
void fg_Round136Thrice(int _Value)
{
	(void)(void)(void)_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: parameter '_Value' is silenced with a cast to void; leave an unused parameter named, without the cast [malterlib-unused-parameter-cast]
}
