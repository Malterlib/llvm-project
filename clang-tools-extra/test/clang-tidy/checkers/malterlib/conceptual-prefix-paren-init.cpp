// RUN: clang-tidy --checks='-*,malterlib-conceptual-prefix' --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" %s -- -std=c++20 2>&1 | FileCheck --allow-empty %s

// A variable initialized in parentheses in a template that isn't
// instantiated, whose initializer is the list of them, which has no type, is
// taken as initialized with what the list holds.

template <class t_CType>
struct TCBox
{
};

template <class t_CType>
void fg_Test(TCBox<t_CType> const &_Box)
{
	auto Third(_Box);
	(void)Third;
}

// CHECK-NOT: {{warning|error}}:
