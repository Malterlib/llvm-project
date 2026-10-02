// REQUIRES: malterlib-plugin
// RUN: clang-tidy --checks='-*,malterlib-no-zero-bit-scan' --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" %s -- -std=c++20 2>&1 | FileCheck --implicit-check-not='warning:' %s
// RUN: clang-tidy --checks='-*,malterlib-no-zero-bit-scan' --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" %s -- -std=c++20 -fconstexpr-steps=0 2>&1 | FileCheck --implicit-check-not='warning:' %s
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -std=c++20 -fsyntax-only -fconstexpr-steps=0 -fplugin-arg-malterlib-checks='-*,malterlib-no-zero-bit-scan' %s 2>&1 | FileCheck --implicit-check-not='warning:' %s

// The check never evaluates what clang didn't (round 235): a construction or
// a call clang never evaluated where it is can run anything when constant
// evaluated, and with `-fconstexpr-steps=0` never end, like CLoopBits. The
// check reads the values clang stored and folds what runs nothing, also a
// construction by a constructor whose body is empty and whose initializers
// only read its parameters, like CBits; what runs something is a value it
// can't tell, like one it can't see.

using umint = decltype(sizeof(0));

int fg_GetHighestBitSetNoZero(umint _Value);

struct CLoopBits
{
	umint m_Bits;
	constexpr CLoopBits(umint _Bits) : m_Bits(_Bits)
	{
		if (__builtin_is_constant_evaluated())
			while (_Bits)
			{
			}
	}
	constexpr CLoopBits operator | (CLoopBits const &_Other) const { return CLoopBits(m_Bits | _Other.m_Bits); }
	constexpr bool operator != (CLoopBits const &_Other) const { return m_Bits != _Other.m_Bits; }
};

int fg_GetHighestBitSetNoZero(CLoopBits const &_Value);

constexpr umint fg_Loop(umint _Value)
{
	while (_Value)
	{
	}
	return 1;
}

struct CBits
{
	umint m_Bits;
	constexpr CBits(umint _Bits) : m_Bits(_Bits) {}
	constexpr CBits operator | (CBits const &_Other) const { return CBits(m_Bits | _Other.m_Bits); }
	constexpr bool operator != (CBits const &_Other) const { return m_Bits != _Other.m_Bits; }
};

int fg_GetHighestBitSetNoZero(CBits const &_Value);

// The round 235 reproducer: the construction isn't evaluated, so the argument
// is one the check can't tell is not zero.
int fg_Construction()
{
	return fg_GetHighestBitSetNoZero(CLoopBits(1));
// CHECK: :[[@LINE-1]]:9: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

int fg_Call()
{
	return fg_GetHighestBitSetNoZero(fg_Loop(1));
// CHECK: :[[@LINE-1]]:9: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

int fg_Made(CLoopBits const &_Value)
{
	return fg_GetHighestBitSetNoZero(_Value | CLoopBits(1));
// CHECK: :[[@LINE-1]]:9: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

// Compared with what it can't tell is zero: taken as a check.
int fg_Compared(CLoopBits const &_Value)
{
	if (_Value != CLoopBits(0))
		return fg_GetHighestBitSetNoZero(_Value);
	return -1;
}

// What runs nothing is folded: made with a bit set, and compared with zero.
int fg_FoldedMade(CBits const &_Value)
{
	return fg_GetHighestBitSetNoZero(_Value | CBits(1));
}

int fg_FoldedCompared(CBits const &_Value)
{
	if (_Value != CBits(0))
		return fg_GetHighestBitSetNoZero(_Value);
	return -1;
}

constexpr CBits gc_One(1);

int fg_Stored(CBits const &_Value)
{
	return fg_GetHighestBitSetNoZero(_Value | gc_One);
}

// A constant clang evaluated where it is declared is read, not evaluated.
constexpr umint gc_Loop = fg_Loop(0);

int fg_StoredCall()
{
	return fg_GetHighestBitSetNoZero(gc_Loop);
}
