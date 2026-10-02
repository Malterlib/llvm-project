// RUN: clang-tidy --checks='-*,malterlib-parameter-ownership' --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" %s -- -std=c++20 2>&1 | FileCheck --check-prefixes=CHECK,FULL %s
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -std=c++20 -fsyntax-only -Wno-uninitialized -fplugin-arg-malterlib-checks='-*,malterlib-parameter-ownership' %s 2>&1 | FileCheck --check-prefixes=CHECK,FULL %s
// RUN: clang-tidy --checks='-*,malterlib-parameter-ownership' --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml', malterlib.WorkReport: true, malterlib-parameter-ownership.WorkLimit: 1}}" %s -- -std=c++20 2>&1 | FileCheck --check-prefixes=CHECK,EXHAUSTED %s
// RUN: clang-tidy --checks='-*,malterlib-parameter-ownership' --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml', malterlib.WorkReport: true, malterlib-parameter-ownership.WorkLimit: 19}}" %s -- -std=c++20 -DDELETES_GIVEN_ONLY 2>&1 | FileCheck --check-prefix=SPENT %s
// RUN: clang-tidy --checks='-*,malterlib-parameter-ownership' --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml', malterlib.WorkReport: true, malterlib-parameter-ownership.WorkLimit: 21}}" %s -- -std=c++20 -DDELETES_GIVEN_ONLY 2>&1 | FileCheck --check-prefix=SPENT %s
// CHECK-NOT: [malterlib-

// The work budget (WorkBudget.h): what the analysis of a parameter can't
// finish, like a walk that comes back to where it started through a
// function object or a reference initialized from itself, runs out of steps,
// and the function then reports nothing more; the next function is analyzed
// from scratch. With a limit of one step (the last RUN line), each judged
// parameter that takes more runs out, those that take one report as usual,
// and the counts the option malterlib.WorkReport prints say how many ran out.

#ifndef DELETES_GIVEN_ONLY

struct CStr
{
	CStr();
	CStr(CStr const &);
	CStr(CStr &&);
	~CStr();
};

void fg_Borrow(CStr const &_Value);

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

// A recursive lambda held in a function object in the destructor (round 227
// #1): its calls lead back into its body.
struct CRecursiveDestructor
{
	int *m_pData = nullptr;
	~CRecursiveDestructor()
	{
		TCFunction<void (unsigned)> fRecurse = [&](unsigned _n)
		{
			if (_n)
				fRecurse(_n - 1);
		};
		fRecurse(1);
		delete m_pData;
	}
};

void fg_TakeRecursive(CRecursiveDestructor _Value)
{
	(void)_Value;
}

// A reference initialized from itself through a getter (round 227 #5).
struct CNode
{
	int m_Count;
	CNode &f_Self()
	{
		return *this;
	}
};

struct CSelfReference
{
	~CSelfReference()
	{
		CNode &Node = Node.f_Self();
		--Node.m_Count;
	}
};

void fg_TakeSelfReference(CSelfReference _Value)
{
	(void)_Value;
}

// Lambdas that each call the one before three times: 3^19 walks of the same
// bodies.
struct CLambdaChain
{
	unsigned m_Bits = 0;
	~CLambdaChain()
	{
		auto f0 = [&](unsigned &_Value) { (void)_Value; };
		auto f1 = [&](unsigned &_Value) { f0(_Value); f0(_Value); f0(_Value); };
		auto f2 = [&](unsigned &_Value) { f1(_Value); f1(_Value); f1(_Value); };
		auto f3 = [&](unsigned &_Value) { f2(_Value); f2(_Value); f2(_Value); };
		auto f4 = [&](unsigned &_Value) { f3(_Value); f3(_Value); f3(_Value); };
		auto f5 = [&](unsigned &_Value) { f4(_Value); f4(_Value); f4(_Value); };
		auto f6 = [&](unsigned &_Value) { f5(_Value); f5(_Value); f5(_Value); };
		auto f7 = [&](unsigned &_Value) { f6(_Value); f6(_Value); f6(_Value); };
		auto f8 = [&](unsigned &_Value) { f7(_Value); f7(_Value); f7(_Value); };
		auto f9 = [&](unsigned &_Value) { f8(_Value); f8(_Value); f8(_Value); };
		auto f10 = [&](unsigned &_Value) { f9(_Value); f9(_Value); f9(_Value); };
		auto f11 = [&](unsigned &_Value) { f10(_Value); f10(_Value); f10(_Value); };
		auto f12 = [&](unsigned &_Value) { f11(_Value); f11(_Value); f11(_Value); };
		auto f13 = [&](unsigned &_Value) { f12(_Value); f12(_Value); f12(_Value); };
		auto f14 = [&](unsigned &_Value) { f13(_Value); f13(_Value); f13(_Value); };
		auto f15 = [&](unsigned &_Value) { f14(_Value); f14(_Value); f14(_Value); };
		auto f16 = [&](unsigned &_Value) { f15(_Value); f15(_Value); f15(_Value); };
		auto f17 = [&](unsigned &_Value) { f16(_Value); f16(_Value); f16(_Value); };
		auto f18 = [&](unsigned &_Value) { f17(_Value); f17(_Value); f17(_Value); };
		auto f19 = [&](unsigned &_Value) { f18(_Value); f18(_Value); f18(_Value); };
		f19(m_Bits);
	}
};

void fg_TakeLambdaChain(CLambdaChain _Value)
{
	(void)_Value;
}

// The next function is analyzed as usual.
void fg_ByValue(CStr _Value)
// CHECK: :[[@LINE-1]]:22: warning: parameter '_Value' takes 'CStr' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
{
	fg_Borrow(_Value);
}

// CHECK-NOT: [malterlib-

// Each parameter has steps of its own, but none after one that ran out is
// judged, as the function reports nothing more.
void fg_FirstRunsOut(CRecursiveDestructor _First, CStr _Second)
{
	(void)_First;
	fg_Borrow(_Second);
}

// CHECK-NOT: [malterlib-
void fg_SecondRunsOut(CStr _First, CRecursiveDestructor _Second)
// CHECK: :[[@LINE-1]]:28: warning: parameter '_First' takes 'CStr' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
{
	fg_Borrow(_First);
	(void)_Second;
}

#endif

// A function that gives back what it is given, whose answer the destructor
// that deletes it relies on: with a limit that runs out while what it gives
// back is looked into (19 and 21 steps, with only this part of the file),
// nothing of the parameter is reported.
int *fg_Get(int *_pValue)
{
	return _pValue;
}

struct CDeletesGiven
{
	int *m_pValue;
	~CDeletesGiven()
	{
		delete fg_Get(m_pValue);
	}
};

void fg_TakeDeletesGiven(CDeletesGiven _Value)
// FULL: :[[@LINE-1]]:40: warning: parameter '_Value' takes 'CDeletesGiven' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
{
}

// CHECK-NOT: [malterlib-
// EXHAUSTED: malterlib work: malterlib-parameter-ownership roots {{[0-9]+}} exhausted {{[1-9][0-9]*}} largest 1 limit 1
// SPENT-NOT: [malterlib-
// SPENT: malterlib work: malterlib-parameter-ownership roots {{[0-9]+}} exhausted 1 largest {{19|21}} limit {{19|21}}
