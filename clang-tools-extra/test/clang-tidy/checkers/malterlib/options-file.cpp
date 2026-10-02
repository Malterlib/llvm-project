// The Malterlib checks need the Malterlib options file, which a
// configuration names in malterlib.OptionsFile, relative to the directory of
// the configuration file, or the plugin is given.
// RUN: rm -rf %t && mkdir -p %t/root/Core %t/root/Module %t/root/Edited %t/elsewhere
// RUN: cp %S/Inputs/ClangTidyOptions.yaml %t/root/Core/ClangTidyOptions.yaml
// RUN: printf 'void swap(int &, int &);\nvoid clear();\nstruct CValue\n{\n\tvoid count();\n};\n' > %t/root/Module/main.cpp
// RUN: cp %t/root/Module/main.cpp %t/root/Edited/main.cpp
// RUN: printf "Checks: '-*,malterlib-function-naming'\nCheckOptions:\n  malterlib.OptionsFile: ../Core/ClangTidyOptions.yaml\n" > %t/root/Module/.clang-tidy

// Relative to the .clang-tidy, from another directory, by clang-tidy and by
// the plugin: a free `swap` is a protocol, a free `clear` and a public
// `count` are not.
// RUN: cd %t/elsewhere && clang-tidy %t/root/Module/main.cpp -- 2>&1 | FileCheck --check-prefix=CHECK-NAMES %s
// RUN: cd %t/elsewhere && %clang -fsyntax-only -fplugin-arg-malterlib-enable %t/root/Module/main.cpp 2>&1 | FileCheck --check-prefix=CHECK-NAMES %s
// CHECK-NAMES-NOT: 'swap'
// CHECK-NAMES: main.cpp:2:6: warning: global function 'clear' should have the 'fg_' prefix [malterlib-function-naming]
// CHECK-NAMES-NOT: 'count'
// CHECK-NAMES-NOT: error:

// The plugin's options-file, relative to the current directory.
// RUN: cd %t/root && %clang -fsyntax-only -fplugin-arg-malterlib-checks='-*,malterlib-function-naming' -fplugin-arg-malterlib-options-file=Core/ClangTidyOptions.yaml %t/root/Edited/main.cpp 2>&1 | FileCheck --check-prefix=CHECK-NAMES %s

// The lists edit what the language and the standard library have, and then
// the options file: a name adds one, `-Name` removes one and `-*` all, by the
// option of all checks and then by the one of the check.
// RUN: printf "Checks: '-*,malterlib-function-naming'\nCheckOptions:\n  malterlib.OptionsFile: ../Core/ClangTidyOptions.yaml\n  LowerCaseFreeFunctions: '-swap;clear'\n" > %t/root/Edited/.clang-tidy
// RUN: clang-tidy %t/root/Edited/main.cpp -- 2>&1 | FileCheck --check-prefix=CHECK-EDITED %s
// CHECK-EDITED: main.cpp:1:6: warning: global function 'swap' should have the 'fg_' prefix [malterlib-function-naming]
// CHECK-EDITED-NOT: 'clear'
// RUN: printf "Checks: '-*,malterlib-function-naming'\nCheckOptions:\n  malterlib.OptionsFile: ../Core/ClangTidyOptions.yaml\n  LowerCaseFreeFunctions: clear\n  malterlib-function-naming.LowerCaseFreeFunctions: '-*;swap'\n" > %t/root/Edited/.clang-tidy
// RUN: clang-tidy %t/root/Edited/main.cpp -- 2>&1 | FileCheck --check-prefix=CHECK-LOCAL %s
// CHECK-LOCAL-NOT: 'swap'
// CHECK-LOCAL: main.cpp:2:6: warning: global function 'clear' should have the 'fg_' prefix [malterlib-function-naming]

// An options file whose lists are sequences or strings edits them too.
// RUN: printf "LowerCaseFreeFunctions:\n  - -swap\n  - clear\nMoveFunctions: 'fg_Move;fg_Take'\n" > %t/root/Core/Other.yaml
// RUN: clang-tidy --checks='-*,malterlib-function-naming' --config="{CheckOptions: {malterlib.OptionsFile: '%t/root/Core/Other.yaml'}}" %t/root/Module/main.cpp -- 2>&1 | FileCheck --check-prefix=CHECK-EDITED %s

// Without it the checks report an error, which fails the run and the compile.
// RUN: not clang-tidy --checks='-*,malterlib-function-naming' --config='{}' %t/root/Module/main.cpp -- 2>&1 | FileCheck --check-prefix=CHECK-UNSET %s
// RUN: printf "Checks: '-*'\n" > %t/empty.yaml
// RUN: not %clang -fsyntax-only -fplugin-arg-malterlib-checks='-*,malterlib-function-naming' -fplugin-arg-malterlib-config-file=%t/empty.yaml %t/root/Module/main.cpp 2>&1 | FileCheck --check-prefix=CHECK-UNSET %s
// Also with -w, where the checks don't run, but their configuration is read.
// RUN: not %clang -fsyntax-only -w -fplugin-arg-malterlib-checks='-*,malterlib-function-naming' -fplugin-arg-malterlib-config-file=%t/empty.yaml %t/root/Module/main.cpp 2>&1 | FileCheck --check-prefix=CHECK-UNSET %s
// RUN: %clang -fsyntax-only -w -fplugin-arg-malterlib-checks='-*,malterlib-function-naming' -fplugin-arg-malterlib-options-file=%t/root/Core/ClangTidyOptions.yaml %t/root/Module/main.cpp 2>&1 | FileCheck --check-prefix=CHECK-NO-WARNINGS --allow-empty %s
// CHECK-NO-WARNINGS-NOT: warning:
// CHECK-UNSET: error: the Malterlib checks need the option 'malterlib.OptionsFile', the Malterlib options file, like 'malterlib.OptionsFile: Malterlib/Core/ClangTidyOptions.yaml' in the CheckOptions of the .clang-tidy file at the root, relative to the directory of that file [clang-tidy-config]

// Once, also when the checks are of another language version, which aren't
// run, each made and dropped.
// RUN: not clang-tidy --checks='-*,malterlib-async-destroy,malterlib-coawait-precedence,malterlib-coroutine-unsafe-this' --config='{}' %t/root/Module/main.cpp -- -std=c++17 2>&1 | FileCheck --check-prefix=CHECK-UNSET-ONCE %s
// CHECK-UNSET-ONCE: 1 error generated.
// CHECK-UNSET-ONCE: error: the Malterlib checks need the option 'malterlib.OptionsFile'
// Nor when it can't be read, or isn't a mapping of names to lists.
// RUN: not clang-tidy --checks='-*,malterlib-function-naming' --config="{CheckOptions: {malterlib.OptionsFile: '%t/missing.yaml'}}" %t/root/Module/main.cpp -- 2>&1 | FileCheck --check-prefix=CHECK-MISSING %s
// CHECK-MISSING: error: can't read the Malterlib options file '{{.*}}missing.yaml': {{.*}} [clang-tidy-config]
// RUN: printf -- "- MoveFunctions\n" > %t/list.yaml
// RUN: not clang-tidy --checks='-*,malterlib-function-naming' --config="{CheckOptions: {malterlib.OptionsFile: '%t/list.yaml'}}" %t/root/Module/main.cpp -- 2>&1 | FileCheck --check-prefix=CHECK-NOTMAP %s
// CHECK-NOTMAP: error: '{{.*}}list.yaml' is not a mapping of option names to values [clang-tidy-config]
// So is an empty file, or one with only comments, but not an empty mapping.
// RUN: printf "" > %t/emptyfile.yaml
// RUN: not clang-tidy --checks='-*,malterlib-function-naming' --config="{CheckOptions: {malterlib.OptionsFile: '%t/emptyfile.yaml'}}" %t/root/Module/main.cpp -- 2>&1 | FileCheck --check-prefix=CHECK-EMPTYFILE %s
// RUN: not clang-tidy --checks='-*,malterlib-function-naming' --config="{CheckOptions: {malterlib.OptionsFile: '%t/emptyfile.yaml'}}" --verify-config 2>&1 | FileCheck --check-prefix=CHECK-EMPTYFILE %s
// RUN: not %clang -fsyntax-only -fplugin-arg-malterlib-checks='-*,malterlib-function-naming' -fplugin-arg-malterlib-options-file=%t/emptyfile.yaml %t/root/Module/main.cpp 2>&1 | FileCheck --check-prefix=CHECK-EMPTYFILE %s
// CHECK-EMPTYFILE: error: '{{.*}}emptyfile.yaml' is not a mapping of option names to values
// RUN: printf "# Only a comment\n---\n" > %t/comment.yaml
// RUN: not clang-tidy --checks='-*,malterlib-function-naming' --config="{CheckOptions: {malterlib.OptionsFile: '%t/comment.yaml'}}" %t/root/Module/main.cpp -- 2>&1 | FileCheck --check-prefix=CHECK-COMMENT %s
// CHECK-COMMENT: error: '{{.*}}comment.yaml' is not a mapping of option names to values [clang-tidy-config]
// RUN: printf "{}\n" > %t/emptymap.yaml
// RUN: clang-tidy --checks='-*,malterlib-function-naming' --config="{CheckOptions: {malterlib.OptionsFile: '%t/emptymap.yaml'}}" %t/root/Module/main.cpp -- 2>&1 | FileCheck --check-prefix=CHECK-EMPTYMAP --allow-empty %s
// CHECK-EMPTYMAP-NOT: error:
// RUN: printf "MoveFunctions:\n  - {a: b}\n" > %t/nested.yaml
// RUN: not clang-tidy --checks='-*,malterlib-function-naming' --config="{CheckOptions: {malterlib.OptionsFile: '%t/nested.yaml'}}" %t/root/Module/main.cpp -- 2>&1 | FileCheck --check-prefix=CHECK-NESTED %s
// CHECK-NESTED: error: the option 'MoveFunctions' in '{{.*}}nested.yaml' has an element that is not a string [clang-tidy-config]

// Nor when it isn't one YAML document that parses to its end, with a value
// for each name.
// RUN: printf '"unterminated\n' > %t/quote.yaml
// RUN: not clang-tidy --checks='-*,malterlib-function-naming' --config="{CheckOptions: {malterlib.OptionsFile: '%t/quote.yaml'}}" %t/root/Module/main.cpp -- 2>&1 | FileCheck --check-prefix=CHECK-QUOTE %s
// CHECK-QUOTE: error: can't parse '{{.*}}quote.yaml': Expected quote at end of scalar [clang-tidy-config]
// RUN: printf 'LowerCaseFreeFunctions: [clear]\n  stray\n' > %t/stray.yaml
// RUN: not clang-tidy --checks='-*,malterlib-function-naming' --config="{CheckOptions: {malterlib.OptionsFile: '%t/stray.yaml'}}" %t/root/Module/main.cpp -- 2>&1 | FileCheck --check-prefix=CHECK-STRAY %s
// CHECK-STRAY: error: the option 'stray' in '{{.*}}stray.yaml' is not a string or a sequence of strings [clang-tidy-config]
// RUN: printf 'MoveFunctions: [fg_Move]\n---\nMoveFunctions: [\n' > %t/trailing.yaml
// RUN: not %clang -fsyntax-only -fplugin-arg-malterlib-checks='-*,malterlib-function-naming' -fplugin-arg-malterlib-options-file=%t/trailing.yaml %t/root/Module/main.cpp 2>&1 | FileCheck --check-prefix=CHECK-TRAILING %s
// CHECK-TRAILING: error: can't parse '{{.*}}trailing.yaml': Could not find closing ]! [clang-tidy-config]
// RUN: printf 'MoveFunctions: [fg_Move]\n---\nMoveFunctions: [fg_Take]\n' > %t/documents.yaml
// RUN: not clang-tidy --checks='-*,malterlib-function-naming' --config="{CheckOptions: {malterlib.OptionsFile: '%t/documents.yaml'}}" %t/root/Module/main.cpp -- 2>&1 | FileCheck --check-prefix=CHECK-DOCUMENTS %s
// CHECK-DOCUMENTS: error: '{{.*}}documents.yaml' has more than one YAML document [clang-tidy-config]

// RUN: printf 'LowerCaseFreeFunctions: clear\nLowerCaseFreeFunctions: swap\n' > %t/twice.yaml
// RUN: not clang-tidy --checks='-*,malterlib-function-naming' --config="{CheckOptions: {malterlib.OptionsFile: '%t/twice.yaml'}}" %t/root/Module/main.cpp -- 2>&1 | FileCheck --check-prefix=CHECK-TWICE %s
// CHECK-TWICE: error: the option 'LowerCaseFreeFunctions' is given twice in '{{.*}}twice.yaml' [clang-tidy-config]

// A `..` after a symbolic link is in the directory the link is to.
// RUN: mkdir -p %t/linked/target/child %t/linked/config
// RUN: ln -s ../target/child %t/linked/config/alias
// RUN: printf 'LowerCaseFreeFunctions: clear\n' > %t/linked/target/options.yaml
// RUN: printf "Checks: '-*,malterlib-function-naming'\nCheckOptions:\n  malterlib.OptionsFile: alias/../options.yaml\n" > %t/linked/config/.clang-tidy
// RUN: printf 'void clear();\n' > %t/linked/config/main.cpp
// RUN: clang-tidy %t/linked/config/main.cpp -- 2>&1 | FileCheck --check-prefix=CHECK-LINKED --allow-empty %s
// RUN: %clang -fsyntax-only -fplugin-arg-malterlib-checks='-*,malterlib-function-naming' -fplugin-arg-malterlib-options-file=%t/linked/config/alias/../options.yaml %t/linked/config/main.cpp 2>&1 | FileCheck --check-prefix=CHECK-LINKED --allow-empty %s
// CHECK-LINKED-NOT: {{error|warning}}:

// The option of all checks of a shared list edits that list, which the option
// of a check of the same name is made of, not that option again: moving
// nothing but fg_Move keeps the forwards of parameter-ownership.
// RUN: printf "MoveFunctions: '-*;fg_Move'\nForwardFunctions: fg_Forward\n" > %t/reset.yaml
// RUN: printf 'struct CStr\n{\n\tCStr();\n\tCStr(CStr const &);\n\tCStr(CStr &&);\n\t~CStr();\n};\ntemplate <typename t_CType> t_CType &&fg_Forward(t_CType &);\nvoid fg_Consume(CStr &&);\nvoid fg_Test(CStr _Value)\n{\n\tfg_Consume(fg_Forward<CStr>(_Value));\n}\n' > %t/forward.cpp
// RUN: clang-tidy --checks='-*,malterlib-parameter-ownership' --config="{CheckOptions: {malterlib.OptionsFile: '%t/reset.yaml'}}" %t/forward.cpp -- 2>&1 | FileCheck --check-prefix=CHECK-RESET %s
// CHECK-RESET: forward.cpp:10:19: warning: parameter '_Value' takes 'CStr' by value; take it by '&&', since the function consumes it [malterlib-parameter-ownership]

// The option of a check of a shared list takes the edits of the file and then
// of the configuration, each of all checks and then of the check: one of all
// checks of the configuration comes after one of the check of the file.
// RUN: printf "malterlib-coroutine-lambda-call.SafeCallees: '-f_RegisterCommand'\n" > %t/order.yaml
// RUN: clang-tidy --checks='-*,malterlib-coroutine-lambda-call' --config="{CheckOptions: {malterlib.OptionsFile: '%t/order.yaml', SafeCallees: f_RegisterCommand}}" %S/Inputs/options-file-safe-callee.cpp -- -std=c++20 -I %S/Inputs 2>&1 | FileCheck --check-prefix=CHECK-ORDER --allow-empty %s
// CHECK-ORDER-NOT: warning:
// RUN: clang-tidy --checks='-*,malterlib-coroutine-lambda-call' --config="{CheckOptions: {malterlib.OptionsFile: '%t/order.yaml'}}" %S/Inputs/options-file-safe-callee.cpp -- -std=c++20 -I %S/Inputs 2>&1 | FileCheck --check-prefix=CHECK-REMOVED %s
// CHECK-REMOVED: options-file-safe-callee.cpp:{{[0-9]+}}:{{[0-9]+}}: warning: {{.*}}[malterlib-coroutine-lambda-call]

// A boolean option is what clang-tidy reads as one, `False` and `0` too, and a
// value that isn't one, or a number that isn't, is reported.
// RUN: printf '#include "malterlib-concurrency.h"\nTCFuture<int> fg_Suspend();\nTCFuture<int> fg_Test(int *_pValue)\n{\n\tco_await fg_Suspend();\n\tco_return *_pValue;\n}\n' > %t/pointer.cpp
// RUN: clang-tidy --checks='-*,malterlib-coroutine-reference-parameter' --config="{CheckOptions: {malterlib.OptionsFile: '%t/root/Core/ClangTidyOptions.yaml', malterlib-coroutine-reference-parameter.CheckPointers: False}}" %t/pointer.cpp -- -std=c++20 -I %S/Inputs 2>&1 | FileCheck --check-prefix=CHECK-NOPOINTERS --allow-empty %s
// RUN: clang-tidy --checks='-*,malterlib-coroutine-reference-parameter' --config="{CheckOptions: {malterlib.OptionsFile: '%t/root/Core/ClangTidyOptions.yaml', CheckPointers: 0}}" %t/pointer.cpp -- -std=c++20 -I %S/Inputs 2>&1 | FileCheck --check-prefix=CHECK-NOPOINTERS --allow-empty %s
// CHECK-NOPOINTERS-NOT: _pValue
// RUN: clang-tidy --checks='-*,malterlib-coroutine-reference-parameter,malterlib-function-naming' --config="{CheckOptions: {malterlib.OptionsFile: '%t/root/Core/ClangTidyOptions.yaml', malterlib-coroutine-reference-parameter.CheckPointers: Maybe, MinimumNameLength: x}}" %t/pointer.cpp -- -std=c++20 -I %S/Inputs 2>&1 | FileCheck --check-prefix=CHECK-BADVALUE %s
// CHECK-BADVALUE-DAG: warning: invalid configuration value 'Maybe' for option 'malterlib-coroutine-reference-parameter.CheckPointers'; expected a bool [clang-tidy-config]
// CHECK-BADVALUE-DAG: warning: invalid configuration value 'x' for option 'MinimumNameLength'; expected an integer [clang-tidy-config]
// CHECK-BADVALUE-DAG: pointer.cpp:3:28: warning: TCFuture coroutine 'fg_Test' uses '_pValue' after a suspension

// A configuration that inherits another's comes after it, also a plain option
// after one with the name of the check.
// RUN: mkdir -p %t/priority/child
// RUN: printf "Checks: '-*,malterlib-coroutine-reference-parameter'\nCheckOptions:\n  malterlib.OptionsFile: %t/root/Core/ClangTidyOptions.yaml\n  malterlib-coroutine-reference-parameter.CheckPointers: true\n" > %t/priority/.clang-tidy
// RUN: printf "InheritParentConfig: true\nCheckOptions:\n  CheckPointers: false\n" > %t/priority/child/.clang-tidy
// RUN: cp %t/pointer.cpp %t/priority/child/pointer.cpp
// RUN: clang-tidy %t/priority/child/pointer.cpp -- -std=c++20 -I %S/Inputs 2>&1 | FileCheck --check-prefix=CHECK-NOPOINTERS --allow-empty %s

// Its list edits come after the other's, which they don't take the place of.
// RUN: mkdir -p %t/edits/child
// RUN: printf "Checks: '-*,malterlib-function-naming'\nCheckOptions:\n  malterlib.OptionsFile: %t/root/Core/ClangTidyOptions.yaml\n  LowerCaseFreeFunctions: alpha\n" > %t/edits/.clang-tidy
// RUN: printf "InheritParentConfig: true\nCheckOptions:\n  LowerCaseFreeFunctions: beta\n" > %t/edits/child/.clang-tidy
// RUN: printf 'void alpha();\nvoid beta();\nvoid gamma();\n' > %t/edits/child/edits.cpp
// RUN: clang-tidy %t/edits/child/edits.cpp -- -std=c++20 2>&1 | FileCheck --check-prefix=CHECK-EDITS --implicit-check-not='{{alpha|beta}}' %s
// CHECK-EDITS: edits.cpp:3:6: warning: {{.*}}'gamma'

// The options file is read through the file system of the files, like one
// that --vfsoverlay replaces.
// RUN: mkdir -p %t/overlay
// RUN: printf '{}\n' > %t/overlay/options.yaml
// RUN: printf 'LowerCaseFreeFunctions: clear\n' > %t/overlay/replacement.yaml
// RUN: printf "{'version': 0, 'roots': [{'name': '%t/overlay', 'type': 'directory', 'contents': [{'name': 'options.yaml', 'type': 'file', 'external-contents': '%t/overlay/replacement.yaml'}]}]}\n" > %t/overlay.yaml
// RUN: clang-tidy --vfsoverlay=%t/overlay.yaml --checks='-*,malterlib-function-naming' --config="{CheckOptions: {malterlib.OptionsFile: '%t/overlay/options.yaml'}}" %t/root/Module/main.cpp -- 2>&1 | FileCheck --check-prefix=CHECK-BLOCK --allow-empty %s
// RUN: clang-tidy --vfsoverlay=%t/overlay.yaml --checks='-*,malterlib-function-naming' --config="{CheckOptions: {malterlib.OptionsFile: '%t/overlay/options.yaml'}}" --dump-config %t/root/Module/main.cpp -- | FileCheck --check-prefix=CHECK-DUMP-OVERLAY %s
// CHECK-DUMP-OVERLAY: malterlib-function-naming.LowerCaseFreeFunctions: '-*;{{.*}};clear'

// A list can be a block scalar.
// RUN: printf 'LowerCaseFreeFunctions: >-\n  clear\n' > %t/block.yaml
// RUN: clang-tidy --checks='-*,malterlib-function-naming' --config="{CheckOptions: {malterlib.OptionsFile: '%t/block.yaml'}}" %t/root/Module/main.cpp -- 2>&1 | FileCheck --check-prefix=CHECK-BLOCK --allow-empty %s
// CHECK-BLOCK-NOT: 'clear'

// The error names the file as it is, also with a `%`.
// RUN: not clang-tidy --checks='-*,malterlib-function-naming' --config="{CheckOptions: {malterlib.OptionsFile: '%t/100%% done.yaml'}}" %t/root/Module/main.cpp -- 2>&1 | FileCheck --check-prefix=CHECK-PERCENT %s
// CHECK-PERCENT: error: can't read the Malterlib options file '{{.*}}100% done.yaml': {{.*}} [clang-tidy-config]

// --dump-config gives the options file, and each list as it is, which takes
// the place of the one built in and of the options file, so the
// configuration it gives finds the same.
// RUN: clang-tidy --checks='-*,malterlib-function-naming' --config="{CheckOptions: {malterlib.OptionsFile: '%t/root/Core/ClangTidyOptions.yaml', MoveFunctions: fg_Take, malterlib-function-naming.LowerCaseFreeFunctions: '-*;clear'}}" --dump-config %t/root/Module/main.cpp -- > %t/dump.yaml
// RUN: FileCheck --check-prefix=CHECK-DUMP --input-file=%t/dump.yaml %s
// CHECK-DUMP: MoveFunctions: '-*;move;fg_Move;fg_Take'
// CHECK-DUMP: malterlib-function-naming.LowerCaseFreeFunctions: '-*;clear'
// CHECK-DUMP: malterlib.OptionsFile: '{{.*}}ClangTidyOptions.yaml'
// RUN: cd %t/elsewhere && clang-tidy --config-file=%t/dump.yaml %t/root/Module/main.cpp -- 2>&1 | FileCheck --check-prefix=CHECK-REREAD %s
// CHECK-REREAD: main.cpp:1:6: warning: global function 'swap' should have the 'fg_' prefix [malterlib-function-naming]
// CHECK-REREAD-NOT: 'clear'
// CHECK-REREAD-NOT: error:
// A configuration file found by a relative path is the directory of its
// paths also when the name of a directory on the way starts with `<`, which a
// configuration given on the command line, `<command-line-config>`, is named.
// RUN: mkdir -p '%t/angle/<module>'
// RUN: printf "LowerCaseFreeFunctions: clear\n" > '%t/angle/<module>/options.yaml'
// RUN: printf "Checks: '-*,malterlib-function-naming'\nCheckOptions:\n  malterlib.OptionsFile: options.yaml\n" > '%t/angle/<module>/tidy.yaml'
// RUN: printf "void clear();\n" > %t/angle/main.cpp
// RUN: cd %t/angle && clang-tidy '--config-file=<module>/tidy.yaml' main.cpp -- 2>&1 | FileCheck --check-prefix=CHECK-ANGLE --allow-empty %s
// RUN: cd %t/angle && %clang -fsyntax-only '-fplugin-arg-malterlib-config-file=<module>/tidy.yaml' main.cpp 2>&1 | FileCheck --check-prefix=CHECK-ANGLE --allow-empty %s
// CHECK-ANGLE-NOT: error:
// CHECK-ANGLE-NOT: warning:
// Not when the options file can't be read, which is reported, rather than
// the lists without it.
// RUN: not clang-tidy --checks='-*,malterlib-function-naming' --config="{CheckOptions: {malterlib.OptionsFile: '%t/missing.yaml'}}" --dump-config %t/root/Module/main.cpp -- 2>&1 | FileCheck --check-prefix=CHECK-DUMP-MISSING %s
// CHECK-DUMP-MISSING: error: can't read the Malterlib options file '{{.*}}missing.yaml': {{.*}} [clang-tidy-config]
// CHECK-DUMP-MISSING-NOT: CheckOptions

// --verify-config knows the options the checks read, also those of all
// checks, but not others.
// RUN: clang-tidy --verify-config --config="{Checks: '-*,malterlib-*', CheckOptions: {malterlib.OptionsFile: '%t/root/Core/ClangTidyOptions.yaml', MoveFunctions: fg_Take, malterlib-no-zero-bit-scan.Functions: fg_Scan}}" 2>&1 | FileCheck --check-prefix=CHECK-VERIFY %s
// CHECK-VERIFY: No config errors detected.
// RUN: not clang-tidy --verify-config --config="{Checks: '-*,malterlib-*', CheckOptions: {malterlib.OptionsFile: '%t/root/Core/ClangTidyOptions.yaml', MoveFunction: fg_Take}}" 2>&1 | FileCheck --check-prefix=CHECK-UNKNOWN %s
// CHECK-UNKNOWN: warning: unknown check option 'MoveFunction'; did you mean 'MoveFunctions' [-verify-config]
// Also the options of the options file, which it can read.
// RUN: printf 'MoveFunction: fg_Take\nmalterlib-async-destroy.MoveFunctions: fg_Take\nmalterlib-no-zero-bit-scan.Functions: fg_Scan\n' > %t/unknown.yaml
// RUN: not clang-tidy --verify-config --config="{Checks: '-*,malterlib-*', CheckOptions: {malterlib.OptionsFile: '%t/unknown.yaml'}}" 2>&1 | FileCheck --check-prefix=CHECK-FILE-UNKNOWN --implicit-check-not=no-zero-bit-scan %s
// CHECK-FILE-UNKNOWN-DAG: unknown.yaml: warning: unknown check option 'MoveFunction'; did you mean 'MoveFunctions' [-verify-config]
// CHECK-FILE-UNKNOWN-DAG: unknown.yaml: warning: unknown check option 'malterlib-async-destroy.MoveFunctions' [-verify-config]
// RUN: not clang-tidy --verify-config --config="{Checks: '-*,malterlib-*', CheckOptions: {malterlib.OptionsFile: '%t/missing.yaml'}}" 2>&1 | FileCheck --check-prefix=CHECK-FILE-MISSING %s
// And a configuration that enables a Malterlib check without it.
// RUN: not clang-tidy --verify-config --config="{Checks: '-*,malterlib-function-naming'}" 2>&1 | FileCheck --check-prefix=CHECK-VERIFY-UNSET %s
// CHECK-VERIFY-UNSET: error: the Malterlib checks need the option 'malterlib.OptionsFile', the Malterlib options file [-verify-config]
// CHECK-FILE-MISSING: error: can't read the Malterlib options file '{{.*}}missing.yaml': {{.*}} [-verify-config]
// RUN: clang-tidy --verify-config --config="{Checks: '-*,malterlib-*', CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" 2>&1 | FileCheck --check-prefix=CHECK-VERIFY %s

// --verify-config reports the errors of an options file once, also one that a
// configuration and one it inherits both name.
// RUN: rm -rf %t.shared && mkdir -p %t.shared/child
// RUN: printf 'MoveFunctions: [\n' > %t.shared/options.yaml
// RUN: printf "Checks: '-*,malterlib-function-naming'\nCheckOptions:\n  malterlib.OptionsFile: '%t.shared/options.yaml'\n" > %t.shared/.clang-tidy
// RUN: printf "InheritParentConfig: true\nCheckOptions:\n  malterlib.OptionsFile: '%t.shared/options.yaml'\n" > %t.shared/child/.clang-tidy
// RUN: echo 'void fg_Test();' > %t.shared/child/main.cpp
// RUN: not clang-tidy --verify-config %t.shared/child/main.cpp -- -std=c++20 2>&1 | FileCheck --check-prefix=CHECK-SHARED %s
// CHECK-SHARED: error:
// CHECK-SHARED-NOT: error:
// Also where they name it by other paths, `options.yaml` and `../options.yaml`.
// RUN: printf "InheritParentConfig: true\nCheckOptions:\n  malterlib.OptionsFile: '%t.shared/child/../options.yaml'\n" > %t.shared/child/.clang-tidy
// RUN: not clang-tidy --verify-config %t.shared/child/main.cpp -- -std=c++20 2>&1 | FileCheck --check-prefix=CHECK-SHARED %s
