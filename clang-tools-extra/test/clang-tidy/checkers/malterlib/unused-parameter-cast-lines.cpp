// RUN: %check_clang_tidy %s malterlib-unused-parameter-cast %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml', malterlib.FixIts: true}}" -- -fno-delayed-template-parsing
// The last line of a file without a line break, and a file with CRLF line
// endings: the fixed file compiles, its other lines as they were.
// RUN: rm -rf %t.lines && mkdir -p %t.lines
// RUN: %python %S/Inputs/fixit-inputs.py last %t.lines/last.cpp
// RUN: clang-tidy --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml', malterlib.FixIts: true}}" --checks='-*,malterlib-unused-parameter-cast' -fix %t.lines/last.cpp -- -std=c++20 2>&1 | FileCheck --check-prefix=CHECK-LAST-MESSAGES %s
// RUN: %clang -fsyntax-only -std=c++20 %t.lines/last.cpp
// RUN: %python %S/Inputs/fixit-inputs.py crlf %t.lines/crlf.cpp
// RUN: clang-tidy --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml', malterlib.FixIts: true}}" --checks='-*,malterlib-unused-parameter-cast' -fix %t.lines/crlf.cpp -- -std=c++20 2>&1 | FileCheck --check-prefix=CHECK-CRLF-MESSAGES %s
// RUN: %clang -fsyntax-only -std=c++20 %t.lines/crlf.cpp
// RUN: tr '\r' '@' < %t.lines/crlf.cpp | FileCheck --strict-whitespace --check-prefix=CHECK-CRLF %s
// CHECK-LAST-MESSAGES: last.cpp:{{[0-9]+}}:{{[0-9]+}}: warning: parameter '_Value' is silenced
// CHECK-CRLF-MESSAGES: crlf.cpp:{{[0-9]+}}:{{[0-9]+}}: warning: parameter '_Value' is silenced
// CHECK-CRLF-MESSAGES: crlf.cpp:11:2: warning: parameter '_Value' is silenced
// The spliced line keeps its line break, and the line removed whole takes
// its CRLF (shown as `@`): `#` still begins a line.
// CHECK-CRLF: {{^}}	int Local = 0; \@{{$}}
// CHECK-CRLF-NEXT: {{^}}	@{{$}}
// CHECK-CRLF-NEXT: {{^}}#if 1@{{$}}
// CHECK-CRLF: {{^}}void fg_Alone(int _Value)@{{$}}
// CHECK-CRLF-NEXT: {{^}}{@{{$}}
// CHECK-CRLF-NEXT: {{^}}#if 1@{{$}}

// A cast's fix removes its whole line only where it is alone on a physical
// line that begins a logical line and ends in a line break of its own;
// otherwise its tokens only, every line break kept: here after a line that a
// splice continues, so the directive after it still begins a line.
void fg_Spliced(int _Value)
{
	int Local = 0; \
	(void)_Value;
#if 1
	++Local;
#endif
// CHECK-MESSAGES: :[[@LINE-4]]:2: warning: parameter '_Value' is silenced with a cast to void; leave an unused parameter named, without the cast [malterlib-unused-parameter-cast]
}
// CHECK-FIXES: {{^}}	int Local = 0; \{{$}}
// CHECK-FIXES-NEXT: {{^}}	{{$}}
// CHECK-FIXES-NEXT: {{^}}#if 1{{$}}

// A splice with whitespace before its line feed is one too.
void fg_SplicedSpace(int _Value)
{
	int Local = 0; \ 
	(void)_Value;
#if 1
	++Local;
#endif
// CHECK-MESSAGES: :[[@LINE-4]]:2: warning: parameter '_Value' is silenced with a cast to void; leave an unused parameter named, without the cast [malterlib-unused-parameter-cast]
}
// CHECK-FIXES: {{^}}	int Local = 0; \ {{$}}
// CHECK-FIXES-NEXT: {{^}}	{{$}}
// CHECK-FIXES-NEXT: {{^}}#if 1{{$}}

// Alone on its line, followed by a directive: the line goes.
void fg_BeforeDirective(int _Value)
{
	(void)_Value;
#if 1
#endif
// CHECK-MESSAGES: :[[@LINE-3]]:2: warning: parameter '_Value' is silenced with a cast to void; leave an unused parameter named, without the cast [malterlib-unused-parameter-cast]
}
// CHECK-FIXES: {{^}}void fg_BeforeDirective(int _Value){{$}}
// CHECK-FIXES-NEXT: {{^}}{{{$}}
// CHECK-FIXES-NEXT: {{^}}#if 1{{$}}

// Inside a macro's definition, over continued lines: no fix.
void fg_Work();
#define DSilence(_Value) \
	(void)_Value; \
	fg_Work()
void fg_InMacro(int _Value)
{
	DSilence(_Value);
}
// CHECK-MESSAGES: :[[@LINE-6]]:2: warning: parameter '_Value' is silenced with a cast to void; leave an unused parameter named, without the cast [malterlib-unused-parameter-cast]
// CHECK-FIXES: {{^}}	(void)_Value; \{{$}}
// CHECK-FIXES: {{^}}	DSilence(_Value);{{$}}

// Sharing its line with a comment: the comment stays.
void fg_WithComment(int _Value)
{
	(void)_Value; // Silenced
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: parameter '_Value' is silenced with a cast to void; leave an unused parameter named, without the cast [malterlib-unused-parameter-cast]
}
// CHECK-FIXES: {{^}}	// Silenced{{$}}

// Sharing its line with another statement.
void fg_WithStatement(int _Value)
{
	int Local = 0; (void)_Value; ++Local;
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: parameter '_Value' is silenced with a cast to void; leave an unused parameter named, without the cast [malterlib-unused-parameter-cast]
}
// CHECK-FIXES: {{^}}	int Local = 0; ++Local;{{$}}

// After and before a block comment that spans lines.
void fg_AfterBlockComment(int _Value)
{
	/* A comment
	   over two lines */ (void)_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: parameter '_Value' is silenced with a cast to void; leave an unused parameter named, without the cast [malterlib-unused-parameter-cast]
}
// CHECK-FIXES: {{^}}	   over two lines */ {{$}}
void fg_BeforeBlockComment(int _Value)
{
	(void)_Value; /* A comment
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: parameter '_Value' is silenced with a cast to void; leave an unused parameter named, without the cast [malterlib-unused-parameter-cast]
	   over two lines */
}
// CHECK-FIXES: {{^}}	/* A comment{{$}}

// Written over two lines after another statement: its line break stays.
void fg_TwoLines(int _Value)
{
	int Local = 0; (void)
		_Value;
#if 1
	++Local;
#endif
// CHECK-MESSAGES: :[[@LINE-5]]:17: warning: parameter '_Value' is silenced with a cast to void; leave an unused parameter named, without the cast [malterlib-unused-parameter-cast]
}
// CHECK-FIXES: {{^}}	int Local = 0; {{$}}
// CHECK-FIXES-EMPTY:
// CHECK-FIXES-NEXT: {{^}}#if 1{{$}}
