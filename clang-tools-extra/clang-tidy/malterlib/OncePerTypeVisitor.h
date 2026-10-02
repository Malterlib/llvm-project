//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_ONCEPERTYPEVISITOR_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_ONCEPERTYPEVISITOR_H

#include "clang/AST/RecursiveASTVisitor.h"
#include "llvm/ADT/DenseSet.h"

namespace clang::tidy::malterlib {

/// A RecursiveASTVisitor that goes through each type that isn't where it is
/// written once, like the type of a local of an instantiation where it is
/// written as the template parameter, which a type that names another twice,
/// like the parameters and result of `F39 (*)(F39, F39)`, meets once for each
/// path to it, a number of times exponential in the depth of such types. What
/// is in a type is the same each time.
template <typename Derived>
class OncePerTypeVisitor : public RecursiveASTVisitor<Derived> {
public:
  bool TraverseType(QualType T, bool TraverseQualifier = true) {
    if (!T.isNull() && !TraversedTypes.insert(T.getAsOpaquePtr()).second)
      return true;
    return RecursiveASTVisitor<Derived>::TraverseType(T, TraverseQualifier);
  }

  /// Forgets the types gone through, for a visitor that is used again.
  void clearTraversedTypes() { TraversedTypes.clear(); }

private:
  llvm::DenseSet<const void *> TraversedTypes;
};

} // namespace clang::tidy::malterlib

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_ONCEPERTYPEVISITOR_H
