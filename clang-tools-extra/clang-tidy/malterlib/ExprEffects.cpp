//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ExprEffects.h"
#include "CheckedAccess.h"
#include "CoroutineUtils.h"
#include "HeldData.h"
#include "NamingUtils.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ExprConcepts.h"
#include "clang/AST/ExprObjC.h"
#include "clang/AST/ExprOpenMP.h"
#include "clang/AST/StmtCXX.h"
#include "clang/AST/StmtObjC.h"
#include "clang/AST/StmtOpenACC.h"
#include "clang/AST/StmtOpenMP.h"
#include "clang/AST/StmtSYCL.h"

// Every switch over a kind of clang's lists all of them, no default, so that
// one added there is an error here until it is classified.
#pragma clang diagnostic error "-Wswitch"

namespace clang::tidy::malterlib {

namespace {

/// The effects of one statement, by its class: an overload for each concrete
/// class, which the dispatch generated from clang's list of them calls; the
/// template is deleted, so that a class without its own overload doesn't
/// compile rather than fall back to one of a base.
class CEffectsBuilder {
public:
  CEffectsBuilder(const ASTContext &Context, CEffects &Result)
      : Context(Context), Result(Result) {}

  template <typename t_CStmt> void of(const t_CStmt *) = delete;

  // Statements.
  void of(const WhileStmt *S);
  void of(const LabelStmt *S);
  void of(const AttributedStmt *S);
  void of(const SwitchStmt *S);
  void of(const CaseStmt *S);
  void of(const DefaultStmt *S);
  void of(const ReturnStmt *S);
  void of(const NullStmt *S);
  void of(const ContinueStmt *S);
  void of(const BreakStmt *S);
  void of(const IndirectGotoStmt *S);
  void of(const IfStmt *S);
  void of(const GotoStmt *S);
  void of(const ForStmt *S);
  void of(const DoStmt *S);
  void of(const DeferStmt *S);
  void of(const DeclStmt *S);
  void of(const CoroutineBodyStmt *S);
  void of(const CoreturnStmt *S);
  void of(const CompoundStmt *S);
  void of(const CapturedStmt *S);
  void of(const CXXTryStmt *S);
  void of(const CXXForRangeStmt *S);
  void of(const CXXExpansionStmtPattern *S);
  void of(const CXXExpansionStmtInstantiation *S);
  void of(const CXXCatchStmt *S);
  void of(const MSAsmStmt *S);
  void of(const GCCAsmStmt *S);
  void of(const SEHTryStmt *S);
  void of(const SEHLeaveStmt *S);
  void of(const SEHFinallyStmt *S);
  void of(const SEHExceptStmt *S);
  void of(const MSDependentExistsStmt *S);
  void of(const SYCLKernelCallStmt *S);
  void of(const UnresolvedSYCLKernelCallStmt *S);

  // Expressions.
  void of(const VAArgExpr *E);
  void of(const UnaryOperator *E);
  void of(const UnaryExprOrTypeTraitExpr *E);
  void of(const TypeTraitExpr *E);
  void of(const SubstNonTypeTemplateParmPackExpr *E);
  void of(const SubstNonTypeTemplateParmExpr *E);
  void of(const StringLiteral *E);
  void of(const StmtExpr *E);
  void of(const SourceLocExpr *E);
  void of(const SizeOfPackExpr *E);
  void of(const ShuffleVectorExpr *E);
  void of(const SYCLUniqueStableNameExpr *E);
  void of(const RequiresExpr *E);
  void of(const RecoveryExpr *E);
  void of(const PseudoObjectExpr *E);
  void of(const PredefinedExpr *E);
  void of(const ParenListExpr *E);
  void of(const ParenExpr *E);
  void of(const PackIndexingExpr *E);
  void of(const PackExpansionExpr *E);
  void of(const UnresolvedMemberExpr *E);
  void of(const UnresolvedLookupExpr *E);
  void of(const OpaqueValueExpr *E);
  void of(const OffsetOfExpr *E);
  void of(const NoInitExpr *E);
  void of(const MemberExpr *E);
  void of(const MatrixSubscriptExpr *E);
  void of(const MatrixSingleSubscriptExpr *E);
  void of(const MatrixElementExpr *E);
  void of(const MaterializeTemporaryExpr *E);
  void of(const MSPropertySubscriptExpr *E);
  void of(const MSPropertyRefExpr *E);
  void of(const LambdaExpr *E);
  void of(const IntegerLiteral *E);
  void of(const InitListExpr *E);
  void of(const ImplicitValueInitExpr *E);
  void of(const ImaginaryLiteral *E);
  void of(const HLSLOutArgExpr *E);
  void of(const GenericSelectionExpr *E);
  void of(const GNUNullExpr *E);
  void of(const FunctionParmPackExpr *E);
  void of(const ExprWithCleanups *E);
  void of(const ConstantExpr *E);
  void of(const FloatingLiteral *E);
  void of(const FixedPointLiteral *E);
  void of(const ExtVectorElementExpr *E);
  void of(const ExpressionTraitExpr *E);
  void of(const EmbedExpr *E);
  void of(const DesignatedInitUpdateExpr *E);
  void of(const DesignatedInitExpr *E);
  void of(const DependentScopeDeclRefExpr *E);
  void of(const DependentCoawaitExpr *E);
  void of(const DeclRefExpr *E);
  void of(const CoyieldExpr *E);
  void of(const CoawaitExpr *E);
  void of(const ConvertVectorExpr *E);
  void of(const ConceptSpecializationExpr *E);
  void of(const CompoundLiteralExpr *E);
  void of(const ChooseExpr *E);
  void of(const CharacterLiteral *E);
  void of(const ImplicitCastExpr *E);
  void of(const CXXStaticCastExpr *E);
  void of(const CXXReinterpretCastExpr *E);
  void of(const CXXDynamicCastExpr *E);
  void of(const CXXConstCastExpr *E);
  void of(const CXXAddrspaceCastExpr *E);
  void of(const CXXFunctionalCastExpr *E);
  void of(const CStyleCastExpr *E);
  void of(const BuiltinBitCastExpr *E);
  void of(const CallExpr *E);
  void of(const UserDefinedLiteral *E);
  void of(const CXXOperatorCallExpr *E);
  void of(const CXXMemberCallExpr *E);
  void of(const CUDAKernelCallExpr *E);
  void of(const CXXUuidofExpr *E);
  void of(const CXXUnresolvedConstructExpr *E);
  void of(const CXXTypeidExpr *E);
  void of(const CXXThrowExpr *E);
  void of(const CXXThisExpr *E);
  void of(const CXXStdInitializerListExpr *E);
  void of(const CXXScalarValueInitExpr *E);
  void of(const CXXRewrittenBinaryOperator *E);
  void of(const CXXReflectExpr *E);
  void of(const CXXPseudoDestructorExpr *E);
  void of(const CXXParenListInitExpr *E);
  void of(const CXXNullPtrLiteralExpr *E);
  void of(const CXXNoexceptExpr *E);
  void of(const CXXNewExpr *E);
  void of(const CXXInheritedCtorInitExpr *E);
  void of(const CXXFoldExpr *E);
  void of(const CXXExpansionSelectExpr *E);
  void of(const CXXDependentScopeMemberExpr *E);
  void of(const CXXDeleteExpr *E);
  void of(const CXXDefaultInitExpr *E);
  void of(const CXXDefaultArgExpr *E);
  void of(const CXXConstructExpr *E);
  void of(const CXXTemporaryObjectExpr *E);
  void of(const CXXBoolLiteralExpr *E);
  void of(const CXXBindTemporaryExpr *E);
  void of(const BlockExpr *E);
  void of(const BinaryOperator *E);
  void of(const CompoundAssignOperator *E);
  void of(const AtomicExpr *E);
  void of(const AsTypeExpr *E);
  void of(const ArrayTypeTraitExpr *E);
  void of(const ArraySubscriptExpr *E);
  void of(const ArraySectionExpr *E);
  void of(const ArrayInitLoopExpr *E);
  void of(const ArrayInitIndexExpr *E);
  void of(const AddrLabelExpr *E);
  void of(const ConditionalOperator *E);
  void of(const BinaryConditionalOperator *E);

  // What isn't C++: every child is opaque. OpenMP's directives and OpenACC's
  // constructs by their families, the rest one by one.
#define ABSTRACT_STMT(Type)
#define STMT(Type, Base)
#define OMPEXECUTABLEDIRECTIVE(Type, Base)                                     \
  void of(const Type *S) { opaque(S); }
#define OPENACCCONSTRUCTSTMT(Type, Base)                                       \
  void of(const Type *S) { opaque(S); }
#define OBJCOBJECTLITERAL(Type, Base)                                          \
  void of(const Type *S) { opaque(S); }
#include "clang/AST/StmtNodes.inc"
  void of(const OMPCanonicalLoop *S) { opaque(S); }
  void of(const OMPIteratorExpr *S) { opaque(S); }
  void of(const OMPArrayShapingExpr *S) { opaque(S); }
  void of(const OpenACCAsteriskSizeExpr *S) { opaque(S); }
  void of(const ObjCSubscriptRefExpr *S) { opaque(S); }
  void of(const ObjCSelectorExpr *S) { opaque(S); }
  void of(const ObjCProtocolExpr *S) { opaque(S); }
  void of(const ObjCPropertyRefExpr *S) { opaque(S); }
  void of(const ObjCMessageExpr *S) { opaque(S); }
  void of(const ObjCIvarRefExpr *S) { opaque(S); }
  void of(const ObjCIsaExpr *S) { opaque(S); }
  void of(const ObjCIndirectCopyRestoreExpr *S) { opaque(S); }
  void of(const ObjCEncodeExpr *S) { opaque(S); }
  void of(const ObjCBoolLiteralExpr *S) { opaque(S); }
  void of(const ObjCAvailabilityCheckExpr *S) { opaque(S); }
  void of(const ObjCBridgedCastExpr *S) { opaque(S); }
  void of(const ObjCForCollectionStmt *S) { opaque(S); }
  void of(const ObjCAutoreleasePoolStmt *S) { opaque(S); }
  void of(const ObjCAtTryStmt *S) { opaque(S); }
  void of(const ObjCAtThrowStmt *S) { opaque(S); }
  void of(const ObjCAtSynchronizedStmt *S) { opaque(S); }
  void of(const ObjCAtFinallyStmt *S) { opaque(S); }
  void of(const ObjCAtCatchStmt *S) { opaque(S); }

private:
  void add(const Stmt *Operand, EUse Use, unsigned Sequence,
           CDestination Into = {}) {
    if (Operand)
      Result.Operands.push_back({Operand, Use, Into, Sequence});
  }
  /// \p Value given to \p Into: bound when it is a glvalue, which only a
  /// reference takes, made from by value otherwise.
  void give(const Expr *Value, CDestination Into, unsigned Sequence) {
    if (Value)
      add(Value, Value->isGLValue() ? EUse::Bind : EUse::Copy, Sequence, Into);
  }
  void opaque(const Stmt *S) {
    for (const Stmt *Child : S->children())
      add(Child, EUse::Opaque, 0);
  }
  void castOperand(const CastExpr *E);
  void call(const CallExpr *E, unsigned FirstArgument, const Expr *Object);
  void initializers(const Expr *Made, llvm::ArrayRef<const Expr *> Inits,
                    const Expr *Filler, const FieldDecl *UnionField);

  const ASTContext &Context;
  CEffects &Result;
};

// Statements.

void CEffectsBuilder::of(const WhileStmt *S) {
  add(S->getConditionVariableDeclStmt(), EUse::Statement, 0);
  add(S->getCond(), EUse::Condition, 1);
  add(S->getBody(), EUse::Statement, 2);
}

void CEffectsBuilder::of(const LabelStmt *S) {
  add(S->getSubStmt(), EUse::Statement, 0);
}

void CEffectsBuilder::of(const AttributedStmt *S) {
  add(S->getSubStmt(), EUse::Statement, 0);
}

void CEffectsBuilder::of(const SwitchStmt *S) {
  add(S->getInit(), EUse::Statement, 0);
  add(S->getConditionVariableDeclStmt(), EUse::Statement, 1);
  add(S->getCond(), EUse::Condition, 2);
  add(S->getBody(), EUse::Statement, 3);
}

void CEffectsBuilder::of(const CaseStmt *S) {
  // The values are constants.
  add(S->getLHS(), EUse::Unevaluated, 0);
  add(S->getRHS(), EUse::Unevaluated, 0);
  add(S->getSubStmt(), EUse::Statement, 1);
}

void CEffectsBuilder::of(const DefaultStmt *S) {
  add(S->getSubStmt(), EUse::Statement, 0);
}

void CEffectsBuilder::of(const ReturnStmt *S) {
  Result.Statement = EStatement::Return;
  Result.bLeaves = true;
  give(S->getRetValue(), {EDestination::Return}, 0);
}

void CEffectsBuilder::of(const NullStmt *) {}
void CEffectsBuilder::of(const ContinueStmt *) {}
void CEffectsBuilder::of(const BreakStmt *) {}
void CEffectsBuilder::of(const GotoStmt *) {}

void CEffectsBuilder::of(const IndirectGotoStmt *S) {
  add(S->getTarget(), EUse::Value, 0);
}

void CEffectsBuilder::of(const IfStmt *S) {
  add(S->getInit(), EUse::Statement, 0);
  add(S->getConditionVariableDeclStmt(), EUse::Statement, 1);
  add(S->getCond(), EUse::Condition, 2);
  // Of `if consteval`, the branch that is constant evaluated doesn't run;
  // of `if constexpr`, the one discarded.
  const Stmt *Skipped = nullptr;
  if (S->isConsteval())
    Skipped = S->isNegatedConsteval() ? S->getElse() : S->getThen();
  else if (S->isConstexpr() && S->getCond() &&
           !S->getCond()->isValueDependent())
    if (const std::optional<const Stmt *> Case = nondiscardedCaseOf(S))
      Skipped = *Case == S->getThen() ? S->getElse() : S->getThen();
  for (const Stmt *Branch : {S->getThen(), S->getElse()})
    add(Branch, Branch == Skipped ? EUse::Unevaluated : EUse::Statement, 3);
}

void CEffectsBuilder::of(const ForStmt *S) {
  add(S->getInit(), EUse::Statement, 0);
  add(S->getConditionVariableDeclStmt(), EUse::Statement, 1);
  add(S->getCond(), EUse::Condition, 2);
  add(S->getBody(), EUse::Statement, 3);
  add(S->getInc(), EUse::Discarded, 4);
}

void CEffectsBuilder::of(const DoStmt *S) {
  add(S->getBody(), EUse::Statement, 0);
  add(S->getCond(), EUse::Condition, 1);
}

void CEffectsBuilder::of(const DeferStmt *S) {
  add(S->getBody(), EUse::Statement, 0);
}

void CEffectsBuilder::of(const DeclStmt *S) {
  Result.Statement = EStatement::Declaration;
  // By what each declaration is: a variable is given its initializer, an
  // expansion statement runs; the rest, types, functions, assertions, hold
  // and run nothing here.
  unsigned Sequence = 0;
  for (const Decl *Declared : S->decls()) {
    Result.Declares.push_back(Declared);
    switch (holderKind(Declared)) {
    case EHolder::Variable:
    case EHolder::Decomposition:
    case EHolder::Parameter:
    case EHolder::ImplicitParameter:
      if (const Expr *Init = cast<VarDecl>(Declared)->getInit())
        give(Init, {EDestination::Variable, Declared}, Sequence);
      break;
    case EHolder::Statement:
      add(cast<CXXExpansionStmtDecl>(Declared)->getInstantiations(),
          EUse::Statement, Sequence);
      break;
    case EHolder::None:
    case EHolder::Binding:
    case EHolder::Field:
    case EHolder::IndirectField:
    case EHolder::Temporary:
    case EHolder::Constant:
    case EHolder::Function:
    case EHolder::Opaque:
      break;
    }
    ++Sequence;
  }
  // The sizes of the arrays of variable length of what it declares, also
  // through a type it declares, `typedef int CRow[n];`, evaluated first.
  for (const Decl *Declared : S->decls()) {
    QualType Type;
    if (const auto *Value = dyn_cast<ValueDecl>(Declared))
      Type = Value->getType();
    else if (const auto *Name = dyn_cast<TypedefNameDecl>(Declared))
      Type = Name->getUnderlyingType();
    for (int Depth = 0; !Type.isNull() && Depth < 16; ++Depth) {
      const ArrayType *Array = Context.getAsArrayType(Type);
      if (!Array)
        break;
      if (const auto *Variable = dyn_cast<VariableArrayType>(Array))
        add(Variable->getSizeExpr(), EUse::Value, 0);
      Type = Array->getElementType();
    }
  }
}

void CEffectsBuilder::of(const CoroutineBodyStmt *S) {
  Result.Statement = EStatement::CoroutineBody;
  // What runs of the coroutine, as its parts: the promise, the parameters
  // moved, the initial suspension, the body, the final suspension, the
  // handlers and what makes and returns what it gives.
  unsigned Sequence = 0;
  using CParts = std::initializer_list<const Stmt *>;
  for (const Stmt *Part : CParts{S->getAllocate(), S->getResultDecl(),
                                 S->getPromiseDeclStmt()})
    add(Part, EUse::Statement, Sequence++);
  for (const Stmt *Move : S->getParamMoves())
    add(Move, EUse::Statement, Sequence);
  ++Sequence;
  for (const Stmt *Part :
       CParts{S->getInitSuspendStmt(), S->getBody(), S->getFallthroughHandler(),
              S->getExceptionHandler(), S->getFinalSuspendStmt(),
              S->getDeallocate(), S->getReturnValueInit(), S->getReturnStmt(),
              S->getReturnStmtOnAllocFailure()})
    add(Part, EUse::Statement, Sequence++);
}

void CEffectsBuilder::of(const CoreturnStmt *S) {
  Result.Statement = EStatement::Coreturn;
  Result.bLeaves = true;
  give(S->getOperand(), {EDestination::Return}, 0);
  // The call of the promise's `return_value` or `return_void`, given the
  // operand.
  add(S->getPromiseCall(), EUse::Discarded, 1);
}

void CEffectsBuilder::of(const CompoundStmt *S) {
  // The last is the value of a statement expression, which the compound
  // statement gives on; a statement's value is dropped otherwise.
  unsigned Sequence = 0;
  for (const Stmt *Child : S->body())
    add(Child,
        Child == S->body_back() && isa<Expr>(Child) ? EUse::Forward
                                                    : EUse::Statement,
        Sequence++);
}

void CEffectsBuilder::of(const CapturedStmt *S) { opaque(S); }

void CEffectsBuilder::of(const CXXTryStmt *S) {
  add(S->getTryBlock(), EUse::Statement, 0);
  for (unsigned iHandler = 0; iHandler < S->getNumHandlers(); ++iHandler)
    add(S->getHandler(iHandler), EUse::Statement, 1);
}

void CEffectsBuilder::of(const CXXForRangeStmt *S) {
  Result.Statement = EStatement::RangeLoop;
  add(S->getInit(), EUse::Statement, 0);
  add(S->getRangeStmt(), EUse::Statement, 1);
  add(S->getBeginStmt(), EUse::Statement, 2);
  add(S->getEndStmt(), EUse::Statement, 2);
  add(S->getCond(), EUse::Condition, 3);
  add(S->getLoopVarStmt(), EUse::Statement, 4);
  add(S->getBody(), EUse::Statement, 5);
  add(S->getInc(), EUse::Discarded, 6);
}

void CEffectsBuilder::of(const CXXExpansionStmtPattern *S) {
  // A pattern, which its instantiations are.
  for (const Stmt *Child : S->children())
    add(Child, EUse::Unevaluated, 0);
}

void CEffectsBuilder::of(const CXXExpansionStmtInstantiation *S) {
  unsigned Sequence = 0;
  for (const Stmt *Preamble : S->getPreambleStmts())
    add(Preamble, EUse::Statement, Sequence++);
  for (const Stmt *Instantiation : S->getInstantiations())
    add(Instantiation, EUse::Statement, Sequence++);
}

void CEffectsBuilder::of(const CXXCatchStmt *S) {
  add(S->getHandlerBlock(), EUse::Statement, 0);
}

void CEffectsBuilder::of(const MSAsmStmt *S) { opaque(S); }
void CEffectsBuilder::of(const GCCAsmStmt *S) { opaque(S); }
void CEffectsBuilder::of(const SEHTryStmt *S) {
  add(S->getTryBlock(), EUse::Statement, 0);
  add(S->getHandler(), EUse::Statement, 1);
}
void CEffectsBuilder::of(const SEHLeaveStmt *) {}
void CEffectsBuilder::of(const SEHFinallyStmt *S) {
  add(S->getBlock(), EUse::Statement, 0);
}

void CEffectsBuilder::of(const SEHExceptStmt *S) {
  add(S->getFilterExpr(), EUse::Condition, 0);
  add(S->getBlock(), EUse::Statement, 1);
}

void CEffectsBuilder::of(const MSDependentExistsStmt *S) { opaque(S); }
void CEffectsBuilder::of(const SYCLKernelCallStmt *S) { opaque(S); }
void CEffectsBuilder::of(const UnresolvedSYCLKernelCallStmt *S) { opaque(S); }

// Expressions.

void CEffectsBuilder::of(const VAArgExpr *E) {
  add(E->getSubExpr(), EUse::Access, 0);
}

void CEffectsBuilder::of(const UnaryOperator *E) {
  const Expr *Operand = E->getSubExpr();
  switch (E->getOpcode()) {
  case UO_PostInc:
  case UO_PostDec:
    add(Operand, EUse::Update, 0);
    return;
  // What is given back is the operand, changed.
  case UO_PreInc:
  case UO_PreDec:
    add(Operand, EUse::Update, 0);
    add(Operand, EUse::Forward, 0);
    return;
  case UO_AddrOf:
    add(Operand, EUse::AddressOf, 0);
    return;
  case UO_Deref:
    add(Operand, EUse::Dereference, 0);
    return;
  case UO_Plus:
    // Of a pointer, the pointer.
    add(Operand,
        Operand->getType()->isPointerType() ? EUse::Forward : EUse::Value, 0);
    return;
  case UO_Minus:
  case UO_Not:
    add(Operand, EUse::Value, 0);
    return;
  case UO_LNot:
    add(Operand, EUse::Condition, 0);
    return;
  // The part of a complex glvalue, or a value.
  case UO_Real:
  case UO_Imag:
    add(Operand, E->isGLValue() ? EUse::Forward : EUse::Value, 0);
    return;
  case UO_Extension:
    add(Operand, EUse::Forward, 0);
    return;
  // Only in what is dependent.
  case UO_Coawait:
    add(Operand, EUse::Opaque, 0);
    return;
  }
}

void CEffectsBuilder::of(const UnaryExprOrTypeTraitExpr *E) {
  // The operand of `sizeof` of an array of variable length is evaluated.
  const bool Evaluated = E->getKind() == UETT_SizeOf &&
                         E->getTypeOfArgument()->isVariableArrayType();
  if (!E->isArgumentType())
    add(E->getArgumentExpr(), Evaluated ? EUse::Value : EUse::Unevaluated, 0);
}

void CEffectsBuilder::of(const TypeTraitExpr *) {}

void CEffectsBuilder::of(const SubstNonTypeTemplateParmPackExpr *) {}

void CEffectsBuilder::of(const SubstNonTypeTemplateParmExpr *E) {
  add(E->getReplacement(), EUse::Forward, 0);
}

void CEffectsBuilder::of(const StringLiteral *) {}

void CEffectsBuilder::of(const StmtExpr *E) {
  add(E->getSubStmt(), EUse::Forward, 0);
}

void CEffectsBuilder::of(const SourceLocExpr *) {}
void CEffectsBuilder::of(const SizeOfPackExpr *) {}

void CEffectsBuilder::of(const ShuffleVectorExpr *E) {
  for (unsigned iExpr = 0; iExpr < E->getNumSubExprs(); ++iExpr)
    add(E->getExpr(iExpr), EUse::Value, 0);
}

void CEffectsBuilder::of(const SYCLUniqueStableNameExpr *) {}

void CEffectsBuilder::of(const RequiresExpr *) {
  // Its requirements aren't evaluated, and aren't its children.
}

void CEffectsBuilder::of(const RecoveryExpr *E) { opaque(E); }

void CEffectsBuilder::of(const PseudoObjectExpr *E) {
  // What the syntax says is told by what it means, which the semantic
  // expressions are, in order: an opaque value is what its source gives the
  // others, the result is the value, the rest are discarded,
  // `__builtin_dump_struct`.
  add(E->getSyntacticForm(), EUse::Unevaluated, 0);
  unsigned Sequence = 0;
  for (const Expr *Semantic : E->semantics()) {
    if (const auto *Opaque = dyn_cast<OpaqueValueExpr>(Semantic)) {
      add(Opaque, EUse::Unevaluated, Sequence);
      give(Opaque->getSourceExpr(),
           {EDestination::Temporary, nullptr, nullptr, Opaque}, Sequence);
    } else {
      add(Semantic,
          Semantic == E->getResultExpr() ? EUse::Forward : EUse::Discarded,
          Sequence);
    }
    ++Sequence;
  }
}

void CEffectsBuilder::of(const PredefinedExpr *E) {
  add(E->getFunctionName(), EUse::Forward, 0);
}

void CEffectsBuilder::of(const ParenListExpr *E) { opaque(E); }

void CEffectsBuilder::of(const ParenExpr *E) {
  add(E->getSubExpr(), EUse::Forward, 0);
}

void CEffectsBuilder::of(const PackIndexingExpr *E) {
  // The pack expression and the index are what selects; of what is
  // expanded, the one selected is the result.
  add(E->getPackIdExpression(), EUse::Unevaluated, 0);
  add(E->getIndexExpr(), EUse::Unevaluated, 0);
  const Expr *Selected =
      E->isInstantiationDependent() ? nullptr : E->getSelectedExpr();
  for (const Expr *Expression : E->getExpressions())
    add(Expression,
        Expression == Selected ? EUse::Forward : EUse::Unevaluated, 0);
}

void CEffectsBuilder::of(const PackExpansionExpr *E) { opaque(E); }
void CEffectsBuilder::of(const UnresolvedMemberExpr *E) { opaque(E); }
void CEffectsBuilder::of(const UnresolvedLookupExpr *E) { opaque(E); }

// What it stands for, which what binds it evaluates once, is what it gives
// on.
void CEffectsBuilder::of(const OpaqueValueExpr *E) {
  add(E->getSourceExpr(), EUse::Forward, 0);
}

void CEffectsBuilder::of(const OffsetOfExpr *E) {
  for (unsigned iIndex = 0; iIndex < E->getNumExpressions(); ++iIndex)
    add(E->getIndexExpr(iIndex), EUse::Value, 0);
}

void CEffectsBuilder::of(const NoInitExpr *) {}

void CEffectsBuilder::of(const MemberExpr *E) {
  Result.Statement = EStatement::Member;
  const Expr *Base = E->getBase();
  const ValueDecl *Member = E->getMemberDecl();
  // By what the member is (holderKind): a data member is a part of the object,
  // or what a pointer to it points to; a reference member is read from it,
  // and what it refers to is elsewhere; a member function is bound to the
  // object, which the call uses (CXXMemberCallExpr); a static member or an
  // enumerator only names it, the object is evaluated and dropped.
  switch (holderKind(Member)) {
  case EHolder::Field:
  case EHolder::IndirectField:
    if (Member->getType()->isReferenceType())
      add(Base, EUse::Access, 0);
    else
      add(Base, E->isArrow() ? EUse::Dereference : EUse::Forward, 0);
    return;
  // A member function; what else holds code, a block or a captured
  // statement, is no member, and what is done with its base can't be told.
  case EHolder::Function:
    if (const auto *Method = dyn_cast<CXXMethodDecl>(Member))
      add(Base,
          Method->isImplicitObjectMemberFunction() ? EUse::Forward
                                                   : EUse::Discarded,
          0);
    else
      add(Base, EUse::Opaque, 0);
    return;
  case EHolder::Opaque:
    add(Base, EUse::Opaque, 0);
    return;
  case EHolder::None:
  case EHolder::Variable:
  case EHolder::Parameter:
  case EHolder::ImplicitParameter:
  case EHolder::Decomposition:
  case EHolder::Binding:
  case EHolder::Temporary:
  case EHolder::Constant:
  case EHolder::Statement:
    add(Base, EUse::Discarded, 0);
    return;
  }
}

void CEffectsBuilder::of(const MatrixSubscriptExpr *E) {
  add(E->getBase(), E->getBase()->isGLValue() ? EUse::Forward : EUse::Value, 0);
  add(E->getRowIdx(), EUse::Value, 1);
  add(E->getColumnIdx(), EUse::Value, 1);
}

void CEffectsBuilder::of(const MatrixSingleSubscriptExpr *E) {
  add(E->getBase(), E->getBase()->isGLValue() ? EUse::Forward : EUse::Value, 0);
  add(E->getRowIdx(), EUse::Value, 1);
}

void CEffectsBuilder::of(const MatrixElementExpr *E) {
  add(E->getBase(), E->getBase()->isGLValue() ? EUse::Forward : EUse::Value, 0);
}

void CEffectsBuilder::of(const MaterializeTemporaryExpr *E) {
  Result.Statement = EStatement::Temporary;
  give(E->getSubExpr(), {EDestination::Temporary, nullptr, nullptr, E}, 0);
}

void CEffectsBuilder::of(const MSPropertySubscriptExpr *E) { opaque(E); }
void CEffectsBuilder::of(const MSPropertyRefExpr *E) { opaque(E); }

void CEffectsBuilder::of(const LambdaExpr *E) {
  Result.Statement = EStatement::Lambda;
  unsigned iCapture = 0;
  const auto CaptureInits = captureInitsOf(E);
  auto Init = CaptureInits.begin();
  for (const LambdaCapture &Capture : E->captures()) {
    const Expr *CaptureInit = *Init++;
    // The members of the closure are initialized in an order that isn't
    // told, each in the same sequence.
    const CDestination Into{
        EDestination::Capture,
        Capture.capturesVariable() ? Capture.getCapturedVar() : nullptr,
        nullptr, E, iCapture};
    switch (Capture.getCaptureKind()) {
    // `this` is a pointer copied, `*this` an object copied.
    case LCK_This:
    case LCK_StarThis:
    case LCK_ByCopy:
      if (CaptureInit)
        add(CaptureInit, EUse::Copy, 0, Into);
      break;
    case LCK_ByRef:
      if (CaptureInit)
        add(CaptureInit, CaptureInit->isGLValue() ? EUse::Bind : EUse::Copy, 0,
            Into);
      break;
    case LCK_VLAType:
      if (CaptureInit)
        add(CaptureInit, EUse::Value, 0);
      break;
    }
    ++iCapture;
  }
  // The body runs where the closure is called, a function of its own.
  add(E->getBody(), EUse::Unevaluated, 1);
}

void CEffectsBuilder::of(const IntegerLiteral *) {}

void CEffectsBuilder::initializers(const Expr *Made,
                                   llvm::ArrayRef<const Expr *> Inits,
                                   const Expr *Filler,
                                   const FieldDecl *UnionField) {
  const QualType Type = Made->getType();
  // The elements of an array, in order, and the filler of the rest.
  if (Type->isArrayType() || Type->isVectorType() ||
      Type->isAnyComplexType()) {
    unsigned iElement = 0;
    for (const Expr *Init : Inits) {
      give(Init, {EDestination::Element, nullptr, nullptr, Made, iElement},
           iElement);
      ++iElement;
    }
    give(Filler, {EDestination::Element, nullptr, nullptr, Made, iElement},
         iElement);
    return;
  }
  const auto *Record = Type->getAsRecordDecl();
  if (!Record) {
    // A scalar in braces, `int Value{x}`, is its value.
    for (const Expr *Init : Inits)
      add(Init, EUse::Forward, 0);
    return;
  }
  // The bases, then the members, skipping unnamed bit-fields; of a union, the
  // one it initializes.
  llvm::SmallVector<CDestination, 8> Targets;
  if (const auto *Class = dyn_cast<CXXRecordDecl>(Record))
    for (const CXXBaseSpecifier &Base : Class->bases()) {
      (void)Base;
      Targets.push_back({EDestination::Base, nullptr, nullptr, Made,
                         static_cast<unsigned>(Targets.size())});
    }
  if (Record->isUnion()) {
    if (UnionField)
      Targets.push_back({EDestination::Field, UnionField, nullptr, Made});
  } else
    for (const FieldDecl *Field : Record->fields())
      if (!Field->isUnnamedBitField())
        Targets.push_back({EDestination::Field, Field, nullptr, Made});
  unsigned iInit = 0;
  for (const Expr *Init : Inits) {
    give(Init,
         iInit < Targets.size()
             ? Targets[iInit]
             : CDestination{EDestination::Field, nullptr, nullptr, Made},
         iInit);
    ++iInit;
  }
}

void CEffectsBuilder::of(const InitListExpr *E) {
  // The syntactic form is told by the semantic one.
  if (E->isSyntacticForm() && E->getSemanticForm()) {
    for (const Expr *Init : E->inits())
      add(Init, EUse::Unevaluated, 0);
    return;
  }
  // Braces that bind a reference, `int &Value{x}`, are what they bind.
  if (E->isGLValue() || E->isTransparent()) {
    for (const Expr *Init : E->inits())
      add(Init, EUse::Forward, 0);
    return;
  }
  llvm::SmallVector<const Expr *, 8> Inits(E->inits());
  initializers(E, Inits, E->getArrayFiller(), E->getInitializedFieldInUnion());
}

void CEffectsBuilder::of(const ImplicitValueInitExpr *) {}

void CEffectsBuilder::of(const ImaginaryLiteral *E) {
  add(E->getSubExpr(), EUse::Value, 0);
}

void CEffectsBuilder::of(const HLSLOutArgExpr *E) { opaque(E); }

void CEffectsBuilder::of(const GenericSelectionExpr *E) {
  if (E->isExprPredicate())
    add(E->getControllingExpr(), EUse::Unevaluated, 0);
  const Expr *Chosen = E->isResultDependent() ? nullptr : E->getResultExpr();
  for (const Expr *Association : E->getAssocExprs())
    add(Association,
        Association == Chosen ? EUse::Forward : EUse::Unevaluated, 0);
}

void CEffectsBuilder::of(const GNUNullExpr *) {}
void CEffectsBuilder::of(const FunctionParmPackExpr *) {}

void CEffectsBuilder::of(const ExprWithCleanups *E) {
  add(E->getSubExpr(), EUse::Forward, 0);
}

void CEffectsBuilder::of(const ConstantExpr *E) {
  add(E->getSubExpr(), EUse::Forward, 0);
}

void CEffectsBuilder::of(const FloatingLiteral *) {}
void CEffectsBuilder::of(const FixedPointLiteral *) {}

void CEffectsBuilder::of(const ExtVectorElementExpr *E) {
  add(E->getBase(), E->getBase()->isGLValue() ? EUse::Forward : EUse::Value, 0);
}

void CEffectsBuilder::of(const ExpressionTraitExpr *E) {
  add(E->getQueriedExpression(), EUse::Unevaluated, 0);
}

void CEffectsBuilder::of(const EmbedExpr *) {}
void CEffectsBuilder::of(const DesignatedInitUpdateExpr *E) { opaque(E); }

void CEffectsBuilder::of(const DesignatedInitExpr *E) {
  // Only in the syntactic form of braces: the value, the first, and the
  // indexes, which are constants.
  for (unsigned iExpr = 0; iExpr < E->getNumSubExprs(); ++iExpr)
    add(E->getSubExpr(iExpr), iExpr == 0 ? EUse::Forward : EUse::Unevaluated,
        0);
}

void CEffectsBuilder::of(const DependentScopeDeclRefExpr *) {}

void CEffectsBuilder::of(const DependentCoawaitExpr *E) {
  Result.bSuspends = true;
  opaque(E);
}

void CEffectsBuilder::of(const DeclRefExpr *) {
  Result.Statement = EStatement::Name;
}

/// Of `co_await` and `co_yield`: what is awaited, the awaiter made of it,
/// which holds it while the coroutine is suspended, whether it is ready, the
/// suspension, and the result.
static void suspension(const CoroutineSuspendExpr *E,
                       llvm::function_ref<void(const Stmt *, EUse, unsigned,
                                               CDestination)>
                           Add) {
  const Expr *Common = E->getCommonExpr();
  const Expr *Operand = E->getOperand();
  const CDestination Awaiter{EDestination::Temporary, nullptr, nullptr, E};
  if (Common)
    Add(Common, Common->isGLValue() ? EUse::Bind : EUse::Copy, 0, Awaiter);
  // The operand is evaluated first, which the awaiter, unless it is it, is
  // made of, and holds.
  if (Operand && Operand != Common)
    Add(Operand, Operand->isGLValue() ? EUse::Bind : EUse::Copy, 0, Awaiter);
  Add(E->getReadyExpr(), EUse::Condition, 1, {});
  Add(E->getSuspendExpr(), EUse::Discarded, 2, {});
  Add(E->getResumeExpr(), EUse::Forward, 3, {});
}

void CEffectsBuilder::of(const CoyieldExpr *E) {
  Result.bSuspends = true;
  suspension(E, [&](const Stmt *S, EUse Use, unsigned Sequence,
                    CDestination Into) { add(S, Use, Sequence, Into); });
}

void CEffectsBuilder::of(const CoawaitExpr *E) {
  Result.bSuspends = true;
  suspension(E, [&](const Stmt *S, EUse Use, unsigned Sequence,
                    CDestination Into) { add(S, Use, Sequence, Into); });
}

void CEffectsBuilder::of(const ConvertVectorExpr *E) {
  add(E->getSrcExpr(), EUse::Value, 0);
}

void CEffectsBuilder::of(const ConceptSpecializationExpr *) {}

void CEffectsBuilder::of(const CompoundLiteralExpr *E) {
  give(E->getInitializer(), {EDestination::Temporary, nullptr, nullptr, E},
       0);
}

void CEffectsBuilder::of(const ChooseExpr *E) {
  add(E->getCond(), EUse::Unevaluated, 0);
  const Expr *Chosen = E->isConditionDependent() ? nullptr : E->getChosenSubExpr();
  for (const Expr *Arm : {E->getLHS(), E->getRHS()})
    add(Arm, Arm == Chosen ? EUse::Forward : EUse::Unevaluated, 0);
}

void CEffectsBuilder::of(const CharacterLiteral *) {}

void CEffectsBuilder::castOperand(const CastExpr *E) {
  const Expr *Operand = E->getSubExpr();
  switch (E->getCastKind()) {
  // A load, also as the bits of another type.
  case CK_LValueToRValue:
  case CK_LValueToRValueBitCast:
    add(Operand, EUse::Access, 0);
    return;
  // The same object or the same value, also as another type, a base, a
  // derived class, another address space, or what is made of it, the
  // constructor or conversion called.
  case CK_NoOp:
  case CK_BitCast:
  case CK_LValueBitCast:
  case CK_BaseToDerived:
  case CK_DerivedToBase:
  case CK_UncheckedDerivedToBase:
  case CK_ToUnion:
  case CK_UserDefinedConversion:
  case CK_ConstructorConversion:
  case CK_AtomicToNonAtomic:
  case CK_NonAtomicToAtomic:
  case CK_AddressSpaceConversion:
  case CK_HLSLArrayRValue:
    add(Operand, EUse::Forward, 0);
    return;
  // The same object, found by the dynamic type of the object, which the
  // check reads, a use of it.
  case CK_Dynamic:
    add(Operand, EUse::Forward, 0);
    add(Operand, EUse::Value, 0);
    return;
  case CK_ArrayToPointerDecay:
  case CK_FunctionToPointerDecay:
    add(Operand, EUse::AddressOf, 0);
    return;
  case CK_ToVoid:
    add(Operand, EUse::Discarded, 0);
    return;
  case CK_PointerToBoolean:
  case CK_MemberPointerToBoolean:
  case CK_IntegralToBoolean:
  case CK_FloatingToBoolean:
  case CK_FixedPointToBoolean:
  case CK_FloatingComplexToBoolean:
  case CK_IntegralComplexToBoolean:
    add(Operand, EUse::Condition, 0);
    return;
  // A value computed from it.
  case CK_NullToPointer:
  case CK_NullToMemberPointer:
  case CK_BaseToDerivedMemberPointer:
  case CK_DerivedToBaseMemberPointer:
  case CK_ReinterpretMemberPointer:
  case CK_IntegralToPointer:
  case CK_PointerToIntegral:
  case CK_MatrixCast:
  case CK_VectorSplat:
  case CK_IntegralCast:
  case CK_IntegralToFloating:
  case CK_FloatingToFixedPoint:
  case CK_FixedPointToFloating:
  case CK_FixedPointCast:
  case CK_FixedPointToIntegral:
  case CK_IntegralToFixedPoint:
  case CK_FloatingToIntegral:
  case CK_BooleanToSignedIntegral:
  case CK_FloatingCast:
  case CK_FloatingRealToComplex:
  case CK_FloatingComplexToReal:
  case CK_FloatingComplexCast:
  case CK_FloatingComplexToIntegralComplex:
  case CK_IntegralRealToComplex:
  case CK_IntegralComplexToReal:
  case CK_IntegralComplexCast:
  case CK_IntegralComplexToFloatingComplex:
  case CK_BuiltinFnToFnPtr:
  case CK_ZeroToOCLOpaqueType:
  case CK_IntToOCLSampler:
  case CK_HLSLVectorTruncation:
  case CK_HLSLMatrixTruncation:
  case CK_HLSLElementwiseCast:
  case CK_HLSLAggregateSplatCast:
    add(Operand, EUse::Value, 0);
    return;
  // What isn't C++, or is dependent.
  case CK_Dependent:
  case CK_CPointerToObjCPointerCast:
  case CK_BlockPointerToObjCPointerCast:
  case CK_AnyPointerToBlockPointerCast:
  case CK_ObjCObjectLValueCast:
  case CK_ARCProduceObject:
  case CK_ARCConsumeObject:
  case CK_ARCReclaimReturnedObject:
  case CK_ARCExtendBlockObject:
  case CK_CopyAndAutoreleaseBlockObject:
    add(Operand, EUse::Opaque, 0);
    return;
  }
}

void CEffectsBuilder::of(const ImplicitCastExpr *E) { castOperand(E); }
void CEffectsBuilder::of(const CXXStaticCastExpr *E) { castOperand(E); }
void CEffectsBuilder::of(const CXXReinterpretCastExpr *E) { castOperand(E); }
void CEffectsBuilder::of(const CXXDynamicCastExpr *E) { castOperand(E); }
void CEffectsBuilder::of(const CXXConstCastExpr *E) { castOperand(E); }
void CEffectsBuilder::of(const CXXAddrspaceCastExpr *E) { castOperand(E); }
void CEffectsBuilder::of(const CXXFunctionalCastExpr *E) { castOperand(E); }
void CEffectsBuilder::of(const CStyleCastExpr *E) { castOperand(E); }
void CEffectsBuilder::of(const BuiltinBitCastExpr *E) { castOperand(E); }

/// The order C++17 gives the operands of an operator called by its notation,
/// as that of the built-in one: of an assignment, the right before the left;
/// of a shift, a subscript, `->*`, a comma, `&&` and `||`, the left before
/// the right; of a call, what is called before the arguments. The others are
/// unsequenced. The sequence of operand \p iOperand.
static unsigned operatorSequence(OverloadedOperatorKind Operator,
                                 unsigned iOperand) {
  switch (Operator) {
  case OO_Equal:
  case OO_PlusEqual:
  case OO_MinusEqual:
  case OO_StarEqual:
  case OO_SlashEqual:
  case OO_PercentEqual:
  case OO_CaretEqual:
  case OO_AmpEqual:
  case OO_PipeEqual:
  case OO_LessLessEqual:
  case OO_GreaterGreaterEqual:
    return iOperand == 0 ? 1 : 0;
  case OO_LessLess:
  case OO_GreaterGreater:
  case OO_Subscript:
  case OO_ArrowStar:
  case OO_Comma:
  case OO_AmpAmp:
  case OO_PipePipe:
  case OO_Call:
    return iOperand == 0 ? 0 : 1;
  case OO_None:
  case OO_New:
  case OO_Delete:
  case OO_Array_New:
  case OO_Array_Delete:
  case OO_Plus:
  case OO_Minus:
  case OO_Star:
  case OO_Slash:
  case OO_Percent:
  case OO_Caret:
  case OO_Amp:
  case OO_Pipe:
  case OO_Tilde:
  case OO_Exclaim:
  case OO_Less:
  case OO_Greater:
  case OO_EqualEqual:
  case OO_ExclaimEqual:
  case OO_LessEqual:
  case OO_GreaterEqual:
  case OO_Spaceship:
  case OO_PlusPlus:
  case OO_MinusMinus:
  case OO_Arrow:
  case OO_Conditional:
  case OO_Coawait:
  case NUM_OVERLOADED_OPERATORS:
    return 0;
  }
  return 0;
}

/// The function \p E calls: directly, or through an unchanged local pointer
/// to a function or a member function, `fStore(p)` of `auto fStore =
/// &fg_Store`, `(Object.*pmSet)(p)` (functionDesignatedBy).
static const FunctionDecl *resolvedCallee(const CallExpr *E) {
  if (const FunctionDecl *Callee = functionCalledBy(E))
    return Callee;
  if (const auto *Member =
          dyn_cast<BinaryOperator>(E->getCallee()->IgnoreParens());
      Member && Member->isPtrMemOp())
    return functionDesignatedBy(Member->getRHS());
  return nullptr;
}

/// Each argument of \p E from \p FirstArgument on, given to the parameter of
/// its callee it initializes; \p Object, when the callee has an explicit
/// object parameter, is the object it is called on as well. Of a builtin that
/// doesn't evaluate its arguments (hasUnevaluatedOperands), none is.
void CEffectsBuilder::call(const CallExpr *E, unsigned FirstArgument,
                           const Expr *Object) {
  Result.Statement = EStatement::Call;
  const FunctionDecl *Callee = resolvedCallee(E);
  Result.Callee = Callee;
  if (hasUnevaluatedOperands(E)) {
    add(E->getCallee(), EUse::Callee, 0);
    for (const Expr *Argument : E->arguments())
      add(Argument, EUse::Unevaluated, 1);
    return;
  }
  // The promise's calls are plumbing: what `co_return` and `co_yield` give
  // the result is decided at them, by the result's type. What the promise is
  // given is read here, where the result is made from it, and kept nowhere;
  // the promise itself is dropped.
  if (isPromiseCall(E)) {
    add(E->getCallee(), EUse::Callee, 0);
    for (const Expr *Argument : E->arguments())
      add(Argument,
          Argument != Object && Argument->isGLValue() ? EUse::Access
                                                      : EUse::Discarded,
          1);
    return;
  }
  const auto *Operator = dyn_cast<CXXOperatorCallExpr>(E);
  // Of an operator member function with an implicit object, the first
  // argument is the object, not a parameter (EUse::CallObject); of a static
  // one, `static operator()`, the object, evaluated and dropped
  // (argumentShift).
  const auto *Method = dyn_cast_or_null<CXXMethodDecl>(Callee);
  const unsigned Shift = argumentShift(E, Callee);
  if (Operator && Method && Method->isStatic() && E->getNumArgs() > 0)
    add(E->getArg(0), EUse::Discarded,
        operatorSequence(Operator->getOperator(), 0));
  add(E->getCallee(), EUse::Callee, 0);
  for (unsigned iArgument = FirstArgument; iArgument < E->getNumArgs();
       ++iArgument) {
    const Expr *Argument = E->getArg(iArgument);
    const unsigned Sequence =
        Operator ? operatorSequence(Operator->getOperator(), iArgument)
                 : 1;
    if (iArgument < Shift)
      continue;
    const unsigned iParameter = iArgument - Shift;
    // An address taken, `std::addressof(Value)`, like `&Value` without its
    // overloads (isAddressOfCall), not a parameter given it.
    if (isAddressOfCall(E)) {
      add(Argument, EUse::AddressOf, Sequence);
      continue;
    }
    const ParmVarDecl *Parameter =
        Callee && iParameter < Callee->getNumParams()
            ? Callee->getParamDecl(iParameter)
            : nullptr;
    const CDestination Into{EDestination::Parameter, Parameter, Callee,
                            nullptr, iParameter};
    // An explicit object bound by reference is the object the member function
    // is called on, as an implicit one is (EUse::CallObject), with the
    // parameter it binds as where it goes; one taken by value is a copy the
    // function makes of it.
    if (Argument == Object && Argument->isGLValue() &&
        (!Parameter || Parameter->getType()->isReferenceType())) {
      add(Argument, EUse::CallObject, Sequence, Into);
      continue;
    }
    give(Argument, Into, Sequence);
    if (Argument == Object)
      add(Argument, EUse::CallObject, Sequence);
  }
}

/// The object an explicit object parameter of the callee of \p E is given,
/// `Data` of `Data.f_Add(1)` with `void f_Add(this CData &, int)`.
static const Expr *explicitObject(const CallExpr *E) {
  const auto *Method = dyn_cast_or_null<CXXMethodDecl>(resolvedCallee(E));
  return Method && Method->isExplicitObjectMemberFunction() &&
                 E->getNumArgs() > 0
             ? E->getArg(0)
             : nullptr;
}

void CEffectsBuilder::of(const CallExpr *E) { call(E, 0, explicitObject(E)); }

void CEffectsBuilder::of(const UserDefinedLiteral *E) { call(E, 0, nullptr); }

void CEffectsBuilder::of(const CXXOperatorCallExpr *E) {
  // The object of an operator member function with an implicit object.
  const auto *Method = dyn_cast_or_null<CXXMethodDecl>(resolvedCallee(E));
  if (Method && Method->isImplicitObjectMemberFunction() &&
      E->getNumArgs() > 0)
    add(E->getArg(0), EUse::CallObject,
        operatorSequence(E->getOperator(), 0));
  call(E, 0, explicitObject(E));
}

// The object, which the member expression called is bound to, is used where
// the call is, after the arguments.
void CEffectsBuilder::of(const CXXMemberCallExpr *E) {
  call(E, 0, nullptr);
  add(E->getImplicitObjectArgument(),
      isPromiseCall(E) ? EUse::Discarded : EUse::CallObject, 0);
}

void CEffectsBuilder::of(const CUDAKernelCallExpr *E) {
  add(E->getConfig(), EUse::Opaque, 0);
  call(E, 0, nullptr);
}

void CEffectsBuilder::of(const CXXUuidofExpr *E) {
  if (!E->isTypeOperand())
    add(E->getExprOperand(), EUse::Unevaluated, 0);
}

void CEffectsBuilder::of(const CXXUnresolvedConstructExpr *E) { opaque(E); }

void CEffectsBuilder::of(const CXXTypeidExpr *E) {
  // Of a glvalue of a polymorphic class, its dynamic type is read.
  if (!E->isTypeOperand())
    add(E->getExprOperand(),
        E->isPotentiallyEvaluated() ? EUse::Access : EUse::Unevaluated, 0);
}

void CEffectsBuilder::of(const CXXThrowExpr *E) {
  Result.bLeaves = true;
  give(E->getSubExpr(), {EDestination::Thrown, nullptr, nullptr, E}, 0);
}

void CEffectsBuilder::of(const CXXThisExpr *) {
  Result.Statement = EStatement::This;
}

void CEffectsBuilder::of(const CXXStdInitializerListExpr *E) {
  // The list refers to the array it is made of.
  Result.Statement = EStatement::InitializerList;
  add(E->getSubExpr(), EUse::Bind, 0,
      {EDestination::Temporary, nullptr, nullptr, E});
}

void CEffectsBuilder::of(const CXXScalarValueInitExpr *) {}

void CEffectsBuilder::of(const CXXRewrittenBinaryOperator *E) {
  add(E->getSemanticForm(), EUse::Forward, 0);
}

void CEffectsBuilder::of(const CXXReflectExpr *) {
  // What it reflects isn't evaluated.
}

void CEffectsBuilder::of(const CXXPseudoDestructorExpr *E) {
  add(E->getBase(), E->isArrow() ? EUse::Dereference : EUse::Discarded, 0);
}

void CEffectsBuilder::of(const CXXParenListInitExpr *E) {
  llvm::SmallVector<const Expr *, 8> Inits(E->getInitExprs());
  initializers(E, Inits, E->getArrayFiller(), E->getInitializedFieldInUnion());
}

void CEffectsBuilder::of(const CXXNullPtrLiteralExpr *) {}

void CEffectsBuilder::of(const CXXNoexceptExpr *E) {
  add(E->getOperand(), EUse::Unevaluated, 0);
}

void CEffectsBuilder::of(const CXXNewExpr *E) {
  Result.Statement = EStatement::New;
  // The size and the placement arguments, given to the allocation function,
  // then what is made, on the heap.
  if (const std::optional<const Expr *> Size = E->getArraySize())
    add(*Size, EUse::Value, 0);
  const FunctionDecl *Allocator = E->getOperatorNew();
  for (unsigned iArgument = 0; iArgument < E->getNumPlacementArgs();
       ++iArgument) {
    const unsigned iParameter = iArgument + 1;
    give(E->getPlacementArg(iArgument),
         {EDestination::Parameter,
          Allocator && iParameter < Allocator->getNumParams()
              ? Allocator->getParamDecl(iParameter)
              : nullptr,
          Allocator, nullptr, iParameter},
         0);
  }
  if (const Expr *Init = E->getInitializer())
    give(Init, {EDestination::Heap, nullptr, nullptr, E}, 1);
}

void CEffectsBuilder::of(const CXXInheritedCtorInitExpr *) {
  // The constructor inherited, given the parameters of the one it is in as
  // they are (storesParameter), which aren't expressions.
  Result.Statement = EStatement::InheritedConstruction;
}
void CEffectsBuilder::of(const CXXFoldExpr *E) { opaque(E); }

void CEffectsBuilder::of(const CXXExpansionSelectExpr *E) {
  add(E->getRangeExpr(), EUse::Forward, 0);
  add(E->getIndexExpr(), EUse::Value, 0);
}

void CEffectsBuilder::of(const CXXDependentScopeMemberExpr *E) { opaque(E); }

void CEffectsBuilder::of(const CXXDeleteExpr *E) {
  add(E->getArgument(), EUse::Dereference, 0);
}

void CEffectsBuilder::of(const CXXDefaultInitExpr *E) {
  // The default member initializer, evaluated where it is used; shared by
  // each of its uses, not a child.
  Result.Statement = EStatement::Default;
  add(E->getExpr(), EUse::Forward, 0);
}

void CEffectsBuilder::of(const CXXDefaultArgExpr *E) {
  // The default argument, evaluated where it is used; shared by each of its
  // uses, not a child.
  Result.Statement = EStatement::Default;
  add(E->getExpr(), EUse::Forward, 0);
}

void CEffectsBuilder::of(const CXXConstructExpr *E) {
  Result.Statement = EStatement::Construction;
  // Each argument given to the constructor; in braces, in order.
  const CXXConstructorDecl *Constructor = E->getConstructor();
  for (unsigned iArgument = 0; iArgument < E->getNumArgs(); ++iArgument)
    give(E->getArg(iArgument),
         {EDestination::Parameter,
          Constructor && iArgument < Constructor->getNumParams()
              ? Constructor->getParamDecl(iArgument)
              : nullptr,
          Constructor, nullptr, iArgument},
         E->isListInitialization() ? iArgument : 0);
}

void CEffectsBuilder::of(const CXXTemporaryObjectExpr *E) {
  if (stackNearlyExhausted())
    return withStack([&] { of(E); });
  of(static_cast<const CXXConstructExpr *>(E));
}

void CEffectsBuilder::of(const CXXBoolLiteralExpr *) {}

void CEffectsBuilder::of(const CXXBindTemporaryExpr *E) {
  add(E->getSubExpr(), EUse::Forward, 0);
}

void CEffectsBuilder::of(const BlockExpr *E) { opaque(E); }

void CEffectsBuilder::of(const BinaryOperator *E) {
  const Expr *Left = E->getLHS();
  const Expr *Right = E->getRHS();
  // Of a pointer to a member function, what it is called on, bound, which
  // the call uses (CXXMemberCallExpr).
  const bool Called =
      E->getType()->isSpecificPlaceholderType(BuiltinType::BoundMember);
  switch (E->getOpcode()) {
  case BO_PtrMemD:
    add(Left, EUse::Forward, 0);
    add(Right, EUse::Value, 1);
    return;
  case BO_PtrMemI:
    add(Left, Called ? EUse::Forward : EUse::Dereference, 0);
    add(Right, EUse::Value, 1);
    return;
  // Pointer arithmetic gives a pointer into what the pointer points to.
  case BO_Add:
  case BO_Sub:
    add(Left,
        Left->getType()->isPointerType() && E->getType()->isPointerType()
            ? EUse::Forward
            : EUse::Value,
        0);
    add(Right,
        Right->getType()->isPointerType() && E->getType()->isPointerType()
            ? EUse::Forward
            : EUse::Value,
        0);
    return;
  case BO_Shl:
  case BO_Shr:
    add(Left, EUse::Value, 0);
    add(Right, EUse::Value, 1);
    return;
  case BO_Mul:
  case BO_Div:
  case BO_Rem:
  case BO_Cmp:
  case BO_LT:
  case BO_GT:
  case BO_LE:
  case BO_GE:
  case BO_EQ:
  case BO_NE:
  case BO_And:
  case BO_Xor:
  case BO_Or:
    add(Left, EUse::Value, 0);
    add(Right, EUse::Value, 0);
    return;
  // Of a constant left, the right runs only when it doesn't decide.
  case BO_LAnd:
  case BO_LOr: {
    add(Left, EUse::Condition, 0);
    bool Value = false;
    const bool Skipped = !Left->isValueDependent() &&
                         !Left->HasSideEffects(Context) &&
                         foldsCondition(Left, Value, Context) &&
                         Value == (E->getOpcode() == BO_LOr);
    add(Right, Skipped ? EUse::Unevaluated : EUse::Condition, 1);
    return;
  }
  // The right is given to what the left designates, which is given back.
  case BO_Assign:
    give(Right, {EDestination::Designated, nullptr, nullptr, Left}, 0);
    add(Left, EUse::Write, 1);
    add(Left, EUse::Forward, 1);
    return;
  case BO_MulAssign:
  case BO_DivAssign:
  case BO_RemAssign:
  case BO_AddAssign:
  case BO_SubAssign:
  case BO_ShlAssign:
  case BO_ShrAssign:
  case BO_AndAssign:
  case BO_XorAssign:
  case BO_OrAssign:
    add(Right, EUse::Value, 0);
    add(Left, EUse::Update, 1);
    add(Left, EUse::Forward, 1);
    return;
  case BO_Comma:
    add(Left, EUse::Discarded, 0);
    add(Right, EUse::Forward, 1);
    return;
  }
}

void CEffectsBuilder::of(const CompoundAssignOperator *E) {
  if (stackNearlyExhausted())
    return withStack([&] { of(E); });
  of(static_cast<const BinaryOperator *>(E));
}

void CEffectsBuilder::of(const AtomicExpr *E) {
  // What the pointer points to is read and changed; the rest are values, or
  // pointers to them.
  for (unsigned iExpr = 0; iExpr < E->getNumSubExprs(); ++iExpr) {
    const Expr *Operand = E->getSubExprs()[iExpr];
    add(Operand,
        Operand == E->getPtr() || Operand->getType()->isPointerType()
            ? EUse::Dereference
            : EUse::Value,
        0);
  }
}

void CEffectsBuilder::of(const AsTypeExpr *E) {
  add(E->getSrcExpr(), EUse::Value, 0);
}

void CEffectsBuilder::of(const ArrayTypeTraitExpr *E) {
  add(E->getDimensionExpression(), EUse::Unevaluated, 0);
}

void CEffectsBuilder::of(const ArraySubscriptExpr *E) {
  // The pointer, which can be either side, `1[p]`; the left is evaluated
  // first.
  for (const Expr *Side : {E->getLHS(), E->getRHS()})
    add(Side, Side == E->getBase() ? EUse::Dereference : EUse::Value,
        Side == E->getLHS() ? 0 : 1);
}

void CEffectsBuilder::of(const ArraySectionExpr *E) { opaque(E); }

void CEffectsBuilder::of(const ArrayInitLoopExpr *E) {
  // The array the common expression stands for, evaluated once, then each
  // element of what is made, by the subexpression, which reads an element of
  // that, by the index, or constructs one from it.
  if (const OpaqueValueExpr *Common = E->getCommonExpr())
    add(Common->getSourceExpr(), EUse::Discarded, 0);
  add(E->getCommonExpr(), EUse::Unevaluated, 0);
  give(E->getSubExpr(), {EDestination::Element, nullptr, nullptr, E}, 1);
}

void CEffectsBuilder::of(const ArrayInitIndexExpr *) {}
void CEffectsBuilder::of(const AddrLabelExpr *) {}

void CEffectsBuilder::of(const ConditionalOperator *E) {
  add(E->getCond(), EUse::Condition, 0);
  // Of a constant condition, only the value it chooses runs.
  bool Taken = false;
  const bool Constant = !E->getCond()->isValueDependent() &&
                        !E->getCond()->HasSideEffects(Context) &&
                        foldsCondition(E->getCond(), Taken, Context);
  add(E->getTrueExpr(),
      Constant && !Taken ? EUse::Unevaluated : EUse::Forward, 1);
  add(E->getFalseExpr(),
      Constant && Taken ? EUse::Unevaluated : EUse::Forward, 1);
}

void CEffectsBuilder::of(const BinaryConditionalOperator *E) {
  // `x ?: y`: x is the condition and, when it holds, the result; the opaque
  // values stand for it.
  // Of a constant one, only the value it chooses runs.
  bool Taken = false;
  const bool Constant = !E->getCommon()->isValueDependent() &&
                        !E->getCommon()->HasSideEffects(Context) &&
                        foldsCondition(E->getCommon(), Taken, Context);
  add(E->getCommon(), EUse::Condition, 0);
  add(E->getCommon(), Constant && !Taken ? EUse::Discarded : EUse::Forward,
      0);
  add(E->getOpaqueValue(), EUse::Unevaluated, 0);
  add(E->getCond(), EUse::Unevaluated, 0);
  add(E->getTrueExpr(), EUse::Unevaluated, 0);
  add(E->getFalseExpr(), Constant && Taken ? EUse::Unevaluated : EUse::Forward,
      1);
}

} // namespace

/// What effectsOf tells of \p S, worked out each time.
static CEffects effectsOfUncached(const Stmt *S, const ASTContext &Context) {
  CEffects Result;
  if (!S)
    return Result;
  CEffectsBuilder Builder(Context, Result);
  switch (S->getStmtClass()) {
  case Stmt::NoStmtClass:
    break;
#define ABSTRACT_STMT(Type)
#define STMT(Type, Base)                                                       \
  case Stmt::Type##Class:                                                      \
    Builder.of(cast<Type>(S));                                                 \
    break;
#include "clang/AST/StmtNodes.inc"
  }
  return Result;
}

bool isPromiseCall(const CallExpr *Call) {
  const auto *Method =
      Call ? dyn_cast_or_null<CXXMethodDecl>(Call->getDirectCallee()) : nullptr;
  if (!Method || !Method->getIdentifier() ||
      !llvm::is_contained({"return_value", "return_void", "yield_value"},
                          identifierTextOf(Method)))
    return false;
  const Expr *Object = nullptr;
  if (const auto *Member = dyn_cast<CXXMemberCallExpr>(Call))
    Object = Member->getImplicitObjectArgument();
  else if (Method->isExplicitObjectMemberFunction() && Call->getNumArgs() > 0)
    Object = Call->getArg(0);
  // The promise is the coroutine's implicit `__promise`.
  const auto *Ref =
      Object ? dyn_cast<DeclRefExpr>(Object->IgnoreParenImpCasts()) : nullptr;
  const auto *Promise = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
  return Promise && Promise->isImplicit() && Promise->getIdentifier() &&
         identifierTextOf(Promise) == "__promise";
}

namespace {
// The effects of the statements of a translation unit, each made once, which
// the checks ask of many times.
struct CEffectsCache {
  uint64_t AST = 0;
  llvm::DenseMap<const Stmt *, std::unique_ptr<CEffects>> Of;
};
thread_local CEffectsCache s_EffectsCache;
} // namespace

const CEffects &keptEffectsOf(const Stmt *S) {
  return *s_EffectsCache.Of.find(S)->second;
}

const CEffects &effectsOf(const Stmt *S, const ASTContext &Context) {
  CEffectsCache &Cache = s_EffectsCache;
  if (const uint64_t AST = astIdentity(Context); Cache.AST != AST) {
    Cache.AST = AST;
    Cache.Of.clear();
  }
  if (const auto It = Cache.Of.find(S); It != Cache.Of.end())
    return *It->second;
  // Made before it is added: what makes it can ask of others. Kept with no
  // more room than its operands take.
  auto Made = std::make_unique<CEffects>(effectsOfUncached(S, Context));
  if (Made->Operands.capacity() > std::max<size_t>(Made->Operands.size(), 1)) {
    llvm::SmallVector<COperandEffect, 1> Exact;
    Exact.reserve(Made->Operands.size());
    Exact.append(Made->Operands.begin(), Made->Operands.end());
    Made->Operands = std::move(Exact);
  }
  std::unique_ptr<CEffects> &Cached = Cache.Of[S];
  if (!Cached)
    Cached = std::move(Made);
  return *Cached;
}

bool usesOnlyPointerValue(const Stmt *User, const Expr *Operand,
                          const ASTContext &Context) {
  if (!User || !Operand || !Operand->getType()->isPointerType())
    return false;
  const Expr *Pointer = Operand->IgnoreParenImpCasts();
  bool Found = false;
  for (const COperandEffect &Effect : effectsOf(User, Context).Operands) {
    const auto *Of = dyn_cast_or_null<Expr>(Effect.Operand);
    if (!Of || Of->IgnoreParenImpCasts() != Pointer)
      continue;
    if (Effect.Use != EUse::Value && Effect.Use != EUse::Condition &&
        Effect.Use != EUse::Discarded)
      return false;
    Found = true;
  }
  return Found;
}

const Expr *sameValue(const Expr *E, const ASTContext &Context) {
  for (int Depth = 0; E && Depth < 32; ++Depth) {
    const CEffects &Of = effectsOf(E, Context);
    const auto *Inner = Of.Operands.size() == 1 &&
                                Of.Operands.front().Use == EUse::Forward
                            ? dyn_cast<Expr>(Of.Operands.front().Operand)
                            : nullptr;
    if (!Inner || Inner->isGLValue() != E->isGLValue() ||
        Inner->getType().getCanonicalType().getUnqualifiedType() !=
            E->getType().getCanonicalType().getUnqualifiedType())
      return E;
    E = Inner;
  }
  return E;
}

const ValueDecl *namedDecl(const Expr *E, const ASTContext &Context) {
  const auto *Ref = dyn_cast_or_null<DeclRefExpr>(sameValue(E, Context));
  return Ref ? Ref->getDecl() : nullptr;
}

const ValueDecl *memberDecl(const Expr *E, const ASTContext &Context) {
  const CEffects &Of = effectsOf(E, Context);
  return Of.Statement == EStatement::Member
             ? cast<MemberExpr>(E)->getMemberDecl()
             : nullptr;
}

const FunctionDecl *callTarget(const Expr *E, const ASTContext &Context) {
  const CEffects &Of = effectsOf(E, Context);
  return Of.Statement == EStatement::Call ? Of.Callee : nullptr;
}

const FunctionDecl *calledFunction(const Expr *E, const ASTContext &Context) {
  const CEffects &Of = effectsOf(E, Context);
  return Of.Statement == EStatement::Call
             ? cast<CallExpr>(E)->getDirectCallee()
             : nullptr;
}

SourceLocation usedLocation(const Stmt *Default) {
  if (const auto *Argument = dyn_cast_or_null<CXXDefaultArgExpr>(Default))
    return Argument->getUsedLocation();
  if (const auto *Initializer = dyn_cast_or_null<CXXDefaultInitExpr>(Default))
    return Initializer->getUsedLocation();
  return Default ? Default->getBeginLoc() : SourceLocation();
}

const Expr *callObject(const Expr *E, const ASTContext &Context) {
  for (const COperandEffect &Effect : effectsOf(E, Context).Operands)
    if (Effect.Use == EUse::CallObject)
      return dyn_cast<Expr>(Effect.Operand);
  return nullptr;
}

const Expr *argumentFor(const Stmt *Call, unsigned iParameter,
                        const ASTContext &Context) {
  for (const COperandEffect &Effect : effectsOf(Call, Context).Operands)
    if ((Effect.Use == EUse::Copy || Effect.Use == EUse::Bind ||
         Effect.Use == EUse::CallObject) &&
        Effect.Into.Kind == EDestination::Parameter &&
        Effect.Into.Index == iParameter)
      return dyn_cast<Expr>(Effect.Operand);
  return nullptr;
}

llvm::StringRef useName(EUse Use) {
  switch (Use) {
  case EUse::Forward:
    return "Forward";
  case EUse::Access:
    return "Access";
  case EUse::Update:
    return "Update";
  case EUse::Write:
    return "Write";
  case EUse::Dereference:
    return "Dereference";
  case EUse::AddressOf:
    return "AddressOf";
  case EUse::Bind:
    return "Bind";
  case EUse::Copy:
    return "Copy";
  case EUse::CallObject:
    return "CallObject";
  case EUse::Callee:
    return "Callee";
  case EUse::Condition:
    return "Condition";
  case EUse::Value:
    return "Value";
  case EUse::Discarded:
    return "Discarded";
  case EUse::Statement:
    return "Statement";
  case EUse::Unevaluated:
    return "Unevaluated";
  case EUse::Opaque:
    return "Opaque";
  }
  return "";
}

llvm::StringRef destinationName(EDestination Kind) {
  switch (Kind) {
  case EDestination::None:
    return "None";
  case EDestination::Variable:
    return "Variable";
  case EDestination::Parameter:
    return "Parameter";
  case EDestination::Field:
    return "Field";
  case EDestination::Designated:
    return "Designated";
  case EDestination::Capture:
    return "Capture";
  case EDestination::Element:
    return "Element";
  case EDestination::Base:
    return "Base";
  case EDestination::Return:
    return "Return";
  case EDestination::Heap:
    return "Heap";
  case EDestination::Temporary:
    return "Temporary";
  case EDestination::Thrown:
    return "Thrown";
  }
  return "";
}

} // namespace clang::tidy::malterlib
