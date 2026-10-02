// RUN: sed 's/return m_pPointTo;/return \&m_Value;/' %s > %t.value.cpp
// RUN: clang-tidy --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" --checks='-*,malterlib-no-zero-bit-scan' %s %t.value.cpp -- -std=c++20 2>&1 | FileCheck %s

// What is decided of a declaration is of the AST it is in: one checked after
// it can have its declarations at the same addresses. Here the handle of the
// second file, a copy of this one, holds what it points to, so changing what
// a copy points to doesn't change it.
// CHECK: no-zero-bit-scan-ast-reuse.cpp:[[@LINE+21]]:{{[0-9]+}}: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first
// CHECK-NOT: value.cpp{{.*}}warning:
using umint = unsigned long;
int fg_GetLowestBitSetNoZero(umint);

namespace {
struct CInfo { umint m_Bits; };

template<unsigned t_n> struct TCHandle {
    CInfo m_Value;
    CInfo *m_pPointTo;
    CInfo *operator->() { return m_pPointTo; }
};

template<unsigned t_n>
int fg_Scan(TCHandle<t_n> &_Handle) {
    if (!_Handle->m_Bits)
        return -1;

    TCHandle<t_n> Copy = _Handle;
    Copy->m_Bits = 0;
    int Result = fg_GetLowestBitSetNoZero(_Handle->m_Bits);

    if constexpr (t_n > 0) {
        CInfo Value{1};
        TCHandle<t_n - 1> Other{Value, &Value};
        fg_Scan(Other);
    }
    return Result;
}

void fg_Instantiate() {
    CInfo Value{1};
    TCHandle<255> Handle{Value, &Value};
    fg_Scan(Handle);
}
}
