// malterlib-parameter-ownership and malterlib-no-zero-bit-scan report nothing
// in a translation unit with an error that stops it from compiling; the same
// code without the error is checked.
// RUN: %check_clang_tidy -std=c++20-or-later %s malterlib-parameter-ownership,malterlib-no-zero-bit-scan %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}"
// RUN: not clang-tidy --checks='-*,malterlib-parameter-ownership,malterlib-no-zero-bit-scan' --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" %s -- -std=c++20 -DBROKEN 2>&1 | FileCheck --implicit-check-not='[malterlib-' --implicit-check-not='Stack dump' --check-prefix=CHECK-BROKEN %s

using umint = decltype(sizeof(0));

int fg_GetHighestBitSetNoZero(umint _Value);

struct CStr
{
	CStr();
	CStr(CStr const &);
	CStr(CStr &&);
	~CStr();
};

void fg_Borrow(CStr const &_Value);

void fg_ByValue(CStr _Value)
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: parameter '_Value' takes 'CStr' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
{
	fg_Borrow(_Value);
}

void fg_Unchecked(umint _Value)
{
	fg_GetHighestBitSetNoZero(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

#ifdef BROKEN
int g_Broken = NotDeclared;
// CHECK-BROKEN: error: use of undeclared identifier 'NotDeclared'
#endif
