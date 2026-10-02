// RUN: %check_clang_tidy -std=c++20-or-later %s malterlib-conceptual-prefix %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml', malterlib-conceptual-prefix.SmartPointerHandleTypes: '-*'}}"

// Without SmartPointerHandleTypes no class is a handle, and the bases of a
// smart pointer aren't gone through: one deeper than they would be followed is
// a pointer.
namespace NNoHandles
{
	template <typename t_CType>
	struct TCUniquePointer
	{
		t_CType *operator->() const;
		t_CType &operator*() const;
		explicit operator bool() const;
	};
	struct CDeepPointer0 : TCUniquePointer<int>
	{
	};
	struct CDeepPointer1 : CDeepPointer0
	{
	};
	struct CDeepPointer2 : CDeepPointer1
	{
	};
	struct CDeepPointer3 : CDeepPointer2
	{
	};
	struct CDeepPointer4 : CDeepPointer3
	{
	};
	struct CDeepPointer5 : CDeepPointer4
	{
	};
	struct CDeepPointer6 : CDeepPointer5
	{
	};
	struct CDeepPointer7 : CDeepPointer6
	{
	};
	struct CDeepPointer8 : CDeepPointer7
	{
	};
	struct CDeepPointer9 : CDeepPointer8
	{
	};
	CDeepPointer9 g_pDeep;
	CDeepPointer9 g_Deep;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'g_Deep' is a pointer and should have the 'p' concept prefix after its storage prefix [malterlib-conceptual-prefix]
}
