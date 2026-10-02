// RUN: %check_clang_tidy %s malterlib-unused-parameter-cast %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}"

void fg_Unused(int _Value)
{
	(void)_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: parameter '_Value' is silenced with a cast to void; leave an unused parameter named, without the cast [malterlib-unused-parameter-cast]
}
// CHECK-FIXES: void fg_Unused(int _Value)
// CHECK-FIXES-NEXT: {
// CHECK-FIXES-NEXT: //
// CHECK-FIXES-NEXT: }
