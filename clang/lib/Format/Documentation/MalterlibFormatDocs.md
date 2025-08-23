Formatting

# Indentation

Indentation is done with 1 tab character. One tab character must be 4 columns. You should make sure that your editor is not set to expand tabs to spaces.

Tabs are easier to navigate and manipulate in the text editor than spaces.

# Limits

One line should in almost all cases be limited to 190 columns.

Make sure that you have turned on column guides in your favorite text editor.

The column limit should be as wide as possible while still fitting on the screen without scrolling. With a 6 pixel wide font this limit allows small screens down to 1280x720 to fit all columns without scrolling horizontally. On desktop screens 2560 pixels wide or wider this allows fitting code side by side for diffing.

# Whitespace

The standard for the whitespace is designed to be consistent. There should be only one correct way for an expression to be broken up with whitespace.

## Operators

All operators, except '.', '->' and '*' (dereference), have spaces before and after:

```
int a = b * c;

if ((a == b) && (c == d))
{
	d = d * b + c * a;
}
```

The comma operator have a space after but not before:

```
fg_Func(1, 2);
```

## Splitting statements

When splitting a statement across several lines, each substatement on the same logical level needs to always be on it's own line:

```
fg_FuncCall(5, 6, (5 + 9));
```

Is split to:

```
fg_FuncCall
	(
		5
		, 6
		, (5 + 9)
	)
;
```

This makes it easy to follow the logic of large statements with complex logic. See below for examples of specific cases.

When a statement is split across lines, all scope markers have to be on their own lines:

```
fg_FuncCall
	(
		5
		, 6
	)
	.f_Call
	(
		7
		, 8
	)
;

using CType =
	TCTemplate
	<
		int
		, bool
	>
;
```

## Clauses

Every start of a block (i.e. '{') is placed on a new line straight under the word it's connected to, while parenthesis is indented. Every end of a block (i.e. '}') is placed on the same indent level as the start block is placed. There should be a space character between the clause and the parenthesis. Correct example:

```
if (bTest)
{
	Call1();
	Call2();
}
```

Incorrect example:

```
if( bTest ){
	Call1();
	Call2();
}
```

If only one statement is used inside a clause no scope ({}) should be used. Correct example:

```
if (bTest)
	Call1();
```

Incorrect example:

```
if (bTest)
{
	Call1();
}
```

### If

Correct examples:

```
if (bTest)
	fg_FuncCall0();
else
	fg_FuncCall1();
```

```
if (bTest)
{
	fg_FuncCall0();
	fg_FuncCall1();
}
else
{
	fg_FuncCall1();
	fg_FuncCall0();
}
```

```
if
(
	bExprX
	&& (bExprY || bExprZ)
	&& bExprA
)
{
	fg_FuncCall0();
	fg_FuncCall1();
}
```

### For

Correct examples:

```
for (mint iValue = 0; iValue < 5; ++iValue)
	fg_FuncCall0(ValueArray[i]);
```

```
for (mint iValue = 0; iValue < 5; ++iValue)
{
	fg_FuncCall0(ValueArray[i]);
	fg_FuncCall1(ValueArray[i]);
}
```

```
for
(
	mint iValue = 0
	; iValue < 5
	; ++iValue
)
{
	fg_FuncCall0(ValueArray[i]);
	fg_FuncCall1(ValueArray[i]);
}
```


### While

Correct examples:

```
while (bTest)
	fg_FuncCall0();
```

```
while (bTest)
{
	fg_FuncCall0();
	fg_FuncCall1();
}
```

```
while
(
	bExprX
	&& (bExprY || bExprZ)
	&& bExprA
)
{
	fg_FuncCall0();
	fg_FuncCall1();
}
```

### Do While

Correct examples:
```
do
	fg_FuncCall0();
while (bTest);
```

```
do
{
	<statements>;
}
while (bTest);
```

```
do
{
	fg_FuncCall0();
	fg_FuncCall1();
}
while
(
	bExprX
	&& (bExprY || bExprZ)
	&& bExprA
);
```

### Switch

Correct examples:

```
switch (Value)
{
case 1:
	fg_FuncCall0();
	break;
case 2:
	fg_FuncCall1();
	break;
default:
	fg_FuncCall1();
	break;
}
```

```
switch (Value)
{
case 1:
	{
		fg_FuncCall0();
		fg_FuncCall1();
	}
	break;
case 2:
	{
		fg_FuncCall0();
		fg_FuncCall1();
	}
	break;
default:
	{
		fg_FuncCall0();
		fg_FuncCall1();
	}
	break;
}
```

```
switch
(
	Value0
	+ Value1
	+ Value2
)
{
default:
	break;
}
```

Returning the value on the same line as the case statement is ok:

```
switch (Value)
{
case 1: return 5;
case 2: return 6;
default: return 7;
}
```

One statement and then a break is ok:

```
switch (Value)
{
case 1: break;
case 2: fg_FuncCall1(); break;
default: fg_FuncCall2(); break;
}
```

But it's not ok to put several statements on the same line. Incorrect example:

```
switch (Value)
{
case 1: break;
case 2: fg_FuncCall1(); fg_FuncCall2(); break;
default: fg_FuncCall2(); break;
}
```

## Functions

A function declaration or prototype is defined as follows. Note that the ordering here is significant:

```
[static] [virtual] [inline] [other_attributes] <return type> f_FunctionName(<parameter list>) [const] [volatile] [= 0];

[static] [virtual] [inline] [other_attributes] auto f_FunctionName(<parameter list>) [const] [volatile] [= 0] -> <return type>;
```

You can never split the function into several lines before the function name. If you need extra space before the function name, specify the function with trailing return type.

```
[static] [virtual] [inline] [other_attributes] auto f_FunctionName(<parameter list>) [const] [volatile] [= 0]
	-> <return type>
;
```

If you would save more space by splitting the parameters, do that instead or in addition to specifying the return type on a separate trailing line.

Any lines after the line with the function name are indented.

Examples:

```
void fg_Function(int _Param0, int _Param1);

void fg_Function(int _Param0, int _Param1)
{
}

template <typename tf_CType>
void fg_Function(int _Param0, int _Param1)
{
}

void f_Function
	(
		int _Param0
		, int _Param1
	)
	const volatile = 0
;

void fg_Function
	(
		int _Param0
		, int _Param1
	)
{
}

template <typename tf_CType>
typename TCLongTemplate<tf_CType>::CType fg_Function(int _Param0, int _Param1);

template <typename tf_CType>
static inline_always auto fg_Function(int _Param0, int _Param1)
	-> typename TCLongTemplate
	<
		tf_CType
		, uint32
		, 3
	>
	::CType::template TCTest<fp32>::CType
;

template <typename tf_CType>
auto fg_Function
	(
		int _Param0
		, int _Param1
	)
	-> typename TCLongTemplate<tf_CType, uint32, 3>::CType
{
}

template <typename tf_CType>
auto f_Function
	(
		int _Param0
		, int _Param1
	)
	const volatile
	-> typename TCLongTemplate<tf_CType>::CType
{
}

template <typename tf_CType>
auto f_Function(int _Param0, int _Param1) const volatile
	-> typename TCLongTemplate<tf_CType>::CType
{
}

auto fg_Function
	(
		int _Param0
		, int _Param1
	)
	-> typename TCLongTemplate<fp32, uint32, 3>::CType
{
}
```

## Classes, Structs and Unions

Ordering of sections:
```
public
protected
private
```

Ordering within sections:
```
Types
Constructors
Destructor
Assignment operators
Comparison operators
Other operators
Functions
Constexpr static member variables
Static member variables
Member variables
```

Correct examples:

```
class CTest
{
public:
	struct CSubStruct
	{
		uint32 m_Member;
	};

	using CRetValue = uint32;

	CRetValue f_PublicFunction(CSubStruct const &_Param);

	uint32 m_PublicMember;

protected:
	struct CProtectedSubStruct
	{
		uint32 m_Member;
	};

	using CProtectedRetValue = uint32;

	CProtectedRetValue fp_ProtectedFunction(CProtectedSubStruct const &_Param);

	uint32 mp_ProtectedMember;

private:
	struct CPrivateSubStruct
	{
		uint32 m_Member;
	};

	using CPrivateRetValue = uint32;

	CPrivateRetValue fp_PrivateFunction(CPrivateSubStruct const &_Param);

	uint32 mp_PrivateMember;
};
```

```
struct CTest
{
	struct CSubStruct
	{
		uint32 m_Member;
	};

	using CRetValue = uint32;

	CRetValue f_PublicFunction(CSubStruct const &_Param);

	uint32 m_PublicMember;

protected:
	struct CProtectedSubStruct
	{
		uint32 m_Member;
	};

	using CProtectedRetValue = uint32;

	CProtectedRetValue fp_ProtectedFunction(CProtectedSubStruct const &_Param);

	uint32 mp_ProtectedMember;

private:
	struct CPrivateSubStruct
	{
		uint32 m_Member;
	};

	using CPrivateRetValue = uint32;

	CPrivateRetValue fp_PrivateFunction(CPrivateSubStruct const &_Param);

	uint32 mp_PrivateMember;
};
```

## Concepts

Require clauses always go on their own line indented. Correct example

```
template <typename tf_CType>
void f_FunctionName(tf_CType &&_Variable) const
	requires cIsCompatible<tf_CType>
;

template <typename tf_CType>
	requires cIsCompatible<tf_CType>
struct TCTestClass
```