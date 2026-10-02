//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CheckedAccess.h"
#include "llvm/Support/MathExtras.h"
#include <algorithm>

namespace clang::tidy::malterlib {

std::optional<int64_t> signedValueOf(const llvm::APSInt &Value) {
  return Value.tryExtValue();
}

std::optional<uint64_t> indexValueOf(const llvm::APSInt &Value) {
  if (Value.isNegative() || Value.getActiveBits() > 64)
    return std::nullopt;
  return Value.getZExtValue();
}

bool haveBitInCommon(const llvm::APSInt &Left, const llvm::APSInt &Right) {
  const unsigned Width = std::max(Left.getBitWidth(), Right.getBitWidth());
  return Left.extend(Width).intersects(Right.extend(Width));
}

llvm::APInt unionOfBits(const llvm::APInt &Left, const llvm::APInt &Right) {
  const unsigned Width = std::max(Left.getBitWidth(), Right.getBitWidth());
  return Left.zext(Width) | Right.zext(Width);
}

llvm::APInt commonBitsOf(const llvm::APInt &Left, const llvm::APInt &Right) {
  const unsigned Width = std::max(Left.getBitWidth(), Right.getBitWidth());
  return Left.zext(Width) & Right.zext(Width);
}

llvm::APSInt integerLiteralValueOf(const IntegerLiteral *Literal) {
  return llvm::APSInt(Literal->getValue(),
                      Literal->getType()->isUnsignedIntegerOrEnumerationType());
}

std::optional<int64_t> checkedAdd(int64_t Left, int64_t Right) {
  int64_t Result = 0;
  if (llvm::AddOverflow(Left, Right, Result))
    return std::nullopt;
  return Result;
}

std::optional<int64_t> checkedSub(int64_t Left, int64_t Right) {
  int64_t Result = 0;
  if (llvm::SubOverflow(Left, Right, Result))
    return std::nullopt;
  return Result;
}

std::optional<int64_t> checkedNegate(int64_t Value) {
  return checkedSub(0, Value);
}

std::optional<StringRef> literalTextOf(const Expr *E) {
  const auto *Literal =
      E ? dyn_cast<StringLiteral>(E->IgnoreParenImpCasts()) : nullptr;
  if (!Literal || !(Literal->isOrdinary() || Literal->isUTF8()))
    return std::nullopt;
  return Literal->getString();
}

const Expr *captureInitAt(const LambdaExpr *Lambda, size_t Index) {
  if (!Lambda || Index >= Lambda->capture_size())
    return nullptr;
  const Expr *Init = Lambda->capture_init_begin()[Index];
  // In a template, the initializer of a capture of what depends on its
  // parameters is a ParenListExpr without a type.
  if (!Init || Init->getType().isNull() || Init->isTypeDependent())
    return nullptr;
  return Init;
}

const Expr *captureInitOf(const LambdaExpr *Lambda,
                          const LambdaCapture &Capture) {
  if (!Lambda || &Capture < Lambda->capture_begin() ||
      &Capture >= Lambda->capture_end())
    return nullptr;
  return captureInitAt(Lambda, &Capture - Lambda->capture_begin());
}

llvm::SmallVector<const Expr *, 4> captureInitsOf(const LambdaExpr *Lambda) {
  llvm::SmallVector<const Expr *, 4> Inits;
  if (!Lambda)
    return Inits;
  for (size_t Index = 0; Index < Lambda->capture_size(); ++Index)
    Inits.push_back(captureInitAt(Lambda, Index));
  return Inits;
}

const VarDecl *capturedVariableOf(const LambdaCapture &Capture) {
  return Capture.capturesVariable()
             ? dyn_cast<VarDecl>(Capture.getCapturedVar())
             : nullptr;
}

const Expr *initializerOf(const VarDecl *Variable) {
  const Expr *Init = Variable ? Variable->getInit() : nullptr;
  return Init && !Init->getType().isNull() ? Init : nullptr;
}

const Expr *initializerOf(const CXXCtorInitializer *Initializer) {
  const Expr *Init = Initializer ? Initializer->getInit() : nullptr;
  return Init && !Init->getType().isNull() ? Init : nullptr;
}

static bool runsNothing(const Expr *E);

bool isEvaluable(const Expr *E) {
  return E && !E->isValueDependent() && !E->containsErrors();
}

std::optional<llvm::APSInt> constantIntegerOf(const Expr *E,
                                              const ASTContext &Context) {
  if (!isEvaluable(E) || !runsNothing(E))
    return std::nullopt;
  Expr::EvalResult Result;
  if (!E->EvaluateAsInt(Result, Context))
    return std::nullopt;
  return Result.Val.getInt();
}

std::optional<llvm::APSInt> integerConstantExprOf(const Expr *E,
                                                  const ASTContext &Context) {
  if (!isEvaluable(E) || !runsNothing(E))
    return std::nullopt;
  return E->getIntegerConstantExpr(Context);
}

std::optional<APValue> foldedValueOf(const Expr *E, const ASTContext &Context) {
  if (!isEvaluable(E) || !runsNothing(E))
    return std::nullopt;
  Expr::EvalResult Result;
  if (!E->EvaluateAsRValue(Result, Context) || Result.HasSideEffects)
    return std::nullopt;
  return Result.Val;
}

Expr::NullPointerConstantKind
nullPointerConstantOf(const Expr *E, ASTContext &Context,
                      Expr::NullPointerConstantValueDependence Dependence) {
  const LangOptions &Language = Context.getLangOpts();
  if ((!Language.CPlusPlus11 || Language.MSVCCompat) &&
      !E->isValueDependent() && !E->getType()->isNullPtrType() &&
      !runsNothing(E))
    return Expr::NPCK_NotNull;
  return E->isNullPointerConstant(Context, Dependence);
}

std::optional<const Stmt *> nondiscardedCaseOf(const IfStmt *If) {
  if (!If->isConstexpr())
    return std::nullopt;
  const auto *Condition = dyn_cast<ConstantExpr>(If->getCond());
  if (!Condition || Condition->isValueDependent() ||
      Condition->getResultAPValueKind() != APValue::Int)
    return std::nullopt;
  return Condition->getResultAsAPSInt().isZero() ? If->getElse()
                                                 : If->getThen();
}

std::optional<Stmt *> nondiscardedCaseOf(IfStmt *If) {
  const std::optional<const Stmt *> Case =
      nondiscardedCaseOf(static_cast<const IfStmt *>(If));
  if (!Case)
    return std::nullopt;
  return *Case == If->getThen() ? If->getThen() : If->getElse();
}

QualType pointeeOf(QualType Type) {
  if (Type.isNull())
    return QualType();
  return Type->getPointeeType();
}

bool pointsToConst(QualType Type) {
  const QualType Pointee = pointeeOf(Type);
  return !Pointee.isNull() && Pointee.isConstQualified();
}

QualType typeArgumentOf(const TemplateArgument &Argument) {
  return Argument.getKind() == TemplateArgument::Type ? Argument.getAsType()
                                                      : QualType();
}

const clang::Type *qualifierTypeOf(NestedNameSpecifier Qualifier) {
  switch (Qualifier.getKind()) {
  case NestedNameSpecifier::Kind::Type:
    return Qualifier.getAsType();
  case NestedNameSpecifier::Kind::Null:
  case NestedNameSpecifier::Kind::Global:
  case NestedNameSpecifier::Kind::Namespace:
  case NestedNameSpecifier::Kind::MicrosoftSuper:
    return nullptr;
  }
  llvm_unreachable("a qualifier of a kind clang doesn't have");
}

llvm::ArrayRef<CXXBaseSpecifier> basesOf(const CXXRecordDecl *Record) {
  if (!Record || !Record->hasDefinition())
    return {};
  return {Record->bases_begin(), Record->bases_end()};
}

StringRef identifierTextOf(const NamedDecl *Declaration) {
  if (!Declaration)
    return StringRef();
  if (const IdentifierInfo *Identifier = Declaration->getIdentifier())
    return Identifier->getName();
  return StringRef();
}

/// Whether \p Declaration is a constant clang stored the value of: an
/// enumerator, or a const variable, not a reference, whose initializer clang
/// evaluated where it declared it, which reading it then reads.
static bool hasStoredValue(const ValueDecl *Declaration) {
  if (isa<EnumConstantDecl>(Declaration))
    return true;
  const auto *Variable = dyn_cast<VarDecl>(Declaration);
  if (!Variable || isa<ParmVarDecl>(Variable) ||
      Variable->getType()->isReferenceType() ||
      !Variable->getType().isConstQualified() ||
      Variable->getType().isVolatileQualified())
    return false;
  const VarDecl *Initialized = nullptr;
  return Variable->getAnyInitializer(Initialized) && Initialized &&
         Initialized->getEvaluatedValue();
}

static bool runsNothing(const Expr *E, const CXXConstructorDecl *Within,
                        unsigned Depth);

/// Whether constructing with \p Constructor runs nothing but what its
/// arguments run: a constexpr constructor whose body is empty and whose
/// initializers of bases and members are written with nothing to run but
/// reading its parameters, `CInt(umint _Value) : m_Lower(_Value) {}`, also of
/// members whose classes construct so, through at most 8 of them.
static bool constructsWithNothingToRun(const CXXConstructorDecl *Constructor,
                                       unsigned Depth) {
  const FunctionDecl *Defined = nullptr;
  if (Depth >= 8 || !Constructor->isConstexpr() ||
      !Constructor->isDefined(Defined))
    return false;
  const auto *Definition = dyn_cast<CXXConstructorDecl>(Defined);
  const auto *Body = Definition
                         ? dyn_cast_or_null<CompoundStmt>(Definition->getBody())
                         : nullptr;
  if (!Body || !Body->body_empty())
    return false;
  return llvm::all_of(
      Definition->inits(), [&](const CXXCtorInitializer *Initializer) {
        return runsNothing(initializerOf(Initializer), Definition, Depth + 1);
      });
}

/// Whether \p E is written with nothing to run, so that evaluating it can't
/// run what clang never ran: literals, the built-in operators and casts over
/// them, type traits, `sizeof`, the value of a template argument, a concept-id
/// or requires expression clang decided, a constant clang stored the value
/// of, also named, an enumerator or `c_Value` of `constexpr int c_Value = 1`,
/// a construction that runs nothing but what its arguments run
/// (constructsWithNothingToRun), `CInt(1)`, and an aggregate of such. Not a
/// call, also of an operator or a conversion a class declares, another
/// construction nor another variable, whose evaluation can run any constexpr
/// function and instantiate templates. Within the initializers of the
/// constructor \p Within, also its parameters.
static bool runsNothing(const Expr *E, const CXXConstructorDecl *Within,
                        unsigned Depth) {
  if (!E)
    return false;
  auto Runs = [&](const Expr *Part) {
    return runsNothing(Part, Within, Depth);
  };
  switch (E->getStmtClass()) {
  case Stmt::CXXBoolLiteralExprClass:
  case Stmt::IntegerLiteralClass:
  case Stmt::CharacterLiteralClass:
  case Stmt::FloatingLiteralClass:
  case Stmt::FixedPointLiteralClass:
  case Stmt::ImaginaryLiteralClass:
  case Stmt::CXXNullPtrLiteralExprClass:
  case Stmt::GNUNullExprClass:
  case Stmt::StringLiteralClass:
  case Stmt::TypeTraitExprClass:
  case Stmt::ArrayTypeTraitExprClass:
  case Stmt::ExpressionTraitExprClass:
  case Stmt::CXXNoexceptExprClass:
  case Stmt::UnaryExprOrTypeTraitExprClass:
  case Stmt::SizeOfPackExprClass:
  case Stmt::CXXScalarValueInitExprClass:
  case Stmt::ImplicitValueInitExprClass:
    return true;
  // What clang decided when it made it.
  case Stmt::ConceptSpecializationExprClass:
  case Stmt::RequiresExprClass:
    return !E->isValueDependent();
  case Stmt::ConstantExprClass: {
    const auto *Constant = cast<ConstantExpr>(E);
    return Constant->hasAPValueResult() || Runs(Constant->getSubExpr());
  }
  case Stmt::SubstNonTypeTemplateParmExprClass:
    return Runs(cast<SubstNonTypeTemplateParmExpr>(E)->getReplacement());
  case Stmt::DeclRefExprClass: {
    const ValueDecl *Named = cast<DeclRefExpr>(E)->getDecl();
    if (const auto *Parameter = dyn_cast<ParmVarDecl>(Named))
      return Within && Parameter->getDeclContext() == Within;
    return hasStoredValue(Named);
  }
  // A copy that runs no constructor of the class, `CInt(c_One)`, and a
  // construction by one that runs nothing.
  case Stmt::CXXConstructExprClass:
  case Stmt::CXXTemporaryObjectExprClass: {
    const auto *Construct = cast<CXXConstructExpr>(E);
    const CXXConstructorDecl *Constructor = Construct->getConstructor();
    return !Construct->isValueDependent() &&
           (Constructor->isTrivial() ||
            constructsWithNothingToRun(Constructor, Depth)) &&
           llvm::all_of(Construct->arguments(), Runs);
  }
  case Stmt::CXXDefaultArgExprClass:
    return Runs(cast<CXXDefaultArgExpr>(E)->getExpr());
  case Stmt::CXXDefaultInitExprClass:
    return Runs(cast<CXXDefaultInitExpr>(E)->getExpr());
  case Stmt::InitListExprClass: {
    const auto *List = cast<InitListExpr>(E);
    const InitListExpr *Semantic =
        List->isSemanticForm() ? List : List->getSemanticForm();
    return Semantic && llvm::all_of(Semantic->inits(), Runs) &&
           (!Semantic->hasArrayFiller() || Runs(Semantic->getArrayFiller()));
  }
  case Stmt::MaterializeTemporaryExprClass:
    return Runs(cast<MaterializeTemporaryExpr>(E)->getSubExpr());
  case Stmt::ExprWithCleanupsClass:
    return Runs(cast<ExprWithCleanups>(E)->getSubExpr());
  case Stmt::ParenExprClass:
    return Runs(cast<ParenExpr>(E)->getSubExpr());
  case Stmt::UnaryOperatorClass:
    return Runs(cast<UnaryOperator>(E)->getSubExpr());
  case Stmt::BinaryOperatorClass: {
    const auto *Binary = cast<BinaryOperator>(E);
    return Runs(Binary->getLHS()) && Runs(Binary->getRHS());
  }
  case Stmt::ConditionalOperatorClass: {
    const auto *Conditional = cast<ConditionalOperator>(E);
    return Runs(Conditional->getCond()) && Runs(Conditional->getTrueExpr()) &&
           Runs(Conditional->getFalseExpr());
  }
  case Stmt::ImplicitCastExprClass:
  case Stmt::CStyleCastExprClass:
  case Stmt::CXXStaticCastExprClass:
  case Stmt::CXXFunctionalCastExprClass: {
    // A construction written as a cast, `CInt(1)`, is told by the
    // construction it holds.
    const auto *Cast = cast<CastExpr>(E);
    return (Cast->getCastKind() == CK_ConstructorConversion ||
            !Cast->getConversionFunction()) &&
           Runs(Cast->getSubExpr());
  }
  case Stmt::NoStmtClass:
  case Stmt::CompoundAssignOperatorClass:
#define ABSTRACT_STMT(Type)
#define STMT(Type, Base) case Stmt::Type##Class:
#define CXXBOOLLITERALEXPR(Type, Base)
#define INTEGERLITERAL(Type, Base)
#define CHARACTERLITERAL(Type, Base)
#define FLOATINGLITERAL(Type, Base)
#define FIXEDPOINTLITERAL(Type, Base)
#define IMAGINARYLITERAL(Type, Base)
#define CXXNULLPTRLITERALEXPR(Type, Base)
#define GNUNULLEXPR(Type, Base)
#define STRINGLITERAL(Type, Base)
#define TYPETRAITEXPR(Type, Base)
#define ARRAYTYPETRAITEXPR(Type, Base)
#define EXPRESSIONTRAITEXPR(Type, Base)
#define CXXNOEXCEPTEXPR(Type, Base)
#define UNARYEXPRORTYPETRAITEXPR(Type, Base)
#define SIZEOFPACKEXPR(Type, Base)
#define CXXSCALARVALUEINITEXPR(Type, Base)
#define IMPLICITVALUEINITEXPR(Type, Base)
#define CONCEPTSPECIALIZATIONEXPR(Type, Base)
#define REQUIRESEXPR(Type, Base)
#define CONSTANTEXPR(Type, Base)
#define SUBSTNONTYPETEMPLATEPARMEXPR(Type, Base)
#define DECLREFEXPR(Type, Base)
#define CXXCONSTRUCTEXPR(Type, Base)
#define CXXDEFAULTARGEXPR(Type, Base)
#define CXXDEFAULTINITEXPR(Type, Base)
#define INITLISTEXPR(Type, Base)
#define MATERIALIZETEMPORARYEXPR(Type, Base)
#define EXPRWITHCLEANUPS(Type, Base)
#define PARENEXPR(Type, Base)
#define UNARYOPERATOR(Type, Base)
#define BINARYOPERATOR(Type, Base)
#define CONDITIONALOPERATOR(Type, Base)
#define IMPLICITCASTEXPR(Type, Base)
#define CSTYLECASTEXPR(Type, Base)
#define CXXSTATICCASTEXPR(Type, Base)
#define CXXFUNCTIONALCASTEXPR(Type, Base)
#include "clang/AST/StmtNodes.inc"
    // A compound assignment, which changes a variable, and every other
    // kind: a call, a variable, a statement.
    return false;
  }
  llvm_unreachable("a statement of a kind clang doesn't have");
}

static bool runsNothing(const Expr *E) { return runsNothing(E, nullptr, 0); }

std::optional<bool> foldedConditionOf(const Expr *E,
                                      const ASTContext &Context) {
  if (!isEvaluable(E) || !runsNothing(E))
    return std::nullopt;
  bool Holds = false;
  if (!E->EvaluateAsBooleanCondition(Holds, Context))
    return std::nullopt;
  return Holds;
}

bool foldsCondition(const Expr *E, bool &Holds, const ASTContext &Context) {
  const std::optional<bool> Folded = foldedConditionOf(E, Context);
  if (Folded)
    Holds = *Folded;
  return Folded.has_value();
}

std::optional<llvm::APSInt> foldedIntegerOf(const Expr *E,
                                            const ASTContext &Context) {
  if (!runsNothing(E))
    return std::nullopt;
  return integerConstantExprOf(E, Context);
}

std::optional<llvm::APSInt> constantValueOf(const VarDecl *Variable,
                                            const ASTContext &Context) {
  if (const APValue *Value = Variable->getEvaluatedValue();
      Value && Value->isInt())
    return Value->getInt();
  return foldedIntegerOf(Variable->getInit(), Context);
}

std::optional<llvm::APSInt>
templateArgumentValueOf(const TemplateArgument &Argument,
                        const ASTContext &Context) {
  switch (Argument.getKind()) {
  case TemplateArgument::Integral:
    return Argument.getAsIntegral();
  case TemplateArgument::Expression:
    return foldedIntegerOf(Argument.getAsExpr(), Context);
  case TemplateArgument::Null:
  case TemplateArgument::Type:
  case TemplateArgument::Declaration:
  case TemplateArgument::NullPtr:
  case TemplateArgument::StructuralValue:
  case TemplateArgument::Template:
  case TemplateArgument::TemplateExpansion:
  case TemplateArgument::Pack:
    return std::nullopt;
  }
  llvm_unreachable("a template argument of a kind clang doesn't have");
}

} // namespace clang::tidy::malterlib
