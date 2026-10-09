//===--- EvaluatedExprVisitor.h - Evaluated expression visitor --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the EvaluatedExprVisitor class template, which visits
//  the potentially-evaluated subexpressions of a potentially-evaluated
//  expression.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_CLANG_AST_EVALUATEDEXPRVISITOR_H
#define LLVM_CLANG_AST_EVALUATEDEXPRVISITOR_H

#include "clang/AST/DeclCXX.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/StmtVisitor.h"
#include "llvm/ADT/STLExtras.h"

namespace clang {

class ASTContext;

/// Given a potentially-evaluated expression, this visitor visits all
/// of its potentially-evaluated subexpressions, recursively.
template<template <typename> class Ptr, typename ImplClass>
class EvaluatedExprVisitorBase : public StmtVisitorBase<Ptr, ImplClass, void> {
protected:
  const ASTContext &Context;

public:
  // Return whether this visitor should recurse into discarded statements for a
  // 'constexpr-if'.
  bool shouldVisitDiscardedStmt() const { return true; }
  // Return whether the operand of a co_await or co_yield is visited only in
  // the expression that makes its awaiter when that expression has it. Only a
  // visitor that goes into every subexpression of that expression can, as one
  // that stops at a call would miss the operand in it.
  bool shouldVisitCoroutineOperandOnce() const { return false; }
#define PTR(CLASS) typename Ptr<CLASS>::type

  explicit EvaluatedExprVisitorBase(const ASTContext &Context) : Context(Context) { }

  // Expressions that have no potentially-evaluated subexpressions (but may have
  // other sub-expressions).
  void VisitDeclRefExpr(PTR(DeclRefExpr) E) { }
  void VisitOffsetOfExpr(PTR(OffsetOfExpr) E) { }
  void VisitUnaryExprOrTypeTraitExpr(PTR(UnaryExprOrTypeTraitExpr) E) { }
  void VisitExpressionTraitExpr(PTR(ExpressionTraitExpr) E) { }
  void VisitBlockExpr(PTR(BlockExpr) E) { }
  void VisitCXXUuidofExpr(PTR(CXXUuidofExpr) E) { }
  void VisitCXXNoexceptExpr(PTR(CXXNoexceptExpr) E) { }

  void VisitMemberExpr(PTR(MemberExpr) E) {
    // Only the base matters.
    return this->Visit(E->getBase());
  }

  void VisitChooseExpr(PTR(ChooseExpr) E) {
    // Don't visit either child expression if the condition is dependent.
    if (E->getCond()->isValueDependent())
      return;
    // Only the selected subexpression matters; the other one is not evaluated.
    return this->Visit(E->getChosenSubExpr());
  }

  void VisitGenericSelectionExpr(PTR(GenericSelectionExpr) E) {
    // The controlling expression of a generic selection is not evaluated.

    // Don't visit either child expression if the condition is type-dependent.
    if (E->isResultDependent())
      return;
    // Only the selected subexpression matters; the other subexpressions and the
    // controlling expression are not evaluated.
    return this->Visit(E->getResultExpr());
  }

  void VisitDesignatedInitExpr(PTR(DesignatedInitExpr) E) {
    // Only the actual initializer matters; the designators are all constant
    // expressions.
    return this->Visit(E->getInit());
  }

  void VisitCXXTypeidExpr(PTR(CXXTypeidExpr) E) {
    if (E->isPotentiallyEvaluated())
      return this->Visit(E->getExprOperand());
  }

  void VisitCallExpr(PTR(CallExpr) CE) {
    if (!CE->isUnevaluatedBuiltinCall(Context))
      return getDerived().VisitExpr(CE);
  }

  void VisitLambdaExpr(PTR(LambdaExpr) LE) {
    // Only visit the capture initializers, and not the body.
    for (LambdaExpr::const_capture_init_iterator I = LE->capture_init_begin(),
                                                 E = LE->capture_init_end();
         I != E; ++I)
      if (*I)
        this->Visit(*I);
  }

  void VisitCoroutineSuspendExpr(PTR(CoroutineSuspendExpr) E) {
    // The operand of a co_await or co_yield is evaluated as part of the
    // expression that makes its awaiter, which is a child too; visiting it
    // in both would visit nested awaits a number of times that doubles with
    // each level. A visitor that goes into all of that expression visits it
    // once, there, when that expression has it. Another visits it as an
    // expression, as any other, and so all of its children.
    if (!getDerived().shouldVisitCoroutineOperandOnce()) {
      getDerived().VisitExpr(E);
      return;
    }
    const Expr *Operand = E->getOperand();
    const Expr *Common = E->getCommonExpr();
    bool InCommon = false;
    if (Operand && Common) {
      // Not into the awaits in it, whose operands are not this one.
      llvm::SmallVector<const Stmt *, 8> Pending{Common};
      while (!Pending.empty() && !InCommon) {
        const Stmt *Current = Pending.pop_back_val();
        if (Current == Operand) {
          InCommon = true;
          break;
        }
        if (isa<CoroutineSuspendExpr>(Current))
          continue;
        for (const Stmt *Child : Current->children())
          if (Child)
            Pending.push_back(Child);
      }
    }
    // When it is that expression itself, `co_await Awaiter` of an lvalue,
    // it is visited once, as that.
    bool OperandVisited = false;
    for (auto *SubStmt : E->children()) {
      if (!SubStmt)
        continue;
      if (SubStmt == Operand) {
        if ((InCommon && Common != Operand) || OperandVisited)
          continue;
        OperandVisited = true;
      }
      this->Visit(SubStmt);
    }
  }

  /// The basis case walks all of the children of the statement or
  /// expression, assuming they are all potentially evaluated.
  void VisitStmt(PTR(Stmt) S) {
    for (auto *SubStmt : S->children())
      if (SubStmt)
        this->Visit(SubStmt);
  }

  void VisitIfStmt(PTR(IfStmt) If) {
    if (!getDerived().shouldVisitDiscardedStmt()) {
      if (auto SubStmt = If->getNondiscardedCase(Context)) {
        if (*SubStmt)
          this->Visit(*SubStmt);
        return;
      }
    }

    getDerived().VisitStmt(If);
  }

  ImplClass &getDerived() { return *static_cast<ImplClass *>(this); }

#undef PTR
};

/// EvaluatedExprVisitor - This class visits 'Expr *'s
template <typename ImplClass>
class EvaluatedExprVisitor
    : public EvaluatedExprVisitorBase<std::add_pointer, ImplClass> {
public:
  explicit EvaluatedExprVisitor(const ASTContext &Context)
      : EvaluatedExprVisitorBase<std::add_pointer, ImplClass>(Context) {}
};

/// ConstEvaluatedExprVisitor - This class visits 'const Expr *'s.
template <typename ImplClass>
class ConstEvaluatedExprVisitor
    : public EvaluatedExprVisitorBase<llvm::make_const_ptr, ImplClass> {
public:
  explicit ConstEvaluatedExprVisitor(const ASTContext &Context)
      : EvaluatedExprVisitorBase<llvm::make_const_ptr, ImplClass>(Context) {}
};
}

#endif // LLVM_CLANG_AST_EVALUATEDEXPRVISITOR_H
