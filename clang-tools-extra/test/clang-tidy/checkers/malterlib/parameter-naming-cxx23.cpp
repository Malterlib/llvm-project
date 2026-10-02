// RUN: %check_clang_tidy -std=c++23-or-later %s malterlib-parameter-naming %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}"

// The `int` of a postfix increment or decrement with an explicit object
// parameter is left unnamed too.
struct CCounter
{
	CCounter operator ++ (this CCounter &_Self, int);
	CCounter operator -- (this CCounter &_Self, int);
};
