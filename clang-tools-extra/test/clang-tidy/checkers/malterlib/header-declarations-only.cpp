// RUN: %check_clang_tidy -std=c++20-or-later %s malterlib-header-declarations-only %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" --header-filter='header-declarations-only' -- -I %S/Inputs
// A definition whose signature is in another file than its body is reported
// where the body is.
// With other extensions of declaration files, the advice names only the files
// that aren't.
// RUN: rm -rf %t.extensions && mkdir -p %t.extensions
// RUN: printf 'inline int fg_Extensions() { return 1; }\n' > %t.extensions/inline.hpp
// RUN: printf '#include "inline.hpp"\n' > %t.extensions/main.cpp
// RUN: clang-tidy --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml', malterlib-header-declarations-only.DeclarationFileExtensions: 'h;hpp'}}" --checks='-*,malterlib-header-declarations-only' --header-filter='.*' %t.extensions/main.cpp -- -std=c++20 2>&1 | FileCheck --check-prefix=CHECK-EXTENSIONS-HPP %s
// CHECK-EXTENSIONS-HPP: inline.hpp:1:12: warning: 'fg_Extensions' is defined in a '.hpp' file, which holds declarations only; define it in a '.cpp' file [malterlib-header-declarations-only]
// RUN: clang-tidy --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml', malterlib-header-declarations-only.DeclarationFileExtensions: 'hpp;cpp'}}" --checks='-*,malterlib-header-declarations-only' --header-filter='.*' %t.extensions/main.cpp -- -std=c++20 2>&1 | FileCheck --check-prefix=CHECK-EXTENSIONS-ALL %s
// CHECK-EXTENSIONS-ALL: inline.hpp:1:12: warning: 'fg_Extensions' is defined in a '.hpp' file, which holds declarations only; define it in a file that holds definitions [malterlib-header-declarations-only]
// RUN: rm -rf %t.split && mkdir -p %t.split
// RUN: printf 'inline int fg_Split()\n' > %t.split/signature.hpp
// RUN: printf '#include "signature.hpp"\n{ return 1; }\n' > %t.split/body.h
// RUN: printf '#include "body.h"\n' > %t.split/main.cpp
// RUN: clang-tidy --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" --checks='-*,malterlib-header-declarations-only' --header-filter='\.h$' %t.split/main.cpp -- -std=c++20 2>&1 | FileCheck --check-prefix=CHECK-SPLIT %s
// Also what is in an expression that a '.h' is included in.
// RUN: rm -rf %t.expression && mkdir -p %t.expression
// RUN: printf '42\n' > %t.expression/fragment.h
// RUN: printf 'inline int fg_Expression()\n{\n  return 1 +\n#include "fragment.h"\n  ;\n}\n' > %t.expression/implementation.hpp
// RUN: printf '#include "implementation.hpp"\n' > %t.expression/main.cpp
// RUN: clang-tidy --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" --checks='-*,malterlib-header-declarations-only' --header-filter='.*' %t.expression/main.cpp -- -std=c++20 2>&1 | FileCheck --check-prefix=CHECK-EXPRESSION %s
// CHECK-EXPRESSION: fragment.h:1:1: warning: 'fg_Expression' is defined in a '.h' file, which holds declarations only; define it in a '.hpp' or '.cpp' file [malterlib-header-declarations-only]
// Also its operator only.
// RUN: rm -rf %t.operator && mkdir -p %t.operator
// RUN: printf '+\n' > %t.operator/fragment.h
// RUN: printf 'inline int fg_Operator()\n{\n  return 1\n#include "fragment.h"\n  2;\n}\n' > %t.operator/implementation.hpp
// RUN: printf '#include "implementation.hpp"\n' > %t.operator/main.cpp
// RUN: clang-tidy --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" --checks='-*,malterlib-header-declarations-only' --header-filter='.*' %t.operator/main.cpp -- -std=c++20 2>&1 | FileCheck --check-prefix=CHECK-OPERATOR %s
// CHECK-OPERATOR: fragment.h:1:1: warning: 'fg_Operator' is defined in a '.h' file, which holds declarations only; define it in a '.hpp' or '.cpp' file [malterlib-header-declarations-only]
// Also inside an initializer of a constructor.
// RUN: rm -rf %t.initializer && mkdir -p %t.initializer
// RUN: printf '42\n' > %t.initializer/fragment.h
// RUN: printf 'struct CObject\n{\n  int m_Value;\n  CObject() : m_Value(1 +\n#include "fragment.h"\n    + 3) {}\n};\n' > %t.initializer/object.hpp
// RUN: printf '#include "object.hpp"\n' > %t.initializer/main.cpp
// RUN: clang-tidy --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" --checks='-*,malterlib-header-declarations-only' --header-filter='.*' %t.initializer/main.cpp -- -std=c++20 2>&1 | FileCheck --check-prefix=CHECK-INITIALIZER %s
// CHECK-INITIALIZER: fragment.h:1:1: warning: 'CObject' is defined in a '.h' file, which holds declarations only; define it in a '.hpp' or '.cpp' file [malterlib-header-declarations-only]
// A body opened in a '.hpp' whose statements are in a '.h' it includes is
// reported where they are.
// RUN: rm -rf %t.opened && mkdir -p %t.opened
// RUN: printf 'inline int fg_Opened()\n{\n' > %t.opened/opening.hpp
// RUN: printf '#include "opening.hpp"\n  return 1;\n}\n' > %t.opened/body.h
// RUN: printf '#include "body.h"\n' > %t.opened/main.cpp
// RUN: clang-tidy --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" --checks='-*,malterlib-header-declarations-only' --header-filter='.*' %t.opened/main.cpp -- -std=c++20 2>&1 | FileCheck --check-prefix=CHECK-OPENED %s
// Also statements nested in the body.
// RUN: rm -rf %t.nested && mkdir -p %t.nested
// RUN: printf 'return 1;\n' > %t.nested/fragment.h
// RUN: printf 'int fg_Nested(bool _bValue)\n{\n\tif (_bValue)\n\t{\n#include "fragment.h"\n\t}\n\treturn 0;\n}\n' > %t.nested/main.cpp
// RUN: clang-tidy --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" --checks='-*,malterlib-header-declarations-only' --header-filter='.*' %t.nested/main.cpp -- -std=c++20 2>&1 | FileCheck --check-prefix=CHECK-NESTED %s
// CHECK-NESTED: fragment.h:1:1: warning: 'fg_Nested' is defined in a '.h' file, which holds declarations only; define it in a '.hpp' or '.cpp' file [malterlib-header-declarations-only]
// Not types that a '.h' included in a body defines, which aren't
// implementation.
// RUN: rm -rf %t.types && mkdir -p %t.types
// RUN: printf 'struct CLocal { unsigned m_Value; };\n' > %t.types/LocalTypes.h
// RUN: printf 'void fg_Types()\n{\n#include "LocalTypes.h"\n\tCLocal Value{1};\n}\n' > %t.types/main.cpp
// RUN: clang-tidy --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" --checks='-*,malterlib-header-declarations-only' --header-filter='.*' %t.types/main.cpp -- -std=c++20 2>&1 | FileCheck --check-prefix=CHECK-TYPES --allow-empty %s
// CHECK-TYPES-NOT: warning:
// Also the initializers of a constructor whose body a '.h' includes.
// RUN: rm -rf %t.initializers && mkdir -p %t.initializers
// RUN: printf 'struct CObject\n{\n\tint m_Value;\n\tCObject() : m_Value(42)\n#include "Object-body.hpp"\n};\n' > %t.initializers/Object.h
// RUN: printf '{}\n' > %t.initializers/Object-body.hpp
// RUN: printf '#include "Object.h"\n' > %t.initializers/main.cpp
// RUN: clang-tidy --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" --checks='-*,malterlib-header-declarations-only' --header-filter='.*' %t.initializers/main.cpp -- -std=c++20 2>&1 | FileCheck --check-prefix=CHECK-INITIALIZERS %s
// CHECK-INITIALIZERS: Object.h:4:2: warning: 'CObject' is defined in a '.h' file, which holds declarations only; define it in a '.hpp' or '.cpp' file [malterlib-header-declarations-only]
// Also a nested closing brace.
// RUN: rm -rf %t.brace && mkdir -p %t.brace
// RUN: printf '}\n' > %t.brace/fragment.h
// RUN: printf 'inline void fg_Brace(bool _bFlag)\n{\n\tif (_bFlag)\n\t{\n#include "fragment.h"\n}\n' > %t.brace/implementation.hpp
// RUN: printf '#include "implementation.hpp"\n' > %t.brace/main.cpp
// RUN: clang-tidy --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" --checks='-*,malterlib-header-declarations-only' --header-filter='.*' %t.brace/main.cpp -- -std=c++20 2>&1 | FileCheck --check-prefix=CHECK-BRACE %s
// CHECK-BRACE: fragment.h:1:1: warning: 'fg_Brace' is defined in a '.h' file, which holds declarations only; define it in a '.hpp' or '.cpp' file [malterlib-header-declarations-only]
// Also at the end of an expression, like a postfix operator.
// RUN: rm -rf %t.postfix && mkdir -p %t.postfix
// RUN: printf '++\n' > %t.postfix/postfix.h
// RUN: printf 'inline unsigned fg_Postfix(unsigned &_Bits)\n{\n  return _Bits\n#include "postfix.h"\n  ;\n}\n' > %t.postfix/implementation.hpp
// RUN: printf '#include "implementation.hpp"\n' > %t.postfix/main.cpp
// RUN: clang-tidy --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" --checks='-*,malterlib-header-declarations-only' --header-filter='.*' %t.postfix/main.cpp -- -std=c++20 2>&1 | FileCheck --check-prefix=CHECK-POSTFIX %s
// CHECK-POSTFIX: postfix.h:1:1: warning: 'fg_Postfix' is defined in a '.h' file, which holds declarations only; define it in a '.hpp' or '.cpp' file [malterlib-header-declarations-only]
// Not declarations of functions and variables defined elsewhere that a '.h'
// included in a body has.
// RUN: rm -rf %t.declarations && mkdir -p %t.declarations
// RUN: printf 'void fg_Work();\nextern unsigned g_Bits;\n' > %t.declarations/Declarations.h
// RUN: printf 'void fg_Run()\n{\n#include "Declarations.h"\n\tfg_Work();\n}\n' > %t.declarations/main.cpp
// RUN: clang-tidy --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" --checks='-*,malterlib-header-declarations-only' --header-filter='.*' %t.declarations/main.cpp -- -std=c++20 2>&1 | FileCheck --check-prefix=CHECK-DECLARATIONS --allow-empty %s
// CHECK-DECLARATIONS-NOT: warning:
// CHECK-OPENED: body.h:2:3: warning: 'fg_Opened' is defined in a '.h' file, which holds declarations only; define it in a '.hpp' or '.cpp' file [malterlib-header-declarations-only]
// CHECK-SPLIT: body.h:2:1: warning: 'fg_Split' is defined in a '.h' file

#include "header-declarations-only.h"
// CHECK-MESSAGES: header-declarations-only.h:3:13: warning: 'fg_Inline' is defined in a '.h' file, which holds declarations only; define it in a '.hpp' or '.cpp' file [malterlib-header-declarations-only]
// CHECK-MESSAGES: header-declarations-only.h:11:7: warning: 'f_Defined' is defined in a '.h' file, which holds declarations only; define it in a '.hpp' or '.cpp' file [malterlib-header-declarations-only]
// CHECK-MESSAGES: header-declarations-only.h:26:19: warning: 'fg_Template' is defined in a '.h' file, which holds declarations only; define it in a '.hpp' or '.cpp' file [malterlib-header-declarations-only]
// CHECK-MESSAGES: header-declarations-only.h:40:36: warning: 'fg_BodyArgument' is defined in a '.h' file, which holds declarations only; define it in a '.hpp' or '.cpp' file [malterlib-header-declarations-only]
// CHECK-MESSAGES: header-declarations-only.h:47:12: warning: 'fg_OpenedByMacro' is defined in a '.h' file, which holds declarations only; define it in a '.hpp' or '.cpp' file [malterlib-header-declarations-only]
// CHECK-MESSAGES: header-declarations-only.h:51:12: warning: 'fg_BracedByMacros' is defined in a '.h' file, which holds declarations only; define it in a '.hpp' or '.cpp' file [malterlib-header-declarations-only]
// CHECK-MESSAGES: header-declarations-only.h:55:34: warning: 'fg_CodeArgument' is defined in a '.h' file, which holds declarations only; define it in a '.hpp' or '.cpp' file [malterlib-header-declarations-only]
// CHECK-MESSAGES: header-declarations-only.h:57:12: warning: 'fg_TryArgument' is defined in a '.h' file, which holds declarations only; define it in a '.hpp' or '.cpp' file [malterlib-header-declarations-only]
// CHECK-MESSAGES: header-declarations-only.h:60:40: warning: 'fg_CoroutineArgument' is defined in a '.h' file, which holds declarations only; define it in a '.hpp' or '.cpp' file [malterlib-header-declarations-only]
// CHECK-MESSAGES: header-declarations-only.h:62:34: warning: 'fg_NestedArgument' is defined in a '.h' file, which holds declarations only; define it in a '.hpp' or '.cpp' file [malterlib-header-declarations-only]
// CHECK-MESSAGES: header-declarations-only.h:68:2: warning: 'CInitializedInHeader' is defined in a '.h' file, which holds declarations only; define it in a '.hpp' or '.cpp' file [malterlib-header-declarations-only]
// CHECK-MESSAGES: header-declarations-only.h:74:8: warning: 'CInitializedInArgument' is defined in a '.h' file, which holds declarations only; define it in a '.hpp' or '.cpp' file [malterlib-header-declarations-only]
// CHECK-MESSAGES: header-declarations-only.h:84:13: warning: 'CInitializedInValue' is defined in a '.h' file, which holds declarations only; define it in a '.hpp' or '.cpp' file [malterlib-header-declarations-only]
// CHECK-MESSAGES: header-declarations-only.h:91:12: warning: 'CInitializedInPart' is defined in a '.h' file, which holds declarations only; define it in a '.hpp' or '.cpp' file [malterlib-header-declarations-only]
#include "header-declarations-only.hpp"

void fg_InCpp()
{
}

// Nor types that a '.h' included in a body whose braces come from macros
// defines.
// RUN: rm -rf %t.macrobraces && mkdir -p %t.macrobraces
// RUN: printf 'struct CLocal { int m_Value; };\n' > %t.macrobraces/types-only.h
// RUN: printf '#define DOpenBody {\n#define DCloseBody }\ninline void fg_Run() DOpenBody\n#include "types-only.h"\nDCloseBody\n' > %t.macrobraces/implementation.hpp
// RUN: printf '#include "implementation.hpp"\n' > %t.macrobraces/main.cpp
// RUN: clang-tidy --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" --checks='-*,malterlib-header-declarations-only' --header-filter='.*' %t.macrobraces/main.cpp -- -std=c++20 2>&1 | FileCheck --check-prefix=CHECK-MACROBRACES --allow-empty %s
// CHECK-MACROBRACES-NOT: warning:
