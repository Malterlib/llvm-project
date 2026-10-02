// RUN: clang-tidy --checks='-*,malterlib-no-zero-bit-scan' --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" %s -- -std=c++20 2>&1 | FileCheck --allow-empty %s

// Whether the destructor of what is destroyed between a test and the scan
// writes what the test tells of is decided once for each class, also of one
// that is a virtual base of each class between it and the most derived, which
// a chain of classes with eight bases that each have the one before as a
// virtual base reaches through a number of paths that grows eightfold with
// each.

unsigned fg_GetLowestBitSetNoZero(unsigned _Value);

template <int t_Level> struct TCLevel;
template <int t_Level, int t_Tag> struct TCBranch : virtual TCLevel<t_Level - 1> {};

template <int t_Level>
struct TCLevel
	: TCBranch<t_Level, 0>, TCBranch<t_Level, 1>, TCBranch<t_Level, 2>, TCBranch<t_Level, 3>
	, TCBranch<t_Level, 4>, TCBranch<t_Level, 5>, TCBranch<t_Level, 6>, TCBranch<t_Level, 7>
{
};

template <> struct TCLevel<0>
{
	~TCLevel() {}
};

unsigned fg_Test(unsigned _Bits)
{
	bool bNonZero = _Bits != 0;
	{
		TCLevel<16> Value;
	}
	if (bNonZero)
		return fg_GetLowestBitSetNoZero(_Bits);
	return 0;
}

// So is whether one writes a global, which a test of it tells of.
unsigned g_Bits;
unsigned fg_TestGlobal()
{
	if (TCLevel<16> Value; !g_Bits)
		return 0;
	return fg_GetLowestBitSetNoZero(g_Bits);
}

// And one that does is still found, through the virtual bases.
template <int t_Level> struct TCWriterLevel;
template <int t_Level, int t_Tag> struct TCWriterBranch : virtual TCWriterLevel<t_Level - 1> {};

template <int t_Level>
struct TCWriterLevel
	: TCWriterBranch<t_Level, 0>, TCWriterBranch<t_Level, 1>, TCWriterBranch<t_Level, 2>, TCWriterBranch<t_Level, 3>
	, TCWriterBranch<t_Level, 4>, TCWriterBranch<t_Level, 5>, TCWriterBranch<t_Level, 6>, TCWriterBranch<t_Level, 7>
{
};

template <> struct TCWriterLevel<0>
{
	~TCWriterLevel() { g_Bits = 0; }
};

unsigned fg_TestWriter()
{
	if (TCWriterLevel<6> Value; !g_Bits)
		return 0;
	return fg_GetLowestBitSetNoZero(g_Bits);
}
// CHECK-NOT: warning:
// CHECK: :[[@LINE-3]]:9: warning: {{.*}} [malterlib-no-zero-bit-scan]

// CHECK-NOT: warning:
