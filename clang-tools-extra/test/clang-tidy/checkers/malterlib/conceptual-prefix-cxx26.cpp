// RUN: %check_clang_tidy -std=c++26-or-later %s malterlib-conceptual-prefix %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}"

// A pointer to a type of a pack named as functions, `tfp_FCallbacks...[0]`, is
// a function pointer.
template <typename ...tfp_FCallbacks>
void fg_Run(tfp_FCallbacks...[0] *_fCallback)
{
}
using FCallback = void ();
void fg_Use()
{
	fg_Run<FCallback>(nullptr);
}
template <typename ...tfp_CTypes>
void fg_RunObject(tfp_CTypes...[0] *_Value)
{
}
void fg_UseObject()
{
	fg_RunObject<int>(nullptr);
}

// A parameter whose type is an element of a pack named as function types is a function object.
namespace NRound117PackIndexing {
struct CAction { void operator()(); };
template <typename ...tfp_FCallbacks>
void fg_Run(tfp_FCallbacks...[0] _Callback) {}
// CHECK-MESSAGES: :[[@LINE-1]]:34: warning: '_Callback' is a function object and should have the 'f' concept prefix after its storage prefix [malterlib-conceptual-prefix]
void fg_Test() { fg_Run<CAction>(CAction{}); }
}
