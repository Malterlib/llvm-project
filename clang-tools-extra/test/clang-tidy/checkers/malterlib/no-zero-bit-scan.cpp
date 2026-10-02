// RUN: %check_clang_tidy -std=c++20-or-later %s malterlib-no-zero-bit-scan %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}"

using umint = unsigned long;

int fg_GetHighestBitSetNoZero(umint _Value);
int fg_GetHighestBitSet(umint _Value);
void fg_Assert(bool _bCondition);
#define DMibCheck(d_Condition) fg_Assert(d_Condition)

void fg_Unchecked(umint _Value)
{
	fg_GetHighestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	fg_GetHighestBitSetNoZero(0);
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: 'fg_GetHighestBitSetNoZero' is called with zero [malterlib-no-zero-bit-scan]
}

void fg_NotProven(umint _Value, unsigned _Shift)
{
	fg_GetHighestBitSetNoZero(static_cast<unsigned char>(_Value | 256u));
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	fg_GetHighestBitSetNoZero(2u << _Shift);
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	fg_GetHighestBitSetNoZero(3u << _Shift);
}

void fg_Assumed(umint _Value)
{
	[[assume(_Value != 0)]];
	fg_GetHighestBitSetNoZero(_Value);
}

void fg_Checked(umint _Value, umint _Other)
{
	fg_GetHighestBitSetNoZero(5);
	fg_GetHighestBitSetNoZero(_Value | 1);
	fg_GetHighestBitSetNoZero(umint(1) << _Value);

	if (_Value)
		fg_GetHighestBitSetNoZero(_Value);

	int Bit = _Value == 0 ? -1 : fg_GetHighestBitSetNoZero(_Value);
	bool bSet = _Value != 0 && fg_GetHighestBitSetNoZero(_Value) > 3;

	while (_Value)
	{
		fg_GetHighestBitSetNoZero(_Value);
		_Value >>= 1;
	}
}

void fg_EarlyReturn(umint _Value)
{
	if (_Value == 0)
		return;

	fg_GetHighestBitSetNoZero(_Value);
}

void fg_Asserted(umint _Value)
{
	DMibCheck(_Value != 0);
	fg_GetHighestBitSetNoZero(_Value);
}

// A template is checked in its instantiations, where the types and constants
// that tell are known.
template <typename tf_CValue>
int fg_Template(tf_CValue _Value)
{
	return fg_GetHighestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
int fg_InstantiateTemplate(umint _Value)
{
	return fg_Template(_Value);
}

using CBits = decltype(fg_GetHighestBitSetNoZero(0));

namespace std
{
	class type_info
	{
	public:
		virtual ~type_info();
	};
}

void fg_Typeid(umint _Value)
{
	auto const &Info = typeid(fg_GetHighestBitSetNoZero(_Value));
}

void fg_NoLint(umint _Value)
{
	fg_GetHighestBitSetNoZero(_Value); // NOLINT(malterlib-no-zero-bit-scan)
}

struct CBitHolder
{
	umint m_Bits = 0;
};

void fg_OtherObject(CBitHolder const &_A, CBitHolder const &_B)
{
	// A check of another object's member doesn't check this one.
	if (_A.m_Bits)
		fg_GetHighestBitSetNoZero(_B.m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	if (_A.m_Bits)
		fg_GetHighestBitSetNoZero(_A.m_Bits);
}

struct CBitInfo
{
	bool m_bReady = false;
	umint m_Bits = 0;
};

void fg_OtherMember(CBitInfo const &_Info)
{
	// A check of another member of the object doesn't check this one.
	if (_Info.m_bReady)
		fg_GetHighestBitSetNoZero(_Info.m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

template <typename t_CInt>
int fg_GetLowestBitSetNoZero(t_CInt const &_Value);

// Bound to a `const &` parameter, like the real bit scans.
void fg_ConstReference(umint _Value)
{
	fg_GetLowestBitSetNoZero(_Value | 1u);
}

// The direction of the test, and writes after it.
void fg_Direction(umint _Value)
{
	if (_Value == 0)
		fg_GetHighestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; {{.*}}
	if (_Value)
		fg_GetHighestBitSetNoZero(_Value);
	else
		fg_GetHighestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; {{.*}}
	(void)(_Value != 0 ? fg_GetHighestBitSetNoZero(_Value) : 0);
	(void)(_Value == 0 ? fg_GetHighestBitSetNoZero(_Value) : 0);
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; {{.*}}
}

void fg_EarlyReturnWrong(umint _Value)
{
	if (_Value)
		return;
	fg_GetHighestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; {{.*}}
}

void fg_Overwritten(umint _Value)
{
	if (!_Value)
		return;
	_Value = 0;
	fg_GetHighestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; {{.*}}
}

// A call in its own condition isn't checked by it; a test before `&&` is.
bool fg_Conditions(umint _Value)
{
	if (fg_GetHighestBitSetNoZero(_Value))
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; {{.*}}
		return true;
	return _Value != 0 && fg_GetHighestBitSetNoZero(_Value);
}

// A check doesn't hold where the value is written after it.
void fg_WrittenAfterCheck(umint _Value, bool _bOther)
{
	if (_Value)
	{
		_Value = 0;
		fg_GetHighestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; {{.*}}
	}
	if (_Value)
	{
		while (_bOther)
		{
			fg_GetHighestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:4: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; {{.*}}
			_Value = 0;
		}
	}
	if (!_Value)
		return;
	else
		_Value = 0;
	fg_GetHighestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; {{.*}}
}

void fg_WrittenBeforeCheck(umint _Value)
{
	_Value = _Value * 3;
	if (!_Value)
		return;
	fg_GetHighestBitSetNoZero(_Value);
	if (_Value)
	{
		if (int Bit = fg_GetHighestBitSetNoZero(_Value); Bit)
			return;
	}
}

// The initializer of an `if` runs before its condition.
void fg_IfInitializer(umint _Value)
{
	if (int Bit = fg_GetHighestBitSetNoZero(_Value); _Value)
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; {{.*}}
		(void)Bit;
}

// An assertion checks in the direction of its condition.
void fg_AssertDirection(umint _Value, umint _Other)
{
	DMibCheck(_Value == 0);
	fg_GetHighestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; {{.*}}
	DMibCheck(_Other != 0 && _Other < 64);
	fg_GetHighestBitSetNoZero(_Other);
}

// A narrowed value can be zero when the value isn't.
void fg_Narrowed(umint _Value)
{
	if (_Value != 0)
		fg_GetHighestBitSetNoZero(static_cast<unsigned char>(_Value));
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; {{.*}}
}

// A jump only leaves when it skips the call.
void fg_Goto(umint _Value)
{
	if (!_Value)
		goto Scan;
Scan:
	fg_GetHighestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; {{.*}}
	if (!_Value)
		goto End;
	fg_GetHighestBitSetNoZero(_Value);
End:
	return;
}

// A test of `operator bool` tests the object.
template <typename t_CType>
int fg_GetLowestBitSetNoZero(t_CType const &_Value);
struct CInt256
{
	unsigned long m_Lower;
	unsigned long m_Upper;
	explicit operator bool() const
	{
		return m_Lower != 0 || m_Upper != 0;
	}
};
int fg_BoolConversion(CInt256 const &_Value)
{
	if (_Value)
		return fg_GetLowestBitSetNoZero(_Value);
	if (!_Value)
		return fg_GetLowestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; {{.*}}
	return -1;
}

// `if consteval` has no condition.
int fg_Consteval(umint _Value)
{
	if consteval
	{
		return fg_GetHighestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; {{.*}}
	}
	return 0;
}

// A check of a value doesn't check what is computed from it, but a check of
// the computed value does.
void fg_Computed(umint _Value, umint const *_pValues)
{
	if (_Value)
		fg_GetHighestBitSetNoZero(_Value - 1);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; {{.*}}
	if (_Value - 1 != 0)
		fg_GetHighestBitSetNoZero(_Value - 1);
	if (_pValues[0] != 0)
		fg_GetHighestBitSetNoZero(_pValues[1]);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; {{.*}}
	if (_pValues[1] != 0)
		fg_GetHighestBitSetNoZero(_pValues[1]);
	if (static_cast<unsigned char>(_Value) != 0)
		fg_GetHighestBitSetNoZero(static_cast<unsigned char>(_Value));
}

// Overloaded comparisons and writes, like of an integer class.
struct CInt512
{
	unsigned long m_Value;
	explicit operator bool() const
	{
		return m_Value != 0;
	}
	bool operator == (int _Other) const;
	CInt512 &operator -- ();
};
void fg_Overloaded(CInt512 _Value)
{
	if (_Value == 0)
		fg_GetLowestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; {{.*}}
	if (_Value)
	{
		--_Value;
		fg_GetLowestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; {{.*}}
	}
}

// Writing an object writes its members, and what a condition writes after a
// test of it is not tested.
struct CBitsInfo
{
	umint m_Bits;
};
void fg_WrittenObject(CBitsInfo _Info, umint _Value)
{
	if (_Info.m_Bits)
	{
		_Info = {};
		fg_GetHighestBitSetNoZero(_Info.m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; {{.*}}
	}
	if (_Value != 0 && (_Value = 0, true))
		fg_GetHighestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; {{.*}}
}

// The branch of an earlier check that leaves can be the `else`.
void fg_ElseLeaves(umint _Value)
{
	if (_Value)
	{
	}
	else
		return;
	fg_GetHighestBitSetNoZero(_Value);
}

// An assertion that is compiled out, like `DMibFastCheck` in a release build,
// tells nothing; a fact for the analysis beside it, which isn't evaluated,
// tells it, also a minimum that is a constant of a template argument, but one
// that allows zero doesn't.
#define DMibFastCheck(d_Expression) (void)0
#define DMibAnalyzerAssume(d_Expression) static_cast<void>(sizeof(!!(d_Expression)))
void fg_CompiledOut(umint _Value, umint _Other)
{
	DMibFastCheck(_Value > 0);
	fg_GetHighestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; {{.*}}
	DMibFastCheck(_Other > 0);
	DMibAnalyzerAssume(_Other > 0);
	fg_GetHighestBitSetNoZero(_Other);
}

struct CAnalyzerParams
{
	static constexpr umint mc_MinSize = 16;
};
template <typename t_CParams>
umint fg_AnalyzerAssumed(umint _Value, umint _Other)
{
	DMibFastCheck(_Value >= t_CParams::mc_MinSize);
	DMibAnalyzerAssume(_Value >= t_CParams::mc_MinSize);
	DMibAnalyzerAssume(_Other >= 0);
	return fg_GetHighestBitSetNoZero(_Value) + fg_GetHighestBitSetNoZero(_Other);
// CHECK-MESSAGES: :[[@LINE-1]]:45: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; {{.*}}
}
template umint fg_AnalyzerAssumed<CAnalyzerParams>(umint _Value, umint _Other);

// The complement of a value of its type isn't zero when the value isn't all
// ones, like a free bit in an entry that isn't fully set.
template <typename t_CEntry>
struct TCBitEntries
{
	static constexpr t_CEntry mcp_FullySet = t_CEntry(~t_CEntry(0));
	t_CEntry m_Entry;

	int f_FindFree() const
	{
		if (m_Entry != mcp_FullySet)
			return fg_GetLowestBitSetNoZero(t_CEntry(~m_Entry));
		return -1;
	}
	int f_FindFreeUnchecked() const
	{
		if (m_Entry != 0)
			return fg_GetLowestBitSetNoZero(t_CEntry(~m_Entry));
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; {{.*}}
		return -1;
	}
};
int fg_UseBitEntries(TCBitEntries<unsigned char> const &_Entries)
{
	return _Entries.f_FindFree() + _Entries.f_FindFreeUnchecked();
}

// Constants of template parameters are checked in each instantiation.
template <umint t_Value>
struct TCConstantBits
{
	static inline int const mc_Bits = fg_GetHighestBitSetNoZero(t_Value - 1);
// CHECK-MESSAGES: :[[@LINE-1]]:36: warning: 'fg_GetHighestBitSetNoZero' is called with zero [malterlib-no-zero-bit-scan]
};
int g_ConstantBits = TCConstantBits<4>::mc_Bits + TCConstantBits<1>::mc_Bits;

// A guard in a template that only its instantiations can tell doesn't hide
// what they tell.
template <typename tf_CInt>
int fg_ZeroInTemplate(tf_CInt _Value)
{
	if (_Value == tf_CInt(0))
		return fg_GetHighestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; {{.*}}
	return -1;
}
template <typename tf_CInt>
int fg_NonZeroInTemplate(tf_CInt _Value)
{
	if (_Value != tf_CInt(0))
		return fg_GetHighestBitSetNoZero(_Value);
	return -1;
}
int g_ZeroInTemplate = fg_ZeroInTemplate(0u) + fg_NonZeroInTemplate(0u);

void fg_ChangedByCondition(umint _Value)
{
	// Tested before it is decremented.
	if (_Value-- != 0)
		fg_GetHighestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; {{.*}}
	// Tested after.
	if (--_Value != 0)
		fg_GetHighestBitSetNoZero(_Value);
	while (_Value-- != 0)
		fg_GetHighestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; {{.*}}
}

void fg_ComparedComputed(umint _Value, umint _Mask)
{
	// `_Value - 1` isn't zero when `_Value` is.
	if (_Value - 1 != 0)
		fg_GetHighestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; {{.*}}
	if ((_Value & _Mask) != 0)
		fg_GetHighestBitSetNoZero(_Value);
	if ((_Value >> 1) != 0)
		fg_GetHighestBitSetNoZero(_Value);
}

void fg_ClearValue(umint &_Value);
void fg_ReadValue(umint const &_Value);
void fg_ClearPointed(umint *_pValue);
void fg_ChangedByCall(umint _Value)
{
	if (_Value)
	{
		fg_ReadValue(_Value);
		fg_GetHighestBitSetNoZero(_Value);
		fg_ClearValue(_Value);
		fg_GetHighestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; {{.*}}
	}
	if (_Value)
	{
		fg_ClearPointed(&_Value);
		fg_GetHighestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; {{.*}}
	}
}

void fg_CompiledOutParentheses(umint _Value, umint _Other)
{
	DMibAnalyzerAssume((_Value == 0));
	fg_GetHighestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; {{.*}}
	DMibAnalyzerAssume(!(_Other));
	fg_GetHighestBitSetNoZero(_Other);
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; {{.*}}
}

void fg_AssertedComplement(unsigned _Value)
{
	DMibCheck(_Value != ~0u);
	fg_GetHighestBitSetNoZero(~_Value);
}

// Through a pointer to a function that changes it, an element other than the
// one tested, an integer class compared with zero, and facts for the
// analysis of members.
void fg_ClearThroughPointer(umint _Value)
{
	if (!_Value)
		return;
	void (*fClear)(umint &) = fg_ClearValue;
	fClear(_Value);
	fg_GetHighestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
void fg_OtherElement(umint *_pValues)
{
	if (_pValues[0])
	{
		_pValues[1] = 0;
		fg_GetHighestBitSetNoZero(_pValues[0]);
	}
}
namespace NIntegerClass
{
	struct CInt128
	{
		umint m_Lower{};
		umint m_Upper{};
		constexpr CInt128(umint _Lower) : m_Lower(_Lower) {}
		constexpr bool operator == (CInt128 const &_Other) const
		{
			return m_Lower == _Other.m_Lower && m_Upper == _Other.m_Upper;
		}
	};
	template <typename tf_CInt>
	int fg_GetLowestBitSetNoZero(tf_CInt const &_Value);
	int fg_ZeroBranch(CInt128 const &_Value)
	{
		if (_Value == 0)
			return fg_GetLowestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
		return -1;
	}
}
struct CMemberBits
{
	umint m_Bits;
};
void fg_AnalyzerAssumedMembers(CMemberBits const &_Info, CMemberBits const &_A, CMemberBits const &_B)
{
	DMibAnalyzerAssume(_Info.m_Bits == 0);
	fg_GetHighestBitSetNoZero(_Info.m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	DMibAnalyzerAssume(_A.m_Bits != 0);
	fg_GetHighestBitSetNoZero(_B.m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	fg_GetHighestBitSetNoZero(_A.m_Bits);
}

// Rewritten comparisons, members of other elements, uncalled lambdas and the
// value a postfix decrement gives.
namespace NRewritten
{
	struct CInt128
	{
		umint m_Lower{};
		umint m_Upper{};
		constexpr CInt128(umint _Lower) : m_Lower(_Lower) {}
		constexpr bool operator == (CInt128 const &_Other) const
		{
			return m_Lower == _Other.m_Lower && m_Upper == _Other.m_Upper;
		}
	};
	template <typename tf_CInt>
	int fg_GetLowestBitSetNoZero(tf_CInt const &_Value);
	int fg_NotEqualReturns(CInt128 const &_Value)
	{
		if (_Value != 0)
			return -1;
		return fg_GetLowestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	}
	int fg_ReversedZero(CInt128 const &_Value)
	{
		if (0 == _Value)
			return fg_GetLowestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
		return -1;
	}
}
struct CElementBits
{
	umint m_Bits;
};
void fg_OtherObjects(CElementBits const *_pInfo, CElementBits const *_pA, CElementBits const *_pB)
{
	if (_pInfo[0].m_Bits)
		fg_GetHighestBitSetNoZero(_pInfo[1].m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	if (_pInfo[0].m_Bits)
		fg_GetHighestBitSetNoZero(_pInfo[0].m_Bits);
	if ((*_pA).m_Bits)
		fg_GetHighestBitSetNoZero((*_pB).m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
void fg_UncalledLambda(umint _Value)
{
	if (!_Value)
		return;
	auto fClear = [&]
		{
			_Value = 0;
		}
	;
	fg_GetHighestBitSetNoZero(_Value);
}
void fg_PostfixArgument(umint _Value)
{
	if (_Value != 0)
		fg_GetHighestBitSetNoZero(_Value--);
}

// Evaluated template arguments, a guard whose right side clears the value,
// and a constant zero in a discarded branch of a template.
template <int t_Bits>
struct TCBitsOf
{
};
namespace NConstantScan
{
	constexpr int fg_GetHighestBitSetNoZero(unsigned const &_Value)
	{
		return 0;
	}
	using CBitsOfZero = TCBitsOf<fg_GetHighestBitSetNoZero(0)>;
// CHECK-MESSAGES: :[[@LINE-1]]:31: warning: 'fg_GetHighestBitSetNoZero' is called with zero [malterlib-no-zero-bit-scan]
}
bool fg_ClearAndTell(umint &_Value);
void fg_ClearingGuard(umint _Value)
{
	if (_Value && fg_ClearAndTell(_Value))
		fg_GetHighestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
template <typename t_CType>
void fg_DiscardedZero()
{
	if constexpr (sizeof(t_CType) == 1)
		fg_GetHighestBitSetNoZero(0);
}
void fg_UseDiscardedZero()
{
	fg_DiscardedZero<int>();
}

// An explicit conversion to a boolean, a pointer given on to what can clear
// it, and overloaded bit operators of an integer class.
void fg_ExplicitBool(umint _Value)
{
	if (!static_cast<bool>(_Value))
		fg_GetHighestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
void fg_ClearPointee(umint *_pValue);
void fg_PointerGivenOn(umint *_pValue)
{
	if (*_pValue != 0)
	{
		fg_ClearPointee(_pValue);
		fg_GetHighestBitSetNoZero(*_pValue);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	}
}
namespace NOverloadedBits
{
	struct CInt
	{
		umint m_Value;
		CInt(umint _Value) : m_Value(_Value) {}
		bool operator == (CInt const &) const = default;
		CInt operator & (CInt const &_Other) const
		{
			return m_Value & _Other.m_Value;
		}
	};
	template <typename tf_CInt>
	int fg_GetLowestBitSetNoZero(tf_CInt const &_Value);
	int fg_MaskedGuard(CInt const &_Value, CInt const &_Mask)
	{
		if ((_Value & _Mask) != 0)
			return fg_GetLowestBitSetNoZero(_Value);
		return -1;
	}
}

// A dependent call that can clear it in a template, the element of a dynamic
// index, a masked test, a lambda that clears it where it is made, and the
// complement of an integer class.
void fg_ClearForTemplate(unsigned &_Value);
template <typename t_CType>
void fg_ClearedInTemplate(t_CType _Value)
{
	if (_Value)
	{
		fg_ClearForTemplate(_Value);
		fg_GetHighestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	}
}
void fg_UseClearedInTemplate()
{
	fg_ClearedInTemplate(1u);
}
struct CIndexedBits
{
	umint m_Bits;
};
void fg_DynamicIndex(CIndexedBits const *_pInfo, unsigned _Index)
{
	if (_Index)
		fg_GetHighestBitSetNoZero(_pInfo[_Index].m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	if (_pInfo[_Index].m_Bits)
		fg_GetHighestBitSetNoZero(_pInfo[_Index].m_Bits);
}
void fg_MaskedZero(umint _Value)
{
	if (!(_Value & 1u))
		fg_GetHighestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	if (_Value & 1u)
		fg_GetHighestBitSetNoZero(_Value);
}
void fg_InvokedClear(umint _Value)
{
	if (!_Value)
		return;
	[&]
		{
			_Value = 0;
		}
	();
	fg_GetHighestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
namespace NComplementClass
{
	struct CInt128
	{
		umint m_Lower;
		umint m_Upper;
		constexpr CInt128(umint _Lower, umint _Upper) : m_Lower(_Lower), m_Upper(_Upper) {}
		constexpr CInt128 operator ~ () const
		{
			return {~m_Lower, ~m_Upper};
		}
		bool operator == (CInt128 const &) const = default;
	};
	template <typename tf_CInt>
	int fg_GetLowestBitSetNoZero(tf_CInt const &_Value);
	int fg_FreeEntry(CInt128 const &_Entry)
	{
		constexpr CInt128 c_FullySet(~umint(0), ~umint(0));
		if (_Entry != c_FullySet)
			return fg_GetLowestBitSetNoZero(CInt128(~_Entry));
		return -1;
	}
}

// The complement of a value compared with all ones by an ordinary operator,
// and a guard overwritten by the capture of a lambda.
struct CEqualInt
{
	unsigned m_Value;
	constexpr CEqualInt(unsigned _Value) : m_Value(_Value) {}
	constexpr CEqualInt operator ~ () const { return CEqualInt(~m_Value); }
	bool operator == (CEqualInt const &_Other) const { return m_Value == _Other.m_Value; }
};
int fg_FreeEqualEntry(CEqualInt const &_Entry)
{
	constexpr CEqualInt c_FullySet(~0u);
	if (_Entry == c_FullySet)
		return -1;
	return fg_GetLowestBitSetNoZero(CEqualInt(~_Entry));
}
void fg_CaptureOverwrites(umint _Value)
{
	if (_Value)
	{
		auto fCallback = [Value = (_Value = 0)] {};
		fg_GetHighestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	}
}

// A fact for the analysis that permits zero doesn't check it, a guarded
// integer class copied by value keeps the guard, and the polymorphic operand
// of a typeid is evaluated.
void fg_CompiledOutPermits(umint _Value, bool _bOther)
{
	DMibAnalyzerAssume(_Value != 0 || _bOther);
	fg_GetHighestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; {{.*}}
	DMibAnalyzerAssume(_Value == static_cast<umint>(0));
	fg_GetHighestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; {{.*}}
}
struct CCopiedInt
{
	umint m_Bits;
	explicit operator bool() const { return m_Bits != 0; }
};
template <typename t_CType>
t_CType fg_MostSignificantBitNoZero(t_CType _Value);
umint fg_CopiedGuard(CCopiedInt _Value)
{
	if (_Value)
		return fg_MostSignificantBitNoZero(_Value).m_Bits;
	return 0;
}
struct CPolymorphic
{
	virtual ~CPolymorphic();
};
void fg_EvaluatedTypeid(CPolymorphic &_Object)
{
	(void)typeid((fg_GetHighestBitSetNoZero(0u), _Object));
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'fg_GetHighestBitSetNoZero' is called with zero [malterlib-no-zero-bit-scan]
}

// A disjunction around a conjunction that checks it, and a scan written in a
// macro whose assertion compares something else with zero.
#define DScanInMacro(d_Value) fg_GetHighestBitSetNoZero(d_Value)
void fg_CompiledOutGrouped(umint _Value, bool _bFlag, bool _bOther)
{
	DMibAnalyzerAssume((_Value != 0 && _bFlag) || _bOther);
	fg_GetHighestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; {{.*}}
	DMibAnalyzerAssume((_Value != 0 && _bFlag) && _bOther);
	fg_GetHighestBitSetNoZero(_Value);
}
void fg_CompiledOutMacroScan(umint _Value)
{
	DMibAnalyzerAssume(_Value + 1 != 0);
	DScanInMacro(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	DMibAnalyzerAssume(_Value != 0);
	DScanInMacro(_Value);
}

// A constant of an integer class that isn't zero.
struct CWideInt
{
	umint m_Lower;
	umint m_Upper;
	constexpr CWideInt(umint _Lower) : m_Lower(_Lower), m_Upper(0) {}
};
void fg_ConstantWideInt()
{
	constexpr CWideInt c_One(1);
	fg_GetLowestBitSetNoZero(c_One);
	constexpr CWideInt c_Zero(0);
	fg_GetLowestBitSetNoZero(c_Zero);
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: {{.*}}
}

// A guard of what a call gives doesn't check what another call gives, also
// with the same arguments, which can give another object.
struct CCalledInfo
{
	umint m_Bits;
};
CCalledInfo &fg_CalledInfo(unsigned _Index);
void fg_GuardOtherCall()
{
	if (fg_CalledInfo(0).m_Bits)
		fg_GetHighestBitSetNoZero(fg_CalledInfo(1).m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; {{.*}}
	if (fg_CalledInfo(0).m_Bits)
		fg_GetHighestBitSetNoZero(fg_CalledInfo(0).m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; {{.*}}
}

// A wider all ones isn't the complement's zero, a member operator that changes
// its object, and the body of a lambda made in an unevaluated operand.
int fg_WiderAllOnes(unsigned _Entry)
{
	if (_Entry != ~0ull)
		return fg_GetLowestBitSetNoZero(~_Entry);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; {{.*}}
	return -1;
}
struct CClearingBits
{
	umint m_Value;
	void operator () () { m_Value = 0; }
};
void fg_ClearedByOperator(CClearingBits &_Bits)
{
	if (_Bits.m_Value)
	{
		_Bits();
		fg_GetHighestBitSetNoZero(_Bits.m_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; {{.*}}
	}
}
using CScanCallback = decltype([] { return fg_GetHighestBitSetNoZero(0u); });
// CHECK-MESSAGES: :[[@LINE-1]]:44: warning: 'fg_GetHighestBitSetNoZero' is called with zero [malterlib-no-zero-bit-scan]

// The grouping of a fact for the analysis, and a conversion whose other
// arguments are defaulted.
void fg_CompiledOutRegrouped(umint _Value)
{
	DMibAnalyzerAssume((_Value + 1) * 2 != 0);
	fg_GetLowestBitSetNoZero(_Value + 1 * 2);
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; {{.*}}
}
void fg_CompiledOutNestedDisjunction(umint _Value, bool _bA, bool _bB)
{
	DMibAnalyzerAssume(_Value != 0 && (_bA || _bB));
	fg_GetLowestBitSetNoZero(_Value);
}
struct CDefaultedInt
{
	umint m_Value;
	constexpr CDefaultedInt(umint _Value, umint = 0) : m_Value(_Value) {}
	constexpr bool operator == (CDefaultedInt const &_Other) const { return m_Value == _Other.m_Value; }
};
int fg_DefaultedZero(CDefaultedInt const &_Value)
{
	if (_Value == 0)
		return fg_GetLowestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: {{.*}}
	return 0;
}

// Ordered against zero: greater tells it isn't zero, at most that it can be.
void fg_OrderedGuards(umint _Value)
{
	if (_Value > 0)
		fg_GetHighestBitSetNoZero(_Value);
	if (_Value <= 0)
		fg_GetHighestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: {{.*}}
	if (_Value > 0)
		return;
	fg_GetHighestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: {{.*}}
}

// A write through a reference alias, a check right after a case label, and a
// discarded branch outside of a template.
void fg_Round51Alias(unsigned _Value)
{
	unsigned &Value = _Value;
	if (_Value)
	{
		Value = 0;
		fg_GetHighestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	}
}

void fg_Round51Case(unsigned _Value, int _Mode)
{
	switch (_Mode)
	{
	case 0:
		if (!_Value)
			return;
		fg_GetHighestBitSetNoZero(_Value);
		break;
	case 1:
		fg_GetHighestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
		break;
	}
}

void fg_Round51Discarded()
{
	if constexpr (false)
		fg_GetHighestBitSetNoZero(0u);
}

// A jump into a checked block passes the check, an ordered comparison of an
// integer class tells, and a local reference to the checked value is checked.
void fg_Round52Jump(unsigned _Value)
{
	goto Scan;
	if (_Value)
	{
	Scan:
		fg_GetHighestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; {{.*}}
	}
}

void fg_Round52CaseInChecked(unsigned _Value, int _Mode)
{
	if (_Value)
	{
		switch (_Mode)
		{
		case 0:
			fg_GetHighestBitSetNoZero(_Value);
			break;
		}
	}
}

struct CRound52Int
{
	unsigned m_Value;
	constexpr CRound52Int(unsigned _Value) : m_Value(_Value) {}
	bool operator <= (CRound52Int const &_Other) const;
	bool operator > (CRound52Int const &_Other) const;
};
void fg_Round52Ordered(CRound52Int const &_Value)
{
	if (_Value <= 0)
		fg_GetLowestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; {{.*}}
	if (_Value > 0)
		fg_GetLowestBitSetNoZero(_Value);
}

void fg_Round52Alias(unsigned _Value)
{
	unsigned const &Value = _Value;
	if (_Value)
		fg_GetHighestBitSetNoZero(Value);
}

// A lambda held in a local that clears the value, a write through a reference
// to the object, a reference that narrows, and the message of an assertion.
void fg_Round53Lambda(unsigned _Value)
{
	if (!_Value)
		return;
	auto fClear = [&] { _Value = 0; };
	fClear();
	fg_GetHighestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

struct CRound53Info
{
	unsigned m_Bits;
};
void fg_Round53Alias(CRound53Info &_Info)
{
	CRound53Info &Alias = _Info;
	if (!_Info.m_Bits)
		return;
	fg_GetHighestBitSetNoZero(Alias.m_Bits);
	Alias.m_Bits = 0;
	fg_GetHighestBitSetNoZero(_Info.m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

void fg_Round53Narrowing(unsigned _Value)
{
	if (!_Value)
		return;
	unsigned char const &Small = _Value;
	fg_GetHighestBitSetNoZero(Small);
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

void fg_Round53Fail(char const *_pMessage);
char const *fg_Round53Message(unsigned _Value);
#define DMibSafeCheck(d_Expression, d_Message) { if (!(d_Expression)) fg_Round53Fail(d_Message); }
void fg_Round53AssertMessage(unsigned _Value)
{
	DMibSafeCheck(_Value == 0, fg_Round53Message(_Value));
	fg_GetHighestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

// A reference that refers to itself, an element through a reference, loops
// that run once, an assertion that changes the value, and a check compared
// with `false`.
struct CRound54Info
{
	CRound54Info &m_Next;
	unsigned m_Bits;
};
int fg_Round54Cycle()
{
	CRound54Info &Alias = Alias.m_Next;
	return fg_GetHighestBitSetNoZero(Alias.m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
int fg_Round54Element(unsigned const *_pValues)
{
	unsigned const &Value = _pValues[0];
	if (_pValues[0])
		return fg_GetHighestBitSetNoZero(Value);
	return -1;
}
int fg_Round54Once(unsigned _Value)
{
	if (!_Value)
		return -1;
	int Result = 0;
	do
	{
		Result = fg_GetHighestBitSetNoZero(_Value);
		_Value = 0;
	}
	while (false);
	return Result;
}
int fg_Round54Break(unsigned _Value)
{
	if (!_Value)
		return -1;
	int Result = 0;
	for (;;)
	{
		Result = fg_GetHighestBitSetNoZero(_Value);
		_Value = 0;
		break;
	}
	return Result;
}
int fg_Round54Asserted(unsigned _Value)
{
	DMibCheck((_Value != 0) && (--_Value, true));
	return fg_GetHighestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
int fg_Round54ComparedWithFalse(unsigned _Value)
{
	if ((_Value == 0) == false)
		return fg_GetHighestBitSetNoZero(_Value);
	return -1;
}

// A fact for the analysis checks the value before an implicit conversion
// that can make it zero.
int fg_Round55Narrowed(unsigned _Value)
{
	DMibAnalyzerAssume(_Value != 0);
	return fg_GetLowestBitSetNoZero<unsigned char>(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; {{.*}}
}
int fg_Round55Widened(unsigned char _Value)
{
	DMibAnalyzerAssume(_Value != 0);
	return fg_GetLowestBitSetNoZero<unsigned>(_Value);
}

// A write through a local pointer to it invalidates a check.
int fg_Round55PointerWrite(umint _Value)
{
	umint *pValue = &_Value;
	if (!_Value)
		return -1;
	*pValue = 0;
	return fg_GetHighestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; {{.*}}
}
int fg_Round55PointerAssigned(umint _Value)
{
	umint *pValue = nullptr;
	pValue = &_Value;
	umint *pCopy = pValue;
	if (!_Value)
		return -1;
	*pCopy = 0;
	return fg_GetHighestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; {{.*}}
}
int fg_Round55PointerElsewhere(umint _Value, umint _Other)
{
	umint *pOther = &_Other;
	if (!_Value)
		return -1;
	*pOther = 0;
	return fg_GetHighestBitSetNoZero(_Value);
}

// An integer class with a bit set by construction, `_Value | CInt(1)`.
struct CRound55Int
{
	umint m_Upper{};
	umint m_Lower{};
	constexpr CRound55Int(umint _Value) : m_Lower(_Value) {}
	constexpr CRound55Int operator | (CRound55Int const &_Other) const
	{
		CRound55Int Ret(0);
		Ret.m_Upper = m_Upper | _Other.m_Upper;
		Ret.m_Lower = m_Lower | _Other.m_Lower;
		return Ret;
	}
};
int fg_Round55IntegerClass(CRound55Int const &_Value)
{
	return fg_GetLowestBitSetNoZero(_Value | CRound55Int(1)) + fg_GetLowestBitSetNoZero(_Value | CRound55Int(0));
// CHECK-MESSAGES: :[[@LINE-1]]:61: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; {{.*}}
}

// A reference bound to itself ends, each pointer of a declaration writes, and
// a call can give another value than a call tested before.
umint fg_Round56NextBits();
constexpr umint fg_Round56Constant(umint _Value)
{
	return _Value | 1;
}
int fg_Round56SelfAlias()
{
	umint const &Value = Value;
	return fg_GetHighestBitSetNoZero(Value);
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; {{.*}}
}
int fg_Round56Declarators(umint _Value)
{
	if (!_Value)
		return -1;
	umint *pFirst = &_Value, *pSecond = &_Value;
	*pFirst = 0;
	(void)pSecond;
	return fg_GetHighestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; {{.*}}
}
int fg_Round56Calls(umint _Value)
{
	if (fg_Round56NextBits() != 0)
		return fg_GetHighestBitSetNoZero(fg_Round56NextBits());
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; {{.*}}
	if (fg_Round56Constant(_Value) != 0)
		return fg_GetHighestBitSetNoZero(fg_Round56Constant(_Value));
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; {{.*}}
	return -1;
}

// A constexpr call that changes what it is given can give another value, and
// an aggregate that refers or points to the value can change it.
constexpr umint fg_Round57Next(umint &_Value)
{
	return _Value--;
}
int fg_Round57Mutating(umint _Value)
{
	if (fg_Round57Next(_Value))
		return fg_GetHighestBitSetNoZero(fg_Round57Next(_Value));
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; {{.*}}
	return 0;
}
struct CRound57Clear
{
	umint &m_Value;
	~CRound57Clear()
	{
		m_Value = 0;
	}
};
int fg_Round57Destroyed(umint _Value)
{
	if (!_Value)
		return 0;
	{
		CRound57Clear Clear{_Value};
	}
	return fg_GetHighestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; {{.*}}
}
int fg_Round57Aggregate(umint _Value)
{
	if (!_Value)
		return 0;
	struct CPointer
	{
		umint *m_pValue;
	};
	CPointer Pointer{&_Value};
	*Pointer.m_pValue = 0;
	return fg_GetHighestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; {{.*}}
}

// A write through a local pointer to a member, `pInfo->m_Bits = 0`, and a
// call operator that changes its object.
struct CRound58Info
{
	umint m_Bits;
};
int fg_Round58Arrow(CRound58Info &_Info)
{
	CRound58Info *pInfo = &_Info;
	if (_Info.m_Bits)
	{
		pInfo->m_Bits = 0;
		return fg_GetHighestBitSetNoZero(_Info.m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; {{.*}}
	}
	return 0;
}
struct CRound58Next
{
	umint m_Next = 1;
	umint operator()()
	{
		return m_Next--;
	}
};
int fg_Round58Stateful(CRound58Next &_Next)
{
	if (_Next())
		return fg_GetHighestBitSetNoZero(_Next());
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; {{.*}}
	return 0;
}

// An instantiation that narrows what the template checks, the increment of a
// `for` that the condition checks, and assigning a pointer to the value.
template <typename t_CType>
int fg_Round59Scan(t_CType _Value)
{
	if (_Value)
		return fg_GetLowestBitSetNoZero<unsigned char>(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; {{.*}}
	return -1;
}
int g_Round59Result = fg_Round59Scan(256u);
void fg_Round59ClearBits(unsigned _Bits)
{
	for (; _Bits; _Bits &= ~(1u << fg_GetLowestBitSetNoZero(_Bits)))
	{
	}
}
int fg_Round59Rebound(umint _Value)
{
	umint *pValue = &_Value;
	if (!_Value)
		return -1;
	pValue = nullptr;
	(void)pValue;
	return fg_GetHighestBitSetNoZero(_Value);
}

// A condition that clears the value it tested, a write through a copy of a
// pointer to the value, and calling a copy of a lambda that clears it.
void fg_Round60ClearedInCondition(unsigned _Value)
{
	if (_Value != 0 ? (_Value = 0, true) : false)
		fg_GetHighestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; {{.*}}
}
void fg_Round60PointerCopy(unsigned *_pValue)
{
	if (!*_pValue)
		return;
	unsigned *pAlias = _pValue;
	*pAlias = 0;
	fg_GetHighestBitSetNoZero(*_pValue);
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; {{.*}}
}
void fg_Round60LambdaCopy(unsigned _Value)
{
	if (!_Value)
		return;
	auto fClear = [&] { _Value = 0; };
	auto fCopy = fClear;
	fCopy();
	fg_GetHighestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; {{.*}}
}

// Pointing the pointer that selects a checked member elsewhere, and a
// conversion to bool that changes the value it tests.
struct CRound61Info
{
	umint m_Bits;
};
int fg_Round61Pointer(CRound61Info *_pInfo, CRound61Info *_pOther)
{
	if (!_pInfo->m_Bits)
		return -1;
	_pInfo = _pOther;
	return fg_GetHighestBitSetNoZero(_pInfo->m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; {{.*}}
}
struct CRound61Clearing
{
	umint m_Bits;
	explicit operator bool()
	{
		umint Value = m_Bits;
		m_Bits = 0;
		return Value != 0;
	}
};
int fg_Round61Clearing(CRound61Clearing _Bits)
{
	if (_Bits)
		return fg_GetHighestBitSetNoZero(_Bits.m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; {{.*}}
	return -1;
}

// Pointing the pointer that an element is read through elsewhere, not a
// reference bound before, and a scan in a `typeid` that isn't evaluated.
int fg_Round62Element(umint *_pBits, umint *_pOther)
{
	if (!_pBits[0])
		return 0;
	_pBits = _pOther;
	return fg_GetHighestBitSetNoZero(_pBits[0]);
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; {{.*}}
}
struct CRound62Info
{
	umint m_Bits;
};
int fg_Round62Bound(CRound62Info *_pInfo, CRound62Info *_pOther)
{
	umint const &Bits = _pInfo->m_Bits;
	if (!Bits)
		return 0;
	_pInfo = _pOther;
	return fg_GetHighestBitSetNoZero(Bits);
}
namespace std
{
	struct type_info;
}
struct CRound62Polymorphic
{
	virtual ~CRound62Polymorphic();
};
bool fg_Round62Typeid(CRound62Polymorphic &_Object)
{
	return noexcept(typeid((fg_GetHighestBitSetNoZero(0), _Object)));
}

// An instantiation that converts the argument with a constructor of a class,
// a rewritten comparison that changes the value, a write that doesn't run, and
// a pointer pointed elsewhere before it writes.
struct CRound63Byte
{
	unsigned char m_Value;
	constexpr CRound63Byte(unsigned _Value) : m_Value(_Value)
	{
	}
};
template <typename t_CType>
int fg_Round63Narrowed(t_CType _Value)
{
	if (_Value != 0)
		return fg_GetLowestBitSetNoZero<CRound63Byte>(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	return -1;
}
int g_Round63Narrowed = fg_Round63Narrowed(256u);
struct CRound63Int
{
	umint m_Value;
	bool operator==(umint _Other)
	{
		umint Old = m_Value;
		m_Value = 0;
		return Old == _Other;
	}
};
int fg_Round63Rewritten(CRound63Int _Value)
{
	if (_Value != 0)
		return fg_GetLowestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	return -1;
}
void fg_Round63NotRun(umint _Value)
{
	if (_Value)
	{
		if constexpr (false)
			_Value = 0;
		(void)sizeof(_Value = 0);
		fg_GetHighestBitSetNoZero(_Value);
	}
}
int fg_Round63Rebound(umint _Value, umint _Other)
{
	umint *pValue = &_Value;
	if (!_Value)
		return -1;
	pValue = &_Other;
	*pValue = 0;
	return fg_GetHighestBitSetNoZero(_Value);
}

// An argument that changes the value isn't what a test of the same expression
// tested, and a comparison with a constant tells whether zero passes it.
int fg_Round64Repeated(umint _Value)
{
	if (--_Value)
		return fg_GetHighestBitSetNoZero(--_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; {{.*}}
	return -1;
}
int fg_Round64NotOne(umint _Value)
{
	if (_Value != 1)
		return fg_GetHighestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; {{.*}}
	return -1;
}
int fg_Round64Below(umint _Value)
{
	if (_Value < 10)
		return fg_GetHighestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; {{.*}}
	return -1;
}
int fg_Round64Above(umint _Value)
{
	if (_Value > 10)
		return fg_GetHighestBitSetNoZero(_Value);
	return -1;
}

// A write through a copy of a pointer to another element or to a member the
// argument reads, a comparison of an integer class with a constant, a change
// the condition tests the result of, and a read through a pointer before it
// points elsewhere.
void fg_Round65Element(umint *_pValues)
{
	if (!_pValues[1])
		return;
	umint *pAlias = _pValues;
	pAlias[1] = 0;
	fg_GetHighestBitSetNoZero(_pValues[1]);
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; {{.*}}
}
struct CRound65Class
{
	umint m_Value;
	bool operator==(umint _Other) const
	{
		return m_Value == _Other;
	}
};
void fg_Round65Class(CRound65Class const &_Value)
{
	if (_Value != 1u)
		fg_GetLowestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; {{.*}}
}
void fg_Round65Prefix(umint _Value)
{
	if (++_Value)
		fg_GetHighestBitSetNoZero(_Value);
}
void fg_Round65ReadFirst(umint _Value, umint _Other)
{
	if (!_Value)
		return;
	umint *pValue = &_Value;
	umint Copy = *pValue;
	pValue = &_Other;
	*pValue = Copy - Copy;
	fg_GetHighestBitSetNoZero(_Value);
}

// Pointing the pointer elsewhere through a reference to it undoes the test of
// what it points to; and a reference bound through a pointer before it is
// pointed elsewhere isn't what a test of the new object checks.
struct CRound66Info
{
	umint m_Bits;
};
int fg_Round66Alias(CRound66Info *_pInfo, CRound66Info *_pOther)
{
	if (_pInfo->m_Bits)
	{
		CRound66Info *&pAlias = _pInfo;
		pAlias = _pOther;
		return fg_GetHighestBitSetNoZero(_pInfo->m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	}
	return 0;
}
int fg_Round66Bound(CRound66Info *_pInfo, CRound66Info *_pOther)
{
	umint const &Bits = _pInfo->m_Bits;
	_pInfo = _pOther;
	if (_pInfo->m_Bits)
		return fg_GetLowestBitSetNoZero(Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	return 0;
}
int fg_Round66BoundChecked(CRound66Info *_pInfo, CRound66Info *_pOther)
{
	umint const &Bits = _pInfo->m_Bits;
	_pInfo = _pOther;
	if (Bits)
		return fg_GetLowestBitSetNoZero(Bits);
	return 0;
}

// A comparison with all ones that can change what it compares doesn't tell
// what its complement is after.
struct CRound67Bits
{
	unsigned m_Bits;
	bool operator!=(CRound67Bits const &_Other)
	{
		unsigned Bits = m_Bits;
		m_Bits = ~0u;
		return Bits != _Other.m_Bits;
	}
	CRound67Bits operator~() const
	{
		return {~m_Bits};
	}
};
int fg_Round67Complement(CRound67Bits _Bits)
{
	if (_Bits != CRound67Bits{~0u})
		return fg_GetLowestBitSetNoZero(CRound67Bits(~_Bits));
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	return -1;
}

// A condition of arithmetic that can be other than zero when the argument is
// doesn't check it; a pointer captured by a lambda can write it; and the
// members of a union share their storage.
void fg_Round68Arithmetic(umint _Value)
{
	if (_Value | 1u)
		fg_GetHighestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	if (_Value - 1)
		fg_GetHighestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
void fg_Round68Capture(umint _Value)
{
	if (!_Value)
		return;
	[pValue = &_Value]
		{
			*pValue = 0;
		}
		()
	;
	fg_GetHighestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
struct CRound68Entry
{
	umint m_Bits;
};
union CRound68Bits
{
	CRound68Entry m_Read;
	CRound68Entry m_Write;
};
void fg_Round68Union(CRound68Bits &_Bits)
{
	if (!_Bits.m_Read.m_Bits)
		return;
	_Bits.m_Write.m_Bits = 0;
	fg_GetHighestBitSetNoZero(_Bits.m_Read.m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

// An aggregate made with parentheses that refers to it can change it; a loop
// over it by reference changes it; and a remainder is zero when it is.
struct CRound69Clear
{
	umint &m_Value;
	~CRound69Clear()
	{
		m_Value = 0;
	}
};
void fg_Round69Paren(umint _Value)
{
	if (!_Value)
		return;
	{
		CRound69Clear Clear(_Value);
	}
	fg_GetHighestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
void fg_Round69Loop(umint (&_Values)[1])
{
	if (!_Values[0])
		return;
	for (auto &Value : _Values)
		Value = 0;
	fg_GetHighestBitSetNoZero(_Values[0]);
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
void fg_Round69Remainder(umint _Value)
{
	if (_Value % 4 != 0)
		fg_GetHighestBitSetNoZero(_Value);
}

// A constant of an integer class that is zero is zero, and a conversion to
// bool of what can be other than zero when the argument is doesn't check it.
struct CRound70Int
{
	unsigned m_Value;
	constexpr CRound70Int(unsigned _Value)
		: m_Value(_Value)
	{
	}
	bool operator==(CRound70Int const &_Other) const
	{
		return m_Value == _Other.m_Value;
	}
	explicit operator bool() const
	{
		return m_Value != 0;
	}
	CRound70Int operator-(CRound70Int const &_Other) const
	{
		return CRound70Int(m_Value - _Other.m_Value);
	}
};
constexpr CRound70Int gc_Round70Zero(0);
int fg_Round70Constant(CRound70Int const &_Value)
{
	if (_Value == gc_Round70Zero)
		return fg_GetLowestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	return -1;
}
int fg_Round70Difference(CRound70Int const &_Value)
{
	if (_Value - CRound70Int(1))
		return fg_GetLowestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	return -1;
}

// Each expansion of a macro in a template is its own call; distinct members
// of a union aren't checked by each other; and a label nested in a statement
// before can be jumped to past a check.
#define DRound71Scan(d_Value) fg_GetHighestBitSetNoZero(d_Value)
template <typename t_CType>
int fg_Round71Checked(t_CType _Value)
{
	if (_Value)
		return DRound71Scan(_Value);
	return -1;
}
template <typename t_CType>
int fg_Round71Unchecked(t_CType _Value)
{
	return DRound71Scan(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
int fg_Round71Use(umint _Value)
{
	return fg_Round71Checked(_Value) + fg_Round71Unchecked(_Value);
}
union CRound71Union
{
	struct CPair
	{
		umint m_First;
		umint m_Second;
	} m_Pair;
	umint m_Other;
};
int fg_Round71Union(CRound71Union const &_Value)
{
	if (_Value.m_Pair.m_First)
		return fg_GetHighestBitSetNoZero(_Value.m_Pair.m_Second);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	return -1;
}
int fg_Round71Label(umint _Value, bool _bJump)
{
	if (_bJump)
		goto Target;
	if (!_Value)
		return -1;
	{
	Target:;
	}
	return fg_GetHighestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

// Given to a member function through a pointer to it, or as a pointer read
// through another, it can be changed; and an overloaded `||` of an integer
// class tells nothing of each side.
struct CRound72Worker
{
	void f_Clear(umint &_Bits)
	{
		_Bits = 0;
	}
};
int fg_Round72Member(umint _Bits, CRound72Worker &_Worker, void (CRound72Worker::*_fClear)(umint &_Bits))
{
	if (!_Bits)
		return -1;
	(_Worker.*_fClear)(_Bits);
	return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
void fg_Round72Clear(umint *_pBits);
int fg_Round72Deref(umint **_ppBits)
{
	if (**_ppBits)
	{
		fg_Round72Clear(*_ppBits);
		return fg_GetLowestBitSetNoZero(**_ppBits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	}
	return -1;
}
struct CRound72Int
{
	unsigned m_Value;
	constexpr CRound72Int(unsigned _Value)
		: m_Value(_Value)
	{
	}
	constexpr bool operator||(CRound72Int const &_Other) const
	{
		return m_Value != 0 || _Other.m_Value != 0;
	}
};
int fg_Round72Or(CRound72Int const &_Bits)
{
	if (_Bits || CRound72Int(1))
		return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	return -1;
}

// Compared with a constant through what doesn't keep zero, which is true for
// zero too.
void fg_Round73Compared(unsigned _Bits)
{
	if ((_Bits | 1u) == 1u)
		fg_GetHighestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	if ((_Bits & 6u) == 2u)
		fg_GetHighestBitSetNoZero(_Bits);
}

// Written through a pointer moved by a constant.
void fg_Round73Moved(unsigned *_pBits)
{
	if (_pBits[1])
	{
		unsigned *pAlias = _pBits + 1;
		*pAlias = 0;
		fg_GetHighestBitSetNoZero(_pBits[1]);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	}
	if (_pBits[2])
	{
		unsigned *pAlias = _pBits + 1;
		*pAlias = 0;
		fg_GetHighestBitSetNoZero(_pBits[2]);
	}
}

// A later iteration of a `do` loop runs after its condition held.
void fg_Round73DoLoop(unsigned _Bits)
{
	if (!_Bits)
		return;
	do
	{
		fg_GetLowestBitSetNoZero(_Bits);
		_Bits &= _Bits - 1;
	}
	while (_Bits);
	_Bits = 6;
	do
	{
		fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
		_Bits &= _Bits - 1;
	}
	while (_Bits != 1);
}

// A loop that runs its body once still runs its initializer and condition
// first.
void fg_Round74Once(unsigned _Bits)
{
	if (_Bits)
	{
		for (_Bits = 0; ;)
		{
			fg_GetHighestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:4: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
			break;
		}
	}
	if (_Bits)
	{
		while ((_Bits = 0, true))
		{
			fg_GetHighestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:4: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
			break;
		}
	}
}

// A zero of an integer class made of several words.
struct CRound74Wide
{
	unsigned long m_Upper;
	unsigned long m_Lower;
	constexpr CRound74Wide(unsigned long _Upper, unsigned long _Lower)
		: m_Upper(_Upper)
		, m_Lower(_Lower)
	{
	}
	bool operator==(CRound74Wide const &_Other) const = default;
};
int fg_GetHighestBitSetNoZero(CRound74Wide const &_Value);
void fg_Round74Wide(CRound74Wide const &_Value)
{
	if (_Value == CRound74Wide(0, 0))
		fg_GetHighestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	if (!(_Value == CRound74Wide(0, 0)))
		fg_GetHighestBitSetNoZero(_Value);
}

// Written through a reference or pointer an aggregate holds, or a pointer
// chosen by a conditional; and a scan in the initializer of a `for` runs
// once, before the loop writes.
struct CRound75Borrowed
{
	unsigned &m_Bits;
};
struct CRound75Pointed
{
	unsigned *m_pBits;
};
int fg_Round75Aggregate(unsigned _Bits, bool _bChoose)
{
	CRound75Borrowed Borrowed{_Bits};
	CRound75Pointed Pointed{&_Bits};
	if (_Bits != 0)
	{
		Borrowed.m_Bits = 0;
		return fg_GetHighestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	}
	if (_Bits != 0)
	{
		*Pointed.m_pBits = 0;
		return fg_GetHighestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	}
	unsigned Other = 1;
	unsigned *pChosen = _bChoose ? &_Bits : &Other;
	if (_Bits != 0)
	{
		*pChosen = 0;
		return fg_GetHighestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	}
	return -1;
}
void fg_Round75ForInit(unsigned _Bits)
{
	if (_Bits == 0)
		return;
	for (int iBit = fg_GetHighestBitSetNoZero(_Bits); iBit >= 0; --iBit)
		_Bits = 0;
}

// A member function of the object changes its members, a pointer through a
// reference to it is the pointer, and the elements of a braced list run in
// order.
struct CRound76Bits
{
	unsigned m_Bits;
	void f_Clear()
	{
		m_Bits = 0;
	}
	int f_Scan()
	{
		if (m_Bits)
		{
			f_Clear();
			return fg_GetLowestBitSetNoZero(m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
		}
		return -1;
	}
};
int fg_Round76Reference(unsigned _Bits)
{
	unsigned *pBits = &_Bits;
	unsigned *&pAlias = pBits;
	if (_Bits != 0)
	{
		*pAlias = 0;
		return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	}
	return -1;
}
struct CRound76Result
{
	int m_Index;
	unsigned m_Remaining;
};
CRound76Result fg_Round76Pop(unsigned _Bits)
{
	if (_Bits == 0)
		return {-1, 0};
	return {fg_GetLowestBitSetNoZero(_Bits), _Bits &= _Bits - 1};
}

// A call through a pointer to a member function that isn't const can change
// what was checked.
struct CRound77Worker
{
	unsigned m_Bits;
	void f_Clear()
	{
		m_Bits = 0;
	}
	unsigned f_Get() const
	{
		return m_Bits;
	}
};
int fg_Round77MemberPointer(CRound77Worker &_Worker)
{
	auto fClear = &CRound77Worker::f_Clear;
	auto fGet = &CRound77Worker::f_Get;
	if (!_Worker.m_Bits)
		return 0;
	(_Worker.*fGet)();
	int Result = fg_GetHighestBitSetNoZero(_Worker.m_Bits);
	(_Worker.*fClear)();
	return Result + fg_GetHighestBitSetNoZero(_Worker.m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

// A complement is zero when the value is all ones, a negation only when it is
// zero.
int fg_Round77Complement(unsigned _Bits)
{
	if (~_Bits)
		return fg_GetHighestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	return 0;
}
int fg_Round77Negation(int _Bits)
{
	if (-_Bits)
		return fg_GetHighestBitSetNoZero(_Bits);
	return 0;
}

// An aggregate initialized with parentheses refers to the value too.
struct CRound78Borrowed
{
	umint &m_Bits;
};
void fg_Round78Parens(umint _Bits)
{
	CRound78Borrowed Borrowed(_Bits);
	if (!_Bits)
		return;
	Borrowed.m_Bits = 0;
	fg_GetHighestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

// What the initializer of the check makes is destroyed after it.
struct CRound78Clear
{
	umint &m_Bits;
	~CRound78Clear()
	{
		m_Bits = 0;
	}
};
void fg_Round78Destroyed(umint _Bits)
{
	if (CRound78Clear Clear{_Bits}; !_Bits)
		return;
	fg_GetHighestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

// A lambda changes only its own copy of what it captures by copy.
void fg_Round78Copy(umint _Bits)
{
	if (!_Bits)
		return;
	auto fCopy = [_Bits]() mutable { _Bits = 0; };
	fCopy();
	auto fAll = [=]() mutable { _Bits = 0; };
	fAll();
	fg_GetHighestBitSetNoZero(_Bits);
}

// An aggregate alias through a base, a temporary of the condition destroyed
// after it is tested, both sides of an overloaded `&&`, and a pointer pointed
// elsewhere only before the reference is bound.
struct CRound79Borrowed
{
	umint &m_Bits;
};
struct CRound79Derived : CRound79Borrowed
{
};
int fg_Round79Base(umint _Bits)
{
	CRound79Derived Borrowed{{_Bits}};
	if (_Bits)
	{
		Borrowed.m_Bits = 0;
		return fg_GetHighestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	}
	return -1;
}
struct CRound79Clear
{
	umint &m_Bits;
	~CRound79Clear()
	{
		m_Bits = 0;
	}
};
int fg_Round79Temporary(umint _Bits)
{
	if ((CRound79Clear{_Bits}, _Bits))
		return fg_GetHighestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	return -1;
}
struct CRound79Int
{
	umint m_Value;
	CRound79Int(umint _Value) : m_Value(_Value) {}
	explicit operator bool() const
	{
		return m_Value != 0;
	}
};
bool operator&&(CRound79Int _Left, CRound79Int _Right)
{
	return bool(_Left) && bool(_Right);
}
CRound79Int fg_Round79Clear(CRound79Int &_Value)
{
	_Value.m_Value = 0;
	return CRound79Int(1);
}
int fg_GetHighestBitSetNoZero(CRound79Int const &_Value);
int fg_Round79Overloaded(CRound79Int _Value)
{
	if (_Value && fg_Round79Clear(_Value))
		return fg_GetHighestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	return -1;
}
struct CRound79Info
{
	umint m_Bits;
};
int fg_Round79Repointed(CRound79Info *_pInfo, CRound79Info *_pOther)
{
	_pInfo = _pOther;
	umint const &Bits = _pInfo->m_Bits;
	if (_pInfo->m_Bits != 0)
		return fg_GetHighestBitSetNoZero(Bits);
	_pInfo = nullptr;
	return -1;
}

// A const member function can change what is mutable.
struct CRound80Info
{
	mutable umint m_Bits;
	void f_Clear() const
	{
		m_Bits = 0;
	}
	umint f_Get() const
	{
		return m_Bits;
	}
};
void fg_Round80Mutable(CRound80Info const &_Info)
{
	if (_Info.m_Bits)
	{
		_Info.f_Get();
		fg_GetHighestBitSetNoZero(_Info.m_Bits);
		_Info.f_Clear();
		fg_GetHighestBitSetNoZero(_Info.m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	}
}

// A const member function can change what is mutable through a local
// reference to it, and a loop that always returns doesn't go on.
struct CRound81Info
{
	mutable umint m_Bits;
	void f_Clear() const
	{
		auto &Bits = m_Bits;
		Bits = 0;
	}
};
int fg_Round81LocalReference(CRound81Info const &_Info)
{
	if (!_Info.m_Bits)
		return 0;
	_Info.f_Clear();
	return fg_GetHighestBitSetNoZero(_Info.m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
int fg_Round81Returns(umint _Bits)
{
	if (!_Bits)
		return 0;
	do
	{
		return fg_GetHighestBitSetNoZero(_Bits);
	}
	while (--_Bits);
}

// Nested awaits are looked at once each, not once per path through the
// operands, which is exponential in their depth.
namespace std
{
	template <typename t_CResult, typename... tp_CArguments>
	struct coroutine_traits
	{
		using promise_type = typename t_CResult::promise_type;
	};
	template <typename t_CPromise = void>
	struct coroutine_handle
	{
		static coroutine_handle from_address(void *_pAddress) noexcept;
	};
}
struct CRound82Awaitable
{
	bool await_ready() noexcept;
	template <typename t_CHandle>
	void await_suspend(t_CHandle _Handle) noexcept;
	umint await_resume() noexcept;
};
struct CRound82Task
{
	struct promise_type
	{
		CRound82Task get_return_object();
		CRound82Awaitable initial_suspend();
		CRound82Awaitable final_suspend() noexcept;
		void return_void();
		void unhandled_exception();
	};
};
CRound82Awaitable fg_Round82Work(umint _Value);
#define DRound82Await(d_Value) co_await fg_Round82Work(d_Value)
#define DRound82Await4(d_Value) DRound82Await(DRound82Await(DRound82Await(DRound82Await(d_Value))))
#define DRound82Await32(d_Value) DRound82Await4(DRound82Await4(DRound82Await4(DRound82Await4(DRound82Await4(DRound82Await4(DRound82Await4(DRound82Await4(d_Value))))))))
CRound82Task fg_Round82Nested(umint _Bits)
{
	if (_Bits)
		fg_GetHighestBitSetNoZero(DRound82Await32(_Bits));
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

// A const member function can write what a pointer member points to, also
// through a subscript, and what a reference member refers to, a const
// conversion can change what is mutable, and a comparison rewritten through
// `operator <=>` can change the value.
struct CRound82Pointer
{
	umint *m_pBits;
	void f_Clear() const
	{
		m_pBits[0] = 0;
	}
	void f_Run() const
	{
		if (m_pBits[0])
		{
			f_Clear();
			fg_GetHighestBitSetNoZero(m_pBits[0]);
// CHECK-MESSAGES: :[[@LINE-1]]:4: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
		}
	}
};
struct CRound82Reference
{
	umint &m_Bits;
	void f_Clear() const
	{
		m_Bits = 0;
	}
	void f_Run() const
	{
		if (m_Bits)
		{
			f_Clear();
			fg_GetHighestBitSetNoZero(m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:4: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
		}
	}
};
struct CRound82Bits
{
	mutable umint m_Bits;
	explicit operator bool() const
	{
		bool bNonZero = m_Bits != 0;
		m_Bits = 0;
		return bNonZero;
	}
};
int fg_GetHighestBitSetNoZero(CRound82Bits const &_Bits);
void fg_Round82Conversion(CRound82Bits const &_Bits)
{
	if (_Bits)
		fg_GetHighestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
struct CRound82Ordered
{
	umint m_Bits;
	int operator<=>(umint _Other)
	{
		umint Old = m_Bits;
		m_Bits = 0;
		return Old > _Other ? 1 : Old < _Other ? -1 : 0;
	}
};
int fg_GetHighestBitSetNoZero(CRound82Ordered const &_Bits);
void fg_Round82Ordered(CRound82Ordered &_Bits)
{
	if (_Bits > 0u)
		fg_GetHighestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

// A const member function that calls another that changes what is mutable,
// one defined elsewhere of an object with a pointer member, a const
// comparison that changes what is mutable, a copy of a local reference that
// a lambda changes, and writes that don't run.
struct CRound83Info
{
	mutable umint m_Bits;
	umint *m_pBits;
	void f_Clear() const
	{
		m_Bits = 0;
	}
	void f_Helper() const
	{
		f_Clear();
	}
	void f_Elsewhere() const;
	void f_Discarded() const
	{
		if constexpr (false)
			m_Bits = 0;
		(void)sizeof(m_Bits = 0);
	}
};
void fg_Round83Helper(CRound83Info const &_Info)
{
	if (_Info.m_Bits)
	{
		_Info.f_Helper();
		fg_GetHighestBitSetNoZero(_Info.m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	}
	if (*_Info.m_pBits)
	{
		_Info.f_Elsewhere();
		fg_GetHighestBitSetNoZero(*_Info.m_pBits);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	}
	if (_Info.m_Bits)
	{
		_Info.f_Discarded();
		fg_GetHighestBitSetNoZero(_Info.m_Bits);
	}
}
struct CRound83Compared
{
	mutable umint m_Bits;
	bool operator!=(umint _Other) const
	{
		bool bResult = m_Bits != _Other;
		m_Bits = 0;
		return bResult;
	}
};
int fg_GetHighestBitSetNoZero(CRound83Compared const &_Bits);
void fg_Round83Compared(CRound83Compared const &_Bits)
{
	if (_Bits != 0)
		fg_GetHighestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
void fg_Round83CopiedReference(umint _Bits)
{
	umint &Bits = _Bits;
	if (_Bits)
	{
		[Bits]() mutable { Bits = 0; }();
		fg_GetHighestBitSetNoZero(_Bits);
	}
}

// Written through a reference bound to either of a conditional, or through a
// cast to a reference.
void fg_Round84Either(umint _Bits, umint _Other, bool _bFirst)
{
	if (!_Bits)
		return;
	umint &Alias = _bFirst ? _Bits : _Other;
	Alias = 0;
	fg_GetHighestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
void fg_Round84Cast(umint _Bits)
{
	if (!_Bits)
		return;
	umint &Alias = static_cast<umint &>(_Bits);
	Alias = 0;
	fg_GetHighestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

// Written through a structured binding, through a local copy of a pointer
// member and through an array of pointers in a const member function, but not
// by what gets a mutable member by value.
struct CRound85Info
{
	umint m_Bits;
};
int fg_Round85Binding(CRound85Info &_Info)
{
	auto &[Bits] = _Info;
	if (_Info.m_Bits)
	{
		Bits = 0;
		return fg_GetHighestBitSetNoZero(_Info.m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	}
	return -1;
}
void fg_Round85Observe(umint _Value) {}
struct CRound85Pointers
{
	umint *m_pBits;
	umint *m_pElements[1];
	mutable umint m_Mutable;
	void f_ClearCopy() const
	{
		auto pBits = m_pBits;
		*pBits = 0;
	}
	void f_ClearElement() const
	{
		*m_pElements[0] = 0;
	}
	void f_Observe() const
	{
		fg_Round85Observe(m_Mutable);
	}
	int f_Scan() const
	{
		if (*m_pBits)
		{
			f_ClearCopy();
			return fg_GetHighestBitSetNoZero(*m_pBits);
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
		}
		if (*m_pElements[0])
		{
			f_ClearElement();
			return fg_GetHighestBitSetNoZero(*m_pElements[0]);
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
		}
		if (m_Mutable)
		{
			f_Observe();
			return fg_GetHighestBitSetNoZero(m_Mutable);
		}
		return -1;
	}
};

// A condition converted to an integer, or chosen by a conditional, that is
// true only for zero doesn't check it; a lambda called through a pointer
// writes what it captures; a pointer pointed elsewhere before a write doesn't
// write the argument, but one pointed at it again in a loop does the next
// time around.
void fg_Round86Casts(unsigned _Bits)
{
	if (static_cast<unsigned>(_Bits == 0))
		fg_GetHighestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	if (_Bits ? false : true)
		fg_GetHighestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	if (_Bits ? true : false)
		fg_GetHighestBitSetNoZero(_Bits);
	if (static_cast<bool>(_Bits))
		fg_GetHighestBitSetNoZero(_Bits);
}
void fg_Round86Lambda(unsigned _Bits)
{
	auto fClear = [&_Bits] { _Bits = 0; };
	auto pClear = &fClear;
	if (_Bits)
	{
		(*pClear)();
		fg_GetHighestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	}
}
void fg_Round86Repointed(unsigned _Bits, unsigned _Other)
{
	auto pBits = &_Bits;
	if (_Bits)
	{
		pBits = &_Other;
		*pBits = 0;
		fg_GetHighestBitSetNoZero(_Bits);
	}
}
void fg_Round86Loop(unsigned _Bits, unsigned _Other, int _nTimes)
{
	unsigned *pBits = &_Other;
	if (_Bits)
	{
		for (int i = 0; i < _nTimes; ++i)
		{
			*pBits = 0;
			pBits = &_Bits;
		}
		fg_GetHighestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	}
}

// A copy of an aggregate that refers to the argument writes it too, and a
// volatile argument is read again by the call.
struct CRound87Borrowed
{
	unsigned &m_Bits;
};
void fg_Round87Copy(unsigned _Bits)
{
	CRound87Borrowed Alias{_Bits};
	CRound87Borrowed Copy = Alias;
	if (_Bits != 0)
	{
		Copy.m_Bits = 0;
		fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	}
}
void fg_Round87Volatile(unsigned volatile &_Bits)
{
	if (_Bits != 0)
		fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

// A const member function of a member object writes what is mutable in it,
// so does a function given it by a reference to const, a lambda that isn't
// called doesn't, and a mutable member that isn't the one tested doesn't
// change it.
struct CRound88Inner
{
	mutable unsigned m_Bits;
	void f_Clear() const
	{
		m_Bits = 0;
	}
};
struct CRound88Outer
{
	CRound88Inner m_Inner;
	void f_Clear() const
	{
		m_Inner.f_Clear();
	}
};
void fg_Round88Nested(CRound88Outer const &_Info)
{
	if (_Info.m_Inner.m_Bits)
	{
		_Info.f_Clear();
		fg_GetHighestBitSetNoZero(_Info.m_Inner.m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	}
}
struct CRound88Info
{
	mutable unsigned m_Bits;
	unsigned m_Tested;
	mutable unsigned m_nQueries;
	void f_Log() const
	{
		++m_nQueries;
	}
	void f_Observe() const
	{
		auto fUnused = [this] { m_Bits = 0; };
	}
};
void fg_Round88Clear(CRound88Info const &_Info)
{
	_Info.m_Bits = 0;
}
void fg_Round88Free(CRound88Info const &_Info)
{
	if (_Info.m_Bits)
	{
		fg_Round88Clear(_Info);
		fg_GetHighestBitSetNoZero(_Info.m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	}
	if (_Info.m_Bits)
	{
		_Info.f_Observe();
		fg_GetHighestBitSetNoZero(_Info.m_Bits);
	}
	if (_Info.m_Tested)
	{
		_Info.f_Log();
		fg_GetHighestBitSetNoZero(_Info.m_Tested);
	}
}

// A comparison with a constant of an integer class is told by its value, and
// a function given a view writes what its reference member refers to.
struct CRound89Int
{
	unsigned m_Value;
	constexpr CRound89Int(unsigned _Value) : m_Value(_Value) {}
	constexpr bool operator < (CRound89Int const &_Other) const
	{
		return m_Value < _Other.m_Value;
	}
};
int fg_GetHighestBitSetNoZero(CRound89Int const &_Bits);
int fg_Round89Class(CRound89Int const &_Bits)
{
	if (_Bits < CRound89Int(10))
		return fg_GetHighestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	return -1;
}
struct CRound89View
{
	unsigned &m_Bits;
};
void fg_Round89Clear(CRound89View const &_View)
{
	_View.m_Bits = 0;
}
int fg_Round89View(CRound89View const &_View)
{
	if (_View.m_Bits)
	{
		fg_Round89Clear(_View);
		return fg_GetHighestBitSetNoZero(_View.m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	}
	return -1;
}

// What a constant of an integer class is, also when the constructor changes
// what it is given.
struct CRound90Byte
{
	unsigned char m_Bits;
	constexpr CRound90Byte(unsigned _Value)
		: m_Bits(static_cast<unsigned char>(_Value))
	{
	}
	constexpr bool operator==(CRound90Byte const &_Other) const
	{
		return m_Bits == _Other.m_Bits;
	}
};
int fg_GetLowestBitSetNoZero(CRound90Byte const &_Value);
void fg_Round90Byte(CRound90Byte const &_Bits)
{
	if (_Bits == CRound90Byte(256))
		fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

// A view written through its pointer by subscript in a function it is given
// to by value.
struct CRound90View
{
	umint *m_pBits;
};
void fg_Round90Clear(CRound90View _View)
{
	_View.m_pBits[0] = 0;
}
void fg_Round90View(CRound90View _View)
{
	if (*_View.m_pBits)
	{
		fg_Round90Clear(_View);
		fg_GetHighestBitSetNoZero(*_View.m_pBits);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	}
}

// A global that a function it calls writes.
umint g_Round90Bits;
void fg_Round90ClearGlobal()
{
	g_Round90Bits = 0;
}
void fg_Round90Global()
{
	if (g_Round90Bits)
	{
		fg_Round90ClearGlobal();
		fg_GetHighestBitSetNoZero(g_Round90Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	}
}

// Another mutable member written by a function it is given to doesn't
// change the one that was checked.
struct CRound90Info
{
	umint m_Bits;
	mutable umint m_nQueries;
};
void fg_Round90Query(CRound90Info const &_Info)
{
	++_Info.m_nQueries;
}
void fg_Round90Mutable(CRound90Info const &_Info)
{
	if (_Info.m_Bits)
	{
		fg_Round90Query(_Info);
		fg_GetHighestBitSetNoZero(_Info.m_Bits);
	}
}

// A copy of a view that a lambda captures refers to what the view does.
struct CRound91View
{
	umint &m_Bits;
};
void fg_Round91Copied(CRound91View _View)
{
	if (!_View.m_Bits)
		return;
	[_View]
		{
			_View.m_Bits = 0;
		}
	();
	fg_GetHighestBitSetNoZero(_View.m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

// A function given a pointer to const that writes a mutable member, also
// through a function it gives it on to.
struct CRound91Info
{
	mutable umint m_Bits;
};
void fg_Round91ClearPointer(CRound91Info const *_pInfo)
{
	_pInfo->m_Bits = 0;
}
void fg_Round91Pointer(CRound91Info const &_Info)
{
	if (!_Info.m_Bits)
		return;
	fg_Round91ClearPointer(&_Info);
	fg_GetHighestBitSetNoZero(_Info.m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
void fg_Round91Clear(CRound91Info const &_Info)
{
	_Info.m_Bits = 0;
}
void fg_Round91Helper(CRound91Info const &_Info)
{
	fg_Round91Clear(_Info);
}
void fg_Round91Wrapped(CRound91Info const &_Info)
{
	if (!_Info.m_Bits)
		return;
	fg_Round91Helper(_Info);
	fg_GetHighestBitSetNoZero(_Info.m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

// An instantiation whose comparison changes the argument, a write through a
// pointer cast to its type, and a `do` loop whose condition tests what it
// writes.
template <typename t_CType>
int fg_GetLowestBitSetNoZero(t_CType const &_Value);
struct CRound92Int
{
	umint m_Bits;
	bool operator!=(umint _Other)
	{
		umint Bits = m_Bits;
		m_Bits = 0;
		return Bits != _Other;
	}
};
template <typename t_CType>
int fg_Round92Scan(t_CType &_Bits)
{
	if (_Bits != 0)
		return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	return -1;
}
int fg_Round92Run(CRound92Int &_Bits)
{
	return fg_Round92Scan(_Bits);
}
int fg_Round92Cast(umint _Bits)
{
	if (!_Bits)
		return -1;
	auto pBits = static_cast<umint *>(&_Bits);
	*pBits = 0;
	return fg_GetHighestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
void fg_Round92Do(umint _Bits)
{
	if (!_Bits)
		return;
	do
	{
		fg_GetHighestBitSetNoZero(_Bits);
	}
	while ((_Bits >>= 1) != 0);
}

// What a constructor or destructor run after the check writes.
unsigned g_Round93Bits;

struct CRound93ResetOnConstruction
{
	CRound93ResetOnConstruction()
	{
		g_Round93Bits = 0;
	}
};

struct CRound93ResetOnDestruction
{
	~CRound93ResetOnDestruction()
	{
		g_Round93Bits = 0;
	}
};

void fg_Round93Construct()
{
	if (!g_Round93Bits)
		return;
	CRound93ResetOnConstruction Reset;
	fg_GetLowestBitSetNoZero(g_Round93Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

void fg_Round93Destroy()
{
	if (!g_Round93Bits)
		return;
	{
		CRound93ResetOnDestruction Reset;
	}
	fg_GetLowestBitSetNoZero(g_Round93Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

// Checked first by a statement with an attribute.
void fg_Round93Likely(unsigned _Bits)
{
	if (!_Bits) [[likely]]
		return;
	fg_GetLowestBitSetNoZero(_Bits);
}

// Written through a pointer in a local array, or through a reference a helper
// gives back.
void fg_Round94PointerArray(unsigned _Bits)
{
	unsigned *Pointers[] = {&_Bits};
	if (!_Bits)
		return;
	*Pointers[0] = 0;
	fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

unsigned &fg_Round94Reference(unsigned &_Bits)
{
	return _Bits;
}

void fg_Round94ReturnedReference(unsigned _Bits)
{
	unsigned &Bits = fg_Round94Reference(_Bits);
	if (!_Bits)
		return;
	Bits = 0;
	fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

// A throw of what has to be destroyed leaves too, and the elements of a
// braced list of a local run in order.
struct CRound95Error
{
	~CRound95Error();
};

int fg_Round95Throw(unsigned _Bits)
{
	if (!_Bits)
		throw CRound95Error{};
	return fg_GetHighestBitSetNoZero(_Bits);
}

void fg_Round95InOrder(unsigned _Bits)
{
	if (_Bits)
	{
		int Values[] = {fg_GetHighestBitSetNoZero(_Bits), int(_Bits = 0)};
	}
}

// Written through a const reference by a member function or a constructor,
// or through a const pointer with its const cast away; and a narrowing keeps
// the low bit that is set.
struct CRound96Info
{
	mutable unsigned m_Bits;
};

struct CRound96Worker
{
	void f_Clear(CRound96Info const &_Info)
	{
		_Info.m_Bits = 0;
	}
};

struct CRound96Clearer
{
	CRound96Clearer(CRound96Info const &_Info)
	{
		_Info.m_Bits = 0;
	}
};

int fg_Round96Member(CRound96Info const &_Info, CRound96Worker &_Worker)
{
	if (!_Info.m_Bits)
		return 0;
	_Worker.f_Clear(_Info);
	return fg_GetHighestBitSetNoZero(_Info.m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

int fg_Round96Constructor(CRound96Info const &_Info)
{
	if (!_Info.m_Bits)
		return 0;
	CRound96Clearer Clearer(_Info);
	return fg_GetHighestBitSetNoZero(_Info.m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

int fg_Round96ConstCast(unsigned _Bits)
{
	if (!_Bits)
		return 0;
	unsigned const *pBits = &_Bits;
	*const_cast<unsigned *>(pBits) = 0;
	return fg_GetHighestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

int fg_Round96Narrowed(unsigned _Bits)
{
	return fg_GetLowestBitSetNoZero<unsigned char>(_Bits | 1u);
}

// A wide constant narrowed keeps only its low bits, a conversion to bool that
// clears the value in an instantiation, and a named constant of an integer
// class.
using uint128 = unsigned __int128;

int fg_Round97Wide(uint128 _Bits)
{
	return fg_GetLowestBitSetNoZero<unsigned char>((unsigned char)(_Bits | (uint128(1) << 100)));
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

struct CRound97Clearing
{
	operator bool()
	{
		bool bResult = m_Bits != 0;
		m_Bits = 0;
		return bResult;
	}

	unsigned m_Bits;
};

template <typename tf_CInt>
int fg_Round97Template(tf_CInt &_Bits)
{
	if (_Bits)
		return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	return 0;
}

int fg_Round97Clearing(CRound97Clearing &_Bits)
{
	return fg_Round97Template(_Bits);
}

struct CRound97Int
{
	constexpr CRound97Int(unsigned _Bits)
		: m_Bits(_Bits)
	{
	}
	bool operator ==(CRound97Int const &_Other) const = default;

	unsigned m_Bits;
};

constexpr CRound97Int gc_Round97One(1);

int fg_Round97Named(CRound97Int const &_Bits)
{
	if (_Bits != gc_Round97One)
		return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	return 0;
}

// A pointer member pointed elsewhere after the reference is bound.
struct CRound98Info
{
	unsigned m_Bits;
};
struct CRound98View
{
	CRound98Info *m_pInfo;
};

int fg_Round98Repointed(CRound98View &_View, CRound98Info *_pOther)
{
	unsigned const &Bits = _View.m_pInfo->m_Bits;
	_View.m_pInfo = _pOther;
	if (_View.m_pInfo->m_Bits != 0)
		return fg_GetHighestBitSetNoZero(Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	return -1;
}

// A comparison of a member in a template that clears it.
struct CRound98Int
{
	mutable unsigned m_Bits;
	bool operator != (int _Other) const
	{
		bool bResult = m_Bits != unsigned(_Other);
		m_Bits = 0;
		return bResult;
	}
};
struct CRound98Holder
{
	CRound98Int m_Value;
};

template <typename t_CType>
int fg_Round98Scan(t_CType const &_Info)
{
	if (_Info.m_Value != 0)
		return fg_GetLowestBitSetNoZero(_Info.m_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	return -1;
}

int fg_Round98UseScan(CRound98Holder const &_Info)
{
	return fg_Round98Scan(_Info);
}

// The constant a constructor makes is compared, `CRound99Unsigned(-1)` of an
// `unsigned`, which zero is less than.
struct CRound99Unsigned
{
	unsigned m_Value;
	constexpr CRound99Unsigned(int _Value)
		: m_Value(_Value)
	{
	}
	friend constexpr bool operator < (CRound99Unsigned const &_Left, CRound99Unsigned const &_Right)
	{
		return _Left.m_Value < _Right.m_Value;
	}
};

int fg_Round99Converted(CRound99Unsigned const &_Bits)
{
	if (_Bits < CRound99Unsigned(-1))
		return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	return -1;
}

// A temporary of the condition whose destructor clears a global.
unsigned g_Round99Bits = 1;
struct CRound99Reset
{
	~CRound99Reset()
	{
		g_Round99Bits = 0;
	}
};

int fg_Round99Destroyed()
{
	if (((void)CRound99Reset{}, g_Round99Bits))
		return fg_GetHighestBitSetNoZero(g_Round99Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	return -1;
}

int fg_Round99DestroyedInInit()
{
	if (CRound99Reset Reset; !g_Round99Bits)
		return -1;
	return fg_GetHighestBitSetNoZero(g_Round99Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

// Written through a local reference to a member of a view given by value.
struct CRound100View
{
	unsigned &m_Bits;
};

void fg_Round100Clear(CRound100View _View)
{
	unsigned &Bits = _View.m_Bits;
	Bits = 0;
}

int fg_Round100Alias(unsigned _Bits)
{
	CRound100View View{_Bits};
	if (!_Bits)
		return 0;
	fg_Round100Clear(View);
	return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

// A copy of a guard destroyed at the end of a block writes what it refers to.
struct CRound100Reset
{
	unsigned &m_Bits;
	~CRound100Reset()
	{
		m_Bits = 0;
	}
};

int fg_Round100Copy(unsigned _Bits)
{
	CRound100Reset Reset{_Bits};
	if (!_Bits)
		return 0;
	{
		CRound100Reset Copy = Reset;
	}
	return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

// A lambda that a const member function calls can write its mutable members.
struct CRound101Info
{
	mutable unsigned m_Bits;
	void f_Clear() const
	{
		[this]
		{
			m_Bits = 0;
		}();
	}
};

int fg_Round101Lambda(CRound101Info const &_Info)
{
	if (!_Info.m_Bits)
		return -1;
	_Info.f_Clear();
	return fg_GetHighestBitSetNoZero(_Info.m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

// A constant of a class of two words.
struct CRound101Int
{
	unsigned long long m_Upper;
	unsigned long long m_Lower;
	constexpr CRound101Int(unsigned long long _Value)
		: m_Upper(0)
		, m_Lower(_Value)
	{
	}
	constexpr bool operator < (CRound101Int const &_Other) const
	{
		return m_Upper < _Other.m_Upper || (m_Upper == _Other.m_Upper && m_Lower < _Other.m_Lower);
	}
};

int fg_Round101Words(CRound101Int const &_Bits)
{
	if (_Bits < CRound101Int(10))
		return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	return -1;
}

// The destructors of the elements of a local array.
unsigned g_Round102Bits;
struct CRound102Reset
{
	~CRound102Reset()
	{
		g_Round102Bits = 0;
	}
};

void fg_Round102Array()
{
	if (!g_Round102Bits)
		return;
	{
		CRound102Reset Resets[1];
	}
	fg_GetHighestBitSetNoZero(g_Round102Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

// A fact for the analysis checks only what it tests.
#define DRound102Scan(d_Value) fg_GetHighestBitSetNoZero(d_Value)

void fg_Round102Partial(unsigned _Bits)
{
	DMibAnalyzerAssume(_Bits != 0);
	DRound102Scan(_Bits & 1u);
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

// A temporary destroyed after the call, at the end of the full-expression.
void fg_Round102Temporary()
{
	(void)(g_Round102Bits && ((void)CRound102Reset{}, fg_GetHighestBitSetNoZero(g_Round102Bits)));
}

// An aggregate that refers to it, only read.
struct CRound102Borrowed
{
	unsigned &m_Bits;
};

void fg_Round102Borrowed(unsigned _Bits)
{
	if (!_Bits)
		return;
	CRound102Borrowed Borrowed{_Bits};
	(void)Borrowed;
	fg_GetHighestBitSetNoZero(_Bits);
}

// Through a pointer a helper gives back, a constructor that binds a reference
// member, and a reference member to const written through a cast.
unsigned *fg_Round103Pointer(unsigned &_Bits)
{
	return &_Bits;
}

int fg_Round103Helper(unsigned _Bits)
{
	unsigned *pBits = fg_Round103Pointer(_Bits);
	if (_Bits)
	{
		*pBits = 0;
		return fg_GetHighestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	}
	return -1;
}

struct CRound103Borrowed
{
	unsigned &m_Bits;
	CRound103Borrowed(unsigned &_Bits)
		: m_Bits(_Bits)
	{
	}
};

int fg_Round103Constructor(unsigned _Bits)
{
	CRound103Borrowed Borrowed(_Bits);
	if (_Bits)
	{
		Borrowed.m_Bits = 0;
		return fg_GetHighestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	}
	return -1;
}

struct CRound103ConstBorrowed
{
	unsigned const &m_Bits;
};

int fg_Round103Const(unsigned _Bits)
{
	CRound103ConstBorrowed Borrowed{_Bits};
	if (_Bits)
	{
		const_cast<unsigned &>(Borrowed.m_Bits) = 0;
		return fg_GetHighestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	}
	return -1;
}

// Through `*this`, and a lambda that copies the object.
struct CRound104Info
{
	unsigned m_Bits;
	int f_Alias()
	{
		if (!m_Bits)
			return -1;
		auto &Self = *this;
		Self.m_Bits = 0;
		return fg_GetHighestBitSetNoZero(m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	}
	int f_Copy()
	{
		if (!m_Bits)
			return -1;
		[*this]() mutable
		{
			m_Bits = 0;
		}();
		return fg_GetHighestBitSetNoZero(m_Bits);
	}
};

// A free comparison operator that clears what it compares.
struct CRound105Int
{
	mutable unsigned m_Bits;
};

int fg_GetHighestBitSetNoZero(CRound105Int const &_Bits);

bool operator!=(CRound105Int const &_Bits, unsigned _Other)
{
	bool bResult = _Bits.m_Bits != _Other;
	_Bits.m_Bits = 0;
	return bResult;
}

int fg_Round105Scan(CRound105Int const &_Bits)
{
	if (_Bits != 0)
		return fg_GetHighestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	return 0;
}

// The destruction of the members and bases of what is destroyed.
unsigned g_Round106Bits = 1;

struct CRound106Reset
{
	~CRound106Reset()
	{
		g_Round106Bits = 0;
	}
};

struct CRound106Wrapper
{
	CRound106Reset m_Reset;
};

struct CRound106Derived : CRound106Reset
{
};

void fg_Round106Members()
{
	if (!g_Round106Bits)
		return;
	{
		CRound106Wrapper Reset;
	}
	fg_GetHighestBitSetNoZero(g_Round106Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

void fg_Round106Bases()
{
	if (!g_Round106Bits)
		return;
	{
		CRound106Derived Reset;
	}
	fg_GetHighestBitSetNoZero(g_Round106Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

// A global written through another of its declarations.
extern unsigned g_Round106Declared;

void fg_Round106ClearDeclared()
{
	g_Round106Declared = 0;
}

unsigned g_Round106Declared = 1;

void fg_Round106Redeclared()
{
	if (!g_Round106Declared)
		return;
	fg_Round106ClearDeclared();
	fg_GetHighestBitSetNoZero(g_Round106Declared);
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

// A static local that a class in its function writes.
void fg_Round106StaticLocal()
{
	static unsigned s_Bits = 1;
	struct CWorker
	{
		static void fs_Clear()
		{
			s_Bits = 0;
		}
	};
	if (!s_Bits)
		return;
	CWorker::fs_Clear();
	fg_GetHighestBitSetNoZero(s_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

// An array destroyed when the `if` that declares it ends.
struct CRound107Clear
{
	unsigned &m_Bits;
	~CRound107Clear()
	{
		m_Bits = 0;
	}
};

int fg_Round107Array(unsigned _Bits)
{
	if (CRound107Clear Clear[1] = {{_Bits}}; !_Bits)
		return -1;
	return fg_GetHighestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

// An aggregate that refers to it, returned by a function.
struct CRound107Borrow
{
	unsigned &m_Bits;
};

CRound107Borrow fg_Round107Borrow(unsigned &_Bits)
{
	return {_Bits};
}

int fg_Round107Returned(unsigned _Bits)
{
	CRound107Borrow Borrow = fg_Round107Borrow(_Bits);
	if (!_Bits)
		return -1;
	Borrow.m_Bits = 0;
	return fg_GetHighestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

// A temporary of the left side of `&&` is destroyed after the right side.
bool fg_Round107AndTemporary(unsigned _Bits)
{
	return (CRound107Clear{_Bits}, _Bits != 0) && fg_GetHighestBitSetNoZero(_Bits) >= 0;
}

// An atomic store, and a placement new, write what they are given.
void fg_Round108Atomic(unsigned _Bits)
{
	if (!_Bits)
		return;
	__atomic_store_n(&_Bits, 0u, __ATOMIC_RELAXED);
	fg_GetHighestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

void *operator new(decltype(sizeof(0)), void *_pStorage) noexcept;

void fg_Round108Placement(unsigned _Bits)
{
	if (!_Bits)
		return;
	new (&_Bits) unsigned(0);
	fg_GetHighestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

// The destructor of a member of a copy destroyed, which writes it.
struct CRound109Reset
{
	unsigned &m_Bits;
	~CRound109Reset()
	{
		m_Bits = 0;
	}
};

struct CRound109Wrapper
{
	CRound109Reset m_Reset;
};

void fg_Round109MemberDestructor(unsigned _Bits)
{
	CRound109Wrapper Reset{{_Bits}};
	if (_Bits)
	{
		{
			CRound109Wrapper Copy = Reset;
		}
		fg_GetHighestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	}
}

// A helper given it as const that casts the const away.
void fg_Round109Clear(unsigned const &_Bits)
{
	const_cast<unsigned &>(_Bits) = 0;
}

void fg_Round109ClearPointer(unsigned const *_pBits)
{
	*const_cast<unsigned *>(_pBits) = 0;
}

void fg_Round109ConstReference(unsigned _Bits)
{
	if (_Bits)
	{
		fg_Round109Clear(_Bits);
		fg_GetHighestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	}
}

void fg_Round109ConstPointer(unsigned _Bits)
{
	if (_Bits)
	{
		fg_Round109ClearPointer(&_Bits);
		fg_GetHighestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	}
}

// A const member function that casts the const away.
struct CRound109Bits
{
	unsigned m_Bits;
	void f_Clear() const
	{
		const_cast<CRound109Bits *>(this)->m_Bits = 0;
	}
};

void fg_Round109ConstMember(CRound109Bits &o_Info)
{
	if (o_Info.m_Bits)
	{
		o_Info.f_Clear();
		fg_GetHighestBitSetNoZero(o_Info.m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	}
}

// A lambda that a helper calls, and one that a scope guard calls when it is
// destroyed.
template <typename tf_FCallback>
void fg_Round110Invoke(tf_FCallback const &_fCallback)
{
	_fCallback();
}

void fg_Round110Invoked(unsigned _Bits)
{
	if (!_Bits)
		return;
	fg_Round110Invoke([&] { _Bits = 0; });
	fg_GetHighestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

template <typename t_FOnExit>
struct TCRound110OnScopeExit
{
	t_FOnExit m_fOnExit;
	~TCRound110OnScopeExit()
	{
		m_fOnExit();
	}
};

void fg_Round110ScopeExit(unsigned _Bits)
{
	if (!_Bits)
		return;
	{
		TCRound110OnScopeExit Guard{[&] { _Bits = 0; }};
	}
	fg_GetHighestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

// A signed multiword integer compared below a constant can be zero, and a
// scan in a macro is reported where the macro is used.
struct CRound111Int
{
	long long m_Upper;
	unsigned long long m_Lower;
	constexpr CRound111Int(int _Value)
		: m_Upper(_Value < 0 ? -1 : 0)
		, m_Lower(_Value)
	{
	}
};
constexpr bool operator < (CRound111Int const &_Left, CRound111Int const &_Right)
{
	return _Left.m_Upper < _Right.m_Upper || (_Left.m_Upper == _Right.m_Upper && _Left.m_Lower < _Right.m_Lower);
}
template <typename t_CType>
int fg_GetHighestBitSetNoZero(t_CType const &_Value);
int fg_Round111Signed(CRound111Int const &_Bits)
{
	if (_Bits < CRound111Int(10))
		return fg_GetHighestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	return -1;
}
int fg_Round111Negative(CRound111Int const &_Bits)
{
	if (CRound111Int(-10) < _Bits)
		return fg_GetHighestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	if (CRound111Int(10) < _Bits)
		return fg_GetHighestBitSetNoZero(_Bits);
	return -1;
}
int fg_Round111Positive(CRound111Int const &_Bits)
{
	if (CRound111Int(10) < _Bits)
		return fg_GetHighestBitSetNoZero(_Bits);
	return -1;
}
#define DRound111Scan(d_Bits) fg_GetHighestBitSetNoZero(d_Bits)
int fg_Round111Suppressed(unsigned _Bits)
{
	return DRound111Scan(_Bits); // NOLINT(malterlib-no-zero-bit-scan)
}
int fg_Round111Unchecked(unsigned _Bits)
{
	return DRound111Scan(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

// A static member is the same variable through any object.
struct CRound112State
{
	static unsigned ms_Bits;
};
int fg_Round112StaticMember(CRound112State &_State)
{
	if (_State.ms_Bits)
	{
		CRound112State::ms_Bits = 0;
		return fg_GetHighestBitSetNoZero(_State.ms_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	}
	if (CRound112State::ms_Bits)
		return fg_GetHighestBitSetNoZero(_State.ms_Bits);
	return -1;
}

// What a local pointer points to is what is read through it.
int fg_Round112ThroughPointer(unsigned _Bits)
{
	unsigned *pBits = &_Bits;
	if (*pBits)
	{
		_Bits = 0;
		return fg_GetHighestBitSetNoZero(*pBits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	}
	return -1;
}
int fg_Round112ThroughPointerChecked(unsigned _Bits)
{
	unsigned *pBits = &_Bits;
	if (*pBits)
		return fg_GetHighestBitSetNoZero(*pBits);
	return -1;
}

// A write through a pointer read through another, `**ppBits = 0`.
int fg_Round112ThroughPointerToPointer(unsigned _Bits)
{
	if (_Bits)
	{
		unsigned *pBits = &_Bits;
		unsigned **ppBits = &pBits;
		**ppBits = 0;
		return fg_GetHighestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	}
	return -1;
}

// A negation, and a double not, are zero only when what they are of is.
int fg_Round112Negated(unsigned _Bits)
{
	if (-_Bits != 0)
		return fg_GetHighestBitSetNoZero(_Bits);
	if (!!_Bits != 0)
		return fg_GetHighestBitSetNoZero(_Bits);
	if (!_Bits != 0)
		return fg_GetHighestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	return -1;
}

// A local reference compared with all ones is the value it is bound to.
int fg_Round112ComplementAlias(unsigned _Bits)
{
	unsigned const &Bits = _Bits;
	if (Bits != ~0u)
		return fg_GetHighestBitSetNoZero(~_Bits);
	return -1;
}

// Round 113: a class of integers of such classes, compared with one, can be zero
namespace NRound113Nested
{
template<typename t_CUpper, typename t_CLower>
struct TCInt {
  t_CLower m_Lower{};
  t_CUpper m_Upper{};
  constexpr TCInt(unsigned _Value = 0) : m_Lower(_Value) {}
  constexpr bool operator==(TCInt const &_Other) const { return m_Lower == _Other.m_Lower && m_Upper == _Other.m_Upper; }
};
using uint256 = TCInt<unsigned __int128, unsigned __int128>;
using uint512 = TCInt<uint256, uint256>;
int fg_GetHighestBitSetNoZero(uint512 const &_Bits);
void fg_Test(uint512 const &_Bits) {
  if (_Bits != uint512(1))
    fg_GetHighestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}

// Round 113: an array of pointers to const can be written through with a const_cast
namespace NRound113ConstArray
{
int fg_GetHighestBitSetNoZero(unsigned _Bits);
void fg_Scan(unsigned _Bits) {
  if (!_Bits) return;
  unsigned const *pPointers[] = {&_Bits};
  *const_cast<unsigned *>(pPointers[0]) = 0;
  fg_GetHighestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}

// Round 114: a comparison that clears its operand through a helper doesn't check it
namespace NRound114Helper
{
struct CBits { mutable unsigned m_Bits; };
int fg_GetLowestBitSetNoZero(CBits const &);
bool fg_TestAndClear(CBits const &_Bits) { unsigned Bits = _Bits.m_Bits; _Bits.m_Bits = 0; return Bits != 0; }
bool operator!=(CBits const &_Bits, int) { return fg_TestAndClear(_Bits); }
int fg_Run(CBits const &_Bits) { if (_Bits != 0) return fg_GetLowestBitSetNoZero(_Bits); return -1; }
// CHECK-MESSAGES: :[[@LINE-1]]:57: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

// Round 114: a capture initializer runs where the lambda is made
namespace NRound114Capture
{
template<class T> int fg_GetLowestBitSetNoZero(T const &);
void fg_Run(unsigned _Bits) {
  if (_Bits) {
    auto f = [Bit = fg_GetLowestBitSetNoZero(_Bits)] { return Bit; };
  }
}
}

// Round 114: a discarded write doesn't make a comparison change its operand
namespace NRound114Discarded
{
struct CBits { mutable unsigned m_Bits; };
int fg_GetLowestBitSetNoZero(CBits const &);
bool operator!=(CBits const &_Bits, int) { if constexpr (false) _Bits.m_Bits = 0; return _Bits.m_Bits != 0; }
int fg_Run(CBits const &_Bits) { if (_Bits != 0) return fg_GetLowestBitSetNoZero(_Bits); return -1; }
}

// A conversion that changes the object can give zero after it was tested.
namespace NRound115ChangingConversion {
int fg_GetLowestBitSetNoZero(unsigned _Bits);
struct CBits {
  mutable unsigned m_Bits;
  operator unsigned() const { unsigned Bits = m_Bits; m_Bits = 0; return Bits; }
};
int fg_Scan(CBits const &_Bits) {
  if (static_cast<unsigned>(_Bits))
    return fg_GetLowestBitSetNoZero(static_cast<unsigned>(_Bits));
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
  return -1;
}
}

// A constructed temporary compared with is evaluated as a whole.
namespace NRound115ConstructedTemporary {
struct CInt {
  unsigned long long m_Upper, m_Lower;
  constexpr CInt(unsigned long long _Upper, unsigned long long _Lower) : m_Upper(_Upper), m_Lower(_Lower) {}
  bool operator<(CInt const &_Other) const { return m_Upper < _Other.m_Upper || (m_Upper == _Other.m_Upper && m_Lower < _Other.m_Lower); }
};
template<class T> int fg_GetLowestBitSetNoZero(T const &);
int fg_Scan(CInt const &_Bits) {
  if (_Bits < CInt(0, 1))
    return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
  return -1;
}
}

// What a local pointer points to is the value tested.
namespace NRound115Pointee {
int fg_GetLowestBitSetNoZero(unsigned _Bits);
int fg_Scan(unsigned _Bits) {
  unsigned *pBits = &_Bits;
  if (*pBits != 0)
    return fg_GetLowestBitSetNoZero(_Bits);
  return -1;
}
int fg_Scan2(unsigned _Bits) {
  unsigned *pBits = &_Bits;
  if (_Bits != 0)
    return fg_GetLowestBitSetNoZero(*pBits);
  return -1;
}
}

// A const member function of a copy can write what the copy shares through a reference member.
namespace NRound116ConstMemberOfCopy {
template<class T>
int fg_GetHighestBitSetNoZero(T const &_Bits);
struct CView {
  unsigned &m_Bits;
  void f_Clear() const { m_Bits = 0; }
};
void fg_Clear(CView _View) { _View.f_Clear(); }
void fg_Test(unsigned _Bits) {
  CView View{_Bits};
  if (_Bits) {
    fg_Clear(View);
    fg_GetHighestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
  }
}
}

// The same member of separate base objects is told apart.
namespace NRound116BaseSubobjects {
template<class T>
int fg_GetHighestBitSetNoZero(T const &_Bits);
struct CBits { unsigned m_Bits; };
struct CFirst : CBits {};
struct CSecond : CBits {};
struct CInfo : CFirst, CSecond {};
int fg_Read(CInfo const &_Info) {
  if (_Info.CFirst::m_Bits)
    return fg_GetHighestBitSetNoZero(_Info.CSecond::m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
  return -1;
}
int fg_Read2(CInfo &_Info) {
  if (_Info.CFirst::m_Bits) {
    _Info.CSecond::m_Bits = 0;
    return fg_GetHighestBitSetNoZero(_Info.CFirst::m_Bits);
  }
  return -1;
}
}

// A member function given a view by value can write through it.
namespace NRound117MemberCopy {
unsigned fg_GetLowestBitSetNoZero(unsigned _Bits);
struct CView { unsigned &m_Bits; };
struct CWorker
{
    void f_Clear(CView _View) { _View.m_Bits = 0; }
};
void fg_Run(unsigned _Bits, CWorker &_Worker)
{
    CView View{_Bits};
    if (!_Bits) return;
    _Worker.f_Clear(View);
    fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}

// The destructor of a copy given by value can write through it.
namespace NRound117CopyDestructor {
unsigned fg_GetLowestBitSetNoZero(unsigned _Bits);
struct CClear
{
    unsigned &m_Bits;
    ~CClear() { m_Bits = 0; }
};
struct CFuture { struct promise_type {}; };
CFuture fg_Consume(CClear _Clear) { return {}; }
void fg_Run(unsigned _Bits)
{
    CClear Clear{_Bits};
    if (!_Bits) return;
    fg_Consume(Clear);
    fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}

// A negative subscript from where a pointer is moved to is the element before.
namespace NRound117NegativeSubscript {
unsigned fg_GetLowestBitSetNoZero(unsigned _Bits);
void fg_Run(unsigned *_pBits)
{
    if (!_pBits[0]) return;
    unsigned *pEnd = _pBits + 1;
    pEnd[-1] = 0;
    fg_GetLowestBitSetNoZero(_pBits[0]);
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}

// A callback called through a local copy writes what it writes.
namespace NRound117CallbackCopy {
unsigned fg_GetLowestBitSetNoZero(unsigned _Bits);
template <typename tf_F>
void fg_Invoke(tf_F _fWork)
{
    auto fCopy = _fWork;
    fCopy();
}
void fg_Run(unsigned _Bits)
{
    if (!_Bits) return;
    fg_Invoke([&] { _Bits = 0; });
    fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}

// A constructor that points a member at a reference parameter aliases it.
namespace NRound118AddressMember {
int fg_GetLowestBitSetNoZero(unsigned const &_Bits);

struct CBorrowed {
  unsigned *m_pBits;
  CBorrowed(unsigned &_Bits) : m_pBits(&_Bits) {}
  void f_Clear() { *m_pBits = 0; }
};

int fg_Scan(unsigned _Bits) {
  CBorrowed Borrowed(_Bits);
  if (_Bits) {
    Borrowed.f_Clear();
    return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
  }
  return -1;
}
}

// An overloaded comparison that writes what is tested invalidates the test.
namespace NRound118ComparisonWrites {
int fg_GetLowestBitSetNoZero(unsigned const &_Bits);
unsigned g_Bits = 1;
enum class EZero : unsigned { mc_Zero = 0 };

bool operator!=(unsigned _Bits, EZero _Zero) {
  g_Bits = 0;
  return _Bits != 0;
}

int fg_Scan() {
  if (g_Bits != EZero::mc_Zero)
    return fg_GetLowestBitSetNoZero(g_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
  return -1;
}
}

// An output of assembly writes it.
namespace NRound118AsmOutput {
int fg_GetLowestBitSetNoZero(unsigned const &_Bits);

int fg_Scan(unsigned _Bits) {
  if (_Bits) {
    asm("" : "=r"(_Bits) : "0"(0u));
    return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
  }
  return -1;
}
}

// Round 119: a copy given on to a helper that writes through it invalidates
// the test, but not a write in a branch of an `if constexpr` that is discarded.
namespace NRound119ForwardedCopy {
template <typename t_CType>
int fg_GetHighestBitSetNoZero(t_CType const &_Value);

struct CView { unsigned &m_Bits; };

void fg_Clear(CView _View) { _View.m_Bits = 0; }
void fg_Forward(CView _View) { fg_Clear(_View); }
void fg_Observe(CView _View) {
  if constexpr (false)
    _View.m_Bits = 0;
}

int fg_Forwarded(CView _View) {
  if (!_View.m_Bits)
    return -1;
  fg_Forward(_View);
  return fg_GetHighestBitSetNoZero(_View.m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

int fg_Discarded(CView _View) {
  if (!_View.m_Bits)
    return -1;
  fg_Observe(_View);
  return fg_GetHighestBitSetNoZero(_View.m_Bits);
}
}

// Round 120: an overloaded `|` that gives a narrower type can lose the bit,
// and a destructor that can be seen not to write doesn't change what is
// checked.
namespace NRound120Narrow {
struct CByte {
  unsigned char m_Value;
  constexpr unsigned char operator|(unsigned _Mask) const { return m_Value | _Mask; }
};
template <typename t_CType>
int fg_GetHighestBitSetNoZero(t_CType const &_Value);

int fg_Scan(CByte const &_Value) {
  return fg_GetHighestBitSetNoZero(_Value | 256u);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}

namespace NRound120Observe {
int fg_GetHighestBitSetNoZero(unsigned _Bits);
struct CObserve {
  unsigned m_Bits;
  ~CObserve() {}
};

int fg_Scan(unsigned _Bits) {
  if (CObserve Observe{_Bits}; !_Bits)
    return -1;
  return fg_GetHighestBitSetNoZero(_Bits);
}
}

// Round 121: a pointer member assigned its address writes it, a comparison
// with what isn't constant that zero passes doesn't check it, and a const
// local made nonzero is.
namespace NRound121
{
template <typename t_CType>
int fg_GetHighestBitSetNoZero(t_CType const &_Number);

struct CBorrowed { unsigned *m_pBits = nullptr; };

int fg_Member(unsigned _Bits) {
  if (!_Bits)
    return -1;
  CBorrowed Borrowed;
  Borrowed.m_pBits = &_Bits;
  *Borrowed.m_pBits = 0;
  return fg_GetHighestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

int fg_Compare(unsigned _Bits, unsigned _Other) {
  if (_Bits <= _Other)
    return fg_GetHighestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
  if (_Bits > _Other)
    return fg_GetHighestBitSetNoZero(_Bits);
  return -1;
}

int fg_Const(unsigned _Bits) {
  const unsigned Bits = _Bits | 1u;
  return fg_GetHighestBitSetNoZero(Bits);
}
}

// Round 122: a loop condition that assigns another operand doesn't check the
// argument, one that clears a bit of it and tests it does, and an element of
// a local array assigned its address writes it.
namespace NRound122
{
int fg_GetHighestBitSetNoZero(unsigned _Bits);

int fg_Mask(unsigned _Bits, unsigned _Mask) {
  if ((_Bits & _Mask) == 0)
    return -1;
  do {
    fg_GetHighestBitSetNoZero(_Bits & _Mask);
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
    _Bits = 0;
  } while ((_Mask = 1) != 0);
  return 0;
}

int fg_Clear(unsigned _Bits) {
  if (!_Bits)
    return -1;
  do {
    fg_GetHighestBitSetNoZero(_Bits);
  } while ((_Bits &= _Bits - 1) != 0);
  return 0;
}

int fg_Array(unsigned _Bits) {
  if (!_Bits)
    return -1;
  unsigned *Pointers[1];
  Pointers[0] = &_Bits;
  *Pointers[0] = 0;
  return fg_GetHighestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}

// Round 123: a const local made from itself tells nothing, nor does a chain of
// them made from others twice, and writing a member of a copy doesn't change
// the original's.
namespace NRound123
{
int fg_GetHighestBitSetNoZero(unsigned _Bits);

int fg_SelfMade() {
  const unsigned Bits = Bits;
  return fg_GetHighestBitSetNoZero(Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

#define DRound123Or(d_N, d_P) const unsigned Bits##d_N = Bits##d_P | Bits##d_P;
int fg_Chain(unsigned _Bits) {
  const unsigned Bits0 = _Bits;
  DRound123Or(1, 0) DRound123Or(2, 1) DRound123Or(3, 2) DRound123Or(4, 3)
  DRound123Or(5, 4) DRound123Or(6, 5) DRound123Or(7, 6) DRound123Or(8, 7)
  DRound123Or(9, 8) DRound123Or(10, 9) DRound123Or(11, 10) DRound123Or(12, 11)
  DRound123Or(13, 12) DRound123Or(14, 13) DRound123Or(15, 14) DRound123Or(16, 15)
  DRound123Or(17, 16) DRound123Or(18, 17) DRound123Or(19, 18) DRound123Or(20, 19)
  DRound123Or(21, 20) DRound123Or(22, 21) DRound123Or(23, 22) DRound123Or(24, 23)
  DRound123Or(25, 24) DRound123Or(26, 25) DRound123Or(27, 26) DRound123Or(28, 27)
  DRound123Or(29, 28) DRound123Or(30, 29) DRound123Or(31, 30) DRound123Or(32, 31)
  return fg_GetHighestBitSetNoZero(Bits32);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

struct CInfo {
  unsigned m_Bits;
};

int fg_CopyChanged(CInfo const &_Info) {
  if (!_Info.m_Bits)
    return -1;
  CInfo Copy = _Info;
  Copy.m_Bits = 0;
  return fg_GetHighestBitSetNoZero(_Info.m_Bits);
}

struct CBorrowed {
  unsigned &m_Bits;
};

int fg_SharedChanged(unsigned _Bits) {
  if (!_Bits)
    return -1;
  CBorrowed Borrowed{_Bits};
  CBorrowed Copy = Borrowed;
  Copy.m_Bits = 0;
  return fg_GetHighestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}

// Round 124: a destructor that gives what it refers to a helper that clears it
// changes it, and an empty one changes nothing.
namespace NRound124
{
int fg_GetHighestBitSetNoZero(unsigned _Bits);
void fg_Clear(unsigned &_Bits) { _Bits = 0; }
void fg_Read(unsigned const &_Bits);

struct CReset {
  unsigned &m_Bits;
  ~CReset() { fg_Clear(m_Bits); }
};

int fg_Reset(unsigned _Bits) {
  if (CReset Reset{_Bits}; !_Bits)
    return -1;
  return fg_GetHighestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

struct CObserve {
  unsigned const &m_Bits;
  ~CObserve() {}
};

int fg_Observe(unsigned _Bits) {
  if ((CObserve{_Bits}, _Bits))
    return fg_GetHighestBitSetNoZero(_Bits);
  return -1;
}

struct CReadOnly {
  unsigned &m_Bits;
  ~CReadOnly() { fg_Read(m_Bits); }
};

int fg_ReadOnly(unsigned _Bits) {
  if (CReadOnly Read{_Bits}; !_Bits)
    return -1;
  return fg_GetHighestBitSetNoZero(_Bits);
}
}

// Round 125: a value tested as an `int` isn't tested narrowed, a call through
// a local pointer to a function that clears a global clears it, and a
// bit-field narrowed to a type at least as wide as it keeps its bits.
namespace NRound125
{
template <typename t_CType>
int fg_GetLowestBitSetNoZero(t_CType const &_Bits);

int fg_Narrowed(unsigned short _Bits) {
  if (_Bits != 0)
    return fg_GetLowestBitSetNoZero<unsigned char>(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
  return 0;
}

int fg_Same(unsigned short _Bits) {
  if (_Bits != 0)
    return fg_GetLowestBitSetNoZero(_Bits);
  return 0;
}

unsigned g_Bits;
void fg_Clear() { g_Bits = 0; }

int fg_ThroughPointer() {
  if (!g_Bits)
    return 0;
  auto fClear = &fg_Clear;
  fClear();
  return fg_GetLowestBitSetNoZero(g_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

struct CBits {
  unsigned m_Bits : 3;
  unsigned m_Wide : 12;
};

int fg_BitField(CBits const &_Info) {
  if (_Info.m_Bits)
    return fg_GetLowestBitSetNoZero<unsigned char>(_Info.m_Bits);
  return 0;
}

int fg_WideBitField(CBits const &_Info) {
  if (_Info.m_Wide)
    return fg_GetLowestBitSetNoZero<unsigned char>(_Info.m_Wide);
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
  return 0;
}
}

// Round 126: a static local pointer to the value writes it, a const
// conversion that clears the value through a global doesn't check it, and
// making a scope guard of it doesn't change it until it is destroyed.
namespace NRound126
{
template <typename t_CType>
int fg_GetLowestBitSetNoZero(t_CType const &_Bits);

unsigned g_Bits = 1;

int fg_StaticAlias() {
  static unsigned *const pBits = &g_Bits;
  if (g_Bits) {
    *pBits = 0;
    return fg_GetLowestBitSetNoZero(g_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
  }
  return 0;
}

struct CInt;
extern CInt g_Value;
struct CInt {
  unsigned m_Bits;
  explicit operator bool() const {
    bool bWasSet = m_Bits;
    g_Value.m_Bits = 0;
    return bWasSet;
  }
};
CInt g_Value{1};

int fg_Conversion() {
  if (g_Value)
    return fg_GetLowestBitSetNoZero(g_Value.m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
  return 0;
}

struct CClear {
  unsigned &m_Bits;
  ~CClear() { m_Bits = 0; }
};

int fg_Guard(unsigned _Bits) {
  if (_Bits) {
    CClear Clear{_Bits};
    return fg_GetLowestBitSetNoZero(_Bits);
  }
  return 0;
}

int fg_GuardEnded(unsigned _Bits) {
  if (_Bits) {
    {
      CClear Clear{_Bits};
    }
    return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
  }
  return 0;
}
}

// Round 127: a default argument that clears the value runs where it is used, a
// const copy of a checked value that nothing writes is checked, and a lambda
// called where it is made runs inside the check.
namespace NRound127
{
template <typename t_CType>
int fg_GetLowestBitSetNoZero(t_CType const &_Bits);

unsigned g_Bits;
void fg_Work(int _Unused = (g_Bits = 0)) {}

int fg_Default() {
  if (!g_Bits)
    return -1;
  fg_Work();
  return fg_GetLowestBitSetNoZero(g_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

int fg_Copy(unsigned _Bits) {
  if (!_Bits)
    return -1;
  const unsigned Bits = _Bits;
  return fg_GetLowestBitSetNoZero(Bits);
}

int fg_CopyChanged(unsigned _Bits) {
  const unsigned Bits = _Bits;
  _Bits = 1;
  if (!_Bits)
    return -1;
  return fg_GetLowestBitSetNoZero(Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

int fg_Lambda(unsigned _Bits) {
  if (_Bits)
    return [&] { return fg_GetLowestBitSetNoZero(_Bits); }();
  return -1;
}

int fg_StoredLambda(unsigned _Bits) {
  auto fScan = [&] { return fg_GetLowestBitSetNoZero(_Bits); };
// CHECK-MESSAGES: :[[@LINE-1]]:29: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
  if (_Bits)
    return fScan();
  return -1;
}
}

// Round 128: an output of inline assembly writes the value, so a const copy
// made before isn't what a later test checks, and a call of a function that
// doesn't return leaves.
namespace NRound128
{
template <typename t_CType>
int fg_GetHighestBitSetNoZero(t_CType const &_Bits);
[[noreturn]] void fg_Fail();

int fg_Assembly(unsigned _Bits) {
  unsigned const Copy = _Bits;
  asm("" : "=r"(_Bits) : "0"(1u));
  if (_Bits)
    return fg_GetHighestBitSetNoZero(Copy);
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
  return -1;
}

int fg_NoReturn(unsigned _Bits) {
  if (_Bits == 0)
    fg_Fail();
  return fg_GetHighestBitSetNoZero(_Bits);
}
}

// Round 129: a loop over pointers to the value writes it through the copies,
// and calling a lambda's call operator by name runs its body.
namespace NRound129
{
template <typename t_CType>
int fg_GetHighestBitSetNoZero(t_CType const &_Bits);

int fg_PointerLoop(unsigned _Bits) {
  if (!_Bits)
    return -1;
  unsigned *Pointers[] = {&_Bits};
  for (auto pBits : Pointers)
    *pBits = 0;
  return fg_GetHighestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

int fg_NamedCall(unsigned _Bits) {
  if (!_Bits)
    return -1;
  auto fClear = [&] { _Bits = 0; };
  fClear.operator()();
  return fg_GetHighestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}

// Round 130: a pointer to the bytes of the value writes them, and a loop that
// ends throwing what has to be destroyed runs once.
namespace NRound130
{
template <typename t_CType>
int fg_GetHighestBitSetNoZero(t_CType const &_Bits);

struct CError {
  ~CError();
};

int fg_Bytes(unsigned _Bits) {
  auto pBytes = reinterpret_cast<unsigned char *>(&_Bits);
  if (_Bits) {
    pBytes[1] = 0;
    return fg_GetHighestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
  }
  return -1;
}

void fg_Throws(unsigned _Bits) {
  if (!_Bits)
    return;
  for (;;) {
    fg_GetHighestBitSetNoZero(_Bits);
    _Bits = 0;
    throw CError{};
  }
}
}

// Round 131: a pointer in an array member or a union writes what it points
// to, and the right side of an assignment runs before its left.
namespace NRound131
{
template <typename t_CType>
int fg_GetLowestBitSetNoZero(t_CType const &_Bits);

struct CPointers {
  unsigned *m_pBits[1];
};

int fg_ArrayMember(unsigned _Bits) {
  if (!_Bits)
    return -1;
  CPointers Pointers{{&_Bits}};
  *Pointers.m_pBits[0] = 0;
  return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

union CPointer {
  unsigned *m_pBits;
  void *m_pOther;
};

int fg_Union(unsigned _Bits) {
  if (!_Bits)
    return -1;
  CPointer Pointer{&_Bits};
  *Pointer.m_pBits = 0;
  return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

unsigned &fg_Slot(unsigned &_Bits) {
  _Bits = 0;
  return _Bits;
}

void fg_Assign(unsigned _Bits) {
  if (!_Bits)
    return;
  fg_Slot(_Bits) = fg_GetLowestBitSetNoZero(_Bits);
}
}

// Round 132: a view a local reference extends is only read like one held by
// value.
namespace NRound132
{
template <typename t_CType>
int fg_GetHighestBitSetNoZero(t_CType const &_Bits);

struct CView {
  unsigned &m_Bits;
};

int fg_Extended(unsigned _Bits) {
  if (!_Bits)
    return 0;
  const CView &View = CView{_Bits};
  return fg_GetHighestBitSetNoZero(_Bits) + int(View.m_Bits);
}
}

// Round 133: changing the index of an element a reference is bound to
// changes what a test of it is of, and a const local made with a bit set
// keeps it when narrowed.
namespace NRound133
{
template <typename t_CType>
int fg_GetLowestBitSetNoZero(t_CType const &_Bits);

int fg_Index() {
  unsigned Values[2] = {0, 1};
  unsigned Index = 0;
  unsigned const &Bits = Values[Index];
  ++Index;
  if (Values[Index])
    return fg_GetLowestBitSetNoZero(Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
  return -1;
}

int fg_Narrowed(unsigned _Bits) {
  const unsigned Bits = _Bits | 1u;
  return fg_GetLowestBitSetNoZero(static_cast<unsigned char>(Bits));
}
}

// Round 134: a destructor writes also by subscript and through what a
// pointer member points to, not in what doesn't run, and what the
// initializer of a loop or a switch makes is destroyed when it ends.
namespace NRound134
{
template <typename t_CType>
int fg_GetLowestBitSetNoZero(t_CType const &_Bits);

struct CReset {
  unsigned *m_pBits;
  ~CReset() { m_pBits[0] = 0; }
};

void fg_Subscript(unsigned _Bits) {
  if (_Bits) {
    { CReset Reset{&_Bits}; }
    fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
  }
}

struct CClear {
  unsigned &m_Bits;
  ~CClear() { m_Bits = 0; }
};

void fg_For(unsigned _Bits) {
  if (_Bits) {
    for (CClear Clear{_Bits}; false;) {
    }
    fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
  }
}

void fg_Switch(unsigned _Bits) {
  if (_Bits) {
    switch (CClear Clear{_Bits}; 0) {
    default:
      break;
    }
    fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
  }
}

struct CObserve {
  unsigned &m_Bits;
  ~CObserve() {
    if constexpr (false)
      m_Bits = 0;
    (void)sizeof(m_Bits = 0);
  }
};

void fg_Discarded(unsigned _Bits) {
  if (_Bits) {
    { CObserve Observe{_Bits}; }
    fg_GetLowestBitSetNoZero(_Bits);
  }
}
}

// Round 135: a const member function writes a mutable member also through a
// local reference to it given on, or atomically, and a lambda given to a
// function called through an unchanged local pointer to it runs.
namespace NRound135
{
template <typename t_CType>
int fg_GetLowestBitSetNoZero(t_CType const &_Bits);
void fg_Clear(unsigned &_Bits) { _Bits = 0; }

struct CAliased {
  mutable unsigned m_Bits;
  void f_Clear() const {
    auto &Bits = m_Bits;
    fg_Clear(Bits);
  }
};

int fg_Aliased(CAliased const &_Info) {
  if (!_Info.m_Bits)
    return 0;
  _Info.f_Clear();
  return fg_GetLowestBitSetNoZero(_Info.m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

struct CAtomic {
  mutable unsigned m_Bits;
  void f_Clear() const { __atomic_store_n(&m_Bits, 0, __ATOMIC_RELAXED); }
};

int fg_Atomic(CAtomic const &_Info) {
  if (!_Info.m_Bits)
    return 0;
  _Info.f_Clear();
  return fg_GetLowestBitSetNoZero(_Info.m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

template <typename t_FFunction>
void fg_Invoke(t_FFunction const &_fFunction) {
  _fFunction();
}

int fg_Pointer(unsigned _Bits) {
  if (!_Bits)
    return 0;
  auto fClear = [&] { _Bits = 0; };
  auto fInvoke = &fg_Invoke<decltype(fClear)>;
  fInvoke(fClear);
  return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}

// Round 136: a rewritten comparison that writes the value through a global
// changes it.
namespace NRound136
{
struct CInt {
  unsigned m_Bits;
  bool operator==(CInt const &_Other) const;
};
CInt g_Bits{1};

bool CInt::operator==(CInt const &_Other) const {
  bool bEqual = m_Bits == _Other.m_Bits;
  g_Bits.m_Bits = 0;
  return bEqual;
}

int fg_GetLowestBitSetNoZero(CInt const &_Bits);

void fg_Rewritten() {
  if (g_Bits != CInt{0})
    fg_GetLowestBitSetNoZero(g_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}

namespace NRound137 {
// A callback called with a named operator, `_fWork.operator()()`.
template <typename tf_FWork>
void fg_Named(tf_FWork const &_fWork) { _fWork.operator()(); }

void fg_CalledNamed(unsigned _Bits) {
  if (!_Bits)
    return;
  fg_Named([&] { _Bits = 0; });
  fg_GetHighestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

// But not one a lambda that is only made calls, `return [_fWork] { _fWork();
// };`.
template <typename tf_FWork>
auto fg_Defer(tf_FWork _fWork) { return [_fWork] { _fWork(); }; }

void fg_Deferred(unsigned _Bits) {
  if (!_Bits)
    return;
  auto fLater = fg_Defer([&] { _Bits = 0; });
  fg_GetHighestBitSetNoZero(_Bits);
}

// Unless it is called where it is made.
template <typename tf_FWork>
void fg_Now(tf_FWork _fWork) { [&] { _fWork(); }(); }

void fg_CalledNow(unsigned _Bits) {
  if (!_Bits)
    return;
  fg_Now([&] { _Bits = 0; });
  fg_GetHighestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}

namespace NRound138 {
[[noreturn]] void fg_Fail();

// A callback given on to a function that calls it, `fg_Invoke(_fWork)`.
template <typename tf_FWork>
void fg_Invoke(tf_FWork const &_fWork) { _fWork(); }
template <typename tf_FWork>
void fg_Forward(tf_FWork const &_fWork) { fg_Invoke(_fWork); }

void fg_Forwarded(unsigned _Bits) {
  if (!_Bits)
    return;
  fg_Forward([&] { _Bits = 0; });
  fg_GetHighestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

// But not one only in what isn't evaluated, `noexcept(_fWork())`.
template <typename tf_FWork>
void fg_Query(tf_FWork const &_fWork) { (void)noexcept(_fWork()); }

void fg_Queried(unsigned _Bits) {
  if (!_Bits)
    return;
  fg_Query([&] { _Bits = 0; });
  fg_GetHighestBitSetNoZero(_Bits);
}

// A call that doesn't return in parentheses, `(fg_Fail());`, leaves.
void fg_Parenthesized(unsigned _Bits) {
  if (!_Bits)
    (fg_Fail());
  fg_GetHighestBitSetNoZero(_Bits);
}

void fg_Cast(unsigned _Bits) {
  if (!_Bits)
    (void)fg_Fail();
  fg_GetHighestBitSetNoZero(_Bits);
}
}

namespace NRound139 {
struct CReset {
  unsigned &m_Bits;
  ~CReset() { m_Bits = 0; }
};

// A temporary a local reference extends is destroyed at the end of its block,
// `auto const &Reset = CReset{_Bits};`.
void fg_Extended(unsigned &_Bits) {
  if (!_Bits)
    return;
  {
    auto const &Reset = CReset{_Bits};
  }
  fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

template <typename tf_FWork>
struct TCGuard {
  tf_FWork m_fWork;
  ~TCGuard() { m_fWork(); }
};

// A guard made of a local lambda that writes it, `TCGuard Guard{fReset};`.
void fg_Guarded(unsigned &_Bits) {
  auto fReset = [&] { _Bits = 0; };
  if (!_Bits)
    return;
  {
    TCGuard Guard{fReset};
  }
  fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

// But not of one that only reads it.
void fg_Read(unsigned &_Bits) {
  auto fLog = [&] { return _Bits + 1; };
  if (!_Bits)
    return;
  {
    TCGuard Guard{fLog};
  }
  fg_GetLowestBitSetNoZero(_Bits);
}
}

namespace NRound140 {
// A virtual base is a separate object from a nonvirtual one of the same
// class.
struct CBase {
  unsigned m_Bits = 0;
};
struct CDirect : CBase {};
struct CVirtual : virtual CBase {};
struct CMixed : CDirect, CVirtual {};

void fg_Mixed(CMixed const &_Info) {
  if (_Info.CDirect::m_Bits)
    fg_GetLowestBitSetNoZero(_Info.CVirtual::m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

void fg_Same(CMixed const &_Info) {
  if (_Info.CVirtual::m_Bits)
    fg_GetLowestBitSetNoZero(_Info.CVirtual::m_Bits);
}

// A lambda called where it is made, also with arguments, `(0)`.
void fg_Arguments(unsigned _Bits) {
  if (_Bits)
    [&](int _Ignored) { fg_GetLowestBitSetNoZero(_Bits); }(0);
}
}

namespace NRound141 {
// Through a cast that is moved by a constant, `static_cast<unsigned *>(
// _pValues) + 1`.
void fg_CastOffset(unsigned *_pValues) {
  if (!_pValues[1])
    return;
  auto *pAlias = static_cast<unsigned *>(_pValues) + 1;
  *pAlias = 0;
  fg_GetLowestBitSetNoZero(_pValues[1]);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

// A fact for the analysis that would change it, `(_Bits = 1)`, isn't
// evaluated, so it tells nothing.
void fg_Assumed(unsigned _Bits) {
  DMibAnalyzerAssume((_Bits = 1));
  fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}

namespace NRound142 {
// A test of a reference to either of a conditional, `unsigned &Bits = bFirst
// ? _First : _Second`, tells only of the one it is bound to.
unsigned fg_Either(unsigned _First, unsigned _Second, bool _bFirst) {
  unsigned &Bits = _bFirst ? _First : _Second;
  if (Bits)
    return fg_GetLowestBitSetNoZero(_First);
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
  return 0;
}

// But of the argument when both are it.
unsigned fg_Same(unsigned _First, bool _bFirst) {
  unsigned &Bits = _bFirst ? _First : _First;
  if (Bits)
    return fg_GetLowestBitSetNoZero(_First);
  return 0;
}

// A constructor given it by reference can change it.
struct CSet {
  CSet(unsigned &_Bits) { _Bits = 1; }
};

unsigned fg_Constructed(unsigned _Bits) {
  unsigned const Bits = _Bits;
  CSet Set(_Bits);
  if (_Bits)
    return fg_GetLowestBitSetNoZero(Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
  return 0;
}
}

namespace NRound144 {
template <typename tf_FCleanup>
struct TCGuard {
  tf_FCleanup m_fCleanup;
  ~TCGuard() { m_fCleanup(); }
};

// A guard made of a copy of a local lambda that writes it.
unsigned fg_Copied(unsigned _Bits) {
  if (!_Bits)
    return 0;
  {
    auto fClear = [&] { _Bits = 0; };
    auto fCopy = fClear;
    TCGuard Guard{fCopy};
  }
  return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}

namespace NRound145 {
// A fact for the analysis that compares an integer class without changing it.
struct CInt {
  unsigned m_Bits;
  constexpr CInt(unsigned _Bits) : m_Bits(_Bits) {}
  constexpr bool operator==(CInt const &_Other) const {
    return m_Bits == _Other.m_Bits;
  }
};
int fg_GetLowestBitSetNoZero(CInt const &_Bits);

int fg_Class(CInt const &_Bits) {
  DMibAnalyzerAssume(_Bits != 0);
  return fg_GetLowestBitSetNoZero(_Bits);
}

// A comparison whose other side changes it, `_Bits > fg_Clear(_Bits)`.
unsigned fg_Clear(unsigned &_Bits) {
  _Bits = 0;
  return 0;
}

int fg_Compared(unsigned _Bits) {
  if (_Bits > fg_Clear(_Bits))
    return fg_GetHighestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
  return -1;
}

// An assumption that the compiler ignores, which has side effects.
int fg_Ignored(unsigned _Bits) {
  [[assume((_Bits = 1))]];
  return fg_GetHighestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}

namespace NRound146 {
struct CInfo
{
	mutable unsigned m_Bits;
};
void fg_Clear(CInfo const &_Info)
{
	_Info.m_Bits = 0;
}

// A call through a local pointer to a function that clears it.
int fg_ThroughPointer(CInfo const &_Info)
{
	if (!_Info.m_Bits)
		return 0;
	auto fClear = &fg_Clear;
	fClear(_Info);
	return fg_GetLowestBitSetNoZero(_Info.m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

// A view given to a function that only reads it doesn't change what it views.
struct CView
{
	unsigned &m_Bits;
};
unsigned fg_Read(CView const &_View)
{
	return _View.m_Bits;
}
unsigned fg_ReadDeeper(CView const &_View)
{
	return fg_Read(_View);
}

int fg_OnlyRead(unsigned _Bits)
{
	if (!_Bits)
		return 0;
	CView View{_Bits};
	unsigned Copy = fg_ReadDeeper(View);
	return fg_GetLowestBitSetNoZero(_Bits) + Copy;
}

// But not one given to a function that writes through it.
void fg_Write(CView const &_View)
{
	_View.m_Bits = 0;
}

int fg_Written(unsigned _Bits)
{
	if (!_Bits)
		return 0;
	CView View{_Bits};
	fg_Write(View);
	return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}

namespace NRound147 {
// A destructor that clears it through a member function of it.
struct CClear
{
	unsigned &m_Bits;
	void f_Clear()
	{
		m_Bits = 0;
	}
	~CClear()
	{
		f_Clear();
	}
};

int fg_ThroughHelper(unsigned _Bits)
{
	if (!_Bits)
		return 0;
	{
		CClear Clear{_Bits};
	}
	return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

// And atomically.
struct CAtomicClear
{
	unsigned &m_Bits;
	~CAtomicClear()
	{
		__atomic_store_n(&m_Bits, 0u, __ATOMIC_RELAXED);
	}
};

int fg_Atomic(unsigned _Bits)
{
	if (!_Bits)
		return 0;
	{
		CAtomicClear Clear{_Bits};
	}
	return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

// What `__builtin_constant_p` is given isn't evaluated.
int fg_IsConstant(unsigned _Bits)
{
	return __builtin_constant_p(fg_GetLowestBitSetNoZero(_Bits));
}
}

namespace NRound148 {
// A test of zero assigned to a boolean tests it.
int fg_Assigned(unsigned _Bits)
{
	bool bZero;
	if ((bZero = (_Bits == 0)))
		return fg_GetHighestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	return -1;
}
}

namespace NRound149 {
unsigned g_Bits;
struct CReset
{
	~CReset()
	{
		g_Bits = 0;
	}
};

// The destructor `delete` runs.
int fg_Deleted()
{
	if (!g_Bits)
		return 0;
	delete new CReset;
	return fg_GetLowestBitSetNoZero(g_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}

namespace NRound150 {
struct CInfo
{
	unsigned m_Bits;
	unsigned m_Other;
};

// Through a reference bound to either of a conditional.
int fg_Conditional(CInfo &_Info, CInfo &_Other, bool _bFirst)
{
	if (_Info.m_Bits == 0)
		return 0;
	CInfo &Info = _bFirst ? _Info : _Other;
	Info.m_Bits = 0;
	return fg_GetLowestBitSetNoZero(_Info.m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

// Another member written through a pointer to the object.
int fg_OtherMember(CInfo &_Info)
{
	if (_Info.m_Bits == 0)
		return 0;
	CInfo *pInfo = &_Info;
	pInfo->m_Other = 0;
	return fg_GetLowestBitSetNoZero(_Info.m_Bits);
}

// A temporary destroyed after the scan, which only binds its reference.
struct CClear
{
	unsigned &m_Bits;
	~CClear()
	{
		m_Bits = 0;
	}
};

int fg_Temporary(unsigned _Bits)
{
	if (_Bits == 0)
		return 0;
	return ((void)CClear{_Bits}, fg_GetLowestBitSetNoZero(_Bits));
}
}

namespace NRound151 {
// Through a local reference to a pointer to it, by subscript.
int fg_Subscript(unsigned _Bits)
{
	unsigned *pBits = &_Bits;
	unsigned *&pAlias = pBits;
	if (!_Bits)
		return 0;
	pAlias[0] = 0;
	return fg_GetHighestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

// What isn't evaluated doesn't test it.
int fg_Unevaluated(unsigned _Bits)
{
	if (noexcept(_Bits != 0))
		return fg_GetHighestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	return 0;
}

// A test saved in a local.
int fg_Saved(unsigned _Bits)
{
	const bool bNonZero = _Bits != 0;
	if (bNonZero)
		return fg_GetHighestBitSetNoZero(_Bits);
	return 0;
}

// But not one that it is changed after.
int fg_SavedChanged(unsigned _Bits)
{
	const bool bNonZero = _Bits != 0;
	_Bits = 0;
	if (bNonZero)
		return fg_GetHighestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	return 0;
}
}

namespace NRound152 {
struct CBorrowed
{
	unsigned *m_pBits;
};

// A pointer member pointed elsewhere doesn't change what it pointed to.
int fg_Repointed(unsigned _Bits)
{
	unsigned Other = 0;
	CBorrowed Borrowed{&_Bits};
	if (!_Bits)
		return 0;
	Borrowed.m_pBits = &Other;
	return fg_GetLowestBitSetNoZero(_Bits);
}
}

namespace NRound153 {
// Changed through a cast to a reference after the copy is made.
int fg_CastWrite(unsigned _Bits) {
  const unsigned Bits = _Bits;
  static_cast<unsigned &>(_Bits) = 1;
  if (_Bits)
    return fg_GetLowestBitSetNoZero(Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
  return -1;
}

// A const local of an integer class made with a set bit.
struct CInt {
  unsigned m_Bits;
  constexpr CInt(unsigned _Bits) : m_Bits(_Bits) {}
  constexpr CInt operator|(CInt const &_Other) const { return CInt(m_Bits | _Other.m_Bits); }
};
int fg_ClassLocal(CInt const &_Bits) {
  const CInt Bits = _Bits | CInt(1);
  return fg_GetLowestBitSetNoZero(Bits);
}
}

namespace NRound154 {
// The narrowed value tested as it is.
int fg_Scan(unsigned _Bits) {
  if (static_cast<unsigned char>(_Bits))
    return fg_GetLowestBitSetNoZero(static_cast<unsigned char>(_Bits));
  return 0;
}
}

namespace NRound155 {
// A pointer to it that a helper only points elsewhere.
void fg_Reset(unsigned *&_pBits) { _pBits = nullptr; }
void fg_ResetThrough(unsigned **_ppBits) { *_ppBits = nullptr; }
void fg_Clear(unsigned *_pBits) { *_pBits = 0; }
int fg_Checked(unsigned _Bits) {
  if (!_Bits)
    return 0;
  unsigned *pBits = &_Bits;
  fg_Reset(pBits);
  fg_ResetThrough(&pBits);
  return fg_GetLowestBitSetNoZero(_Bits);
}
// One that writes what it points to does change it.
int fg_Cleared(unsigned _Bits) {
  if (!_Bits)
    return 0;
  unsigned *pBits = &_Bits;
  fg_Clear(pBits);
  return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
// What isn't evaluated.
int fg_Unevaluated(unsigned _Bits) {
  if (!_Bits)
    return 0;
  (void)__builtin_constant_p((_Bits = 0));
  return fg_GetLowestBitSetNoZero(_Bits);
}
int fg_Unselected(unsigned _Bits) {
  return __builtin_choose_expr(1, 0, fg_GetLowestBitSetNoZero(_Bits));
}
}

namespace NRound156 {
// Written through a reference a const accessor gives.
struct CView {
  unsigned &m_Bits;
  unsigned &f_Bits() const { return m_Bits; }
};
int fg_Accessor(CView const &_View) {
  if (!_View.m_Bits)
    return -1;
  unsigned &Bits = _View.f_Bits();
  Bits = 0;
  return fg_GetLowestBitSetNoZero(_View.m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
// A test compared with an integer.
int fg_ComparedWithOne(unsigned _Bits) {
  if ((_Bits != 0) == 1)
    return fg_GetLowestBitSetNoZero(_Bits);
  return -1;
}
int fg_ComparedWithZero(unsigned _Bits) {
  if ((_Bits == 0) == 0)
    return fg_GetLowestBitSetNoZero(_Bits);
  return -1;
}
// An assumption in parentheses.
int fg_Assumed(unsigned _Bits) {
  (DMibAnalyzerAssume(_Bits != 0));
  return fg_GetLowestBitSetNoZero(_Bits);
}
}

namespace NRound157 {
// A local made of it that nothing names, destroyed after the scan.
struct CClear {
  unsigned &m_Bits;
  CClear(unsigned &_Bits) : m_Bits(_Bits) {}
  ~CClear() { m_Bits = 0; }
};
int fg_Scan(unsigned _Bits) {
  if (!_Bits)
    return -1;
  CClear Clear(_Bits);
  return fg_GetLowestBitSetNoZero(_Bits);
}
// Destroyed before it.
int fg_Destroyed(unsigned _Bits) {
  if (!_Bits)
    return -1;
  {
    CClear Clear(_Bits);
  }
  return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}

namespace NRound158 {
// A guard given it after it is made.
struct CClear {
  unsigned *m_pBits = nullptr;
  ~CClear() {
    if (m_pBits)
      *m_pBits = 0;
  }
};
void fg_Given(unsigned _Bits) {
  if (!_Bits)
    return;
  {
    CClear Clear;
    Clear.m_pBits = &_Bits;
  }
  fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
// A virtual call, whose override can write what is mutable.
struct CInfo {
  mutable unsigned m_Bits;
};
struct ICMutator {
  virtual void f_Clear(CInfo const &_Info) const {}
};
void fg_Virtual(CInfo const &_Info, ICMutator const &_Mutator) {
  if (!_Info.m_Bits)
    return;
  _Mutator.f_Clear(_Info);
  fg_GetLowestBitSetNoZero(_Info.m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
// Through a pointer a lambda returns of what it captures.
void fg_Lambda(unsigned _Bits) {
  auto pBits = [&] { return &_Bits; }();
  if (!_Bits)
    return;
  *pBits = 0;
  fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
// A switch that leaves for zero.
int fg_Switch(unsigned _Bits) {
  switch (_Bits) {
  case 0:
    return -1;
  default:
    return fg_GetLowestBitSetNoZero(_Bits);
  }
}
// One that falls through from zero.
int fg_FallThrough(unsigned _Bits) {
  switch (_Bits) {
  case 0:
    _Bits = 0;
    [[fallthrough]];
  case 1:
    return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
  default:
    return -1;
  }
}
}

namespace NRound159 {
// Promoted to int, both are still never negative.
using uint8 = unsigned char;
void fg_Promoted(uint8 _Bits, uint8 _Limit) {
  if (_Bits <= _Limit)
    fg_GetHighestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
// A reference bound through a pointer to a member.
struct CInfo {
  unsigned m_Bits;
};
void fg_MemberPointer(CInfo &_Info) {
  if (_Info.m_Bits != 0) {
    unsigned &Bits = _Info.*(&CInfo::m_Bits);
    Bits = 0;
    fg_GetHighestBitSetNoZero(_Info.m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
  }
}
}

namespace NRound160 {
unsigned g_Bits;
void fg_ClearGlobal() { g_Bits = 0; }
// Through a constant pointer to a function that isn't local.
auto const g_fClear = &fg_ClearGlobal;
void fg_Constant() {
  if (!g_Bits)
    return;
  g_fClear();
  fg_GetLowestBitSetNoZero(g_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
// Written after the scan, in an operand that runs after it.
void fg_Sequenced(unsigned _Bits) {
  if (!_Bits)
    return;
  auto Value = fg_GetLowestBitSetNoZero(_Bits) << (_Bits = 0);
}
// Destroyed at the end of the scope of the reference that extends it.
struct CReset {
  ~CReset() { g_Bits = 0; }
};
void fg_Extended() {
  if (!g_Bits)
    return;
  CReset const &Reset = CReset{};
  fg_GetLowestBitSetNoZero(g_Bits);
}
// A union whose destructor destroys no member.
union CUnion {
  CReset m_Reset;
  unsigned m_Value;
  CUnion() : m_Value(0) {}
  ~CUnion() {}
};
void fg_Union() {
  if (!g_Bits)
    return;
  {
    CUnion Value;
  }
  fg_GetLowestBitSetNoZero(g_Bits);
}
}

namespace NRound161 {
// A temporary aggregate made of it, destroyed at the end of the statement.
struct CClear {
  unsigned &m_Bits;
  ~CClear() { m_Bits = 0; }
};
int fg_Temporary(unsigned _Bits) {
  if (!_Bits)
    return -1;
  (void)CClear{_Bits};
  return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
// Changed through a reference member of an aggregate after the copy.
struct CSet {
  unsigned &m_Bits;
};
int fg_Copy(unsigned _Bits) {
  const unsigned Bits = _Bits;
  CSet Setter{_Bits};
  Setter.m_Bits = 1;
  if (_Bits)
    return fg_GetLowestBitSetNoZero(Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
  return -1;
}
// A temporary a reference extends, made by a constructor that only binds it.
struct CBound {
  unsigned &m_Bits;
  CBound(unsigned &_Bits) : m_Bits(_Bits) {}
  ~CBound() { m_Bits = 0; }
};
int fg_Extended(unsigned _Bits) {
  if (!_Bits)
    return -1;
  auto const &Bound = CBound{_Bits};
  return fg_GetLowestBitSetNoZero(_Bits);
}
}

namespace NRound162 {
// A constructor that writes through a pointer it is given.
struct CReset {
  CReset(unsigned *_pBits) { *_pBits = 0; }
};
int fg_Constructed(unsigned _Bits) {
  if (!_Bits)
    return -1;
  CReset Reset{&_Bits};
  return fg_GetHighestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
// The value an integer class is stepped to, compared.
struct CInt {
  unsigned m_Bits;
  CInt &operator--() {
    --m_Bits;
    return *this;
  }
  bool operator!=(unsigned _Other) const { return m_Bits != _Other; }
};
int fg_GetHighestBitSetNoZero(CInt const &_Bits);
int fg_Stepped(CInt _Bits) {
  if (--_Bits != 0u)
    return fg_GetHighestBitSetNoZero(_Bits);
  return -1;
}
}

namespace NRound163 {
unsigned g_Bits = 1;
// A temporary of a condition whose member's destructor clears it.
struct CReset {
  ~CReset() { g_Bits = 0; }
};
struct CWrapper {
  CReset m_Reset;
};
int fg_Wrapped() {
  if (((void)CWrapper{}, g_Bits))
    return fg_GetLowestBitSetNoZero(g_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
  return -1;
}
// A braced case that leaves for zero.
int fg_Braced(unsigned _Bits) {
  switch (_Bits) {
  case 0: {
    return -1;
  }
  default:
    return fg_GetLowestBitSetNoZero(_Bits);
  }
}
// Through a local pointer to a bit scan.
int fg_GetLowestBitSetNoZero(unsigned _Bits);
int fg_Pointer() {
  auto fScan = &fg_GetLowestBitSetNoZero;
  return fScan(0u);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: 'fg_GetLowestBitSetNoZero' is called with zero [malterlib-no-zero-bit-scan]
}
}

namespace NRound164 {
int fg_GetLowestBitSetNoZero(unsigned const &_Bits);
// Deleting an object made with it, whose destructor writes through what it
// keeps.
struct CClear {
  unsigned &m_Bits;
  ~CClear() { m_Bits = 0; }
};
int fg_Deleted(unsigned _Bits) {
  auto pClear = new CClear{_Bits};
  if (!_Bits) {
    delete pClear;
    return -1;
  }
  delete pClear;
  return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
// The allocation and deallocation functions that `new` and `delete` call.
unsigned g_Bits = 1;
struct CAllocated {
  static void *operator new(__SIZE_TYPE__ _Size) {
    g_Bits = 0;
    return __builtin_malloc(_Size);
  }
};
int fg_New() {
  if (!g_Bits)
    return -1;
  auto pObject = new CAllocated;
  int Result = fg_GetLowestBitSetNoZero(g_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
  delete pObject;
  return Result;
}
struct CDeallocated {
  static void operator delete(void *_pMemory) {
    g_Bits = 0;
    __builtin_free(_pMemory);
  }
};
int fg_Delete(CDeallocated *_pObject) {
  if (!g_Bits)
    return -1;
  delete _pObject;
  return fg_GetLowestBitSetNoZero(g_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}

namespace NRound165 {
int fg_GetLowestBitSetNoZero(unsigned _Bits);
// A copy constructor that writes through the member it binds to the one of
// what it copies.
struct CView {
  unsigned &m_Bits;
  explicit CView(unsigned &_Bits) : m_Bits(_Bits) {}
  CView(CView const &_Other) : m_Bits(_Other.m_Bits) { m_Bits = 0; }
};
void fg_Observe(CView _Value) {}
int fg_Copied(CView const &_View) {
  if (!_View.m_Bits)
    return -1;
  fg_Observe(_View);
  return fg_GetLowestBitSetNoZero(_View.m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
// A guard whose branches both leave.
int fg_BothLeave(unsigned _Bits, bool _bThrow) {
  if (_Bits == 0) {
    if (_bThrow)
      throw 1;
    else
      return -1;
  }
  return fg_GetLowestBitSetNoZero(_Bits);
}
}

namespace NRound166 {
int fg_GetLowestBitSetNoZero(unsigned _Bits);
// Not pointed at it in a branch of an `if constexpr` that is discarded.
int fg_Discarded(unsigned _Bits) {
  if (!_Bits)
    return 0;
  unsigned Other = 0;
  unsigned *pBits = &Other;
  if constexpr (false)
    pBits = &_Bits;
  *pBits = 0;
  return fg_GetLowestBitSetNoZero(_Bits);
}
// Not the destructor of another object, which writes that object.
struct CBits {
  unsigned m_Bits;
  ~CBits() { m_Bits = 0; }
  int f_Run() {
    if (CBits Other{0}; !m_Bits)
      return 0;
    return fg_GetLowestBitSetNoZero(m_Bits);
  }
};
}

namespace NRound167 {
int fg_GetLowestBitSetNoZero(unsigned _Bits);
unsigned g_Bits;
// A write deeper in the calls than is looked into.
template <unsigned t_Depth> void fg_Clear() {
  if constexpr (t_Depth != 0)
    fg_Clear<t_Depth - 1>();
  else
    g_Bits = 0;
}
void fg_Deep() {
  if (g_Bits) {
    fg_Clear<8>();
    fg_GetLowestBitSetNoZero(g_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
  }
}
// Through a constant pointer that isn't local, or a reference to it.
constexpr auto gc_fScan = &fg_GetLowestBitSetNoZero;
void fg_Constant() {
  gc_fScan(0u);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'fg_GetLowestBitSetNoZero' is called with zero [malterlib-no-zero-bit-scan]
  auto &fScan = fg_GetLowestBitSetNoZero;
  fScan(0u);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'fg_GetLowestBitSetNoZero' is called with zero [malterlib-no-zero-bit-scan]
}
}

namespace NRound168 {
int fg_GetLowestBitSetNoZero(unsigned const &_Bits);
// A global checked before a suspension, during which what else runs can
// clear it.
unsigned g_Bits;
CRound82Task fg_Suspended() {
  if (!g_Bits)
    co_return;
  co_await fg_Round82Work(0);
  fg_GetLowestBitSetNoZero(g_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
// Not a reference to const bound to a temporary that isn't zero.
int fg_Bound(unsigned _Bits) {
  unsigned const &Bits = _Bits | 1u;
  return fg_GetLowestBitSetNoZero(Bits);
}
}

namespace NRound169 {
int fg_GetLowestBitSetNoZero(unsigned const &_Bits);
// Through a copy of a pointer member of a local.
struct CView {
  unsigned *m_pBits;
};
void fg_MemberCopy(unsigned _Bits) {
  if (_Bits) {
    CView View{&_Bits};
    auto pBits = View.m_pBits;
    *pBits = 0;
    fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
  }
}
// What a pointer parameter points to, which can change while it is
// suspended, but not a parameter held by value.
CRound82Task fg_Pointed(unsigned *_pBits) {
  if (!*_pBits)
    co_return;
  co_await fg_Round82Work(0);
  fg_GetLowestBitSetNoZero(*_pBits);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
CRound82Task fg_ByValue(unsigned _Bits) {
  if (!_Bits)
    co_return;
  co_await fg_Round82Work(0);
  fg_GetLowestBitSetNoZero(_Bits);
}
}

namespace NRound170 {
int fg_GetLowestBitSetNoZero(unsigned const &_Bits);
// What a reference member of a parameter refers to, which can change while
// it is suspended, but not after an await that doesn't suspend.
struct CView {
  unsigned &m_Bits;
};
CRound82Task fg_Shared(CView _View) {
  if (!_View.m_Bits)
    co_return;
  co_await fg_Round82Work(0);
  fg_GetLowestBitSetNoZero(_View.m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
struct CReady {
  bool await_ready() noexcept { return true; }
  template <typename t_CHandle> void await_suspend(t_CHandle _Handle) noexcept {}
  void await_resume() noexcept {}
};
unsigned g_Bits;
CRound82Task fg_Ready() {
  if (!g_Bits)
    co_return;
  co_await CReady{};
  fg_GetLowestBitSetNoZero(g_Bits);
}
// Pointed at it through a pointer to the pointer, and written through a
// reference a lambda returns.
int fg_Redirected(unsigned _Bits) {
  if (!_Bits)
    return -1;
  unsigned Other = 1;
  unsigned *pBits = &Other;
  unsigned **ppBits = &pBits;
  *ppBits = &_Bits;
  *pBits = 0;
  return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
int fg_Returned(unsigned _Bits) {
  if (!_Bits)
    return -1;
  auto fBits = [&]() -> unsigned & { return _Bits; };
  fBits() = 0;
  return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}

namespace NRound171 {
int fg_GetLowestBitSetNoZero(unsigned const &_Bits);
// Where an atomic operation puts what it reads, or what a compare and
// exchange gives back.
void fg_Load(unsigned _Bits) {
  if (!_Bits)
    return;
  unsigned Zero = 0;
  __atomic_load(&Zero, &_Bits, __ATOMIC_RELAXED);
  fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
void fg_CompareExchange(unsigned _Bits) {
  if (!_Bits)
    return;
  unsigned Zero = 0;
  __atomic_compare_exchange_n(&Zero, &_Bits, 1u, false, __ATOMIC_RELAXED,
                              __ATOMIC_RELAXED);
  fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}

namespace NRound172 {
int fg_GetLowestBitSetNoZero(unsigned _Bits);
unsigned g_Bits;
void fg_Clear() { g_Bits = 0; }
// Through a local pointer dereferenced.
int fg_Dereferenced() {
  if (!g_Bits)
    return -1;
  auto fClear = &fg_Clear;
  (*fClear)();
  return fg_GetLowestBitSetNoZero(g_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
int fg_DereferencedScan() {
  auto fScan = &fg_GetLowestBitSetNoZero;
  return (*fScan)(0u);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: 'fg_GetLowestBitSetNoZero' is called with zero [malterlib-no-zero-bit-scan]
}
}

namespace NRound173 {
unsigned fg_GetHighestBitSetNoZero(unsigned _Bits);
// A conversion that reads atomically can give another value each time.
struct CAtomicBits {
  unsigned m_Value;
  operator unsigned() const {
    return __atomic_load_n(&m_Value, __ATOMIC_RELAXED);
  }
};
unsigned fg_Scan(CAtomicBits const &_Bits) {
  if (_Bits)
    return fg_GetHighestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
  return 0;
}
}

namespace NRound174 {
// Compared with a floating point zero.
int fg_FloatZero(unsigned _Bits) {
  if (_Bits == 0.0)
    return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
  return 0;
}
int fg_FloatNotZero(unsigned _Bits) {
  if (_Bits != 0.0)
    return fg_GetLowestBitSetNoZero(_Bits);
  return 0;
}
// A conditional whose condition excludes zero from the value it chooses.
int fg_Chosen(unsigned _Bits) { return fg_GetLowestBitSetNoZero(_Bits ? _Bits : 1u); }
int fg_ChosenWhenZero(unsigned _Bits) {
  return fg_GetLowestBitSetNoZero(_Bits ? 1u : _Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
int fg_ChosenZero(unsigned _Bits) { return fg_GetLowestBitSetNoZero(_Bits ? _Bits : 0u); }
// CHECK-MESSAGES: :[[@LINE-1]]:44: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
int fg_ChosenOther(unsigned _Bits, unsigned _Other) {
  return fg_GetLowestBitSetNoZero(_Bits ? _Other : 1u);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}

namespace NRound175 {
// A const copy of what a helper that casts the const away changes.
unsigned &fg_RemoveQualifiers(unsigned const &_Value) {
  return const_cast<unsigned &>(_Value);
}
int fg_Copied(unsigned _Bits) {
  const unsigned Bits = _Bits;
  fg_RemoveQualifiers(_Bits) = 1;
  if (_Bits)
    return fg_GetLowestBitSetNoZero(Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
  return 0;
}
// Written through a pointer moved by what isn't constant.
int fg_Moved(unsigned *_pBits, unsigned _Offset) {
  if (!_pBits[0])
    return 0;
  unsigned *pAlias = _pBits + _Offset;
  *pAlias = 0;
  return fg_GetLowestBitSetNoZero(_pBits[0]);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}

namespace NRound176 {
struct CView {
  unsigned &m_Bits;
};
struct COuter {
  CView m_View;
};
// Bound in an array, or a nested aggregate, that only reads it.
int fg_Array(unsigned _Bits) {
  if (!_Bits)
    return -1;
  CView Views[]{{_Bits}};
  (void)Views;
  return fg_GetLowestBitSetNoZero(_Bits);
}
int fg_Nested(unsigned _Bits) {
  if (!_Bits)
    return -1;
  COuter Outer{{_Bits}};
  (void)Outer;
  return fg_GetLowestBitSetNoZero(_Bits);
}
// But not one that writes it.
int fg_Written(unsigned _Bits) {
  if (!_Bits)
    return -1;
  CView Views[]{{_Bits}};
  Views[0].m_Bits = 0;
  return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}

namespace NRound177 {
// Compared with a floating point constant.
int fg_BelowHalf(unsigned _Bits) {
  if (_Bits < 0.5)
    return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
  return 0;
}
int fg_AboveHalf(unsigned _Bits) {
  if (_Bits > 0.5)
    return fg_GetLowestBitSetNoZero(_Bits);
  return 0;
}
int fg_HalfAbove(unsigned _Bits) {
  if (0.5 < _Bits)
    return fg_GetLowestBitSetNoZero(_Bits);
  return 0;
}
}

namespace NRound178 {
// Negated after a conversion to floating point, which is zero only when the
// integer is.
int fg_Converted(unsigned _Bits) {
  if (!static_cast<double>(_Bits))
    return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
  return -1;
}
int fg_ConvertedChecked(unsigned _Bits) {
  if (static_cast<double>(_Bits))
    return fg_GetLowestBitSetNoZero(_Bits);
  return -1;
}
}

namespace NRound179 {
unsigned g_Bits;
void fg_Clear() { g_Bits = 0; }
// Called through a copy of a local pointer to it.
int fg_Copied() {
  if (!g_Bits)
    return -1;
  auto fClear = &fg_Clear;
  auto fCopy = fClear;
  fCopy();
  return fg_GetLowestBitSetNoZero(g_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}

namespace NRound180 {
// An increment that isn't evaluated.
void fg_Run(unsigned _Bits) {
  if (_Bits != 0 && noexcept(_Bits++))
    fg_GetLowestBitSetNoZero(_Bits);
}
}

namespace NRound181 {
unsigned g_Bits;
unsigned &g_Alias = g_Bits;
unsigned *const gc_pBits = &g_Bits;
// Written through a reference or a const pointer at namespace scope.
int fg_ThroughReference() {
  if (!g_Bits)
    return -1;
  g_Alias = 0;
  return fg_GetLowestBitSetNoZero(g_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
int fg_ThroughPointer() {
  if (!g_Bits)
    return -1;
  *gc_pBits = 0;
  return fg_GetLowestBitSetNoZero(g_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}

namespace NRound182 {
// Written through what a function makes of the address of what it is given.
struct CView {
  unsigned *m_pBits;
};
CView fg_View(unsigned &_Bits) { return {&_Bits}; }
int fg_Viewed(unsigned _Bits) {
  CView View = fg_View(_Bits);
  if (_Bits) {
    *View.m_pBits = 0;
    return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
  }
  return -1;
}
}

namespace NRound183 {
// Written in a handler, which doesn't run before what the try block does.
unsigned fg_Scan(unsigned _Bits) {
  if (_Bits == 0)
    return 0;
  try {
    return fg_GetLowestBitSetNoZero(_Bits);
  } catch (...) {
    _Bits = 0;
    return _Bits;
  }
}
}

namespace NRound184 {
// A lambda a scope exit keeps, which it calls only when it ends.
struct COnScopeExit {
  ~COnScopeExit();
};
struct COnScopeExitHelper {
  template <typename t_FFunction>
  COnScopeExit operator/(t_FFunction &&_fFunction) const {
    return {};
  }
};
extern COnScopeExitHelper const &g_OnScopeExit;
int fg_Scan(unsigned _Bits) {
  if (!_Bits)
    return -1;
  auto Cleanup = g_OnScopeExit / [&] { _Bits = 0; };
  return fg_GetLowestBitSetNoZero(_Bits);
}
}

namespace NRound185 {
int fg_GetHighestBitSetNoZero(umint _Value);
template <typename t_CUpper, typename t_CLower>
struct TCInt {
  t_CLower m_Lower{};
  t_CUpper m_Upper{};
  constexpr TCInt(unsigned _Value = 0) : m_Lower(_Value) {}
  constexpr int operator<=>(TCInt const &_Other) const;
  constexpr bool operator==(TCInt const &_Other) const;
  constexpr TCInt operator<<(unsigned _Shift) const;
};
using uint256 = TCInt<unsigned __int128, unsigned __int128>;
int fg_GetHighestBitSetNoZero(uint256 const &_Bits);
// Zero passes a bound of an integer class of unsigned words.
int fg_Bounded(uint256 const &_Bits, uint256 const &_Maximum) {
  if (_Bits <= _Maximum)
    return fg_GetHighestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
  return -1;
}
// But not one it is above.
int fg_Above(uint256 const &_Bits, uint256 const &_Minimum) {
  if (_Bits > _Minimum)
    return fg_GetHighestBitSetNoZero(_Bits);
  return -1;
}
// A single bit of one, made of an odd integer.
int fg_Mask(unsigned _Shift) {
  if (_Shift >= 256)
    return -1;
  return fg_GetHighestBitSetNoZero(uint256(1) << _Shift);
}
int fg_EvenMask(unsigned _Shift) {
  return fg_GetHighestBitSetNoZero(uint256(2) << _Shift);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
// A member checked through a smart pointer, as through a pointer.
template <typename t_CType>
struct TCUniquePointer {
  t_CType *operator->() const;
  t_CType &operator*() const;
};
struct CInfo {
  umint m_Bits;
};
int fg_Scan(TCUniquePointer<CInfo> const &_pInfo) {
  if (_pInfo->m_Bits)
    return fg_GetHighestBitSetNoZero(_pInfo->m_Bits);
  return -1;
}
int fg_Dereferenced(TCUniquePointer<CInfo> const &_pInfo) {
  if ((*_pInfo).m_Bits)
    return fg_GetHighestBitSetNoZero(_pInfo->m_Bits);
  return -1;
}
// Not when it is pointed elsewhere, nor the member of another.
int fg_Pointed(TCUniquePointer<CInfo> &_pInfo, TCUniquePointer<CInfo> const &_pOther) {
  if (_pInfo->m_Bits) {
    _pInfo = _pOther;
    return fg_GetHighestBitSetNoZero(_pInfo->m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
  }
  return -1;
}
int fg_Other(TCUniquePointer<CInfo> const &_pInfo, TCUniquePointer<CInfo> const &_pOther) {
  if (_pInfo->m_Bits)
    return fg_GetHighestBitSetNoZero(_pOther->m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
  return -1;
}
}

namespace NRound186 {
int fg_GetLowestBitSetNoZero(umint _Value);
// A destructor that clears through a scope guard of its own.
template <typename t_FCleanup>
struct TCOnScopeExit {
  t_FCleanup m_fCleanup;
  ~TCOnScopeExit() { m_fCleanup(); }
};
struct COnScopeExitHelper {};
inline COnScopeExitHelper g_OnScopeExit;
template <typename t_FCleanup>
TCOnScopeExit<t_FCleanup> operator/(COnScopeExitHelper, t_FCleanup &&_fCleanup) {
  return {static_cast<t_FCleanup &&>(_fCleanup)};
}
struct CReset {
  umint &m_Bits;
  ~CReset() {
    auto Cleanup = g_OnScopeExit / [this] { m_Bits = 0; };
  }
};
int fg_Reset(umint _Bits) {
  if (!_Bits)
    return -1;
  { CReset Reset{_Bits}; }
  return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
// An element read through an index operator that only gives access to it.
void fg_Fail();
struct CVector {
  umint *m_pData;
  umint m_Len;
  umint &operator[](umint _Index) {
    if (_Index >= m_Len)
      throw 1;
    return m_pData[_Index];
  }
  umint &f_Push(umint _Value) {
    m_pData[m_Len] = _Value;
    return m_pData[m_Len++];
  }
};
int fg_Element(CVector &_Values) {
  if (_Values[0] == 0)
    return -1;
  return fg_GetLowestBitSetNoZero(_Values[0]);
}
int fg_Changed(CVector &_Values) {
  if (_Values[0] == 0)
    return -1;
  _Values[0] = 0;
  return fg_GetLowestBitSetNoZero(_Values[0]);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}

namespace NRound187 {
int fg_GetLowestBitSetNoZero(umint _Value);
int fg_GetHighestBitSetNoZero(umint _Value);
// Written through what a smart pointer gives of what it points to.
template <typename t_CType>
struct TCUniquePointer {
  t_CType *m_pData;
  t_CType *f_Get() const { return m_pData; }
  t_CType *operator->() const { return m_pData; }
};
struct CInfo {
  umint m_Bits;
};
int fg_Written(TCUniquePointer<CInfo> const &_pInfo) {
  if (!_pInfo->m_Bits)
    return -1;
  auto pInfo = _pInfo.f_Get();
  pInfo->m_Bits = 0;
  return fg_GetLowestBitSetNoZero(_pInfo->m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
int fg_Read(TCUniquePointer<CInfo> const &_pInfo) {
  if (!_pInfo->m_Bits)
    return -1;
  auto pInfo = _pInfo.f_Get();
  umint Copy = pInfo->m_Bits;
  return fg_GetLowestBitSetNoZero(_pInfo->m_Bits) + int(Copy);
}
// An integer class made of words, whose first is the upper one, isn't made
// of an odd integer.
template <typename t_CUpper, typename t_CLower>
struct TCInt {
  t_CLower m_Lower{};
  t_CUpper m_Upper{};
  constexpr TCInt(unsigned _Value = 0) : m_Lower(_Value) {}
  constexpr TCInt(t_CUpper _Upper, t_CLower _Lower) : m_Lower(_Lower), m_Upper(_Upper) {}
  constexpr TCInt operator<<(unsigned _Shift) const;
};
using uint256 = TCInt<unsigned __int128, unsigned __int128>;
int fg_GetHighestBitSetNoZero(uint256 const &_Bits);
int fg_Upper(unsigned _Shift) {
  if (_Shift >= 256)
    return -1;
  return fg_GetHighestBitSetNoZero(uint256(1, 0) << _Shift);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}

namespace NRound188 {
int fg_GetLowestBitSetNoZero(umint _Value);
// Another member written through a smart pointer, like through a pointer.
struct CInfo {
  umint m_Bits;
  umint m_nQueries;
};
template <typename t_CType>
struct TCUniquePointer {
  t_CType *m_pPointTo;
  t_CType *operator->() const { return m_pPointTo; }
};
int fg_Other(TCUniquePointer<CInfo> const &_pInfo) {
  if (!_pInfo->m_Bits)
    return -1;
  ++_pInfo->m_nQueries;
  return fg_GetLowestBitSetNoZero(_pInfo->m_Bits);
}
int fg_Same(TCUniquePointer<CInfo> const &_pInfo) {
  if (!_pInfo->m_Bits)
    return -1;
  --_pInfo->m_Bits;
  return fg_GetLowestBitSetNoZero(_pInfo->m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}

namespace NRound189 {
int fg_GetLowestBitSetNoZero(umint _Value);
// A scope guard called early runs its lambda.
template <typename t_FCleanup>
struct TCOnScopeExit {
  t_FCleanup mp_fOnExit;
  bool mp_bValid = true;
  ~TCOnScopeExit() {
    if (mp_bValid)
      mp_fOnExit();
  }
  void operator()() {
    if (mp_bValid) {
      mp_bValid = false;
      mp_fOnExit();
    }
  }
};
struct COnScopeExitHelper {
  template <typename t_FCleanup>
  TCOnScopeExit<t_FCleanup> operator/(t_FCleanup &&_fCleanup) const {
    return {static_cast<t_FCleanup &&>(_fCleanup)};
  }
};
extern COnScopeExitHelper const &g_OnScopeExit;
int fg_Early(umint _Bits) {
  auto Cleanup = g_OnScopeExit / [&] { _Bits = 0; };
  if (!_Bits)
    return -1;
  Cleanup();
  return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
// A member of an element by an index operator that can't change the vector.
struct CInfo {
  umint m_Bits;
};
template <typename t_CType>
struct TCVector {
  t_CType *m_pData;
  t_CType const &operator[](umint _iIndex) const { return m_pData[_iIndex]; }
};
int fg_Element(TCVector<CInfo> const &_Infos, umint _iInfo) {
  if (_Infos[_iInfo].m_Bits)
    return fg_GetLowestBitSetNoZero(_Infos[_iInfo].m_Bits);
  return -1;
}
int fg_OtherElement(TCVector<CInfo> const &_Infos, umint _iInfo) {
  if (_Infos[_iInfo].m_Bits)
    return fg_GetLowestBitSetNoZero(_Infos[0].m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
  return -1;
}
}

namespace NRound190 {
int fg_GetHighestBitSetNoZero(umint _Value);
// Written through what a member function gives back of a pointer member.
struct CTraits {
  using CChar = unsigned char;
};
template <typename t_CStrTraits>
struct TCStr {
  using CChar = typename t_CStrTraits::CChar;
  CChar *m_pData;
  CChar *f_GetStrWritable() { return m_pData; }
  int f_Scan() {
    if (m_pData[0] == 0)
      return -1;
    CChar *pData = f_GetStrWritable();
    pData[0] = 0;
    return fg_GetHighestBitSetNoZero(m_pData[0]);
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
  }
  int f_Read() {
    if (m_pData[0] == 0)
      return -1;
    CChar *pData = f_GetStrWritable();
    CChar Copy = pData[1];
    return fg_GetHighestBitSetNoZero(m_pData[0]) + Copy;
  }
};
int fg_Use(TCStr<CTraits> &_Str) { return _Str.f_Scan() + _Str.f_Read(); }
}

namespace NRound191 {
int fg_GetLowestBitSetNoZero(umint _Value);
// Written through what a smart pointer gives, read through its operator.
template <typename t_CType>
struct TCUniquePointer {
  t_CType *m_pData;
  t_CType *f_Get() const { return m_pData; }
  t_CType *operator->() const { return m_pData; }
  t_CType &operator*() const { return *m_pData; }
};
int fg_Aliased(TCUniquePointer<umint> const &_pBits) {
  auto pBits = _pBits.f_Get();
  if (*_pBits) {
    *pBits = 0;
    return fg_GetLowestBitSetNoZero(*_pBits);
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
  }
  return -1;
}
int fg_AliasRead(TCUniquePointer<umint> const &_pBits) {
  auto pBits = _pBits.f_Get();
  if (*_pBits) {
    umint Copy = *pBits;
    return fg_GetLowestBitSetNoZero(*_pBits) + int(Copy);
  }
  return -1;
}
// What a comparison that C++20 rewrites tests of a value it steps or assigns.
struct CInt {
  umint m_Lower = 0;
  constexpr CInt(unsigned _Value = 0) : m_Lower(_Value) {}
  constexpr bool operator==(CInt const &_Other) const { return m_Lower == _Other.m_Lower; }
  constexpr CInt &operator--() {
    --m_Lower;
    return *this;
  }
  constexpr CInt &operator&=(CInt const &_Other) {
    m_Lower &= _Other.m_Lower;
    return *this;
  }
  constexpr CInt operator-(CInt const &_Other) const { return CInt(m_Lower - _Other.m_Lower); }
};
int fg_GetLowestBitSetNoZero(CInt const &_Bits);
int fg_Step(CInt _Bits) {
  if (--_Bits != 0)
    return fg_GetLowestBitSetNoZero(_Bits);
  return -1;
}
int fg_Clear(CInt _Bits) {
  if ((_Bits &= _Bits - 1) != 0)
    return fg_GetLowestBitSetNoZero(_Bits);
  return -1;
}
int fg_StepAfter(CInt _Bits) {
  if (_Bits != 0)
    return fg_GetLowestBitSetNoZero(--_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
  return -1;
}
}

namespace NRound192 {
// A reference wrapper that keeps its pointer in a struct member, set in the
// body of its constructor, made directly or returned by a function.
template <typename t_CType>
class TCReference {
  struct CData {
    t_CType *m_pPointTo;
  };
  CData m_Data;

public:
  TCReference(t_CType &_Value) { this->m_Data.m_pPointTo = &_Value; }
  t_CType &f_Get() const { return *m_Data.m_pPointTo; }
};
template <typename t_CType>
TCReference<t_CType> fg_Reference(t_CType &_Value) {
  return TCReference<t_CType>(_Value);
}
int fg_GetLowestBitSetNoZero(umint _Bits);
int fg_ScanReturned(umint _Bits) {
  auto Bits = fg_Reference(_Bits);
  if (!_Bits)
    return 0;
  Bits.f_Get() = 0;
  return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
int fg_ScanMade(umint _Bits) {
  TCReference<umint> Bits(_Bits);
  if (!_Bits)
    return 0;
  Bits.f_Get() = 0;
  return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}

namespace NRound193 {
int fg_GetLowestBitSetNoZero(umint _Bits);
struct CInfo {
  umint m_Bits;
};
// A copy of a smart pointer points to what it points to; a copy of an
// optional holds its own value.
template <typename t_CType>
struct TCSharedPointer {
  struct CData {
    t_CType *m_pPointTo;
  };
  CData m_Data;
  t_CType *operator->() const { return m_Data.m_pPointTo; }
};
template <typename t_CType>
struct TCOptional {
  t_CType m_Value;
  t_CType const *operator->() const { return &m_Value; }
  t_CType *operator->() { return &m_Value; }
};
int fg_ScanShared(TCSharedPointer<CInfo> const &_pInfo) {
  auto pCopy = _pInfo;
  if (_pInfo->m_Bits) {
    pCopy->m_Bits = 0;
    return fg_GetLowestBitSetNoZero(_pInfo->m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
  }
  return -1;
}
int fg_ScanOptional(TCOptional<CInfo> const &_Info) {
  auto Copy = _Info;
  if (_Info->m_Bits) {
    Copy->m_Bits = 0;
    return fg_GetLowestBitSetNoZero(_Info->m_Bits);
  }
  return -1;
}
// An explicit copy of an integer class is what it copies.
struct CWide {
  umint m_Low;
  umint m_High;
  constexpr CWide(umint _Low = 0, umint _High = 0) : m_Low(_Low), m_High(_High) {}
  CWide(CWide const &) = default;
  constexpr explicit operator bool() const { return m_Low || m_High; }
};
int fg_GetLowestBitSetNoZero(CWide const &_Bits);
int fg_ScanCopy(CWide const &_Bits) {
  if (_Bits)
    return fg_GetLowestBitSetNoZero(CWide(_Bits));
  return -1;
}
int fg_ScanCast(CWide const &_Bits) {
  if (_Bits)
    return fg_GetLowestBitSetNoZero(static_cast<CWide>(_Bits));
  return -1;
}
}

namespace NRound194 {
int fg_GetLowestBitSetNoZero(umint _Bits);
struct CInfo {
  umint m_Bits;
};
// A copy a function returns, `fg_TempCopy(_pInfo)`, shares what a copy of a
// smart pointer does.
template <typename t_CType>
struct TCSharedPointer {
  t_CType *m_pObject;
  t_CType *operator->() const { return m_pObject; }
};
template <typename tf_CType>
tf_CType fg_TempCopy(tf_CType const &_Value) {
  return _Value;
}
int fg_ScanCopied(TCSharedPointer<CInfo> const &_pInfo) {
  if (!_pInfo->m_Bits)
    return -1;
  auto pCopy = fg_TempCopy(_pInfo);
  pCopy->m_Bits = 0;
  return fg_GetLowestBitSetNoZero(_pInfo->m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
// An integer class converted to an integer keeps the bits of the member the
// conversion gives.
struct CWide {
  umint m_Upper;
  umint m_Lower;
  constexpr CWide(umint _Lower = 0) : m_Upper(0), m_Lower(_Lower) {}
  constexpr CWide operator|(CWide const &_Other) const {
    CWide Result;
    Result.m_Upper = m_Upper | _Other.m_Upper;
    Result.m_Lower = m_Lower | _Other.m_Lower;
    return Result;
  }
  constexpr CWide operator<<(int _Shift) const;
  constexpr explicit operator umint() const { return m_Lower; }
};
int fg_ScanConverted(CWide _Bits) {
  return fg_GetLowestBitSetNoZero(umint(_Bits | CWide(1)));
}
int fg_ScanConvertedHigh(CWide _Bits) {
  return fg_GetLowestBitSetNoZero(umint(_Bits | (CWide(1) << 100)));
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}

namespace NRound195 {
// A conditional narrowed by the scan, of values that don't fit, can give zero;
// of values that fit, it can't.
void fg_Narrowed(unsigned short _Bits) {
  fg_GetLowestBitSetNoZero<unsigned char>(_Bits ? _Bits : 1);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
void fg_Fits(unsigned char _Bits) {
  fg_GetLowestBitSetNoZero<unsigned short>(_Bits ? _Bits : 1);
}
// An integer class of integer classes, converted through both.
struct CWord {
  umint m_Upper;
  umint m_Lower;
  constexpr CWord(umint _Lower = 0) : m_Upper(0), m_Lower(_Lower) {}
  constexpr explicit operator umint() const { return m_Lower; }
};
struct CWide {
  CWord m_Upper;
  CWord m_Lower;
  constexpr CWide(umint _Lower = 0) : m_Upper(0), m_Lower(_Lower) {}
  constexpr CWide operator|(CWide const &_Other) const;
  constexpr CWide operator<<(int _Shift) const;
  constexpr explicit operator umint() const { return umint(m_Lower); }
};
void fg_Nested(CWide const &_Bits) {
  fg_GetLowestBitSetNoZero(umint(_Bits | CWide(1)));
}
void fg_NestedHigh(CWide const &_Bits) {
  fg_GetLowestBitSetNoZero(umint(_Bits | (CWide(1) << 300)));
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}

namespace NRound196 {
int fg_GetLowestBitSetNoZero(umint const &_Bits);
// An element by the index operator of a vector is what a pointer from its
// array getter points to.
template <typename t_CData>
struct TCVector {
  t_CData *m_pData;
  t_CData &operator[](umint _Index) { return m_pData[_Index]; }
  t_CData *f_GetArray() { return m_pData; }
};
int fg_ThroughArray(TCVector<umint> &_Bits) {
  if (!_Bits[0])
    return -1;
  auto pBits = _Bits.f_GetArray();
  pBits[0] = 0;
  return fg_GetLowestBitSetNoZero(_Bits[0]);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
int fg_Unchanged(TCVector<umint> &_Bits) {
  if (!_Bits[0])
    return -1;
  return fg_GetLowestBitSetNoZero(_Bits[0]);
}
// The bits an integer class is known to have set, above the lowest 64 too.
using uint128 = unsigned __int128;
template <typename t_CInt>
int fg_GetHighestBitSetNoZero(t_CInt const &_Number);
template <typename t_CUpper, typename t_CLower>
struct TCInt {
  t_CLower m_Lower{};
  t_CUpper m_Upper{};
  constexpr TCInt(t_CLower _Lower) : m_Lower(_Lower) {}
  constexpr TCInt(t_CUpper _Upper, t_CLower _Lower) : m_Lower(_Lower), m_Upper(_Upper) {}
  template <typename t_CInt>
  constexpr explicit operator t_CInt() const { return t_CInt(m_Lower); }
  constexpr TCInt operator|(TCInt const &_Other) const { return {m_Upper | _Other.m_Upper, m_Lower | _Other.m_Lower}; }
};
using uint256 = TCInt<uint128, uint128>;
int fg_HighBit(uint256 const &_Bits) {
  return fg_GetHighestBitSetNoZero(uint128(_Bits | uint256(uint128(1) << 100)));
}
int fg_HighBitNarrowed(uint256 const &_Bits) {
  return fg_GetHighestBitSetNoZero(umint(_Bits | uint256(uint128(1) << 100)));
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}

namespace NRound197 {
int fg_GetLowestBitSetNoZero(umint const &_Bits);
struct CInfo {
  umint m_Bits;
  int m_Count;
};
// What a helper gives back of a smart pointer it is given, `return
// _pInfo.f_Get();`, is what it points to.
template <typename t_CType>
struct TCUniquePointer {
  struct CData {
    t_CType *m_pPointTo;
  } m_Data;
  t_CType *f_Get() const { return m_Data.m_pPointTo; }
  t_CType *operator->() const { return m_Data.m_pPointTo; }
};
CInfo *fg_GetInfo(TCUniquePointer<CInfo> const &_pInfo) { return _pInfo.f_Get(); }
int fg_ThroughHelper(TCUniquePointer<CInfo> const &_pInfo) {
  if (!_pInfo->m_Bits)
    return -1;
  auto pInfo = fg_GetInfo(_pInfo);
  pInfo->m_Bits = 0;
  return fg_GetLowestBitSetNoZero(_pInfo->m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
// Writing another element, or another member of the element, doesn't change
// the one checked.
template <typename t_CType>
struct TCVector {
  t_CType *m_pData;
  t_CType &operator[](umint _Index) { return m_pData[_Index]; }
};
int fg_OtherElement(TCVector<umint> &_Bits) {
  if (!_Bits[0])
    return -1;
  ++_Bits[1];
  return fg_GetLowestBitSetNoZero(_Bits[0]);
}
int fg_SameElement(TCVector<umint> &_Bits) {
  if (!_Bits[0])
    return -1;
  --_Bits[0];
  return fg_GetLowestBitSetNoZero(_Bits[0]);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
int fg_Whole(TCVector<umint> &_Bits, TCVector<umint> &_Other) {
  if (!_Bits[0])
    return -1;
  _Bits = _Other;
  return fg_GetLowestBitSetNoZero(_Bits[0]);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
int fg_OtherMember(TCVector<CInfo> &_Infos) {
  if (!_Infos[0].m_Bits)
    return -1;
  ++_Infos[0].m_Count;
  return fg_GetLowestBitSetNoZero(_Infos[0].m_Bits);
}
}

namespace NRound198 {
template <typename tf_CInt>
int fg_GetLowestBitSetNoZero(tf_CInt const &_Number);
template <typename t_FOnExitFunctor>
struct TCOnScopeExit {
  t_FOnExitFunctor mp_fOnExitFunctor;
  TCOnScopeExit(t_FOnExitFunctor &&_fOnExitFunctor)
      : mp_fOnExitFunctor(static_cast<t_FOnExitFunctor &&>(_fOnExitFunctor)) {}
  ~TCOnScopeExit() { mp_fOnExitFunctor(); }
};
struct COnScopeExitHelper {
  template <typename t_FOnExitFunctor>
  TCOnScopeExit<t_FOnExitFunctor> operator/(t_FOnExitFunctor &&_fOnExitFunctor) const {
    return TCOnScopeExit<t_FOnExitFunctor>(static_cast<t_FOnExitFunctor &&>(_fOnExitFunctor));
  }
};
extern COnScopeExitHelper const &g_OnScopeExit;
// A const copy checked itself.
int fg_Const(umint _Bits) {
  umint const Bits = _Bits;
  if (Bits == 0)
    return -1;
  return fg_GetLowestBitSetNoZero(Bits);
}
// A scope exit kept in a variable runs at the end of its scope, after the
// scan; one discarded runs at the end of its statement, before.
int fg_Guard(umint _Bits) {
  if (!_Bits)
    return -1;
  auto Cleanup = g_OnScopeExit / [&] { _Bits = 0; };
  return fg_GetLowestBitSetNoZero(_Bits);
}
int fg_Discarded(umint _Bits) {
  if (!_Bits)
    return -1;
  (void)(g_OnScopeExit / [&] { _Bits = 0; });
  return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}

namespace NRound199 {
int fg_GetLowestBitSetNoZero(umint const &_Bits);
// A vector whose index operator and array getter go through storage it
// points to, like `TCVector`.
template <typename t_CData>
struct TCVector {
  struct CStorage {
    t_CData *m_pData;
    t_CData *f_GetData() { return m_pData; }
  };
  CStorage *mp_pStorage;
  t_CData &operator[](umint _Index) { return mp_pStorage->f_GetData()[_Index]; }
  t_CData *f_GetArray() { return mp_pStorage ? mp_pStorage->f_GetData() : nullptr; }
};
// An element at an index that isn't a constant can be the one checked.
void fg_RuntimeIndex(TCVector<umint> &_Bits, umint _Index) {
  if (!_Bits[0])
    return;
  _Bits[_Index] = 0;
  fg_GetLowestBitSetNoZero(_Bits[0]);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
void fg_OtherConstant(TCVector<umint> &_Bits) {
  if (!_Bits[0])
    return;
  _Bits[1] = 0;
  fg_GetLowestBitSetNoZero(_Bits[0]);
}
// A reference bound to an element by an index that changes after.
void fg_IndexChanged(TCVector<umint> &_Bits, umint _Index) {
  auto &Bits = _Bits[_Index];
  _Index = 1;
  if (!_Bits[_Index])
    return;
  fg_GetLowestBitSetNoZero(Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
// What the array getter gives points to the elements.
void fg_GetArray(TCVector<umint> &_Bits) {
  if (!_Bits[0])
    return;
  auto *pBits = _Bits.f_GetArray();
  pBits[0] = 0;
  fg_GetLowestBitSetNoZero(_Bits[0]);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
void fg_GetArrayOther(TCVector<umint> &_Bits) {
  if (!_Bits[0])
    return;
  auto *pBits = _Bits.f_GetArray();
  pBits[1] = 0;
  fg_GetLowestBitSetNoZero(_Bits[0]);
}
}

namespace NRound200 {
int fg_GetLowestBitSetNoZero(umint const &_Bits);
void fg_RestoreFlags(umint _Flags);
void fg_ClearFlags(umint &_Flags);
// A scope guard, like `TCOnScopeExit`, that calls what it holds.
template <typename t_FCleanup>
struct TCOnScopeExit {
  t_FCleanup m_fCleanup;
  ~TCOnScopeExit() { m_fCleanup(); }
};
// Given a copy of what is checked, which it can't change, also made from
// it, but not what it captures by reference or a pointer to it.
int fg_CopyCaptured(umint _Bits) {
  if (_Bits == 0)
    return -1;
  {
    TCOnScopeExit Cleanup{[_Bits] { fg_RestoreFlags(_Bits); }};
  }
  {
    TCOnScopeExit Cleanup{[Bits = _Bits] { fg_RestoreFlags(Bits); }};
  }
  return fg_GetLowestBitSetNoZero(_Bits);
}
int fg_ReferenceCaptured(umint _Bits) {
  if (_Bits == 0)
    return -1;
  {
    TCOnScopeExit Cleanup{[&_Bits] { fg_ClearFlags(_Bits); }};
  }
  return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
int fg_PointerCaptured(umint _Bits) {
  if (_Bits == 0)
    return -1;
  {
    umint *pBits = &_Bits;
    TCOnScopeExit Cleanup{[pBits] { fg_ClearFlags(*pBits); }};
  }
  return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
// Also a temporary.
int fg_CopyCapturedTemporary(umint _Bits) {
  if (_Bits == 0 ||
      (TCOnScopeExit{[_Bits] { fg_RestoreFlags(_Bits); }}, false))
    return -1;
  return fg_GetLowestBitSetNoZero(_Bits);
}
int fg_CopyCapturedStatement(umint _Bits) {
  if (_Bits == 0)
    return -1;
  (void)TCOnScopeExit{[_Bits] { fg_RestoreFlags(_Bits); }};
  return fg_GetLowestBitSetNoZero(_Bits);
}
int fg_ReferenceCapturedStatement(umint _Bits) {
  if (_Bits == 0)
    return -1;
  (void)TCOnScopeExit{[&_Bits] { fg_ClearFlags(_Bits); }};
  return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}

namespace NRound201 {
int fg_GetLowestBitSetNoZero(umint const &_Bits);
// A copy of a smart pointer that is used only through `operator*`, whose
// `operator->` isn't instantiated, reading through a base that depends on the
// parameters, points to what it points to.
template <typename t_CType>
struct TCSharedPointerData {
  struct CData {
    t_CType *m_pPointTo;
  };
  CData m_Data;
};
template <typename t_CType>
struct TCSharedPointer : TCSharedPointerData<t_CType> {
  t_CType *operator->() const { return this->m_Data.m_pPointTo; }
  t_CType &operator*() const { return *this->m_Data.m_pPointTo; }
  explicit operator bool() const { return this->m_Data.m_pPointTo; }
};
int fg_ScanShared(TCSharedPointer<umint> const &_pBits) {
  if (!_pBits || !*_pBits)
    return -1;
  auto pCopy = _pBits;
  *pCopy = 0;
  return fg_GetLowestBitSetNoZero(*_pBits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
// An optional whose `operator*` that isn't const only gives access to its
// value, through a function that gives what a pointer it is given points to.
template <typename t_CType>
struct TCEvalReturn {
  static t_CType &fs_Value(t_CType *_pValue) { return *_pValue; }
};
template <typename t_CType>
struct TCOptional {
  alignas(t_CType) unsigned char mp_Storage[sizeof(t_CType)];
  bool mp_bValid = false;
  [[noreturn]] static void fp_ThrowEmpty();
  t_CType &fp_GetAs() {
    t_CType *pRet = (t_CType *)mp_Storage;
    return TCEvalReturn<t_CType>::fs_Value(pRet);
  }
  t_CType &operator*() {
    if (!mp_bValid)
      fp_ThrowEmpty();
    return fp_GetAs();
  }
  explicit operator bool() const { return mp_bValid; }
  void f_Clear() { mp_bValid = false; }
};
int fg_ScanAndClear(TCOptional<umint> &_Bits) {
  if (!_Bits || !*_Bits)
    return -1;
  int Bit = fg_GetLowestBitSetNoZero(*_Bits);
  _Bits.f_Clear();
  return Bit;
}
}

namespace NRound202 {
int fg_GetLowestBitSetNoZero(umint const &_Bits);
struct CEntry {
  umint m_Bits;
};
// A copy of an iterator whose `operator->` gives what a getter of it gives,
// a pointer member or null, points to what it points to.
template <typename t_CData>
struct TCVectorIterator {
  t_CData *m_pBegin;
  t_CData *m_pEnd;
  t_CData *fp_GetCurrent() const {
    if (m_pBegin != m_pEnd)
      return m_pBegin;
    else
      return nullptr;
  }
  t_CData *operator->() const { return fp_GetCurrent(); }
};
int fg_Scan(TCVectorIterator<CEntry> _iEntry) {
  if (!_iEntry->m_Bits)
    return -1;
  auto iCopy = _iEntry;
  iCopy->m_Bits = 0;
  return fg_GetLowestBitSetNoZero(_iEntry->m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}

namespace NRound203 {
int fg_GetLowestBitSetNoZero(umint const &_Bits);
struct CEntry {
  umint m_Bits;
};
// A reference a getter gives of what an index operator gives, written
// through.
template <typename t_CData>
struct TCVector {
  t_CData *m_pData;
  t_CData &operator[](umint _Index) { return m_pData[_Index]; }
  t_CData &f_GetFirst() { return (*this)[0]; }
};
int fg_ScanGetter(TCVector<CEntry> &_Entries) {
  if (!_Entries[0].m_Bits)
    return -1;
  auto &Entry = _Entries.f_GetFirst();
  Entry.m_Bits = 0;
  return fg_GetLowestBitSetNoZero(_Entries[0].m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
// A function given by name to what calls it, which writes what is read.
umint g_Bits;
void fg_Clear() { g_Bits = 0; }
void fg_Keep() {}
template <typename tf_FWork>
void fg_Invoke(tf_FWork &&_fWork) {
  _fWork();
}
void fg_InvokePointer(void (*_fWork)()) { _fWork(); }
int fg_ScanNamed() {
  if (!g_Bits)
    return -1;
  fg_Invoke(fg_Clear);
  return fg_GetLowestBitSetNoZero(g_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
int fg_ScanPointer() {
  if (!g_Bits)
    return -1;
  fg_InvokePointer(&fg_Clear);
  return fg_GetLowestBitSetNoZero(g_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
int fg_ScanKept() {
  if (!g_Bits)
    return -1;
  fg_Invoke(fg_Keep);
  return fg_GetLowestBitSetNoZero(g_Bits);
}
}

namespace NRound204 {
int fg_GetLowestBitSetNoZero(umint const &_Bits);
// A scope guard that calls what it holds unless it is cleared.
template <typename t_FCleanup>
struct TCOnScopeExit {
  t_FCleanup m_fCleanup;
  bool m_bActive = true;
  ~TCOnScopeExit() {
    if (m_bActive)
      m_fCleanup();
  }
  void f_Clear() { m_bActive = false; }
  void f_ClearFunctor() { m_bActive = false; }
};
template <typename tf_FCleanup>
TCOnScopeExit<tf_FCleanup> fg_OnScopeExit(tf_FCleanup &&_fCleanup) {
  return {static_cast<tf_FCleanup &&>(_fCleanup)};
}
// A helper whose guard calls what it is given when its scope ends.
template <typename tf_FCleanup>
void fg_RunWithCleanup(tf_FCleanup &&_fCleanup) {
  auto Cleanup = fg_OnScopeExit(static_cast<tf_FCleanup &&>(_fCleanup));
}
int fg_ScanHelperGuard(umint _Bits) {
  if (!_Bits)
    return -1;
  fg_RunWithCleanup([&] { _Bits = 0; });
  return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
// One cleared before its scope ends doesn't call it, but not one a jump can
// leave the scope past the clear of.
int fg_ScanCancelled(umint _Bits) {
  if (!_Bits)
    return -1;
  {
    auto Cleanup = fg_OnScopeExit([&] { _Bits = 0; });
    Cleanup.f_Clear();
  }
  return fg_GetLowestBitSetNoZero(_Bits);
}
// Also by what else of it the options name as removing what it holds.
int fg_ScanCancelledFunctor(umint _Bits) {
  if (!_Bits)
    return -1;
  {
    auto Cleanup = fg_OnScopeExit([&] { _Bits = 0; });
    Cleanup.f_ClearFunctor();
  }
  return fg_GetLowestBitSetNoZero(_Bits);
}
int fg_ScanSkipped(umint _Bits, bool _bSkip) {
  if (!_Bits)
    return -1;
  for (int i = 0; i < 1; ++i) {
    auto Cleanup = fg_OnScopeExit([&] { _Bits = 0; });
    if (_bSkip)
      break;
    Cleanup.f_Clear();
  }
  return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}

namespace NRound205 {
int fg_GetLowestBitSetNoZero(umint const &_Bits);
void fg_Work();
template <typename t_FCleanup>
struct TCOnScopeExit {
  t_FCleanup m_fCleanup;
  bool m_bActive = true;
  TCOnScopeExit(t_FCleanup _fCleanup) : m_fCleanup(_fCleanup) {}
  TCOnScopeExit(TCOnScopeExit &&_Other) : m_fCleanup(_Other.m_fCleanup) {
    _Other.m_bActive = false;
  }
  ~TCOnScopeExit() {
    if (m_bActive)
      m_fCleanup();
  }
  void f_Clear() { m_bActive = false; }
};
// In a try, what can throw before the clear skips it.
int fg_ScanThrow(umint _Bits) {
  if (!_Bits)
    return -1;
  try {
    TCOnScopeExit Cleanup{[&] { _Bits = 0; }};
    fg_Work();
    Cleanup.f_Clear();
  } catch (...) {
  }
  return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
// One moved into another that is cleared.
int fg_ScanMoved(umint _Bits) {
  if (!_Bits)
    return -1;
  {
    TCOnScopeExit Cleanup{[&] { _Bits = 0; }};
    auto Moved = static_cast<decltype(Cleanup) &&>(Cleanup);
    Moved.f_Clear();
  }
  return fg_GetLowestBitSetNoZero(_Bits);
}
}

namespace NRound206 {
int fg_GetLowestBitSetNoZero(umint const &_Bits);
template <typename t_FCleanup>
struct TCOnScopeExit {
  t_FCleanup m_fCleanup;
  ~TCOnScopeExit() { m_fCleanup(); }
};
// A view given to a helper whose guard writes through it when its scope
// ends.
struct CEntry {
  umint &m_Value;
};
void fg_Reset(CEntry _Entry) {
  TCOnScopeExit Cleanup{[&] { _Entry.m_Value = 0; }};
}
void fg_ScanView(CEntry _Entry) {
  if (!_Entry.m_Value)
    return;
  fg_Reset(_Entry);
  fg_GetLowestBitSetNoZero(_Entry.m_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
// A copy of a smart pointer pointed elsewhere doesn't change what it pointed
// to.
template <typename t_CType>
struct TCSharedPointer {
  t_CType *m_pObject;
  TCSharedPointer(TCSharedPointer const &);
  TCSharedPointer &operator=(decltype(nullptr));
  ~TCSharedPointer();
  void f_Clear();
  t_CType *operator->() const { return m_pObject; }
  explicit operator bool() const { return m_pObject; }
};
struct CInfo {
  umint m_Bits;
};
void fg_ScanShared(TCSharedPointer<CInfo> const &_pInfo) {
  auto pCopy = _pInfo;
  if (!_pInfo || !_pInfo->m_Bits)
    return;
  pCopy = nullptr;
  pCopy.f_Clear();
  fg_GetLowestBitSetNoZero(_pInfo->m_Bits);
}
// An integer class widened keeps its bits; narrowed it doesn't.
template <unsigned t_nBits>
struct TCInt {
  umint m_Words[t_nBits / 64];
  TCInt(umint _Value);
  template <unsigned t_nOtherBits>
  TCInt(TCInt<t_nOtherBits> const &_Other);
  explicit operator bool() const;
};
int fg_GetLowestBitSetNoZero(TCInt<256> const &_Bits);
int fg_GetLowestBitSetNoZero(TCInt<128> const &_Bits);
void fg_ScanWidened(TCInt<128> _Bits, umint _Small) {
  if (!_Bits || !_Small)
    return;
  fg_GetLowestBitSetNoZero(TCInt<256>(_Bits));
  fg_GetLowestBitSetNoZero(TCInt<256>(_Small));
}
void fg_ScanNarrowed(TCInt<256> _Bits) {
  if (!_Bits)
    return;
  fg_GetLowestBitSetNoZero(TCInt<128>(_Bits));
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}

namespace NRound207 {
int fg_GetLowestBitSetNoZero(umint const &_Bits);
// Through an iterator a container gives at its first element, also one that
// converts to a pointer to it.
template <typename t_CData>
struct TCIterator {
  t_CData *m_pCurrent;
  operator t_CData *() const { return m_pCurrent; }
};
template <typename t_CData>
struct TCVector {
  t_CData *m_pData;
  t_CData &operator[](umint _Index) { return m_pData[_Index]; }
  TCIterator<t_CData> f_GetIterator() { return {m_pData}; }
};
void fg_ScanIterator(TCVector<umint> &_Values) {
  if (_Values[0]) {
    auto iValue = _Values.f_GetIterator();
    *iValue = 0;
    fg_GetLowestBitSetNoZero(_Values[0]);
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
  }
}
// A lambda called through a copy a helper gives back.
template <typename t_CType>
t_CType fg_TempCopy(t_CType const &_Value) {
  return _Value;
}
void fg_ScanCopied(umint _Bits) {
  if (!_Bits)
    return;
  auto fClear = [&] { _Bits = 0; };
  auto fCopy = fg_TempCopy(fClear);
  fCopy();
  fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}

namespace NRound208 {
int fg_GetLowestBitSetNoZero(umint const &_Bits);
// Through an iterator advanced from the first element before it is used.
template <typename t_CData>
struct TCIterator {
  t_CData *m_pCurrent;
  t_CData &operator*() const { return *m_pCurrent; }
  TCIterator &operator++() {
    ++m_pCurrent;
    return *this;
  }
  TCIterator &operator+=(umint _Step) {
    m_pCurrent += _Step;
    return *this;
  }
};
template <typename t_CData>
struct TCVector {
  t_CData *m_pData;
  t_CData &operator[](umint _Index) { return m_pData[_Index]; }
  TCIterator<t_CData> f_GetIterator() { return {m_pData}; }
};
void fg_ScanAdvanced(TCVector<umint> &_Values) {
  if (!_Values[1])
    return;
  auto iValue = _Values.f_GetIterator();
  ++iValue;
  *iValue = 0;
  fg_GetLowestBitSetNoZero(_Values[1]);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
void fg_ScanOther(TCVector<umint> &_Values) {
  if (!_Values[0])
    return;
  auto iValue = _Values.f_GetIterator();
  ++iValue;
  *iValue = 0;
  fg_GetLowestBitSetNoZero(_Values[0]);
}
void fg_ScanStepped(TCVector<umint> &_Values) {
  if (!_Values[2])
    return;
  auto iValue = _Values.f_GetIterator();
  iValue += 2;
  *iValue = 0;
  fg_GetLowestBitSetNoZero(_Values[2]);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
// Moved where it isn't known how far, at any element.
void fg_ScanUnknown(TCVector<umint> &_Values, bool _bNext) {
  if (!_Values[1])
    return;
  auto iValue = _Values.f_GetIterator();
  if (_bNext)
    ++iValue;
  *iValue = 0;
  fg_GetLowestBitSetNoZero(_Values[1]);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

// An integer widened to an integer class by a constructor with a parameter
// left to its default, like that of `TCInt`.
struct CDummy {
  static CDummy ms_Dummy;
};
template <bool t_bEnable, typename t_CType>
struct TCEnableIfType {};
template <typename t_CType>
struct TCEnableIfType<true, t_CType> {
  using CType = t_CType;
};
template <typename t_CUpper, typename t_CLower>
struct TCInt {
  t_CUpper m_Upper;
  t_CLower m_Lower;
  template <typename t_CType>
  constexpr TCInt(t_CType const &_Convert,
                  typename TCEnableIfType<__is_fundamental(t_CType), CDummy &>::CType
                      _pDummy = CDummy::ms_Dummy)
      : m_Upper(0), m_Lower(_Convert) {}
};
using uint256 = TCInt<unsigned __int128, unsigned __int128>;
int fg_GetLowestBitSetNoZero(uint256 const &_Bits);
int fg_Widen(unsigned long long _Bits) {
  if (_Bits == 0)
    return -1;
  return fg_GetLowestBitSetNoZero(uint256(_Bits));
}
int fg_WidenUnchecked(unsigned long long _Bits) {
  return fg_GetLowestBitSetNoZero(uint256(_Bits));
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}

namespace NRound210 {
int fg_GetLowestBitSetNoZero(umint _Bits);
// A const member function that gives a pointer member on to what changes what
// it points to.
void fg_Clear(umint *_pBits) { *_pBits = 0; }
void fg_Read(umint const *_pBits);
struct CBitView {
  umint *m_pBits;
  void f_Clear() const { fg_Clear(m_pBits); }
  void f_Read() const { fg_Read(m_pBits); }
};
int fg_Scan(CBitView const &_View) {
  if (!*_View.m_pBits)
    return -1;
  _View.f_Clear();
  return fg_GetLowestBitSetNoZero(*_View.m_pBits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
int fg_ScanRead(CBitView const &_View) {
  if (!*_View.m_pBits)
    return -1;
  _View.f_Read();
  return fg_GetLowestBitSetNoZero(*_View.m_pBits);
}
}

namespace NRound211 {
int fg_GetLowestBitSetNoZero(umint const &_Bits);
// Written through a structured binding of a copy whose member is a reference.
struct CBorrowedBits {
  umint &m_Bits;
};
int fg_Scan(umint &_Bits) {
  auto [Bits] = CBorrowedBits{_Bits};
  if (!_Bits)
    return -1;
  Bits = 0;
  return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
// Also a copy initialized with what it is made of.
int fg_ScanCopy(umint &_Bits) {
  CBorrowedBits Copy = CBorrowedBits{_Bits};
  if (!_Bits)
    return -1;
  Copy.m_Bits = 0;
  return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
// Not one whose member is its own.
struct COwnBits {
  umint m_Bits;
};
int fg_ScanOwn(umint &_Bits) {
  auto [Bits] = COwnBits{_Bits};
  if (!_Bits)
    return -1;
  Bits = 0;
  return fg_GetLowestBitSetNoZero(_Bits);
}
}

namespace NRound212 {
int fg_GetLowestBitSetNoZero(umint const &_Bits);
template <typename t_FCleanup>
struct TCOnScopeExit {
  t_FCleanup m_fCleanup;
  bool m_bActive = true;
  TCOnScopeExit(t_FCleanup _fCleanup) : m_fCleanup(_fCleanup) {}
  ~TCOnScopeExit() {
    if (m_bActive)
      m_fCleanup();
  }
  void f_Clear() { m_bActive = false; }
};
struct CValues {
  CValues(int _nValues);
  ~CValues();
};
// In a try, a constructor that can throw before the clear skips it.
int fg_ScanConstruct(umint _Bits) {
  if (!_Bits)
    return -1;
  try {
    TCOnScopeExit Cleanup{[&] { _Bits = 0; }};
    CValues Values(16);
    Cleanup.f_Clear();
  } catch (...) {
  }
  return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
// Not one that can't throw.
struct CPlain {
  CPlain(int _nValues) noexcept;
};
int fg_ScanNothrow(umint _Bits) {
  if (!_Bits)
    return -1;
  try {
    TCOnScopeExit Cleanup{[&] { _Bits = 0; }};
    CPlain Values(16);
    Cleanup.f_Clear();
  } catch (...) {
  }
  return fg_GetLowestBitSetNoZero(_Bits);
}
}

namespace NRound213 {
// The value an integer class had before its postfix decrement.
template <typename t_CUpper, typename t_CLower>
struct TCInt {
  t_CLower m_Lower{};
  t_CUpper m_Upper{};
  constexpr explicit operator bool() const {
    return m_Upper != 0 || m_Lower != 0;
  }
  constexpr TCInt &operator--() {
    if (!m_Lower--)
      --m_Upper;
    return *this;
  }
  constexpr TCInt operator--(int) {
    TCInt Ret = *this;
    --*this;
    return Ret;
  }
};
using uint256 = TCInt<unsigned __int128, unsigned __int128>;
int fg_GetLowestBitSetNoZero(uint256 const &_Bits);
void fg_Scan(uint256 _Bits) {
  if (_Bits)
    fg_GetLowestBitSetNoZero(_Bits--);
}
void fg_ScanAfter(uint256 _Bits) {
  if (_Bits)
    fg_GetLowestBitSetNoZero(--_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}

namespace NRound214 {
int fg_GetLowestBitSetNoZero(umint _Bits);
// Written through what a function gives a reference to.
static umint gs_Bits = 1;
umint &fg_Bits() { return gs_Bits; }
umint &fg_Ref(umint &_Bits) { return _Bits; }
int fg_Scan() {
  if (gs_Bits == 0)
    return -1;
  fg_Bits() = 0;
  return fg_GetLowestBitSetNoZero(gs_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
int fg_ScanGiven(umint _Bits) {
  if (!_Bits)
    return -1;
  fg_Ref(_Bits) = 0;
  return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}

namespace NRound215 {
int fg_GetLowestBitSetNoZero(umint _Bits);
template <typename t_CData>
struct TCVector {
  t_CData *m_pData;
  umint m_Len;
  t_CData &operator[](umint _Index) { return m_pData[_Index]; }
  t_CData *begin() { return m_pData; }
  t_CData *end() { return m_pData + m_Len; }
};
// The reference of a loop over a container is changed by a write of the
// container.
int fg_Scan(TCVector<umint> &_Words) {
  for (umint &Word : _Words) {
    if (!Word)
      continue;
    _Words[0] = 0;
    return fg_GetLowestBitSetNoZero(Word);
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
  }
  return -1;
}
// A const copy of an integer class has what it was copied from.
struct CInt {
  umint m_Lower;
  umint m_Upper;
  explicit operator bool() const { return m_Lower || m_Upper; }
};
int fg_GetLowestBitSetNoZero(CInt const &_Bits);
int fg_ScanCopy(CInt _Bits) {
  if (!_Bits)
    return -1;
  const CInt Bits = _Bits;
  return fg_GetLowestBitSetNoZero(Bits);
}
}

namespace NRound216 {
int fg_GetLowestBitSetNoZero(umint const &_Bits);
// A const copy of an integer class whose member is written after it.
struct CInt {
  umint m_Lower;
  umint m_Upper;
  explicit operator bool() const { return m_Lower || m_Upper; }
};
int fg_GetLowestBitSetNoZero(CInt const &_Bits);
void fg_ScanSnapshot(CInt _Bits) {
  CInt const Bits = _Bits;
  _Bits.m_Lower = 1;
  if (_Bits)
    fg_GetLowestBitSetNoZero(Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
// Cleared through what an owning pointer destroys.
struct CClear {
  umint &m_Bits;
  CClear(umint &_Bits) : m_Bits(_Bits) {}
  ~CClear() { m_Bits = 0; }
};
template <typename t_CType>
struct TCUniquePointer {
  t_CType *m_pData;
  template <typename tf_CProxy> TCUniquePointer(tf_CProxy &&_Proxy);
  ~TCUniquePointer() { delete m_pData; }
  void f_Clear() {
    delete m_pData;
    m_pData = nullptr;
  }
};
struct CConstructProxy {
  umint &m_Bits;
};
CConstructProxy fg_Construct(umint &_Bits);
void fg_ScanOwned(umint _Bits) {
  TCUniquePointer<CClear> pClear = fg_Construct(_Bits);
  if (_Bits) {
    pClear.f_Clear();
    fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
  }
}
}

namespace NRound217 {
int fg_GetLowestBitSetNoZero(umint _Bits);
struct CInfo {
  umint m_Bits;
};
template <typename t_CType>
struct TCSharedPointer {
  struct CData {
    t_CType *m_pPointTo;
  };
  CData m_Data;
  t_CType *operator->() const { return m_Data.m_pPointTo; }
  t_CType &operator*() const { return *m_Data.m_pPointTo; }
  explicit operator bool() const { return m_Data.m_pPointTo; }
};
// A const member function writes through what a smart pointer member points
// to, whose const operators give access to it.
struct CBits {
  TCSharedPointer<CInfo> m_pInfo;
  void f_Clear() const { m_pInfo->m_Bits = 0; }
  void f_ClearStar() const { (*m_pInfo).m_Bits = 0; }
  void f_Read() const { umint Bits = m_pInfo->m_Bits; (void)Bits; }
  int f_Scan() const {
    if (!m_pInfo || !m_pInfo->m_Bits)
      return -1;
    f_Clear();
    return fg_GetLowestBitSetNoZero(m_pInfo->m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
  }
  int f_ScanStar() const {
    if (!m_pInfo || !m_pInfo->m_Bits)
      return -1;
    f_ClearStar();
    return fg_GetLowestBitSetNoZero(m_pInfo->m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
  }
  int f_ScanRead() const {
    if (!m_pInfo || !m_pInfo->m_Bits)
      return -1;
    f_Read();
    return fg_GetLowestBitSetNoZero(m_pInfo->m_Bits);
  }
};
// Writing through what a copy was made from changes what the copy points to.
int fg_ScanCopy(TCSharedPointer<CInfo> const &_pInfo) {
  auto pCopy = _pInfo;
  if (!pCopy || !pCopy->m_Bits)
    return -1;
  _pInfo->m_Bits = 0;
  return fg_GetLowestBitSetNoZero(pCopy->m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
// An optional's copy has its own value.
template <typename t_CType>
struct TCOptional {
  t_CType m_Value;
  t_CType const *operator->() const { return &m_Value; }
  t_CType *operator->() { return &m_Value; }
};
int fg_ScanOptionalCopy(TCOptional<CInfo> &_Info) {
  auto Copy = _Info;
  if (!Copy->m_Bits)
    return -1;
  _Info->m_Bits = 0;
  return fg_GetLowestBitSetNoZero(Copy->m_Bits);
}
}

namespace NRound218 {
int fg_GetLowestBitSetNoZero(umint const &_Bits);
// A const call operator writes through what it refers to.
struct CClear {
  umint &m_Bits;
  void operator()() const { m_Bits = 0; }
};
struct CObserve {
  umint &m_Bits;
  umint operator()() const { return m_Bits; }
};
int fg_ScanCleared(umint &_Bits) {
  CClear Clear{_Bits};
  if (!_Bits)
    return -1;
  Clear();
  return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
int fg_ScanObserved(umint &_Bits) {
  CObserve Observe{_Bits};
  if (!_Bits)
    return -1;
  Observe();
  return fg_GetLowestBitSetNoZero(_Bits);
}
// A scope exit that calls what it is given when it ends changes only what
// the body of its lambda changes.
template <typename t_FFunction>
struct TCOnScopeExit {
  t_FFunction m_fFunction;
  ~TCOnScopeExit() { m_fFunction(); }
};
struct COnScopeExitHelper {
  template <typename t_FFunction>
  TCOnScopeExit<t_FFunction> operator/(t_FFunction &&_fFunction) const {
    return {static_cast<t_FFunction &&>(_fFunction)};
  }
};
extern COnScopeExitHelper const &g_OnScopeExit;
void fg_Observe(umint _Bits);
void fg_Clear(umint &_Bits);
int fg_ScanObservedAtExit(umint _Bits) {
  if (!_Bits)
    return -1;
  {
    auto Cleanup = g_OnScopeExit / [&] { fg_Observe(_Bits); };
  }
  return fg_GetLowestBitSetNoZero(_Bits);
}
int fg_ScanClearedAtExit(umint _Bits) {
  if (!_Bits)
    return -1;
  {
    auto Cleanup = g_OnScopeExit / [&] { _Bits = 0; };
  }
  return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
int fg_ScanClearedByHelperAtExit(umint _Bits) {
  if (!_Bits)
    return -1;
  {
    auto Cleanup = g_OnScopeExit / [&] { fg_Clear(_Bits); };
  }
  return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}

namespace NRound219 {
int fg_GetLowestBitSetNoZero(umint _Bits);
// Each lock of a weak pointer gives what the others do.
template <typename t_CType>
struct TCSharedPointer {
  struct CData {
    t_CType *m_pPointTo;
  };
  CData m_Data;
  t_CType *operator->() const { return m_Data.m_pPointTo; }
  t_CType &operator*() const { return *m_Data.m_pPointTo; }
  explicit operator bool() const { return m_Data.m_pPointTo; }
};
template <typename t_CType>
struct TCWeakPointer {
  t_CType *m_pObject;
  TCSharedPointer<t_CType> f_Lock() const;
};
int fg_ScanLocked(TCWeakPointer<umint> const &_pWeak) {
  auto pBits = _pWeak.f_Lock();
  if (!pBits || !*pBits)
    return -1;
  if (auto pOther = _pWeak.f_Lock())
    *pOther = 0;
  return fg_GetLowestBitSetNoZero(*pBits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
int fg_ScanLockedOther(TCWeakPointer<umint> const &_pWeak,
                       TCWeakPointer<umint> const &_pOtherWeak) {
  auto pBits = _pWeak.f_Lock();
  if (!pBits || !*pBits)
    return -1;
  umint Bits = *_pOtherWeak.f_Lock();
  (void)Bits;
  return fg_GetLowestBitSetNoZero(*pBits);
}
// A temporary copy with its own storage is changed by itself.
template <typename t_CData>
struct TCVector {
  t_CData *m_pData;
  TCVector(TCVector const &_Other);
  t_CData &operator[](int _Index) const { return m_pData[_Index]; }
  int f_GetLen() const;
};
template <typename tf_CType>
tf_CType fg_TempCopy(tf_CType const &_Value) {
  return _Value;
}
void fg_Consume(TCVector<umint> &&_Bits) { _Bits[0] = 0; }
int fg_ScanCopied(TCVector<umint> const &_Bits) {
  if (_Bits.f_GetLen() == 0 || !_Bits[0])
    return -1;
  fg_Consume(fg_TempCopy(_Bits));
  return fg_GetLowestBitSetNoZero(_Bits[0]);
}
// Not one that shares it, a view that its copies copy the pointer of.
struct CView {
  umint *m_pBits;
};
void fg_ConsumeView(CView &&_View) { *_View.m_pBits = 0; }
int fg_ScanViewed(umint &_Bits) {
  if (!_Bits)
    return -1;
  fg_ConsumeView(CView{&_Bits});
  return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}

namespace NRound220 {
int fg_GetLowestBitSetNoZero(umint _Bits);
struct CInfo {
  umint m_Bits;
};
template <typename t_CType>
struct TCSharedPointer {
  struct CData {
    t_CType *m_pPointTo;
  };
  CData m_Data;
  t_CType *operator->() const { return m_Data.m_pPointTo; }
  t_CType &operator*() const { return *m_Data.m_pPointTo; }
  TCSharedPointer &operator=(TCSharedPointer const &_Other);
};
// Pointing a smart pointer elsewhere changes what it was checked through.
int fg_Replaced(TCSharedPointer<CInfo> &_pInfo,
                TCSharedPointer<CInfo> const &_pOther) {
  if (!_pInfo->m_Bits)
    return -1;
  _pInfo = _pOther;
  return fg_GetLowestBitSetNoZero(_pInfo->m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
int fg_ReplacedStar(TCSharedPointer<umint> &_pBits,
                    TCSharedPointer<umint> const &_pOther) {
  if (!*_pBits)
    return -1;
  _pBits = _pOther;
  return fg_GetLowestBitSetNoZero(*_pBits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
// Writing what it points to doesn't.
int fg_Kept(TCSharedPointer<CInfo> &_pInfo, umint &_Other) {
  if (!_pInfo->m_Bits)
    return -1;
  _Other = 0;
  return fg_GetLowestBitSetNoZero(_pInfo->m_Bits);
}
// A sign of an integer class is zero only when what it is of is.
template <typename t_CLower, typename t_CUpper>
struct TCInt {
  t_CLower m_Lower{};
  t_CUpper m_Upper{};
  constexpr TCInt operator-() const;
  constexpr TCInt operator+() const;
  constexpr explicit operator bool() const;
};
using uint256 = TCInt<unsigned __int128, unsigned __int128>;
int fg_GetLowestBitSetNoZero(uint256 const &_Bits);
int fg_Negated(uint256 const &_Bits) {
  if (-_Bits)
    return fg_GetLowestBitSetNoZero(_Bits);
  return -1;
}
int fg_Plus(uint256 const &_Bits) {
  if (+_Bits)
    return fg_GetLowestBitSetNoZero(_Bits);
  return -1;
}
}

namespace NRound221 {
int fg_GetLowestBitSetNoZero(umint _Bits);
template <typename t_CType>
struct TCSharedPointer {
  struct CData {
    t_CType *m_pPointTo;
  };
  CData m_Data;
  t_CType *operator->() const { return m_Data.m_pPointTo; }
  t_CType &operator*() const { return *m_Data.m_pPointTo; }
  explicit operator bool() const { return m_Data.m_pPointTo; }
};
template <typename t_CType>
struct TCWeakPointer {
  t_CType *m_pObject;
  TCSharedPointer<t_CType> f_Lock() const;
};
// A copy made through a local reference shares what it is bound to has.
void fg_ThroughReference(TCSharedPointer<umint> const &_pBits) {
  if (!*_pBits)
    return;
  TCSharedPointer<umint> const &pAlias = _pBits;
  auto pCopy = pAlias;
  *pCopy = 0;
  fg_GetLowestBitSetNoZero(*_pBits);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
// A lock of a copy of a weak pointer gives what a lock of it gives.
void fg_LockedCopy(TCWeakPointer<umint> const &_pWeak) {
  auto pBits = _pWeak.f_Lock();
  if (!pBits || !*pBits)
    return;
  auto pWeakCopy = _pWeak;
  auto pCopy = pWeakCopy.f_Lock();
  *pCopy = 0;
  fg_GetLowestBitSetNoZero(*pBits);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}

namespace NRound222 {
int fg_GetLowestBitSetNoZero(umint _Bits);
// Copies of an iterator that gives the address of what its node pointer
// points to share it.
struct CEntry {
  umint m_Bits;
};
template <typename t_CData>
struct TCLinkedList {
  struct CMember {
    t_CData m_Object;
  };
  struct CIterator {
    CMember *m_Iter;
    t_CData *f_GetCurrent() const {
      CMember *pMember = m_Iter;
      if (pMember)
        return &pMember->m_Object;
      return nullptr;
    }
    t_CData *operator->() const { return f_GetCurrent(); }
  };
};
int fg_ScanCopiedIterator(TCLinkedList<CEntry>::CIterator _iEntry) {
  auto iCopy = _iEntry;
  if (!_iEntry->m_Bits)
    return -1;
  iCopy->m_Bits = 0;
  return fg_GetLowestBitSetNoZero(_iEntry->m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
// An element written through what a getter gives is that element.
template <typename t_CData>
struct TCVector {
  t_CData *m_pData;
  t_CData &operator[](umint _Index) { return m_pData[_Index]; }
  t_CData *f_GetArray() { return m_pData; }
};
int fg_ScanOtherElement(TCVector<umint> &_Bits) {
  if (!_Bits[0])
    return -1;
  _Bits.f_GetArray()[1] = 0;
  return fg_GetLowestBitSetNoZero(_Bits[0]);
}
int fg_ScanSameElement(TCVector<umint> &_Bits) {
  if (!_Bits[0])
    return -1;
  _Bits.f_GetArray()[0] = 0;
  return fg_GetLowestBitSetNoZero(_Bits[0]);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
// What the frame holds by value stays across a suspension, also of an
// integer class through its operators, and an element of an array of its
// own.
using uint128 = unsigned __int128;
template <typename T>
int fg_GetLowestBitSetNoZero(T const &);
template <typename t_CUpper, typename t_CLower>
struct TCInt {
  t_CLower m_Lower{};
  t_CUpper m_Upper{};
  constexpr TCInt(t_CUpper _Upper, t_CLower _Lower)
      : m_Lower(_Lower), m_Upper(_Upper) {}
  constexpr TCInt operator~() const { return {~m_Upper, ~m_Lower}; }
  constexpr bool operator==(TCInt const &_Other) const {
    return m_Lower == _Other.m_Lower && m_Upper == _Other.m_Upper;
  }
};
using uint256 = TCInt<uint128, uint128>;
CRound82Task fg_ScanComplement(uint256 _Bits) {
  constexpr uint256 c_All(~uint128(0), ~uint128(0));
  if (_Bits == c_All)
    co_return;
  co_await fg_Round82Work(0);
  fg_GetLowestBitSetNoZero(~_Bits);
}
CRound82Task fg_ScanLocalElement(umint _Value) {
  umint Values[2] = {_Value, 0};
  if (!Values[0])
    co_return;
  co_await fg_Round82Work(0);
  fg_GetLowestBitSetNoZero(Values[0]);
}
umint g_Values[2];
CRound82Task fg_ScanGlobalElement() {
  if (!g_Values[0])
    co_return;
  co_await fg_Round82Work(0);
  fg_GetLowestBitSetNoZero(g_Values[0]);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}

namespace NRound223 {
int fg_GetLowestBitSetNoZero(umint _Bits);
template <typename t_CData>
struct TCVector {
  t_CData *m_pData;
  struct CIterator {
    t_CData *m_pData;
    explicit operator bool() const { return m_pData; }
    t_CData &operator*() const { return *m_pData; }
  };
  TCVector(TCVector const &_Other);
  TCVector(TCVector &_Other) : TCVector(fg_Const(_Other)) {}
  static TCVector const &fg_Const(TCVector const &_Value) { return _Value; }
  t_CData &operator[](umint _Index) { return m_pData[_Index]; }
  umint f_GetLen() const;
  CIterator f_GetIterator();
};
// Writing the element an iterator is at through the container changes what
// the iterator reads.
int fg_ScanThroughIterator(TCVector<umint> &_Values) {
  auto iValue = _Values.f_GetIterator();
  if (!iValue || !*iValue)
    return -1;
  _Values[0] = 0;
  return fg_GetLowestBitSetNoZero(*iValue);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
// Copying a container through a constructor that only reads it changes
// nothing.
int fg_ScanCopied(TCVector<umint> &_Values) {
  if (!_Values.f_GetLen() || !_Values[0])
    return -1;
  auto Copy = _Values;
  return fg_GetLowestBitSetNoZero(_Values[0]);
}
}

namespace NRound224 {
// Widening a value that is nonzero by its construction keeps it nonzero.
template <typename t_CUpper, typename t_CLower>
struct TCInt {
  t_CLower m_Lower{};
  t_CUpper m_Upper{};
  constexpr TCInt(t_CLower _Value = 0, int _Dummy = 0) : m_Lower(_Value) {}
};
using uint256 = TCInt<unsigned __int128, unsigned __int128>;
int fg_GetLowestBitSetNoZero(uint256 const &_Bits);
int fg_Widened(umint _Bits) {
  return fg_GetLowestBitSetNoZero(uint256(_Bits | 1u));
}
int fg_WidenedAny(umint _Bits) {
  return fg_GetLowestBitSetNoZero(uint256(_Bits));
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}

namespace NRound225 {
// A const local of an integer class made with a bit set keeps it, also
// through a conversion to its low word.
template <typename t_CUpper, typename t_CLower>
struct TCInt {
  t_CLower m_Lower{};
  t_CUpper m_Upper{};
  constexpr TCInt(t_CLower _Value = 0, int _Dummy = 0) : m_Lower(_Value) {}
  constexpr TCInt operator|(TCInt const &_Other) const {
    TCInt Result;
    Result.m_Lower = m_Lower | _Other.m_Lower;
    Result.m_Upper = m_Upper | _Other.m_Upper;
    return Result;
  }
  constexpr explicit operator umint() const { return umint(m_Lower); }
};
using uint256 = TCInt<unsigned __int128, unsigned __int128>;
int fg_GetLowestBitSetNoZero(umint _Bits);
int fg_ScanLowWord(uint256 _Bits) {
  uint256 const Bits = _Bits | uint256(1);
  return fg_GetLowestBitSetNoZero(umint(Bits));
}
int fg_ScanLowWordDirect(uint256 _Bits) {
  return fg_GetLowestBitSetNoZero(umint(_Bits | uint256(1)));
}
int fg_ScanLowWordUnknown(uint256 _Bits) {
  uint256 const Bits = _Bits;
  return fg_GetLowestBitSetNoZero(umint(Bits));
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}

namespace NRound226 {
// A loop variable is destroyed at the end of each run of the body, which
// changes what its destructor writes, unlike a reference.
struct CClear {
  umint &m_Bits;
  CClear(umint &_Bits) : m_Bits(_Bits) {}
  CClear(CClear const &_Other) : m_Bits(_Other.m_Bits) {}
  ~CClear() { m_Bits = 0; }
};
template <typename t_CValue>
struct TCVector {
  t_CValue *begin();
  t_CValue *end();
  void f_Insert(t_CValue const &_Value);
  t_CValue &operator[](umint _iValue);
};
int fg_GetLowestBitSetNoZero(umint _Bits);
int fg_ScanLoop(umint &_Bits) {
  TCVector<CClear> Clears;
  Clears.f_Insert(CClear(_Bits));
  if (!_Bits)
    return -1;
  for (auto Clear : Clears) {
  }
  return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
int fg_ScanBlock(umint &_Bits) {
  TCVector<CClear> Clears;
  Clears.f_Insert(CClear(_Bits));
  if (!_Bits)
    return -1;
  {
    auto Clear = Clears[0];
  }
  return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
int fg_ScanReferenceLoop(umint &_Bits) {
  TCVector<CClear> Clears;
  Clears.f_Insert(CClear(_Bits));
  if (!_Bits)
    return -1;
  for (auto &Clear : Clears) {
  }
  return fg_GetLowestBitSetNoZero(_Bits);
}
int fg_ScanUnrelatedLoop(umint &_Bits, umint &_Other) {
  TCVector<CClear> Clears;
  Clears.f_Insert(CClear(_Other));
  if (!_Bits)
    return -1;
  for (auto Clear : Clears) {
  }
  return fg_GetLowestBitSetNoZero(_Bits);
}
}

namespace NRound226Narrow {
// Narrowing an integer class into a narrower one of the same template keeps
// its low bits, of which one is set.
template <typename t_CUpper, typename t_CLower>
struct TCInt {
  t_CLower m_Lower{};
  t_CUpper m_Upper{};
  constexpr TCInt() = default;
  constexpr TCInt(t_CLower _Lower) : m_Lower(_Lower) {}
  constexpr TCInt(t_CUpper _Upper, t_CLower _Lower)
      : m_Lower(_Lower), m_Upper(_Upper) {}
  template <typename t_CUpper2, typename t_CLower2>
  constexpr TCInt(TCInt<t_CUpper2, t_CLower2> const &_Other);
  constexpr TCInt operator|(TCInt const &_Other) const {
    TCInt Result;
    Result.m_Lower = m_Lower | _Other.m_Lower;
    Result.m_Upper = m_Upper | _Other.m_Upper;
    return Result;
  }
};
using uint256 = TCInt<unsigned __int128, unsigned __int128>;
using uint512 = TCInt<uint256, uint256>;
template <typename t_CType>
int fg_GetLowestBitSetNoZero(t_CType const &_Bits);
int fg_Narrowed(uint512 const &_Bits) {
  return fg_GetLowestBitSetNoZero(uint256(_Bits | uint512(1)));
}
int fg_NarrowedHigh(uint512 const &_Bits) {
  return fg_GetLowestBitSetNoZero(uint256(_Bits | uint512(uint256(1), 0)));
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
int fg_NarrowedUnknown(uint512 const &_Bits) {
  return fg_GetLowestBitSetNoZero(uint256(_Bits));
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}

namespace NRound227Narrow {
// Narrowing keeps each word of it below the width of what it narrows to, not
// only the lowest.
using NRound226Narrow::uint256;
using NRound226Narrow::uint512;
using NRound226Narrow::fg_GetLowestBitSetNoZero;
int fg_SecondWord(uint512 const &_Bits) {
  return fg_GetLowestBitSetNoZero(uint256(_Bits | uint512(0, uint256(1, 0))));
}
int fg_ThirdWord(uint512 const &_Bits) {
  return fg_GetLowestBitSetNoZero(uint256(_Bits | uint512(uint256(0, 1), 0)));
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}

void *operator new(decltype(sizeof(0)), void *_pWhere) noexcept;
namespace NRound228 {
// What keeps where it is given only refers to it, which doesn't change it,
// but writes through it do, also of what an optional holds, whose storage
// can't be seen into.
template <typename t_CType>
struct TCReference {
  struct CData {
    t_CType *m_pPointTo;
  };
  CData m_Data;
  TCReference(t_CType &_Value) { m_Data.m_pPointTo = &_Value; }
  TCReference(TCReference const &_Other) { m_Data = _Other.m_Data; }
  t_CType &f_Get() const { return *m_Data.m_pPointTo; }
};
template <typename t_CType>
struct TCOptional {
  alignas(t_CType) unsigned char m_Storage[sizeof(t_CType)];
  template <typename tf_CValue>
  TCOptional(tf_CValue &&_Value) {
    new (m_Storage) t_CType(static_cast<tf_CValue &&>(_Value));
  }
  t_CType *operator->() { return reinterpret_cast<t_CType *>(m_Storage); }
  t_CType &operator*() { return *reinterpret_cast<t_CType *>(m_Storage); }
};
int fg_GetLowestBitSetNoZero(umint _Bits);
int fg_Referred(umint _Bits) {
  if (_Bits == 0)
    return -1;
  TCReference<umint> Bits(_Bits);
  return fg_GetLowestBitSetNoZero(_Bits) + int(Bits.f_Get() & 1);
}
int fg_ReferredWritten(umint _Bits) {
  if (_Bits == 0)
    return -1;
  TCReference<umint> Bits(_Bits);
  Bits.f_Get() = 0;
  return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
int fg_Optional(umint _Bits) {
  TCOptional<TCReference<umint>> Bits{TCReference<umint>(_Bits)};
  if (_Bits == 0)
    return -1;
  Bits->f_Get() = 0;
  return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
int fg_OptionalPointer(umint _Bits) {
  TCOptional<umint *> pBits{&_Bits};
  if (_Bits == 0)
    return -1;
  **pBits = 0;
  return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
int fg_OptionalRead(umint _Bits) {
  TCOptional<TCReference<umint>> Bits{TCReference<umint>(_Bits)};
  if (_Bits == 0)
    return -1;
  return fg_GetLowestBitSetNoZero(_Bits) + int(Bits->f_Get() & 1);
}
}

namespace NRound229 {
// What an optional holds that it gives through its bases to where it keeps
// it, also a union, is reached through its operators; not its own members,
// like the flag of a guard.
using NRound228::TCReference;
template <typename t_CType>
struct TCStorage {
  union {
    t_CType m_Value;
  };
  bool m_bSet = false;
  template <typename tf_CValue>
  TCStorage(tf_CValue &&_Value) : m_Value(static_cast<tf_CValue &&>(_Value)) {}
  ~TCStorage() {}
};
template <typename t_CType>
struct TCVariant : TCStorage<t_CType> {
  template <typename tf_CValue>
  TCVariant(tf_CValue &&_Value)
      : TCStorage<t_CType>(static_cast<tf_CValue &&>(_Value)) {}
};
template <typename t_CType>
struct TCOptional : TCVariant<t_CType> {
  TCOptional(t_CType &&_Value)
      : TCVariant<t_CType>(static_cast<t_CType &&>(_Value)) {}
  t_CType *operator->() { return &this->m_Value; }
  void f_Clear() { this->m_bSet = false; }
};
int fg_GetLowestBitSetNoZero(umint _Bits);
int fg_Optional(umint _Bits) {
  TCOptional<TCReference<umint>> Bits{TCReference<umint>(_Bits)};
  if (_Bits == 0)
    return -1;
  Bits->f_Get() = 0;
  return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
int fg_OptionalCleared(umint _Bits) {
  TCOptional<TCReference<umint>> Bits{TCReference<umint>(_Bits)};
  if (_Bits == 0)
    return -1;
  Bits.f_Clear();
  return fg_GetLowestBitSetNoZero(_Bits);
}
}

namespace NRound229Loop {
// A loop over the bits of an integer class whose condition clears the lowest
// and tests what is left.
struct uint256 {
  unsigned __int128 m_Lower = 0;
  unsigned __int128 m_Upper = 0;
  constexpr uint256(unsigned __int128 _Lower = 0) : m_Lower(_Lower) {}
  uint256 operator-(uint256 const &_Other) const;
  uint256 &operator&=(uint256 const &_Other);
  bool operator==(uint256 const &_Other) const;
};
int fg_GetLowestBitSetNoZero(uint256 const &_Bits);
void fg_Visit(int _Index);
void fg_Enumerate(uint256 _Bits) {
  if (_Bits == 0)
    return;
  do {
    fg_Visit(fg_GetLowestBitSetNoZero(_Bits));
  } while ((_Bits &= _Bits - uint256(1)) != 0);
}
void fg_EnumerateOther(uint256 _Bits, uint256 _Other) {
  if (_Bits == 0)
    return;
  do {
    fg_Visit(fg_GetLowestBitSetNoZero(_Bits));
  } while ((_Other &= _Bits - uint256(1)) != 0);
}
}

namespace NRound230 {
// A copy of a member iterator refers where it does; a copy of a shared
// pointer that only counts its copies through what it points to doesn't
// change what else is there.
template <typename t_CValue>
struct TCIterator {
  t_CValue *m_pValue;
  TCIterator(TCIterator const &_Other) : m_pValue(_Other.m_pValue) {}
  operator t_CValue *() const { return m_pValue; }
};
struct CInfo {
  umint m_Bits;
  int m_RefCount;
  void f_Increase() { ++m_RefCount; }
};
template <typename t_CType>
struct TCSharedPointer {
  t_CType *m_pPointTo;
  TCSharedPointer(TCSharedPointer const &_Other) { fp_SetInit(_Other.fp_Get()); }
  ~TCSharedPointer();
  void fp_SetInit(t_CType *_pPtr) {
    if (_pPtr)
      _pPtr->f_Increase();
    m_pPointTo = _pPtr;
  }
  t_CType *fp_Get() const { return m_pPointTo; }
  t_CType *operator->() const { return m_pPointTo; }
};
int fg_GetLowestBitSetNoZero(umint _Bits);
struct CWorker {
  TCIterator<umint> m_iBits;
  TCSharedPointer<CInfo> m_pInfo;
  int f_Iterator() {
    if (!*m_iBits)
      return 0;
    auto iBits = m_iBits;
    *iBits = 0;
    return fg_GetLowestBitSetNoZero(*m_iBits);
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
  }
  int f_Shared() {
    if (!m_pInfo->m_Bits)
      return 0;
    auto pCopy = m_pInfo;
    return fg_GetLowestBitSetNoZero(m_pInfo->m_Bits);
  }
};
}

namespace NRound231 {
// A constant of an integer class made of its words, `uint256(0, 1)`, is what
// they make, not its first.
template <typename t_CUpper, typename t_CLower>
struct TCInt {
  t_CLower m_Lower{};
  t_CUpper m_Upper{};
  constexpr TCInt(t_CLower _Lower) : m_Lower(_Lower) {}
  constexpr TCInt(t_CUpper _Upper, t_CLower _Lower)
      : m_Lower(_Lower), m_Upper(_Upper) {}
  bool operator==(TCInt const &_Other) const;
  TCInt operator-(TCInt const &_Other) const;
  TCInt &operator&=(TCInt const &_Other);
};
using uint256 = TCInt<unsigned __int128, unsigned __int128>;
int fg_GetLowestBitSetNoZero(uint256 const &_Bits);
void fg_Sentinel(uint256 _Bits) {
  if (_Bits == uint256(0))
    return;
  do {
    fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
  } while ((_Bits &= _Bits - uint256(1)) != uint256(0, 1));
}
void fg_Zero(uint256 _Bits) {
  if (_Bits == uint256(0))
    return;
  do {
    fg_GetLowestBitSetNoZero(_Bits);
  } while ((_Bits &= _Bits - uint256(1)) != uint256(0, 0));
}
}

namespace NRound232 {
// A copy of a vector of pointers points to what its elements do; what a
// getter gives of the storage the index operator indexes is its elements;
// a function wrapper of a lambda that only reads doesn't change it.
template <typename t_CData>
struct TCVectorData {
  t_CData *m_pData;
  t_CData *f_GetData() { return m_pData; }
};
template <typename t_CData>
struct TCVector {
  struct CStaticData {
    TCVectorData<t_CData> *m_pData;
  };
  CStaticData mp_StaticData;
  TCVector(TCVector const &_Other);
  ~TCVector();
  int f_GetLen() const { return 2; }
  static void fsp_CheckBounds(int _Len, int _Index) {}
  t_CData *f_GetArray() {
    if (mp_StaticData.m_pData)
      return mp_StaticData.m_pData->f_GetData();
    else
      return nullptr;
  }
  t_CData &operator[](int _Index) {
    fsp_CheckBounds(f_GetLen(), _Index);
    return (mp_StaticData.m_pData->f_GetData())[_Index];
  }
};
struct CInfo {
  umint m_Bits;
};
template <typename t_FSignature>
struct TCFunction;
template <typename t_CReturn>
struct TCFunction<t_CReturn()> {
  alignas(void *) unsigned char m_Storage[32];
  template <typename t_FFunction>
  TCFunction(t_FFunction &&_fFunction) {
    new (m_Storage) t_FFunction(static_cast<t_FFunction &&>(_fFunction));
  }
  ~TCFunction();
  t_CReturn operator()();
};
int fg_GetLowestBitSetNoZero(umint _Bits);
int fg_Copied(TCVector<CInfo *> &_Values) {
  auto Copy = _Values;
  if (_Values.f_GetLen() == 0 || !_Values[0] || !_Values[0]->m_Bits)
    return -1;
  Copy[0]->m_Bits = 0;
  return fg_GetLowestBitSetNoZero(_Values[0]->m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
int fg_OtherElement(TCVector<umint> &_Values) {
  if (_Values.f_GetLen() < 2 || !_Values[0])
    return -1;
  _Values.f_GetArray()[1] = 0;
  return fg_GetLowestBitSetNoZero(_Values[0]);
}
int fg_SameElement(TCVector<umint> &_Values) {
  if (_Values.f_GetLen() < 2 || !_Values[0])
    return -1;
  _Values.f_GetArray()[0] = 0;
  return fg_GetLowestBitSetNoZero(_Values[0]);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
int fg_ReadOnly(umint _Bits) {
  TCFunction<umint()> fRead = [&] { return _Bits; };
  if (!_Bits)
    return -1;
  umint Observed = fRead();
  return fg_GetLowestBitSetNoZero(_Bits) + int(Observed & 1);
}
int fg_Writes(umint _Bits) {
  TCFunction<umint()> fClear = [&] { return _Bits = 0; };
  if (!_Bits)
    return -1;
  fClear();
  return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}

namespace NRound234 {
struct CFreeState {
  umint m_Bits;
};
// Through what a lambda returns a reference to.
int fg_Scan(CFreeState &_State) {
  auto fState = [&]() -> CFreeState & { return _State; };
  if (!_State.m_Bits)
    return -1;
  fState().m_Bits = 0;
  return fg_GetLowestBitSetNoZero(_State.m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
// Not of another.
int fg_Other(CFreeState &_State, CFreeState &_Other) {
  auto fOther = [&]() -> CFreeState & { return _Other; };
  if (!_State.m_Bits)
    return -1;
  fOther().m_Bits = 0;
  return fg_GetLowestBitSetNoZero(_State.m_Bits);
}
}

namespace NRound235 {
// A const copy of a parameter by reference to const.
int fg_Scan(umint const &_Bits) {
  if (_Bits == 0)
    return -1;
  umint const Bits = _Bits;
  return fg_GetLowestBitSetNoZero(Bits);
}
// Not of one written after the copy.
int fg_Written(umint &_Bits) {
  if (_Bits == 0)
    return -1;
  umint const Bits = _Bits;
  _Bits = 0;
  return fg_GetLowestBitSetNoZero(Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}

namespace NRound236Iterator {
// A copy of a map iterator, whose tree iterator it holds points at the node
// it is at, is at the same entry.
struct CInfo {
  umint m_Bits;
};
struct CNode {
  CInfo m_Value;
  CInfo &f_Value() { return m_Value; }
};
struct CTreeIterator {
  CNode *m_pStack[4];
  int m_iStack = -1;
  static CNode *fs_FromLink(CNode *_pLink) { return _pLink; }
  CNode *f_GetCurrent() const {
    if (m_iStack >= 0)
      return fs_FromLink(m_pStack[m_iStack]);
    return nullptr;
  }
};
struct CIterator {
  CTreeIterator mp_Iter;
  CInfo *f_GetCurrent() const {
    auto *pNode = mp_Iter.f_GetCurrent();
    if (pNode)
      return &pNode->f_Value();
    return nullptr;
  }
  CInfo *operator->() const { return f_GetCurrent(); }
};
int fg_Scan(CIterator _iInfo) {
  auto iCopy = _iInfo;
  if (!_iInfo->m_Bits)
    return -1;
  iCopy->m_Bits = 0;
  return fg_GetLowestBitSetNoZero(_iInfo->m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}

namespace NRound236Narrow {
// Narrowing an integer class whose constructor from an integer masks its low
// bits into one member and shifts the rest into the other.
template <typename t_CUpper, typename t_CLower>
struct TCInt {
  t_CUpper m_Upper{};
  t_CLower m_Lower{};
  constexpr TCInt() = default;
  template <typename t_CType>
    requires(__is_integral(t_CType))
  constexpr TCInt(t_CType const &_Convert, int _Dummy = 0)
      : m_Upper((_Convert >> (sizeof(t_CType) * 8 - 1)) >> 1),
        m_Lower(_Convert & t_CType(-1)) {}
  template <typename t_CUpper2, typename t_CLower2>
  constexpr TCInt(TCInt<t_CUpper2, t_CLower2> const &_Other);
  constexpr TCInt operator|(TCInt const &_Other) const {
    TCInt Result;
    Result.m_Lower = m_Lower | _Other.m_Lower;
    Result.m_Upper = m_Upper | _Other.m_Upper;
    return Result;
  }
};
using uint128 = TCInt<unsigned long long, unsigned long long>;
using uint256 = TCInt<uint128, uint128>;
template <typename t_CType>
int fg_GetLowestBitSetNoZero(t_CType const &_Bits);
int fg_Narrowed(uint256 const &_Bits) {
  return fg_GetLowestBitSetNoZero(uint128(_Bits | uint256(1)));
}
int fg_NarrowedUnknown(uint256 const &_Bits) {
  return fg_GetLowestBitSetNoZero(uint128(_Bits));
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}

namespace NRound239 {
struct CInfo {
  umint m_Bits;
  umint m_nQueries;
};
template <typename t_CType>
struct TCUniquePointer {
  t_CType *m_pObject;
  t_CType *operator->() const { return m_pObject; }
  t_CType *f_Get() const { return m_pObject; }
};
// Another member of what a smart pointer gives, through its getter.
int fg_Scan(TCUniquePointer<CInfo> const &_pInfo) {
  if (!_pInfo->m_Bits)
    return -1;
  ++_pInfo.f_Get()->m_nQueries;
  return fg_GetLowestBitSetNoZero(_pInfo->m_Bits);
}
// But not the member checked.
int fg_Cleared(TCUniquePointer<CInfo> const &_pInfo) {
  if (!_pInfo->m_Bits)
    return -1;
  _pInfo.f_Get()->m_Bits = 0;
  return fg_GetLowestBitSetNoZero(_pInfo->m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
// Another element at a negative index.
int fg_ScanLast(umint *_pEnd) {
  if (!_pEnd[-1])
    return -1;
  _pEnd[-2] = 0;
  return fg_GetLowestBitSetNoZero(_pEnd[-1]);
}
}

namespace NRound240 {
umint &fg_RemoveQualifiers(umint const &_Value) {
  return const_cast<umint &>(_Value);
}
// Written through a local reference to const.
int fg_Alias(umint _Bits) {
  const umint Bits = _Bits;
  umint const &Alias = _Bits;
  fg_RemoveQualifiers(Alias) = 1;
  if (!_Bits)
    return -1;
  return fg_GetLowestBitSetNoZero(Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
// Not in what never runs.
int fg_Discarded(umint _Bits) {
  const umint Bits = _Bits;
  if constexpr (sizeof(umint) == 1)
    _Bits |= 1;
  static_assert(noexcept(_Bits = 0));
  if (!_Bits)
    return -1;
  return fg_GetLowestBitSetNoZero(Bits);
}
}

namespace NRound241 {
template <typename t_CUpper, typename t_CLower>
struct TCInt {
  t_CUpper m_Upper{};
  t_CLower m_Lower{};
  constexpr TCInt() = default;
  constexpr TCInt(t_CLower _Lower) : m_Lower(_Lower) {}
  constexpr TCInt operator|(TCInt const &_Other) const {
    TCInt Result;
    Result.m_Lower = m_Lower | _Other.m_Lower;
    Result.m_Upper = m_Upper | _Other.m_Upper;
    return Result;
  }
};
using uint128 = TCInt<unsigned long long, unsigned long long>;
template <typename t_CType>
int fg_GetLowestBitSetNoZero(t_CType const &_Bits);
// A word of it checked, which isn't zero when it isn't.
int fg_Word(uint128 const &_Bits) {
  if (_Bits.m_Lower == 0)
    return -1;
  return fg_GetLowestBitSetNoZero(_Bits);
}
int fg_Upper(uint128 const &_Bits) {
  if (!_Bits.m_Upper)
    return -1;
  return fg_GetLowestBitSetNoZero(_Bits);
}
// A temporary that isn't zero, bound to a reference to const.
int fg_Bound(uint128 const &_Bits) {
  auto const &Bits = _Bits | uint128(1);
  return fg_GetLowestBitSetNoZero(Bits);
}
}

namespace NRound242 {
template <typename t_CUpper, typename t_CLower>
struct TCInt {
  using CLower = t_CLower;
  t_CLower m_Lower{};
  t_CUpper m_Upper{};
};
using uint256 = TCInt<unsigned long long, unsigned long long>;
template <typename t_CUpper, typename t_CLower>
constexpr typename TCInt<t_CUpper, t_CLower>::CLower const &
fg_GetLower(TCInt<t_CUpper, t_CLower> const &_Integer) noexcept {
  return _Integer.m_Lower;
}
template <typename tf_CInt>
int fg_GetLowestBitSetNoZero(tf_CInt const &_Number) noexcept;
// What a helper gives a reference to of what it is given, which is written.
int fg_Scan(uint256 &_Bits) {
  auto const &Lower = fg_GetLower(_Bits);
  if (Lower == 0)
    return -1;
  _Bits.m_Lower = 0;
  return fg_GetLowestBitSetNoZero(Lower);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
// Not by writing another word.
int fg_Other(uint256 &_Bits) {
  auto const &Lower = fg_GetLower(_Bits);
  if (Lower == 0)
    return -1;
  _Bits.m_Upper = 0;
  return fg_GetLowestBitSetNoZero(Lower);
}
// What it gives compared with zero, which is the word it scans.
int fg_Compared(uint256 const &_Bits) {
  if (fg_GetLower(_Bits) != 0)
    return fg_GetLowestBitSetNoZero(fg_GetLower(_Bits));
  return -1;
}
}
namespace NRound243 {
template <typename t_CData>
struct TCVector {
  t_CData *m_pData;
  umint m_nLen;
  t_CData &operator[](umint _Index) { return m_pData[_Index]; }
  umint f_GetLen() const { return m_nLen; }
  t_CData &f_GetLast() { return (*this)[f_GetLen() - 1]; }
};
struct CEntry {
  umint m_Bits;
};
template <typename tf_CInt>
int fg_GetLowestBitSetNoZero(tf_CInt const &_Number) noexcept;
// Written through an element at a computed index, which can be the one
// checked.
int fg_Scan(TCVector<CEntry> &_Entries) {
  if (_Entries.f_GetLen() != 1 || !_Entries[0].m_Bits)
    return -1;
  auto &Entry = _Entries.f_GetLast();
  Entry.m_Bits = 0;
  return fg_GetLowestBitSetNoZero(_Entries[0].m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}
namespace NRound244 {
int fg_GetLowestBitSetNoZero(umint _Bits);
struct CSupportWeakTag {};
template <typename t_CType, typename... tp_COptions>
struct TCSharedPointer {
  struct CData {
    t_CType *m_pPointTo;
  };
  CData m_Data;
  t_CType *operator->() const { return m_Data.m_pPointTo; }
};
template <typename t_CType, typename... tp_COptions>
struct TCWeakPointer {
  struct CData {
    t_CType *m_pPointTo;
  };
  CData m_Data;
  template <typename tf_CType, typename... tfp_COptions>
  TCWeakPointer(TCSharedPointer<tf_CType, tfp_COptions...> const &_Other);
  TCSharedPointer<t_CType, CSupportWeakTag, tp_COptions...> f_Lock() const {
    TCSharedPointer<t_CType, CSupportWeakTag, tp_COptions...> pReturn;
    pReturn.m_Data.m_pPointTo = m_Data.m_pPointTo;
    return pReturn;
  }
};
struct CInfo {
  umint m_Bits;
};
// A lock of a weak pointer made from a shared one gives a copy of it.
int fg_Scan(TCSharedPointer<CInfo, CSupportWeakTag> const &_pInfo) {
  if (!_pInfo->m_Bits)
    return -1;
  TCWeakPointer<CInfo> pWeak(_pInfo);
  auto pLocked = pWeak.f_Lock();
  pLocked->m_Bits = 0;
  return fg_GetLowestBitSetNoZero(_pInfo->m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
// Not of another.
int fg_Other(TCSharedPointer<CInfo, CSupportWeakTag> const &_pInfo,
             TCSharedPointer<CInfo, CSupportWeakTag> const &_pOther) {
  if (!_pInfo->m_Bits)
    return -1;
  TCWeakPointer<CInfo> pWeak(_pOther);
  auto pLocked = pWeak.f_Lock();
  pLocked->m_Bits = 0;
  return fg_GetLowestBitSetNoZero(_pInfo->m_Bits);
}
}
namespace NRound245 {
int fg_GetLowestBitSetNoZero(umint _Bits);
template <typename t_CType>
struct TCSharedPointer {
  struct CData {
    t_CType *m_pPointTo;
  };
  CData m_Data;
  t_CType *operator->() const { return m_Data.m_pPointTo; }
};
struct CInfo {
  umint m_Bits;
};
// A helper that writes it through a local copy of the pointer it is given.
void fg_Clear(TCSharedPointer<CInfo> const &_pInfo) {
  auto pCopy = _pInfo;
  pCopy->m_Bits = 0;
}
int fg_Scan(TCSharedPointer<CInfo> const &_pInfo) {
  if (!_pInfo->m_Bits)
    return -1;
  fg_Clear(_pInfo);
  return fg_GetLowestBitSetNoZero(_pInfo->m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
// What a helper gives a reference to of what it is given, tested and scanned
// through two calls of it, unless written between them.
struct CInt {
  umint m_Lower;
  umint m_Upper;
};
umint const &fg_GetLower(CInt const &_Integer) { return _Integer.m_Lower; }
int fg_Lower(CInt const &_Bits) {
  if (fg_GetLower(_Bits))
    return fg_GetLowestBitSetNoZero(fg_GetLower(_Bits));
  return -1;
}
int fg_LowerWritten(CInt &_Bits) {
  if (!fg_GetLower(_Bits))
    return -1;
  _Bits.m_Lower = 0;
  return fg_GetLowestBitSetNoZero(fg_GetLower(_Bits));
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}

namespace NRound328 {
// A tagged pointer, which keeps the pointer in an integer member that its
// copies copy, so a write through a copy writes what it points to.
template <typename t_CType, int t_nBits = 1>
struct TCBitStorePointer {
  TCBitStorePointer(t_CType *_pPtr = nullptr) : mp_PointTo(fsp_ToInt(_pPtr)) {}
  TCBitStorePointer(TCBitStorePointer const &_Other) : mp_PointTo(_Other.mp_PointTo) {}
  TCBitStorePointer &operator=(TCBitStorePointer const &_Other) {
    mp_PointTo = _Other.mp_PointTo;
    return *this;
  }
  t_CType *f_Get() const { return fsp_FromInt(mp_PointTo); }
  t_CType *operator->() const { return fsp_FromInt(mp_PointTo); }
  static umint fsp_ToInt(t_CType *_pPtr) { return umint(_pPtr) >> t_nBits; }
  static constexpr t_CType *fsp_FromInt(umint _Pointer) {
    return (t_CType *)(_Pointer << t_nBits);
  }
  umint mp_PointTo : sizeof(umint) * 8 - t_nBits;
  umint mp_Bits : t_nBits;
};
struct CInfo {
  umint m_Bits;
};
int fg_Scan(TCBitStorePointer<CInfo> const &_pInfo) {
  if (!_pInfo->m_Bits)
    return -1;
  auto pCopy = _pInfo;
  pCopy->m_Bits = 0;
  return fg_GetLowestBitSetNoZero(_pInfo->m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}

namespace NRound329 {
// A word converted to an integer class, which is zero when the word is, also
// masked, `(uint256(_Bits) & _Mask) == 0`.
template <typename t_CUpper, typename t_CLower>
struct TCInt {
  t_CLower m_Lower{};
  t_CUpper m_Upper{};
  constexpr TCInt(unsigned long long _Value = 0) : m_Lower(_Value) {}
  constexpr TCInt operator&(TCInt const &_Other) const {
    TCInt Result;
    Result.m_Lower = m_Lower & _Other.m_Lower;
    Result.m_Upper = m_Upper & _Other.m_Upper;
    return Result;
  }
  constexpr bool operator==(TCInt const &_Other) const {
    return m_Lower == _Other.m_Lower && m_Upper == _Other.m_Upper;
  }
};
using uint256 = TCInt<unsigned long long, unsigned long long>;
int fg_GetLowestBitSetNoZero(unsigned long long _Value);
int fg_ScanMasked(unsigned long long _Bits, uint256 const &_Mask) {
  if ((uint256(_Bits) & _Mask) == 0)
    return -1;
  return fg_GetLowestBitSetNoZero(_Bits);
}
int fg_ScanConverted(unsigned long long _Bits) {
  if (uint256(_Bits) == 0)
    return -1;
  return fg_GetLowestBitSetNoZero(_Bits);
}
}

namespace NRound330VirtualBases {
// A callable searched for in the bases of what the pointer is given to, each
// once, also a virtual one, which is a base through each path to it.
using uint32 = unsigned;
int fg_GetHighestBitSetNoZero(uint32 const &);
template<int N, int I>
struct TCLayer :
    virtual TCLayer<N-1, 0>, virtual TCLayer<N-1, 1>,
    virtual TCLayer<N-1, 2>, virtual TCLayer<N-1, 3>,
    virtual TCLayer<N-1, 4>, virtual TCLayer<N-1, 5>,
    virtual TCLayer<N-1, 6>, virtual TCLayer<N-1, 7>,
    virtual TCLayer<N-1, 8>, virtual TCLayer<N-1, 9> {};
template<int I> struct TCLayer<0, I> {};
struct CStore : TCLayer<8, 0> { CStore(uint32 *); };
void fg_Test(uint32 _Bits) {
  if (!_Bits) return;
  CStore Store(&_Bits);
  fg_GetHighestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}

namespace NRound330Destructor {
// A destructor that writes an integer class through a reference member by
// its overloaded assignment.
struct uint256 {
  unsigned long long m_Words[4]{};
  uint256(unsigned long long _Value = 0) : m_Words{_Value} {}
  uint256 &operator=(uint256 const &_Other) { for (int i = 0; i < 4; ++i) m_Words[i] = _Other.m_Words[i]; return *this; }
  bool operator!() const { return !(m_Words[0] | m_Words[1] | m_Words[2] | m_Words[3]); }
};
int fg_GetLowestBitSetNoZero(uint256 const &_Value);
struct CClearBits {
  uint256 &m_Bits;
  ~CClearBits() { m_Bits = 0; }
};
int fg_Scan(uint256 _Bits) {
  if (!_Bits)
    return 0;
  { CClearBits Clear{_Bits}; }
  return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}

namespace NRound331 {
int fg_GetLowestBitSetNoZero(umint const &_Bits);
// A scope guard armed again after it is cleared calls what it holds.
template <typename t_FCleanup>
struct TCOnScopeExit {
  t_FCleanup m_fCleanup;
  bool m_bActive = true;
  ~TCOnScopeExit() {
    if (m_bActive)
      m_fCleanup();
  }
  void f_Clear() { m_bActive = false; }
  TCOnScopeExit &operator=(TCOnScopeExit &&_Other) {
    m_bActive = _Other.m_bActive;
    _Other.m_bActive = false;
    return *this;
  }
};
int fg_ScanRearmed(umint _Bits) {
  if (!_Bits)
    return -1;
  {
    auto fClear = [&] { _Bits = 0; };
    TCOnScopeExit<decltype(fClear)> Cleanup{fClear};
    Cleanup.f_Clear();
    Cleanup = TCOnScopeExit<decltype(fClear)>{fClear};
  }
  return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
int fg_ScanReactivated(umint _Bits) {
  if (!_Bits)
    return -1;
  {
    auto fClear = [&] { _Bits = 0; };
    TCOnScopeExit<decltype(fClear)> Cleanup{fClear};
    Cleanup.f_Clear();
    Cleanup.m_bActive = true;
  }
  return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
// But not one cleared again.
int fg_ScanClearedAgain(umint _Bits) {
  if (!_Bits)
    return -1;
  {
    auto fClear = [&] { _Bits = 0; };
    TCOnScopeExit<decltype(fClear)> Cleanup{fClear};
    Cleanup.f_Clear();
    Cleanup.f_Clear();
  }
  return fg_GetLowestBitSetNoZero(_Bits);
}
// A wrapper of an integer is the integer it holds, which a conversion gives,
// but not one that holds more.
template <typename t_CType>
struct TCAutoClear {
  t_CType m_Value = 0;
  operator t_CType &() { return m_Value; }
  operator const t_CType &() const { return m_Value; }
  const t_CType &f_Get() const { return m_Value; }
  template <typename tf_CRight>
  bool operator==(tf_CRight const &_Right) const { return f_Get() == _Right; }
};
using zmint = TCAutoClear<umint>;
int fg_ScanWrapped(zmint _Bits) {
  if (_Bits == 0)
    return -1;
  return fg_GetLowestBitSetNoZero(umint(_Bits));
}
int fg_ScanWrappedReference(zmint const &_nBytes) {
  if (_nBytes != 0)
    return fg_GetLowestBitSetNoZero(umint(_nBytes));
  return 0;
}
struct CPair {
  umint m_Value = 0;
  umint m_Other = 0;
  operator const umint &() const { return m_Value; }
  bool operator==(int _Right) const { return m_Other == umint(_Right); }
};
int fg_ScanPair(CPair _Pair) {
  if (_Pair == 0)
    return -1;
  return fg_GetLowestBitSetNoZero(umint(_Pair));
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
// An element read through an iterator that converts to a pointer to it,
// which a write of the element changes.
template <typename t_CData>
struct TCIterator {
  t_CData *m_pCurrent;
  operator t_CData *() const { return m_pCurrent; }
};
template <typename t_CData>
struct TCVector {
  t_CData *m_pData;
  t_CData &operator[](umint _Index) { return m_pData[_Index]; }
  t_CData const &operator[](umint _Index) const { return m_pData[_Index]; }
  t_CData const &f_GetFirst() const { return (*this)[0]; }
  TCIterator<t_CData> f_GetIterator() { return {m_pData}; }
};
int fg_ScanIterator(TCVector<umint> &_Values) {
  auto iValue = _Values.f_GetIterator();
  if (!*iValue)
    return -1;
  _Values[0] = 0;
  return fg_GetLowestBitSetNoZero(*iValue);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
int fg_ScanIteratorUnchanged(TCVector<umint> &_Values) {
  auto iValue = _Values.f_GetIterator();
  if (!*iValue)
    return -1;
  return fg_GetLowestBitSetNoZero(*iValue);
}
// A reference a getter gives of an element is that element, which a test of
// it by the index checks.
int fg_ScanFirst(TCVector<umint> const &_Bits) {
  auto const &Bits = _Bits.f_GetFirst();
  if (!_Bits[0])
    return -1;
  return fg_GetLowestBitSetNoZero(Bits);
}
int fg_ScanFirstCompared(TCVector<umint> const &_Bits) {
  auto const &Bits = _Bits.f_GetFirst();
  if (_Bits[0] == 0)
    return -1;
  return fg_GetLowestBitSetNoZero(Bits);
}
int fg_ScanSecond(TCVector<umint> const &_Bits) {
  auto const &Bits = _Bits.f_GetFirst();
  if (!_Bits[1])
    return -1;
  return fg_GetLowestBitSetNoZero(Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
// An integer class assigned and then tested as a boolean, `while (_Bits >>=
// 1)`, through an operator that gives its object, is tested after it is
// assigned, but not through one that gives another.
struct CWide {
  umint m_Low = 0;
  umint m_High = 0;
  CWide &operator>>=(int _Shift) {
    m_Low = (m_Low >> _Shift) | (m_High << (64 - _Shift));
    m_High >>= _Shift;
    return *this;
  }
  CWide &operator<<=(int _Shift);
  explicit operator bool() const { return m_Low || m_High; }
};
CWide g_Other;
CWide &CWide::operator<<=(int _Shift) {
  m_High = (m_High << _Shift) | (m_Low >> (64 - _Shift));
  m_Low <<= _Shift;
  return g_Other;
}
int fg_GetLowestBitSetNoZero(CWide const &_Bits);
int fg_ScanShifted(CWide _Bits) {
  int Result = -1;
  while (_Bits >>= 1)
    Result = fg_GetLowestBitSetNoZero(_Bits);
  return Result;
}
int fg_ScanShiftedOther(CWide _Bits) {
  int Result = -1;
  while (_Bits <<= 1)
    Result = fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
  return Result;
}
}

namespace NRound332 {
int fg_GetLowestBitSetNoZero(umint const &_Bits);
template <typename t_FCleanup>
struct TCOnScopeExit {
  t_FCleanup m_fCleanup;
  bool m_bActive = true;
  ~TCOnScopeExit() {
    if (m_bActive)
      m_fCleanup();
  }
  void f_Clear() { m_bActive = false; }
};
// A shared scope guard whose last owner is destroyed calls what it holds,
// also where the pointer is a member of a member it destroys it through.
template <typename t_CType>
struct TCSharedPointer {
  t_CType *m_pPointTo = nullptr;
  ~TCSharedPointer() { delete m_pPointTo; }
};
template <typename t_CType>
void fg_DeleteObject(t_CType *_pObject) { delete _pObject; }
template <typename t_CType>
struct TCSharedData {
  t_CType *m_pPointTo = nullptr;
};
template <typename t_CType>
struct TCSharedHeld {
  TCSharedData<t_CType> m_Data;
  ~TCSharedHeld() { fg_DeleteObject(m_Data.m_pPointTo); }
};
struct COnScopeExitSharedHelper {
  template <typename tf_FCleanup>
  TCSharedPointer<TCOnScopeExit<tf_FCleanup>> operator/(tf_FCleanup &&_fCleanup) const {
    return {new TCOnScopeExit<tf_FCleanup>{static_cast<tf_FCleanup &&>(_fCleanup)}};
  }
};
struct COnScopeExitHeldHelper {
  template <typename tf_FCleanup>
  TCSharedHeld<TCOnScopeExit<tf_FCleanup>> operator/(tf_FCleanup &&_fCleanup) const {
    return {{new TCOnScopeExit<tf_FCleanup>{static_cast<tf_FCleanup &&>(_fCleanup)}}};
  }
};
COnScopeExitSharedHelper const g_OnScopeExitShared;
COnScopeExitHeldHelper const g_OnScopeExitHeld;
int fg_ScanShared(umint _Bits) {
  if (!_Bits)
    return -1;
  {
    auto pCleanup = g_OnScopeExitShared / [&] { _Bits = 0; };
  }
  return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
int fg_ScanHeld(umint _Bits) {
  if (!_Bits)
    return -1;
  {
    auto pCleanup = g_OnScopeExitHeld / [&] { _Bits = 0; };
  }
  return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
// What else a holder of a scope guard removes, `f_ClearCache()`, leaves it
// armed: only what writes what its destructor reads cancels it.
template <typename t_FCleanup>
struct TCScopedCache {
  TCOnScopeExit<t_FCleanup> m_Cleanup;
  unsigned m_nCached = 0;
  void f_ClearCache() { m_nCached = 0; }
};
int fg_ScanCache(umint _Bits) {
  if (_Bits == 0)
    return -1;
  {
    auto fReset = [&] { _Bits = 0; };
    TCScopedCache<decltype(fReset)> Cache{{fReset}};
    Cache.f_ClearCache();
  }
  return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
int fg_ScanCancelled(umint _Bits) {
  if (_Bits == 0)
    return -1;
  {
    auto fReset = [&] { _Bits = 0; };
    TCOnScopeExit<decltype(fReset)> Cleanup{fReset};
    Cleanup.f_Clear();
  }
  return fg_GetLowestBitSetNoZero(_Bits);
}
// A conversion of an integer class of several words to a wider integer that
// puts the lower word in last, `Ret = Ret | t_CInt(m_Lower)`, keeps its bits.
using uint32 = unsigned;
using uint64 = unsigned long long;
template <typename t_CUpper, typename t_CLower>
struct TCInt {
  t_CLower m_Lower{};
  t_CUpper m_Upper{};
  constexpr TCInt() = default;
  constexpr TCInt(t_CLower _Lower) : m_Lower(_Lower) {}
  constexpr TCInt(t_CUpper _Upper, t_CLower _Lower) : m_Lower(_Lower), m_Upper(_Upper) {}
  constexpr TCInt operator|(TCInt const &_Other) const {
    return {m_Upper | _Other.m_Upper, m_Lower | _Other.m_Lower};
  }
  template <typename t_CInt>
  constexpr explicit operator t_CInt() const {
    if constexpr (sizeof(t_CInt) <= sizeof(t_CLower))
      return t_CInt(m_Lower);
    else {
      t_CInt Ret;
      Ret = t_CInt((t_CInt(m_Upper) << (sizeof(t_CLower) * 8 - 1)) << 1);
      Ret = Ret | t_CInt(m_Lower);
      return Ret;
    }
  }
};
using uint96 = TCInt<TCInt<uint32, uint32>, uint32>;
int fg_GetLowestBitSetNoZero(uint64 const &_Bits);
int fg_ScanWide(uint96 const &_Bits) {
  return fg_GetLowestBitSetNoZero(uint64(_Bits | uint96(1)));
}
int fg_ScanWideUnset(uint96 const &_Bits) {
  return fg_GetLowestBitSetNoZero(uint64(_Bits | uint96(0)));
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
// A wrapper of an integer class is the integer class it holds.
template <typename t_CType>
struct TCAutoClear {
  t_CType m_Value{};
  operator const t_CType &() const { return m_Value; }
  template <typename tf_CRight>
  bool operator==(tf_CRight const &_Right) const { return m_Value == _Right; }
};
struct CWide {
  umint m_Low = 0;
  umint m_High = 0;
  constexpr CWide(umint _Low = 0, umint _High = 0) : m_Low(_Low), m_High(_High) {}
  bool operator==(CWide const &_Other) const { return m_Low == _Other.m_Low && m_High == _Other.m_High; }
};
int fg_GetLowestBitSetNoZero(CWide const &_Bits);
int fg_ScanWrappedWide(TCAutoClear<CWide> const &_Bits) {
  if (_Bits == 0)
    return -1;
  return fg_GetLowestBitSetNoZero(CWide(_Bits));
}
}

namespace NRound333 {
int fg_GetLowestBitSetNoZero(umint const &_Bits);
// A write through a copy of an iterator that converts to a pointer to the
// element writes the element.
template <typename t_CData>
struct TCIterator {
  t_CData *m_pCurrent;
  operator t_CData *() const { return m_pCurrent; }
};
template <typename t_CData>
struct TCVector {
  t_CData *m_pData;
  t_CData &operator[](umint _Index) { return m_pData[_Index]; }
  t_CData const &operator[](umint _Index) const { return m_pData[_Index]; }
  t_CData const &f_GetFirst() const { return (*this)[0]; }
  TCIterator<t_CData> f_GetIterator() { return {m_pData}; }
};
int fg_ScanCopied(TCVector<umint> &_Values) {
  auto iValue = _Values.f_GetIterator();
  if (!_Values[0])
    return -1;
  auto iCopy = iValue;
  *iCopy = 0;
  return fg_GetLowestBitSetNoZero(_Values[0]);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
// A getter that only gives access gives the same element each time.
int fg_ScanGetter(TCVector<umint> const &_Values) {
  if (!_Values.f_GetFirst())
    return -1;
  return fg_GetLowestBitSetNoZero(_Values.f_GetFirst());
}
// A pointer member pointed elsewhere before it is written through doesn't
// write what it pointed to.
struct CCursor {
  umint *m_pWord;
};
int fg_ScanRebound(umint _Bits) {
  if (!_Bits)
    return -1;
  umint Other = 1;
  CCursor Current{&_Bits};
  Current.m_pWord = &Other;
  *Current.m_pWord = 0;
  return fg_GetLowestBitSetNoZero(_Bits);
}
int fg_ScanNotRebound(umint _Bits) {
  if (!_Bits)
    return -1;
  CCursor Current{&_Bits};
  *Current.m_pWord = 0;
  return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
// The all ones value an explicit conversion to the type of the complement
// keeps, also of a wrapper of the integer.
using uint64 = unsigned long long;
template <typename t_CType>
struct TCAutoClear {
  t_CType m_Value = 0;
  operator t_CType &() { return m_Value; }
  operator const t_CType &() const { return m_Value; }
};
int fg_GetLowestBitSetNoZero(uint64 const &_Bits);
int fg_FindFreeBit(TCAutoClear<uint64> const &_Used) {
  if (uint64(_Used) == ~uint64(0))
    return -1;
  return fg_GetLowestBitSetNoZero(uint64(~_Used));
}
// A word of an integer class that its conversion to a wider integer keeps.
using uint32 = unsigned;
using uint16 = unsigned short;
template <typename t_CUpper, typename t_CLower>
struct TCInt {
  t_CLower m_Lower{};
  t_CUpper m_Upper{};
  template <typename t_CInt>
  constexpr explicit operator t_CInt() const {
    if constexpr (sizeof(t_CInt) <= sizeof(t_CLower))
      return t_CInt(m_Lower);
    else {
      t_CInt Ret;
      Ret = t_CInt((t_CInt(m_Upper) << (sizeof(t_CLower) * 8 - 1)) << 1);
      Ret = Ret | t_CInt(m_Lower);
      return Ret;
    }
  }
};
using uint96 = TCInt<TCInt<uint32, uint32>, uint32>;
int fg_GetLowestBitSetNoZero(uint16 const &_Bits);
int fg_ScanWord(uint96 const &_Bits) {
  if (_Bits.m_Lower == 0)
    return -1;
  return fg_GetLowestBitSetNoZero(uint64(_Bits));
}
// But not one the conversion narrows.
int fg_ScanNarrowed(uint96 const &_Bits) {
  if (_Bits.m_Lower == 0)
    return -1;
  return fg_GetLowestBitSetNoZero(uint16(_Bits));
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}

namespace NRound334 {
int fg_GetLowestBitSetNoZero(umint _Bits);
// What a lookup gives a pointer into is changed by a write of the map, and a
// const lookup given a key by a reference it doesn't write gives the same
// value each time.
template <typename t_CKey, typename t_CValue>
struct TCMap {
  struct CNode {
    t_CKey m_Key;
    t_CValue m_Value;
  };
  CNode *m_pNodes;
  template <typename tf_CKey> CNode const *fp_Find(tf_CKey const &_Key) const {
    return m_pNodes;
  }
  template <typename tf_CKey> t_CValue *f_FindEqual(tf_CKey &&_Key) {
    return &const_cast<CNode *>(fp_Find(_Key))->m_Value;
  }
  template <typename tf_CKey> t_CValue &operator[](tf_CKey &&_Key) {
    return const_cast<CNode *>(fp_Find(_Key))->m_Value;
  }
  template <typename tf_CKey> t_CValue const &operator[](tf_CKey &&_Key) const {
    return fp_Find(_Key)->m_Value;
  }
};
int fg_ScanFound(TCMap<int, umint> &_Bits, int _Key) {
  auto *pBits = _Bits.f_FindEqual(_Key);
  if (!pBits || *pBits == 0)
    return -1;
  _Bits[_Key] = 0;
  return fg_GetLowestBitSetNoZero(*pBits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
int fg_ScanFoundUnchanged(TCMap<int, umint> &_Bits, int _Key) {
  auto *pBits = _Bits.f_FindEqual(_Key);
  if (!pBits || *pBits == 0)
    return -1;
  return fg_GetLowestBitSetNoZero(*pBits);
}
int fg_ScanKeyed(TCMap<int, umint> const &_Bits, int _Key) {
  if (_Bits[_Key] == 0)
    return -1;
  return fg_GetLowestBitSetNoZero(_Bits[_Key]);
}
// But not of a key the lookup writes.
struct CWritingMap {
  umint m_Bits;
  umint const &operator[](int &_Key) const {
    ++_Key;
    return m_Bits;
  }
};
int fg_ScanWritingKey(CWritingMap const &_Bits, int _Key) {
  if (_Bits[_Key] == 0)
    return -1;
  return fg_GetLowestBitSetNoZero(_Bits[_Key]);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
// Cleared through what an owning pointer destroys that is assigned to it after
// it is made.
struct CClear {
  umint &m_Bits;
  CClear(umint &_Bits) : m_Bits(_Bits) {}
  ~CClear() { m_Bits = 0; }
};
template <typename t_CType>
struct TCUniquePointer {
  t_CType *m_pData = nullptr;
  TCUniquePointer() = default;
  template <typename tf_CProxy> TCUniquePointer &operator=(tf_CProxy &&_Proxy);
  ~TCUniquePointer() { delete m_pData; }
  void f_Clear() {
    delete m_pData;
    m_pData = nullptr;
  }
};
struct CConstructProxy {
  umint &m_Bits;
};
CConstructProxy fg_Construct(umint &_Bits);
void fg_ScanAssigned(umint _Bits, umint _Other) {
  TCUniquePointer<CClear> pClear;
  pClear = fg_Construct(_Bits);
  if (_Bits) {
    pClear.f_Clear();
    fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
  }
  TCUniquePointer<CClear> pOther;
  pOther = fg_Construct(_Other);
  if (_Bits) {
    pOther.f_Clear();
    fg_GetLowestBitSetNoZero(_Bits);
  }
}
}
namespace NRound334Tagged {
int fg_GetLowestBitSetNoZero(umint _Bits);
// A const lookup that gives a pointer member to what only decodes it,
// `CLink::fs_GetPtr(m_pRoot)`, gives the same value each time, but not one
// that writes through what it is given.
struct CLink {
  CLink *m_pNext;
  umint m_Value;
  static CLink *fs_GetPtr(CLink *_pLink) {
    return (CLink *)((umint)_pLink & ~umint(3));
  }
  static CLink *fs_Clear(CLink *_pLink) {
    _pLink->m_Value = 0;
    return _pLink;
  }
};
struct CTaggedMap {
  CLink *m_pRoot;
  umint const &operator[](int _Key) const {
    return CLink::fs_GetPtr(m_pRoot)->m_Value;
  }
};
struct CClearingMap {
  CLink *m_pRoot;
  umint const &operator[](int _Key) const {
    return CLink::fs_Clear(m_pRoot)->m_Value;
  }
};
int fg_ScanTagged(CTaggedMap const &_Bits, int _Key) {
  if (_Bits[_Key] == 0)
    return -1;
  return fg_GetLowestBitSetNoZero(_Bits[_Key]);
}
int fg_ScanCleared(CClearingMap const &_Bits, int _Key) {
  if (_Bits[_Key] == 0)
    return -1;
  return fg_GetLowestBitSetNoZero(_Bits[_Key]);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}
namespace NRound334Local {
int fg_GetLowestBitSetNoZero(umint const &_Bits);
// A callback called through a local reference to it in a destructor, and a
// const member function called through a local reference to a member.
template <typename t_FCleanup> struct TCOnScopeExit {
  t_FCleanup m_fCleanup;
  ~TCOnScopeExit() {
    auto &fCleanup = m_fCleanup;
    fCleanup();
  }
};
int fg_ScanCleaned(umint &_Bits) {
  if (!_Bits)
    return -1;
  {
    auto fClear = [&] { _Bits = 0; };
    TCOnScopeExit<decltype(fClear)> Cleanup{fClear};
  }
  return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
struct CMaskState {
  mutable umint m_Bits;
  void f_Clear() const { m_Bits = 0; }
};
struct CMask {
  CMaskState m_State;
  void f_Clear() const {
    auto &State = m_State;
    State.f_Clear();
  }
};
int fg_ScanMask(CMask const &_Mask) {
  if (!_Mask.m_State.m_Bits)
    return -1;
  _Mask.f_Clear();
  return fg_GetLowestBitSetNoZero(_Mask.m_State.m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}

namespace NRound335 {
int fg_GetLowestBitSetNoZero(umint _Bits);
int fg_GetHighestBitSetNoZero(umint _Bits);
// A test of a member of an element only its index reads doesn't check the
// index, but a test that a value isn't the constant it is less by checks the
// difference.
struct CSizeInfo {
  bool m_bEnabled;
};
template <typename t_CData> struct TCVector {
  t_CData *m_pData;
  t_CData const &operator[](umint _Index) const { return m_pData[_Index]; }
};
int fg_SizeClass(TCVector<CSizeInfo> const &_Sizes, umint _Size) {
  if (_Sizes[_Size].m_bEnabled)
    return fg_GetHighestBitSetNoZero(_Size);
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
  return -1;
}
int fg_SizeClassArray(CSizeInfo const *_pSizes, umint _Size) {
  if (_pSizes[_Size].m_bEnabled)
    return fg_GetHighestBitSetNoZero(_Size);
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
  return -1;
}
int fg_CapacityBit(umint _Size) {
  if (_Size <= 1)
    return -1;
  return fg_GetHighestBitSetNoZero(_Size - 1);
}
int fg_CapacityBitLess(umint _Size) {
  if (2 > _Size)
    return -1;
  return fg_GetHighestBitSetNoZero(_Size - 1);
}
int fg_CapacityBitWrong(umint _Size) {
  if (_Size < 1)
    return -1;
  return fg_GetHighestBitSetNoZero(_Size - 1);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
// A function wrapper runs what it has where it is called, also what is
// assigned to it, and a lambda a function made runs its body.
template <typename t_FSignature> struct TCFunction;
template <typename t_CReturn> struct TCFunction<t_CReturn()> {
  void *m_pCallable = nullptr;
  t_CReturn (*m_fCall)(void *) = nullptr;
  TCFunction() = default;
  template <typename tf_FCallable> TCFunction(tf_FCallable &&_fCallable) {}
  template <typename tf_FCallable>
  TCFunction &operator=(tf_FCallable &&_fCallable) {
    return *this;
  }
  t_CReturn operator()() const { return m_fCall(m_pCallable); }
};
void fg_ScanAssigned(umint _Bits) {
  TCFunction<void()> fWork = [] {};
  fWork = [&] { _Bits = 0; };
  if (_Bits) {
    fWork();
    fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
  }
}
void fg_ScanReplaced(umint _Bits) {
  TCFunction<void()> fWork = [&] { _Bits = 0; };
  fWork = [] {};
  if (_Bits) {
    fWork();
    fg_GetLowestBitSetNoZero(_Bits);
  }
}
void fg_ScanDefaulted(umint _Bits) {
  TCFunction<void()> fClear;
  fClear = [&] { _Bits = 0; };
  if (_Bits) {
    fClear();
    fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
  }
}
void fg_ScanReassignedLambda(umint _Bits) {
  auto fNothing = [] {};
  auto fWork = fNothing;
  if (_Bits) {
    fWork();
    fg_GetLowestBitSetNoZero(_Bits);
  }
}
auto fg_MakeClear(umint &_Bits) {
  return [&_Bits] { _Bits = 0; };
}
void fg_ScanMade(umint _Bits) {
  auto fClear = fg_MakeClear(_Bits);
  if (!_Bits)
    return;
  fClear();
  fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
// Clearing an owning pointer destroys what was assigned to it before, not
// after, also by assigning it, also of what a call it is made with
// constructs.
struct CClear {
  umint &m_Bits;
  CClear(umint &_Bits) : m_Bits(_Bits) {}
  ~CClear() { m_Bits = 0; }
};
template <typename t_CType> struct TCUniquePointer {
  t_CType *m_pData = nullptr;
  TCUniquePointer() = default;
  template <typename tf_CProxy> TCUniquePointer(tf_CProxy &&_Proxy);
  template <typename tf_CProxy> TCUniquePointer &operator=(tf_CProxy &&_Proxy);
  ~TCUniquePointer() { delete m_pData; }
  void f_Clear() {
    delete m_pData;
    m_pData = nullptr;
  }
};
struct CConstructProxy {
  umint &m_Bits;
};
CConstructProxy fg_Construct(umint &_Bits);
int fg_ScanBeforeAssigned(umint _Bits, umint _Other) {
  TCUniquePointer<CClear> pClear = fg_Construct(_Other);
  if (!_Bits)
    return -1;
  pClear.f_Clear();
  int Bit = fg_GetLowestBitSetNoZero(_Bits);
  pClear = fg_Construct(_Bits);
  return Bit;
}
int fg_ScanAssignedInLoop(umint _Bits, umint _Other) {
  TCUniquePointer<CClear> pClear = fg_Construct(_Other);
  int Bit = -1;
  for (int iRound = 0; iRound < 2; ++iRound) {
    if (!_Bits)
      return -1;
    pClear.f_Clear();
    Bit = fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
    pClear = fg_Construct(_Bits);
  }
  return Bit;
}
template <typename t_FCleanup> struct TCOnScopeExit {
  t_FCleanup m_fCleanup;
  ~TCOnScopeExit() { m_fCleanup(); }
};
using COnScopeExitShared = TCUniquePointer<TCOnScopeExit<TCFunction<void()>>>;
struct COnScopeExitSharedHelper {
  template <typename tf_FCleanup>
  COnScopeExitShared operator/(tf_FCleanup &&_fCleanup) const {
    return {};
  }
};
extern COnScopeExitSharedHelper const &g_OnScopeExitShared;
int fg_ScanShared(umint _Bits) {
  auto Cleanup = g_OnScopeExitShared / [&] { _Bits = 0; };
  if (_Bits == 0)
    return -1;
  Cleanup.f_Clear();
  return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
int fg_ScanSharedNull(umint _Bits) {
  auto Cleanup = g_OnScopeExitShared / [&] { _Bits = 0; };
  if (_Bits == 0)
    return -1;
  Cleanup = nullptr;
  return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}
namespace NRound335Captured {
template <typename T> int fg_GetLowestBitSetNoZero(T const &);
// A lambda that writes through what an init capture shares, `[pState =
// _pState]`, writes what it points to.
struct CState {
  umint m_Bits;
  unsigned m_nUsers;
};
template <typename T> struct TCSharedPointer {
  struct CData { T *m_pPointTo; } m_Data;
  TCSharedPointer(TCSharedPointer const &_Other) : m_Data(_Other.m_Data) {
    ++m_Data.m_pPointTo->m_nUsers;
  }
  ~TCSharedPointer() {
    if (--m_Data.m_pPointTo->m_nUsers == 0)
      delete m_Data.m_pPointTo;
  }
  T *operator->() const { return m_Data.m_pPointTo; }
};
void fg_Scan(TCSharedPointer<CState> const &_pState) {
  if (!_pState->m_Bits)
    return;
  auto fClear = [pState = _pState] { pState->m_Bits = 0; };
  fClear();
  fg_GetLowestBitSetNoZero(_pState->m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}

namespace NRound336 {
// A signed integer class narrowed keeps its low words; a value less a
// constant of an integer class is zero only when the value is the constant;
// widening a conditional keeps what each value is.
template <typename t_CUpper, typename t_CLower> struct TCInt {
  t_CLower m_Lower{};
  t_CUpper m_Upper{};
  constexpr TCInt(t_CLower _Value = 0, int _Dummy = 0) : m_Lower(_Value) {}
  template <typename tf_CUpper, typename tf_CLower>
  constexpr explicit TCInt(TCInt<tf_CUpper, tf_CLower> const &_Other)
      : m_Lower(t_CLower(_Other.m_Lower)) {}
  constexpr TCInt operator|(TCInt const &_Other) const {
    TCInt Result;
    Result.m_Lower = m_Lower | _Other.m_Lower;
    Result.m_Upper = m_Upper | _Other.m_Upper;
    return Result;
  }
  constexpr TCInt operator-(TCInt const &_Other) const {
    TCInt Result;
    Result.m_Lower = m_Lower - _Other.m_Lower;
    Result.m_Upper = m_Upper;
    return Result;
  }
  constexpr bool operator<=(TCInt const &_Other) const {
    return m_Lower <= _Other.m_Lower;
  }
  constexpr bool operator<(TCInt const &_Other) const {
    return m_Lower < _Other.m_Lower;
  }
  constexpr explicit operator bool() const { return m_Lower || m_Upper; }
};
using uint128 = TCInt<unsigned long long, unsigned long long>;
using int256 = TCInt<__int128, unsigned __int128>;
using uint256 = TCInt<unsigned __int128, unsigned __int128>;
int fg_GetLowestBitSetNoZero(uint128 const &_Bits);
int fg_GetLowestBitSetNoZero(uint256 const &_Bits);
int fg_GetHighestBitSetNoZero(uint256 const &_Bits);
int fg_ScanNarrowed(int256 const &_Bits) {
  return fg_GetLowestBitSetNoZero(uint128(_Bits | int256(1)));
}
int fg_ScanNarrowedUnknown(int256 const &_Bits) {
  return fg_GetLowestBitSetNoZero(uint128(_Bits));
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
int fg_Bucket(uint256 const &_Size) {
  if (_Size <= 1)
    return -1;
  return fg_GetHighestBitSetNoZero(_Size - 1);
}
int fg_BucketWrong(uint256 const &_Size) {
  if (_Size < 1)
    return -1;
  return fg_GetHighestBitSetNoZero(_Size - 1);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
int fg_ScanWidened(uint128 _Bits) {
  return fg_GetLowestBitSetNoZero(uint256(_Bits ? _Bits : 1));
}
int fg_ScanWidenedWrong(uint128 _Bits) {
  return fg_GetLowestBitSetNoZero(uint256(_Bits ? _Bits : 0));
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}
namespace NRound336Callbacks {
int fg_GetLowestBitSetNoZero(umint _Bits);
// Calling an element of a local container runs what it was given, also
// through a range loop; the call operator of a lambda a class takes as its
// own runs its body.
template <typename t_FSignature> struct TCFunction;
template <typename t_CReturn> struct TCFunction<t_CReturn()> {
  void *m_pCallable = nullptr;
  t_CReturn (*m_fCall)(void *) = nullptr;
  TCFunction() = default;
  template <typename tf_FCallable> TCFunction(tf_FCallable &&_fCallable) {}
  t_CReturn operator()() const { return m_fCall(m_pCallable); }
};
template <typename t_CData> struct TCVector {
  t_CData *m_pData = nullptr;
  umint m_nElements = 0;
  template <typename tf_CData> void f_Insert(tf_CData &&_Data) {}
  t_CData &operator[](umint _Index) { return m_pData[_Index]; }
  t_CData *begin() { return m_pData; }
  t_CData *end() { return m_pData + m_nElements; }
};
int fg_ScanLoop(umint _Bits) {
  TCVector<TCFunction<void()>> Work;
  Work.f_Insert([&] { _Bits = 0; });
  if (!_Bits)
    return -1;
  for (auto &fWork : Work)
    fWork();
  return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
int fg_ScanIndexed(umint _Bits) {
  TCVector<TCFunction<void()>> Work;
  Work.f_Insert([&] { _Bits = 0; });
  if (!_Bits)
    return -1;
  Work[0]();
  return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
int fg_ScanOther(umint _Bits, umint _Other) {
  TCVector<TCFunction<void()>> Work;
  Work.f_Insert([&] { _Other = 0; });
  if (!_Bits)
    return -1;
  Work[0]();
  return fg_GetLowestBitSetNoZero(_Bits);
}
template <typename... tp_FFunctors>
struct TCMergedFunctors : tp_FFunctors... {
  using tp_FFunctors::operator()...;
};
template <typename... tfp_FToMerge>
auto fg_MergeFunctors(tfp_FToMerge... p_fToMerge) {
  return TCMergedFunctors<tfp_FToMerge...>{p_fToMerge...};
}
int fg_ScanMerged(umint _Bits) {
  if (!_Bits)
    return -1;
  auto fWork = fg_MergeFunctors([&](int) { _Bits = 0; }, [](char const *) {});
  fWork(1);
  return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
int fg_ScanMergedCopy(umint _Bits) {
  if (!_Bits)
    return -1;
  auto fWork =
      fg_MergeFunctors([_Bits](int) mutable { _Bits = 0; }, [](char const *) {});
  fWork(1);
  return fg_GetLowestBitSetNoZero(_Bits);
}
}

namespace NRound336Bound
{
	// A const member function bound to an object, called through what binds it,
	// doesn't change the object.
	int fg_GetLowestBitSetNoZero(umint _Bits);

	template <typename t_CMember, typename t_CThis>
	struct TCBound
	{
		t_CMember m_pMember;
		t_CThis m_pThis;

		auto operator ()()
		{
			return (m_pThis->*m_pMember)();
		}
	};

	template <typename t_CType>
	struct TCUniquePointer
	{
		t_CType *m_pPointTo = nullptr;

		t_CType *operator ->() const
		{
			return m_pPointTo;
		}

		template <typename t_CMember>
		TCBound<t_CMember, t_CType *> operator ->*(t_CMember const &_pMember) const
		{
			return {_pMember, m_pPointTo};
		}
	};

	struct CState
	{
		umint m_Bits;

		umint f_Read() const
		{
			return m_Bits;
		}
	};

	umint fg_ScanBound(TCUniquePointer<CState> const &_pState)
	{
		if (!_pState->m_Bits)
			return 0;
		auto fRead = _pState->*&CState::f_Read;
		umint Read = fRead();
		return Read + fg_GetLowestBitSetNoZero(_pState->m_Bits);
	}
}

namespace NRound337 {
int fg_GetLowestBitSetNoZero(umint _Bits);
int fg_GetHighestBitSetNoZero(umint _Bits);
// Forcing the deletion of what an owning pointer points to, also through a
// local reference to it, destroys it; a lambda held in a local and moved into
// a guard gives what it captures.
struct CClear {
  umint &m_Bits;
  CClear(umint &_Bits) : m_Bits(_Bits) {}
  ~CClear() { m_Bits = 0; }
};
template <typename t_CType> struct TCOwner {
  t_CType *m_pData = nullptr;
  TCOwner(t_CType *_pData) : m_pData(_pData) {}
  ~TCOwner() { delete m_pData; }
  void f_Clear() {
    delete m_pData;
    m_pData = nullptr;
  }
  void f_ForceDelete() {
    delete m_pData;
    m_pData = nullptr;
  }
};
int fg_ScanForced(umint _Bits) {
  TCOwner<CClear> pClear(new CClear(_Bits));
  if (!_Bits)
    return -1;
  pClear.f_ForceDelete();
  return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
int fg_ScanAlias(umint _Bits) {
  TCOwner<CClear> pClear(new CClear(_Bits));
  auto &pAlias = pClear;
  if (!_Bits)
    return -1;
  pAlias.f_Clear();
  return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
template <typename t_CType> t_CType &&fg_Move(t_CType &_Value) {
  return static_cast<t_CType &&>(_Value);
}
template <typename t_FCleanup> struct TCOnScopeExit {
  t_FCleanup m_fCleanup;
  TCOnScopeExit(t_FCleanup &&_fCleanup) : m_fCleanup(fg_Move(_fCleanup)) {}
  ~TCOnScopeExit() { m_fCleanup(); }
};
int fg_ScanHeld(umint _Bits) {
  auto fClear = [&] { _Bits = 0; };
  TCOwner<TCOnScopeExit<decltype(fClear)>> pCleanup{
      new TCOnScopeExit<decltype(fClear)>(fg_Move(fClear))};
  if (!_Bits)
    return -1;
  pCleanup.f_Clear();
  return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
// A value converted as a constant is, `umint(-1)`, the largest one, and a
// named constant of an integer class is what it is made of.
int fg_ScanBounded(umint _Size) {
  if (_Size == 0)
    return -1;
  if (_Size >= umint(-1))
    return -1;
  return fg_GetHighestBitSetNoZero(_Size - 1);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
template <typename t_CUpper, typename t_CLower> struct TCInt {
  t_CLower m_Lower{};
  t_CUpper m_Upper{};
  constexpr TCInt(t_CLower _Value = 0, int _Dummy = 0) : m_Lower(_Value) {}
  constexpr TCInt operator-(TCInt const &_Other) const {
    TCInt Result;
    Result.m_Lower = m_Lower - _Other.m_Lower;
    Result.m_Upper = m_Upper;
    return Result;
  }
  constexpr bool operator<=(TCInt const &_Other) const {
    return m_Lower <= _Other.m_Lower;
  }
  struct CConstants;
};
using uint256 = TCInt<unsigned __int128, unsigned __int128>;
template <typename t_CUpper, typename t_CLower>
struct TCInt<t_CUpper, t_CLower>::CConstants {
  static constexpr TCInt mc_One = TCInt(1);
};
int fg_GetHighestBitSetNoZero(uint256 const &_Bits);
int fg_Bucket(uint256 const &_Size) {
  if (_Size <= uint256::CConstants::mc_One)
    return -1;
  return fg_GetHighestBitSetNoZero(_Size - uint256::CConstants::mc_One);
}
// An iterator of a map is at any key, whatever its position, and a reference
// bound to an element with a temporary key refers to it.
template <typename t_CKey, typename t_CValue> struct TCMap {
  struct CNode {
    t_CKey m_Key;
    t_CValue m_Value;
  };
  CNode *m_pNodes;
  struct CIterator {
    CNode *m_pNode;
    t_CValue &operator*() const { return m_pNode->m_Value; }
  };
  CIterator f_GetIterator() { return {m_pNodes}; }
  template <typename tf_CKey> CNode *fp_Find(tf_CKey const &_Key) const;
  template <typename tf_CKey> t_CValue &operator[](tf_CKey &&_Key) {
    return fp_Find(_Key)->m_Value;
  }
  template <typename tf_CKey> t_CValue const &operator[](tf_CKey &&_Key) const {
    return fp_Find(_Key)->m_Value;
  }
};
int fg_ScanKey(TCMap<int, umint> &_Bits) {
  auto iValue = _Bits.f_GetIterator();
  auto const &Bits = _Bits;
  if (!Bits[7])
    return -1;
  *iValue = 0;
  return fg_GetLowestBitSetNoZero(Bits[7]);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
void fg_ScanBound(TCMap<umint, umint> &_Masks) {
  umint &Bits = _Masks[umint(1)];
  if (Bits) {
    _Masks[umint(1)] = 0;
    fg_GetLowestBitSetNoZero(Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
  }
}
}


namespace NRound338 {
int fg_GetLowestBitSetNoZero(umint _Bits);
template <typename t_CType> t_CType &&fg_Forward(t_CType &_Value) {
  return static_cast<t_CType &&>(_Value);
}
template <typename t_CType> t_CType &&fg_Move(t_CType &_Value) {
  return static_cast<t_CType &&>(_Value);
}
// An index operator that inserts what it is asked for, given the key it
// forwards to the lambda that makes a node, gives the element it checked.
struct CNode {
  int m_Key;
  umint m_Value;
  CNode(int _Key) : m_Key(_Key), m_Value(0) {}
};
struct CTree {
  CNode *m_pRoot = nullptr;
  template <typename tf_FMake>
  CNode *f_FindEqualOrInsert(int const &_Key, tf_FMake &&_fMake) {
    if (m_pRoot && m_pRoot->m_Key == _Key)
      return m_pRoot;
    m_pRoot = _fMake();
    return m_pRoot;
  }
};
struct CMap {
  CTree m_Tree;
  template <typename tf_CKey> umint &operator[](tf_CKey &&_Key) {
    auto fMake = [&]() -> CNode * {
      return new CNode(fg_Forward<tf_CKey>(_Key));
    };
    return m_Tree.f_FindEqualOrInsert(_Key, fMake)->m_Value;
  }
};
int fg_FirstBit(CMap &_Masks, int _Key) {
  if (_Masks[_Key] != 0)
    return fg_GetLowestBitSetNoZero(_Masks[_Key]);
  return -1;
}
// Not one whose lambda writes the key it is given.
struct CChangingMap {
  CTree m_Tree;
  umint &operator[](int &_Key) {
    auto fMake = [&]() -> CNode * { return new CNode(++_Key); };
    return m_Tree.f_FindEqualOrInsert(_Key, fMake)->m_Value;
  }
};
int fg_FirstBitChanging(CChangingMap &_Masks, int _Key) {
  if (_Masks[_Key] != 0)
    return fg_GetLowestBitSetNoZero(_Masks[_Key]);
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
  return -1;
}
// What an optional holds runs what it was given.
template <typename t_FSignature> struct TCFunction;
template <typename t_CReturn> struct TCFunction<t_CReturn()> {
  void *m_pCallable = nullptr;
  t_CReturn (*m_fCall)(void *) = nullptr;
  TCFunction() = default;
  template <typename tf_FCallable> TCFunction(tf_FCallable &&_fCallable) {}
  t_CReturn operator()() const { return m_fCall(m_pCallable); }
};
struct CStorage {
  CStorage();
  CStorage(CStorage const &_Other);
  CStorage(CStorage &_Other);
};
template <typename t_CType> struct TCOptional : CStorage {
  t_CType m_Value;
  TCOptional() = default;
  TCOptional(TCOptional const &) = default;
  TCOptional(TCOptional &) = default;
  template <typename tf_CType> TCOptional &operator=(tf_CType &&_Value) {
    return *this;
  }
  t_CType &operator*() { return m_Value; }
};
int fg_ScanOptional(umint _Bits) {
  TCOptional<TCFunction<void()>> fClear;
  fClear = [&] { _Bits = 0; };
  if (!_Bits)
    return -1;
  (*fClear)();
  return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
// A write on a branch that returns doesn't reach the scan.
int fg_FindOrClear(umint &o_Bits, bool _bClear) {
  if (!o_Bits)
    return -1;
  if (_bClear) {
    o_Bits = 0;
    return -1;
  }
  return fg_GetLowestBitSetNoZero(o_Bits);
}
// Moving what owns it leaves what it points to.
struct CState {
  umint m_Bits;
};
template <typename t_CType> struct TCUniquePointer {
  t_CType *m_pData = nullptr;
  TCUniquePointer(TCUniquePointer &&_Other) : m_pData(_Other.m_pData) {
    _Other.m_pData = nullptr;
  }
  t_CType *operator->() const { return m_pData; }
};
int fg_ScanMoved(TCUniquePointer<CState> &&_pState) {
  umint const &Bits = _pState->m_Bits;
  if (!Bits)
    return -1;
  auto pOwned = fg_Move(_pState);
  return fg_GetLowestBitSetNoZero(Bits);
}
// A helper that writes through what a const getter gives.
template <typename t_CType> struct TCSharedPointer {
  t_CType *m_pData;
  t_CType *f_Get() const { return m_pData; }
  t_CType *operator->() const { return m_pData; }
};
void fg_Clear(TCSharedPointer<CState> &_pState) { _pState.f_Get()->m_Bits = 0; }
int fg_ScanShared(TCSharedPointer<CState> &_pState) {
  if (!_pState->m_Bits)
    return -1;
  fg_Clear(_pState);
  return fg_GetLowestBitSetNoZero(_pState->m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
// A copy of an optional leaves it.
int fg_ScanCopied(TCOptional<umint> &_Bits) {
  if (!*_Bits)
    return -1;
  auto Copy = _Bits;
  return fg_GetLowestBitSetNoZero(*_Bits);
}
}

namespace NRound339 {
using uint8 = unsigned char;
int fg_GetHighestBitSetNoZero(uint8 const &_Value);
int fg_GetLowestBitSetNoZero(umint _Bits);
// A difference narrowed is zero also at other values than the constant it
// is less.
int fg_LastBucket(umint _Size) {
  if (_Size <= 1)
    return -1;
  return fg_GetHighestBitSetNoZero(uint8(_Size - 1));
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
// What an optional holds runs what it was given also through a getter, and
// so does a member of a local made with one.
template <typename t_FSignature> struct TCFunction;
template <typename t_CReturn> struct TCFunction<t_CReturn()> {
  void *m_pCallable = nullptr;
  t_CReturn (*m_fCall)(void *) = nullptr;
  TCFunction() = default;
  template <typename tf_FCallable> TCFunction(tf_FCallable &&_fCallable) {}
  t_CReturn operator()() const { return m_fCall(m_pCallable); }
};
template <typename t_CType> struct TCOptional {
  t_CType m_Value;
  template <typename tf_CType> TCOptional &operator=(tf_CType &&_Value) {
    return *this;
  }
  t_CType &f_Get() { return m_Value; }
};
int fg_ScanGetter(umint _Bits) {
  TCOptional<TCFunction<void()>> fClear;
  fClear = [&] { _Bits = 0; };
  if (!_Bits)
    return -1;
  fClear.f_Get()();
  return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
struct CState {
  TCFunction<void()> m_fClear;
};
int fg_ScanMember(umint _Bits) {
  if (!_Bits)
    return -1;
  CState State{[&] { _Bits = 0; }};
  State.m_fClear();
  return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
// Writing the element of another key of a map leaves the one checked.
struct CMap {
  umint &operator[](int _Key);
};
int fg_ScanKey(CMap &_Masks) {
  if (_Masks[0]) {
    _Masks[1] = 0;
    return fg_GetLowestBitSetNoZero(_Masks[0]);
  }
  return -1;
}
int fg_ScanSameKey(CMap &_Masks) {
  if (_Masks[0]) {
    _Masks[0] = 0;
    return fg_GetLowestBitSetNoZero(_Masks[0]);
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
  }
  return -1;
}
// A countdown of an integer class tests what its decrement gives, and a
// constant of words is the integer they make.
template <typename t_CUpper, typename t_CLower> struct TCInt {
  t_CLower m_Lower{};
  t_CUpper m_Upper{};
  constexpr TCInt(unsigned _Value = 0) : m_Lower(_Value) {}
  constexpr TCInt(t_CUpper _Upper, t_CLower _Lower)
      : m_Lower(_Lower), m_Upper(_Upper) {}
  TCInt &operator--();
  explicit operator bool() const;
  bool operator<=(TCInt const &_Right) const;
  TCInt operator-(TCInt const &_Right) const;
};
using uint256 = TCInt<unsigned __int128, unsigned __int128>;
int fg_GetHighestBitSetNoZero(uint256 const &_Bits);
void fg_ScanCountdown(uint256 _Value) {
  if (!_Value)
    return;
  do {
    fg_GetHighestBitSetNoZero(_Value);
  } while (--_Value);
}
int fg_ScanWords(uint256 const &_Size) {
  constexpr uint256 c_MinSize(1, 0);
  if (_Size <= c_MinSize)
    return -1;
  return fg_GetHighestBitSetNoZero(_Size - c_MinSize);
}
int fg_ScanOtherWords(uint256 const &_Size) {
  constexpr uint256 c_MinSize(1, 0);
  if (_Size <= uint256(0, 1))
    return -1;
  return fg_GetHighestBitSetNoZero(_Size - c_MinSize);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}

namespace NRound339Converted {
int fg_GetLowestBitSetNoZero(umint _Bits);
// A smart pointer converted from another of its template shares what it
// points to, like a copy.
struct CBase {
  umint m_Bits;
};
struct CDerived : CBase {};
template <typename t_CType> struct TCSharedPointer {
  t_CType *m_pPointTo;
  TCSharedPointer(TCSharedPointer const &_Other)
      : m_pPointTo(_Other.m_pPointTo) {}
  template <typename tf_CType>
  TCSharedPointer(TCSharedPointer<tf_CType> const &_Other)
      : m_pPointTo(_Other.m_pPointTo) {}
  t_CType *operator->() const { return m_pPointTo; }
  // Calls what it points to, which doesn't make it a function wrapper.
  template <typename... tf_CParams>
  auto operator()(tf_CParams &&...p_Params) const {
    return (*m_pPointTo)(static_cast<tf_CParams &&>(p_Params)...);
  }
};
int fg_ScanConverted(TCSharedPointer<CDerived> const &_pState) {
  if (!_pState->m_Bits)
    return -1;
  TCSharedPointer<CBase> pBase = _pState;
  pBase->m_Bits = 0;
  return fg_GetLowestBitSetNoZero(_pState->m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
int fg_ScanCopy(TCSharedPointer<CDerived> const &_pState) {
  if (!_pState->m_Bits)
    return -1;
  TCSharedPointer<CDerived> pCopy = _pState;
  pCopy->m_Bits = 0;
  return fg_GetLowestBitSetNoZero(_pState->m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}

namespace NRound340 {
int fg_GetLowestBitSetNoZero(umint _Bits);
template <typename t_CType> t_CType &&fg_Move(t_CType &_Value) {
  return static_cast<t_CType &&>(_Value);
}
struct CState {
  umint m_Bits;
  unsigned m_nRefs;
};
template <typename t_CType> struct TCSharedPointer {
  t_CType *m_pData;
  TCSharedPointer(TCSharedPointer const &_Other) : m_pData(_Other.m_pData) {
    ++m_pData->m_nRefs;
  }
  TCSharedPointer(TCSharedPointer &&_Other) : m_pData(_Other.m_pData) {
    _Other.m_pData = nullptr;
  }
  ~TCSharedPointer() {
    if (m_pData && --m_pData->m_nRefs == 0)
      delete m_pData;
  }
  t_CType *operator->() const { return m_pData; }
};
// A pointer moved from a copy shares what it points to.
int fg_ScanMoved(TCSharedPointer<CState> const &_pState) {
  auto pCopy = _pState;
  auto pMoved = fg_Move(pCopy);
  if (!_pState->m_Bits)
    return -1;
  pMoved->m_Bits = 0;
  return fg_GetLowestBitSetNoZero(_pState->m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
// So does one moved on to a function that writes what it points to.
void fg_Consume(TCSharedPointer<CState> &&_pState) {
  auto pState = fg_Move(_pState);
  pState->m_Bits = 0;
}
int fg_ScanConsumed(TCSharedPointer<CState> const &_pState) {
  auto pCopy = _pState;
  if (!_pState->m_Bits)
    return -1;
  fg_Consume(fg_Move(pCopy));
  return fg_GetLowestBitSetNoZero(_pState->m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
// Destroying a copy made of one that keeps what it points to only gives up
// its share.
void fg_ScanReleased(TCSharedPointer<CState> const &_pState) {
  if (_pState->m_Bits) {
    { auto pCopy = _pState; }
    fg_GetLowestBitSetNoZero(_pState->m_Bits);
  }
}
// A copy of a vector of vectors copies them, but shares what a pointer in
// an element points to.
template <typename t_CType> struct TCVector {
  t_CType *m_pData;
  TCVector(TCVector const &_Other);
  t_CType *f_GetArray() const { return m_pData; }
  t_CType &operator[](umint _Index) { return f_GetArray()[_Index]; }
  t_CType const &operator[](umint _Index) const {
    return f_GetArray()[_Index];
  }
};
int fg_ScanNested(TCVector<TCVector<umint>> const &_Bits) {
  auto Copy = _Bits;
  if (!_Bits[0][0])
    return -1;
  Copy[0][0] = 0;
  return fg_GetLowestBitSetNoZero(_Bits[0][0]);
}
struct CHolder {
  umint *m_pBits;
};
int fg_ScanHolder(TCVector<CHolder> const &_Bits) {
  auto Copy = _Bits;
  if (!_Bits[0].m_pBits[0])
    return -1;
  Copy[0].m_pBits[0] = 0;
  return fg_GetLowestBitSetNoZero(_Bits[0].m_pBits[0]);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}
namespace NRound340Integers {
// A constant of an integer class is what the class converts it to,
// `uint256(-1)`, the largest.
template <typename t_CUpper, typename t_CLower> struct TCInt {
  t_CLower m_Lower{};
  t_CUpper m_Upper{};
  constexpr TCInt(int _Value)
      : m_Lower(t_CLower(_Value)), m_Upper(_Value < 0 ? t_CUpper(-1) : 0) {}
  bool operator>(TCInt const &_Right) const;
  TCInt operator-(TCInt const &_Right) const;
  explicit operator bool() const;
};
using uint256 = TCInt<unsigned __int128, unsigned __int128>;
using int256 = TCInt<__int128, unsigned __int128>;
int fg_GetLowestBitSetNoZero(uint256 const &_Bits);
int fg_GetLowestBitSetNoZero(int256 const &_Bits);
int fg_ScanLargest(uint256 const &_Bits) {
  if (_Bits > uint256(1))
    return fg_GetLowestBitSetNoZero(_Bits - uint256(-1));
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
  return -1;
}
// The only member of a wrapper of a signed integer class is its value.
template <typename t_CType> struct TCAutoClear {
  t_CType m_Value;
  operator t_CType const &() const { return m_Value; }
  bool operator==(int _Right) const;
};
using zint256 = TCAutoClear<int256>;
int fg_FirstBit(zint256 const &_Bits) {
  if (_Bits == 0)
    return -1;
  return fg_GetLowestBitSetNoZero(int256(_Bits));
}
}

namespace NRound341 {
int fg_GetLowestBitSetNoZero(umint _Bits);
template <typename t_CType> t_CType &&fg_Move(t_CType &_Value) {
  return static_cast<t_CType &&>(_Value);
}
// A copy of its own of what can't be copied as it is shares nothing, also
// where it is destroyed.
template <typename t_CType> struct TCCopy {
  t_CType const &m_Source;
};
template <typename t_CType> TCCopy<t_CType> fg_Copy(t_CType const &_Source) {
  return {_Source};
}
struct CState {
  umint m_Bits;
};
template <typename t_CType> struct TCUniquePointer {
  t_CType *m_pData;
  TCUniquePointer(TCUniquePointer const &) = delete;
  TCUniquePointer(TCUniquePointer &&_Other);
  TCUniquePointer(TCCopy<TCUniquePointer> &&_Copy)
      : m_pData(new t_CType(*_Copy.m_Source.m_pData)) {}
  ~TCUniquePointer() { delete m_pData; }
  t_CType *operator->() const { return m_pData; }
};
int fg_ScanOwn(TCUniquePointer<CState> const &_pState) {
  TCUniquePointer<CState> pCopy(fg_Copy(_pState));
  if (!_pState->m_Bits)
    return -1;
  pCopy->m_Bits = 0;
  return fg_GetLowestBitSetNoZero(_pState->m_Bits);
}
// An iterator of a map is at any key, also one only known where it runs,
// and a reverse one too.
template <typename t_CKey, typename t_CValue> struct TCMap {
  struct CIterator {
    t_CValue *m_pValue;
    t_CValue &operator*() const { return *m_pValue; }
  };
  CIterator f_GetIterator();
  CIterator f_GetIteratorReverse();
  t_CValue &operator[](t_CKey const &_Key);
};
int fg_ScanKey(TCMap<int, umint> &_Masks, int _Key) {
  auto iMask = _Masks.f_GetIterator();
  if (!_Masks[_Key])
    return -1;
  *iMask = 0;
  return fg_GetLowestBitSetNoZero(_Masks[_Key]);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
int fg_ScanReverse(TCMap<int, umint> &_Masks) {
  auto iMask = _Masks.f_GetIteratorReverse();
  if (!_Masks[1])
    return -1;
  *iMask = 0;
  return fg_GetLowestBitSetNoZero(_Masks[1]);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
// A pointer element of a map assigned what is checked writes it.
int fg_ScanPointers(umint _Bits) {
  TCMap<umint, umint *> Pointers;
  Pointers[umint(0)] = &_Bits;
  if (!_Bits)
    return -1;
  *Pointers[umint(0)] = 0;
  return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
// Writing an element of a copy at an index doesn't write the index.
template <typename t_CType> struct TCVector {
  t_CType *m_pData;
  TCVector(TCVector const &_Other);
  t_CType *f_GetArray() const { return m_pData; }
  t_CType &operator[](umint _Index) { return f_GetArray()[_Index]; }
  t_CType const &operator[](umint _Index) const {
    return f_GetArray()[_Index];
  }
};
int fg_ScanIndex(TCVector<umint> const &_Bits, umint _Index) {
  if (!_Bits[_Index])
    return -1;
  auto Copy = _Bits;
  Copy[_Index] = 0;
  return fg_GetLowestBitSetNoZero(_Bits[_Index]);
}
// A copy of a vector of pointers shares what they point to.
int fg_ScanPointed(TCVector<umint *> const &_Bits) {
  auto Copy = _Bits;
  if (!*_Bits[0])
    return -1;
  *Copy[0] = 0;
  return fg_GetLowestBitSetNoZero(*_Bits[0]);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
// A member function that can't change its object, bound to it, doesn't.
struct CReader {
  umint m_Bits;
  umint f_Read() const { return m_Bits; }
};
template <typename t_FMethod, typename t_CObject> struct TCMemberFunctor {
  t_FMethod m_fMethod;
  t_CObject *m_pObject;
  auto operator()() { return (m_pObject->*m_fMethod)(); }
};
template <typename t_FMethod, typename t_CObject>
TCMemberFunctor<t_FMethod, t_CObject>
fg_MemberFunctionFunctor(t_FMethod _fMethod, t_CObject *_pObject) {
  return {_fMethod, _pObject};
}
int fg_ScanBound(CReader &_Reader) {
  auto fRead = fg_MemberFunctionFunctor(&CReader::f_Read, &_Reader);
  if (!_Reader.m_Bits)
    return -1;
  fRead();
  return fg_GetLowestBitSetNoZero(_Reader.m_Bits);
}
}
namespace NRound341Shared {
int fg_GetLowestBitSetNoZero(umint _Bits);
// A smart pointer shared as const points to what the original does.
struct CState {
  umint m_Bits;
};
template <typename t_CType> struct TCSharedPointer {
  t_CType *m_pData;
  TCSharedPointer(t_CType *_pData) : m_pData(_pData) {}
  TCSharedPointer(TCSharedPointer const &_Other) : m_pData(_Other.m_pData) {}
  TCSharedPointer<t_CType const> f_ShareAsConst() const {
    return TCSharedPointer<t_CType const>(m_pData);
  }
  t_CType *operator->() const { return m_pData; }
};
int fg_ScanConst(TCSharedPointer<CState> const &_pState) {
  auto pConst = _pState.f_ShareAsConst();
  if (!pConst->m_Bits)
    return -1;
  _pState->m_Bits = 0;
  return fg_GetLowestBitSetNoZero(pConst->m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
// A copy of a map of vectors, whose index operator is a template, copies
// them.
template <typename t_CType> struct TCVector {
  t_CType *m_pData;
  TCVector(TCVector const &_Other);
  t_CType *f_GetArray() const { return m_pData; }
  t_CType &operator[](umint _Index) { return f_GetArray()[_Index]; }
  t_CType const &operator[](umint _Index) const {
    return f_GetArray()[_Index];
  }
};
template <typename t_CKey, typename t_CValue> struct TCMap {
  t_CValue *m_pValues;
  TCMap(TCMap const &_Other);
  t_CValue *f_GetValues() const { return m_pValues; }
  template <typename tf_CKey> t_CValue &operator[](tf_CKey &&_Key) {
    return f_GetValues()[0];
  }
  template <typename tf_CKey>
  t_CValue const &operator[](tf_CKey &&_Key) const {
    return f_GetValues()[0];
  }
};
int fg_ScanNested(TCMap<umint, TCVector<umint>> const &_Masks) {
  auto Copy = _Masks;
  if (!_Masks[umint(0)][0])
    return -1;
  Copy[umint(0)][0] = 0;
  return fg_GetLowestBitSetNoZero(_Masks[umint(0)][0]);
}
}

namespace NRound342Capture {
int fg_GetHighestBitSetNoZero(umint const &_Bits);
// A lambda that keeps a cleanup it captures runs it where it is destroyed.
// Reduced TCOnScopeExit destructor behavior.
template <typename t_FCleanup> struct TCOnScopeExit {
  t_FCleanup m_fCleanup;
  ~TCOnScopeExit() { m_fCleanup(); }
};

int fg_Scan(umint _Bits) {
  if (!_Bits)
    return -1;
  {
    auto fKeepAlive =
        [Cleanup = TCOnScopeExit{[&] { _Bits = 0; }}] {};
  }
  return fg_GetHighestBitSetNoZero(_Bits); 
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}
namespace NRound342ByReference {
int fg_GetLowestBitSetNoZero(umint _Value);
// A pointer captured by reference is what it points to where the lambda is
// called.
int fg_Scan(umint _Bits)
{
    umint *pBits = nullptr;
    auto fClear = [&pBits] { *pBits = 0; };
    pBits = &_Bits;
    if (!_Bits)
        return -1;
    fClear();
    return fg_GetLowestBitSetNoZero(_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}
namespace NRound342Weak {
int fg_GetHighestBitSetNoZero(umint const &_Bits);
// What a weak pointer a shared pointer gives locks to is what that points to.
struct CState { umint m_Bits; };
template <typename t_CType> struct TCSharedPointer;
template <typename t_CType> struct TCWeakPointer {
  t_CType *m_pPointTo;
  TCWeakPointer(TCSharedPointer<t_CType> const &_Shared) : m_pPointTo(_Shared.m_pPointTo) {}
  TCSharedPointer<t_CType> f_Lock() const;
};
template <typename t_CType> struct TCSharedPointer {
  t_CType *m_pPointTo;
  TCSharedPointer(t_CType *_p) : m_pPointTo(_p) {}
  TCSharedPointer(TCSharedPointer const &_Other) : m_pPointTo(_Other.m_pPointTo) {}
  using CWeakPointer = TCWeakPointer<t_CType>;
  CWeakPointer f_Weak() const { return *this; }
  t_CType *operator->() const { return m_pPointTo; }
};
template <typename t_CType>
TCSharedPointer<t_CType> TCWeakPointer<t_CType>::f_Lock() const { return TCSharedPointer<t_CType>(m_pPointTo); }
int fg_Scan(TCSharedPointer<CState> const &_pState) {
  if (!_pState->m_Bits)
    return -1;
  auto pWeak = _pState.f_Weak();
  auto pLocked = pWeak.f_Lock();
  pLocked->m_Bits = 0;
  return fg_GetHighestBitSetNoZero(_pState->m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}
namespace NRound343WeakOptions {
int fg_GetHighestBitSetNoZero(umint const &_Bits);
// Also where the weak pointer gives a shared pointer of other options, like
// `TCSharedPointer<CState, CSupportWeakTag, CAllocator>` of a
// `TCSharedPointer<CState, CSupportWeakTag>`.
struct CState { umint m_Bits; };
struct CSupportWeakTag {};
struct CAllocator {};
template <typename t_CType, typename... tp_COptions> struct TCSharedPointer;
template <typename t_CType, typename... tp_COptions> struct TCWeakPointer {
  t_CType *m_pPointTo;
  template <typename... tp_COptions2>
  TCWeakPointer(TCSharedPointer<t_CType, tp_COptions2...> const &_Shared) : m_pPointTo(_Shared.m_pPointTo) {}
  TCSharedPointer<t_CType, CSupportWeakTag, tp_COptions...> f_Lock() const;
};
template <typename t_CType, typename... tp_COptions> struct TCSharedPointer {
  t_CType *m_pPointTo;
  TCSharedPointer(t_CType *_p) : m_pPointTo(_p) {}
  TCSharedPointer(TCSharedPointer const &_Other) : m_pPointTo(_Other.m_pPointTo) {}
  using CWeakPointer = TCWeakPointer<t_CType, CAllocator>;
  CWeakPointer f_Weak() const { return *this; }
  t_CType *operator->() const { return m_pPointTo; }
};
template <typename t_CType, typename... tp_COptions>
TCSharedPointer<t_CType, CSupportWeakTag, tp_COptions...> TCWeakPointer<t_CType, tp_COptions...>::f_Lock() const { return TCSharedPointer<t_CType, CSupportWeakTag, tp_COptions...>(m_pPointTo); }
int fg_Scan(TCSharedPointer<CState, CSupportWeakTag> const &_pState) {
  if (!_pState->m_Bits)
    return -1;
  auto pWeak = _pState.f_Weak();
  auto pLocked = pWeak.f_Lock();
  pLocked->m_Bits = 0;
  return fg_GetHighestBitSetNoZero(_pState->m_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}
namespace NRound342TypedKeys {
int fg_GetLowestBitSetNoZero(umint _Bits);
// Keys written as converted constants, also named, are told apart.
template <typename t_CKey, typename t_CValue> struct TCMap {
  t_CValue &operator[](t_CKey const &_Key);
};
constexpr umint c_First = 0;
constexpr umint c_Second = 1;
void fg_Scan(TCMap<umint, umint> &_Masks) {
  if (!_Masks[umint(0)])
    return;
  _Masks[umint(1)] = 0;
  fg_GetLowestBitSetNoZero(_Masks[umint(0)]);
}
void fg_ScanNamed(TCMap<umint, umint> &_Masks) {
  if (!_Masks[c_First])
    return;
  _Masks[c_Second] = 0;
  fg_GetLowestBitSetNoZero(_Masks[c_First]);
}
void fg_ScanSame(TCMap<umint, umint> &_Masks) {
  if (!_Masks[umint(0)])
    return;
  _Masks[c_First] = 0;
  fg_GetLowestBitSetNoZero(_Masks[umint(0)]);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
}

// A helper called on many paths, `fg_Work<N - 1>(); fg_Work<N - 1>();`, is
// looked into once for what is looked for, not once per path, whose number
// grows tenfold with each level.
namespace NRound359RepeatedHelpers {
umint g_Bits;
#define DRepeat10(_Call) _Call; _Call; _Call; _Call; _Call; _Call; _Call; _Call; _Call; _Call
template <int t_Level> void fg_Work() {
  if constexpr (t_Level > 0) {
    DRepeat10(fg_Work<t_Level - 1>());
  }
}
template <int t_Level> void fg_Clear() {
  if constexpr (t_Level > 0) {
    DRepeat10(fg_Clear<t_Level - 1>());
  } else {
    g_Bits = 0;
  }
}
int fg_Scan() {
  if (g_Bits) {
    fg_Work<7>();
    return fg_GetHighestBitSetNoZero(g_Bits);
  }
  return 0;
}
int fg_ScanCleared() {
  if (g_Bits) {
    fg_Clear<6>();
    return fg_GetHighestBitSetNoZero(g_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
  }
  return 0;
}
#undef DRepeat10
}
