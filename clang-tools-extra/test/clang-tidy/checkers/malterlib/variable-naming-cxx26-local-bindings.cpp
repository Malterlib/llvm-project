// RUN: %check_clang_tidy -std=c++26-or-later %s malterlib-variable-naming %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}"

// Clang asserts at the end of a function body that declares constexpr
// structured bindings, "Leftover expressions for odr-use checking"
// (Sema::ActOnFinishFunctionBody); a build without assertions accepts it.
// UNSUPPORTED: asserts

// Constexpr structured bindings of a function are constants.
struct CBindingPair
{
	int m_First;
	int m_Second;
};
void fg_ConstexprBindings()
{
	constexpr auto [c_First, c_Second] = CBindingPair{1, 2};
	static constexpr auto [s_First, s_Second] = CBindingPair{1, 2};
// CHECK-MESSAGES: :[[@LINE-1]]:25: warning: static structured binding 's_First' should have the 'c_' prefix [malterlib-variable-naming]
// CHECK-MESSAGES: :[[@LINE-2]]:34: warning: static structured binding 's_Second' should have the 'c_' prefix [malterlib-variable-naming]
}
