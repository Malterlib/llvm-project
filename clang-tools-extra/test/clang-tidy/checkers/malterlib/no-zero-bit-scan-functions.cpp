// RUN: clang-tidy %s '-checks=-*,malterlib-no-zero-bit-scan' -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml', malterlib-no-zero-bit-scan.Functions: '__builtin_clz;fg_LeadingZerosNoZero'}}" -- -std=c++20 2>&1 | FileCheck %s --implicit-check-not=warning:

// A configured scan is recommended a zero-safe alternative only where one is
// named, the scan without `NoZero`: `__builtin_clz` has none, so the message
// names none.
unsigned fg_LeadingZeros(unsigned _Bits)
{
	return __builtin_clz(_Bits);
	// CHECK: :[[@LINE-1]]:9: warning: the argument of '__builtin_clz' is not checked for zero first; check it [malterlib-no-zero-bit-scan]
}

int fg_LeadingZerosNoZero(unsigned _Bits);

int fg_Scan(unsigned _Bits)
{
	return fg_LeadingZerosNoZero(_Bits);
	// CHECK: :[[@LINE-1]]:9: warning: the argument of 'fg_LeadingZerosNoZero' is not checked for zero first; check it, or call 'fg_LeadingZeros', which handles zero [malterlib-no-zero-bit-scan]
}
