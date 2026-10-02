// RUN: %check_clang_tidy -std=c++26-or-later %s malterlib-variable-naming %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}"

// Constexpr structured bindings are constants.
struct CBindingPair
{
	int m_First;
	int m_Second;
};
constexpr auto [gc_BindingFirst, gc_BindingSecond] = CBindingPair{1, 2};
constexpr auto [g_BindingFirst, g_BindingSecond] = CBindingPair{1, 2};
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: global structured binding 'g_BindingFirst' should have the 'gc_' prefix [malterlib-variable-naming]
// CHECK-MESSAGES: :[[@LINE-2]]:33: warning: global structured binding 'g_BindingSecond' should have the 'gc_' prefix [malterlib-variable-naming]
// Of a function, in variable-naming-cxx26-local-bindings.cpp.

// The placeholder of what is ignored is no name, also repeated.
struct CPlaceholderTriple
{
	int m_First;
	int m_Second;
	int m_Third;
};

void fg_Placeholders(CPlaceholderTriple const &_Triple)
{
	auto [First, _, _] = _Triple;
	auto _ = First;
	int _ = 0;
	auto [Other, Second, _] = _Triple;
	(void)Other;
	(void)Second;
}
