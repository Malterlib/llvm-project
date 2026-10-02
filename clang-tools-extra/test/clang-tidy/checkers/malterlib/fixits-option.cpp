// The checks attach their fix-its only when the configuration sets
// malterlib.FixIts. Without it, the default, and with it false, they report
// the same warnings and notes, and no fix is applied or exported.
// RUN: %check_clang_tidy -std=c++20-or-later %s malterlib-struct-not-class,malterlib-unused-parameter-cast %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}"
// RUN: diff %t.orig %t.cpp
// The other runs read the same source without the FileCheck lines, as
// %check_clang_tidy does, so the source lines clang-tidy shows have none.
// RUN: cp %t.orig %t.in.cpp
// RUN: clang-tidy --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml', malterlib.FixIts: false}}" --checks='-*,malterlib-struct-not-class,malterlib-unused-parameter-cast' --export-fixes=%t.off.yaml %t.in.cpp -- -std=c++20 2>&1 | FileCheck --check-prefix=CHECK-MESSAGES --implicit-check-not='{{warning|error}}:' %s
// RUN: FileCheck --check-prefix=CHECK-EXPORT --implicit-check-not=ReplacementText --input-file=%t.off.yaml %s
// With it on, the fixes are applied, with the same warnings.
// RUN: clang-tidy --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml', malterlib.FixIts: true}}" --checks='-*,malterlib-struct-not-class,malterlib-unused-parameter-cast' --export-fixes=%t.on.yaml %t.in.cpp -- -std=c++20 2>&1 | FileCheck --check-prefix=CHECK-MESSAGES --implicit-check-not='{{warning|error}}:' %s
// RUN: FileCheck --check-prefix=CHECK-EXPORT-ON --input-file=%t.on.yaml %s
// RUN: cp %t.orig %t.fixed.cpp
// RUN: clang-tidy --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml', malterlib.FixIts: 1}}" --checks='-*,malterlib-struct-not-class,malterlib-unused-parameter-cast' -fix %t.fixed.cpp -- -std=c++20 2>&1 | FileCheck --check-prefix=CHECK-MESSAGES --implicit-check-not='{{warning|error}}:' %s
// RUN: FileCheck --check-prefix=CHECK-FIXED --input-file=%t.fixed.cpp %s
// RUN: %clang -fsyntax-only -std=c++20 %t.fixed.cpp
// A value that isn't a boolean is reported and leaves fix-its off.
// RUN: clang-tidy --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml', malterlib.FixIts: maybe}}" --checks='-*,malterlib-struct-not-class,malterlib-unused-parameter-cast' --export-fixes=%t.bad.yaml %t.in.cpp -- -std=c++20 2>&1 | FileCheck --check-prefixes=CHECK-BAD,CHECK-MESSAGES --implicit-check-not='{{warning|error}}:' %s
// RUN: FileCheck --check-prefix=CHECK-EXPORT --implicit-check-not=ReplacementText --input-file=%t.bad.yaml %s
// The option is one the configuration may name.
// RUN: clang-tidy --verify-config --config="{Checks: '-*,malterlib-struct-not-class', CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml', malterlib.FixIts: true}}" 2>&1 | FileCheck --check-prefix=CHECK-VERIFY --implicit-check-not='{{warning|error}}:' %s
// The plugin offers no fixes, with the option on or off.
// RUN: rm -rf %t.plugin && mkdir -p %t.plugin
// RUN: %python %S/Inputs/fixit-inputs.py plugin-config-on %t.plugin/on.yaml
// RUN: %python %S/Inputs/fixit-inputs.py plugin-config-off %t.plugin/off.yaml
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -std=c++20 -fdiagnostics-parseable-fixits -fplugin-arg-malterlib-config-file=%t.plugin/off.yaml %t.in.cpp 2>&1 | FileCheck --check-prefix=CHECK-PLUGIN --implicit-check-not=fix-it: %s
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -std=c++20 -fdiagnostics-parseable-fixits -fplugin-arg-malterlib-config-file=%t.plugin/on.yaml %t.in.cpp 2>&1 | FileCheck --check-prefix=CHECK-PLUGIN --implicit-check-not=fix-it: %s

// CHECK-BAD: warning: invalid configuration value 'maybe' for option 'malterlib.FixIts'; expected a bool [clang-tidy-config]
// CHECK-VERIFY: No config errors detected.
// CHECK-EXPORT: DiagnosticName: malterlib-struct-not-class
// CHECK-EXPORT: DiagnosticName: malterlib-unused-parameter-cast
// CHECK-EXPORT-ON: DiagnosticName: malterlib-struct-not-class
// CHECK-EXPORT-ON: ReplacementText: struct
// CHECK-PLUGIN-DAG: in.cpp:{{[0-9]+}}:{{[0-9]+}}: warning: 'CExample' should be declared with 'struct'
// CHECK-PLUGIN-DAG: in.cpp:{{[0-9]+}}:{{[0-9]+}}: warning: 'public:' repeats the access
// CHECK-PLUGIN-DAG: in.cpp:{{[0-9]+}}:{{[0-9]+}}: warning: parameter '_Value' is silenced

class CExample;
class CExample
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'CExample' should be declared with 'struct', like Malterlib types are [malterlib-struct-not-class]
{
public:
	int m_Value;
};
// CHECK-FIXED: {{^}}struct CExample;
// CHECK-FIXED-NEXT: {{^}}struct CExample
// CHECK-FIXED-NEXT: {{^}}//{{$}}
// CHECK-FIXED-NEXT: {{^}}{
// CHECK-FIXED-EMPTY:
// CHECK-FIXED-NEXT: {{^}}	int m_Value;

struct CRepeated
{
public:
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'public:' repeats the access in effect in 'CRepeated', the default of a struct [malterlib-struct-not-class]
	int m_Value;
};
// CHECK-FIXED: {{^}}struct CRepeated
// CHECK-FIXED-NEXT: {{^}}{
// CHECK-FIXED-EMPTY:
// CHECK-FIXED-NEXT: {{^}}//{{$}}

void fg_Unused(int _Value)
{
	(void)_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: parameter '_Value' is silenced with a cast to void; leave an unused parameter named, without the cast [malterlib-unused-parameter-cast]
}
// CHECK-FIXED: {{^}}void fg_Unused(int _Value)
// CHECK-FIXED-NEXT: {{^}}{
// CHECK-FIXED-NEXT: {{^}}//{{$}}
