// RUN: %check_clang_tidy -std=c++20-or-later %s malterlib-header-declarations-only %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}"
// Also one whose body a class includes from another source, which is reported
// in it, and one whose name is in another file than its body, which is
// reported at the body.
// RUN: rm -rf %t.fragment && mkdir -p %t.fragment
// RUN: printf 'void f_Run() {}\n' > %t.fragment/members.cpp
// RUN: printf 'struct CIncludes\n{\n#include "members.cpp"\n};\n' > %t.fragment/main.cpp
// RUN: clang-tidy --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" --checks='-*,malterlib-header-declarations-only' --header-filter='.*' %t.fragment/main.cpp -- -std=c++20 2>&1 | FileCheck --check-prefix=CHECK-FRAGMENT %s
// CHECK-FRAGMENT: members.cpp:1:6: warning: 'f_Run' is defined in its class in a '.cpp' file
// RUN: rm -rf %t.signature && mkdir -p %t.signature
// RUN: printf 'void f_Run()\n' > %t.signature/signature.hpp
// RUN: printf 'struct CSplit\n{\n#include "signature.hpp"\n\t{\n\t}\n};\n' > %t.signature/main.cpp
// RUN: clang-tidy --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" --checks='-*,malterlib-header-declarations-only' --header-filter='\.cpp$' %t.signature/main.cpp -- -std=c++20 2>&1 | FileCheck --check-prefix=CHECK-SIGNATURE %s
// CHECK-SIGNATURE: main.cpp:4:2: warning: 'f_Run' is defined in its class in a '.cpp' file
// A body is in a source by its braces only: also one whose opening brace an
// include gives and whose closing brace a source has, reported at that brace.
// RUN: rm -rf %t.opening && mkdir -p %t.opening
// RUN: printf 'void f_Run() {\n' > %t.opening/opening.hpp
// RUN: printf 'struct COpening\n{\n#include "opening.hpp"\n\tint Value = 0;\n\t(void)Value;\n\t}\n};\n' > %t.opening/main.cpp
// RUN: clang-tidy --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" --checks='-*,malterlib-header-declarations-only' --header-filter='.*' %t.opening/main.cpp -- -std=c++20 2>&1 | FileCheck --check-prefix=CHECK-OPENING %s
// CHECK-OPENING: main.cpp:6:2: warning: 'f_Run' is defined in its class in a '.cpp' file
// But not one whose statements only, also nested, a source has; of a function
// try block, the closing brace of its last handler.
// RUN: rm -rf %t.nested && mkdir -p %t.nested
// RUN: printf 'void f_Run() {\n' > %t.nested/open.hpp
// RUN: printf '\tif (true) {\n' > %t.nested/if.hpp
// RUN: printf '\t}\n' > %t.nested/ifclose.hpp
// RUN: printf '}\n' > %t.nested/close.hpp
// RUN: printf 'struct CNested\n{\n#include "open.hpp"\n#include "if.hpp"\n\t\tint Value = 0;\n\t\t(void)Value;\n#include "ifclose.hpp"\n#include "close.hpp"\n};\n' > %t.nested/main.cpp
// RUN: clang-tidy --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" --checks='-*,malterlib-header-declarations-only' --header-filter='.*' %t.nested/main.cpp -- -std=c++20 2>&1 | FileCheck --allow-empty --check-prefix=CHECK-NESTED-SOURCE %s
// CHECK-NESTED-SOURCE-NOT: warning:
// RUN: rm -rf %t.try && mkdir -p %t.try
// RUN: printf 'void f_Run() try {\n' > %t.try/tryopen.hpp
// RUN: printf 'struct CTry\n{\n#include "tryopen.hpp"\n\t}\n\tcatch (...)\n\t{\n\t}\n};\n' > %t.try/main.cpp
// RUN: clang-tidy --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" --checks='-*,malterlib-header-declarations-only' --header-filter='.*' %t.try/main.cpp -- -std=c++20 2>&1 | FileCheck --check-prefix=CHECK-TRY %s
// CHECK-TRY: main.cpp:7:2: warning: 'f_Run' is defined in its class in a '.cpp' file
// Of a function try block, by the braces of its block and its last handler,
// not by its `try`: not one whose `try` only a source has, and one whose
// opening brace a source has, reported there.
// RUN: rm -rf %t.trykeyword && mkdir -p %t.trykeyword
// RUN: printf '{\n}\ncatch (...)\n{\n}\n' > %t.trykeyword/block.hpp
// RUN: printf 'struct CTryKeyword\n{\n\tvoid f_Run() try\n#include "block.hpp"\n};\n' > %t.trykeyword/main.cpp
// RUN: clang-tidy --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" --checks='-*,malterlib-header-declarations-only' --header-filter='.*' %t.trykeyword/main.cpp -- -std=c++20 2>&1 | FileCheck --allow-empty --check-prefix=CHECK-TRY-KEYWORD %s
// CHECK-TRY-KEYWORD-NOT: warning:
// RUN: rm -rf %t.trybrace && mkdir -p %t.trybrace
// RUN: printf 'void f_Run() try\n' > %t.trybrace/keyword.hpp
// RUN: printf '}\ncatch (...)\n{\n}\n' > %t.trybrace/close.hpp
// RUN: printf 'struct CTryBrace\n{\n#include "keyword.hpp"\n\t{\n#include "close.hpp"\n};\n' > %t.trybrace/main.cpp
// RUN: clang-tidy --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" --checks='-*,malterlib-header-declarations-only' --header-filter='.*' %t.trybrace/main.cpp -- -std=c++20 2>&1 | FileCheck --check-prefix=CHECK-TRY-BRACE %s
// CHECK-TRY-BRACE: main.cpp:4:2: warning: 'f_Run' is defined in its class in a '.cpp' file
// A body in a source that a declaration file gives a part of is reported for
// both.
// RUN: rm -rf %t.both && mkdir -p %t.both
// RUN: printf '\t\treturn 1;\n' > %t.both/fragment.h
// RUN: printf 'struct CBoth\n{\n\tint f_Run()\n\t{\n#include "fragment.h"\n\t}\n};\n' > %t.both/main.cpp
// RUN: clang-tidy --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" --checks='-*,malterlib-header-declarations-only' --header-filter='.*' %t.both/main.cpp -- -std=c++20 2>&1 | FileCheck --check-prefix=CHECK-BOTH %s
// CHECK-BOTH-DAG: main.cpp:3:6: warning: 'f_Run' is defined in its class in a '.cpp' file
// CHECK-BOTH-DAG: fragment.h:1:3: warning: 'f_Run' is defined in a '.h' file, which holds declarations only
// When the source is also a declaration file, its rule, no definitions at all,
// is the one reported.
// RUN: rm -rf %t.overlap && mkdir -p %t.overlap
// RUN: printf 'struct COverlap\n{\n\tvoid f_Run() {}\n};\n' > %t.overlap/main.cpp
// RUN: clang-tidy --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml', malterlib-header-declarations-only.DeclarationFileExtensions: 'h;cpp'}}" --checks='-*,malterlib-header-declarations-only' %t.overlap/main.cpp -- -std=c++20 2>&1 | FileCheck --check-prefix=CHECK-OVERLAP %s
// CHECK-OVERLAP-NOT: is defined in its class
// CHECK-OVERLAP: main.cpp:3:7: warning: 'f_Run' is defined in a '.cpp' file, which holds declarations only
// CHECK-OVERLAP-NOT: is defined in its class
// Not when a source fragment only declares, like a local class.
// RUN: rm -rf %t.declares && mkdir -p %t.declares
// RUN: printf 'struct CLocal { void f_Local() {} };\n' > %t.declares/types.cpp
// RUN: printf 'void f_Run() {\n#include "types.cpp"\n}\n' > %t.declares/method.hpp
// RUN: printf 'struct COuter\n{\n#include "method.hpp"\n};\n' > %t.declares/main.cpp
// RUN: clang-tidy --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" --checks='-*,malterlib-header-declarations-only' --header-filter='.*' %t.declares/main.cpp -- -std=c++20 2>&1 | FileCheck --allow-empty --check-prefix=CHECK-DECLARES %s
// CHECK-DECLARES-NOT: warning:
// Also of a class local to an Objective-C method, or a block.
// RUN: rm -rf %t.objc && mkdir -p %t.objc
// RUN: printf '@interface CObject\n- (void)f_Run;\n@end\n@implementation CObject\n- (void)f_Run\n{\n\tstruct CLocal\n\t{\n\t\tvoid f_Local() {}\n\t};\n\tauto fBlock = ^{ struct CBlockLocal { void f_Local() {} }; };\n}\n@end\nstruct CNotLocal\n{\n\tvoid f_Inline() {}\n};\n' > %t.objc/main.mm
// RUN: clang-tidy --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" --checks='-*,malterlib-header-declarations-only' %t.objc/main.mm -- -x objective-c++ -std=c++20 -fblocks 2>&1 | FileCheck --check-prefix=CHECK-OBJC %s
// CHECK-OBJC-NOT: 'f_Local' is defined in its class
// CHECK-OBJC: main.mm:16:7: warning: 'f_Inline' is defined in its class in a '.mm' file
// CHECK-OBJC-NOT: 'f_Local' is defined in its class

// In a source, a class declares its members and their bodies follow it, as a
// header and its .hpp do: not one defined in the class, also a friend, of a
// nested class or a class template, but one defaulted or deleted, and those of
// a class local to a function.
struct CInClass
{
	CInClass() = default;
	CInClass(CInClass const &_Other) = delete;
	void f_Inline() {}
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: 'f_Inline' is defined in its class in a '.cpp' file; declare it in the class and define it after it [malterlib-header-declarations-only]
	int f_Value() const { return 1; }
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: 'f_Value' is defined in its class in a '.cpp' file; declare it in the class and define it after it [malterlib-header-declarations-only]
	void f_Declared();
	friend bool operator == (CInClass const &_Left, CInClass const &_Right) { return true; }
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'operator==' is defined in its class in a '.cpp' file; declare it in the class and define it after it [malterlib-header-declarations-only]
	struct CNested
	{
		void f_Nested() {}
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: 'f_Nested' is defined in its class in a '.cpp' file; declare it in the class and define it after it [malterlib-header-declarations-only]
	};
};

void CInClass::f_Declared()
{
}

template <typename t_CType>
struct TCInClass
{
	void f_Template() {}
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: 'f_Template' is defined in its class in a '.cpp' file; declare it in the class and define it after it [malterlib-header-declarations-only]
};

void fg_Local()
{
	struct CLocal
	{
		void f_Local() {}
		struct CLocalNested
		{
			void f_LocalNested() {}
		};
	};
	auto fLambda = [] {};
	(void)fLambda;
}

// By the braces written where they are used, also when a macro gives the
// `try` and the rest, before what leaves out a body a macro gives; not braces
// a macro's definition gives.
#define DInClassTry try
#define DInClassCloseTry } catch (...) {}
#define DInClassBody { }
struct CMacroBraces
{
	void f_Run() DInClassTry {
	DInClassCloseTry
// CHECK-MESSAGES: :[[@LINE-2]]:7: warning: 'f_Run' is defined in its class in a '.cpp' file; declare it in the class and define it after it [malterlib-header-declarations-only]
	void f_Other() DInClassBody
};
