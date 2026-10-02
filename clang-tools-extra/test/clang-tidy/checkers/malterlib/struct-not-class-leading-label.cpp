// The leading `public:` of a class fixed to a struct is removed only where
// removing it removes nothing but the label in the file of the class's
// braces, as a repeated label is: not one a fragment that other classes
// include gives, nor one that holds a preprocessing directive, a comment, an
// attribute or a macro; those classes are fixed with their label kept. The
// fixed file compiles, and the fragment is unchanged.
// RUN: rm -rf %t.label && mkdir -p %t.label
// RUN: %python %S/Inputs/fixit-inputs.py leading-label-fragment %t.label/Public.hpp
// RUN: sed -n -e '/^\/\/ BEGIN-SOURCE$/,/^\/\/ END-SOURCE$/p' %s > %t.label/main.cpp
// RUN: clang-tidy --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml', malterlib.FixIts: true}}" --checks='-*,malterlib-struct-not-class' --header-filter='.*' -fix %t.label/main.cpp -- -std=c++20 -I %t.label 2>&1 | FileCheck --check-prefix=CHECK-MESSAGES %s
// RUN: FileCheck --check-prefix=CHECK-FIXES --input-file=%t.label/main.cpp %s
// RUN: FileCheck --check-prefix=CHECK-FRAGMENT --input-file=%t.label/Public.hpp %s
// RUN: %clang -fsyntax-only -std=c++20 -I %t.label %t.label/main.cpp

// CHECK-MESSAGES-DAG: main.cpp:{{[0-9]+}}:1: warning: 'CFirstShared' should be declared with 'struct'
// CHECK-MESSAGES-DAG: main.cpp:{{[0-9]+}}:1: warning: 'CSecondShared' should be declared with 'struct'
// CHECK-MESSAGES-DAG: main.cpp:{{[0-9]+}}:1: warning: 'CDirective' should be declared with 'struct'
// CHECK-MESSAGES-DAG: main.cpp:{{[0-9]+}}:1: warning: 'CCommented' should be declared with 'struct'
// CHECK-MESSAGES-DAG: main.cpp:{{[0-9]+}}:1: warning: 'CAnnotated' should be declared with 'struct'
// CHECK-MESSAGES-DAG: main.cpp:{{[0-9]+}}:1: warning: 'CMacroLabel' should be declared with 'struct'
// CHECK-MESSAGES-DAG: main.cpp:{{[0-9]+}}:1: warning: 'CPlain' should be declared with 'struct'
// CHECK-FRAGMENT: {{^}}public:{{$}}

// BEGIN-SOURCE
// Round 200 (llvm-naming-struct-1): a leading label from a fragment other
// classes include; the second, whose members before it are private, isn't
// fixed.
class CFirstShared
{
#include "Public.hpp"
	int m_Value = 0;
};
class CSecondShared
{
	int mp_Hidden = 0;
#include "Public.hpp"
	int m_Value = 0;
};
int g_Shared = CSecondShared{}.m_Value + CFirstShared{}.m_Value;
// Round 200 (llvm-naming-struct-2): a leading label that holds a
// preprocessing directive.
class CDirective
{
public
#define DValue 42
:
	int m_Value = DValue;
};
// A comment in it, an attribute and a macro; the class whose label a macro
// gives isn't fixed.
class CCommented
{
public /* The interface */ :
	int m_Value = 0;
};
class CAnnotated
{
public __attribute__((annotate("section"))):
	int m_Value = 0;
};
#define DPublic public
class CMacroLabel
{
DPublic:
	int m_Value = 0;
};
// One written alone in the file of the braces is removed.
class CPlain
{
public:
	int m_Value = 0;
};
int g_Value = CDirective{}.m_Value + CCommented{}.m_Value + CAnnotated{}.m_Value + CMacroLabel{}.m_Value + CPlain{}.m_Value;
// END-SOURCE

// CHECK-FIXES: {{^}}struct CFirstShared{{$}}
// CHECK-FIXES-NEXT: {{^[{]$}}
// CHECK-FIXES-NEXT: {{^}}#include "Public.hpp"{{$}}
// CHECK-FIXES: {{^}}class CSecondShared{{$}}
// CHECK-FIXES: {{^}}struct CDirective{{$}}
// CHECK-FIXES-NEXT: {{^[{]$}}
// CHECK-FIXES-NEXT: {{^}}public{{$}}
// CHECK-FIXES-NEXT: {{^}}#define DValue 42{{$}}
// CHECK-FIXES-NEXT: {{^}}:{{$}}
// CHECK-FIXES: {{^}}struct CCommented{{$}}
// CHECK-FIXES-NEXT: {{^[{]$}}
// CHECK-FIXES-NEXT: {{^}}public /* The interface */ :{{$}}
// CHECK-FIXES: {{^}}struct CAnnotated{{$}}
// CHECK-FIXES-NEXT: {{^[{]$}}
// CHECK-FIXES-NEXT: {{^}}public __attribute__((annotate("section"))):{{$}}
// CHECK-FIXES: {{^}}class CMacroLabel{{$}}
// CHECK-FIXES-NEXT: {{^[{]$}}
// CHECK-FIXES-NEXT: {{^}}DPublic:{{$}}
// CHECK-FIXES: {{^}}struct CPlain{{$}}
// CHECK-FIXES-NEXT: {{^[{]$}}
// CHECK-FIXES-EMPTY:
// CHECK-FIXES-NEXT: {{^}}	int m_Value = 0;{{$}}
