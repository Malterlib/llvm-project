//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_STACKSEGMENTS_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_STACKSEGMENTS_H

#include "WorkBudget.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include <cstdint>
#include <functional>
#include <optional>
#include <type_traits>
#include <utility>

namespace clang::tidy::malterlib {

/// The stack the walks of the checks recurse on, which grows by segments of
/// the thread's own: what the code they look at nests, an expression, an
/// alias of an alias, a call of a call, is as deep as it is written, and a
/// walk that recurses once for each level would run out of the thread's
/// stack. A walk calls through withStack where it recurses (TCRecursive),
/// which goes on on a new segment when the one it is on has less room left
/// than a walk takes between two such calls. The segments are of the same
/// thread, so what the walks keep of it (thread_local) is the same.

/// Whether the stack segment the thread runs on has less room left than
/// StackReserve.
bool stackNearlyExhausted();

/// Whether the stack segment the thread runs on has less room left than
/// StackReserve at \p Here, as stackNearlyExhausted asked there would tell,
/// and \p Here counted as asked (exchangeDeepestStack).
bool stackNearlyExhaustedAt(uintptr_t Here);

/// Where the stack of the thread is here.
uintptr_t stackHere();

/// The deepest of where stackNearlyExhaustedAt was asked since the last
/// call, which starts again from \p Deepest: how far below where it starts
/// a walk asks, which it asks again as far below where it is asked again.
uintptr_t exchangeDeepestStack(uintptr_t Deepest);

/// Runs \p fRun on a new stack segment of the thread, and returns when it
/// does.
void runOnNewStackSegment(llvm::function_ref<void()> fRun);

/// What \p fCall gives, called here, or on a new stack segment when this one
/// is nearly exhausted (stackNearlyExhausted).
template <typename tf_FCall> decltype(auto) withStack(tf_FCall &&fCall) {
  using CResult = decltype(fCall());
  if (!stackNearlyExhausted())
    return std::forward<tf_FCall>(fCall)();
  // The root of a budgeted check gives up here (workOutOfStack): the call
  // still runs, on a new segment, and its walk returns at its first step.
  workOutOfStack();
  if constexpr (std::is_void_v<CResult>) {
    runOnNewStackSegment([&] { std::forward<tf_FCall>(fCall)(); });
  } else if constexpr (std::is_reference_v<CResult>) {
    std::remove_reference_t<CResult> *Result = nullptr;
    runOnNewStackSegment([&] { Result = &std::forward<tf_FCall>(fCall)(); });
    return static_cast<CResult>(*Result);
  } else {
    std::optional<CResult> Result;
    runOnNewStackSegment([&] { Result.emplace(std::forward<tf_FCall>(fCall)()); });
    return CResult(std::move(*Result));
  }
}

template <typename t_FSignature> class TCRecursive;

/// A function a walk recurses through, like `std::function`, each call of
/// which is on a stack with room for it (withStack).
template <typename t_CResult, typename... tp_CArguments>
class TCRecursive<t_CResult(tp_CArguments...)> {
public:
  TCRecursive() = default;
  template <typename tf_FFunction,
            typename = std::enable_if_t<!std::is_same_v<
                std::decay_t<tf_FFunction>, TCRecursive>>>
  TCRecursive(tf_FFunction &&fFunction)
      : fFunction(std::forward<tf_FFunction>(fFunction)) {}
  template <typename tf_FFunction,
            typename = std::enable_if_t<!std::is_same_v<
                std::decay_t<tf_FFunction>, TCRecursive>>>
  TCRecursive &operator=(tf_FFunction &&fNew) {
    fFunction = std::forward<tf_FFunction>(fNew);
    return *this;
  }

  t_CResult operator()(tp_CArguments... Arguments) const {
    return withStack([&]() -> t_CResult {
      return fFunction(std::forward<tp_CArguments>(Arguments)...);
    });
  }

  explicit operator bool() const { return static_cast<bool>(fFunction); }

private:
  std::function<t_CResult(tp_CArguments...)> fFunction;
};

} // namespace clang::tidy::malterlib

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_STACKSEGMENTS_H
