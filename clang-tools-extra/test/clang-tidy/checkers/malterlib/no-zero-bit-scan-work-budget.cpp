// RUN: clang-tidy --checks='-*,malterlib-no-zero-bit-scan' --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml', malterlib-no-zero-bit-scan.Functions: 'fg_Scan'}}" %s -- -std=c++20 2>&1 | FileCheck --check-prefixes=CHECK,FULL %s
// RUN: clang-tidy --checks='-*,malterlib-no-zero-bit-scan' --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml', malterlib-no-zero-bit-scan.Functions: 'fg_Scan', malterlib.WorkReport: true, malterlib-no-zero-bit-scan.WorkLimit: 145}}" %s -- -std=c++20 -DCLEARS_THROUGH_POINTER_ONLY 2>&1 | FileCheck --check-prefix=SPENT %s
// RUN: clang-tidy --checks='-*,malterlib-no-zero-bit-scan' --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml', malterlib-no-zero-bit-scan.Functions: 'fg_Scan', malterlib.WorkReport: true, malterlib-no-zero-bit-scan.WorkLimit: 1}}" %s -- -std=c++20 2>&1 | FileCheck --check-prefix=EXHAUSTED %s
// EXHAUSTED-NOT: [malterlib-
// CHECK-NOT: [malterlib-
// EXHAUSTED: malterlib work: malterlib-no-zero-bit-scan roots {{[0-9]+}} exhausted {{[1-9][0-9]*}} largest 1 limit 1

// The work budget (WorkBudget.h): what the analysis of a scan can't finish,
// like a walk that comes back to where it started through a function object,
// runs out of steps, and the scan reports nothing; the next scan is analyzed
// from scratch. With a limit of one step (the last RUN line), every scan runs
// out and nothing is reported.

unsigned fg_Scan(unsigned _Value);

#ifndef CLEARS_THROUGH_POINTER_ONLY

template <typename t_CSignature>
struct TCFunction;

template <typename t_CReturn, typename... tp_CParams>
struct TCFunction<t_CReturn (tp_CParams...)>
{
	template <typename t_CFunctor>
	TCFunction(t_CFunctor _fFunctor)
	{
	}
	t_CReturn operator () (tp_CParams... p_Params) const;
};

// A recursive lambda held in a function object, in a const member function
// called between the check and the scan (round 227 #2).
struct CState
{
	mutable unsigned m_Queries = 0;
	void f_Work() const
	{
		TCFunction<void (unsigned)> fRecurse = [&](unsigned _n)
		{
			if (_n)
				fRecurse(_n - 1);
		};
		fRecurse(1);
	}
};

unsigned fg_ConstMethod(unsigned _Value, CState const &_State)
{
	if (!_Value)
		return 0;
	_State.f_Work();
	return fg_Scan(_Value);
}

// One that returns a reference, assigned between the check and the scan
// (round 227 #3).
unsigned fg_ReferenceResult(unsigned _Value)
{
	TCFunction<unsigned &(unsigned)> fRecurse = [&](unsigned _n) -> unsigned &
	{
		if (_n)
			return fRecurse(_n - 1);
		return _Value;
	};
	if (!_Value)
		return 0;
	fRecurse(1) = 0;
	return fg_Scan(_Value);
}

// A cursor that walks a tree (round 227 #4): copied two ways from what it
// reaches itself, what the scan reads through it can't be told, and the scan
// is taken as checked; copied one way, it is followed as before.
struct CNode;

struct CCursor
{
	CNode *m_pNode;
};

struct CNode
{
	CCursor m_Left;
	CCursor m_Right;
	unsigned m_Bits;
};

unsigned fg_TwoWays(CCursor _Cursor)
{
	if (!_Cursor.m_pNode->m_Bits)
		return 0;
	_Cursor = _Cursor.m_pNode->m_Left;
	_Cursor = _Cursor.m_pNode->m_Right;
	return fg_Scan(_Cursor.m_pNode->m_Bits);
}

unsigned fg_TwoWaysChecked(CCursor _Cursor)
{
	_Cursor = _Cursor.m_pNode->m_Left;
	_Cursor = _Cursor.m_pNode->m_Right;
	if (_Cursor.m_pNode->m_Bits)
		return fg_Scan(_Cursor.m_pNode->m_Bits);
	return 0;
}

// CHECK-NOT: [malterlib-
unsigned fg_OneWay(CCursor _Cursor)
{
	if (!_Cursor.m_pNode->m_Bits)
		return 0;
	_Cursor = _Cursor.m_pNode->m_Left;
	return fg_Scan(_Cursor.m_pNode->m_Bits);
	// CHECK: :[[@LINE-1]]:9: warning: the argument of 'fg_Scan' is not checked for zero first; check it [malterlib-no-zero-bit-scan]
}

// Lambdas that each call the one before three times, between the check and
// the scan: 3^19 walks of the same bodies.
unsigned fg_LambdaChain(unsigned _Value)
{
	auto f0 = [&](unsigned &_Bits) { (void)_Bits; };
	auto f1 = [&](unsigned &_Bits) { f0(_Bits); f0(_Bits); f0(_Bits); };
	auto f2 = [&](unsigned &_Bits) { f1(_Bits); f1(_Bits); f1(_Bits); };
	auto f3 = [&](unsigned &_Bits) { f2(_Bits); f2(_Bits); f2(_Bits); };
	auto f4 = [&](unsigned &_Bits) { f3(_Bits); f3(_Bits); f3(_Bits); };
	auto f5 = [&](unsigned &_Bits) { f4(_Bits); f4(_Bits); f4(_Bits); };
	auto f6 = [&](unsigned &_Bits) { f5(_Bits); f5(_Bits); f5(_Bits); };
	auto f7 = [&](unsigned &_Bits) { f6(_Bits); f6(_Bits); f6(_Bits); };
	auto f8 = [&](unsigned &_Bits) { f7(_Bits); f7(_Bits); f7(_Bits); };
	auto f9 = [&](unsigned &_Bits) { f8(_Bits); f8(_Bits); f8(_Bits); };
	auto f10 = [&](unsigned &_Bits) { f9(_Bits); f9(_Bits); f9(_Bits); };
	auto f11 = [&](unsigned &_Bits) { f10(_Bits); f10(_Bits); f10(_Bits); };
	auto f12 = [&](unsigned &_Bits) { f11(_Bits); f11(_Bits); f11(_Bits); };
	auto f13 = [&](unsigned &_Bits) { f12(_Bits); f12(_Bits); f12(_Bits); };
	auto f14 = [&](unsigned &_Bits) { f13(_Bits); f13(_Bits); f13(_Bits); };
	auto f15 = [&](unsigned &_Bits) { f14(_Bits); f14(_Bits); f14(_Bits); };
	auto f16 = [&](unsigned &_Bits) { f15(_Bits); f15(_Bits); f15(_Bits); };
	auto f17 = [&](unsigned &_Bits) { f16(_Bits); f16(_Bits); f16(_Bits); };
	auto f18 = [&](unsigned &_Bits) { f17(_Bits); f17(_Bits); f17(_Bits); };
	auto f19 = [&](unsigned &_Bits) { f18(_Bits); f18(_Bits); f18(_Bits); };
	if (!_Value)
		return 0;
	f19(_Value);
	return fg_Scan(_Value);
}

// CHECK-NOT: [malterlib-
// The next scan is analyzed as usual.
unsigned fg_Unchecked(unsigned _Value)
{
	return fg_Scan(_Value);
	// CHECK: :[[@LINE-1]]:9: warning: the argument of 'fg_Scan' is not checked for zero first; check it [malterlib-no-zero-bit-scan]
}

// CHECK-NOT: [malterlib-

#endif

// A call through a local pointer to a function, whose callee writes what is
// scanned: with a limit that runs out while it is looked into (145 steps,
// with only this part of the file), nothing of the scan is reported.
void fg_Clear(unsigned &_Value)
{
	_Value = 0;
}

unsigned fg_ClearsThroughPointer(unsigned _Value)
{
	if (!_Value)
		return 0;
	auto const pClear = &fg_Clear;
	pClear(_Value);
	return fg_Scan(_Value);
	// FULL: :[[@LINE-1]]:9: warning: the argument of 'fg_Scan' is not checked for zero first; check it [malterlib-no-zero-bit-scan]
}

// CHECK-NOT: [malterlib-
// SPENT-NOT: [malterlib-
// SPENT: malterlib work: malterlib-no-zero-bit-scan roots {{[0-9]+}} exhausted 1 largest 145 limit 145
