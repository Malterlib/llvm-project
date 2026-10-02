// RUN: %check_clang_tidy -std=c++20-or-later %s malterlib-parameter-ownership %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}"

// Round 205: chains of 40, followed to their end once each, without a depth
// past which what they do isn't told: locals made of the one before, what a
// helper given nothing calls. Reported: a destructor that releases through
// the locals, and one that unlocks through the helpers. Clean: the same that
// only logs or reads.

void fg_Log(int const *_pData);
void fg_ReleaseThroughLocals(int *_pData)
{
	int *pData0 = _pData;
	int *pData1 = pData0;
	int *pData2 = pData1;
	int *pData3 = pData2;
	int *pData4 = pData3;
	int *pData5 = pData4;
	int *pData6 = pData5;
	int *pData7 = pData6;
	int *pData8 = pData7;
	int *pData9 = pData8;
	int *pData10 = pData9;
	int *pData11 = pData10;
	int *pData12 = pData11;
	int *pData13 = pData12;
	int *pData14 = pData13;
	int *pData15 = pData14;
	int *pData16 = pData15;
	int *pData17 = pData16;
	int *pData18 = pData17;
	int *pData19 = pData18;
	int *pData20 = pData19;
	int *pData21 = pData20;
	int *pData22 = pData21;
	int *pData23 = pData22;
	int *pData24 = pData23;
	int *pData25 = pData24;
	int *pData26 = pData25;
	int *pData27 = pData26;
	int *pData28 = pData27;
	int *pData29 = pData28;
	int *pData30 = pData29;
	int *pData31 = pData30;
	int *pData32 = pData31;
	int *pData33 = pData32;
	int *pData34 = pData33;
	int *pData35 = pData34;
	int *pData36 = pData35;
	int *pData37 = pData36;
	int *pData38 = pData37;
	int *pData39 = pData38;
	delete pData39;
}
void fg_LogThroughLocals(int *_pData)
{
	int *pData0 = _pData;
	int *pData1 = pData0;
	int *pData2 = pData1;
	int *pData3 = pData2;
	int *pData4 = pData3;
	int *pData5 = pData4;
	int *pData6 = pData5;
	int *pData7 = pData6;
	int *pData8 = pData7;
	int *pData9 = pData8;
	int *pData10 = pData9;
	int *pData11 = pData10;
	int *pData12 = pData11;
	int *pData13 = pData12;
	int *pData14 = pData13;
	int *pData15 = pData14;
	int *pData16 = pData15;
	int *pData17 = pData16;
	int *pData18 = pData17;
	int *pData19 = pData18;
	int *pData20 = pData19;
	int *pData21 = pData20;
	int *pData22 = pData21;
	int *pData23 = pData22;
	int *pData24 = pData23;
	int *pData25 = pData24;
	int *pData26 = pData25;
	int *pData27 = pData26;
	int *pData28 = pData27;
	int *pData29 = pData28;
	int *pData30 = pData29;
	int *pData31 = pData30;
	int *pData32 = pData31;
	int *pData33 = pData32;
	int *pData34 = pData33;
	int *pData35 = pData34;
	int *pData36 = pData35;
	int *pData37 = pData36;
	int *pData38 = pData37;
	int *pData39 = pData38;
	fg_Log(pData39);
}
struct CReleasedThroughLocals
{
	int *m_pData;
	~CReleasedThroughLocals()
	{
		fg_ReleaseThroughLocals(m_pData);
	}
};
void fg_UseReleased(CReleasedThroughLocals _Value)
// CHECK-MESSAGES: :[[@LINE-1]]:44: warning: parameter '_Value' takes 'CReleasedThroughLocals' by value; {{.*}}
{
}
struct CLoggedThroughLocals
{
	int *m_pData;
	~CLoggedThroughLocals()
	{
		fg_LogThroughLocals(m_pData);
	}
};
void fg_UseLogged(CLoggedThroughLocals _Value)
{
}
unsigned g_nLocks;
void fg_Unlock0()
{
	--g_nLocks;
}
void fg_Unlock1()
{
	fg_Unlock0();
}
void fg_Unlock2()
{
	fg_Unlock1();
}
void fg_Unlock3()
{
	fg_Unlock2();
}
void fg_Unlock4()
{
	fg_Unlock3();
}
void fg_Unlock5()
{
	fg_Unlock4();
}
void fg_Unlock6()
{
	fg_Unlock5();
}
void fg_Unlock7()
{
	fg_Unlock6();
}
void fg_Unlock8()
{
	fg_Unlock7();
}
void fg_Unlock9()
{
	fg_Unlock8();
}
void fg_Unlock10()
{
	fg_Unlock9();
}
void fg_Unlock11()
{
	fg_Unlock10();
}
void fg_Unlock12()
{
	fg_Unlock11();
}
void fg_Unlock13()
{
	fg_Unlock12();
}
void fg_Unlock14()
{
	fg_Unlock13();
}
void fg_Unlock15()
{
	fg_Unlock14();
}
void fg_Unlock16()
{
	fg_Unlock15();
}
void fg_Unlock17()
{
	fg_Unlock16();
}
void fg_Unlock18()
{
	fg_Unlock17();
}
void fg_Unlock19()
{
	fg_Unlock18();
}
void fg_Unlock20()
{
	fg_Unlock19();
}
void fg_Unlock21()
{
	fg_Unlock20();
}
void fg_Unlock22()
{
	fg_Unlock21();
}
void fg_Unlock23()
{
	fg_Unlock22();
}
void fg_Unlock24()
{
	fg_Unlock23();
}
void fg_Unlock25()
{
	fg_Unlock24();
}
void fg_Unlock26()
{
	fg_Unlock25();
}
void fg_Unlock27()
{
	fg_Unlock26();
}
void fg_Unlock28()
{
	fg_Unlock27();
}
void fg_Unlock29()
{
	fg_Unlock28();
}
void fg_Unlock30()
{
	fg_Unlock29();
}
void fg_Unlock31()
{
	fg_Unlock30();
}
void fg_Unlock32()
{
	fg_Unlock31();
}
void fg_Unlock33()
{
	fg_Unlock32();
}
void fg_Unlock34()
{
	fg_Unlock33();
}
void fg_Unlock35()
{
	fg_Unlock34();
}
void fg_Unlock36()
{
	fg_Unlock35();
}
void fg_Unlock37()
{
	fg_Unlock36();
}
void fg_Unlock38()
{
	fg_Unlock37();
}
void fg_Unlock39()
{
	fg_Unlock38();
}
unsigned fg_Count0()
{
	return g_nLocks;
}
unsigned fg_Count1()
{
	return fg_Count0();
}
unsigned fg_Count2()
{
	return fg_Count1();
}
unsigned fg_Count3()
{
	return fg_Count2();
}
unsigned fg_Count4()
{
	return fg_Count3();
}
unsigned fg_Count5()
{
	return fg_Count4();
}
unsigned fg_Count6()
{
	return fg_Count5();
}
unsigned fg_Count7()
{
	return fg_Count6();
}
unsigned fg_Count8()
{
	return fg_Count7();
}
unsigned fg_Count9()
{
	return fg_Count8();
}
unsigned fg_Count10()
{
	return fg_Count9();
}
unsigned fg_Count11()
{
	return fg_Count10();
}
unsigned fg_Count12()
{
	return fg_Count11();
}
unsigned fg_Count13()
{
	return fg_Count12();
}
unsigned fg_Count14()
{
	return fg_Count13();
}
unsigned fg_Count15()
{
	return fg_Count14();
}
unsigned fg_Count16()
{
	return fg_Count15();
}
unsigned fg_Count17()
{
	return fg_Count16();
}
unsigned fg_Count18()
{
	return fg_Count17();
}
unsigned fg_Count19()
{
	return fg_Count18();
}
unsigned fg_Count20()
{
	return fg_Count19();
}
unsigned fg_Count21()
{
	return fg_Count20();
}
unsigned fg_Count22()
{
	return fg_Count21();
}
unsigned fg_Count23()
{
	return fg_Count22();
}
unsigned fg_Count24()
{
	return fg_Count23();
}
unsigned fg_Count25()
{
	return fg_Count24();
}
unsigned fg_Count26()
{
	return fg_Count25();
}
unsigned fg_Count27()
{
	return fg_Count26();
}
unsigned fg_Count28()
{
	return fg_Count27();
}
unsigned fg_Count29()
{
	return fg_Count28();
}
unsigned fg_Count30()
{
	return fg_Count29();
}
unsigned fg_Count31()
{
	return fg_Count30();
}
unsigned fg_Count32()
{
	return fg_Count31();
}
unsigned fg_Count33()
{
	return fg_Count32();
}
unsigned fg_Count34()
{
	return fg_Count33();
}
unsigned fg_Count35()
{
	return fg_Count34();
}
unsigned fg_Count36()
{
	return fg_Count35();
}
unsigned fg_Count37()
{
	return fg_Count36();
}
unsigned fg_Count38()
{
	return fg_Count37();
}
unsigned fg_Count39()
{
	return fg_Count38();
}
struct CUnlocks
{
	CUnlocks() = default;
	CUnlocks(CUnlocks const &_Other) = delete;
	~CUnlocks()
	{
		fg_Unlock39();
	}
};
void fg_UseUnlocks(CUnlocks _Value)
// CHECK-MESSAGES: :[[@LINE-1]]:29: warning: parameter '_Value' takes 'CUnlocks' by value; {{.*}}
{
}
struct CCounts
{
	CCounts() = default;
	CCounts(CCounts const &_Other) = delete;
	~CCounts()
	{
		fg_Count39();
	}
};
void fg_UseCounts(CCounts _Value)
{
}
