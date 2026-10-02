// RUN: clang-tidy --checks='-*,malterlib-parameter-ownership' --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" %s -- -std=c++20 2>&1 | FileCheck --allow-empty %s

// The pattern of a specialization of coroutine_traits is matched with each
// pair of a shared type and what it names once, also a type list of a type
// list that names the one before four times, which would be matched again
// through each path to it, a number of times exponential in its depth.

template <class t_CA, class t_CB, class t_CC, class t_CD>
struct TCList
{
};

template <class t_CType>
using TCLevel0 = t_CType;
#define DLevel(N, P) template <class t_CType> using TCLevel##N = TCList<TCLevel##P<t_CType>, TCLevel##P<t_CType>, TCLevel##P<t_CType>, TCLevel##P<t_CType>>;
DLevel(1, 0) DLevel(2, 1) DLevel(3, 2) DLevel(4, 3) DLevel(5, 4) DLevel(6, 5) DLevel(7, 6) DLevel(8, 7)
DLevel(9, 8) DLevel(10, 9) DLevel(11, 10) DLevel(12, 11)

template <class t_CType>
struct TCWrapped
{
};

namespace std
{
	template <class t_CReturn, class... tp_CArguments>
	struct coroutine_traits;
	template <class t_CType, class... tp_CArguments>
	struct coroutine_traits<TCWrapped<TCLevel12<t_CType>>, tp_CArguments...>
	{
		struct promise_type
		{
		};
	};
}

// Of each function, also one with no parameters.
#define DFunction(N) TCWrapped<TCLevel12<int>> fg_Test##N() { return {}; }
#define DFunctions4(N) DFunction(N##0) DFunction(N##1) DFunction(N##2) DFunction(N##3)
DFunctions4(0) DFunctions4(1) DFunctions4(2) DFunctions4(3) DFunctions4(4) DFunctions4(5) DFunctions4(6) DFunctions4(7)

// CHECK-NOT: {{warning|error}}:
