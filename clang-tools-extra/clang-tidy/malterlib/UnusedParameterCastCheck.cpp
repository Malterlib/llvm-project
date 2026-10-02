//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "UnusedParameterCastCheck.h"
#include "NamingUtils.h"
#include "clang/AST/ASTContext.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Lex/Lexer.h"

using namespace clang::ast_matchers;

namespace clang::tidy::malterlib {

/// A statement with attributes, like `[[likely]] Statement;`.
static const ast_matchers::internal::VariadicDynCastAllOfMatcher<Stmt,
                                                                  AttributedStmt>
    attributedStmt;

namespace {
/// Matches what \p Inner matches past parentheses and any number of casts to
/// void, `(void)(void)_Value`.
AST_MATCHER_P(Expr, ignoringVoidCasts, ast_matchers::internal::Matcher<Expr>,
              Inner) {
  const Expr *E = Node.IgnoreParens();
  for (const auto *Cast = dyn_cast<ExplicitCastExpr>(E);
       Cast && Cast->getType()->isVoidType();
       Cast = dyn_cast<ExplicitCastExpr>(E))
    E = Cast->getSubExpr()->IgnoreParens();
  return Inner.matches(*E, Finder, Builder);
}
} // namespace

void UnusedParameterCastCheck::registerMatchers(MatchFinder *Finder) {
  Finder->addMatcher(
      explicitCastExpr(
          hasDestinationType(voidType()),
          hasSourceExpression(ignoringVoidCasts(
              declRefExpr(to(parmVarDecl().bind("parameter"))).bind("ref"))),
          // A statement of its own, also the body of an `if` or a loop, or
          // the initializer of one or of a `switch`, or after a label or a
          // statement attribute, `[[likely]]`, or what a `return` of a void
          // function returns, also a `co_return`.
          hasParent(stmt(anyOf(
              compoundStmt().bind("block"), ifStmt(), switchStmt(), forStmt(),
              whileStmt(), doStmt(), cxxForRangeStmt(), labelStmt(), caseStmt(),
              attributedStmt(), defaultStmt(), returnStmt(), coreturnStmt(),
              binaryOperator(hasOperatorName(",")), cxxFoldExpr(),
              conditionalOperator()))))
          .bind("cast"),
      this);
}

/// The range to remove a statement that is alone on its lines with: from the
/// start of its first line to the start of the line after it. Otherwise the
/// statement and its semicolon.
static std::optional<CharSourceRange>
statementRemovalRange(const Expr *Statement, const SourceManager &SM,
                      const LangOptions &LangOpts) {
  const SourceLocation Begin = Statement->getBeginLoc();
  if (!Begin.isFileID() || !Statement->getEndLoc().isFileID())
    return std::nullopt;
  const SourceLocation AfterSemicolon = Lexer::findLocationAfterToken(
      Statement->getEndLoc(), tok::semi, SM, LangOpts,
      /*SkipTrailingWhitespaceAndNewLine=*/false);
  // Not one written across files, `(void)` and `#include "Name.hpp"`, whose
  // offsets are of different buffers.
  if (AfterSemicolon.isInvalid() ||
      SM.getFileID(Statement->getEndLoc()) != SM.getFileID(Begin) ||
      SM.getFileID(AfterSemicolon) != SM.getFileID(Begin))
    return std::nullopt;

  const FileID File = SM.getFileID(Begin);
  const StringRef Buffer = SM.getBufferData(File);
  const unsigned BeginOffset = SM.getFileOffset(Begin);
  unsigned LineStart = BeginOffset;
  while (LineStart > 0 && (Buffer[LineStart - 1] == ' ' ||
                           Buffer[LineStart - 1] == '\t'))
    --LineStart;
  unsigned End = SM.getFileOffset(AfterSemicolon);
  while (End < Buffer.size() && (Buffer[End] == ' ' || Buffer[End] == '\t'))
    ++End;
  const bool AloneOnLine =
      (LineStart == 0 || Buffer[LineStart - 1] == '\n') &&
      (End == Buffer.size() || Buffer[End] == '\n' || Buffer[End] == '\r');
  if (!AloneOnLine)
    return CharSourceRange::getCharRange(Begin, AfterSemicolon);
  if (End < Buffer.size() && Buffer[End] == '\r')
    ++End;
  if (End < Buffer.size() && Buffer[End] == '\n')
    ++End;
  const SourceLocation FileStart = SM.getLocForStartOfFile(File);
  return CharSourceRange::getCharRange(FileStart.getLocWithOffset(LineStart),
                                       FileStart.getLocWithOffset(End));
}

void UnusedParameterCastCheck::check(const MatchFinder::MatchResult &Result) {
  if (Repeated.repeated(Result))
    return;
  const auto *Cast = Result.Nodes.getNodeAs<ExplicitCastExpr>("cast");
  const auto *Parameter = Result.Nodes.getNodeAs<ParmVarDecl>("parameter");
  const SourceManager &SM = *Result.SourceManager;
  const LangOptions &LangOpts = Result.Context->getLangOpts();

  // Reading a volatile parameter is an access of its own, not silencing, and
  // a cast that isn't evaluated, like in `noexcept(((void)_Value, 0))`, picks
  // the built-in comma.
  if (Cast->getSubExpr()->HasSideEffects(*Result.Context) ||
      isInUnevaluatedOperand(DynTypedNode::create(*Cast), *Result.Context))
    return;
  // A side of a comma, `((void)_Value, 42)` or `fg_Work(), (void)_Value`, of
  // a class or enumeration, which an overloaded `operator,` can take, also
  // when only the other side is one, `(void)_Value, _Other`, is cast to pick
  // the built-in one, unlike one whose other side is only cast too,
  // `(void)_Value, (void)_Other`.
  if (const auto Parents = instantiatedParents(*Cast, *Result.Context);
      !Parents.empty())
    if (const auto *Comma = Parents[0].get<BinaryOperator>();
        Comma && Comma->getOpcode() == BO_Comma) {
      const QualType Type = Parameter->getType().getNonReferenceType();
      const Expr *OtherSide = Comma->getLHS()->IgnoreParens() == Cast
                                  ? Comma->getRHS()
                                  : Comma->getLHS();
      const auto *Other = dyn_cast<ExplicitCastExpr>(OtherSide->IgnoreParens());
      auto Overloadable = [](QualType Side) {
        return Side->isRecordType() || Side->isEnumeralType() ||
               Side->isDependentType();
      };
      if ((Overloadable(Type) ||
           Overloadable(OtherSide->getType().getNonReferenceType())) &&
          !(Other && Other->getType()->isVoidType()))
        return;
    }
  // So is the pattern of a fold over a comma with what it starts or ends
  // with, `((void)_Values, ..., 42)`, which the elements of the pack, of any
  // type, are combined with, unlike one of only casts, `((void)_Values,
  // ...)`.
  if (const auto Parents = instantiatedParents(*Cast, *Result.Context);
      !Parents.empty())
    if (const auto *Fold = Parents[0].get<CXXFoldExpr>();
        Fold && Fold->getOperator() == BO_Comma && Fold->getInit() &&
        Fold->getPattern()->IgnoreParens() == Cast) {
      const auto *Other =
          dyn_cast<ExplicitCastExpr>(Fold->getInit()->IgnoreParens());
      if (!(Other && Other->getType()->isVoidType()))
        return;
    }
  // In a lambda or block, a parameter of a function around it is captured:
  // naming it, like `(void)_Subscription;` in a `[=]` lambda, makes the
  // default capture copy it, like to keep it alive in the closure.
  if (Result.Nodes.getNodeAs<DeclRefExpr>("ref")
          ->refersToEnclosingVariableOrCapture())
    return;
  const SourceLocation Loc = reportLocation(Cast->getBeginLoc(), SM);
  if (Loc.isInvalid())
    return;

  // The parameter keeps its name, which tells what it is, also unused: the
  // builds don't warn about unused parameters.
  auto Diag = diag(Loc, "parameter %0 is silenced with a cast to void; leave "
                        "an unused parameter named, without the cast")
              << Parameter;
  // Removing the body of an `if` or a loop, or what a label labels, would
  // leave it without one.
  const auto *Block = Result.Nodes.getNodeAs<CompoundStmt>("block");
  if (!Block)
    return;
  // Nor the last statement of a statement expression, `({ 42; (void)_Value;
  // })`, which gives its type.
  if (const auto Parents = instantiatedParents(*Block, *Result.Context);
      !Parents.empty() && Parents[0].get<StmtExpr>() && !Block->body_empty() &&
      Block->body_back() == Cast)
    return;
  if (const std::optional<CharSourceRange> Removal =
          statementRemovalRange(Cast, SM, LangOpts))
    Diag << FixItHint::CreateRemoval(*Removal);
}

} // namespace clang::tidy::malterlib
