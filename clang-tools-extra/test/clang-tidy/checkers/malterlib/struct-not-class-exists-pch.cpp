// RUN: %check_clang_tidy -std=c++20-or-later %s malterlib-struct-not-class %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -fms-extensions -I %S/Inputs
// A class whose definition comes from a precompiled header, whose
// preprocessing the translation unit didn't see, isn't checked for repeated
// labels; included as text, it is.
// Labels of an included fragment of a body aren't compared, nor with them.
// RUN: rm -rf %t.fragment && mkdir -p %t.fragment
// RUN: printf 'private:\n\tint mp_First;\nprivate:\n\tint mp_Second;\n' > %t.fragment/members.hpp
// RUN: printf 'struct CFragment\n{\nprivate:\n\tint mp_Zero;\n#include "members.hpp"\n};\n' > %t.fragment/main.cpp
// RUN: clang-tidy --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" --checks='-*,malterlib-struct-not-class' --header-filter='.*' %t.fragment/main.cpp -- -std=c++20 2>&1 | FileCheck --allow-empty --check-prefix=CHECK-FRAGMENT %s
// CHECK-FRAGMENT-NOT: warning:
// RUN: rm -rf %t.pch && mkdir -p %t.pch
// RUN: printf '#pragma once\nstruct CFromHeader\n{\nprivate:\n\tint mp_First;\nprivate:\n\tint mp_Second;\n};\n' > %t.pch/header.h
// RUN: printf '#include "header.h"\nCFromHeader g_Value;\n' > %t.pch/main.cpp
// RUN: %clang -x c++-header -std=c++20 %t.pch/header.h -o %t.pch/header.h.pch
// RUN: clang-tidy --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" --checks='-*,malterlib-struct-not-class' --header-filter='.*' %t.pch/main.cpp -- -std=c++20 -include-pch %t.pch/header.h.pch 2>&1 | FileCheck --allow-empty --check-prefix=CHECK-PCH %s
// CHECK-PCH-NOT: warning:
// RUN: clang-tidy --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" --checks='-*,malterlib-struct-not-class' --header-filter='.*' %t.pch/main.cpp -- -std=c++20 2>&1 | FileCheck --check-prefix=CHECK-TEXT %s
// CHECK-TEXT: header.h:6:1: warning: 'private:' repeats the access in effect in 'CFromHeader', that of the label before it

// In Microsoft mode, `-fms-extensions`, where a block of `__if_exists` or
// `__if_not_exists`, whose labels are only there when what it names is, can be
// in any class, also through macros, no class is checked for repeated labels.
struct CConfig
{
};
struct CIfExists
{
private:
	int mp_First;
	__if_exists(CConfig::m_Value)
	{
public:
		int m_Public;
	}
private:
	int mp_Last;
};
struct CIfNotExists
{
	__if_not_exists(CConfig)
	{
private:
		int mp_Extra;
	}
public:
	int m_Visible;
};
struct CIfExistsElsewhere
{
	__if_exists(CConfig)
	{
		int m_Configured;
	}
private:
	int mp_First;
private:
	int mp_Last;
};
// Also one spelled across a line splice.
struct CIfExistsSpliced
{
private:
	int mp_First;
	__if_exi\
sts(CConfig::m_Value)
	{
public:
		int m_Public;
	}
private:
	int mp_Last;
};
// Nor are the labels of a class whose braces a macro gives, whose body isn't
// gone through.
#define DOpenBrace {
struct CMacroBrace DOpenBrace
private:
	int mp_First;
private:
	int mp_Last;
};
// Nor of one whose body uses a macro whose definition, also through the
// macros it uses, has one, or includes a file.
#define DIfConfig __if_exists(CConfig)
#define DIfNested DIfConfig
struct CMacroExists
{
	DIfConfig
	{
	private:
		int mp_A;
	}
private:
	int mp_B;
private:
	int mp_C;
};
struct CNestedMacroExists
{
	DIfNested
	{
	private:
		int mp_A;
	}
private:
	int mp_B;
private:
	int mp_C;
};
struct CIncludesInBody
{
private:
	int mp_A;
private:
	int mp_B;
#include "struct-not-class-empty.h"
};
// Also without one.
struct CNoIfExists
{
private:
	int mp_First;
private:
	int mp_Last;
};
// Outside Microsoft mode, the default access isn't known of a class whose
// keyword an include gives, which another configuration can make the other.
// RUN: rm -rf %t.tag && mkdir -p %t.tag
// RUN: printf 'struct CChosen\n' > %t.tag/tag.h
// RUN: printf '#include "tag.h"\n{\n\tusing CType = int;\npublic:\n\tvoid f_Run();\n};\n' > %t.tag/main.cpp
// RUN: clang-tidy --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" --checks='-*,malterlib-struct-not-class' %t.tag/main.cpp -- -std=c++20 2>&1 | FileCheck --allow-empty --check-prefix=CHECK-TAG %s
// CHECK-TAG-NOT: warning:
// Outside Microsoft mode, a class whose body includes a file isn't checked,
// and one that doesn't is.
// RUN: rm -rf %t.include && mkdir -p %t.include
// RUN: printf 'struct CIncludes\n{\nprivate:\n\tint mp_A;\nprivate:\n\tint mp_B;\n#include "empty.h"\n};\nstruct CPlain\n{\nprivate:\n\tint mp_A;\nprivate:\n\tint mp_B;\n};\n' > %t.include/main.cpp
// RUN: printf '// Nothing.\n' > %t.include/empty.h
// RUN: clang-tidy --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" --checks='-*,malterlib-struct-not-class' %t.include/main.cpp -- -std=c++20 2>&1 | FileCheck --check-prefix=CHECK-INCLUDE %s
// CHECK-INCLUDE-NOT: main.cpp:5:1: warning:
// CHECK-INCLUDE: main.cpp:13:1: warning: 'private:' repeats the access in effect in 'CPlain', that of the label before it
// CHECK-INCLUDE-NOT: main.cpp:5:1: warning:
