// RUN: %python %S/Inputs/many-shared-function-types.py 40 > %t.cpp
// RUN: clang-tidy --checks='-*,malterlib-coawait-precedence' --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" %t.cpp -- -std=c++20 2>&1 | FileCheck --allow-empty %s
// RUN: %python %S/Inputs/many-shared-function-types.py 40 parents > %t-parents.cpp
// RUN: clang-tidy --checks='-*,malterlib-coawait-precedence,malterlib-coroutine-reference-parameter' --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" %t-parents.cpp -- -std=c++20 -I %S/Inputs 2>&1 | FileCheck --allow-empty %s
// RUN: %python %S/Inputs/many-shared-function-types.py 40 aliases > %t-aliases.cpp
// RUN: clang-tidy --checks='-*,malterlib-conceptual-prefix' --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" %t-aliases.cpp -- -std=c++20 2>&1 | FileCheck --allow-empty %s
// RUN: %python %S/Inputs/many-shared-function-types.py 10 arguments > %t-arguments.cpp
// RUN: clang-tidy --checks='-*,malterlib-conceptual-prefix' --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" %t-arguments.cpp -- -std=c++20 2>&1 | FileCheck --allow-empty %s
// RUN: %python %S/Inputs/many-shared-function-types.py 40 coroutines > %t-coroutines.cpp
// RUN: clang-tidy --checks='-*,malterlib-async-destroy,malterlib-continue-running-on-actor,malterlib-coroutine-actor-reference,malterlib-coroutine-lambda-call,malterlib-coroutine-reference-capture,malterlib-coroutine-reference-parameter,malterlib-coroutine-unsafe-this' --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" %t-coroutines.cpp -- -std=c++20 -I %S/Inputs 2>&1 | FileCheck --check-prefix=COROUTINES %s
// RUN: %python %S/Inputs/many-shared-function-types.py 40 defaults > %t-defaults.cpp
// RUN: clang-tidy --checks='-*,malterlib-parameter-naming' --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" %t-defaults.cpp -- -std=c++20 2>&1 | FileCheck --check-prefix=DEFAULTS %s
// RUN: %python %S/Inputs/many-shared-function-types.py 12 callbacks > %t-callbacks.cpp
// RUN: clang-tidy --checks='-*,malterlib-conceptual-prefix' --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" %t-callbacks.cpp -- -std=c++20 2>&1 | FileCheck --allow-empty %s
// RUN: clang-tidy --checks='-*,readability-redundant-access-specifiers' --config='{}' %t.cpp -- -std=c++20 2>&1 | FileCheck --allow-empty %s

// The matchers go through a type that names another twice, like the
// parameters and result of `F39 (*)(F39, F39)`, which the field of an
// instantiation has where it is written as the template parameter, once, not
// once for each path to it, which is exponential in the depth of such types.
// So do the parent map, which the matcher of a modulo builds, the index of the
// instantiations of templates, which a coroutine template's instantiation
// makes, the collection of the uses of alias templates of the naming checks,
// and a matcher of children, which goes no deeper than it matches. And the
// naming checks compare each pair of types a partial specialization matches
// once, also a type list of a type list that names the one before ten times.
// So do the coroutine checks, which collect the locals, captures and awaits
// of coroutines instantiated with such a type, and the naming checks, which
// collect the function types of a default template argument, and make the
// parameters of one with the arguments of each specialization.

// CHECK-NOT: warning:

// The checks still warn where they did.
// COROUTINES: warning: TCFuture coroutine 'fg_Read' uses '_State' after a suspension{{.*}} [malterlib-coroutine-reference-parameter]
// COROUTINES: warning: a TCFuture coroutine lambda uses 'Local', which it captures by reference, after a suspension{{.*}} [malterlib-coroutine-reference-capture]
// COROUTINES: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' moved the coroutine to another actor{{.*}} [malterlib-continue-running-on-actor]
// COROUTINES: warning: a TCFuture coroutine lambda captures 'Local' by reference but is not called directly where 'Local' is{{.*}} [malterlib-coroutine-reference-capture]
// COROUTINES-NOT: warning:
// DEFAULTS: warning: parameter should be named{{.*}} [malterlib-parameter-naming]
// DEFAULTS: warning: parameter 'o_Value' has the 'o_' prefix of an output{{.*}} [malterlib-parameter-naming]
// DEFAULTS-NOT: warning:
