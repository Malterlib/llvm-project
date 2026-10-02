// REQUIRES: malterlib-plugin
// RUN: clang-tidy --checks='-*,malterlib-conceptual-prefix' --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" %s -- -std=c++20 2>&1 | FileCheck --implicit-check-not='warning:' %s
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -std=c++20 -fsyntax-only -fconstexpr-steps=0 -fplugin-arg-malterlib-checks='-*,malterlib-conceptual-prefix' %s 2>&1 | FileCheck --implicit-check-not='warning:' %s

// A constraint is read as clang checked it and never evaluated by the check:
// one that calls a function, of an operator nothing uses, which clang never
// evaluated, can't be told, so the concept's prefix is allowed and none is
// required. Evaluating it would run the function inside the compiler, which
// for fg_Loop never ends with `-fconstexpr-steps=0`.

constexpr bool fg_Loop(unsigned _Value)
{
	while (_Value)
	{
	}
	return true;
}

constexpr bool fg_Holds()
{
	return true;
}

template <int t_Value>
struct TCLoopIterator
{
	int &operator*() requires (fg_Loop(1));
};

TCLoopIterator<0> g_iLoop;
TCLoopIterator<1> g_Loop;

template <int t_Value>
struct TCCallIterator
{
	int &operator*() requires (fg_Holds());
};

TCCallIterator<0> g_iCall;
TCCallIterator<1> g_Call;

// One written with nothing to run is folded as before: known not to hold.
template <int t_Value>
struct TCFoldedIterator
{
	int &operator*() requires (sizeof(int) > 64);
};

TCFoldedIterator<0> g_iFolded;
// CHECK: :[[@LINE-1]]:21: warning: 'g_iFolded' has the 'i' concept prefix of an iterator, but is not one [malterlib-conceptual-prefix]
TCFoldedIterator<1> g_Folded;

// Known to hold.
template <int t_Value>
struct TCTrueIterator
{
	int &operator*() requires (sizeof(int) > 1 && __is_integral(int));
};

TCTrueIterator<0> g_iTrue;
TCTrueIterator<1> g_True;
// CHECK: :[[@LINE-1]]:19: warning: 'g_True' is an iterator and should have the 'i' concept prefix after its storage prefix [malterlib-conceptual-prefix]
