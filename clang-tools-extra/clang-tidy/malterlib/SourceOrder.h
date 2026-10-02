//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_SOURCEORDER_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_SOURCEORDER_H

#include "clang/AST/ASTContext.h"
#include "clang/AST/Stmt.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"

namespace clang::tidy::malterlib {

/// Whether \p Left is before \p Right in the translation unit, an invalid
/// location before each valid one. Equal locations are neither.
inline bool isBeforeInSource(SourceLocation Left, SourceLocation Right,
                             const SourceManager &SM) {
  if (Left == Right || Right.isInvalid())
    return false;
  if (Left.isInvalid())
    return true;
  return SM.isBeforeInTranslationUnit(Left, Right);
}

/// Whether \p Left is before \p Right in the translation unit: by where they
/// begin, then the one around the other first. Equal ranges are neither.
inline bool isBeforeInSource(SourceRange Left, SourceRange Right,
                             const SourceManager &SM) {
  if (isBeforeInSource(Left.getBegin(), Right.getBegin(), SM))
    return true;
  if (isBeforeInSource(Right.getBegin(), Left.getBegin(), SM))
    return false;
  return isBeforeInSource(Right.getEnd(), Left.getEnd(), SM);
}

/// Whether \p Left comes before \p Right in the source: by where they begin,
/// then the one around the other first, then the order clang made them in
/// (Stmt::getID, an offset in the allocator of the AST that is the same in
/// each compile of the same source), so a total order of the statements of a
/// translation unit that doesn't depend on where they are in memory.
inline bool isBeforeInSource(const Stmt *Left, const Stmt *Right,
                             const ASTContext &Context) {
  const SourceManager &SM = Context.getSourceManager();
  if (isBeforeInSource(Left->getSourceRange(), Right->getSourceRange(), SM))
    return true;
  if (isBeforeInSource(Right->getSourceRange(), Left->getSourceRange(), SM))
    return false;
  return Left->getID(Context) < Right->getID(Context);
}

/// The statements of \p Range, a container iterated in an order of its own,
/// like a set keyed by pointers, in the order of the source
/// (isBeforeInSource), for what is reported or decided by the order.
template <typename t_CStatement, typename t_CRange>
llvm::SmallVector<const t_CStatement *, 4>
inSourceOrder(const t_CRange &Range, const ASTContext &Context) {
  llvm::SmallVector<const t_CStatement *, 4> Ordered(Range.begin(),
                                                     Range.end());
  llvm::sort(Ordered, [&](const t_CStatement *Left, const t_CStatement *Right) {
    return isBeforeInSource(Left, Right, Context);
  });
  return Ordered;
}

} // namespace clang::tidy::malterlib

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_SOURCEORDER_H
