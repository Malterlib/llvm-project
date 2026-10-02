// RUN: %check_clang_tidy -std=c++23-or-later %s malterlib-parameter-naming %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}"

// The `int` of a postfix increment or decrement with an explicit object
// parameter is left unnamed too.
struct CCounter
{
	CCounter operator ++ (this CCounter &_Self, int);
	CCounter operator -- (this CCounter &_Self, int);
};

// An accessor const through its explicit object, which gives what can be
// written, makes an object of its class writable.
struct CExplicitAccessor
{
	int *m_pValue;
	int &f_Get(this CExplicitAccessor const &_Self);
};
void fg_ExplicitAccessorOutput(CExplicitAccessor o_Value);

// Also one whose explicit object depends on its parameters, which can be
// const: can't be told, so either way.
struct CDeducedSelfAccessor
{
	int *m_pValue;
	template <typename tf_CSelf>
	int &f_Get(this tf_CSelf &&_Self);
};
void fg_DeducedSelfOutput(CDeducedSelfAccessor o_Value);
