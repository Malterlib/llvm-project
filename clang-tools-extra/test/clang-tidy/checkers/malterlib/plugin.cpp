// REQUIRES: malterlib-plugin
// RUN: %clang -fsyntax-only %s 2>&1 | FileCheck --check-prefix=CHECK-NONE --allow-empty %s
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -fplugin-arg-malterlib-checks='-*,malterlib-function-naming' %s 2>&1 | FileCheck --check-prefix=CHECK-WARNING %s
// RUN: not %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -Werror -fplugin-arg-malterlib-checks='-*,malterlib-function-naming' %s 2>&1 | FileCheck --check-prefix=CHECK-ERROR %s
// RUN: not %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -fplugin-arg-malterlib-checks='-*,malterlib-function-naming' -fplugin-arg-malterlib-warnings-as-errors='malterlib-*' %s 2>&1 | FileCheck --check-prefix=CHECK-ERROR %s
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -Werror -fplugin-arg-malterlib-checks='-*,malterlib-function-naming' -fplugin-arg-malterlib-warnings-as-errors='-*' %s 2>&1 | FileCheck --check-prefix=CHECK-WARNING %s
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -w -fplugin-arg-malterlib-checks='-*,malterlib-function-naming' %s 2>&1 | FileCheck --check-prefix=CHECK-NONE --allow-empty %s
// RUN: not %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -fplugin-arg-malterlib-unknown=1 %s 2>&1 | FileCheck --check-prefix=CHECK-UNKNOWN %s
// RUN: not %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -fplugin-arg-malterlib-checks='-*,malterlib-function-naming' '-fplugin-arg-malterlib-header-filter=[' %s 2>&1 | FileCheck --check-prefix=CHECK-REGEX %s
// RUN: echo "Checks: '-*,malterlib-function-naming'" > %t.yaml
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -fplugin-arg-malterlib-config-file=%t.yaml %s 2>&1 | FileCheck --check-prefix=CHECK-WARNING %s

// CHECK-NONE-NOT: malterlib-function-naming
// CHECK-WARNING: :[[@LINE+3]]:6: warning: global function 'Bad' should have the 'fg_' prefix [malterlib-function-naming]
// CHECK-ERROR: :[[@LINE+2]]:6: error: global function 'Bad' should have the 'fg_' prefix [malterlib-function-naming]
// CHECK-WARNING-NOT: NoLint
void Bad();
void NoLint(); // NOLINT(malterlib-function-naming)

// With -w, the checks that report what the preprocessor sees, like the naming
// of macros, don't run either, but are still there for the callbacks they gave
// it.
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -w -fplugin-arg-malterlib-checks='-*,malterlib-macro-naming' %s 2>&1 | FileCheck --check-prefix=CHECK-W-MACRO --allow-empty %s
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -fplugin-arg-malterlib-checks='-*,malterlib-macro-naming' %s 2>&1 | FileCheck --check-prefix=CHECK-MACRO-NAMING %s
// CHECK-W-MACRO-NOT: malterlib-macro-naming
// CHECK-MACRO-NAMING: :[[@LINE+1]]:9: warning: {{.*}}[malterlib-macro-naming]
#define BadMacro 1

// CHECK-UNKNOWN: error: unknown argument 'unknown=1' for the malterlib plugin
// CHECK-REGEX: error: invalid regular expression '[' for the malterlib plugin's header-filter

// The plugin checks only part of a translation unit, so it offers no fixes,
// which clang-tidy applies.
// RUN: printf 'class CFixed\n{\npublic:\n\tint m_Value;\n};\n' > %t.fix.cpp
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -fdiagnostics-parseable-fixits -fplugin-arg-malterlib-checks='-*,malterlib-struct-not-class' %t.fix.cpp 2>&1 | FileCheck --check-prefix=CHECK-NOFIX %s
// CHECK-NOFIX: warning: 'CFixed' should be declared with 'struct'
// CHECK-NOFIX-NOT: fix-it:

// Header filters apply before the diagnostics reach the compiler.
// RUN: mkdir -p %t.dir && echo "void BadInHeader();" > %t.dir/header.h
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -include %t.dir/header.h -fplugin-arg-malterlib-checks='-*,malterlib-function-naming' -fplugin-arg-malterlib-header-filter='.*' -fplugin-arg-malterlib-exclude-header-filter='header\.h' %s 2>&1 | FileCheck --check-prefix=CHECK-FILTERED %s
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -include %t.dir/header.h -fplugin-arg-malterlib-checks='-*,malterlib-function-naming' -fplugin-arg-malterlib-header-filter='header\.h' %s 2>&1 | FileCheck --check-prefix=CHECK-HEADER %s
// CHECK-FILTERED-NOT: BadInHeader
// An empty header filter, also of a configuration file, checks no header, and
// the main file.
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -include %t.dir/header.h -fplugin-arg-malterlib-checks='-*,malterlib-function-naming' '-fplugin-arg-malterlib-header-filter=' %s 2>&1 | FileCheck --check-prefix=NOHEADERS %s
// RUN: printf "Checks: '-*,malterlib-function-naming'\nHeaderFilterRegex: ''\n" > %t.empty.yaml
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -include %t.dir/header.h -fplugin-arg-malterlib-config-file=%t.empty.yaml %s 2>&1 | FileCheck --check-prefix=NOHEADERS %s
// NOHEADERS-NOT: BadInHeader
// NOHEADERS-NOT: error:
// NOHEADERS: plugin.cpp:17:6: warning: global function 'Bad' should have the 'fg_' prefix
// NOHEADERS-NOT: BadInHeader
// CHECK-HEADER: header.h:1:6: warning: global function 'BadInHeader' should have the 'fg_' prefix [malterlib-function-naming]

// The header filters match the absolute name of a header without `.` and `..`
// components, whatever relative or `..` path it was included through: an own
// header through a relative -I is reported, and one of a nested directory
// included through `../` is excluded.
// RUN: rm -rf %t.norm && mkdir -p %t.norm/repo/Source %t.norm/repo/Include %t.norm/repo/Nested
// RUN: echo 'void BadOwn();' > %t.norm/repo/Include/A.h
// RUN: echo 'void BadNestedRepo();' > %t.norm/repo/Nested/B.h
// RUN: printf '#include "A.h"\n#include "../Nested/B.h"\n' > %t.norm/repo/Source/main.cpp
// RUN: cd %t.norm/repo && %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -I Include -fplugin-arg-malterlib-checks='-*,malterlib-function-naming' -fplugin-arg-malterlib-header-filter='^%t.norm/repo/' -fplugin-arg-malterlib-exclude-header-filter='^(%t.norm/repo/Nested)/' Source/main.cpp 2>&1 | FileCheck --check-prefix=CHECK-NORMALIZED %s
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -I %t.norm/repo/Source/../Include -fplugin-arg-malterlib-checks='-*,malterlib-function-naming' -fplugin-arg-malterlib-header-filter='^%t.norm/repo/' -fplugin-arg-malterlib-exclude-header-filter='^(%t.norm/repo/Nested)/' %t.norm/repo/Source/main.cpp 2>&1 | FileCheck --check-prefix=CHECK-NORMALIZED %s
// CHECK-NORMALIZED: A.h:1:6: warning: global function 'BadOwn' should have the 'fg_' prefix [malterlib-function-naming]
// CHECK-NORMALIZED-NOT: BadNestedRepo

// With the rules of the repositories, the deepest repository a file is in
// decides whether it is checked, also the source: a header of another enabled
// repository is reported, but not one of a repository nested in it that isn't
// enabled, one its includes don't match, or one an exclude matches, and a
// source an exclude matches isn't checked at all.
// RUN: rm -rf %t.repos && mkdir -p %t.repos/Root/Source %t.repos/Root/Generated %t.repos/Root/Lib/Unchecked
// RUN: echo 'void BadLib();' > %t.repos/Root/Lib/L.h
// RUN: echo 'void BadLibOther();' > %t.repos/Root/Lib/M.h
// RUN: echo 'void BadUnchecked();' > %t.repos/Root/Lib/Unchecked/U.h
// RUN: echo 'void BadGenerated();' > %t.repos/Root/Generated/G.h
// RUN: printf '#include "../Lib/L.h"\n#include "../Lib/M.h"\n#include "../Lib/Unchecked/U.h"\n#include "../Generated/G.h"\nvoid BadSource();\n' > %t.repos/Root/Source/main.cpp
// RUN: cp %t.repos/Root/Source/main.cpp %t.repos/Root/Generated/excluded.cpp
// RUN: printf '[{"Location": "%t.repos/Root", "Tidy": {"Enabled": true, "ExcludeFiles": ["Generated/*"]}}, {"Location": "%t.repos/Root/Lib/", "Tidy": {"Enabled": true, "IncludeFiles": ["L.*", "Unchecked/*"]}}, {"Location": "%t.repos/Root/Lib/Unchecked", "Tidy": {"Enabled": false}}]' > %t.repos/rules.json
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -fplugin-arg-malterlib-checks='-*,malterlib-function-naming' -fplugin-arg-malterlib-repositories=%t.repos/rules.json %t.repos/Root/Source/main.cpp 2>&1 | FileCheck --check-prefix=CHECK-REPOS %s
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -fplugin-arg-malterlib-checks='-*,malterlib-function-naming' -fplugin-arg-malterlib-repositories=%t.repos/rules.json %t.repos/Root/Generated/excluded.cpp 2>&1 | FileCheck --check-prefix=CHECK-REPOS-EXCLUDED --allow-empty %s
// CHECK-REPOS-NOT: {{BadLibOther|BadUnchecked|BadGenerated}}
// CHECK-REPOS: L.h:1:6: warning: global function 'BadLib' should have the 'fg_' prefix [malterlib-function-naming]
// CHECK-REPOS-NOT: {{BadLibOther|BadUnchecked|BadGenerated}}
// CHECK-REPOS: main.cpp:5:6: warning: global function 'BadSource' should have the 'fg_' prefix [malterlib-function-naming]
// CHECK-REPOS-NOT: {{BadLibOther|BadUnchecked|BadGenerated}}
// CHECK-REPOS-EXCLUDED-NOT: warning

// The rules replace the header filters, and must be readable.
// RUN: not %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -fplugin-arg-malterlib-repositories=%t.repos/rules.json -fplugin-arg-malterlib-header-filter=x %s 2>&1 | FileCheck --check-prefix=CHECK-REPOS-FILTER %s
// CHECK-REPOS-FILTER: error: the malterlib plugin's repositories decide which headers are checked, so they can't be given with header filters
// RUN: printf '{"Location": 1}' > %t.repos/bad.json
// RUN: not %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -fplugin-arg-malterlib-repositories=%t.repos/bad.json %s 2>&1 | FileCheck --check-prefix=CHECK-REPOS-BAD %s
// CHECK-REPOS-BAD: error: invalid malterlib plugin repositories '{{.*}}bad.json': expected an array of repositories

// A `*` followed by `?` matches at least one character, `*?.cpp`, in the
// includes and in the excludes.
// RUN: rm -rf %t.wild && mkdir -p %t.wild/In %t.wild/Out
// RUN: echo 'void BadIncluded();' > %t.wild/In/Bad.cpp
// RUN: echo 'void BadExcluded();' > %t.wild/Out/Bad.cpp
// RUN: printf '[{"Location": "%t.wild/In", "Tidy": {"Enabled": true, "IncludeFiles": ["*?.cpp"]}}, {"Location": "%t.wild/Out", "Tidy": {"Enabled": true, "ExcludeFiles": ["*?.cpp"]}}]' > %t.wild/rules.json
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -fplugin-arg-malterlib-checks='-*,malterlib-function-naming' -fplugin-arg-malterlib-repositories=%t.wild/rules.json %t.wild/In/Bad.cpp 2>&1 | FileCheck --check-prefix=CHECK-WILD-IN %s
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -fplugin-arg-malterlib-checks='-*,malterlib-function-naming' -fplugin-arg-malterlib-repositories=%t.wild/rules.json %t.wild/Out/Bad.cpp 2>&1 | FileCheck --check-prefix=CHECK-WILD-OUT --allow-empty %s
// CHECK-WILD-IN: Bad.cpp:1:6: warning: global function 'BadIncluded' should have the 'fg_' prefix [malterlib-function-naming]
// CHECK-WILD-OUT-NOT: warning

// A header included through a symbolic link outside its repository is of the
// repository it is in.
// RUN: rm -rf %t.link && mkdir -p %t.link/repo %t.link/include
// RUN: echo 'void BadLinked();' > %t.link/repo/header.h
// RUN: ln -s %t.link/repo/header.h %t.link/include/header.h
// RUN: echo '#include <header.h>' > %t.link/repo/main.cpp
// RUN: printf '[{"Location": "%t.link/repo", "Tidy": {"Enabled": true}}]' > %t.link/rules.json
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -I %t.link/include -fplugin-arg-malterlib-checks='-*,malterlib-function-naming' -fplugin-arg-malterlib-repositories=%t.link/rules.json %t.link/repo/main.cpp 2>&1 | FileCheck --check-prefix=CHECK-LINK %s
// CHECK-LINK: header.h:1:6: warning: global function 'BadLinked' should have the 'fg_' prefix [malterlib-function-naming]

// So is a source compiled through a symbolic link.
// RUN: echo 'void BadLinkedSource();' > %t.link/repo/source.cpp
// RUN: ln -s %t.link/repo/source.cpp %t.link/include/source.cpp
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -fplugin-arg-malterlib-checks='-*,malterlib-function-naming' -fplugin-arg-malterlib-repositories=%t.link/rules.json %t.link/include/source.cpp 2>&1 | FileCheck --check-prefix=CHECK-LINK-SOURCE %s
// CHECK-LINK-SOURCE: source.cpp:1:6: warning: global function 'BadLinkedSource' should have the 'fg_' prefix [malterlib-function-naming]

// And a header filter matches a header by where it is too, when it is
// included through a symbolic link.
// RUN: echo '#include "../include/header.h"' > %t.link/repo/relative.cpp
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -fplugin-arg-malterlib-checks='-*,malterlib-function-naming' -fplugin-arg-malterlib-header-filter='^%t.link/repo/' %t.link/repo/relative.cpp 2>&1 | FileCheck --check-prefix=CHECK-LINK-FILTER %s
// CHECK-LINK-FILTER: header.h:1:6: warning: global function 'BadLinked' should have the 'fg_' prefix [malterlib-function-naming]

// But one that a symbolic link in an enabled repository leads into one that
// isn't is of that one, and so is one a header filter excludes by either name.
// RUN: rm -rf %t.into && mkdir -p %t.into/repo/External %t.into/repo/Include
// RUN: echo 'void BadExternal();' > %t.into/repo/External/api.h
// RUN: ln -s ../External/api.h %t.into/repo/Include/api.h
// RUN: echo '#include "Include/api.h"' > %t.into/repo/main.cpp
// RUN: printf '[{"Location": "%t.into/repo", "Tidy": {"Enabled": true}}, {"Location": "%t.into/repo/External", "Tidy": {"Enabled": false}}]' > %t.into/rules.json
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -fplugin-arg-malterlib-checks='-*,malterlib-function-naming' -fplugin-arg-malterlib-repositories=%t.into/rules.json %t.into/repo/main.cpp 2>&1 | FileCheck --check-prefix=CHECK-LINK-INTO --allow-empty %s
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -fplugin-arg-malterlib-checks='-*,malterlib-function-naming' -fplugin-arg-malterlib-header-filter='.*' -fplugin-arg-malterlib-exclude-header-filter='Include/' %t.into/repo/main.cpp 2>&1 | FileCheck --check-prefix=CHECK-LINK-INTO --allow-empty %s
// CHECK-LINK-INTO-NOT: BadExternal

// A source reached through `..` after a symbolic link is where the link leads,
// `Unchecked/Link/../source.cpp` in `Checked`, not in the repository its name
// is in without the `..`.
// RUN: rm -rf %t.dotdot && mkdir -p %t.dotdot/Checked/Subdir %t.dotdot/Unchecked
// RUN: echo 'void BadDotDot();' > %t.dotdot/Checked/source.cpp
// RUN: ln -s %t.dotdot/Checked/Subdir %t.dotdot/Unchecked/Link
// RUN: printf '[{"Location": "%t.dotdot/Checked", "Tidy": {"Enabled": true}}, {"Location": "%t.dotdot/Unchecked", "Tidy": {"Enabled": false}}]' > %t.dotdot/rules.json
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -fplugin-arg-malterlib-checks='-*,malterlib-function-naming' -fplugin-arg-malterlib-repositories=%t.dotdot/rules.json %t.dotdot/Unchecked/Link/../source.cpp 2>&1 | FileCheck --check-prefix=CHECK-DOTDOT %s
// CHECK-DOTDOT: source.cpp:1:6: warning: global function 'BadDotDot' should have the 'fg_' prefix [malterlib-function-naming]

// A repository whose location is reached through a symbolic link is where
// the link leads, which the real name of a linked source is compared with.
// RUN: rm -rf %t.canon && mkdir -p %t.canon/Real
// RUN: echo 'void BadCanonical();' > %t.canon/Real/source.cpp
// RUN: ln -s %t.canon/Real %t.canon/Link
// RUN: ln -s %t.canon/Real/source.cpp %t.canon/source.cpp
// RUN: printf '[{"Location": "%t.canon/Link", "Tidy": {"Enabled": true}}]' > %t.canon/rules.json
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -fplugin-arg-malterlib-checks='-*,malterlib-function-naming' -fplugin-arg-malterlib-repositories=%t.canon/rules.json %t.canon/source.cpp 2>&1 | FileCheck --check-prefix=CHECK-CANONICAL %s
// CHECK-CANONICAL: source.cpp:1:6: warning: global function 'BadCanonical' should have the 'fg_' prefix [malterlib-function-naming]

// A setting of another type is an error, not a setting that turns checking
// off.
// RUN: printf '[{"Location": "%t.canon/Real", "Tidy": {"Enabled": "true"}}]' > %t.canon/typed.json
// RUN: not %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -fplugin-arg-malterlib-repositories=%t.canon/typed.json %s 2>&1 | FileCheck --check-prefix=CHECK-REPOS-TYPED %s
// CHECK-REPOS-TYPED: error: invalid malterlib plugin repositories '{{.*}}typed.json': expected Tidy.Enabled of {{.*}}Real to be a boolean

// A declaration whose definition is in a header that is not reported, which
// the plugin doesn't look into, can name a non-const reference either way.
// RUN: mkdir -p %t.read && printf 'inline int fg_Read(int &_Value) { return *(&_Value); }\n' > %t.read/read.hpp
// RUN: printf '#include "read.hpp"\nint fg_Read(int &_Value);\n' > %t.read/main.cpp
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -fplugin-arg-malterlib-checks='-*,malterlib-parameter-naming' -fplugin-arg-malterlib-header-filter=nomatch %t.read/main.cpp 2>&1 | FileCheck --check-prefix=CHECK-UNSEEN --allow-empty %s
// CHECK-UNSEEN-NOT: warning:

// A member of a class in a class template, defined outside of it in a source
// whose header isn't reported, is checked as it is instantiated.
// RUN: mkdir -p %t.nestedmember && printf 'template <typename t_CType>\nstruct TCOuter {\n  struct CInner {\n    void f_Run(t_CType const &_Value);\n  };\n};\n' > %t.nestedmember/outer.hpp
// RUN: printf '#include "outer.hpp"\ntemplate <typename t_CType>\nvoid TCOuter<t_CType>::CInner::f_Run(t_CType const &o_Value) {}\nvoid fg_Use() {\n  TCOuter<int>::CInner Value;\n  Value.f_Run(1);\n}\n' > %t.nestedmember/main.cpp
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -fplugin-arg-malterlib-checks='-*,malterlib-parameter-naming' -fplugin-arg-malterlib-header-filter=nomatch %t.nestedmember/main.cpp 2>&1 | FileCheck --check-prefix=CHECK-NESTED-MEMBER %s
// CHECK-NESTED-MEMBER: main.cpp:3:{{[0-9]+}}: warning: {{.*}}'o_Value'{{.*}} [malterlib-parameter-naming]

// So is a member template of a class template defined outside of it.
// RUN: mkdir -p %t.membertemplate && printf 'template <typename t_CType>\nstruct TCBox {\n  template <typename tf_CType>\n  void f_Run(tf_CType const &_Value);\n};\n' > %t.membertemplate/declarations.h
// RUN: printf 'template <typename t_CType>\ntemplate <typename tf_CType>\nvoid TCBox<t_CType>::f_Run(tf_CType const &o_Value) {}\n' > %t.membertemplate/definitions.hpp
// RUN: printf '#include "declarations.h"\n#include "definitions.hpp"\nvoid fg_Use() { TCBox<int>{}.f_Run(1); }\n' > %t.membertemplate/main.cpp
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -fplugin-arg-malterlib-checks='-*,malterlib-parameter-naming' -fplugin-arg-malterlib-header-filter='definitions[.]hpp' %t.membertemplate/main.cpp 2>&1 | FileCheck --check-prefix=CHECK-MEMBER-TEMPLATE %s
// CHECK-MEMBER-TEMPLATE: definitions.hpp:3:{{[0-9]+}}: warning: {{.*}}'o_Value'{{.*}} [malterlib-parameter-naming]

// With -Wfatal-errors, the first error is fatal, like the compiler's own.
// RUN: not %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -Werror -Wfatal-errors -fplugin-arg-malterlib-checks='-*,malterlib-function-naming' %s 2>&1 | FileCheck --check-prefix=CHECK-FATAL %s
// CHECK-FATAL: fatal error: global function 'Bad' should have the 'fg_' prefix
// CHECK-FATAL-NOT: error:

// A reported header included in a namespace of a header that is not reported.
// RUN: echo 'void BadNested();' > %t.dir/nested.hpp
// RUN: echo 'namespace NOuter { void BadOuter(); }' > %t.dir/outer.h
// RUN: printf 'namespace NOuter\n{\n#include "nested.hpp"\n}\n' > %t.dir/wrapper.h
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -I %t.dir -include %t.dir/wrapper.h -fplugin-arg-malterlib-checks='-*,malterlib-function-naming' -fplugin-arg-malterlib-header-filter='nested\.hpp' %s 2>&1 | FileCheck --check-prefix=CHECK-NESTED %s
// CHECK-NESTED: nested.hpp:1:6: warning: global function 'BadNested' should have the 'fg_' prefix [malterlib-function-naming]

// Diagnostics at the end of the main file are reported too.
// RUN: printf '#define Bad 1\n' > %t.dir/eof.cpp
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -fplugin-arg-malterlib-checks='-*,malterlib-macro-naming' %t.dir/eof.cpp 2>&1 | FileCheck --check-prefix=CHECK-MACRO %s
// CHECK-MACRO: eof.cpp:1:9: warning: macro 'Bad' should have the 'D' prefix followed by upper camel case [malterlib-macro-naming]

// A diagnostic in a macro that several expansions report is reported once.
// RUN: printf '#define DField int Wrong;\nstruct CA { DField };\nstruct CB { DField };\n' > %t.dir/macro.cpp
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -fplugin-arg-malterlib-checks='-*,malterlib-variable-naming' %t.dir/macro.cpp 2>&1 | FileCheck --check-prefix=CHECK-ONCE %s
// CHECK-ONCE: macro.cpp:1:20: warning: member variable 'Wrong' should have the 'm_' prefix [malterlib-variable-naming]
// CHECK-ONCE-NOT: warning:

// A reported header included in a class of a header that is not reported.
// RUN: echo 'void BadInRecord();' > %t.dir/inner.hpp
// RUN: printf 'struct COuter\n{\n#include "inner.hpp"\n};\n' > %t.dir/record.h
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -I %t.dir -include %t.dir/record.h -fplugin-arg-malterlib-checks='-*,malterlib-function-naming' -fplugin-arg-malterlib-header-filter='inner\.hpp' %s 2>&1 | FileCheck --check-prefix=CHECK-RECORD %s
// CHECK-RECORD: inner.hpp:1:6: warning: member function 'BadInRecord' should have the 'f_' prefix [malterlib-function-naming]

// A reported header included in a class template of a header that is not
// reported.
// RUN: echo 'void BadInTemplate();' > %t.dir/inner-template.hpp
// RUN: printf 'template <typename t_CType>\nstruct TCOuter\n{\n#include "inner-template.hpp"\n};\n' > %t.dir/template.h
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -I %t.dir -include %t.dir/template.h -fplugin-arg-malterlib-checks='-*,malterlib-function-naming' -fplugin-arg-malterlib-header-filter='inner-template\.hpp' %s 2>&1 | FileCheck --check-prefix=CHECK-TEMPLATE %s
// CHECK-TEMPLATE: inner-template.hpp:1:6: warning: member function 'BadInTemplate' should have the 'f_' prefix [malterlib-function-naming]

// A reported header included in an enum or a function of one that is not is
// checked too.
// RUN: rm -rf %t.nested && mkdir -p %t.nested
// RUN: printf 'enum EStatus\n{\n#include "values.hpp"\n};\nvoid fg_Function()\n{\n#include "locals.hpp"\n}\n' > %t.nested/wrapper.h
// RUN: echo 'BadValue' > %t.nested/values.hpp
// RUN: echo 'struct Local {};' > %t.nested/locals.hpp
// RUN: echo '#include "wrapper.h"' > %t.nested/main.cpp
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -I %t.nested -fplugin-arg-malterlib-checks='-*,malterlib-type-naming' -fplugin-arg-malterlib-header-filter='(values|locals)\.hpp' %t.nested/main.cpp 2>&1 | FileCheck --check-prefix=CHECK-ENCLOSED %s
// CHECK-ENCLOSED-DAG: values.hpp:1:1: warning: enumerator 'BadValue' should have the name of its enum followed by an underscore, 'EStatus_'
// CHECK-ENCLOSED-DAG: locals.hpp:1:8: warning: class 'Local' should have the 'C' or 'IC' prefix

// A function of a header that isn't reported whose body includes one that is
// is checked for what that header writes.
// RUN: mkdir -p %t.body && printf 'void fg_Wrapper()\n{\n#include "body.hpp"\n}\n' > %t.body/wrapper.h
// RUN: printf '[](int Value) {}(1);\n' > %t.body/body.hpp
// RUN: printf '#include "wrapper.h"\n' > %t.body/main.cpp
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -fplugin-arg-malterlib-checks='-*,malterlib-parameter-naming' '-fplugin-arg-malterlib-header-filter=body\.hpp' %t.body/main.cpp 2>&1 | FileCheck --check-prefix=CHECK-BODY %s
// CHECK-BODY: body.hpp:1:8: warning: parameter 'Value' should have the '_' prefix [malterlib-parameter-naming]

// Also in a statement of the body, `if (...) { #include "nested.hpp" }`.
// RUN: mkdir -p %t.nested && printf 'void fg_Wrapper()\n{\n\tif (true)\n\t{\n#include "nested.hpp"\n\t}\n}\n' > %t.nested/wrapper.h
// RUN: printf '[](int Value) {}(1);\n' > %t.nested/nested.hpp
// RUN: printf '#include "wrapper.h"\n' > %t.nested/main.cpp
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -fplugin-arg-malterlib-checks='-*,malterlib-parameter-naming' '-fplugin-arg-malterlib-header-filter=nested\.hpp' %t.nested/main.cpp 2>&1 | FileCheck --check-prefix=CHECK-STATEMENT %s
// CHECK-STATEMENT: nested.hpp:1:8: warning: parameter 'Value' should have the '_' prefix [malterlib-parameter-naming]

// Also in an expression of the body, `auto fLambda = #include "body.hpp";`.
// RUN: mkdir -p %t.initializer && printf 'void fg_Wrapper()\n{\n\tauto fLambda =\n#include "body.hpp"\n\t;\n}\n' > %t.initializer/wrapper.h
// RUN: printf '[](int Value) {}\n' > %t.initializer/body.hpp
// RUN: printf '#include "wrapper.h"\n' > %t.initializer/main.cpp
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -fplugin-arg-malterlib-checks='-*,malterlib-parameter-naming' '-fplugin-arg-malterlib-header-filter=body\.hpp' %t.initializer/main.cpp 2>&1 | FileCheck --check-prefix=CHECK-INITIALIZER %s
// CHECK-INITIALIZER: body.hpp:1:8: warning: parameter 'Value' should have the '_' prefix [malterlib-parameter-naming]

// Also in the initializer of a variable at namespace scope.
// RUN: mkdir -p %t.variable && printf 'inline auto g_fCallback =\n#include "body.hpp"\n' > %t.variable/wrapper.h
// RUN: printf '[](int Wrong) {};\n' > %t.variable/body.hpp
// RUN: printf '#include "wrapper.h"\n' > %t.variable/main.cpp
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -std=c++20 -fplugin-arg-malterlib-checks='-*,malterlib-parameter-naming' '-fplugin-arg-malterlib-header-filter=body\.hpp' %t.variable/main.cpp 2>&1 | FileCheck --check-prefix=CHECK-VARIABLE %s
// CHECK-VARIABLE: body.hpp:1:8: warning: parameter 'Wrong' should have the '_' prefix [malterlib-parameter-naming]

// Also in the initializers of a constructor.
// RUN: mkdir -p %t.ctor && printf 'struct CWrapper\n{\n\tint m_Value;\n\tCWrapper() : m_Value(\n#include "ctor.hpp"\n\t) {}\n};\n' > %t.ctor/ctor.h
// RUN: printf '[](int Wrong) { return 1; }(1)\n' > %t.ctor/ctor.hpp
// RUN: printf '#include "ctor.h"\n' > %t.ctor/main.cpp
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -fplugin-arg-malterlib-checks='-*,malterlib-parameter-naming' '-fplugin-arg-malterlib-header-filter=ctor\.hpp' %t.ctor/main.cpp 2>&1 | FileCheck --check-prefix=CHECK-CTOR %s
// CHECK-CTOR: ctor.hpp:1:8: warning: parameter 'Wrong' should have the '_' prefix [malterlib-parameter-naming]

// A header filter of a configuration file that isn't a regular expression.
// RUN: printf "Checks: '-*,malterlib-function-naming'\nHeaderFilterRegex: '['\n" > %t.badfilter.yaml
// RUN: not %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -fplugin-arg-malterlib-config-file=%t.badfilter.yaml %s 2>&1 | FileCheck --check-prefix=CHECK-BAD-FILTER %s
// CHECK-BAD-FILTER: error: invalid regular expression in the malterlib plugin's HeaderFilterRegex:

// Also in a default argument of a declaration without a body.
// RUN: mkdir -p %t.default && printf 'void fg_Wrapper(int _Value =\n#include "default.hpp"\n);\n' > %t.default/wrapper.h
// RUN: printf '[](int Wrong) { return 1; }(1)\n' > %t.default/default.hpp
// RUN: printf '#include "wrapper.h"\n' > %t.default/main.cpp
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -std=c++20 -fplugin-arg-malterlib-checks='-*,malterlib-parameter-naming' '-fplugin-arg-malterlib-header-filter=default\.hpp' %t.default/main.cpp 2>&1 | FileCheck --check-prefix=CHECK-DEFAULT %s
// CHECK-DEFAULT: default.hpp:1:8: warning: parameter 'Wrong' should have the '_' prefix [malterlib-parameter-naming]
// Also in a default argument of the parameters of a class template.
// RUN: mkdir -p %t.templatedefault && printf 'template <typename t_FCallback = void (\n#include "params.hpp"\n)>\nstruct TCOuter {};\n' > %t.templatedefault/wrapper.hpp
// RUN: printf 'int Wrong\n' > %t.templatedefault/params.hpp
// RUN: printf '#include "wrapper.hpp"\n' > %t.templatedefault/main.cpp
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -std=c++20 -fplugin-arg-malterlib-checks='-*,malterlib-parameter-naming' '-fplugin-arg-malterlib-header-filter=params[.]hpp$' %t.templatedefault/main.cpp 2>&1 | FileCheck --check-prefix=CHECK-TEMPLATEDEFAULT %s
// CHECK-TEMPLATEDEFAULT: params.hpp:1:5: warning: parameter 'Wrong' should have the '_' prefix [malterlib-parameter-naming]
// Also in the parameters of a partial specialization.
// RUN: mkdir -p %t.partial && printf 'template <typename t_CType> struct TCWrapper;\ntemplate <\n#include "params.hpp"\n> struct TCWrapper<Wrong *> {};\n' > %t.partial/wrapper.hpp
// RUN: printf 'typename Wrong\n' > %t.partial/params.hpp
// RUN: printf '#include "wrapper.hpp"\n' > %t.partial/main.cpp
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -std=c++20 -fplugin-arg-malterlib-checks='-*,malterlib-parameter-naming' '-fplugin-arg-malterlib-header-filter=params[.]hpp$' %t.partial/main.cpp 2>&1 | FileCheck --check-prefix=CHECK-PARTIAL %s
// CHECK-PARTIAL: params.hpp:1:10: warning: template parameter 'Wrong' should have the 't_' prefix [malterlib-parameter-naming]
// Also in the bases of a class.
// RUN: mkdir -p %t.base && printf 'decltype([](int Wrong) {})\n' > %t.base/base.hpp
// RUN: printf 'template <typename t_CType> struct TCBase {};\nstruct CWrapper : TCBase<\n#include "base.hpp"\n> {};\n' > %t.base/wrapper.hpp
// RUN: printf '#include "wrapper.hpp"\n' > %t.base/main.cpp
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -std=c++20 -fplugin-arg-malterlib-checks='-*,malterlib-parameter-naming' '-fplugin-arg-malterlib-header-filter=base[.]hpp$' %t.base/main.cpp 2>&1 | FileCheck --check-prefix=CHECK-BASE %s
// CHECK-BASE: base.hpp:1:17: warning: parameter 'Wrong' should have the '_' prefix [malterlib-parameter-naming]

// A configuration of the working directory that can't be parsed, which the
// configuration of the source file doesn't inherit, isn't used.
// RUN: mkdir -p %t.otherconfig/project && printf 'Checks: [broken\n' > %t.otherconfig/.clang-tidy && printf "Checks: '-*,malterlib-function-naming'\nInheritParentConfig: false\n" > %t.otherconfig/project/.clang-tidy && printf 'void Bad();\n' > %t.otherconfig/project/main.cpp
// RUN: cd %t.otherconfig && %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -fplugin-arg-malterlib-enable project/main.cpp 2>&1 | FileCheck --check-prefix=CHECK-OTHERCONFIG %s
// CHECK-OTHERCONFIG-NOT: error
// CHECK-OTHERCONFIG: main.cpp:1:6: warning: global function 'Bad' should have the 'fg_' prefix [malterlib-function-naming]
// CHECK-OTHERCONFIG-NOT: error

// A configuration of the source file that can't be parsed is an error, also
// when one of a parent of it can.
// RUN: mkdir -p %t.childconfig/project && printf "Checks: '-*,malterlib-function-naming'\n" > %t.childconfig/.clang-tidy && printf 'Checks: [broken\n' > %t.childconfig/project/.clang-tidy && printf 'void fg_Ok();\n' > %t.childconfig/project/main.cpp
// RUN: cd %t.childconfig/project && not %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -fplugin-arg-malterlib-enable main.cpp 2>&1 | FileCheck --check-prefix=CHECK-CHILDCONFIG %s
// CHECK-CHILDCONFIG: error: invalid malterlib plugin configuration

// A configuration file that can't be parsed is an error of the compilation.
// RUN: mkdir -p %t.badconfig && printf 'Checks: [broken\n' > %t.badconfig/.clang-tidy && printf 'void Bad();\n' > %t.badconfig/main.cpp
// RUN: not %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -fplugin-arg-malterlib-enable %t.badconfig/main.cpp 2>&1 | FileCheck --check-prefix=CHECK-BADCONFIG %s
// CHECK-BADCONFIG: error: invalid malterlib plugin configuration '{{.*}}.clang-tidy':

// Round 129: the leading requires clause of a function template is looked
// into for a header that is reported.
// RUN: rm -rf %t.requires && mkdir -p %t.requires
// RUN: printf '[](int Wrong) { return true; }(0)\n' > %t.requires/constraint.hpp
// RUN: printf 'template <typename tf_CType> requires (\n#include "constraint.hpp"\n)\nvoid fg_Test() {}\n' > %t.requires/wrapper.hpp
// RUN: printf '#include "wrapper.hpp"\n' > %t.requires/main.cpp
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -std=c++20 -fsyntax-only -fplugin-arg-malterlib-checks='-*,malterlib-parameter-naming' '-fplugin-arg-malterlib-header-filter=constraint[.]hpp$' %t.requires/main.cpp 2>&1 | FileCheck --check-prefix=CHECK-CONSTRAINT %s
// CHECK-CONSTRAINT: constraint.hpp:1:8: warning: parameter 'Wrong' should have the '_' prefix [malterlib-parameter-naming]

// Round 133: a diagnostic of a macro argument that the macro expands more than
// once is forwarded once.
// RUN: printf '#define DFields(d_Name) struct COne { int d_Name; }; struct CTwo { int d_Name; };\nDFields(Value)\n' > %t.twice.cpp
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -std=c++20 -fsyntax-only -fplugin-arg-malterlib-checks='-*,malterlib-variable-naming' %t.twice.cpp 2>&1 | FileCheck --check-prefix=CHECK-TWICE --implicit-check-not=warning: %s
// CHECK-TWICE: twice.cpp:2:9: warning: member variable 'Value' should have the 'm_' prefix [malterlib-variable-naming]

// The uses of an alias template in a header the plugin doesn't report in tell
// what its parameters are too: there the alias is of an integer, here of a
// boolean, so it isn't a boolean.
// RUN: mkdir -p %t.aliasuses && printf 'template <typename t_FSignature> struct TCFunction {};\ntemplate <typename t_CTraits>\nusing TCCallback = TCFunction<void (typename t_CTraits::CValue _Value)>;\n' > %t.aliasuses/alias.h
// RUN: printf '#include "alias.h"\nstruct CIntTraits { using CValue = int; };\ntemplate <typename t_CTraits>\nstruct TCUse { TCCallback<t_CTraits> m_fCallback; };\ninline TCUse<CIntTraits> g_Use{};\n' > %t.aliasuses/use.h
// RUN: printf '#include "use.h"\nstruct CBoolTraits { using CValue = bool; };\nTCCallback<CBoolTraits> g_fCallback;\n' > %t.aliasuses/main.cpp
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -std=c++20 -fsyntax-only -Werror -fplugin-arg-malterlib-checks='-*,malterlib-conceptual-prefix' '-fplugin-arg-malterlib-header-filter=alias[.]h$' %t.aliasuses/main.cpp 2>&1 | FileCheck --check-prefix=CHECK-ALIASUSES --allow-empty %s
// CHECK-ALIASUSES-NOT: warning:
// CHECK-ALIASUSES-NOT: error:

// The configuration is read through the file system of the compilation, also
// a `.clang-tidy` and a configuration file that an overlay gives, which name
// the Malterlib options file.
// RUN: rm -rf %t.vfs && mkdir -p %t.vfs/src %t.vfs/overlaid
// RUN: echo 'void BadOverlaid();' > %t.vfs/src/main.cpp
// RUN: printf "Checks: '-*,malterlib-function-naming'\nCheckOptions:\n  malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'\n" > %t.vfs/overlaid/config.yaml
// RUN: printf '{"version": 0, "roots": [{"name": "%t.vfs/src", "type": "directory", "contents": [{"name": ".clang-tidy", "type": "file", "external-contents": "%t.vfs/overlaid/config.yaml"}, {"name": "given.yaml", "type": "file", "external-contents": "%t.vfs/overlaid/config.yaml"}]}]}' > %t.vfs/overlay.yaml
// RUN: %clang -fsyntax-only -ivfsoverlay %t.vfs/overlay.yaml -fplugin-arg-malterlib-enable %t.vfs/src/main.cpp 2>&1 | FileCheck --check-prefix=CHECK-VFS %s
// RUN: %clang -fsyntax-only -ivfsoverlay %t.vfs/overlay.yaml -fplugin-arg-malterlib-config-file=%t.vfs/src/given.yaml %t.vfs/src/main.cpp 2>&1 | FileCheck --check-prefix=CHECK-VFS %s
// CHECK-VFS: warning: global function 'BadOverlaid' should have the 'fg_' prefix [malterlib-function-naming]
// Also one a configuration file inherits.
// RUN: mkdir -p %t.vfs/src/child && echo 'void BadOverlaid();' > %t.vfs/src/child/main.cpp
// RUN: printf 'InheritParentConfig: true\n' > %t.vfs/src/child/config.yaml
// RUN: %clang -fsyntax-only -ivfsoverlay %t.vfs/overlay.yaml -fplugin-arg-malterlib-config-file=%t.vfs/src/child/config.yaml %t.vfs/src/child/main.cpp 2>&1 | FileCheck --check-prefix=CHECK-VFS %s

// The configuration files a source's checks use are dependencies of its
// compilation, `-MD`, also the `.clang-tidy` it inherits and the Malterlib
// options file, so that a build checks it again when one changes.
// RUN: rm -rf %t.deps && mkdir -p %t.deps/project/child
// RUN: cp %S/Inputs/ClangTidyOptions.yaml %t.deps/project/options.yaml
// RUN: printf "Checks: '-*,malterlib-function-naming'\nCheckOptions:\n  malterlib.OptionsFile: '%t.deps/project/options.yaml'\n" > %t.deps/project/.clang-tidy
// RUN: printf 'InheritParentConfig: true\n' > %t.deps/project/child/.clang-tidy
// RUN: echo 'void fg_Good();' > %t.deps/project/child/main.cpp
// RUN: %clang -fsyntax-only -MD -MF %t.deps/main.d -fplugin-arg-malterlib-enable %t.deps/project/child/main.cpp
// RUN: FileCheck --check-prefix=CHECK-DEPS --input-file=%t.deps/main.d %s
// CHECK-DEPS-DAG: project/.clang-tidy
// CHECK-DEPS-DAG: project/child/.clang-tidy
// CHECK-DEPS-DAG: project/options.yaml
