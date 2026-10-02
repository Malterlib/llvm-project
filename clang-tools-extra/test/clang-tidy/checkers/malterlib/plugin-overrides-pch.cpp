// The overrides a virtual call can run are the same with a precompiled header
// built with the plugin, which stores those it declares, as without one: of
// the header, of the source, of a function the source defines outside of its
// class, and of what the source instantiates of a template of the header.
// With MALTERLIB_TIDY_CHECK_OVERRIDES, the compile also walks the whole
// translation unit and stops at the first override that the two don't share.
// REQUIRES: malterlib-plugin
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -std=c++20 -x c++-header -fplugin-arg-malterlib-checks='-*,malterlib-coroutine-actor-reference' -I %S/Inputs %S/Inputs/plugin-overrides-pch.h -o %t.pch 2>&1 | count 0
// RUN: env MALTERLIB_TIDY_CHECK_OVERRIDES=1 %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -std=c++20 -fsyntax-only -fplugin-arg-malterlib-checks='-*,malterlib-coroutine-actor-reference' -I %S/Inputs -include-pch %t.pch %s 2>&1 | FileCheck --check-prefixes=CHECK,CHECK-INDEX --implicit-check-not=warning: --implicit-check-not=error: %s
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -std=c++20 -fsyntax-only -fplugin-arg-malterlib-checks='-*,malterlib-coroutine-actor-reference' -I %S/Inputs -include plugin-overrides-pch.h %s 2>&1 | FileCheck --implicit-check-not=warning: --implicit-check-not=error: --implicit-check-not=overrides: %s
// A precompiled header built without the plugin has no index, and the compile
// walks the whole translation unit.
// RUN: %clang -std=c++20 -x c++-header -I %S/Inputs %S/Inputs/plugin-overrides-pch.h -o %t.plain.pch 2>&1 | count 0
// RUN: env MALTERLIB_TIDY_CHECK_OVERRIDES=1 %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -std=c++20 -fsyntax-only -fplugin-arg-malterlib-checks='-*,malterlib-coroutine-actor-reference' -I %S/Inputs -include-pch %t.plain.pch %s 2>&1 | FileCheck --implicit-check-not=warning: --implicit-check-not=error: --implicit-check-not=overrides: %s
// With the index, the compile reads few of the lexical declaration contexts
// of the header, not all of them, as the walk of the whole translation unit
// does.
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -std=c++20 -fsyntax-only -fplugin-arg-malterlib-checks='-*,malterlib-coroutine-actor-reference' -I %S/Inputs -include-pch %t.pch %s -Xclang -print-stats 2>&1 | FileCheck --check-prefix=STATS %s
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -std=c++20 -fsyntax-only -fplugin-arg-malterlib-checks='-*,malterlib-coroutine-actor-reference' -I %S/Inputs -include-pch %t.plain.pch %s -Xclang -print-stats 2>&1 | FileCheck --check-prefix=STATS-WALK %s

// CHECK-INDEX: overrides: {{[1-9][0-9]*}} stored by the precompiled header, {{[1-9][0-9]*}} declared in the compile, the same as the walk of the translation unit
// STATS: {{^ *[0-9]+/[0-9]+}} lexical declcontexts read ({{[1-3]?[0-9]\.}}
// STATS-WALK: {{^ *[0-9]+/[0-9]+}} lexical declcontexts read ({{9[0-9]\.|1[0-9][0-9]\.}}

struct CHeaderApp : virtual CHeaderBase {
  TCFuture<int> f_Run() {
    auto Guard = co_await f_CheckDestroyedOnResume();
    int *pValue = mp_Items.f_FindEqual(1);
    co_await fg_Suspend();
    f_Tick();
    co_return *pValue;
    // CHECK: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
  }
};

struct CSourceSide : virtual CSourceBase {
  void f_Tick() override { (void)fp_DestroyInternal(); }
};
struct CSourceApp : virtual CSourceBase {
  TCFuture<int> f_Run() {
    auto Guard = co_await f_CheckDestroyedOnResume();
    int *pValue = mp_Items.f_FindEqual(1);
    co_await fg_Suspend();
    f_Tick();
    co_return *pValue;
    // CHECK: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
  }
};

struct CTemplateApp : virtual CTemplateBase {
  TCFuture<int> f_Run() {
    auto Guard = co_await f_CheckDestroyedOnResume();
    int *pValue = mp_Items.f_FindEqual(1);
    co_await fg_Suspend();
    f_Tick();
    co_return *pValue;
    // CHECK: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
  }
};
struct CTemplateCombined : CTemplateApp, TCTemplateSide<int> {};

void COutOfLineSide::f_Tick() { (void)fp_DestroyInternal(); }
struct COutOfLineApp : virtual COutOfLineBase {
  TCFuture<int> f_Run() {
    auto Guard = co_await f_CheckDestroyedOnResume();
    int *pValue = mp_Items.f_FindEqual(1);
    co_await fg_Suspend();
    f_Tick();
    co_return *pValue;
    // CHECK: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
  }
};

struct CPlainApp : virtual CPlainBase {
  TCFuture<int> f_Run() {
    auto Guard = co_await f_CheckDestroyedOnResume();
    int *pValue = mp_Items.f_FindEqual(1);
    co_await fg_Suspend();
    f_Tick();
    co_return *pValue;
  }
};
