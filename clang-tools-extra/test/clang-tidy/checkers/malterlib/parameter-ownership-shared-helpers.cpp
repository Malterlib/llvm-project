// RUN: clang-tidy --checks='-*,malterlib-parameter-ownership' --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" %s -- -std=c++20 2>&1 | FileCheck --allow-empty %s

// Whether a helper keeps a parameter is decided once for each parameter at
// each depth, also of helpers that each give it to the one before sixteen
// times, which would be looked at again through each path to them, a number of
// times exponential in their depth.

#define DFour(X) X; X; X; X

template <int t_iDepth>
void fg_Read(int _Value)
{
	if constexpr (t_iDepth == 0)
		(void)_Value;
	else
	{
		DFour(DFour(fg_Read<t_iDepth - 1>(_Value)));
	}
}

struct CPageSizeScope
{
	CPageSizeScope(int _Value)
	{
		fg_Read<7>(_Value);
	}
};

struct CPagedByteVector
{
	int m_PageSize;
	~CPagedByteVector()
	{
		CPageSizeScope PageScope(m_PageSize);
	}
};

void fg_Test(CPagedByteVector _Value)
{
}

// CHECK-NOT: {{warning|error}}:
