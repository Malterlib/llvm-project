//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "WorkBudget.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <mutex>

namespace clang::tidy::malterlib {

thread_local CWorkState t_WorkState;

namespace {
struct CWorkCounts {
  uint64_t Roots = 0;
  uint64_t Exhausted = 0;
  uint64_t Largest = 0;
  uint64_t Limit = 0;
};

// The counts of all threads by check, made by the first root that reports and
// printed when the process ends.
class CWorkReport {
public:
  void add(llvm::StringRef Check, uint64_t Steps, bool Exhausted,
           uint64_t Limit) {
    std::lock_guard<std::mutex> Lock(Mutex);
    CWorkCounts &Counts = ByCheck[Check];
    ++Counts.Roots;
    Counts.Exhausted += Exhausted ? 1 : 0;
    Counts.Largest = std::max(Counts.Largest, Steps);
    Counts.Limit = std::max(Counts.Limit, Limit);
  }
  ~CWorkReport() {
    for (const auto &Entry : ByCheck)
      llvm::errs() << "malterlib work: " << Entry.getKey() << " roots "
                   << Entry.getValue().Roots << " exhausted "
                   << Entry.getValue().Exhausted << " largest "
                   << Entry.getValue().Largest << " limit "
                   << Entry.getValue().Limit << "\n";
  }

private:
  std::mutex Mutex;
  llvm::StringMap<CWorkCounts> ByCheck;
};

CWorkReport &workReport() {
  static CWorkReport s_Report;
  return s_Report;
}

thread_local uint64_t t_Generation = 0;
} // namespace

bool workExhausted() {
  t_WorkState.Spent = true;
  return false;
}

uint64_t workGeneration() { return t_Generation; }

CWorkScope::CWorkScope(llvm::StringRef Check, uint64_t Limit, bool Report)
    : Check(Check), Limit(Limit), Report(Report),
      SpentBefore(t_WorkState.Spent), Previous(t_WorkState) {
  t_WorkState = {Limit, true, t_WorkState.Spent};
}

CWorkScope::~CWorkScope() {
  const CWorkState Ended = t_WorkState;
  if (Report)
    workReport().add(Check, Limit - Ended.Left,
                     Ended.Spent && !SpentBefore && Ended.Left == 0, Limit);
  t_WorkState = Previous;
  t_WorkState.Spent = Ended.Spent;
  // The match is done: what was kept of the AST while a root of it was given
  // up is dropped (astIdentity), not before, while its walks can still hold
  // what they were given.
  if (!Previous.Active && Ended.Spent) {
    ++t_Generation;
    t_WorkState.Spent = false;
  }
}

} // namespace clang::tidy::malterlib
