// RUN: %check_clang_tidy -std=c++20-or-later %s malterlib-struct-not-class %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml', malterlib.FixIts: true}}" -- --target=x86_64-pc-windows-msvc

// The explicit instantiations of a class template with what the Windows
// targets write between the tag and the name, `__declspec(dllimport)`, also
// from a macro, are fixed with it.
#define DMibExport __declspec(dllexport)
template <typename t_CType>
class TCBuffer
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'TCBuffer' should be declared with 'struct', like Malterlib types are [malterlib-struct-not-class]
// CHECK-FIXES: {{^}}struct TCBuffer{{$}}
{
public:
	t_CType *m_pData;
};
extern template class __declspec(dllimport) TCBuffer<int>;
// CHECK-FIXES: {{^}}extern template struct __declspec(dllimport) TCBuffer<int>;
template class DMibExport TCBuffer<long>;
// CHECK-FIXES: {{^}}template struct DMibExport TCBuffer<long>;
