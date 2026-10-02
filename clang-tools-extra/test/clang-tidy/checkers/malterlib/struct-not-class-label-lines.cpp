// RUN: %check_clang_tidy %s malterlib-struct-not-class %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml', malterlib.FixIts: true}}"

// A repeated label's fix removes its tokens only, keeping each line break among
// them, also after a line that a splice continues, whose splice the label's
// first token begins with, and before a directive: no two lines are joined,
// and the fixed code compiles.
struct CLabelLines
{
private:
	int m_Value; \
private
:
#if 1
	int m_Other;
#endif
// CHECK-MESSAGES: :[[@LINE-6]]:15: warning: 'private:' repeats the access in effect in 'CLabelLines', that of the label before it [malterlib-struct-not-class]
};
// CHECK-FIXES: {{^}}	int m_Value; {{$}}
// CHECK-FIXES-EMPTY:
// CHECK-FIXES-NEXT: {{^}}#if 1{{$}}
