//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_WORKBUDGET_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_WORKBUDGET_H

#include "llvm/ADT/StringRef.h"
#include <cstdint>
#include <type_traits>

namespace clang::tidy::malterlib {

/// The work a budgeted check may do for one root (CWorkScope): each walk that
/// recurses, each loop that follows what a definition leads to and each
/// element a growing set gains spends a step (spendWork). The limit of a check
/// is many times what its largest root of real code takes. A root that spends
/// it all is given up: its walks answer what they answer when they can't tell,
/// and its match reports nothing more (MalterlibCheck::diag), and once the
/// match is done nothing kept of the AST is kept (astIdentity changes), so the
/// next match analyzes from scratch.
struct CWorkState {
  uint64_t Left = 0;
  bool Active = false;
  bool Spent = false;
};

extern thread_local CWorkState t_WorkState;

/// Gives up the root being checked, the first time its budget is spent;
/// false.
bool workExhausted();

/// Spends a step of the work of the root being checked; false once all of it
/// is spent, when the walk returns at once. Always true outside a root.
inline bool spendWork() {
  CWorkState &State = t_WorkState;
  if (!State.Active)
    return true;
  if (State.Left != 0) {
    --State.Left;
    return true;
  }
  return workExhausted();
}

/// What a walk answers once its root is given up (spendWork): the value of
/// \p t_CType that value-initialization gives, which its callers take as what
/// can't be told; nothing of void.
template <typename t_CType> t_CType cantTell() {
  if constexpr (!std::is_void_v<t_CType>)
    return t_CType();
}

/// Stops the work of the root being checked from running out while a report
/// it began is made, so the report says what it would have said in full;
/// whether it was counted before, which resumeWork is given.
inline bool pauseWork() {
  const bool Was = t_WorkState.Active;
  t_WorkState.Active = false;
  return Was;
}

/// Counts the work of the root again as before pauseWork, which gave \p Was.
inline void resumeWork(bool Was) { t_WorkState.Active = Was; }

/// Spends the rest of the work of the root being checked, where its walks
/// would need more stack than the thread has (withStack): its depth is
/// bounded by the stack, not only by its steps, so a walk ends the same with
/// or without the stack segments. Real code doesn't go that deep
/// (lbt/work-limit.sh).
inline void workOutOfStack() {
  if (t_WorkState.Active)
    t_WorkState.Left = 0;
}

/// Whether a root of the match being checked is given up (spendWork).
inline bool workSpent() { return t_WorkState.Spent; }

/// What is added to the identity of each AST (astIdentity), which changes
/// each time a match whose root was given up ends on this thread.
uint64_t workGeneration();

/// A root of the check \p Check while it is alive, with \p Limit steps: of a
/// match, or nested in one, like a parameter of a function, which has its own
/// steps but shares whether the match is given up. With \p Report, its steps
/// and whether it ran out are counted by check, and the counts are printed
/// to the standard error when the process ends.
class CWorkScope {
public:
  CWorkScope(llvm::StringRef Check, uint64_t Limit, bool Report);
  CWorkScope(const CWorkScope &) = delete;
  CWorkScope &operator=(const CWorkScope &) = delete;
  ~CWorkScope();

private:
  llvm::StringRef Check;
  uint64_t Limit;
  bool Report;
  bool SpentBefore;
  CWorkState Previous;
};

} // namespace clang::tidy::malterlib

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_WORKBUDGET_H
