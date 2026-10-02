// RUN: %check_clang_tidy -std=c++20-or-later %s malterlib-parameter-naming,malterlib-conceptual-prefix %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}"

// The parameters of function types written the same, which are one type, are
// each checked by what the use of the alias makes of them.

struct CTraits { using CBool = bool; };
template<typename t_FSignature> struct TCFunction {};
template<typename t_CFirst, typename t_CSecond> struct TCPair {};

template<typename t_CTraits>
using TCPairOfCallbacks = TCPair<
  TCFunction<void (typename t_CTraits::CBool o_Ready)>,
// CHECK-MESSAGES: :[[@LINE-1]]:46: warning: parameter 'o_Ready' has the 'o_' prefix of an output
  TCFunction<void (typename t_CTraits::CBool o_Done)>>;
// CHECK-MESSAGES: :[[@LINE-1]]:46: warning: parameter 'o_Done' has the 'o_' prefix of an output

TCPairOfCallbacks<CTraits> g_Callbacks;
