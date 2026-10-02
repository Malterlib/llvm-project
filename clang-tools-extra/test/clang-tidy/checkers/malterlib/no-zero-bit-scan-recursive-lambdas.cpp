// RUN: %check_clang_tidy -std=c++20-or-later %s malterlib-no-zero-bit-scan %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}"

// A lambda that calls itself, given itself as an argument, is looked into
// once: reached again, it writes what its other calls do.

unsigned fg_GetHighestBitSetNoZero(unsigned _Value);

// A recursive generic lambda a factory gives back.
inline auto fg_MakeRecursive()
{
	return [](auto const &_fSelf, bool _bAgain) -> void
	{
		if (_bAgain)
			_fSelf(_fSelf, false);
	};
}

void fg_Factory(unsigned _Bits)
{
	auto fRecursive = fg_MakeRecursive();
	if (_Bits)
	{
		fRecursive(fRecursive, true);
		(void)fg_GetHighestBitSetNoZero(_Bits);
	}
}

// One that writes what is checked, through the reference it captures, still
// does when it is reached again.
void fg_FactoryWrites(unsigned _Bits)
{
	auto fClear = [&_Bits](auto const &_fSelf, bool _bAgain) -> void
	{
		if (_bAgain)
			_fSelf(_fSelf, false);
		_Bits = 0;
	};
	if (_Bits)
	{
		fClear(fClear, true);
		(void)fg_GetHighestBitSetNoZero(_Bits);
		// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
	}
}

// A recursive generic lambda given as a callback to the function of the scan.
template <typename t_FCallback>
unsigned fg_CallBack(unsigned _Bits, t_FCallback const &_fCallback)
{
	if (!_Bits)
		return 0;
	_fCallback(_fCallback, 3u);
	return fg_GetHighestBitSetNoZero(_Bits);
}

void fg_Callback(unsigned _Bits)
{
	fg_CallBack(_Bits, [](auto const &_fSelf, unsigned _Count) -> void
		{
			if (_Count)
				_fSelf(_fSelf, _Count - 1);
		}
	);
}

// The scan of what isn't checked is still reported.
void fg_Unchecked(unsigned _Bits)
{
	auto fRecursive = fg_MakeRecursive();
	fRecursive(fRecursive, true);
	(void)fg_GetHighestBitSetNoZero(_Bits);
	// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
