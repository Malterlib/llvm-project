// REQUIRES: malterlib-plugin
// Symbolic links, which Git's ln for Windows copies, and where Windows
// resolves `..` before following a link.
// UNSUPPORTED: system-windows
// The rules of the repositories and the header filters of the plugin
// (plugin.cpp) with symbolic links.

// A header included through a symbolic link outside its repository is of the
// repository it is in.
// RUN: rm -rf %t.link && mkdir -p %t.link/repo %t.link/include
// RUN: echo 'void BadLinked();' > %t.link/repo/header.h
// RUN: ln -s %t.link/repo/header.h %t.link/include/header.h
// RUN: echo '#include <header.h>' > %t.link/repo/main.cpp
// RUN: printf '[{"Location": "%/t.link/repo", "Tidy": {"Enabled": true}}]' > %t.link/rules.json
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
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -fplugin-arg-malterlib-checks='-*,malterlib-function-naming' -fplugin-arg-malterlib-header-filter='^%/t.link/repo/' %t.link/repo/relative.cpp 2>&1 | FileCheck --check-prefix=CHECK-LINK-FILTER %s
// CHECK-LINK-FILTER: header.h:1:6: warning: global function 'BadLinked' should have the 'fg_' prefix [malterlib-function-naming]

// But one that a symbolic link in an enabled repository leads into one that
// isn't is of that one, and so is one a header filter excludes by either name.
// RUN: rm -rf %t.into && mkdir -p %t.into/repo/External %t.into/repo/Include
// RUN: echo 'void BadExternal();' > %t.into/repo/External/api.h
// RUN: ln -s ../External/api.h %t.into/repo/Include/api.h
// RUN: echo '#include "Include/api.h"' > %t.into/repo/main.cpp
// RUN: printf '[{"Location": "%/t.into/repo", "Tidy": {"Enabled": true}}, {"Location": "%/t.into/repo/External", "Tidy": {"Enabled": false}}]' > %t.into/rules.json
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -fplugin-arg-malterlib-checks='-*,malterlib-function-naming' -fplugin-arg-malterlib-repositories=%t.into/rules.json %t.into/repo/main.cpp 2>&1 | FileCheck --check-prefix=CHECK-LINK-INTO --allow-empty %s
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -fplugin-arg-malterlib-checks='-*,malterlib-function-naming' -fplugin-arg-malterlib-header-filter='.*' -fplugin-arg-malterlib-exclude-header-filter='Include/' %t.into/repo/main.cpp 2>&1 | FileCheck --check-prefix=CHECK-LINK-INTO --allow-empty %s
// CHECK-LINK-INTO-NOT: BadExternal

// A source reached through `..` after a symbolic link is where the link leads,
// `Unchecked/Link/../source.cpp` in `Checked`, not in the repository its name
// is in without the `..`.
// RUN: rm -rf %t.dotdot && mkdir -p %t.dotdot/Checked/Subdir %t.dotdot/Unchecked
// RUN: echo 'void BadDotDot();' > %t.dotdot/Checked/source.cpp
// RUN: ln -s %t.dotdot/Checked/Subdir %t.dotdot/Unchecked/Link
// RUN: printf '[{"Location": "%/t.dotdot/Checked", "Tidy": {"Enabled": true}}, {"Location": "%/t.dotdot/Unchecked", "Tidy": {"Enabled": false}}]' > %t.dotdot/rules.json
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -fplugin-arg-malterlib-checks='-*,malterlib-function-naming' -fplugin-arg-malterlib-repositories=%t.dotdot/rules.json %t.dotdot/Unchecked/Link/../source.cpp 2>&1 | FileCheck --check-prefix=CHECK-DOTDOT %s
// CHECK-DOTDOT: source.cpp:1:6: warning: global function 'BadDotDot' should have the 'fg_' prefix [malterlib-function-naming]

// A repository whose location is reached through a symbolic link is where
// the link leads, which the real name of a linked source is compared with.
// RUN: rm -rf %t.canon && mkdir -p %t.canon/Real
// RUN: echo 'void BadCanonical();' > %t.canon/Real/source.cpp
// RUN: ln -s %t.canon/Real %t.canon/Link
// RUN: ln -s %t.canon/Real/source.cpp %t.canon/source.cpp
// RUN: printf '[{"Location": "%/t.canon/Link", "Tidy": {"Enabled": true}}]' > %t.canon/rules.json
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -fplugin-arg-malterlib-checks='-*,malterlib-function-naming' -fplugin-arg-malterlib-repositories=%t.canon/rules.json %t.canon/source.cpp 2>&1 | FileCheck --check-prefix=CHECK-CANONICAL %s
// CHECK-CANONICAL: source.cpp:1:6: warning: global function 'BadCanonical' should have the 'fg_' prefix [malterlib-function-naming]
// Also a location with a `.` in it.
// RUN: printf '[{"Location": "%/t.canon/Link/.", "Tidy": {"Enabled": true}}]' > %t.canon/dotted.json
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -fplugin-arg-malterlib-checks='-*,malterlib-function-naming' -fplugin-arg-malterlib-repositories=%t.canon/dotted.json %t.canon/Real/source.cpp 2>&1 | FileCheck --check-prefix=CHECK-CANONICAL %s
// A `..` after a link is the parent of where the link leads, not of the
// link.
// RUN: rm -rf %t.updir && mkdir -p %t.updir/A %t.updir/B/Sub
// RUN: ln -s %t.updir/B/Sub %t.updir/A/Link
// RUN: echo 'void BadBeside();' > %t.updir/A/source.cpp
// RUN: echo 'void BadParent();' > %t.updir/B/source.cpp
// RUN: printf '[{"Location": "%/t.updir/A/Link/..", "Tidy": {"Enabled": true}}]' > %t.updir/rules.json
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -fplugin-arg-malterlib-checks='-*,malterlib-function-naming' -fplugin-arg-malterlib-repositories=%t.updir/rules.json %t.updir/B/source.cpp 2>&1 | FileCheck --check-prefix=CHECK-UPDIR %s
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -fplugin-arg-malterlib-checks='-*,malterlib-function-naming' -fplugin-arg-malterlib-repositories=%t.updir/rules.json %t.updir/A/source.cpp 2>&1 | FileCheck --check-prefix=CHECK-NOT-UPDIR --allow-empty %s
// CHECK-UPDIR: source.cpp:1:6: warning: global function 'BadParent' should have the 'fg_' prefix [malterlib-function-naming]
// CHECK-NOT-UPDIR-NOT: BadBeside
// Round 200 (llvm-naming-struct-2): an overlay's directory at a linked
// repository location, which is its own real name in the overlay, doesn't
// hide where the link leads: the location's real name is asked of each file
// system the compilation's is made of. A linked repository that is disabled
// excludes the source where the link leads, in an enabled parent, with the
// overlay as without it, and one that is enabled checks it.
// RUN: rm -rf %t.vfs && mkdir -p %t.vfs/Real
// RUN: ln -s %t.vfs/Real %t.vfs/Link
// RUN: echo 'void BadOverlaid();' > %t.vfs/Real/source.cpp
// RUN: printf '{"version": 0, "roots": [{"name": "%/t.vfs/Link", "type": "directory", "contents": []}]}' > %t.vfs/overlay.yaml
// RUN: printf '[{"Location": "%/t.vfs", "Tidy": {"Enabled": true}}, {"Location": "%/t.vfs/Link", "Tidy": {"Enabled": false}}]' > %t.vfs/disabled.json
// RUN: printf '[{"Location": "%/t.vfs/Link", "Tidy": {"Enabled": true}}]' > %t.vfs/enabled.json
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -Werror -fplugin-arg-malterlib-checks='-*,malterlib-function-naming' -fplugin-arg-malterlib-repositories=%t.vfs/disabled.json %t.vfs/Real/source.cpp 2>&1 | FileCheck --check-prefix=CHECK-NOT-OVERLAID --allow-empty %s
// RUN: %clang -ivfsoverlay %t.vfs/overlay.yaml -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -Werror -fplugin-arg-malterlib-checks='-*,malterlib-function-naming' -fplugin-arg-malterlib-repositories=%t.vfs/disabled.json %t.vfs/Real/source.cpp 2>&1 | FileCheck --check-prefix=CHECK-NOT-OVERLAID --allow-empty %s
// RUN: %clang -ivfsoverlay %t.vfs/overlay.yaml -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -fplugin-arg-malterlib-checks='-*,malterlib-function-naming' -fplugin-arg-malterlib-repositories=%t.vfs/enabled.json %t.vfs/Real/source.cpp 2>&1 | FileCheck --check-prefix=CHECK-OVERLAID %s
// CHECK-NOT-OVERLAID-NOT: BadOverlaid
// CHECK-OVERLAID: source.cpp:1:6: warning: global function 'BadOverlaid' should have the 'fg_' prefix [malterlib-function-naming]
