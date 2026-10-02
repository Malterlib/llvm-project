//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CoawaitPrecedenceCheck.h"
#include "CoroutineUtils.h"
#include "NamingUtils.h"
#include "clang/AST/ExprCXX.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"

using namespace clang::ast_matchers;

namespace clang::tidy::malterlib {

void CoawaitPrecedenceCheck::registerMatchers(MatchFinder *Finder) {
  // Each kind of node by itself, which the finder tries only on nodes of that
  // kind, not on every expression.
  Finder->addMatcher(
      cxxOperatorCallExpr(hasOverloadedOperatorName("%")).bind("modulo"), this);
  Finder->addMatcher(binaryOperator(hasOperatorName("%")).bind("modulo"), this);
}

void CoawaitPrecedenceCheck::check(const MatchFinder::MatchResult &Result) {
  // Nothing in a translation unit with an error that stops it from compiling
  // (hasUncompilableErrors): no flow is built there, and what the errors leave
  // can't be relied on.
  if (hasUncompilableErrors(*Result.Context))
    return;
  if (Repeated.repeated(Result))
    return;
  const auto *Modulo = Result.Nodes.getNodeAs<Expr>("modulo");
  // What a template is instantiated as, not the template, whose instantiations
  // tell what `%` is.
  if (isInTemplatePattern(DynTypedNode::create(*Modulo), *Result.Context))
    return;
  const Expr *Left = nullptr;
  const Expr *Right = nullptr;
  if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(Modulo)) {
    Left = Operator->getArg(0);
    Right = Operator->getArg(1);
  } else if (const auto *Binary = dyn_cast<BinaryOperator>(Modulo)) {
    Left = Binary->getLHS();
    Right = Binary->getRHS();
  } else
    return;
  // `%` adds a message to the errors of a future, while on numbers it is the
  // remainder, like `co_await fg_Count() % 2`.
  const Expr *Message = Right->IgnoreParenImpCasts();
  if (!isa<StringLiteral>(Message) &&
      (Message->getType()->isArithmeticType() ||
       Message->getType()->isEnumeralType()))
    return;
  // `(co_await X) % Y` says what it means.
  const Expr *Awaited = Left->IgnoreImplicit();
  if (!isa<CoawaitExpr>(Awaited))
    return;
  // So does a remainder of a number that is a class, of the same type as what
  // is awaited, like `co_await fg_Read() % Divisor` of a `TCInt`.
  if (!isa<StringLiteral>(Message) &&
      Result.Context->hasSameUnqualifiedType(
          Message->getType().getNonReferenceType(),
          Awaited->getType().getNonReferenceType()))
    return;

  const SourceManager &SM = *Result.SourceManager;
  const SourceLocation Loc = reportLocation(Awaited->getBeginLoc(), SM);
  if (Loc.isInvalid())
    return;
  // No fix-it: the parenthesized form gives `%` another operand, whose
  // result may not fit where the original's did.
  diag(Loc, "'%%' applies to the result of 'co_await', which binds tighter; "
            "parenthesize the operand of 'co_await' to apply it to what is "
            "awaited");
}

} // namespace clang::tidy::malterlib
