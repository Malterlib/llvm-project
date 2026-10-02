// RUN: %check_clang_tidy -std=c++20-or-later %s malterlib-no-zero-bit-scan %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}"

// Round 205 (llvm-naming-struct-1): chains of 30, each step made of the one
// before twice, are told once per step (a memo of each local, the summary of
// each helper), not again on each way to it, and to their end, without a
// depth past which they would be told otherwise. Clean: references to const
// bound to temporaries made with a set bit, const locals with a set bit kept
// by narrowing, and helpers that only read what they are given. Reported:
// the same made without it, and helpers whose last writes what is mutable,
// which an operator that calls them changes.

using umint = decltype(sizeof(0));
using uint8 = unsigned char;
int fg_GetHighestBitSetNoZero(umint _Value);
int fg_ReferencesSet(unsigned _Bits)
{
	unsigned const &Bits0 = _Bits | 1u;
	unsigned const &Bits1 = Bits0 | Bits0;
	unsigned const &Bits2 = Bits1 | Bits1;
	unsigned const &Bits3 = Bits2 | Bits2;
	unsigned const &Bits4 = Bits3 | Bits3;
	unsigned const &Bits5 = Bits4 | Bits4;
	unsigned const &Bits6 = Bits5 | Bits5;
	unsigned const &Bits7 = Bits6 | Bits6;
	unsigned const &Bits8 = Bits7 | Bits7;
	unsigned const &Bits9 = Bits8 | Bits8;
	unsigned const &Bits10 = Bits9 | Bits9;
	unsigned const &Bits11 = Bits10 | Bits10;
	unsigned const &Bits12 = Bits11 | Bits11;
	unsigned const &Bits13 = Bits12 | Bits12;
	unsigned const &Bits14 = Bits13 | Bits13;
	unsigned const &Bits15 = Bits14 | Bits14;
	unsigned const &Bits16 = Bits15 | Bits15;
	unsigned const &Bits17 = Bits16 | Bits16;
	unsigned const &Bits18 = Bits17 | Bits17;
	unsigned const &Bits19 = Bits18 | Bits18;
	unsigned const &Bits20 = Bits19 | Bits19;
	unsigned const &Bits21 = Bits20 | Bits20;
	unsigned const &Bits22 = Bits21 | Bits21;
	unsigned const &Bits23 = Bits22 | Bits22;
	unsigned const &Bits24 = Bits23 | Bits23;
	unsigned const &Bits25 = Bits24 | Bits24;
	unsigned const &Bits26 = Bits25 | Bits25;
	unsigned const &Bits27 = Bits26 | Bits26;
	unsigned const &Bits28 = Bits27 | Bits27;
	unsigned const &Bits29 = Bits28 | Bits28;
	return fg_GetHighestBitSetNoZero(Bits29);
}
int fg_ReferencesUnset(unsigned _Bits)
{
	unsigned const &Bits0 = _Bits | 0u;
	unsigned const &Bits1 = Bits0 | Bits0;
	unsigned const &Bits2 = Bits1 | Bits1;
	unsigned const &Bits3 = Bits2 | Bits2;
	unsigned const &Bits4 = Bits3 | Bits3;
	unsigned const &Bits5 = Bits4 | Bits4;
	unsigned const &Bits6 = Bits5 | Bits5;
	unsigned const &Bits7 = Bits6 | Bits6;
	unsigned const &Bits8 = Bits7 | Bits7;
	unsigned const &Bits9 = Bits8 | Bits8;
	unsigned const &Bits10 = Bits9 | Bits9;
	unsigned const &Bits11 = Bits10 | Bits10;
	unsigned const &Bits12 = Bits11 | Bits11;
	unsigned const &Bits13 = Bits12 | Bits12;
	unsigned const &Bits14 = Bits13 | Bits13;
	unsigned const &Bits15 = Bits14 | Bits14;
	unsigned const &Bits16 = Bits15 | Bits15;
	unsigned const &Bits17 = Bits16 | Bits16;
	unsigned const &Bits18 = Bits17 | Bits17;
	unsigned const &Bits19 = Bits18 | Bits18;
	unsigned const &Bits20 = Bits19 | Bits19;
	unsigned const &Bits21 = Bits20 | Bits20;
	unsigned const &Bits22 = Bits21 | Bits21;
	unsigned const &Bits23 = Bits22 | Bits22;
	unsigned const &Bits24 = Bits23 | Bits23;
	unsigned const &Bits25 = Bits24 | Bits24;
	unsigned const &Bits26 = Bits25 | Bits25;
	unsigned const &Bits27 = Bits26 | Bits26;
	unsigned const &Bits28 = Bits27 | Bits27;
	unsigned const &Bits29 = Bits28 | Bits28;
	return fg_GetHighestBitSetNoZero(Bits29);
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
int fg_ValuesSet(unsigned _Bits)
{
	unsigned const Bits0 = _Bits | 1u;
	unsigned const Bits1 = Bits0 | Bits0;
	unsigned const Bits2 = Bits1 | Bits1;
	unsigned const Bits3 = Bits2 | Bits2;
	unsigned const Bits4 = Bits3 | Bits3;
	unsigned const Bits5 = Bits4 | Bits4;
	unsigned const Bits6 = Bits5 | Bits5;
	unsigned const Bits7 = Bits6 | Bits6;
	unsigned const Bits8 = Bits7 | Bits7;
	unsigned const Bits9 = Bits8 | Bits8;
	unsigned const Bits10 = Bits9 | Bits9;
	unsigned const Bits11 = Bits10 | Bits10;
	unsigned const Bits12 = Bits11 | Bits11;
	unsigned const Bits13 = Bits12 | Bits12;
	unsigned const Bits14 = Bits13 | Bits13;
	unsigned const Bits15 = Bits14 | Bits14;
	unsigned const Bits16 = Bits15 | Bits15;
	unsigned const Bits17 = Bits16 | Bits16;
	unsigned const Bits18 = Bits17 | Bits17;
	unsigned const Bits19 = Bits18 | Bits18;
	unsigned const Bits20 = Bits19 | Bits19;
	unsigned const Bits21 = Bits20 | Bits20;
	unsigned const Bits22 = Bits21 | Bits21;
	unsigned const Bits23 = Bits22 | Bits22;
	unsigned const Bits24 = Bits23 | Bits23;
	unsigned const Bits25 = Bits24 | Bits24;
	unsigned const Bits26 = Bits25 | Bits25;
	unsigned const Bits27 = Bits26 | Bits26;
	unsigned const Bits28 = Bits27 | Bits27;
	unsigned const Bits29 = Bits28 | Bits28;
	return fg_GetHighestBitSetNoZero(uint8(Bits29));
}
int fg_ValuesUnset(unsigned _Bits)
{
	unsigned const Bits0 = _Bits | 256u;
	unsigned const Bits1 = Bits0 | Bits0;
	unsigned const Bits2 = Bits1 | Bits1;
	unsigned const Bits3 = Bits2 | Bits2;
	unsigned const Bits4 = Bits3 | Bits3;
	unsigned const Bits5 = Bits4 | Bits4;
	unsigned const Bits6 = Bits5 | Bits5;
	unsigned const Bits7 = Bits6 | Bits6;
	unsigned const Bits8 = Bits7 | Bits7;
	unsigned const Bits9 = Bits8 | Bits8;
	unsigned const Bits10 = Bits9 | Bits9;
	unsigned const Bits11 = Bits10 | Bits10;
	unsigned const Bits12 = Bits11 | Bits11;
	unsigned const Bits13 = Bits12 | Bits12;
	unsigned const Bits14 = Bits13 | Bits13;
	unsigned const Bits15 = Bits14 | Bits14;
	unsigned const Bits16 = Bits15 | Bits15;
	unsigned const Bits17 = Bits16 | Bits16;
	unsigned const Bits18 = Bits17 | Bits17;
	unsigned const Bits19 = Bits18 | Bits18;
	unsigned const Bits20 = Bits19 | Bits19;
	unsigned const Bits21 = Bits20 | Bits20;
	unsigned const Bits22 = Bits21 | Bits21;
	unsigned const Bits23 = Bits22 | Bits22;
	unsigned const Bits24 = Bits23 | Bits23;
	unsigned const Bits25 = Bits24 | Bits24;
	unsigned const Bits26 = Bits25 | Bits25;
	unsigned const Bits27 = Bits26 | Bits26;
	unsigned const Bits28 = Bits27 | Bits27;
	unsigned const Bits29 = Bits28 | Bits28;
	return fg_GetHighestBitSetNoZero(uint8(Bits29));
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
struct CBits
{
	mutable unsigned m_Bits;
};
struct CLooked
{
	unsigned m_Bits;
};
void fg_Touch0(CBits const &_Bits)
{
	_Bits.m_Bits = 0;
}
void fg_Touch1(CBits const &_Bits)
{
	fg_Touch0(_Bits);
	fg_Touch0(_Bits);
}
void fg_Touch2(CBits const &_Bits)
{
	fg_Touch1(_Bits);
	fg_Touch1(_Bits);
}
void fg_Touch3(CBits const &_Bits)
{
	fg_Touch2(_Bits);
	fg_Touch2(_Bits);
}
void fg_Touch4(CBits const &_Bits)
{
	fg_Touch3(_Bits);
	fg_Touch3(_Bits);
}
void fg_Touch5(CBits const &_Bits)
{
	fg_Touch4(_Bits);
	fg_Touch4(_Bits);
}
void fg_Touch6(CBits const &_Bits)
{
	fg_Touch5(_Bits);
	fg_Touch5(_Bits);
}
void fg_Touch7(CBits const &_Bits)
{
	fg_Touch6(_Bits);
	fg_Touch6(_Bits);
}
void fg_Touch8(CBits const &_Bits)
{
	fg_Touch7(_Bits);
	fg_Touch7(_Bits);
}
void fg_Touch9(CBits const &_Bits)
{
	fg_Touch8(_Bits);
	fg_Touch8(_Bits);
}
void fg_Touch10(CBits const &_Bits)
{
	fg_Touch9(_Bits);
	fg_Touch9(_Bits);
}
void fg_Touch11(CBits const &_Bits)
{
	fg_Touch10(_Bits);
	fg_Touch10(_Bits);
}
void fg_Touch12(CBits const &_Bits)
{
	fg_Touch11(_Bits);
	fg_Touch11(_Bits);
}
void fg_Touch13(CBits const &_Bits)
{
	fg_Touch12(_Bits);
	fg_Touch12(_Bits);
}
void fg_Touch14(CBits const &_Bits)
{
	fg_Touch13(_Bits);
	fg_Touch13(_Bits);
}
void fg_Touch15(CBits const &_Bits)
{
	fg_Touch14(_Bits);
	fg_Touch14(_Bits);
}
void fg_Touch16(CBits const &_Bits)
{
	fg_Touch15(_Bits);
	fg_Touch15(_Bits);
}
void fg_Touch17(CBits const &_Bits)
{
	fg_Touch16(_Bits);
	fg_Touch16(_Bits);
}
void fg_Touch18(CBits const &_Bits)
{
	fg_Touch17(_Bits);
	fg_Touch17(_Bits);
}
void fg_Touch19(CBits const &_Bits)
{
	fg_Touch18(_Bits);
	fg_Touch18(_Bits);
}
void fg_Touch20(CBits const &_Bits)
{
	fg_Touch19(_Bits);
	fg_Touch19(_Bits);
}
void fg_Touch21(CBits const &_Bits)
{
	fg_Touch20(_Bits);
	fg_Touch20(_Bits);
}
void fg_Touch22(CBits const &_Bits)
{
	fg_Touch21(_Bits);
	fg_Touch21(_Bits);
}
void fg_Touch23(CBits const &_Bits)
{
	fg_Touch22(_Bits);
	fg_Touch22(_Bits);
}
void fg_Touch24(CBits const &_Bits)
{
	fg_Touch23(_Bits);
	fg_Touch23(_Bits);
}
void fg_Touch25(CBits const &_Bits)
{
	fg_Touch24(_Bits);
	fg_Touch24(_Bits);
}
void fg_Touch26(CBits const &_Bits)
{
	fg_Touch25(_Bits);
	fg_Touch25(_Bits);
}
void fg_Touch27(CBits const &_Bits)
{
	fg_Touch26(_Bits);
	fg_Touch26(_Bits);
}
void fg_Touch28(CBits const &_Bits)
{
	fg_Touch27(_Bits);
	fg_Touch27(_Bits);
}
void fg_Touch29(CBits const &_Bits)
{
	fg_Touch28(_Bits);
	fg_Touch28(_Bits);
}
unsigned operator+(CBits const &_Bits)
{
	fg_Touch29(_Bits);
	return _Bits.m_Bits;
}
int fg_Touched(CBits const &_Bits)
{
	if (!(+_Bits))
		return 0;
	return fg_GetHighestBitSetNoZero(+_Bits);
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
void fg_Look0(CBits const &_Bits)
{
	unsigned Read = _Bits.m_Bits; (void)Read;
}
void fg_Look1(CBits const &_Bits)
{
	fg_Look0(_Bits);
	fg_Look0(_Bits);
}
void fg_Look2(CBits const &_Bits)
{
	fg_Look1(_Bits);
	fg_Look1(_Bits);
}
void fg_Look3(CBits const &_Bits)
{
	fg_Look2(_Bits);
	fg_Look2(_Bits);
}
void fg_Look4(CBits const &_Bits)
{
	fg_Look3(_Bits);
	fg_Look3(_Bits);
}
void fg_Look5(CBits const &_Bits)
{
	fg_Look4(_Bits);
	fg_Look4(_Bits);
}
void fg_Look6(CBits const &_Bits)
{
	fg_Look5(_Bits);
	fg_Look5(_Bits);
}
void fg_Look7(CBits const &_Bits)
{
	fg_Look6(_Bits);
	fg_Look6(_Bits);
}
void fg_Look8(CBits const &_Bits)
{
	fg_Look7(_Bits);
	fg_Look7(_Bits);
}
void fg_Look9(CBits const &_Bits)
{
	fg_Look8(_Bits);
	fg_Look8(_Bits);
}
void fg_Look10(CBits const &_Bits)
{
	fg_Look9(_Bits);
	fg_Look9(_Bits);
}
void fg_Look11(CBits const &_Bits)
{
	fg_Look10(_Bits);
	fg_Look10(_Bits);
}
void fg_Look12(CBits const &_Bits)
{
	fg_Look11(_Bits);
	fg_Look11(_Bits);
}
void fg_Look13(CBits const &_Bits)
{
	fg_Look12(_Bits);
	fg_Look12(_Bits);
}
void fg_Look14(CBits const &_Bits)
{
	fg_Look13(_Bits);
	fg_Look13(_Bits);
}
void fg_Look15(CBits const &_Bits)
{
	fg_Look14(_Bits);
	fg_Look14(_Bits);
}
void fg_Look16(CBits const &_Bits)
{
	fg_Look15(_Bits);
	fg_Look15(_Bits);
}
void fg_Look17(CBits const &_Bits)
{
	fg_Look16(_Bits);
	fg_Look16(_Bits);
}
void fg_Look18(CBits const &_Bits)
{
	fg_Look17(_Bits);
	fg_Look17(_Bits);
}
void fg_Look19(CBits const &_Bits)
{
	fg_Look18(_Bits);
	fg_Look18(_Bits);
}
void fg_Look20(CBits const &_Bits)
{
	fg_Look19(_Bits);
	fg_Look19(_Bits);
}
void fg_Look21(CBits const &_Bits)
{
	fg_Look20(_Bits);
	fg_Look20(_Bits);
}
void fg_Look22(CBits const &_Bits)
{
	fg_Look21(_Bits);
	fg_Look21(_Bits);
}
void fg_Look23(CBits const &_Bits)
{
	fg_Look22(_Bits);
	fg_Look22(_Bits);
}
void fg_Look24(CBits const &_Bits)
{
	fg_Look23(_Bits);
	fg_Look23(_Bits);
}
void fg_Look25(CBits const &_Bits)
{
	fg_Look24(_Bits);
	fg_Look24(_Bits);
}
void fg_Look26(CBits const &_Bits)
{
	fg_Look25(_Bits);
	fg_Look25(_Bits);
}
void fg_Look27(CBits const &_Bits)
{
	fg_Look26(_Bits);
	fg_Look26(_Bits);
}
void fg_Look28(CBits const &_Bits)
{
	fg_Look27(_Bits);
	fg_Look27(_Bits);
}
void fg_Look29(CBits const &_Bits)
{
	fg_Look28(_Bits);
	fg_Look28(_Bits);
}
unsigned operator-(CBits const &_Bits)
{
	fg_Look29(_Bits);
	return _Bits.m_Bits;
}
int fg_Looked(CBits const &_Bits)
{
	if (!(-_Bits))
		return 0;
	return fg_GetHighestBitSetNoZero(-_Bits);
}
