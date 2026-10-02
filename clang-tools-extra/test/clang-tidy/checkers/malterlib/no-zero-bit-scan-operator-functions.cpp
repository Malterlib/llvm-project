// RUN: clang-tidy %s '-checks=-*,malterlib-no-zero-bit-scan' -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml', malterlib-no-zero-bit-scan.Functions: 'CScan::operator();operator+;CScan::operator unsigned'}}" -- -std=c++20 2>&1 | FileCheck %s --implicit-check-not=warning:

// Scans configured by the name of an operator or a conversion: the warning
// names no alternative, which only a name ending in `NoZero` has.

struct CScan
{
	unsigned operator()(unsigned _Value) const;
	operator unsigned() const;
};

struct CBits
{
};

unsigned operator+(CBits const &_Bits);

unsigned fg_Call(CScan _Scan, unsigned _Value)
{
	return _Scan.operator()(_Value);
	// CHECK: :[[@LINE-1]]:9: warning: the argument of 'operator()' is not checked for zero first; check it [malterlib-no-zero-bit-scan]
}

unsigned fg_Unary(CBits const &_Bits)
{
	return +_Bits;
	// CHECK: :[[@LINE-1]]:9: warning: the argument of 'operator+' is not checked for zero first; check it [malterlib-no-zero-bit-scan]
}
