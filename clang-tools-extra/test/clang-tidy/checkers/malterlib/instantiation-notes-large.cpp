// RUN: %check_clang_tidy -std=c++20-or-later %s malterlib-coroutine-reference-parameter %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -I %S/Inputs

#include "malterlib-concurrency.h"

TCFuture<int> fg_Suspend();

// Where a warning in a template was instantiated, with template arguments
// too large to print, of the instantiation or of the classes it is in, which
// can be exponentially long, is noted without them.
template <int t_N>
struct TCSignature
{
	using CPrevious = typename TCSignature<t_N - 1>::CType;
	using CType = TCFunction<CPrevious (CPrevious)>;
};
template <>
struct TCSignature<0>
{
	using CType = int;
};
using CLarge = TCSignature<40>::CType;

template <typename t_CType>
struct TCReader
{
	template <typename tf_CType>
	TCFuture<int> f_Read(int &_Value)
	{
		co_await fg_Suspend();
		co_return _Value;
	}

	TCFuture<int> f_Run(int &_Value)
	{
		co_await fg_Suspend();
		co_return _Value;
	}
};

template <typename t_CType>
struct TCSmallReader
{
	template <typename tf_CType>
	TCFuture<int> f_Read(int &_Value)
	{
		co_await fg_Suspend();
		co_return _Value;
	}
};

template <typename tf_CType>
TCFuture<int> fg_Read(int &_Value)
{
	co_await fg_Suspend();
	co_return _Value;
}

void fg_Use(int &_Value)
{
	TCReader<CLarge> Reader;
	Reader.f_Read<int>(_Value);
	Reader.f_Run(_Value);
	TCSmallReader<int> SmallReader;
	SmallReader.f_Read<CLarge>(_Value);
	fg_Read<CLarge>(_Value);
}

// CHECK-NOTES: :27:28: warning: TCFuture coroutine 'f_Read' uses '_Value' after a suspension{{.*}}
// CHECK-NOTES: :61:9: note: in instantiation of function template specialization 'TCReader<...>::f_Read<...>' requested here
// CHECK-NOTES: :30:13: note: '_Value' is used here after a suspension{{.*}}
// CHECK-NOTES: :33:27: warning: TCFuture coroutine 'f_Run' uses '_Value' after a suspension{{.*}}
// CHECK-NOTES: :62:9: note: in instantiation of member function 'TCReader<...>::f_Run' requested here
// CHECK-NOTES: :36:13: note: '_Value' is used here after a suspension{{.*}}
// CHECK-NOTES: :44:28: warning: TCFuture coroutine 'f_Read' uses '_Value' after a suspension{{.*}}
// CHECK-NOTES: :64:14: note: in instantiation of function template specialization 'TCSmallReader<...>::f_Read<...>' requested here
// CHECK-NOTES: :47:13: note: '_Value' is used here after a suspension{{.*}}
// CHECK-NOTES: :52:28: warning: TCFuture coroutine 'fg_Read' uses '_Value' after a suspension{{.*}}
// CHECK-NOTES: :65:2: note: in instantiation of function template specialization 'fg_Read<...>' requested here
// CHECK-NOTES: :55:12: note: '_Value' is used here after a suspension{{.*}}

// Also of the template arguments in packs, `TCTypeList<CPrevious, CPrevious>`,
// and of pointers to members.
template <typename... tp_CTypes>
struct TCTypeList
{
};
template <int t_N>
struct TCNestedList
{
	using CPrevious = typename TCNestedList<t_N - 1>::CType;
	using CType = TCTypeList<CPrevious, CPrevious>;
};
template <>
struct TCNestedList<0>
{
	using CType = TCTypeList<>;
};

template <typename t_CType>
struct TCMember
{
};
template <int t_N>
struct TCNestedMember
{
	using CPrevious = typename TCNestedMember<t_N - 1>::CType;
	using CType = CPrevious TCMember<CPrevious>::*;
};
template <>
struct TCNestedMember<0>
{
	using CType = int;
};

template <typename... tp_CTypes>
TCFuture<int> fg_ReadList(TCTypeList<tp_CTypes...>, int &_Value)
{
	co_await fg_Suspend();
	co_return _Value;
}

template <typename tf_CType>
TCFuture<int> fg_ReadMember(int &_Value)
{
	co_await fg_Suspend();
	co_return _Value;
}

void fg_UseNested(int &_Value)
{
	::fg_ReadList(TCNestedList<40>::CType{}, _Value);
	fg_ReadMember<TCNestedMember<40>::CType>(_Value);
}

// CHECK-NOTES: :116:58: warning: TCFuture coroutine 'fg_ReadList' uses '_Value' after a suspension{{.*}}
// CHECK-NOTES: :131:4: note: in instantiation of function template specialization 'fg_ReadList<...>' requested here
// CHECK-NOTES: :119:12: note: '_Value' is used here after a suspension{{.*}}
// CHECK-NOTES: :123:34: warning: TCFuture coroutine 'fg_ReadMember' uses '_Value' after a suspension{{.*}}
// CHECK-NOTES: :132:2: note: in instantiation of function template specialization 'fg_ReadMember<...>' requested here
// CHECK-NOTES: :126:12: note: '_Value' is used here after a suspension{{.*}}
