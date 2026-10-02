//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "NoZeroBitScanCheck.h"
#include "../utils/OptionsUtils.h"
#include "NamingUtils.h"
#include "OncePerTypeVisitor.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTLambda.h"
#include "clang/AST/CXXInheritance.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/RecordLayout.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Basic/Builtins.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Lex/Lexer.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringSet.h"
#include <functional>
#include <map>

using namespace clang::ast_matchers;

namespace clang::tidy::malterlib {

NoZeroBitScanCheck::NoZeroBitScanCheck(StringRef Name,
                                       ClangTidyContext *Context)
    : MalterlibCheck(Name, Context),
      // The functions and macros of the standard library; the Malterlib
      // options file adds Malterlib's.
      Functions(utils::options::parseStringList(listOption("Functions", ""))),
      CheckMacros(
          utils::options::parseStringList(listOption("CheckMacros", "assert"))),
      AssumeMacros(
          utils::options::parseStringList(listOption("AssumeMacros", ""))) {}

void NoZeroBitScanCheck::storeCheckOptions(ClangTidyOptions::OptionMap &Opts) {
  Options.store(Opts, "Functions",
                utils::options::serializeStringList(Functions));
  Options.store(Opts, "CheckMacros",
                utils::options::serializeStringList(CheckMacros));
  Options.store(Opts, "AssumeMacros",
                utils::options::serializeStringList(AssumeMacros));
}

void NoZeroBitScanCheck::registerMatchers(MatchFinder *Finder) {
  Finder->addMatcher(callExpr(callee(functionDecl(hasAnyName(Functions))),
                              argumentCountIs(1))
                         .bind("call"),
                     this);
  // Also through an unchanged local pointer to one, `auto fScan =
  // &fg_GetLowestBitSetNoZero; fScan(_Bits);`, a constant one that isn't
  // local, `constexpr auto gc_fScan = &fg_GetLowestBitSetNoZero;`, or a
  // reference to one, `auto &fScan = fg_GetLowestBitSetNoZero;`.
  // Also dereferenced, `(*fScan)(_Bits)`.
  const auto Pointer = declRefExpr(to(varDecl()));
  Finder->addMatcher(
      callExpr(argumentCountIs(1),
               callee(expr(ignoringParenImpCasts(anyOf(
                   Pointer, unaryOperator(hasOperatorName("*"),
                                          hasUnaryOperand(ignoringParenImpCasts(
                                              Pointer))))))))
          .bind("local"),
      this);
}

namespace {

/// The constant index \p Index is, a literal, also negative, `pEnd[-1]`, also
/// converted to an integer that holds it, `umint(0)`, or a named constant,
/// `constexpr umint c_Key = 0`.
static std::optional<int64_t> constantIndex(const Expr *Index) {
  // Whether it is converted, which keeps a value any integer holds, up to
  // 127, not one a narrower one could wrap, `uint8(256)`.
  bool Converted = false;
  bool Negative = false;
  for (int Depth = 0; Depth < 8; ++Depth) {
    Index = Index->IgnoreParenImpCasts();
    if (const auto *Sign = dyn_cast<UnaryOperator>(Index);
        Sign && !Negative &&
        (Sign->getOpcode() == UO_Minus || Sign->getOpcode() == UO_Plus)) {
      Negative = Sign->getOpcode() == UO_Minus;
      Index = Sign->getSubExpr();
      continue;
    }
    if (const auto *Cast = dyn_cast<ExplicitCastExpr>(Index)) {
      const auto *Builtin = Cast->getType()->getAs<BuiltinType>();
      if (!Builtin || !Builtin->isInteger() || Builtin->isBooleanType() ||
          !Cast->getSubExpr()->getType()->isIntegerType())
        return std::nullopt;
      Converted = true;
      Index = Cast->getSubExpr();
      continue;
    }
    break;
  }
  std::optional<llvm::APSInt> Value;
  if (const auto *Literal = dyn_cast<IntegerLiteral>(Index))
    Value = llvm::APSInt(Literal->getValue(), /*isUnsigned=*/true);
  else if (const auto *Ref = dyn_cast<DeclRefExpr>(Index))
    if (const auto *Named = dyn_cast<VarDecl>(Ref->getDecl());
        Named && Named->isConstexpr() &&
        Named->getType()->isIntegralOrEnumerationType())
      if (const APValue *Evaluated = Named->evaluateValue();
          Evaluated && Evaluated->isInt())
        Value = Evaluated->getInt();
  if (!Value || Value->isNegative() || Value->getActiveBits() > 62)
    return std::nullopt;
  if (Converted && Value->getActiveBits() > 7)
    return std::nullopt;
  const int64_t Constant = static_cast<int64_t>(Value->getZExtValue());
  return Negative ? -Constant : Constant;
}

/// Collects the variables, parameters and members an expression reads. A
/// member is named with the object it is of, as far as that can be told, so
/// `_A.m_Bits` and `_B.m_Bits` are different reads.
/// The values the returns of the body of \p Lambda give, not those of the
/// lambdas in it.
static llvm::SmallVector<const Expr *, 4>
lambdaReturns(const LambdaExpr *Lambda) {
  llvm::SmallVector<const Expr *, 4> Values;
  std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
    if (!S || isa<LambdaExpr>(S))
      return;
    if (const auto *Return = dyn_cast<ReturnStmt>(S)) {
      if (Return->getRetValue())
        Values.push_back(Return->getRetValue());
      return;
    }
    for (const Stmt *Child : childrenOnce(S))
      Visit(Child);
  };
  Visit(Lambda->getBody());
  return Values;
}

/// The lambda that \p Call, a call of its call operator where it is made or
/// of a local holding it, runs, when it captures only by reference, so what it
/// returns of what it captures is that variable, `[&] { return &_Bits; }()`.
static const LambdaExpr *capturingLambdaCalled(const CallExpr *Call) {
  const auto *Invoke = dyn_cast<CXXOperatorCallExpr>(Call);
  if (!Invoke || Invoke->getOperator() != OO_Call || Invoke->getNumArgs() < 1)
    return nullptr;
  const LambdaExpr *Lambda = calledLambda(Invoke->getArg(0));
  if (!Lambda ||
      !llvm::all_of(Lambda->captures(), [](const LambdaCapture &Capture) {
        return Capture.getCaptureKind() == LCK_ByRef ||
               Capture.getCaptureKind() == LCK_This;
      }))
    return nullptr;
  return Lambda;
}

/// The smart pointer \p E gets the pointer of, `_pInfo.f_Get()`, which
/// points where its `operator->` does, or null.
static const Expr *smartPointerGot(const Expr *E) {
  const auto *Call = dyn_cast<CXXMemberCallExpr>(E->IgnoreParenImpCasts());
  const CXXMethodDecl *Method = Call ? Call->getMethodDecl() : nullptr;
  if (!Method || !Method->getIdentifier() || Call->getNumArgs() != 0 ||
      (Method->getName() != "f_Get" && Method->getName() != "get") ||
      !Call->getType()->isPointerType() || !Call->getImplicitObjectArgument())
    return nullptr;
  const Expr *Object = Call->getImplicitObjectArgument();
  const CXXRecordDecl *Record =
      Object->getType().getNonReferenceType()->getAsCXXRecordDecl();
  if (!Record || !Record->hasDefinition() ||
      Record->getDefinition()
          ->lookup(Record->getASTContext().DeclarationNames.getCXXOperatorName(
              OO_Arrow))
          .empty())
    return nullptr;
  return Object;
}

/// What the free function that \p Call calls gives back on its one return of
/// a member of what it is given by reference, `_Integer.m_Lower` of
/// `fg_GetLower(_Bits)`, with the place of that parameter, or null.
static std::pair<const Expr *, unsigned>
memberOfGivenReturnedAt(const CallExpr *Call) {
  const FunctionDecl *Callee = Call->getDirectCallee();
  const FunctionDecl *Definition = Callee ? Callee->getDefinition() : nullptr;
  if (!Definition || isa<CXXMethodDecl>(Definition) || !Call->isGLValue())
    return {nullptr, 0};
  const auto *Body = dyn_cast_or_null<CompoundStmt>(Definition->getBody());
  const auto *Return = Body && Body->size() == 1
                           ? dyn_cast<ReturnStmt>(Body->body_front())
                           : nullptr;
  const Expr *Value =
      Return && Return->getRetValue() ? Return->getRetValue() : nullptr;
  const Expr *Base = Value ? Value->IgnoreParenImpCasts() : nullptr;
  bool Member = false;
  while (const auto *Access = dyn_cast_or_null<MemberExpr>(Base)) {
    if (Access->isArrow() || !isa<FieldDecl>(Access->getMemberDecl()))
      return {nullptr, 0};
    Member = true;
    Base = Access->getBase()->IgnoreParenImpCasts();
  }
  const auto *Ref = dyn_cast_or_null<DeclRefExpr>(Base);
  const auto *Parameter = Ref ? dyn_cast<ParmVarDecl>(Ref->getDecl()) : nullptr;
  if (!Member || !Parameter || !Parameter->getType()->isReferenceType() ||
      Parameter->getFunctionScopeIndex() >= Call->getNumArgs())
    return {nullptr, 0};
  return {Value, Parameter->getFunctionScopeIndex()};
}

static bool memberOfGivenReturned(const CallExpr *Call) {
  return memberOfGivenReturnedAt(Call).first != nullptr;
}

class ReferenceCollector : public OncePerTypeVisitor<ReferenceCollector> {
public:
  /// With \p Context, also the expressions it has, by their structure, like
  /// `_Value - 1`.
  explicit ReferenceCollector(const ASTContext *Context = nullptr)
      : Context(Context) {}

  llvm::StringSet<> References;

  // Not what isn't evaluated, `sizeof(_Bits)` or `noexcept(_Bits != 0)`,
  // which neither reads nor tests it.
  bool TraverseUnaryExprOrTypeTraitExpr(UnaryExprOrTypeTraitExpr *) {
    return true;
  }
  // Nor what a builtin only looks at the type or constness of, nor the arm of
  // `__builtin_choose_expr` it doesn't choose.
  bool TraverseCallExpr(CallExpr *Call) {
    return isUnevaluatedNode(Call) ||
           RecursiveASTVisitor<ReferenceCollector>::TraverseCallExpr(Call);
  }
  bool TraverseChooseExpr(ChooseExpr *Choose) {
    if (Choose->isConditionDependent())
      return RecursiveASTVisitor<ReferenceCollector>::TraverseChooseExpr(
          Choose);
    return TraverseStmt(Choose->getChosenSubExpr());
  }
  bool TraverseCXXNoexceptExpr(CXXNoexceptExpr *) { return true; }
  bool TraverseRequiresExpr(RequiresExpr *) { return true; }
  bool TraverseCXXTypeidExpr(CXXTypeidExpr *Typeid) {
    return !Typeid->isPotentiallyEvaluated() ||
           RecursiveASTVisitor::TraverseCXXTypeidExpr(Typeid);
  }

  bool VisitExpr(Expr *E) {
    if (Context)
      References.insert(expressionKey(E, *Context));
    // A call of a free function that gives a reference to a member of what
    // it is given, `fg_GetLower(_Bits)` of `return _Integer.m_Lower;`, reads
    // that member, `_Bits.m_Lower`.
    if (const auto *Call = dyn_cast<CallExpr>(E);
        Call && memberOfGivenReturned(Call))
      if (const std::string Member = objectKey(Call); !Member.empty())
        References.insert(Member);
    return true;
  }

  // A variable is a read by itself, unless it is only the object of a field,
  // which the field's path names: `_Info.m_bReady` doesn't read
  // `_Info.m_Bits`. A member function, like `operator bool`, reads its
  // object.
  bool TraverseMemberExpr(MemberExpr *Member) {
    References.insert(path(Member));
    // Through a local reference bound to either of a conditional, `CInfo
    // &Info = bFirst ? _Info : _Other`, also of each of them.
    {
      const Expr *Object = Member->getBase()->IgnoreParenImpCasts();
      for (const MemberExpr *Inner = Member;
           !Inner->isArrow() && isa<MemberExpr>(Object);
           Object = Inner->getBase()->IgnoreParenImpCasts())
        Inner = cast<MemberExpr>(Object);
      const auto *Ref = dyn_cast<DeclRefExpr>(Object);
      const auto *Conditional = Ref ? dyn_cast_or_null<ConditionalOperator>(
                                          aliasedValue(Ref->getDecl()))
                                    : nullptr;
      const std::string Own = Ref ? key(Ref->getDecl()) : std::string();
      if (const std::string Path = path(Member);
          Conditional && llvm::StringRef(Path).starts_with(Own + "."))
        for (const Expr *Arm :
             {Conditional->getTrueExpr(), Conditional->getFalseExpr()})
          if (const std::string Arms = objectKey(Arm); !Arms.empty())
            References.insert(Arms + Path.substr(Own.size()));
    }
    VisitExpr(Member);
    if (isa<CXXMethodDecl>(Member->getMemberDecl()))
      return RecursiveASTVisitor::TraverseMemberExpr(Member);
    const Expr *Base = Member->getBase()->IgnoreParenImpCasts();
    while (const auto *Inner = dyn_cast<MemberExpr>(Base))
      Base = Inner->getBase()->IgnoreParenImpCasts();
    // Through a pointer, `_pInfo->m_Bits`, the pointer is read too: pointing
    // it elsewhere, `_pInfo = _pOther` or `++_pInfo`, changes what is read,
    // but not what a reference bound before refers to.
    if (const auto *Ref = dyn_cast<DeclRefExpr>(Base);
        Ref && Ref->getType()->isPointerType() && !AliasDepth && !Written)
      References.insert(selectorKey(Ref->getDecl()));
    if (isa<DeclRefExpr, CXXThisExpr>(Base))
      return true;
    // Also through a smart pointer, `_pInfo->m_Bits`, whose operator gives
    // what it points to, like a pointer, also through its getter,
    // `_pInfo.f_Get()->m_Bits`.
    const Expr *Smart = nullptr;
    if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(Base);
        Operator && Operator->getNumArgs() == 1 &&
        (Operator->getOperator() == OO_Arrow ||
         Operator->getOperator() == OO_Star) &&
        Operator->getArg(0)->getType()->isRecordType())
      Smart = Operator->getArg(0);
    else
      Smart = smartPointerGot(Base);
    if (Smart)
      if (const auto *Ref =
              dyn_cast<DeclRefExpr>(Smart->IgnoreParenImpCasts())) {
        if (!AliasDepth && !Written)
          References.insert(selectorKey(Ref->getDecl()));
        return true;
      }
    // Of an element by an index operator, `_Infos[0].m_Bits`, which its path
    // names, not the whole element; its index is read.
    if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(Base);
        Operator && Operator->getOperator() == OO_Subscript &&
        !objectKey(Operator).empty()) {
      for (unsigned iArgument = 1; iArgument < Operator->getNumArgs();
           ++iArgument)
        TraverseStmt(const_cast<Expr *>(Operator->getArg(iArgument)));
      return true;
    }
    return RecursiveASTVisitor::TraverseMemberExpr(Member);
  }
  bool VisitDeclRefExpr(DeclRefExpr *Ref) {
    References.insert(key(Ref->getDecl()));
    // A local reference is also what it refers to, `unsigned &Value =
    // _Value`.
    if (const Expr *Aliased = aliasedValue(Ref->getDecl());
        Aliased && AliasDepth < 4) {
      // Of a member of what a helper is given, `fg_GetLower(_Bits)`, that
      // member, `_Bits.m_Lower`, not all of it, and what finds it.
      if (const auto *Call = dyn_cast<CallExpr>(Aliased);
          Call && memberOfGivenReturned(Call))
        if (const std::string Member = objectKey(Call); !Member.empty()) {
          References.insert(Member);
          const unsigned iParameter = memberOfGivenReturnedAt(Call).second;
          for (unsigned iArgument = 0; iArgument < Call->getNumArgs();
               ++iArgument)
            if (iArgument != iParameter)
              TraverseStmt(const_cast<Expr *>(Call->getArg(iArgument)));
          return true;
        }
      ++AliasDepth;
      TraverseStmt(const_cast<Expr *>(Aliased));
      --AliasDepth;
    }
    return true;
  }
  int AliasDepth = 0;
  /// Whether what is collected is written, like `_pValues[1] = 0`, which
  /// writes the element, not the pointer it is found through.
  bool Written = false;

  // An element at a constant index, like `_pValues[0]`, is a read of its own,
  // which writing `_pValues[1]` doesn't touch.
  bool TraverseArraySubscriptExpr(ArraySubscriptExpr *Subscript) {
    const std::optional<int64_t> Index = constantIndex(Subscript->getIdx());
    const Expr *Base = Subscript->getBase()->IgnoreParenImpCasts();
    // Through local references to it, `pAlias[0]` of `unsigned *&pAlias =
    // pBits`, of what they are bound to.
    for (int Depth = 0; Depth < 8; ++Depth)
      if (const auto *Ref = dyn_cast<DeclRefExpr>(Base))
        if (const Expr *Aliased = aliasedValue(Ref->getDecl());
            Aliased && isa<DeclRefExpr>(Aliased->IgnoreParenImpCasts())) {
          Base = Aliased->IgnoreParenImpCasts();
          continue;
        }
    std::string BasePath;
    if (const auto *Ref = dyn_cast<DeclRefExpr>(Base))
      BasePath = key(Ref->getDecl());
    else if (const auto *Member = dyn_cast<MemberExpr>(Base);
             Member && !isa<CXXMethodDecl>(Member->getMemberDecl()))
      BasePath = path(Member);
    // Also what a getter gives of a pointer member, `_Bits.f_GetArray()[1]`
    // of `return m_pData;`, like an index operator that indexes the member,
    // `_Bits[1]` of `return m_pData[_Index];`.
    else if (const auto *Getter = dyn_cast<CXXMemberCallExpr>(Base);
             Getter && Getter->getNumArgs() == 0 && Getter->getMethodDecl() &&
             !callsOverride(Getter))
      if (const auto *Callee =
              dyn_cast<MemberExpr>(Getter->getCallee()->IgnoreParens());
          Callee && !Callee->isArrow())
        if (const FunctionDecl *Definition =
                Getter->getMethodDecl()->getDefinition())
          if (const auto *Body =
                  dyn_cast_or_null<CompoundStmt>(Definition->getBody());
              Body && Body->size() == 1)
            if (const auto *Return = dyn_cast<ReturnStmt>(Body->body_front());
                Return && Return->getRetValue())
              if (const auto *Member = dyn_cast<MemberExpr>(
                      Return->getRetValue()->IgnoreParenImpCasts());
                  Member && isa<FieldDecl>(Member->getMemberDecl()) &&
                  Member->getType()->isPointerType() &&
                  isa<CXXThisExpr>(Member->getBase()->IgnoreParenImpCasts()))
                if (const std::string Object =
                        objectKey(Getter->getImplicitObjectArgument());
                    !Object.empty())
                  BasePath = Object + "." + declKey(Member->getMemberDecl());
    // Also what a getter gives of the storage that the index operator of its
    // class indexes, `_Values.f_GetArray()[1]` of `return
    // mp_StaticData.m_pData->f_GetData();` and `return
    // (mp_StaticData.m_pData->f_GetData())[_Index];` of `TCVector`, which is
    // the element of the object, `_Values[1]`, like that operator gives.
    if (BasePath.empty())
      if (const auto *Getter = dyn_cast<CXXMemberCallExpr>(Base);
          Getter && Getter->getNumArgs() == 0 && Getter->getMethodDecl() &&
          Getter->getImplicitObjectArgument() && !callsOverride(Getter))
        if (const std::string Storage =
                indexedStorage(Getter->getMethodDecl(), /*Index=*/false);
            !Storage.empty() &&
            llvm::any_of(Getter->getMethodDecl()->getParent()->methods(),
                         [&](const CXXMethodDecl *Method) {
                           return Method->getOverloadedOperator() ==
                                      OO_Subscript &&
                                  indexedStorage(Method, /*Index=*/true) ==
                                      Storage;
                         }))
          BasePath = objectKey(Getter->getImplicitObjectArgument());
    if (!Index || BasePath.empty())
      return RecursiveASTVisitor::TraverseArraySubscriptExpr(Subscript);
    VisitExpr(Subscript);
    // Through a pointer, `_pBits[0]`, also the pointer, like through `->`.
    if (const auto *Ref = dyn_cast<DeclRefExpr>(Base);
        Ref && Ref->getType()->isPointerType() && !AliasDepth && !Written)
      References.insert(selectorKey(Ref->getDecl()));
    References.insert(BasePath + "[" + std::to_string(*Index) + "]");
    return true;
  }

  // What a pointer points to, `*_pBits`, is its element `[0]`, which writing
  // `_pBits[1]` doesn't touch.
  // So is what a smart pointer points to, `*_pBits`, read through its
  // operator.
  // An element by an index operator, `_Bits[0]` of a vector, is the element
  // of what it gives, also what a pointer from a getter, `f_GetArray()`,
  // points to, as objectKey tells, not the whole container, which writing
  // another element, `++_Bits[1]`, doesn't change; its index is read.
  bool TraverseCXXOperatorCallExpr(CXXOperatorCallExpr *Operator) {
    if (Operator->getOperator() == OO_Subscript)
      if (const std::string Element = objectKey(Operator); !Element.empty()) {
        VisitExpr(Operator);
        References.insert(Element);
        // Not where what is written is collected, `Copy[_Index] = 0`, which
        // doesn't write the index.
        if (!Written)
          for (unsigned iArgument = 1; iArgument < Operator->getNumArgs();
               ++iArgument)
            TraverseStmt(Operator->getArg(iArgument));
        return true;
      }
    const auto *Ref =
        Operator->getOperator() == OO_Star && Operator->getNumArgs() == 1 &&
                Operator->getArg(0)->getType()->isRecordType()
            ? dyn_cast<DeclRefExpr>(Operator->getArg(0)->IgnoreParenImpCasts())
            : nullptr;
    if (!Ref || aliasedValue(Ref->getDecl()))
      return RecursiveASTVisitor::TraverseCXXOperatorCallExpr(Operator);
    VisitExpr(Operator);
    if (!AliasDepth && !Written)
      References.insert(selectorKey(Ref->getDecl()));
    References.insert(key(Ref->getDecl()) + "[0]");
    return true;
  }

  bool TraverseUnaryOperator(UnaryOperator *Unary) {
    const Expr *Pointer = Unary->getOpcode() == UO_Deref
                              ? Unary->getSubExpr()->IgnoreParenImpCasts()
                              : nullptr;
    // Also an iterator converted to a pointer, `*iValue` of `operator
    // t_CData *()`, like one through its `operator*` above.
    bool Converted = false;
    if (const auto *Conversion = dyn_cast_or_null<CXXMemberCallExpr>(Pointer);
        Conversion &&
        isa_and_nonnull<CXXConversionDecl>(Conversion->getMethodDecl()) &&
        Conversion->getImplicitObjectArgument() &&
        Conversion->getImplicitObjectArgument()->getType()->isRecordType()) {
      Pointer = Conversion->getImplicitObjectArgument()->IgnoreParenImpCasts();
      Converted = true;
    }
    // Also what a pointer element an index operator gives points to,
    // `*_pBits[0]` of a vector of pointers, its element `[0]`, which writing it
    // doesn't write the pointer.
    if (const auto *Element = dyn_cast_or_null<CXXOperatorCallExpr>(Pointer);
        Element && !Converted && Element->getOperator() == OO_Subscript &&
        Element->getType()->isPointerType())
      if (const std::string Key = objectKey(Element); !Key.empty()) {
        VisitExpr(Unary);
        if (!Written &&
            !TraverseStmt(const_cast<CXXOperatorCallExpr *>(Element)))
          return false;
        References.insert(Key + "[0]");
        return true;
      }
    const auto *Ref = dyn_cast_or_null<DeclRefExpr>(Pointer);
    // Not through a local reference to a pointer, `unsigned *&pAlias =
    // pBits`, which is followed to what it is bound to.
    if (!Ref || (!Converted && !Ref->getType()->isPointerType()) ||
        aliasedValue(Ref->getDecl()))
      return RecursiveASTVisitor::TraverseUnaryOperator(Unary);
    // What the conversion reads and gives, which a copy of the iterator also
    // points to, is still what it is.
    if (Converted && !RecursiveASTVisitor::TraverseUnaryOperator(Unary))
      return false;
    if (!Converted)
      VisitExpr(Unary);
    if (!AliasDepth && !Written)
      References.insert(selectorKey(Ref->getDecl()));
    References.insert(key(Ref->getDecl()) + "[0]");
    return true;
  }

  /// The key of the expression \p E, by its structure, also by the types it
  /// converts to, which the profile leaves out of implicit conversions:
  /// `_Bits` converted to `int` isn't `_Bits` converted to `uint8`.
  static std::string expressionKey(const Expr *E, const ASTContext &Context) {
    // One that suspends, `co_await fg_Read(x)`, has another value each time it
    // is evaluated, and is its own: its profile would go through the operand
    // of each co_await again in the expressions that make and call its
    // awaiter, a number of times exponential in the depth of nested awaits.
    std::function<bool(const Stmt *)> Suspends = [&](const Stmt *S) {
      if (!S)
        return false;
      if (isa<CoroutineSuspendExpr>(S))
        return true;
      for (const Stmt *Child : childrenOnce(S))
        if (Suspends(Child))
          return true;
      return false;
    };
    if (Suspends(E))
      return "expr:suspends:" + std::to_string(reinterpret_cast<uintptr_t>(E));
    llvm::FoldingSetNodeID ID;
    E->Profile(ID, Context, /*Canonical=*/true);
    std::function<void(const Stmt *)> AddTypes = [&](const Stmt *S) {
      if (!S)
        return;
      if (const auto *Cast = dyn_cast<CastExpr>(S))
        ID.AddPointer(
            Context.getCanonicalType(Cast->getType()).getAsOpaquePtr());
      for (const Stmt *Child : childrenOnce(S))
        AddTypes(Child);
    };
    AddTypes(E);
    return "expr:" + std::to_string(ID.ComputeHash());
  }

  /// The key of the value \p E is, a variable or a member of an object, like
  /// `_pInfo[_Index].m_Bits`, without the variables that find the object,
  /// like `_Index`. Empty for other expressions.
  static std::string valueKey(const Expr *E) {
    E = E->IgnoreParens();
    if (const auto *Ref = dyn_cast<DeclRefExpr>(E))
      return key(Ref->getDecl());
    if (const auto *Member = dyn_cast<MemberExpr>(E);
        Member && isa<FieldDecl, VarDecl>(Member->getMemberDecl()))
      return path(Member);
    return "";
  }

private:
  const ASTContext *Context;

  static std::string key(const ValueDecl *Decl) {
    // The same variable in each of its declarations, `extern unsigned
    // g_Bits;` and its definition.
    Decl = cast<ValueDecl>(Decl->getCanonicalDecl());
    // A variable that functions it calls can write, a global, a static
    // member or a static local, which a class in its function can write, is
    // told by its `g`.
    if (const auto *Variable = dyn_cast<VarDecl>(Decl);
        Variable && Variable->hasGlobalStorage())
      return "g" + std::to_string(reinterpret_cast<uintptr_t>(Decl));
    return std::to_string(reinterpret_cast<uintptr_t>(Decl));
  }

  /// What tells apart a member reached through \p Object of separate base
  /// objects of the same class, `_Info.CFirst::m_Bits` and
  /// `_Info.CSecond::m_Bits` of `struct CInfo : CFirst, CSecond`: the bases
  /// it is reached through, when its class is more than one base of the
  /// object. It is empty otherwise, which is related to each of them.
  static std::string basesOf(const Expr *Object) {
    llvm::SmallVector<const ImplicitCastExpr *, 2> Casts;
    for (Object = Object->IgnoreParens();
         const auto *Cast = dyn_cast<ImplicitCastExpr>(Object);
         Object = Cast->getSubExpr()->IgnoreParens())
      if (Cast->getCastKind() == CK_DerivedToBase ||
          Cast->getCastKind() == CK_UncheckedDerivedToBase)
        Casts.push_back(Cast);
    if (Casts.empty())
      return "";
    QualType Type = Object->getType();
    if (Type->isPointerType())
      Type = Type->getPointeeType();
    const auto *Derived = Type->getAsCXXRecordDecl();
    const auto *Base = Casts.front()->getType()->isPointerType()
                           ? Casts.front()->getType()->getPointeeCXXRecordDecl()
                           : Casts.front()->getType()->getAsCXXRecordDecl();
    if (!Derived || !Base || !Derived->hasDefinition())
      return "";
    CXXBasePaths Paths(/*FindAmbiguities=*/true, /*RecordPaths=*/true,
                       /*DetectVirtual=*/false);
    if (!Derived->isDerivedFrom(Base, Paths))
      return "";
    // Separate objects through each path without a virtual base, and one
    // through all those with one, which is one object however it is reached,
    // also besides a separate one, `struct CMixed : CDirect, CVirtual`.
    unsigned nSeparate = 0;
    bool Virtual = false;
    for (const CXXBasePath &Path : Paths)
      if (llvm::any_of(Path, [](const CXXBasePathElement &Element) {
            return Element.Base->isVirtual();
          }))
        Virtual = true;
      else
        ++nSeparate;
    if (nSeparate + (Virtual ? 1 : 0) < 2)
      return "";
    std::string Bases;
    bool ThroughVirtual = false;
    for (const ImplicitCastExpr *Cast : llvm::reverse(Casts))
      for (const CXXBaseSpecifier *Specifier : Cast->path()) {
        ThroughVirtual |= Specifier->isVirtual();
        if (const CXXRecordDecl *Record =
                Specifier->getType()->getAsCXXRecordDecl())
          Bases += "^" + std::to_string(reinterpret_cast<uintptr_t>(
                             Record->getCanonicalDecl()));
      }
    if (ThroughVirtual)
      return "^virtual^" + std::to_string(reinterpret_cast<uintptr_t>(
                               Base->getCanonicalDecl()));
    return Bases;
  }

  /// The member through the members and the variable or `this` it is of.
  /// Of an object that can't be told apart, it is a member of that object
  /// only, which no other expression is the same as.
  static std::string path(const MemberExpr *Member) {
    // A static member is the same variable through any object, `_State
    // .ms_Bits` and `CState::ms_Bits`.
    if (isa<VarDecl>(Member->getMemberDecl()))
      return key(Member->getMemberDecl());
    // The members, from the innermost.
    llvm::SmallVector<const ValueDecl *, 4> Members{Member->getMemberDecl()};
    llvm::SmallVector<std::string, 4> Bases{basesOf(Member->getBase())};
    const MemberExpr *Outermost = Member;
    const Expr *Base = Member->getBase()->IgnoreParenImpCasts();
    // A member of a static member, `_State.ms_Info.m_Bits`, is of that
    // variable.
    const ValueDecl *StaticObject = nullptr;
    while (const auto *Inner = dyn_cast<MemberExpr>(Base)) {
      if (Outermost->isArrow())
        break;
      if (isa<VarDecl>(Inner->getMemberDecl())) {
        StaticObject = Inner->getMemberDecl();
        break;
      }
      Members.push_back(Inner->getMemberDecl());
      Bases.push_back(basesOf(Inner->getBase()));
      Outermost = Inner;
      Base = Inner->getBase()->IgnoreParenImpCasts();
    }
    // The members of a union share their storage: a path into one is marked
    // at the union, `_Bits.~.m_Read.m_Bits`, which is related to every other
    // path into it, like `_Bits.~.m_Write.m_Bits`, but is still its own.
    std::string Path;
    for (size_t iMember = Members.size(); iMember-- > 0;) {
      const ValueDecl *Decl = Members[iMember];
      if (!Path.empty())
        Path += ".";
      if (const auto *Record = dyn_cast<RecordDecl>(Decl->getDeclContext());
          Record && Record->isUnion())
        Path += "~.";
      Path += key(Decl) + Bases[iMember];
    }
    if (StaticObject)
      return key(StaticObject) + "." + Path;
    std::string Object = objectKey(Base);
    if (Object.empty())
      Object = "@" + std::to_string(reinterpret_cast<uintptr_t>(Base));
    // Through a pointer, `pInfo->m_Bits`, it is of what the pointer points
    // to, like `(*pInfo).m_Bits`.
    else if (Outermost->isArrow() && !isa<CXXThisExpr>(Base))
      Object += "[0]";
    return Object + "." + Path;
  }

public:
  /// The key of the variable or member \p Decl.
  static std::string declKey(const ValueDecl *Decl) { return key(Decl); }

  /// The key of the pointer \p Decl as what selects the object it points
  /// to, like `_pInfo` of `_pInfo->m_Bits`: pointing it elsewhere changes
  /// what is read, but writing what it points to doesn't change it.
  static std::string selectorKey(const ValueDecl *Decl) {
    return key(Decl) + "*";
  }

  /// What the local reference \p Decl is bound to directly, without a
  /// conversion, like `_Value` of `unsigned &Value = _Value` but not of
  /// `uint8 const &Small = _Value`, also through a cast to a reference,
  /// `static_cast<unsigned &>(_Value)`, or either of a conditional,
  /// `bFirst ? _First : _Second`, which may be either, or the call of a member
  /// function that gives what it refers into, `_View.f_Bits()`.
  static const Expr *aliasedValue(const ValueDecl *Decl) {
    // A name of a structured binding of a reference, `auto &[Bits] = _Info`,
    // is the member it binds, also one of a copy that is a reference, `auto
    // [Bits] = CBorrowedBits{_Bits}` of `unsigned &m_Bits`, which refers to
    // what the copy's does.
    if (const auto *Binding = dyn_cast<BindingDecl>(Decl)) {
      const auto *Decomposed =
          dyn_cast_or_null<VarDecl>(Binding->getDecomposedDecl());
      if (!Decomposed || !Decomposed->hasLocalStorage() ||
          !Binding->getBinding())
        return nullptr;
      const auto *Member =
          dyn_cast<MemberExpr>(Binding->getBinding()->IgnoreImplicit());
      return Decomposed->getType()->isReferenceType() ||
                     (Member &&
                      Member->getMemberDecl()->getType()->isReferenceType())
                 ? Binding->getBinding()
                 : nullptr;
    }
    // Also one at namespace scope, `unsigned &g_Alias = g_Bits`, which can't
    // be bound to another either.
    const auto *Alias = dyn_cast<VarDecl>(Decl);
    if (!Alias || !Alias->getType()->isReferenceType() ||
        isa<ParmVarDecl>(Alias) || !Alias->getInit())
      return nullptr;
    // Also with the cleanups of the temporaries made for it, `_Masks[umint(1)]`.
    const Expr *Init = Alias->getInit()->IgnoreParens();
    if (const auto *Full = dyn_cast<FullExpr>(Init))
      Init = Full->getSubExpr()->IgnoreParens();
    for (int Depth = 0; Depth < 64; ++Depth) {
      // What a helper gives back as it is given it, `unsigned &Bits =
      // fg_Ref(_Bits)` of `unsigned &fg_Ref(unsigned &_Bits) { return
      // _Bits; }`.
      if (const auto *Call = dyn_cast<CallExpr>(Init); Call && Call->isGLValue()) {
        // Also what a lambda that captures by reference returns, `[&]()
        // -> unsigned & { return _Bits; }()`.
        if (const LambdaExpr *Lambda = capturingLambdaCalled(Call)) {
          const llvm::SmallVector<const Expr *, 4> Values =
              lambdaReturns(Lambda);
          if (Values.size() != 1 || !Values[0]->isGLValue())
            return nullptr;
          Init = Values[0]->IgnoreParens();
          continue;
        }
        const llvm::SmallVector<const Expr *, 2> Returned =
            returnedArguments(Call);
        // Of a member function, into its object as far as can be told,
        // `unsigned &Bits = _View.f_Bits()`, which the call names, and so
        // of a free function that gives a member of what it is given,
        // `fg_GetLower(_Bits)` of `return _Integer.m_Lower;`.
        if (Returned.empty() &&
            (isa<CXXMemberCallExpr>(Call) ||
             (isa<CXXOperatorCallExpr>(Call) &&
              isa_and_nonnull<CXXMethodDecl>(Call->getDirectCallee())) ||
             memberOfGivenReturned(Call)))
          return Call;
        if (Returned.size() != 1 || !Returned[0]->isGLValue())
          return nullptr;
        Init = Returned[0]->IgnoreParens();
        continue;
      }
      if (const auto *Cast = dyn_cast<ImplicitCastExpr>(Init)) {
        if (Cast->getCastKind() != CK_NoOp &&
            Cast->getCastKind() != CK_DerivedToBase &&
            Cast->getCastKind() != CK_UncheckedDerivedToBase)
          return nullptr;
        Init = Cast->getSubExpr()->IgnoreParens();
      } else if (const auto *Cast = dyn_cast<ExplicitCastExpr>(Init);
                 Cast && Cast->isGLValue() &&
                 (Cast->getCastKind() == CK_NoOp ||
                  Cast->getCastKind() == CK_LValueBitCast ||
                  Cast->getCastKind() == CK_DerivedToBase ||
                  Cast->getCastKind() == CK_UncheckedDerivedToBase))
        Init = Cast->getSubExpr()->IgnoreParens();
      else
        break;
    }
    if (const auto *Conditional = dyn_cast<ConditionalOperator>(Init))
      return Conditional->isGLValue() ? Init : nullptr;
    // Also a member through a pointer to it, `_Info.*(&CInfo::m_Bits)`, which
    // is in the object.
    if (const auto *Member = dyn_cast<BinaryOperator>(Init);
        Member && Member->isPtrMemOp())
      return Init;
    return isa<DeclRefExpr, MemberExpr, ArraySubscriptExpr, UnaryOperator>(Init)
               ? Init
               : nullptr;
  }

  /// What the member function \p Method, whose body can be seen, gives of
  /// its object's storage, as written, `this.<mp_StaticData>.<m_pData>->
  /// <f_GetData>()` of `return mp_StaticData.m_pData->f_GetData();`, other
  /// than null, on each of its returns: with \p Index, that it indexes by its
  /// parameter, `return (...)[_Index];`. Empty when it can't be told.
  static std::string indexedStorage(const CXXMethodDecl *Method, bool Index) {
    const FunctionDecl *Definition = Method ? Method->getDefinition() : nullptr;
    if (!Definition || !Definition->getBody() ||
        Definition->getNumParams() != (Index ? 1u : 0u))
      return "";
    std::function<std::string(const Expr *, int)> Spelled =
        [&](const Expr *E, int Depth) -> std::string {
      E = E->IgnoreParenImpCasts();
      if (Depth > 8)
        return "";
      if (isa<CXXThisExpr>(E))
        return "this";
      if (const auto *Member = dyn_cast<MemberExpr>(E);
          Member && isa<FieldDecl>(Member->getMemberDecl())) {
        const std::string Of = Spelled(Member->getBase(), Depth + 1);
        return Of.empty() ? Of
                          : Of + (Member->isArrow() ? "->" : ".") +
                                declKey(Member->getMemberDecl());
      }
      if (const auto *Call = dyn_cast<CXXMemberCallExpr>(E);
          Call && Call->getNumArgs() == 0 && Call->getMethodDecl() &&
          Call->getImplicitObjectArgument()) {
        const auto *Callee =
            dyn_cast<MemberExpr>(Call->getCallee()->IgnoreParens());
        const std::string Of =
            Spelled(Call->getImplicitObjectArgument(), Depth + 1);
        return Of.empty() || !Callee
                   ? std::string()
                   : Of + (Callee->isArrow() ? "->" : ".") + "call" +
                         declKey(Call->getMethodDecl()->getCanonicalDecl());
      }
      return "";
    };
    std::string Storage;
    bool One = true;
    std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
      if (!S || !One || isa<LambdaExpr>(S))
        return;
      if (const auto *Return = dyn_cast<ReturnStmt>(S)) {
        const Expr *Value = Return->getRetValue()
                                ? Return->getRetValue()->IgnoreParenImpCasts()
                                : nullptr;
        if (!Value) {
          One = false;
          return;
        }
        // Not null, which isn't storage.
        if (!Index &&
            (isa<CXXNullPtrLiteralExpr, GNUNullExpr>(Value) ||
             Value->isNullPointerConstant(Definition->getASTContext(),
                                          Expr::NPC_ValueDependentIsNotNull)))
          return;
        if (Index) {
          const auto *Element = dyn_cast<ArraySubscriptExpr>(Value);
          const auto *Given =
              Element ? dyn_cast<DeclRefExpr>(
                            Element->getIdx()->IgnoreParenImpCasts())
                      : nullptr;
          if (!Given || Given->getDecl() != Definition->getParamDecl(0)) {
            One = false;
            return;
          }
          Value = Element->getBase();
        }
        const std::string Spelling = Spelled(Value, 0);
        if (Spelling.empty() || (!Storage.empty() && Storage != Spelling))
          One = false;
        Storage = Spelling;
        return;
      }
      for (const Stmt *Child : childrenOnce(S))
        Visit(Child);
    };
    Visit(Definition->getBody());
    return One ? Storage : "";
  }

  /// The key of the object \p Base is, as far as it can be told: a variable,
  /// `this`, an element at a constant index or through a variable, like
  /// `_pInfo[1]`, or what a pointer points to, like `*_pA`, also a smart
  /// pointer, `*_pInfo`, or through its `operator->`, `_pInfo->`, of which
  /// the key is the pointer's. Empty when it can't be told.
  static std::string objectKey(const Expr *Base, int Depth = 0) {
    Base = Base->IgnoreParenImpCasts();
    // Also through a cast that only takes the const away, `*const_cast<unsigned
    // *>(pBits)`, which is the same object.
    if (const auto *Cast = dyn_cast<CXXConstCastExpr>(Base))
      return objectKey(Cast->getSubExpr(), Depth);
    if (const auto *Ref = dyn_cast<DeclRefExpr>(Base)) {
      // A local reference to an object is that object, `CInfo &Alias =
      // _Info`, through a few of them, which `path` can go through again,
      // like for `CInfo &Alias = Alias.m_Next`.
      static thread_local int s_nFollowed = 0;
      if (const Expr *Aliased = aliasedValue(Ref->getDecl());
          Aliased && Depth < 4 && s_nFollowed < 8) {
        ++s_nFollowed;
        std::string Object = objectKey(Aliased, Depth + 1);
        --s_nFollowed;
        if (!Object.empty())
          return Object;
      }
      return key(Ref->getDecl());
    }
    if (isa<CXXThisExpr>(Base))
      return "this";
    if (const auto *Member = dyn_cast<MemberExpr>(Base);
        Member && !isa<CXXMethodDecl>(Member->getMemberDecl()))
      return path(Member);
    // What a lambda that captures by reference returns a reference to,
    // `fState()` of `auto fState = [&]() -> CState & { return _State; }`.
    if (const auto *Call = dyn_cast<CallExpr>(Base);
        Call && Call->isGLValue() && Depth < 4)
      if (const LambdaExpr *Lambda = capturingLambdaCalled(Call)) {
        const llvm::SmallVector<const Expr *, 4> Values = lambdaReturns(Lambda);
        return Values.size() == 1 && Values[0]->isGLValue()
                   ? objectKey(Values[0], Depth + 1)
                   : "";
      }
    // What a smart pointer gets, `_pInfo.f_Get()`, is where its `operator->`
    // points.
    if (const Expr *Smart = smartPointerGot(Base))
      return objectKey(Smart, Depth);
    if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(Base);
        Operator && Operator->getNumArgs() == 1 &&
        Operator->getArg(0)->getType()->isRecordType()) {
      if (Operator->getOperator() == OO_Arrow)
        return objectKey(Operator->getArg(0), Depth);
      if (Operator->getOperator() == OO_Star) {
        const std::string Pointer = objectKey(Operator->getArg(0), Depth);
        return Pointer.empty() ? Pointer : Pointer + "[0]";
      }
    }
    // An element by an index operator that can't change the container, like
    // `_Infos[_i]` of a vector, like one of an array, also by one that inserts
    // what it is asked for, `_Masks[1]` of a map, which gives the element of
    // that key, apart from the others.
    if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(Base);
        Operator && Operator->getOperator() == OO_Subscript &&
        Operator->getNumArgs() == 2)
      if (const auto *Method =
              dyn_cast_or_null<CXXMethodDecl>(Operator->getDirectCallee());
          Method && (Method->isConst() || onlyGivesAccess(Method) ||
                     Operator->isLValue())) {
        std::string Container = objectKey(Operator->getArg(0), Depth);
        if (Container.empty())
          return Container;
        // Of the element of a pointer member it can be seen to give, `return
        // m_pData[_Index];` of `TCVector`, which what a getter of the member
        // gives, `_Bits.f_GetArray()`, points into too.
        if (const FunctionDecl *Definition = Method->getDefinition();
            Definition && Definition->getBody() &&
            Definition->getNumParams() == 1)
          if (const auto *Body = dyn_cast<CompoundStmt>(Definition->getBody());
              Body && Body->size() == 1)
            if (const auto *Return = dyn_cast<ReturnStmt>(Body->body_front());
                Return && Return->getRetValue())
              if (const auto *Element = dyn_cast<ArraySubscriptExpr>(
                      Return->getRetValue()->IgnoreParenImpCasts())) {
                const auto *Member = dyn_cast<MemberExpr>(
                    Element->getBase()->IgnoreParenImpCasts());
                const auto *Given = dyn_cast<DeclRefExpr>(
                    Element->getIdx()->IgnoreParenImpCasts());
                if (Member &&
                    isa<CXXThisExpr>(
                        Member->getBase()->IgnoreParenImpCasts()) &&
                    isa<FieldDecl>(Member->getMemberDecl()) &&
                    Member->getMemberDecl()->getType()->isPointerType() &&
                    Given && Given->getDecl() == Definition->getParamDecl(0))
                  Container += "." + declKey(Member->getMemberDecl());
              }
        const Expr *Index = Operator->getArg(1)->IgnoreParenImpCasts();
        if (const std::optional<int64_t> Constant = constantIndex(Index))
          return Container + "[" + std::to_string(*Constant) + "]";
        if (const auto *Variable = dyn_cast<DeclRefExpr>(Index))
          return Container + "[$" + key(Variable->getDecl()) + "]";
        // At a computed index, `(*this)[f_GetLen() - 1]` of `f_GetLast()`,
        // an element that can be any, of this index only.
        return Container + "[$?" +
               std::to_string(reinterpret_cast<uintptr_t>(Index)) + "]";
      }
    // What a free function gives a reference to of what it is given,
    // `fg_GetLower(_Bits)` of `return _Integer.m_Lower;`, is that of what it is
    // given.
    if (const auto *Call = dyn_cast<CallExpr>(Base); Call && Depth < 4)
      if (const auto [Value, iParameter] = memberOfGivenReturnedAt(Call);
          Value) {
        const ParmVarDecl *Parameter =
            Call->getDirectCallee()->getDefinition()->getParamDecl(iParameter);
        const std::string Inner = objectKey(Value, Depth + 1);
        const std::string Own = key(Parameter);
        if (StringRef(Inner).starts_with(Own)) {
          const StringRef Rest = StringRef(Inner).drop_front(Own.size());
          const std::string Object =
              objectKey(Call->getArg(iParameter), Depth + 1);
          if (!Object.empty() &&
              (Rest.empty() || Rest[0] == '.' || Rest[0] == '['))
            return Object + Rest.str();
        }
      }
    // What a getter of an object gives a reference to of it,
    // `_Entries.f_GetFirst()` of `return (*this)[0];`, is that of the object.
    if (const auto *Call = dyn_cast<CXXMemberCallExpr>(Base);
        Call && Call->isGLValue() && Depth < 4 && Call->getMethodDecl() &&
        !callsOverride(Call))
      if (const auto *Callee =
              dyn_cast<MemberExpr>(Call->getCallee()->IgnoreParens());
          Callee && !Callee->isArrow())
        if (const FunctionDecl *Definition =
                Call->getMethodDecl()->getDefinition();
            Definition && Definition->getNumParams() == 0)
          if (const auto *Body =
                  dyn_cast_or_null<CompoundStmt>(Definition->getBody());
              Body && Body->size() == 1)
            if (const auto *Return = dyn_cast<ReturnStmt>(Body->body_front());
                Return && Return->getRetValue()) {
              const std::string Inner =
                  objectKey(Return->getRetValue(), Depth + 1);
              const StringRef Rest = StringRef(Inner).drop_front(4);
              if (StringRef(Inner).starts_with("this") &&
                  (Rest.empty() || Rest[0] == '.' || Rest[0] == '[')) {
                const std::string Object =
                    objectKey(Call->getImplicitObjectArgument(), Depth + 1);
                if (!Object.empty())
                  return Object + Rest.str();
              }
            }
    if (const auto *Deref = dyn_cast<UnaryOperator>(Base);
        Deref && Deref->getOpcode() == UO_Deref) {
      // `*this` is the object, like `this->`.
      if (isa<CXXThisExpr>(Deref->getSubExpr()->IgnoreParenImpCasts()))
        return "this";
      // What an object converts to a pointer to, `*iValue` of a
      // `TCVectorIterator`, like `*iValue` of one with `operator*`.
      if (const auto *Conversion = dyn_cast<CXXMemberCallExpr>(
              Deref->getSubExpr()->IgnoreParenImpCasts());
          Conversion &&
          isa_and_nonnull<CXXConversionDecl>(Conversion->getMethodDecl()) &&
          Conversion->getImplicitObjectArgument() &&
          Conversion->getImplicitObjectArgument()->getType()->isRecordType()) {
        const std::string Object =
            objectKey(Conversion->getImplicitObjectArgument(), Depth);
        return Object.empty() ? Object : Object + "[0]";
      }
      const std::string Pointer = objectKey(Deref->getSubExpr());
      return Pointer.empty() ? Pointer : Pointer + "[0]";
    }
    if (const auto *Subscript = dyn_cast<ArraySubscriptExpr>(Base)) {
      const std::string Array = objectKey(Subscript->getBase());
      if (Array.empty())
        return Array;
      const Expr *Index = Subscript->getIdx()->IgnoreParenImpCasts();
      if (const std::optional<int64_t> Constant = constantIndex(Index))
        return Array + "[" + std::to_string(*Constant) + "]";
      if (const auto *Variable = dyn_cast<DeclRefExpr>(Index))
        return Array + "[$" + key(Variable->getDecl()) + "]";
    }
    return "";
  }
};

} // namespace

static llvm::StringSet<> referencesOf(const Stmt *S,
                                      const ASTContext *Context = nullptr,
                                      bool Written = false) {
  ReferenceCollector Collector(Context);
  Collector.Written = Written;
  Collector.TraverseStmt(const_cast<Stmt *>(S));
  return Collector.References;
}

/// Whether \p S reads one of \p References, or has one of the expressions
/// of them.
static bool readsAny(const Stmt *S, const llvm::StringSet<> &References,
                     const ASTContext *Context = nullptr) {
  if (!S)
    return false;
  return llvm::any_of(referencesOf(S, Context).keys(), [&](StringRef Reference) {
    return References.contains(Reference);
  });
}

/// Whether \p S has a label in it, which a `goto` can jump to past what is
/// before it, not in a lambda.
static bool containsLabel(const Stmt *S) {
  if (!S || isa<LambdaExpr>(S))
    return false;
  if (isa<LabelStmt>(S))
    return true;
  return llvm::any_of(childrenOnce(S),
                      [](const Stmt *Child) { return containsLabel(Child); });
}

/// Whether \p A and \p B are of the same data: the same, or one is a member
/// of the other, like `_Info` and `_Info.m_Bits`.
static bool isRelated(StringRef A, StringRef B) {
  // Into the same union, `_Bits.~.m_Read` and `_Bits.~.m_Write`, which share
  // its storage.
  if (const size_t Union = A.find(".~."); Union != StringRef::npos &&
                                          B.size() >= Union + 3 &&
                                          B.substr(0, Union + 3) ==
                                              A.substr(0, Union + 3))
    return true;
  // An element at an index that isn't a constant, `_Bits[$<_Index>]`, can be
  // any, also `_Bits[0]`.
  size_t iA = 0, iB = 0;
  while (iA < A.size() && iB < B.size()) {
    if (A[iA] == '[' && B[iB] == '[') {
      const size_t EndA = A.find(']', iA);
      const size_t EndB = B.find(']', iB);
      if (EndA == StringRef::npos || EndB == StringRef::npos)
        break;
      const StringRef IndexA = A.slice(iA + 1, EndA);
      const StringRef IndexB = B.slice(iB + 1, EndB);
      // Also a negative one, `pEnd[-1]`.
      auto IsNumber = [](StringRef Index) {
        Index.consume_front("-");
        return !Index.empty() && llvm::all_of(Index, llvm::isDigit);
      };
      if (IndexA != IndexB && IsNumber(IndexA) && IsNumber(IndexB))
        return false;
      iA = EndA + 1;
      iB = EndB + 1;
      continue;
    }
    if (A[iA] != B[iB])
      return false;
    ++iA;
    ++iB;
  }
  // A member without the bases it is reached through, `_Info.m_Bits`, is
  // related to it through each of them, `_Info.m_Bits^CFirst`.
  const StringRef RestA = A.drop_front(iA);
  const StringRef RestB = B.drop_front(iB);
  const StringRef Rest = RestA.empty() ? RestB : RestA;
  return (RestA.empty() || RestB.empty()) &&
         (Rest.empty() || Rest[0] == '.' || Rest[0] == '[' || Rest[0] == '*' ||
          Rest[0] == '^');
}

/// Whether \p S, what is written, is of the data of one of \p References.
static bool touchesAny(const Stmt *S, const llvm::StringSet<> &References) {
  if (!S)
    return false;
  return llvm::any_of(referencesOf(S, nullptr, /*Written=*/true).keys(),
                      [&](StringRef Written) {
    return llvm::any_of(References.keys(), [&](StringRef Reference) {
      return isRelated(Written, Reference);
    });
  });
}

/// Whether \p Made, what an object is made of, gives it one of
/// \p References: like `touchesAny`, but a lambda in it doesn't give it what
/// it captures a copy of that can't refer to anything, `[_Bits] { ... }` or
/// `[Bits = _Bits] { ... }`, only what it captures otherwise and what its
/// body uses besides.
static bool givesAny(const Stmt *S, const llvm::StringSet<> &References) {
  if (!S)
    return false;
  const auto *Lambda = dyn_cast<LambdaExpr>(S);
  // Also a local that holds a lambda, `fg_Move(fClear)` of `auto fClear =
  // [&] { _Bits = 0; };`, which gives what the lambda would.
  std::function<bool(const Stmt *)> HoldsGiving = [&](const Stmt *Inner) {
    if (!Inner || isa<LambdaExpr>(Inner))
      return false;
    if (const auto *Ref = dyn_cast<DeclRefExpr>(Inner))
      if (const auto *Local = dyn_cast<VarDecl>(Ref->getDecl());
          Local && Local->hasLocalStorage() && !isa<ParmVarDecl>(Local)) {
        const CXXRecordDecl *Closure =
            Local->getType().getNonReferenceType()->getAsCXXRecordDecl();
        if (Closure && Closure->isLambda())
          if (const LambdaExpr *Held = calledLambda(Ref);
              Held && givesAny(Held, References))
            return true;
      }
    return llvm::any_of(childrenOnce(Inner), HoldsGiving);
  };
  if (!Lambda && HoldsGiving(S))
    return true;
  if (!Lambda) {
    std::function<bool(const Stmt *)> HasLambda = [&](const Stmt *Inner) {
      return isa<LambdaExpr>(Inner) ||
             llvm::any_of(childrenOnce(Inner), HasLambda);
    };
    if (!HasLambda(S))
      return touchesAny(S, References);
    return llvm::any_of(childrenOnce(S), [&](const Stmt *Child) {
      return givesAny(Child, References);
    });
  }
  llvm::StringSet<> Copied;
  for (const auto [Capture, Init] :
       llvm::zip(Lambda->captures(), Lambda->capture_inits())) {
    const auto *Variable = Capture.capturesVariable()
                               ? dyn_cast<VarDecl>(Capture.getCapturedVar())
                               : nullptr;
    const QualType Type = Variable ? Variable->getType() : QualType();
    if (Capture.getCaptureKind() == LCK_ByCopy && !Type.isNull() &&
        !Type->isReferenceType() &&
        (Type->isArithmeticType() || Type->isEnumeralType())) {
      if (!Variable->isInitCapture())
        Copied.insert(std::to_string(
            reinterpret_cast<uintptr_t>(Variable->getCanonicalDecl())));
      continue;
    }
    if (touchesAny(Init, References))
      return true;
  }
  return llvm::any_of(
      referencesOf(Lambda->getBody(), nullptr, /*Written=*/true).keys(),
      [&](StringRef Written) {
        if (Copied.contains(Written.take_while(llvm::isDigit)))
          return false;
        return llvm::any_of(References.keys(), [&](StringRef Reference) {
          return isRelated(Written, Reference);
        });
      });
}

/// Whether making a \p To of a \p From keeps its bits: an integer class at
/// least as wide, `TCInt` of another `TCInt` or of an integer.
static bool widensInteger(QualType From, QualType To,
                          const ASTContext &Context) {
  From = From.getNonReferenceType().getUnqualifiedType();
  To = To.getNonReferenceType().getUnqualifiedType();
  const auto *Made = dyn_cast_or_null<ClassTemplateSpecializationDecl>(
      To->getAsCXXRecordDecl());
  if (!Made || From->isDependentType() || To->isDependentType() ||
      !Made->hasDefinition())
    return false;
  const auto *Given = dyn_cast_or_null<ClassTemplateSpecializationDecl>(
      From->getAsCXXRecordDecl());
  if (Given && (!Given->hasDefinition() ||
                Given->getSpecializedTemplate()->getCanonicalDecl() !=
                    Made->getSpecializedTemplate()->getCanonicalDecl()))
    return false;
  if (!Given && !From->isIntegerType())
    return false;
  return Context.getTypeSize(To) >= Context.getTypeSize(From);
}

/// What \p Made, if it makes a \p To by widening it, widens: the one argument
/// given to its constructor, also when the others are left to their defaults,
/// like the dummy parameter of `TCInt`'s constructor of an integer.
static const Expr *widenedFrom(const Expr *Made, QualType To,
                               const ASTContext &Context) {
  const auto *Widen = dyn_cast<CXXConstructExpr>(Made);
  if (!Widen || Widen->getNumArgs() < 1 ||
      !llvm::all_of(llvm::drop_begin(Widen->arguments()),
                    [](const Expr *Argument) {
                      return isa<CXXDefaultArgExpr>(Argument);
                    }) ||
      !widensInteger(Widen->getArg(0)->getType(), To, Context))
    return nullptr;
  return Widen->getArg(0);
}

static bool isUnsignedInteger(QualType Type, int Depth);
static bool isIntegerClass(QualType Type, int Depth = 0);

/// What \p Made, if it makes an integer class of a wider one of the same
/// template, `uint256(_Bits | uint512(1))`, also of a signed one, `int512`,
/// narrows, which keeps the low words: the one argument given to its
/// constructor.
static const Expr *narrowedFrom(const Expr *Made, const ASTContext &Context) {
  const auto *Narrow = dyn_cast<CXXConstructExpr>(Made);
  if (!Narrow || Narrow->getNumArgs() < 1 ||
      Narrow->getConstructor()->isCopyOrMoveConstructor() ||
      !llvm::all_of(llvm::drop_begin(Narrow->arguments()),
                    [](const Expr *Argument) {
                      return isa<CXXDefaultArgExpr>(Argument);
                    }))
    return nullptr;
  const QualType From =
      Narrow->getArg(0)->getType().getNonReferenceType().getUnqualifiedType();
  const QualType To = Narrow->getType().getUnqualifiedType();
  const auto *Narrower = dyn_cast_or_null<ClassTemplateSpecializationDecl>(
      To->getAsCXXRecordDecl());
  const auto *Given = dyn_cast_or_null<ClassTemplateSpecializationDecl>(
      From->getAsCXXRecordDecl());
  if (!Narrower || !Given || From->isDependentType() || To->isDependentType() ||
      !Narrower->hasDefinition() || !Given->hasDefinition() ||
      Given->getSpecializedTemplate()->getCanonicalDecl() !=
          Narrower->getSpecializedTemplate()->getCanonicalDecl() ||
      !isIntegerClass(From) || !isIntegerClass(To) ||
      Context.getTypeSize(To) >= Context.getTypeSize(From))
    return nullptr;
  return Narrow->getArg(0);
}

/// Whether changing \p Object, a smart pointer whose copies share what it
/// points to, `pCopy = nullptr` or `pCopy.f_Clear()`, changes only which
/// object it points to, not that object.
static bool sharesPointee(const CXXRecordDecl *Record);

/// The element that \p Iterator, declared by \p Declaration at the first
/// element of a container, is at where it is used: the first, or the one that
/// `++iValue`, `--iValue` or `iValue += 2` after the declaration, in the same
/// block, advance it to before each use. None when that isn't known, or isn't
/// the same at each.
static std::optional<uint64_t> iteratorPosition(const VarDecl *Iterator,
                                                const DeclStmt *Declaration,
                                                ASTContext &Context) {
  const auto Parents = instantiatedParents(*Declaration, Context);
  const auto *Block =
      Parents.empty() ? nullptr : Parents[0].get<CompoundStmt>();
  if (!Block)
    return std::nullopt;
  auto IsIterator = [&](const Expr *E) {
    const auto *Ref = dyn_cast<DeclRefExpr>(E->IgnoreParenImpCasts());
    return Ref && Ref->getDecl() == Iterator;
  };
  // How far a statement moves it, when it does nothing else.
  auto Moves = [&](const Stmt *S) -> std::optional<int64_t> {
    const auto *E = dyn_cast<Expr>(S);
    if (!E)
      return std::nullopt;
    E = E->IgnoreImplicit()->IgnoreParens();
    if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(E);
        Operator && Operator->getNumArgs() >= 1 &&
        IsIterator(Operator->getArg(0))) {
      switch (Operator->getOperator()) {
      case OO_PlusPlus:
        return 1;
      case OO_MinusMinus:
        return -1;
      case OO_PlusEqual:
      case OO_MinusEqual: {
        if (Operator->getNumArgs() != 2)
          return std::nullopt;
        std::optional<llvm::APSInt> Step =
            Operator->getArg(1)->getIntegerConstantExpr(Context);
        if (!Step || Step->getSignificantBits() > 32)
          return std::nullopt;
        return Operator->getOperator() == OO_PlusEqual ? Step->getExtValue()
                                                       : -Step->getExtValue();
      }
      default:
        return std::nullopt;
      }
    }
    return std::nullopt;
  };
  // Whether a use only reads through it, `*iValue`, `iValue->m_Bits` or its
  // conversion to a pointer, rather than moving or changing it.
  std::function<bool(const Stmt *)> Changes = [&](const Stmt *S) {
    if (!S)
      return false;
    if (const auto *Ref = dyn_cast<DeclRefExpr>(S);
        Ref && Ref->getDecl() == Iterator) {
      DynTypedNode Node = DynTypedNode::create(*Ref);
      for (int Depth = 0; Depth < 8; ++Depth) {
        const auto Above = instantiatedParents(Node, Context);
        if (Above.empty())
          return true;
        if (Above[0].get<ParenExpr>() || Above[0].get<ImplicitCastExpr>()) {
          Node = Above[0];
          continue;
        }
        if (const auto *Operator = Above[0].get<CXXOperatorCallExpr>())
          return !(Operator->getOperator() == OO_Star ||
                   Operator->getOperator() == OO_Arrow) ||
                 Operator->getNumArgs() != 1;
        if (const auto *Member = Above[0].get<MemberExpr>()) {
          const auto Call = instantiatedParents(*Member, Context);
          const auto *Conversion =
              dyn_cast<CXXConversionDecl>(Member->getMemberDecl());
          return !(Conversion && !Call.empty() &&
                   Call[0].get<CXXMemberCallExpr>());
        }
        return true;
      }
      return true;
    }
    return llvm::any_of(childrenOnce(S), Changes);
  };
  std::function<bool(const Stmt *)> Uses = [&](const Stmt *S) {
    if (!S)
      return false;
    if (const auto *Ref = dyn_cast<DeclRefExpr>(S);
        Ref && Ref->getDecl() == Iterator)
      return true;
    return llvm::any_of(childrenOnce(S), Uses);
  };
  int64_t Offset = 0;
  std::optional<int64_t> UsedAt;
  bool After = false;
  for (const Stmt *S : Block->body()) {
    if (S == Declaration) {
      After = true;
      continue;
    }
    if (!After)
      continue;
    if (std::optional<int64_t> Step = Moves(S)) {
      Offset += *Step;
      if (Offset < 0 || Offset > (int64_t(1) << 32))
        return std::nullopt;
      continue;
    }
    if (!Uses(S))
      continue;
    if (Changes(S) || (UsedAt && *UsedAt != Offset))
      return std::nullopt;
    UsedAt = Offset;
  }
  return UsedAt.value_or(Offset);
}
static bool onlyPointerChanged(const Expr *Object) {
  const CXXRecordDecl *Record =
      Object->getType().getNonReferenceType()->getAsCXXRecordDecl();
  return Record && sharesPointee(Record);
}

/// Whether \p S, what is written, is one of \p References itself, or a
/// member of it, not what it points to, also as what selects what it points
/// to, `_pInfo` of `_pInfo->m_Bits` through a smart pointer, which pointing
/// it elsewhere, `_pInfo = _pOther`, changes.
static bool namesAny(const Stmt *S, const llvm::StringSet<> &References) {
  return llvm::any_of(
      referencesOf(S, nullptr, /*Written=*/true).keys(),
      [&](StringRef Written) {
        return llvm::any_of(References.keys(), [&](StringRef Reference) {
          return Reference == Written ||
                 (Reference.starts_with(Written) &&
                  (Reference[Written.size()] == '.' ||
                   Reference.drop_front(Written.size()) == "*"));
        });
      });
}

/// Whether assigning \p Target writes one of \p References: like
/// `touchesAny`, but assigning a pointer, `pValue = nullptr`, doesn't write
/// what it points to, `pValue[0]`.
static bool assignsAny(const Expr *Target,
                       const llvm::StringSet<> &References) {
  const Expr *Assigned = Target->IgnoreParenImpCasts();
  // What a lambda called returns a reference to, `fBits() = 0` with `auto
  // fBits = [&]() -> unsigned & { return _Bits; }`, of each of its returns.
  if (const auto *Call = dyn_cast<CallExpr>(Assigned);
      Call && Call->isLValue()) {
    const Expr *Callee = Call->getCallee();
    if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(Call);
        Operator && Operator->getOperator() == OO_Call &&
        Operator->getNumArgs() >= 1)
      Callee = Operator->getArg(0);
    if (const LambdaExpr *Lambda = calledLambda(Callee)) {
      bool Assigns = false;
      std::function<void(const Stmt *)> Returns = [&](const Stmt *S) {
        if (!S || Assigns || isa<LambdaExpr>(S))
          return;
        if (const auto *Return = dyn_cast<ReturnStmt>(S);
            Return && Return->getRetValue())
          Assigns = assignsAny(Return->getRetValue(), References);
        for (const Stmt *Child : childrenOnce(S))
          Returns(Child);
      };
      Returns(Lambda->getBody());
      return Assigns;
    }
    // Also what a function whose body can be seen returns a reference to,
    // `fg_Bits() = 0` of `return gs_Bits;`, or of what it is given,
    // `fg_Ref(_Bits) = 0`.
    static thread_local int s_Depth = 0;
    const FunctionDecl *Function = Call->getDirectCallee();
    const FunctionDecl *Definition =
        Function && !isa<CXXOperatorCallExpr>(Call) &&
                !(isa<CXXMethodDecl>(Function) &&
                  !cast<CXXMethodDecl>(Function)->isStatic())
            ? Function->getDefinition()
            : nullptr;
    if (Definition && Definition->getBody() && s_Depth < 8) {
      ++s_Depth;
      const llvm::scope_exit Done([] { --s_Depth; });
      for (const Expr *Argument : returnedArguments(Call))
        if (assignsAny(Argument, References))
          return true;
      bool Assigns = false;
      std::function<void(const Stmt *)> Returns = [&](const Stmt *S) {
        if (!S || Assigns || isa<LambdaExpr>(S))
          return;
        if (const auto *Return = dyn_cast<ReturnStmt>(S);
            Return && Return->getRetValue()) {
          // Of what isn't its own, a global or a static member.
          const auto *Ref = dyn_cast<DeclRefExpr>(
              Return->getRetValue()->IgnoreParenImpCasts());
          const auto *Global =
              Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
          if (Global && Global->hasGlobalStorage())
            Assigns = assignsAny(Return->getRetValue(), References);
        }
        for (const Stmt *Child : childrenOnce(S))
          Returns(Child);
      };
      Returns(Definition->getBody());
      if (Assigns)
        return true;
    }
  }
  // Also through a local reference to the pointer, `CInfo *&pAlias =
  // _pInfo`.
  for (int Depth = 0; Depth < 8; ++Depth) {
    const auto *Ref = dyn_cast<DeclRefExpr>(Assigned);
    const Expr *Aliased =
        Ref ? ReferenceCollector::aliasedValue(Ref->getDecl()) : nullptr;
    if (!Aliased || !isa<DeclRefExpr>(Aliased->IgnoreParenImpCasts()))
      break;
    Assigned = Aliased->IgnoreParenImpCasts();
  }
  if (const auto *Ref = dyn_cast<DeclRefExpr>(Assigned);
      Ref && Ref->getType()->isPointerType()) {
    const std::string Pointer = ReferenceCollector::declKey(Ref->getDecl());
    const std::string Selector =
        ReferenceCollector::selectorKey(Ref->getDecl());
    return llvm::any_of(References.keys(), [&](StringRef Reference) {
      return Reference == Pointer || Reference == Selector;
    });
  }
  // Also a pointer member, `Borrowed.m_pBits = &Other`, or an element of an
  // array of pointers at a constant index, `Pointers[0] = &Other`, which is
  // pointed elsewhere without writing what it pointed to.
  if (Assigned->getType()->isPointerType()) {
    std::string Pointer;
    if (isa<MemberExpr>(Assigned))
      Pointer = ReferenceCollector::valueKey(Assigned);
    else if (const auto *Element = dyn_cast<ArraySubscriptExpr>(Assigned))
      if (const auto *Array =
              dyn_cast<DeclRefExpr>(Element->getBase()->IgnoreParenImpCasts());
          Array && Array->getType()->isArrayType())
        if (const std::optional<int64_t> Index =
                constantIndex(Element->getIdx()))
          Pointer = ReferenceCollector::declKey(Array->getDecl()) + "[" +
                    std::to_string(*Index) + "]";
    // Like a pointer, the pointer itself, or what it selects, `*` after it.
    if (!Pointer.empty())
      return References.contains(Pointer) || References.contains(Pointer + "*");
  }
  return touchesAny(Target, References);
}

namespace {
/// What a check of the argument is about.
struct CArgumentKeys {
  /// What a condition tests: the variable or member the argument is, or the
  /// expression of an argument that is computed, like `_Value - 1`.
  llvm::StringSet<> Tested;
  /// The variables and members the argument reads, whose writes invalidate
  /// a check.
  llvm::StringSet<> Read;
  /// For an argument that is the complement of a value of its type, like
  /// `CEntryType(~Entry)`, that value, which isn't all ones when the argument
  /// isn't zero.
  llvm::StringSet<> ComplementOf;
  QualType ComplementType;
  /// For an argument that is a value less a constant, like `_Size - 1`, which
  /// is zero only when the value is that constant, the value and the
  /// constant.
  std::string DifferenceOf;
  llvm::APSInt DifferenceZeroAt;
};
} // namespace

static bool writesAny(const Stmt *S, const llvm::StringSet<> &References);
static bool postWritesAny(const Stmt *S, const llvm::StringSet<> &References);

/// Whether \p S is in a branch of an `if constexpr` whose condition, known
/// where it is, discards it, like `if constexpr (false)`.
static bool isInDiscardedBranch(const Stmt *S, ASTContext &Context) {
  DynTypedNode Node = DynTypedNode::create(*S);
  for (int Depth = 0; Depth < 256; ++Depth) {
    const auto Parents = instantiatedParents(Node, Context);
    if (Parents.empty() || Parents[0].get<FunctionDecl>() ||
        Parents[0].get<LambdaExpr>())
      return false;
    const Stmt *Child = Node.get<Stmt>();
    if (const auto *If = Parents[0].get<IfStmt>();
        If && If->isConstexpr() && If->getCond() &&
        !If->getCond()->isValueDependent() && Child &&
        (Child == If->getThen() || Child == If->getElse()))
      if (std::optional<llvm::APSInt> Value =
              If->getCond()->getIntegerConstantExpr(Context);
          Value && Value->isZero() == (Child == If->getThen()))
        return true;
    Node = Parents[0];
  }
  return false;
}

/// Whether converting \p Cast keeps a value that is not zero not zero: a
/// conversion to an integer that is not narrower, also than a bit-field it
/// reads, `unsigned m_Bits : 3` to `uint8`, or one that is not between
/// integers.
static bool keepsNonZero(const CastExpr *Cast, const ASTContext &Context) {
  const QualType To = Cast->getType();
  const QualType From = Cast->getSubExpr()->getType();
  if (To->isDependentType() || From->isDependentType())
    return false;
  if (To->isBooleanType())
    return true;
  // An explicit copy, `uint256(_Bits)` or `static_cast<uint256>(_Bits)`, of
  // what the copy constructor is given.
  if (const auto *Copy = dyn_cast<CXXConstructExpr>(
          Cast->getSubExpr()->IgnoreImplicitAsWritten());
      Copy && Cast->getCastKind() == CK_ConstructorConversion &&
      Copy->getConstructor()->isCopyOrMoveConstructor() &&
      Context.hasSameUnqualifiedType(To, From))
    return true;
  if (!To->isIntegralOrEnumerationType() ||
      !From->isIntegralOrEnumerationType())
    return Cast->getCastKind() == CK_NoOp ||
           Cast->getCastKind() == CK_LValueToRValue;
  unsigned FromWidth = Context.getIntWidth(From);
  if (const FieldDecl *Field = Cast->getSubExpr()->getSourceBitField();
      Field && !Field->getBitWidth()->isValueDependent())
    FromWidth = std::min(FromWidth, Field->getBitWidthValue());
  return Context.getIntWidth(To) >= FromWidth;
}

/// Whether the constant \p Value, an integer or an integer class made of
/// integers, like `TCInt` of its words, has a bit set; std::nullopt when it is
/// made of something else.
static std::optional<bool> hasBitSet(const APValue &Value, int Depth = 0) {
  if (Depth > 8)
    return std::nullopt;
  if (Value.isInt())
    return !Value.getInt().isZero();
  bool Set = false;
  auto Part = [&](const APValue &Inner) {
    const std::optional<bool> PartSet = hasBitSet(Inner, Depth + 1);
    if (!PartSet)
      return false;
    Set |= *PartSet;
    return true;
  };
  if (Value.isStruct()) {
    for (unsigned iBase = 0; iBase < Value.getStructNumBases(); ++iBase)
      if (!Part(Value.getStructBase(iBase)))
        return std::nullopt;
    for (unsigned iField = 0; iField < Value.getStructNumFields(); ++iField)
      if (!Part(Value.getStructField(iField)))
        return std::nullopt;
    return Set;
  }
  if (Value.isArray()) {
    for (unsigned iElement = 0; iElement < Value.getArrayInitializedElts();
         ++iElement)
      if (!Part(Value.getArrayInitializedElt(iElement)))
        return std::nullopt;
    if (Value.hasArrayFiller() && !Part(Value.getArrayFiller()))
      return std::nullopt;
    return Set;
  }
  return std::nullopt;
}

/// The members of its object that the conversion \p Conversion gives, as an
/// integer, `return t_CInt(m_Lower);`, also in the branch an `if constexpr`
/// keeps: one, or of a member that is an integer class itself, `m_Lower` of
/// `uint512`, its members its own conversion gives, in turn. Empty when it
/// gives something else.
static llvm::SmallVector<const FieldDecl *, 4>
givenMembers(const FunctionDecl *Conversion, const ASTContext &Context,
             int Depth = 0) {
  llvm::SmallVector<const FieldDecl *, 4> Given;
  if (!Conversion || !Conversion->getBody() || Depth > 4)
    return Given;
  bool One = true;
  bool Found = false;
  // What \p Value gives of the members, as a return gives it.
  std::function<llvm::SmallVector<const FieldDecl *, 4>(const Expr *)> Gives =
      [&](const Expr *Value) {
      Value = Value ? Value->IgnoreParenCasts() : nullptr;
      // Also converted in turn, `t_CInt(m_Lower)` of a `uint256` member.
      const FunctionDecl *Next = nullptr;
      if (const auto *Call = dyn_cast_or_null<CXXMemberCallExpr>(Value);
          Call && isa_and_nonnull<CXXConversionDecl>(Call->getMethodDecl()) &&
          Call->getImplicitObjectArgument()) {
        Next = Call->getMethodDecl()->getDefinition();
        Value = Call->getImplicitObjectArgument()->IgnoreParenImpCasts();
      }
      const auto *Member = dyn_cast_or_null<MemberExpr>(Value);
      const auto *Field =
          Member && isa<CXXThisExpr>(Member->getBase()->IgnoreParenImpCasts())
              ? dyn_cast<FieldDecl>(Member->getMemberDecl())
              : nullptr;
      llvm::SmallVector<const FieldDecl *, 4> Chain;
      if (Field && !Next && Field->getType()->isIntegerType())
        Chain.push_back(Field);
      else if (Field && Next && Field->getType()->isRecordType()) {
        Chain = givenMembers(Next, Context, Depth + 1);
        if (!Chain.empty())
          Chain.insert(Chain.begin(), Field);
      }
      return Chain;
    };
  // Also a local that the statement before the return gives the member in,
  // with what else it is made of, `Ret = Ret | t_CInt(m_Lower); return Ret;`
  // of a conversion that puts the upper member above it: the bits of the
  // member that are set are set in it.
  auto GivesThrough = [&](const Expr *Value, const Stmt *Before) {
    const auto *Ref = Value ? dyn_cast<DeclRefExpr>(Value->IgnoreParenCasts())
                            : nullptr;
    const auto *Local = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
    const auto *Assign = dyn_cast_or_null<BinaryOperator>(
        dyn_cast_or_null<Expr>(Before)
            ? cast<Expr>(Before)->IgnoreParenImpCasts()
            : nullptr);
    if (!Local || !Local->hasLocalStorage() || !Assign)
      return llvm::SmallVector<const FieldDecl *, 4>();
    auto Names = [&](const Expr *E) {
      const auto *Named = dyn_cast<DeclRefExpr>(E->IgnoreParenCasts());
      return Named && Named->getDecl() == Local;
    };
    if (!Names(Assign->getLHS()))
      return llvm::SmallVector<const FieldDecl *, 4>();
    if (Assign->getOpcode() == BO_OrAssign)
      return Gives(Assign->getRHS());
    const auto *Or =
        Assign->getOpcode() == BO_Assign
            ? dyn_cast<BinaryOperator>(Assign->getRHS()->IgnoreParenCasts())
            : nullptr;
    if (!Or || Or->getOpcode() != BO_Or)
      return llvm::SmallVector<const FieldDecl *, 4>();
    if (Names(Or->getLHS()))
      return Gives(Or->getRHS());
    if (Names(Or->getRHS()))
      return Gives(Or->getLHS());
    return llvm::SmallVector<const FieldDecl *, 4>();
  };
  std::function<void(const Stmt *, const Stmt *)> Visit =
      [&](const Stmt *S, const Stmt *Before) {
    if (!S || !One || isa<LambdaExpr>(S))
      return;
    if (const auto *Return = dyn_cast<ReturnStmt>(S)) {
      llvm::SmallVector<const FieldDecl *, 4> Chain = Gives(Return->getRetValue());
      if (Chain.empty())
        Chain = GivesThrough(Return->getRetValue(), Before);
      if (Chain.empty() || (Found && Chain != Given))
        One = false;
      Given = std::move(Chain);
      Found = true;
      return;
    }
    if (const auto *Compound = dyn_cast<CompoundStmt>(S)) {
      const Stmt *Previous = nullptr;
      for (const Stmt *Child : Compound->body()) {
        Visit(Child, Previous);
        Previous = Child;
      }
      return;
    }
    if (const auto *If = dyn_cast<IfStmt>(S); If && If->isConstexpr())
      if (const std::optional<const Stmt *> Case =
              If->getNondiscardedCase(Context)) {
        Visit(*Case, nullptr);
        return;
      }
    for (const Stmt *Child : childrenOnce(S))
      Visit(Child, nullptr);
  };
  Visit(Conversion->getBody(), nullptr);
  if (!One)
    Given.clear();
  return Given;
}

static bool isUnsignedInteger(QualType Type, int Depth);

/// The bits of the members \p Members, one in the other, of the integer class
/// \p E that are set whatever it is made of: of a constant, `uint256(1)`, and
/// of what `|` sets on either side and `&` on both, which work on each member
/// of an integer class.
/// How many bits of an integer the bits it is known to have set tell, which
/// is as wide as the integers and the members of integer classes are.
static constexpr unsigned KnownBitsWidth = 256;

static llvm::APInt knownSetMemberBits(const Expr *E,
                                      llvm::ArrayRef<const FieldDecl *> Members,
                                      const ASTContext &Context, int Depth) {
  const llvm::APInt None(KnownBitsWidth, 0);
  if (Depth > 16 || Members.empty())
    return None;
  E = E->IgnoreParenImpCasts();
  if (const auto *Full = dyn_cast<FullExpr>(E))
    E = Full->getSubExpr()->IgnoreParenImpCasts();
  if (const auto *Materialize = dyn_cast<MaterializeTemporaryExpr>(E))
    E = Materialize->getSubExpr()->IgnoreParenImpCasts();
  if (const auto *Copy = dyn_cast<CXXConstructExpr>(E);
      Copy && Copy->getNumArgs() == 1 &&
      Copy->getConstructor()->isCopyOrMoveConstructor())
    E = Copy->getArg(0)->IgnoreParenImpCasts();
  // Of a local that can't change, what it is made with, `uint256 const Bits
  // = _Bits | uint256(1);`, of a class without mutable members.
  if (const auto *Ref = dyn_cast<DeclRefExpr>(E))
    if (const auto *Local = dyn_cast<VarDecl>(Ref->getDecl());
        Local && Local->hasLocalStorage() && !isa<ParmVarDecl>(Local) &&
        !Local->getType()->isReferenceType() &&
        Local->getType().isConstQualified() &&
        !Local->getType().isVolatileQualified() && Local->getInit() &&
        Local->getType()->getAsCXXRecordDecl() &&
        !Local->getType()->getAsCXXRecordDecl()->hasMutableFields())
      return knownSetMemberBits(Local->getInit(), Members, Context, Depth + 1);
  if (!E->isValueDependent() && E->getType()->isRecordType() &&
      Members.front()->getParent()->getCanonicalDecl() ==
          E->getType()->getAsRecordDecl()->getCanonicalDecl()) {
    Expr::EvalResult Result;
    if (E->EvaluateAsRValue(Result, Context, /*InConstantContext=*/true) &&
        !Result.HasSideEffects) {
      const APValue *Value = &Result.Val;
      for (const FieldDecl *Member : Members) {
        if (!Value->isStruct() ||
            Member->getFieldIndex() >= Value->getStructNumFields())
          return None;
        Value = &Value->getStructField(Member->getFieldIndex());
      }
      if (Value->isInt())
        return Value->getInt().zextOrTrunc(KnownBitsWidth);
    }
  }
  if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(E);
      Operator && Operator->getNumArgs() == 2 &&
      (Operator->getOperator() == OO_Pipe ||
       Operator->getOperator() == OO_Amp) &&
      isIntegerClass(Operator->getType().getNonReferenceType()) &&
      llvm::all_of(Operator->arguments(), [&](const Expr *Operand) {
        return Context.hasSameUnqualifiedType(
            Operand->getType().getNonReferenceType(),
            Operator->getType().getNonReferenceType());
      })) {
    const llvm::APInt Left =
        knownSetMemberBits(Operator->getArg(0), Members, Context, Depth + 1);
    const llvm::APInt Right =
        knownSetMemberBits(Operator->getArg(1), Members, Context, Depth + 1);
    return Operator->getOperator() == OO_Pipe ? Left | Right : Left & Right;
  }
  return None;
}

/// The bits of the integer \p E that are set whatever it is made of, as far as
/// they can be told: of a constant, and of what `|` sets on either side and
/// `&` on both, `_Bits | 1`, through conversions, which keep the low ones.
static llvm::APInt knownSetBits(const Expr *E, const ASTContext &Context,
                                int Depth = 0) {
  const llvm::APInt None(KnownBitsWidth, 0);
  if (Depth > 16)
    return None;
  E = E->IgnoreParens();
  if (!E->isValueDependent())
    if (std::optional<llvm::APSInt> Value = E->getIntegerConstantExpr(Context))
      return Value->zextOrTrunc(KnownBitsWidth);
  if (const auto *Cast = dyn_cast<CastExpr>(E);
      Cast && Cast->getType()->isIntegerType() &&
      Cast->getSubExpr()->getType()->isIntegerType()) {
    // The low bits, which are what narrowing keeps.
    return knownSetBits(Cast->getSubExpr(), Context, Depth + 1) &
           llvm::APInt::getLowBitsSet(
               KnownBitsWidth,
               std::min(Context.getIntWidth(Cast->getType()), KnownBitsWidth));
  }
  if (const auto *Binary = dyn_cast<BinaryOperator>(E)) {
    if (Binary->getOpcode() == BO_Or)
      return knownSetBits(Binary->getLHS(), Context, Depth + 1) |
             knownSetBits(Binary->getRHS(), Context, Depth + 1);
    if (Binary->getOpcode() == BO_And)
      return knownSetBits(Binary->getLHS(), Context, Depth + 1) &
             knownSetBits(Binary->getRHS(), Context, Depth + 1);
  }
  // An integer class converted to an integer, `uint64(_Bits | uint256(1))`,
  // by a conversion that gives one member of it, `return t_CInt(m_Lower);`,
  // also of a member, `uint512`: the bits of that member that are set in a
  // constant `|` gives it, or both sides of `&`.
  const Expr *Converted = E->IgnoreImplicit();
  if (const auto *Cast = dyn_cast<CastExpr>(Converted);
      Cast && Cast->getCastKind() == CK_UserDefinedConversion)
    Converted = Cast->getSubExpr()->IgnoreImplicit();
  if (const auto *Conversion = dyn_cast<CXXMemberCallExpr>(Converted);
      Conversion && E->getType()->isIntegerType() &&
      isa_and_nonnull<CXXConversionDecl>(Conversion->getMethodDecl()) &&
      Conversion->getImplicitObjectArgument())
    if (const llvm::SmallVector<const FieldDecl *, 4> Given =
            givenMembers(Conversion->getMethodDecl()->getDefinition(), Context);
        !Given.empty()) {
      return knownSetMemberBits(Conversion->getImplicitObjectArgument(), Given,
                                Context, Depth + 1) &
             llvm::APInt::getLowBitsSet(
                 KnownBitsWidth,
                 std::min(Context.getIntWidth(E->getType()), KnownBitsWidth));
    }
  // A local that can't change, what it is made of, `const unsigned Bits =
  // _Bits | 1`.
  if (const auto *Ref = dyn_cast<DeclRefExpr>(E))
    if (const auto *Local = dyn_cast<VarDecl>(Ref->getDecl());
        Local && Local->hasLocalStorage() && !isa<ParmVarDecl>(Local) &&
        Local->getInit() && !Local->getType()->isReferenceType() &&
        Local->getType().isConstQualified() &&
        !Local->getType().isVolatileQualified() &&
        Local->getType()->isIntegerType())
      return knownSetBits(Local->getInit(), Context, Depth + 1);
  return None;
}

/// The member of the integer class \p Class that holds its lowest bits: the
/// one its constructor of an integer makes of it, `TCInt(t_CLower _Lower) :
/// m_Lower(_Lower)`, also the others it takes left to their defaults.
static const FieldDecl *lowMember(const CXXRecordDecl *Class) {
  if (!Class || !Class->hasDefinition())
    return nullptr;
  Class = Class->getDefinition();
  // Also the instantiations of a constructor template, `template <typename
  // t_CType> TCInt(t_CType const &_Convert)`.
  llvm::SmallVector<const CXXConstructorDecl *, 8> Constructors(
      Class->ctors().begin(), Class->ctors().end());
  for (const Decl *Member : Class->decls())
    if (const auto *Template = dyn_cast<FunctionTemplateDecl>(Member);
        Template && isa<CXXConstructorDecl>(Template->getTemplatedDecl()))
      for (const FunctionDecl *Specialization : Template->specializations())
        Constructors.push_back(cast<CXXConstructorDecl>(Specialization));
  for (const CXXConstructorDecl *Constructor : Constructors) {
    if (Constructor->isCopyOrMoveConstructor() ||
        Constructor->getNumParams() < 1 ||
        llvm::any_of(llvm::drop_begin(Constructor->parameters()),
                     [](const ParmVarDecl *Parameter) {
                       return !Parameter->hasDefaultArg();
                     }))
      continue;
    const ParmVarDecl *Given = Constructor->getParamDecl(0);
    const auto *Definition =
        dyn_cast_or_null<CXXConstructorDecl>(Constructor->getDefinition());
    if (!Definition)
      continue;
    // Also the one given its masked low bits, `m_Lower(_Convert & Mask)`, of
    // a constructor that gives another one what is above them, `m_Upper(
    // _Convert >> 63 >> 1)`.
    auto IsGiven = [&](const Expr *E) {
      const auto *Ref =
          dyn_cast_or_null<DeclRefExpr>(E ? E->IgnoreParenImpCasts() : nullptr);
      return Ref && Ref->getDecl() == Given;
    };
    auto IsShiftedDown = [&](const Expr *E) {
      for (int Depth = 0; E && Depth < 8; ++Depth) {
        const auto *Shift = dyn_cast<BinaryOperator>(E->IgnoreParenImpCasts());
        if (!Shift || Shift->getOpcode() != BO_Shr)
          return false;
        E = Shift->getLHS();
        if (IsGiven(E))
          return true;
      }
      return false;
    };
    const FieldDecl *Masked = nullptr;
    bool Shifted = false;
    for (const CXXCtorInitializer *Initializer : Definition->inits()) {
      const Expr *Init = Initializer->isMemberInitializer()
                             ? Initializer->getInit()->IgnoreImplicit()
                             : nullptr;
      // Also copied, of a class, `uint256`.
      if (const auto *Copy = dyn_cast_or_null<CXXConstructExpr>(Init);
          Copy && Copy->getNumArgs() == 1 &&
          Copy->getConstructor()->isCopyOrMoveConstructor())
        Init = Copy->getArg(0)->IgnoreImplicit();
      const auto *Ref = dyn_cast_or_null<DeclRefExpr>(Init);
      if (Ref && Ref->getDecl() == Given &&
          Initializer->getMember()
                  ->getType()
                  .getCanonicalType()
                  .getUnqualifiedType() ==
              Given->getType().getCanonicalType().getUnqualifiedType())
        return Initializer->getMember();
      if (!Init)
        continue;
      // Also made into a member of an integer class, `m_Lower(_Convert &
      // Mask)` of a `uint256`, by its constructor from an integer.
      if (const auto *Convert = dyn_cast<CXXConstructExpr>(Init);
          Convert && Convert->getNumArgs() >= 1 &&
          !Convert->getConstructor()->isCopyOrMoveConstructor() &&
          llvm::all_of(llvm::drop_begin(Convert->arguments()),
                       [](const Expr *Argument) {
                         return isa<CXXDefaultArgExpr>(Argument);
                       }))
        Init = Convert->getArg(0)->IgnoreImplicit();
      if (const auto *Mask =
              dyn_cast<BinaryOperator>(Init->IgnoreParenImpCasts());
          Mask && Mask->getOpcode() == BO_And &&
          (IsGiven(Mask->getLHS()) || IsGiven(Mask->getRHS())))
        Masked = Initializer->getMember();
      else if (IsShiftedDown(Init))
        Shifted = true;
    }
    if (Masked && Shifted)
      return Masked;
  }
  return nullptr;
}

/// An integer of an integer class: the members it is in, one in the other,
/// and the place of its lowest bit in the value.
struct CIntegerWord {
  llvm::SmallVector<const FieldDecl *, 4> Members;
  uint64_t Offset = 0;
};

/// The integers the integer class \p Class is made of, of two members, the
/// one with its lowest bits, as lowMember tells, and the one above it, also
/// each of a class in turn; false when it can't be told.
static bool integerWords(const CXXRecordDecl *Class, const ASTContext &Context,
                         llvm::SmallVectorImpl<CIntegerWord> &Words,
                         llvm::SmallVector<const FieldDecl *, 4> Members = {},
                         uint64_t Offset = 0, int Depth = 0) {
  const FieldDecl *Low = lowMember(Class);
  if (!Low || Depth > 8 || Words.size() > 64)
    return false;
  Class = Class->getDefinition();
  const FieldDecl *High = nullptr;
  unsigned nFields = 0;
  for (const FieldDecl *Field : Class->fields()) {
    ++nFields;
    if (Field != Low)
      High = Field;
  }
  if (nFields != 2 || !High)
    return false;
  auto Add = [&](const FieldDecl *Field, uint64_t At) {
    llvm::SmallVector<const FieldDecl *, 4> Inner = Members;
    Inner.push_back(Field);
    if (Field->getType()->isIntegerType()) {
      Words.push_back({std::move(Inner), At});
      return true;
    }
    return integerWords(Field->getType()->getAsCXXRecordDecl(), Context, Words,
                        std::move(Inner), At, Depth + 1);
  };
  return Add(Low, Offset) &&
         Add(High, Offset + Context.getTypeSize(Low->getType()));
}

/// Whether \p E is not zero by construction, like `Value | 1` or
/// `1 << Shift`.
static bool isNonZeroByConstruction(const Expr *E, const ASTContext &Context) {
  E = E->IgnoreParens();
  // Bound to a `const &` parameter, like the bit scans take.
  if (const auto *Materialize = dyn_cast<MaterializeTemporaryExpr>(E))
    return isNonZeroByConstruction(Materialize->getSubExpr(), Context);
  // Widened into an integer class that keeps its bits, `uint256(_Bits | 1u)`,
  // also from a narrower one, `uint512(uint256(...))`.
  {
    const Expr *Made = E;
    for (int Depth = 0; Depth < 4; ++Depth)
      if (const auto *Bind = dyn_cast<CXXBindTemporaryExpr>(Made))
        Made = Bind->getSubExpr()->IgnoreParens();
      else if (const auto *Cast = dyn_cast<CastExpr>(Made);
               Cast && (Cast->getCastKind() == CK_ConstructorConversion ||
                        (isa<CXXFunctionalCastExpr>(Cast) &&
                         Cast->getCastKind() == CK_NoOp)))
        Made = Cast->getSubExpr()->IgnoreParens();
      else
        break;
    if (const auto *Construct = dyn_cast<CXXConstructExpr>(Made);
        Construct && !Construct->getConstructor()->isCopyOrMoveConstructor()) {
      if (const Expr *From =
              widenedFrom(Construct, Construct->getType(), Context)) {
        // Through conversions to wider integers on the way, not narrower
        // ones.
        const Expr *Given = From->IgnoreParens();
        while (const auto *Widen = dyn_cast<ImplicitCastExpr>(Given)) {
          const QualType Source = Widen->getSubExpr()->getType();
          if (Widen->getCastKind() == CK_IntegralCast &&
              Source->isIntegerType() &&
              Context.getTypeSize(Widen->getType()) >=
                  Context.getTypeSize(Source))
            Given = Widen->getSubExpr()->IgnoreParens();
          else
            break;
        }
        if (isNonZeroByConstruction(Given, Context))
          return true;
      } else if (const Expr *From = narrowedFrom(Construct, Context)) {
        // Narrowed into a narrower one of the same template, `uint256(_Bits
        // | uint512(1))`, which keeps its low bits, as narrowing an integer
        // does.
        llvm::SmallVector<CIntegerWord, 8> Words;
        const uint64_t Kept = Context.getTypeSize(Construct->getType());
        if (integerWords(
                From->getType().getNonReferenceType()->getAsCXXRecordDecl(),
                Context, Words))
          for (const CIntegerWord &Word : Words) {
            if (Word.Offset >= Kept)
              continue;
            const unsigned Width = std::min<uint64_t>(
                {Context.getIntWidth(Word.Members.back()->getType()),
                 Kept - Word.Offset, KnownBitsWidth});
            if (!(knownSetMemberBits(From, Word.Members, Context, 0) &
                  llvm::APInt::getLowBitsSet(KnownBitsWidth, Width))
                     .isZero())
              return true;
          }
      }
    }
  }
  // The initializer of a local made with temporaries, `const CInt Bits =
  // _Bits | CInt(1)`.
  if (const auto *Full = dyn_cast<FullExpr>(E))
    return isNonZeroByConstruction(Full->getSubExpr(), Context);
  // A local that can't change, of what it is made, `const unsigned Bits =
  // _Bits | 1`, also read, `Bits` converted to a value.
  const Expr *Named = E;
  if (const auto *Load = dyn_cast<ImplicitCastExpr>(E);
      Load && Load->getCastKind() == CK_LValueToRValue)
    Named = Load->getSubExpr()->IgnoreParens();
  // Also of an integer class, copied, `CInt(Bits)`.
  else if (const auto *Copy = dyn_cast<CXXConstructExpr>(E);
           Copy && Copy->getNumArgs() == 1 &&
           Copy->getConstructor()->isCopyOrMoveConstructor())
    Named = Copy->getArg(0)->IgnoreParenImpCasts();
  // Each local once, also one that others are made from many times, `const
  // unsigned Bits1 = Bits0 | Bits0;`, and one made from itself, `const
  // unsigned Bits = Bits;`, tells nothing.
  // Also a reference to const bound to a temporary, `unsigned const &Bits =
  // _Bits | 1`, which lives as long as it, and is what it is made with.
  auto BoundTemporary = [](const VarDecl *Local) -> const Expr * {
    if (!Local->getType()->isReferenceType() ||
        !Local->getType().getNonReferenceType().isConstQualified() ||
        Local->getType().getNonReferenceType().isVolatileQualified())
      return nullptr;
    const Expr *Init = Local->getInit();
    if (const auto *Full = dyn_cast<FullExpr>(Init))
      Init = Full->getSubExpr();
    const auto *Materialize =
        dyn_cast<MaterializeTemporaryExpr>(Init->IgnoreParens());
    return Materialize ? Materialize->getSubExpr() : nullptr;
  };
  if (const auto *Ref = dyn_cast<DeclRefExpr>(Named))
    if (const auto *Local = dyn_cast<VarDecl>(Ref->getDecl());
        Local && Local->hasLocalStorage() && !isa<ParmVarDecl>(Local) &&
        Local->getInit() && Local->getType()->isReferenceType())
      // Also of a class without mutable members, which the const reference
      // keeps, `auto const &Bits = _Bits | uint256(1)`.
      if (const Expr *Temporary = BoundTemporary(Local);
          Temporary &&
          (!Local->getType().getNonReferenceType()->isRecordType() ||
           (Local->getType()->getPointeeCXXRecordDecl() &&
            Local->getType()->getPointeeCXXRecordDecl()->hasDefinition() &&
            !Local->getType()
                 ->getPointeeCXXRecordDecl()
                 ->hasMutableFields()))) {
        // Not through itself, `unsigned const &Bits = Bits + 0`.
        static thread_local llvm::SmallPtrSet<const VarDecl *, 4> s_Telling;
        if (!s_Telling.insert(Local).second)
          return false;
        const llvm::scope_exit Done([&] { s_Telling.erase(Local); });
        if (isNonZeroByConstruction(Temporary, Context))
          return true;
      }
  if (const auto *Ref = dyn_cast<DeclRefExpr>(Named))
    if (const auto *Local = dyn_cast<VarDecl>(Ref->getDecl());
        Local && Local->hasLocalStorage() && !isa<ParmVarDecl>(Local) &&
        Local->getInit() && !Local->getType()->isReferenceType() &&
        Local->getType().isConstQualified() &&
        !Local->getType().isVolatileQualified() &&
        // Of a class, one without mutable members, which a const one keeps.
        (!Local->getType()->isRecordType() ||
         (Local->getType()->getAsCXXRecordDecl() &&
          Local->getType()->getAsCXXRecordDecl()->hasDefinition() &&
          !Local->getType()->getAsCXXRecordDecl()->hasMutableFields()))) {
      struct CLocals {
        llvm::DenseMap<const VarDecl *, bool> NonZero;
      };
      auto [It, Inserted] =
          astCache<CLocals>(Context).NonZero.try_emplace(Local, false);
      if (!Inserted) {
        if (It->second)
          return true;
      } else if (isNonZeroByConstruction(Local->getInit(), Context)) {
        astCache<CLocals>(Context).NonZero[Local] = true;
        return true;
      }
    }
  // A value that depends on template parameters can still be made of parts
  // that tell, like `Value | (umint(1) << t_Shift)`.
  if (!E->isValueDependent())
    if (std::optional<llvm::APSInt> Value = E->getIntegerConstantExpr(Context))
      return !Value->isZero();
  // A constant of an integer class, like `constexpr CInt c_One(1)` or
  // `CInt(1)`.
  if (!E->isValueDependent() && E->getType()->isRecordType()) {
    Expr::EvalResult Result;
    if (E->EvaluateAsRValue(Result, Context, /*InConstantContext=*/true) &&
        !Result.HasSideEffects)
      if (const std::optional<bool> Set = hasBitSet(Result.Val))
        return *Set;
  }
  if (const auto *Cast = dyn_cast<CastExpr>(E)) {
    if (keepsNonZero(Cast, Context))
      return isNonZeroByConstruction(Cast->getSubExpr(), Context);
    // A narrowing keeps the low bits, of which one is set, `uint8(_Bits |
    // 1)`.
    return Cast->getType()->isIntegerType() &&
           !knownSetBits(Cast, Context).isZero();
  }
  // An integer class converted to an integer, `uint64(_Bits | uint256(1))`,
  // which keeps the bits of the member it gives.
  if (const auto *Conversion = dyn_cast<CXXMemberCallExpr>(E);
      Conversion && E->getType()->isIntegerType() &&
      isa_and_nonnull<CXXConversionDecl>(Conversion->getMethodDecl()))
    return !knownSetBits(Conversion, Context).isZero();
  // Also of an integer class, `_Value | CInt(1)`, that gives that class: not
  // what gives another type, like a narrower integer, which can lose the bit.
  if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(E);
      Operator && Operator->getOperator() == OO_Pipe &&
      Operator->getNumArgs() == 2) {
    const QualType Result = Operator->getType().getNonReferenceType();
    auto IsOfResult = [&](const Expr *Operand) {
      return Result->isRecordType() &&
             Context.hasSameUnqualifiedType(
                 Operand->getType().getNonReferenceType(), Result);
    };
    return (IsOfResult(Operator->getArg(0)) &&
            isNonZeroByConstruction(Operator->getArg(0), Context)) ||
           (IsOfResult(Operator->getArg(1)) &&
            isNonZeroByConstruction(Operator->getArg(1), Context));
  }
  if (const auto *Binary = dyn_cast<BinaryOperator>(E)) {
    if (Binary->getOpcode() == BO_Or)
      return isNonZeroByConstruction(Binary->getLHS(), Context) ||
             isNonZeroByConstruction(Binary->getRHS(), Context);
    // An odd value keeps its lowest bit for every shift that is defined, but
    // shifting out every set bit of other values gives zero.
    if (Binary->getOpcode() == BO_Shl) {
      const Expr *Left = Binary->getLHS();
      if (Left->isValueDependent())
        return false;
      if (std::optional<llvm::APSInt> Value =
              Left->getIntegerConstantExpr(Context))
        return (*Value)[0];
    }
  }
  // So does one of an integer class made of an odd integer, `uint256(1) <<
  // Shift`, which gives that class, for every shift narrower than it, as an
  // integer does for every shift that is defined.
  if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(E);
      Operator && Operator->getOperator() == OO_LessLess &&
      Operator->getNumArgs() == 2 && Operator->getType()->isRecordType() &&
      Context.hasSameUnqualifiedType(
          Operator->getType().getNonReferenceType(),
          Operator->getArg(0)->getType().getNonReferenceType())) {
    const Expr *Made = Operator->getArg(0);
    for (int Depth = 0; Depth < 8; ++Depth) {
      Made = Made->IgnoreParenImpCasts();
      if (const auto *Materialize = dyn_cast<MaterializeTemporaryExpr>(Made))
        Made = Materialize->getSubExpr();
      else if (const auto *Bind = dyn_cast<CXXBindTemporaryExpr>(Made))
        Made = Bind->getSubExpr();
      else if (const auto *Cast = dyn_cast<CXXFunctionalCastExpr>(Made))
        Made = Cast->getSubExpr();
      else
        break;
    }
    // Made of one integer, not of words, like the upper and lower ones of
    // `uint256(1, 0)`.
    if (const auto *Construct = dyn_cast<CXXConstructExpr>(Made);
        Construct && Construct->getNumArgs() >= 1 &&
        !Construct->getConstructor()->isCopyOrMoveConstructor() &&
        llvm::all_of(llvm::drop_begin(Construct->arguments()),
                     [](const Expr *Argument) {
                       return isa<CXXDefaultArgExpr>(Argument);
                     })) {
      const Expr *Given = Construct->getArg(0)->IgnoreImpCasts();
      if (!Given->isValueDependent() && Given->getType()->isIntegerType())
        if (std::optional<llvm::APSInt> Value =
                Given->getIntegerConstantExpr(Context))
          return (*Value)[0];
    }
  }
  return false;
}

namespace {
/// What a condition tells about zero in the values it reads.
enum class EZero {
  Excluded, ///< They are not zero.
  Possible, ///< One of them can be zero.
  Unknown   ///< It can't be told; taken as a check, best effort.
};
} // namespace

/// Whether \p Made makes an object of a class that can't be copied as it is,
/// like a unique pointer, of what a call gives of another class, not one it
/// moves from, `TCUniquePointer<CState> pCopy(fg_Copy(_pState))`, which
/// then has its own of what it points to.
static bool isOwnCopy(const Expr *Made) {
  const auto *Construct = dyn_cast_or_null<CXXConstructExpr>(
      Made ? Made->IgnoreImplicit() : nullptr);
  const CXXRecordDecl *Record = Construct && Construct->getNumArgs() == 1
                                    ? Construct->getType()->getAsCXXRecordDecl()
                                    : nullptr;
  if (!Record || !Record->hasDefinition() ||
      llvm::none_of(Record->getDefinition()->ctors(),
                    [](const CXXConstructorDecl *Constructor) {
                      return Constructor->isCopyConstructor() &&
                             Constructor->isDeleted();
                    }))
    return false;
  const Expr *Given =
      Construct->getArg(0)->IgnoreImplicit()->IgnoreParenImpCasts();
  const CXXRecordDecl *From = Given->getType()->getAsCXXRecordDecl();
  return isa<CallExpr>(Given) && Given->isPRValue() && From &&
         From->getCanonicalDecl() != Record->getCanonicalDecl();
}

/// The object whose only member \p E reads, an integer, also an integer class,
/// also a signed one, through a conversion that gives it, `umint(_Bits)` of a
/// `TCAutoClear<umint>` or `int256(_Bits)` of a `zint256`, whose value is
/// then the value of the object, or null.
static const Expr *wrapperOf(const Expr *E) {
  for (int Depth = 0; Depth < 8; ++Depth) {
    E = E->IgnoreParens();
    const auto *Cast = dyn_cast<CastExpr>(E);
    if (!Cast || !llvm::is_contained({CK_UserDefinedConversion, CK_NoOp,
                                      CK_LValueToRValue},
                                     Cast->getCastKind()))
      break;
    E = Cast->getSubExpr();
  }
  const auto *Call = dyn_cast<CXXMemberCallExpr>(E);
  const auto *Method =
      Call ? dyn_cast_or_null<CXXConversionDecl>(Call->getMethodDecl())
           : nullptr;
  const FunctionDecl *Definition = Method ? Method->getDefinition() : nullptr;
  const auto *Body =
      Definition ? dyn_cast_or_null<CompoundStmt>(Definition->getBody())
                 : nullptr;
  if (!Body || Body->size() != 1 || !Call->getImplicitObjectArgument())
    return nullptr;
  const auto *Return = dyn_cast<ReturnStmt>(Body->body_front());
  const auto *Member =
      Return && Return->getRetValue()
          ? dyn_cast<MemberExpr>(Return->getRetValue()->IgnoreParenImpCasts())
          : nullptr;
  const CXXRecordDecl *Record = Method->getParent();
  if (!Member || !isa<CXXThisExpr>(Member->getBase()->IgnoreParenImpCasts()) ||
      Record->getNumBases() != 0 || Record->isUnion() ||
      !(Member->getType()->isIntegerType() ||
        isIntegerClass(Member->getType())))
    return nullptr;
  const auto Fields = Record->fields();
  if (Fields.empty() || std::next(Fields.begin()) != Fields.end() ||
      Member->getMemberDecl() != *Fields.begin())
    return nullptr;
  return Call->getImplicitObjectArgument();
}

/// What \p E, a read through a local pointer, `*pBits`, reads: what the
/// pointer is pointed at there, `_Bits` of `unsigned *pBits = &_Bits`, or
/// null when that can't be told.
static const Expr *pointeeOf(const Expr *E) {
  const auto *Deref = dyn_cast<UnaryOperator>(E->IgnoreParenImpCasts());
  const auto *Ref =
      Deref && Deref->getOpcode() == UO_Deref
          ? dyn_cast<DeclRefExpr>(Deref->getSubExpr()->IgnoreParenImpCasts())
          : nullptr;
  const auto *Pointer = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
  if (!Pointer || !Pointer->hasLocalStorage() ||
      !Pointer->getType()->isPointerType())
    return nullptr;
  const Expr *Value = assignedValueAt(Pointer, Ref);
  const auto *Address =
      Value ? dyn_cast<UnaryOperator>(Value->IgnoreParenImpCasts()) : nullptr;
  return Address && Address->getOpcode() == UO_AddrOf
             ? Address->getSubExpr()->IgnoreParenImpCasts()
             : nullptr;
}

/// Whether \p E is zero when what \p Keys tests is, so that it not being
/// zero tells that the argument isn't: the argument itself, also converted,
/// or made from it by what keeps zero, like `Value & Mask`, `Value >> 1` or
/// `--Value`, but not `Value - 1`.
static bool isZeroWithArgument(const Expr *E, const CArgumentKeys &Keys,
                               const ASTContext &Context) {
  for (int Depth = 0; Depth < 32; ++Depth) {
    // The argument can be one of the conversions, like the one inside a
    // `static_cast<uint8>(Value)`.
    if (Keys.Tested.contains(ReferenceCollector::expressionKey(E, Context)))
      return true;
    // Also an element by an index operator, `_Bits[0]`, which a reference a
    // getter gives to it is, by the element.
    if (const auto *Element = dyn_cast<CXXOperatorCallExpr>(E);
        Element && Element->getOperator() == OO_Subscript)
      if (const std::string Object = ReferenceCollector::objectKey(Element);
          !Object.empty() && Keys.Tested.contains(Object))
        return true;
    if (const auto *Paren = dyn_cast<ParenExpr>(E)) {
      E = Paren->getSubExpr();
      continue;
    }
    if (const auto *Materialize = dyn_cast<MaterializeTemporaryExpr>(E)) {
      E = Materialize->getSubExpr();
      continue;
    }
    if (const auto *Cast = dyn_cast<CastExpr>(E)) {
      E = Cast->getSubExpr();
      continue;
    }
    // Its `operator bool` or conversion to an integer.
    if (const auto *Call = dyn_cast<CXXMemberCallExpr>(E);
        Call && isa_and_nonnull<CXXConversionDecl>(Call->getMethodDecl())) {
      E = Call->getImplicitObjectArgument();
      continue;
    }
    // Also through a local pointer to it, `*pBits`.
    if (const Expr *Pointee = pointeeOf(E)) {
      E = Pointee;
      continue;
    }
    // A word of an integer class, `_Bits.m_Lower` of a `uint256`, which is
    // zero when the whole is.
    if (const auto *Word = dyn_cast<MemberExpr>(E);
        Word && !Word->isArrow() && isa<FieldDecl>(Word->getMemberDecl()) &&
        Word->getBase()->getType()->isRecordType() &&
        (Word->getType()->isIntegerType() || Word->getType()->isRecordType())) {
      if (const std::string Value = ReferenceCollector::valueKey(E);
          !Value.empty() && Keys.Tested.contains(Value))
        return true;
      E = Word->getBase();
      continue;
    }
    // Its conversion to an integer class, `uint256(_Bits)`, also a copy of
    // one, which is zero when it is.
    if (const auto *Make = dyn_cast<CXXConstructExpr>(E);
        Make && Make->getNumArgs() >= 1 &&
        isUnsignedInteger(Make->getType(), 0) &&
        llvm::all_of(llvm::drop_begin(Make->arguments()),
                     [](const Expr *Argument) {
                       return isa<CXXDefaultArgExpr>(Argument);
                     }) &&
        (Make->getArg(0)->getType()->isIntegerType() ||
         isUnsignedInteger(Make->getArg(0)->getType().getNonReferenceType(),
                           0))) {
      E = Make->getArg(0);
      continue;
    }
    // Also the word a free function gives a reference to of what it is given,
    // `fg_GetLower(_Bits)` of `return _Integer.m_Lower;`, like the member.
    if (const auto *Call = dyn_cast<CallExpr>(E)) {
      if (const auto [Value, iParameter] = memberOfGivenReturnedAt(Call);
          Value) {
        if (const std::string Word = ReferenceCollector::objectKey(Call);
            !Word.empty() && Keys.Tested.contains(Word))
          return true;
        E = Call->getArg(iParameter);
        continue;
      }
    }
    if (isa<DeclRefExpr, MemberExpr>(E)) {
      const std::string Value = ReferenceCollector::valueKey(E);
      return !Value.empty() && Keys.Tested.contains(Value);
    }
    // What is assigned is the value tested, `(Value = Next) != 0`, also after
    // `--Value`.
    if (const auto *Unary = dyn_cast<UnaryOperator>(E);
        Unary && Unary->isPrefix() && Unary->isIncrementDecrementOp()) {
      E = Unary->getSubExpr();
      continue;
    }
    // Also overloaded, of an integer class, `--_Bits`, whose one argument is
    // what it changes and gives.
    if (const auto *Step = dyn_cast<CXXOperatorCallExpr>(E);
        Step && Step->getNumArgs() == 1 &&
        (Step->getOperator() == OO_PlusPlus ||
         Step->getOperator() == OO_MinusMinus)) {
      E = Step->getArg(0);
      continue;
    }
    // And a compound assignment of its own, `(_Bits &= _Bits - 1) != 0`,
    // which gives its object.
    if (const auto *Assign = dyn_cast<CXXOperatorCallExpr>(E);
        Assign && Assign->isAssignmentOp() && Assign->getNumArgs() == 2 &&
        Assign->isLValue() &&
        isa_and_nonnull<CXXMethodDecl>(Assign->getDirectCallee())) {
      E = Assign->getArg(0);
      continue;
    }
    // A negation is zero only when what it negates is, `-_Value != 0`, and
    // so is a double not, `!!_Value != 0`, but not a single one.
    if (const auto *Unary = dyn_cast<UnaryOperator>(E)) {
      if (Unary->getOpcode() == UO_Minus || Unary->getOpcode() == UO_Plus) {
        E = Unary->getSubExpr();
        continue;
      }
      if (const auto *Inner = dyn_cast<UnaryOperator>(
              Unary->getSubExpr()->IgnoreParenImpCasts());
          Unary->getOpcode() == UO_LNot && Inner &&
          Inner->getOpcode() == UO_LNot) {
        E = Inner->getSubExpr();
        continue;
      }
    }
    // Also overloaded, like of an integer class, `-_Bits` of a `uint256`.
    if (const auto *Sign = dyn_cast<CXXOperatorCallExpr>(E);
        Sign && Sign->getNumArgs() == 1 &&
        (Sign->getOperator() == OO_Minus || Sign->getOperator() == OO_Plus)) {
      E = Sign->getArg(0);
      continue;
    }
    if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(E);
        Operator && Operator->getNumArgs() == 2) {
      switch (Operator->getOperator()) {
      case OO_Amp:
      case OO_Star:
        return isZeroWithArgument(Operator->getArg(0), Keys, Context) ||
               isZeroWithArgument(Operator->getArg(1), Keys, Context);
      case OO_LessLess:
      case OO_GreaterGreater:
      case OO_Slash:
      case OO_Percent:
        E = Operator->getArg(0);
        continue;
      default:
        return false;
      }
    }
    if (const auto *Binary = dyn_cast<BinaryOperator>(E)) {
      if (Binary->isAssignmentOp()) {
        E = Binary->getLHS();
        continue;
      }
      switch (Binary->getOpcode()) {
      case BO_And:
      case BO_Mul:
        return isZeroWithArgument(Binary->getLHS(), Keys, Context) ||
               isZeroWithArgument(Binary->getRHS(), Keys, Context);
      case BO_Shl:
      case BO_Shr:
      case BO_Div:
      case BO_Rem:
        E = Binary->getLHS();
        continue;
      default:
        return false;
      }
    }
    return false;
  }
  return false;
}

/// What comparing \p Left and \p Right, for inequality with \p NotEqual,
/// tells when it is \p Branch: like `Value != 0`, not zero when true. What
/// is compared with zero must be zero when the argument is: `Value - 1 != 0`
/// doesn't tell that `Value` isn't.
static EZero comparedWithZero(const Expr *Left, const Expr *Right,
                              bool NotEqual, bool Branch,
                              const CArgumentKeys &Keys,
                              const ASTContext &Context) {
  auto IsZero = [&](const Expr *Side) {
    // Also converted to a class, like the `CInt128` that `Value == 0`
    // compares with, also a constant of one, `gc_Zero`.
    for (int Depth = 0; Depth < 8; ++Depth) {
      Side = Side->IgnoreParenImpCasts();
      if (const auto *Ref = dyn_cast<DeclRefExpr>(Side))
        if (const auto *Constant = dyn_cast<VarDecl>(Ref->getDecl());
            Constant && Constant->getType().isConstQualified() &&
            Constant->getInit() && Constant->getType()->isRecordType()) {
          Side = Constant->getInit();
          continue;
        }
      if (const auto *Materialize = dyn_cast<MaterializeTemporaryExpr>(Side))
        Side = Materialize->getSubExpr();
      else if (const auto *Bind = dyn_cast<CXXBindTemporaryExpr>(Side))
        Side = Bind->getSubExpr();
      // Also one whose other arguments are defaulted, like the dummy of
      // `TCInt(t_CValue, CDummy const & = {})`. What a constant one makes is
      // what it is, which can differ from what it is given, `CByte(256)`.
      else if (const auto *Construct = dyn_cast<CXXConstructExpr>(Side);
               Construct && Construct->getNumArgs() >= 1 &&
               llvm::all_of(llvm::drop_begin(Construct->arguments()),
                            [](const Expr *Argument) {
                              return isa<CXXDefaultArgExpr>(Argument);
                            }) &&
               !Construct->getConstructor()->isCopyOrMoveConstructor()) {
        Expr::EvalResult Made;
        if (!Construct->isValueDependent() &&
            Construct->EvaluateAsRValue(Made, Context))
          return hasBitSet(Made.Val) == false;
        Side = Construct->getArg(0);
      }
      else if (const auto *Cast = dyn_cast<CXXFunctionalCastExpr>(Side))
        Side = Cast->getSubExpr();
      else
        break;
    }
    if (Side->isValueDependent())
      return false;
    // A constant of an integer class made of several words, `CInt(0, 0)`,
    // or made by default, is zero when none of its bits is set.
    if (Side->getType()->isRecordType()) {
      Expr::EvalResult Result;
      return Side->EvaluateAsRValue(Result, Context) &&
             hasBitSet(Result.Val) == false;
    }
    // Also a floating point one, `_Bits == 0.0`, which only zero converts to.
    if (Side->getType()->isRealFloatingType()) {
      Expr::EvalResult Result;
      return Side->EvaluateAsRValue(Result, Context) && Result.Val.isFloat() &&
             Result.Val.getFloat().isZero();
    }
    std::optional<llvm::APSInt> Value = Side->getIntegerConstantExpr(Context);
    return Value && Value->isZero();
  };
  const Expr *Compared = IsZero(Right) ? Left : (IsZero(Left) ? Right : nullptr);
  if (!Compared)
    return EZero::Unknown;
  if (!isZeroWithArgument(Compared, Keys, Context))
    return EZero::Possible;
  return NotEqual == Branch ? EZero::Excluded : EZero::Possible;
}

/// Whether \p E is made from the argument, like `_Bits | 1`, also by what
/// doesn't keep zero.
static bool madeFromArgument(const Expr *E, const CArgumentKeys &Keys,
                             const ASTContext &Context) {
  if (isZeroWithArgument(E, Keys, Context))
    return true;
  return llvm::any_of(childrenOnce(E), [&](const Stmt *Child) {
    const auto *Part = dyn_cast_or_null<Expr>(Child);
    return Part && madeFromArgument(Part, Keys, Context);
  });
}

/// Whether \p Type is an unsigned integer, or a class of integers made of
/// unsigned words, like `uint256`, a `TCInt` of unsigned words, which is never
/// negative.
static bool isUnsignedInteger(QualType Type, int Depth = 0) {
  Type = Type.getNonReferenceType().getCanonicalType();
  if (Type->isUnsignedIntegerOrEnumerationType())
    return true;
  const CXXRecordDecl *Record = Type->getAsCXXRecordDecl();
  if (!Record || Depth > 8 || !Record->hasDefinition() || Record->isUnion() ||
      Record->getNumBases() != 0)
    return false;
  bool Any = false;
  for (const FieldDecl *Field : Record->fields()) {
    if (!isUnsignedInteger(Field->getType(), Depth + 1))
      return false;
    Any = true;
  }
  return Any;
}

/// Whether \p Type is a class of integers made of words of integers, signed
/// or not, like `int512`, which works on each word for `|` and `&` and keeps
/// its low words when narrowed.
static bool isIntegerClass(QualType Type, int Depth) {
  Type = Type.getNonReferenceType().getCanonicalType();
  const CXXRecordDecl *Record = Type->getAsCXXRecordDecl();
  if (!Record || Depth > 8 || !Record->hasDefinition() || Record->isUnion() ||
      Record->getNumBases() != 0)
    return false;
  bool Any = false;
  for (const FieldDecl *Field : Record->fields()) {
    const QualType Word = Field->getType().getCanonicalType();
    if (!Word->isIntegerType() && !isIntegerClass(Word, Depth + 1))
      return false;
    Any = true;
  }
  return Any;
}

/// What comparing the argument with a constant that isn't zero, with
/// \p Opcode, like `_Value != 1` or `_Value < 10`, tells when it is
/// \p Branch: whether zero passes the comparison is known. Unknown when it
/// isn't such a comparison.
static EZero comparedWithConstant(const Expr *Left, const Expr *Right,
                                  BinaryOperatorKind Opcode, bool Branch,
                                  const CArgumentKeys &Keys,
                                  const ASTContext &Context) {
  // With a floating point constant, `_Bits < 0.5`, which zero passes when
  // `0.0 < 0.5` does.
  for (const bool ConstantOnLeft : {false, true}) {
    const Expr *Side = ConstantOnLeft ? Left : Right;
    const Expr *Compared = ConstantOnLeft ? Right : Left;
    Expr::EvalResult Result;
    if (!Side->getType()->isRealFloatingType() || Side->isValueDependent() ||
        !Side->EvaluateAsRValue(Result, Context) || !Result.Val.isFloat())
      continue;
    const llvm::APFloat &Value = Result.Val.getFloat();
    if (Value.isZero() || !isZeroWithArgument(Compared, Keys, Context))
      return EZero::Unknown;
    const llvm::APFloat::cmpResult Order =
        llvm::APFloat::getZero(Value.getSemantics()).compare(Value);
    // As zero is compared: `0.0 < 0.5` with the constant on the right.
    auto Is = [&](llvm::APFloat::cmpResult Wanted) { return Order == Wanted; };
    const bool Less = Is(ConstantOnLeft ? llvm::APFloat::cmpGreaterThan
                                        : llvm::APFloat::cmpLessThan);
    const bool Greater = Is(ConstantOnLeft ? llvm::APFloat::cmpLessThan
                                           : llvm::APFloat::cmpGreaterThan);
    const bool Equal = Is(llvm::APFloat::cmpEqual);
    bool ZeroPasses = false;
    switch (Opcode) {
    case BO_EQ:
      ZeroPasses = Equal;
      break;
    case BO_NE:
      ZeroPasses = !Equal;
      break;
    case BO_LT:
      ZeroPasses = Less;
      break;
    case BO_GT:
      ZeroPasses = Greater;
      break;
    case BO_LE:
      ZeroPasses = Less || Equal;
      break;
    case BO_GE:
      ZeroPasses = Greater || Equal;
      break;
    default:
      return EZero::Unknown;
    }
    return ZeroPasses == Branch ? EZero::Possible : EZero::Excluded;
  }
  // Also of an integer class of the type of the other side made from one,
  // `_Bits < CInt(10)`, also converted, `_Bits < 10` of `operator<(CInt const
  // &)`, which is that integer.
  auto Constant = [&](const Expr *Side,
                      const Expr *Other) -> std::optional<llvm::APSInt> {
    if (Side->isValueDependent())
      return std::nullopt;
    if (Side->getType()->isRecordType() &&
        Context.hasSameUnqualifiedType(
            Side->getType().getNonReferenceType(),
            Other->getType().getNonReferenceType())) {
      const Expr *Made = Side;
      for (int Depth = 0; Depth < 8; ++Depth) {
        Made = Made->IgnoreParenImpCasts();
        if (const auto *Materialize = dyn_cast<MaterializeTemporaryExpr>(Made))
          Made = Materialize->getSubExpr();
        else if (const auto *Bind = dyn_cast<CXXBindTemporaryExpr>(Made))
          Made = Bind->getSubExpr();
        else if (const auto *Cast = dyn_cast<CXXFunctionalCastExpr>(Made))
          Made = Cast->getSubExpr();
        else
          break;
      }
      const auto *Construct = dyn_cast<CXXConstructExpr>(Made);
      // The integer an object of a class of integers holds, like `CInt(10)`
      // of a `CInt` of an upper and a lower word, as what tells how it
      // compares with zero: a word that is negative, of the one signed word
      // that a signed one has, `TCInt<int64, uint64>`, or else the first that
      // isn't zero, or when there is one word, that one.
      // Also of words that are such classes themselves, `TCInt<uint256,
      // uint256>`, by the integers in them.
      auto Held = [](const APValue &Value) -> std::optional<llvm::APSInt> {
        llvm::SmallVector<llvm::APSInt, 8> Words;
        std::function<bool(const APValue &, int)> Collect =
            [&](const APValue &Part, int Depth) {
              if (Part.isInt()) {
                Words.push_back(Part.getInt());
                return true;
              }
              if (Depth > 8 || !Part.isStruct() ||
                  Part.getStructNumBases() != 0 ||
                  Part.getStructNumFields() == 0)
                return false;
              for (unsigned iField = 0; iField < Part.getStructNumFields();
                   ++iField)
                if (!Collect(Part.getStructField(iField), Depth + 1))
                  return false;
              return true;
            };
        if (!Value.isStruct() || !Collect(Value, 0) || Words.empty())
          return std::nullopt;
        if (Words.size() == 1)
          return Words[0];
        std::optional<llvm::APSInt> NonZero;
        std::optional<llvm::APSInt> Signed;
        for (const llvm::APSInt &Word : Words) {
          if (Word.isSigned()) {
            if (Signed)
              return std::nullopt;
            Signed = Word;
          }
          if (!NonZero && !Word.isZero())
            NonZero = Word;
        }
        if (Signed && Signed->isNegative())
          return Signed;
        return NonZero;
      };
      // A named constant of it, `gc_One`, is the integer it holds, when it
      // holds one, or zero when no bit of it is set; a temporary made of its
      // words, `CInt(0, 1)`, is too, below.
      if (!Construct || Construct->getConstructor()->isCopyOrMoveConstructor()) {
        Expr::EvalResult Named;
        if (!isa<DeclRefExpr, MemberExpr>(Made) ||
            !Made->EvaluateAsRValue(Named, Context) || Named.HasSideEffects)
          return std::nullopt;
        if (hasBitSet(Named.Val) == false)
          return llvm::APSInt::get(0);
        return Held(Named.Val);
      }
      // What a constant one makes is zero, `CByte(256)`, also when what it
      // is given isn't, and otherwise the integer it holds, `CUnsigned(-1)`
      // of an `unsigned`, which is what is compared, rather than what it is
      // given.
      Expr::EvalResult Whole;
      if (!Construct->isValueDependent() &&
          Construct->EvaluateAsRValue(Whole, Context)) {
        if (hasBitSet(Whole.Val) == false)
          return llvm::APSInt::get(0);
        return Held(Whole.Val);
      }
      // One that can't be evaluated can keep anything of what it is given.
      return std::nullopt;
    }
    if (!Side->getType()->isIntegralOrEnumerationType())
      return std::nullopt;
    return Side->getIntegerConstantExpr(Context);
  };
  std::optional<llvm::APSInt> Value = Constant(Right, Left);
  const Expr *Compared = Left;
  if (!Value) {
    Value = Constant(Left, Right);
    Compared = Right;
    // The constant on the left, `10 > _Value`, like `_Value < 10`.
    switch (Opcode) {
    case BO_LT:
      Opcode = BO_GT;
      break;
    case BO_GT:
      Opcode = BO_LT;
      break;
    case BO_LE:
      Opcode = BO_GE;
      break;
    case BO_GE:
      Opcode = BO_LE;
      break;
    default:
      break;
    }
  }
  // Compared with what isn't constant, as unsigned values, which zero always
  // passes, `_Bits <= _Other`, or never does, `_Bits > _Other`, also with
  // what is never zero, `_Bits >= (_Other | 1)`; otherwise it can't be told.
  if (!Value) {
    auto Decide = [&](const Expr *Argument, const Expr *Other,
                      BinaryOperatorKind Operator) -> std::optional<EZero> {
      // Also both promoted to `int` from unsigned types, `uint8`, which are
      // never negative, and classes of unsigned words, `uint256`.
      auto Unsigned = [](const Expr *E) {
        return isUnsignedInteger(E->IgnoreImpCasts()->getType());
      };
      if (!(isUnsignedInteger(Argument->getType()) ||
            (Unsigned(Argument) && Unsigned(Other))) ||
          !isZeroWithArgument(Argument, Keys, Context))
        return std::nullopt;
      const bool OtherNonZero = isNonZeroByConstruction(Other, Context);
      std::optional<bool> ZeroPasses;
      switch (Operator) {
      case BO_LE:
        ZeroPasses = true;
        break;
      case BO_GT:
        ZeroPasses = false;
        break;
      case BO_LT:
      case BO_NE:
        if (OtherNonZero)
          ZeroPasses = true;
        break;
      case BO_GE:
      case BO_EQ:
        if (OtherNonZero)
          ZeroPasses = false;
        break;
      default:
        break;
      }
      if (!ZeroPasses)
        return std::nullopt;
      return *ZeroPasses == Branch ? EZero::Possible : EZero::Excluded;
    };
    // The operator was turned for the right side being compared.
    BinaryOperatorKind LeftOpcode = Opcode;
    switch (Opcode) {
    case BO_LT:
      LeftOpcode = BO_GT;
      break;
    case BO_GT:
      LeftOpcode = BO_LT;
      break;
    case BO_LE:
      LeftOpcode = BO_GE;
      break;
    case BO_GE:
      LeftOpcode = BO_LE;
      break;
    default:
      break;
    }
    if (std::optional<EZero> Zero = Decide(Right, Left, Opcode))
      return *Zero;
    if (std::optional<EZero> Zero = Decide(Left, Right, LeftOpcode))
      return *Zero;
    return EZero::Unknown;
  }
  if (Value->isZero())
    return EZero::Unknown;
  // Made from it by what doesn't keep zero, `(_Bits | 1) == 1`, which is
  // true for zero too: it tells nothing.
  if (!isZeroWithArgument(Compared, Keys, Context))
    return madeFromArgument(Compared, Keys, Context) ? EZero::Possible
                                                     : EZero::Unknown;
  // Whether zero passes, `0 < 10`.
  const llvm::APSInt Zero(llvm::APInt(Value->getBitWidth(), 0),
                          Value->isUnsigned());
  bool ZeroPasses = false;
  switch (Opcode) {
  case BO_EQ:
    ZeroPasses = false;
    break;
  case BO_NE:
    ZeroPasses = true;
    break;
  case BO_LT:
    ZeroPasses = Zero < *Value;
    break;
  case BO_GT:
    ZeroPasses = Zero > *Value;
    break;
  case BO_LE:
    ZeroPasses = Zero <= *Value;
    break;
  case BO_GE:
    ZeroPasses = Zero >= *Value;
    break;
  default:
    return EZero::Unknown;
  }
  return ZeroPasses == Branch ? EZero::Possible : EZero::Excluded;
}

/// What comparing \p Left and \p Right with \p Opcode tells: with zero,
/// like `Value != 0` or `Value > 0`, or else with a constant that isn't zero,
/// like `Value != 1`.
static EZero comparedWith(const Expr *Left, const Expr *Right,
                          BinaryOperatorKind Opcode, bool Branch,
                          const CArgumentKeys &Keys,
                          const ASTContext &Context) {
  const bool Excludes = Opcode == BO_NE || Opcode == BO_LT || Opcode == BO_GT;
  const EZero Zero =
      comparedWithZero(Left, Right, Excludes, Branch, Keys, Context);
  return Zero != EZero::Unknown
             ? Zero
             : comparedWithConstant(Left, Right, Opcode, Branch, Keys, Context);
}

/// What comparing \p Left and \p Right, for inequality with \p NotEqual,
/// tells about an argument that is the complement of one of them, when the
/// other is all ones of its type: like `Entry != mcp_FullySet` that the
/// complement of `Entry` isn't zero when true. Unknown when it isn't such a
/// comparison.
static EZero comparedWithAllOnes(const Expr *Left, const Expr *Right,
                                 bool NotEqual, bool Branch,
                                 const CArgumentKeys &Keys,
                                 const ASTContext &Context) {
  if (Keys.ComplementType.isNull() || Keys.ComplementType->isDependentType() ||
      (!Keys.ComplementType->isIntegralOrEnumerationType() &&
       !Keys.ComplementType->isRecordType()))
    return EZero::Unknown;
  const bool Integral = Keys.ComplementType->isIntegralOrEnumerationType();
  const unsigned Width = Integral ? Context.getIntWidth(Keys.ComplementType) : 0;
  auto IsValue = [&](const Expr *Side) {
    Side = Side->IgnoreParenImpCasts();
    // Also through a local reference to it, `Bits` of `unsigned const &Bits
    // = _Bits`, which is the value it is bound to.
    // Also converted explicitly to the type of the complement, keeping its
    // value, `uint64(_Used)` of a `uint64`, also of a wrapper of one,
    // `zuint64`, through its conversion.
    for (int Depth = 0; Side && Depth < 8; ++Depth) {
      if (const auto *Cast = dyn_cast<ExplicitCastExpr>(Side);
          Cast && Integral && Cast->getType()->isIntegralOrEnumerationType() &&
          Context.getIntWidth(Cast->getType()) == Width) {
        const Expr *Inner = Cast->getSubExpr();
        if (const Expr *Wrapper = wrapperOf(Inner))
          Inner = Wrapper;
        Inner = Inner->IgnoreParenImpCasts();
        if (Inner->getType()->isIntegralOrEnumerationType() &&
            Context.getIntWidth(Inner->getType()) == Width) {
          Side = Inner;
          continue;
        }
        if (Inner->getType()->isRecordType() && wrapperOf(Cast->getSubExpr())) {
          Side = Inner;
          continue;
        }
      }
      if (isa<DeclRefExpr, MemberExpr>(Side) &&
          llvm::all_of(referencesOf(Side).keys(), [&](StringRef Key) {
            return Keys.ComplementOf.contains(Key);
          }))
        return true;
      const auto *Ref = dyn_cast<DeclRefExpr>(Side);
      const Expr *Aliased =
          Ref ? ReferenceCollector::aliasedValue(Ref->getDecl()) : nullptr;
      Side = Aliased ? Aliased->IgnoreParenImpCasts() : nullptr;
    }
    return false;
  };
  // An integer class, like `CInt128(~0ul, ~0ul)`, is all ones when each of
  // its integers is.
  std::function<bool(const APValue &)> AllOnesValue =
      [&](const APValue &Value) -> bool {
    if (Value.isInt())
      return Value.getInt().isAllOnes();
    if (!Value.isStruct() || Value.getStructNumFields() == 0)
      return false;
    for (unsigned iBase = 0; iBase < Value.getStructNumBases(); ++iBase)
      if (!AllOnesValue(Value.getStructBase(iBase)))
        return false;
    for (unsigned iField = 0; iField < Value.getStructNumFields(); ++iField)
      if (!AllOnesValue(Value.getStructField(iField)))
        return false;
    return true;
  };
  auto IsAllOnes = [&](const Expr *Side) {
    if (Side->isValueDependent())
      return false;
    if (!Integral) {
      Expr::EvalResult Result;
      return Side->EvaluateAsRValue(Result, Context) &&
             AllOnesValue(Result.Val);
    }
    std::optional<llvm::APSInt> Value = Side->getIntegerConstantExpr(Context);
    if (!Value || Value->getBitWidth() < Width)
      return false;
    // All ones of the argument's type, converted as the comparison converts
    // it: an unsigned `~0u` never equals `~0ull`.
    const llvm::APSInt AllOnes(
        llvm::APInt::getAllOnes(Width),
        Keys.ComplementType->isUnsignedIntegerOrEnumerationType());
    return llvm::APSInt::isSameValue(AllOnes.extend(Value->getBitWidth()),
                                     *Value) ||
           (Value->getBitWidth() == Width && Value->isAllOnes());
  };
  if ((IsValue(Left) && IsAllOnes(Right)) ||
      (IsValue(Right) && IsAllOnes(Left)))
    return NotEqual == Branch ? EZero::Excluded : EZero::Possible;
  return EZero::Unknown;
}

/// Whether the overloaded operator that \p Operator calls can change what it
/// is given: a member that isn't const, or one that takes a reference to
/// what isn't const.
static bool methodChangesObject(const CXXMethodDecl *Method,
                                bool OnlyShared = false);

/// Whether the body of \p Definition writes what is mutable of a parameter
/// it is given as const, `_Bits.m_Bits = 0` of `mutable unsigned m_Bits`,
/// also through a helper it gives it to, `fg_TestAndClear(_Bits)`, but not
/// in a branch of an `if constexpr` that is discarded.
static bool writesMutableOfParameters(const FunctionDecl *Definition,
                                      int Depth) {
  if (!Definition || !Definition->getBody() || Depth > 8)
    return false;
  const ASTContext &Context = Definition->getASTContext();
  bool Writes = false;
  std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
    if (!S || Writes || isa<LambdaExpr>(S))
      return;
    if (const auto *If = dyn_cast<IfStmt>(S); If && If->isConstexpr())
      if (const std::optional<const Stmt *> Case =
              If->getNondiscardedCase(Context)) {
        Visit(If->getInit());
        Visit(If->getCond());
        Visit(*Case);
        return;
      }
    const Expr *Target = nullptr;
    if (const auto *Assign = dyn_cast<BinaryOperator>(S);
        Assign && Assign->isAssignmentOp())
      Target = Assign->getLHS();
    else if (const auto *Step = dyn_cast<UnaryOperator>(S);
             Step && Step->isIncrementDecrementOp())
      Target = Step->getSubExpr();
    for (const Expr *Member = Target ? Target->IgnoreParenImpCasts() : nullptr;
         const auto *Access = dyn_cast_or_null<MemberExpr>(Member);
         Member = Access->getBase()->IgnoreParenImpCasts()) {
      const auto *Field = dyn_cast<FieldDecl>(Access->getMemberDecl());
      const auto *Base = dyn_cast<DeclRefExpr>(
          Access->getBase()->IgnoreParenImpCasts());
      if (Field && Field->isMutable() && Base &&
          isa<ParmVarDecl>(Base->getDecl()))
        Writes = true;
    }
    if (const auto *Call = dyn_cast<CallExpr>(S);
        Call && Call->getDirectCallee() &&
        llvm::any_of(Call->arguments(), [](const Expr *Argument) {
          const auto *Ref =
              dyn_cast<DeclRefExpr>(Argument->IgnoreParenImpCasts());
          return Ref && isa<ParmVarDecl>(Ref->getDecl());
        }) &&
        writesMutableOfParameters(Call->getDirectCallee()->getDefinition(),
                                  Depth + 1))
      Writes = true;
    for (const Stmt *Child : childrenOnce(S))
      Visit(Child);
  };
  Visit(Definition->getBody());
  return Writes;
}

static bool mayChangeOperands(const CXXOperatorCallExpr *Operator) {
  const auto *Callee =
      dyn_cast_or_null<FunctionDecl>(Operator->getCalleeDecl());
  if (!Callee)
    return false;
  // Also a const one that changes what is mutable.
  if (const auto *Method = dyn_cast<CXXMethodDecl>(Callee);
      Method && Method->isImplicitObjectMemberFunction() &&
      methodChangesObject(Method))
    return true;
  if (llvm::any_of(Callee->parameters(), [](const ParmVarDecl *Parameter) {
        return Parameter->getType()->isReferenceType() &&
               !Parameter->getType().getNonReferenceType().isConstQualified();
      }))
    return true;
  // Also a free one given it as const, whose body writes what is mutable of
  // it, `_Bits.m_Bits = 0` of `mutable unsigned m_Bits`.
  return writesMutableOfParameters(Callee->getDefinition(), 0);
}

static bool changesObject(const CXXMemberCallExpr *Call);

/// Whether, when \p Condition is \p Branch, the values of \p References it
/// reads are known not to be zero: like `Value`, `Value != 0` or `!Value` on
/// the false branch.
/// Whether \p Condition, when it holds, tells that what it assigns to one of
/// \p References isn't zero: it tests the assignment, `(_Bits >>= 1) != 0`
/// or `(_Bits >>= 1)`, whose value is what is assigned.
static bool checksWhatItWrites(const Expr *Condition,
                               const llvm::StringSet<> &References,
                               const ASTContext &Context) {
  // Also with the temporaries it makes, `uint256(1)`.
  const Expr *Tested = Condition->IgnoreImplicit()->IgnoreParenImpCasts();
  // Zero, also of an integer class, `uint256(0)` or `0` converted to it.
  auto IsZero = [&](const Expr *Side) {
    Side = Side->IgnoreImplicit()->IgnoreParens();
    // Of one integer, `uint256(0)`, not of words, `uint256(0, 1)`, which is
    // told by what it makes.
    if (const auto *Made = dyn_cast<CXXConstructExpr>(Side);
        Made && Made->getNumArgs() >= 1 &&
        !Made->getConstructor()->isCopyOrMoveConstructor() &&
        llvm::all_of(llvm::drop_begin(Made->arguments()),
                     [](const Expr *Argument) {
                       return isa<CXXDefaultArgExpr>(Argument);
                     }))
      Side = Made->getArg(0)->IgnoreImplicit()->IgnoreParens();
    if (Side->isValueDependent())
      return false;
    if (std::optional<llvm::APSInt> Value =
            Side->getIntegerConstantExpr(Context))
      return Value->isZero();
    Expr::EvalResult Result;
    if (Side->getType()->isRecordType() &&
        Side->EvaluateAsRValue(Result, Context, /*InConstantContext=*/true) &&
        !Result.HasSideEffects)
      if (const std::optional<bool> Set = hasBitSet(Result.Val))
        return !*Set;
    return false;
  };
  // Compared with zero, also by an operator of an integer class, `(_Bits &=
  // _Bits - uint256(1)) != 0`, or one rewritten from its `==`.
  const Expr *Left = nullptr;
  const Expr *Right = nullptr;
  if (const auto *Compare = dyn_cast<BinaryOperator>(Tested);
      Compare && Compare->getOpcode() == BO_NE) {
    Left = Compare->getLHS();
    Right = Compare->getRHS();
  } else if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(Tested);
             Operator && Operator->getOperator() == OO_ExclaimEqual &&
             Operator->getNumArgs() == 2) {
    Left = Operator->getArg(0);
    Right = Operator->getArg(1);
  } else if (const auto *Rewritten =
                 dyn_cast<CXXRewrittenBinaryOperator>(Tested);
             Rewritten && Rewritten->getOperator() == BO_NE) {
    Left = Rewritten->getLHS();
    Right = Rewritten->getRHS();
  }
  if (Left) {
    Tested = IsZero(Right) ? Left : (IsZero(Left) ? Right : nullptr);
    if (!Tested)
      return false;
    Tested = Tested->IgnoreImplicit()->IgnoreParenImpCasts();
  } else if (const auto *Converted = dyn_cast<CXXMemberCallExpr>(Tested);
             Converted &&
             isa_and_nonnull<CXXConversionDecl>(Converted->getMethodDecl()) &&
             Converted->getType()->isBooleanType() &&
             isUnsignedInteger(
                 Converted->getImplicitObjectArgument()->getType(), 0))
    // Also as a boolean, `while (--_Value)` of an integer class.
    Tested = Converted->getImplicitObjectArgument()
                 ->IgnoreImplicit()
                 ->IgnoreParenImpCasts();
  // What it assigns is what it tests, also made of what it had, `(_Bits &=
  // _Bits - 1) != 0`; it checks the argument only when the argument reads
  // only what it writes, not `_Bits & _Mask` of `(_Mask = 1) != 0`. Also by
  // an assignment operator of an integer class, which gives what it assigns.
  // Also what a prefix increment or decrement gives, `--_Value`, the value it
  // makes.
  const Expr *Assigned = nullptr;
  if (const auto *Assign = dyn_cast<BinaryOperator>(Tested);
      Assign && Assign->isAssignmentOp() && Assign->getType()->isIntegerType())
    Assigned = Assign->getLHS();
  else if (const auto *Step = dyn_cast<UnaryOperator>(Tested);
           Step && Step->isPrefix() && Step->isIncrementDecrementOp() &&
           Step->getType()->isIntegerType())
    Assigned = Step->getSubExpr();
  else if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(Tested);
           Operator &&
           (Operator->getOperator() == OO_PlusPlus ||
            Operator->getOperator() == OO_MinusMinus) &&
           Operator->getNumArgs() == 1 && Operator->isLValue() &&
           isUnsignedInteger(Operator->getArg(0)->getType(), 0) &&
           Context.hasSameUnqualifiedType(
               Operator->getType().getNonReferenceType(),
               Operator->getArg(0)->getType().getNonReferenceType()))
    Assigned = Operator->getArg(0);
  else if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(Tested);
           Operator && Operator->isAssignmentOp() &&
           Operator->getNumArgs() == 2 && Operator->isLValue() &&
           isUnsignedInteger(Operator->getArg(0)->getType(), 0) &&
           Context.hasSameUnqualifiedType(
               Operator->getType().getNonReferenceType(),
               Operator->getArg(0)->getType().getNonReferenceType()))
    Assigned = Operator->getArg(0);
  if (!Assigned)
    return false;
  const llvm::StringSet<> Written =
      referencesOf(Assigned, nullptr, /*Written=*/true);
  return !References.empty() &&
         llvm::all_of(References.keys(), [&](StringRef Reference) {
           return llvm::any_of(Written.keys(), [&](StringRef Key) {
             return isRelated(Key, Reference);
           });
         });
}

/// Whether the destructor of \p Record, when its body can be seen, writes one
/// of \p References, like a global, `~CReset() { g_Bits = 0; }`.
static bool
destructorWritesAnyImpl(const CXXRecordDecl *Record,
                        const llvm::StringSet<> &References, int Depth,
                        llvm::DenseMap<const CXXRecordDecl *, int> &Looked) {
  if (!Record || !Record->hasDefinition() || Depth > 16)
    return false;
  Record = Record->getDefinition();
  // Each record once, at the least depth it is met at, of which what is
  // below it is looked at the furthest: a base that is virtual is a base of
  // each class between it and the most derived, and would be looked at again
  // through each path to it, also a class of several members. One looked at
  // writes none, or the look would have ended.
  if (auto [It, Inserted] = Looked.try_emplace(Record, Depth); !Inserted) {
    if (It->second <= Depth)
      return false;
    It->second = Depth;
  }
  const CXXDestructorDecl *Destructor = Record->getDestructor();
  const FunctionDecl *Definition =
      Destructor ? Destructor->getDefinition() : nullptr;
  if (Definition && writesAny(Definition->getBody(), References))
    return true;
  // Also the destructors of its members and bases, which it runs after its
  // body, `struct CWrapper { CReset m_Reset; };`, but not of the members of
  // a union, which it destroys only when its body does.
  auto Of = [](QualType Type) {
    return Type->getBaseElementTypeUnsafe()->getAsCXXRecordDecl();
  };
  if (!Record->isUnion())
    for (const FieldDecl *Field : Record->fields())
      if (destructorWritesAnyImpl(Of(Field->getType()), References, Depth + 1,
                                  Looked))
        return true;
  for (const CXXBaseSpecifier &Base : Record->bases())
    if (destructorWritesAnyImpl(Of(Base.getType()), References, Depth + 1,
                                Looked))
      return true;
  return false;
}

/// Like destructorWritesAnyImpl, of the globals and static members of
/// \p References only: what the body names of its object, `m_Bits`, is of
/// the object it destroys, not the one of the function it is destroyed in,
/// and it can't name locals and parameters of that function.
static bool destructorWritesAny(const CXXRecordDecl *Record,
                                const llvm::StringSet<> &References) {
  llvm::StringSet<> Globals;
  for (StringRef Reference : References.keys())
    if (Reference.starts_with("g"))
      Globals.insert(Reference);
  llvm::DenseMap<const CXXRecordDecl *, int> Looked;
  return !Globals.empty() &&
         destructorWritesAnyImpl(Record, Globals, 0, Looked);
}

static bool destructorWritesThroughMember(const CXXRecordDecl *Record,
                                          int Depth = 0);

/// The test \p Ref, a local boolean, saved, `bool bNonZero = _Bits != 0;`,
/// when it is still what it tells at \p Ref: the local isn't changed, nothing
/// written between its declaration and \p Ref changes what \p Keys tell of,
/// and no loop around \p Ref but not the declaration can run it again after
/// a change. Null otherwise.
static const Expr *savedTest(const DeclRefExpr *Ref, const CArgumentKeys &Keys,
                             const ASTContext &Context) {
  const auto *Local = dyn_cast<VarDecl>(Ref->getDecl());
  if (!Local || !Local->hasLocalStorage() || isa<ParmVarDecl>(Local) ||
      !Local->getType()->isBooleanType() || !Local->getInit() ||
      Local->isInitCapture())
    return nullptr;
  const auto *Function =
      dyn_cast_or_null<FunctionDecl>(Local->getParentFunctionOrMethod());
  if (!Function || !Function->getBody())
    return nullptr;
  // Not changed anywhere in its function.
  llvm::StringSet<> Saved;
  Saved.insert(ReferenceCollector::declKey(Local));
  if (!Local->getType().isConstQualified() &&
      writesAny(Function->getBody(), Saved))
    return nullptr;
  std::function<bool(const Stmt *)> Jumps = [&](const Stmt *S) {
    return S && (isa<GotoStmt, IndirectGotoStmt, LabelStmt>(S) ||
                 llvm::any_of(childrenOnce(S), Jumps));
  };
  const SourceManager &SM = Context.getSourceManager();
  const SourceLocation From = Local->getInit()->getEndLoc();
  const SourceLocation To = Ref->getBeginLoc();
  if (From.isInvalid() || To.isInvalid() || From.isMacroID() ||
      To.isMacroID() || !SM.isBeforeInTranslationUnit(From, To))
    return nullptr;
  ASTContext &Mutable = const_cast<ASTContext &>(Context);
  DynTypedNode Node = DynTypedNode::create(*Ref);
  for (int Depth = 0; Depth < 256; ++Depth) {
    const auto Parents = instantiatedParents(Node, Mutable);
    if (Parents.empty())
      return nullptr;
    Node = Parents[0];
    const SourceRange Range = Node.getSourceRange();
    if (Range.isValid() &&
        !SM.isBeforeInTranslationUnit(From, Range.getBegin()))
      break;
    if (Node.get<ForStmt>() || Node.get<WhileStmt>() || Node.get<DoStmt>() ||
        Node.get<CXXForRangeStmt>() || Node.get<LambdaExpr>())
      return nullptr;
  }
  // What lies wholly between them.
  bool Writes = false;
  std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
    if (!S || Writes)
      return;
    const SourceRange Range = S->getSourceRange();
    if (Range.isInvalid())
      return;
    if (!SM.isBeforeInTranslationUnit(Range.getBegin(), To) ||
        !SM.isBeforeInTranslationUnit(From, Range.getEnd()))
      return;
    if (SM.isBeforeInTranslationUnit(From, Range.getBegin()) &&
        SM.isBeforeInTranslationUnit(Range.getEnd(), To)) {
      Writes = writesAny(S, Keys.Read) || Jumps(S);
      return;
    }
    for (const Stmt *Child : childrenOnce(S))
      Visit(Child);
  };
  Visit(Function->getBody());
  return Writes ? nullptr : Local->getInit();
}

/// The integer that \p Value, a constant of the integer class \p Type, is: its
/// words, also of the words in them, as they are laid out on the target, like
/// the bits of an integer of its size, `uint256(1, 0)` with the upper word
/// first given. None for what else it is made of.
static std::optional<llvm::APSInt> integerOfClass(const APValue &Value,
                                                  QualType Type,
                                                  const ASTContext &Context,
                                                  int Depth = 0) {
  if (Depth > 8 || Type->isDependentType() || Type->isIncompleteType())
    return std::nullopt;
  const unsigned Width = Context.getTypeSize(Type);
  if (Value.isInt())
    return llvm::APSInt(Value.getInt().zextOrTrunc(Width), /*isUnsigned=*/true);
  const CXXRecordDecl *Record = Type->getAsCXXRecordDecl();
  if (!Value.isStruct() || !Record || Value.getStructNumBases() != 0 ||
      !Record->hasDefinition())
    return std::nullopt;
  const ASTRecordLayout &Layout = Context.getASTRecordLayout(Record);
  llvm::APInt Bits(Width, 0);
  unsigned iField = 0;
  for (const FieldDecl *Field : Record->fields()) {
    if (Field->isBitField() || iField >= Value.getStructNumFields())
      return std::nullopt;
    const std::optional<llvm::APSInt> Word = integerOfClass(
        Value.getStructField(iField), Field->getType(), Context, Depth + 1);
    if (!Word)
      return std::nullopt;
    const unsigned Offset = Layout.getFieldOffset(iField);
    const unsigned WordWidth = Word->getBitWidth();
    if (Offset + WordWidth > Width)
      return std::nullopt;
    const unsigned Shift = Context.getTargetInfo().isBigEndian()
                               ? Width - Offset - WordWidth
                               : Offset;
    Bits |= Word->zext(Width).shl(Shift);
    ++iField;
  }
  // Signed as the class is, `int256(-1)`.
  return llvm::APSInt(Bits,
                      /*isUnsigned=*/Depth != 0 || isUnsignedInteger(Type, 0));
}

/// The integer constant \p E is, also made a class of one, `uint256(1)`, also
/// of its words, `uint256(1, 0)`, or none.
static std::optional<llvm::APSInt> integerConstant(const Expr *E,
                                                   const ASTContext &Context) {
  for (int Depth = 0; E && Depth < 8; ++Depth) {
    E = E->IgnoreImplicit()->IgnoreParenImpCasts();
    // An integer as it is converted, `umint(-1)`, the largest, not `-1`.
    Expr::EvalResult Constant;
    if (E->getType()->isIntegralOrEnumerationType() &&
        !E->isValueDependent() && E->EvaluateAsInt(Constant, Context))
      return Constant.Val.getInt();
    // Also of a constant of an integer class, `uint256::CConstants::
    // mc_One`, what it is made of.
    if (const auto *Ref = dyn_cast<DeclRefExpr>(E))
      if (const auto *Named = dyn_cast<VarDecl>(Ref->getDecl());
          Named && Named->getInit() &&
          (Named->isConstexpr() || Named->getType().isConstQualified()) &&
          !Named->getType()->isReferenceType()) {
        if (const APValue *Evaluated =
                Named->isConstexpr() && isIntegerClass(Named->getType()) &&
                        !Named->getType()->isDependentType()
                    ? Named->evaluateValue()
                    : nullptr)
          return integerOfClass(*Evaluated, Named->getType(), Context);
        E = Named->getInit();
        continue;
      }
    // A constant made of words, `uint256(1, 0)`.
    if (const auto *Made = dyn_cast<CXXConstructExpr>(E);
        Made && Made->getNumArgs() >= 2 && !Made->isValueDependent() &&
        isIntegerClass(Made->getType()) &&
        !llvm::all_of(llvm::drop_begin(Made->arguments()),
                      [](const Expr *Argument) {
                        return isa<CXXDefaultArgExpr>(Argument);
                      })) {
      Expr::EvalResult Evaluated;
      if (!Made->EvaluateAsRValue(Evaluated, Context))
        return std::nullopt;
      return integerOfClass(Evaluated.Val, Made->getType(), Context);
    }
    if (const auto *Construct = dyn_cast<CXXConstructExpr>(E);
        Construct && Construct->getNumArgs() >= 1 &&
        llvm::all_of(llvm::drop_begin(Construct->arguments()),
                     [](const Expr *Argument) {
                       return isa<CXXDefaultArgExpr>(Argument);
                     })) {
      // As the class converts it, `uint256(-1)`, the largest, not `-1`.
      Expr::EvalResult Evaluated;
      if (!Construct->isValueDependent() &&
          isIntegerClass(Construct->getType()) &&
          Construct->EvaluateAsRValue(Evaluated, Context))
        if (const std::optional<llvm::APSInt> Made =
                integerOfClass(Evaluated.Val, Construct->getType(), Context))
          return Made;
      E = Construct->getArg(0);
      continue;
    }
    if (const auto *Cast = dyn_cast<CXXFunctionalCastExpr>(E)) {
      E = Cast->getSubExpr();
      continue;
    }
    return std::nullopt;
  }
  return std::nullopt;
}

/// A comparison \p E makes, of its operands, also overloaded, `_Size <= 1` of
/// an integer class, also through `operator <=>`.
static std::optional<std::tuple<BinaryOperatorKind, const Expr *, const Expr *>>
comparisonOf(const Expr *E) {
  if (const auto *Binary = dyn_cast<BinaryOperator>(E);
      Binary && Binary->isComparisonOp())
    return std::make_tuple(Binary->getOpcode(), Binary->getLHS(),
                           Binary->getRHS());
  if (const auto *Rewritten = dyn_cast<CXXRewrittenBinaryOperator>(E)) {
    const CXXRewrittenBinaryOperator::DecomposedForm Form =
        Rewritten->getDecomposedForm();
    if (BinaryOperator::isComparisonOp(Form.Opcode))
      return std::make_tuple(Form.Opcode, Form.LHS, Form.RHS);
  }
  if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(E);
      Operator && Operator->getNumArgs() == 2 &&
      llvm::is_contained({OO_EqualEqual, OO_ExclaimEqual, OO_Less, OO_Greater,
                          OO_LessEqual, OO_GreaterEqual},
                         Operator->getOperator()))
    return std::make_tuple(
        BinaryOperator::getOverloadedOpcode(Operator->getOperator()),
        Operator->getArg(0), Operator->getArg(1));
  return std::nullopt;
}

static EZero zeroWhen(const Expr *Condition, bool Branch,
                      const CArgumentKeys &Keys, const ASTContext &Context) {
  // `if consteval` has no condition.
  if (!Condition)
    return EZero::Possible;
  const Expr *E = Condition->IgnoreParenImpCasts();
  // A test saved in a local that isn't changed, `bool bNonZero = _Bits !=
  // 0; if (bNonZero)`, is that test, when nothing between them changes what
  // it tests, and no loop around the use runs it again after a change.
  if (const auto *Ref = dyn_cast<DeclRefExpr>(E))
    if (const Expr *Saved = savedTest(Ref, Keys, Context))
      return zeroWhen(Saved, Branch, Keys, Context);
  // A temporary of it, destroyed at the end of the condition, can change it
  // after it is tested, `(CClear{_Value}, _Value)`, when the condition is a
  // full expression; of one that is a part of an expression, like the left
  // side of `&&`, it is destroyed at the end of that, after what the
  // condition guards in it.
  const bool FullExpression =
      isa<ExprWithCleanups>(Condition) ||
      isa<ExprWithCleanups>(Condition->IgnoreParenImpCasts());
  std::function<bool(const Stmt *)> Destroys = [&](const Stmt *S) {
    if (!S || isa<LambdaExpr>(S))
      return false;
    // A destructor whose body can be seen writes what it is given only
    // through its members, `~CClear() { m_Bits = 0; }`, unlike an empty one,
    // `~CObserve() {}`.
    if (const auto *Bind = dyn_cast<CXXBindTemporaryExpr>(S);
        Bind && Bind->getTemporary()->getDestructor() &&
        !Bind->getTemporary()->getDestructor()->isTrivial()) {
      const CXXDestructorDecl *Destructor =
          Bind->getTemporary()->getDestructor();
      const FunctionDecl *Definition = Destructor->getDefinition();
      const bool Seen = Definition && Definition->getBody();
      if ((givesAny(Bind->getSubExpr(), Keys.Read) &&
           (!Seen || destructorWritesThroughMember(Destructor->getParent()))) ||
          destructorWritesAny(Destructor->getParent(), Keys.Read))
        return true;
    }
    return llvm::any_of(childrenOnce(S), Destroys);
  };
  if (FullExpression && Destroys(Condition))
    return EZero::Possible;
  if (const auto *Cleanups = dyn_cast<ExprWithCleanups>(E))
    E = Cleanups->getSubExpr()->IgnoreParenImpCasts();
  // What tests only the value whose complement the argument is tells only by
  // comparing it with all ones.
  // Also through a local pointer to it, `*pBits`.
  std::function<bool(const Stmt *)> ReadsPointee = [&](const Stmt *S) {
    if (!S || isa<LambdaExpr>(S))
      return false;
    if (const auto *Read = dyn_cast<Expr>(S))
      if (const Expr *Pointee = pointeeOf(Read);
          Pointee && Keys.Tested.contains(ReferenceCollector::valueKey(Pointee)))
        return true;
    return llvm::any_of(childrenOnce(S), ReadsPointee);
  };
  // Also a word of it, `_Bits.m_Lower` of a `uint256`.
  std::function<bool(const Stmt *)> ReadsWord = [&](const Stmt *S) {
    if (!S || isa<LambdaExpr>(S))
      return false;
    if (const auto *Word = dyn_cast<MemberExpr>(S);
        Word && Word->getBase()->getType()->isRecordType() &&
        isZeroWithArgument(Word, Keys, Context))
      return true;
    return llvm::any_of(childrenOnce(S), ReadsWord);
  };
  const bool Tested =
      readsAny(E, Keys.Tested, &Context) || ReadsPointee(E) || ReadsWord(E);
  // A value less a constant, `_Size - 1`, is zero only when the value is the
  // constant, which a comparison of the value with a constant can rule out,
  // `_Size <= 1` returning.
  if (const auto Compare = !Tested && !Keys.DifferenceOf.empty()
                                ? comparisonOf(E)
                                : std::nullopt) {
    auto [Opcode, Left, Right] = *Compare;
    Left = Left->IgnoreImplicit()->IgnoreParenImpCasts();
    Right = Right->IgnoreImplicit()->IgnoreParenImpCasts();
    if (ReferenceCollector::valueKey(Right) == Keys.DifferenceOf) {
      std::swap(Left, Right);
      Opcode = BinaryOperator::reverseComparisonOp(Opcode);
    }
    if (const std::optional<llvm::APSInt> Constant =
            ReferenceCollector::valueKey(Left) == Keys.DifferenceOf
                ? integerConstant(Right, Context)
                : std::nullopt) {
      const int Order =
          llvm::APSInt::compareValues(Keys.DifferenceZeroAt, *Constant);
      bool Holds = false;
      switch (Opcode) {
      case BO_LT: Holds = Order < 0; break;
      case BO_GT: Holds = Order > 0; break;
      case BO_LE: Holds = Order <= 0; break;
      case BO_GE: Holds = Order >= 0; break;
      case BO_EQ: Holds = Order == 0; break;
      default: Holds = Order != 0; break;
      }
      return Holds == Branch ? EZero::Possible : EZero::Excluded;
    }
  }
  if (!Tested && !readsAny(E, Keys.ComplementOf))
    return EZero::Possible;
  // A condition that tests the value before it changes it, like `Value-- !=
  // 0`, doesn't tell what it is after.
  if (postWritesAny(E, Keys.Read))
    return EZero::Possible;
  // One that changes it first and tests what it gives, `++Value` or `(Value
  // = fg_Next())`, tells what it is after.
  if (const auto *Unary = dyn_cast<UnaryOperator>(E);
      Unary && Unary->isPrefix() && Unary->isIncrementDecrementOp() &&
      Keys.Tested.contains(
          ReferenceCollector::valueKey(Unary->getSubExpr()->IgnoreParens())))
    return Branch ? EZero::Excluded : EZero::Possible;
  if (const auto *Assign = dyn_cast<BinaryOperator>(E);
      Assign && Assign->isAssignmentOp() &&
      Keys.Tested.contains(
          ReferenceCollector::valueKey(Assign->getLHS()->IgnoreParens())))
    return Branch ? EZero::Excluded : EZero::Possible;
  // The argument tested as a boolean, like `if (_Value - 1)`, also a word of
  // it, `if (_Bits.m_Lower)` of a `uint256`, which is zero when it is.
  if (Keys.Tested.contains(ReferenceCollector::expressionKey(E, Context)) ||
      ((isa<MemberExpr>(E) ||
        (isa<CXXOperatorCallExpr>(E) &&
         cast<CXXOperatorCallExpr>(E)->getOperator() == OO_Subscript)) &&
       isZeroWithArgument(E, Keys, Context)))
    return Branch ? EZero::Excluded : EZero::Possible;
  if (const auto *Unary = dyn_cast<UnaryOperator>(E);
      Unary && Unary->getOpcode() == UO_LNot)
    return zeroWhen(Unary->getSubExpr(), !Branch, Keys, Context);
  // Converted explicitly to what is zero only when it is, like
  // `static_cast<bool>(Value)` or `static_cast<unsigned>(Value == 0)`, not
  // narrowed, `static_cast<uint8>(Value)`.
  if (const auto *Cast = dyn_cast<ExplicitCastExpr>(E)) {
    // What it converts as it is, the argument, which is narrowed by an
    // implicit conversion in it, `static_cast<uint8>(_Bits)`.
    if (keepsNonZero(Cast, Context) &&
        Keys.Tested.contains(ReferenceCollector::expressionKey(
            Cast->getSubExpr()->IgnoreParens(), Context)))
      return Branch ? EZero::Excluded : EZero::Possible;
    const QualType To = Cast->getType();
    const QualType From = Cast->getSubExpr()->getType();
    // Also an integer converted to floating point, which is zero only when
    // it is, `static_cast<double>(_Bits)`.
    if (To->isBooleanType() ||
        (To->isRealFloatingType() && Cast->getSubExpr()
                                         ->IgnoreImpCasts()
                                         ->getType()
                                         ->isIntegralOrEnumerationType()) ||
        (To->isIntegralOrEnumerationType() &&
         (From->isBooleanType() ||
          (From->isIntegralOrEnumerationType() && !From->isDependentType() &&
           !To->isDependentType() &&
           Context.getIntWidth(To) >= Context.getIntWidth(From)))))
      return zeroWhen(Cast->getSubExpr(), Branch, Keys, Context);
  }
  if (const auto *Binary = dyn_cast<BinaryOperator>(E)) {
    // A comma gives its right side.
    if (Binary->getOpcode() == BO_Comma)
      return zeroWhen(Binary->getRHS(), Branch, Keys, Context);
    // So does an assignment to a boolean, `(bZero = (_Bits == 0))`, or to
    // what has the type of its right side, which keeps its value, unless the
    // right side writes what it tells of.
    if (const QualType Target = Binary->getLHS()->getType();
        Binary->getOpcode() == BO_Assign &&
        (Target->isBooleanType() ||
         Context.hasSameUnqualifiedType(
             Target, Binary->getRHS()->IgnoreParenImpCasts()->getType())) &&
        !writesAny(Binary->getRHS(), Keys.Read))
      return zeroWhen(Binary->getRHS(), Branch, Keys, Context);
    if (Binary->isLogicalOp()) {
      const EZero Left = zeroWhen(Binary->getLHS(), Branch, Keys, Context);
      const EZero Right = zeroWhen(Binary->getRHS(), Branch, Keys, Context);
      // What the right side writes isn't what the left side tested, and
      // what it tells after writing must be told for sure.
      if (writesAny(Binary->getRHS(), Keys.Read))
        return Right == EZero::Excluded ? EZero::Excluded : EZero::Possible;
      // Both are as the branch for `&&` true and `||` false, either for the
      // others.
      const bool Both = (Binary->getOpcode() == BO_LAnd) == Branch;
      if (Both)
        return Left == EZero::Excluded || Right == EZero::Excluded
                   ? EZero::Excluded
                   : (Left == EZero::Unknown || Right == EZero::Unknown
                          ? EZero::Unknown
                          : EZero::Possible);
      return Left == EZero::Possible || Right == EZero::Possible
                 ? EZero::Possible
                 : (Left == EZero::Unknown || Right == EZero::Unknown
                        ? EZero::Unknown
                        : EZero::Excluded);
    }
    // A comparison whose sides can change it, `_Bits > fg_Clear(_Bits)`, in
    // either order, tells nothing of what it is after, but not a side that is
    // what it is changed to, `--_Value != 0` or `(_Value = _Other) != 0`.
    auto SideWrites = [&](const Expr *Side) {
      Side = Side->IgnoreParenImpCasts();
      if (const auto *Unary = dyn_cast<UnaryOperator>(Side);
          Unary && Unary->isPrefix() && Unary->isIncrementDecrementOp())
        return false;
      if (const auto *Assign = dyn_cast<BinaryOperator>(Side);
          Assign && Assign->isAssignmentOp())
        return writesAny(Assign->getRHS(), Keys.Read);
      return writesAny(Side, Keys.Read);
    };
    if (Binary->isComparisonOp() &&
        (SideWrites(Binary->getLHS()) || SideWrites(Binary->getRHS())))
      return EZero::Possible;
    // Ordered against zero: `Value > 0` and `Value < 0` tell it isn't zero,
    // like `!=`, and `Value <= 0` and `Value >= 0` that it can be, like `==`.
    if (Binary->isRelationalOp()) {
      if (!Tested)
        return EZero::Possible;
      return comparedWith(Binary->getLHS(), Binary->getRHS(),
                          Binary->getOpcode(), Branch, Keys, Context);
    }
    // Compared with zero, like `Value == 0` or `Value != 0`, or, for a
    // complement, with all ones, like `Entry != mcp_FullySet`.
    // A boolean compared with `true` or `false`, `(_Value == 0) == false`, is
    // that boolean or its negation, also with `1` or `0`, which it is
    // promoted to compare with.
    if (Binary->getOpcode() == BO_EQ || Binary->getOpcode() == BO_NE)
      for (const auto &[Side, Other] :
           {std::pair{Binary->getLHS(), Binary->getRHS()},
            std::pair{Binary->getRHS(), Binary->getLHS()}}) {
        std::optional<bool> Constant;
        if (const auto *Literal =
                dyn_cast<CXXBoolLiteralExpr>(Other->IgnoreParenImpCasts()))
          Constant = Literal->getValue();
        else if (const Expr *Number = Other->IgnoreParenImpCasts();
                 !Number->isValueDependent() &&
                 Number->getType()->isIntegerType())
          if (std::optional<llvm::APSInt> Value =
                  Number->getIntegerConstantExpr(Context);
              Value && (Value->isZero() || Value->isOne()))
            Constant = Value->isOne();
        if (Constant && Side->IgnoreParenImpCasts()->getType()->isBooleanType())
          return zeroWhen(Side->IgnoreParenImpCasts(),
                          ((Binary->getOpcode() == BO_EQ) == *Constant) ==
                              Branch,
                          Keys, Context);
      }
    if (Binary->getOpcode() == BO_EQ || Binary->getOpcode() == BO_NE) {
      const EZero Complement =
          comparedWithAllOnes(Binary->getLHS(), Binary->getRHS(),
                              Binary->getOpcode() == BO_NE, Branch, Keys,
                              Context);
      if (Complement != EZero::Unknown)
        return Complement;
      if (!Tested)
        return EZero::Possible;
      return comparedWith(Binary->getLHS(), Binary->getRHS(),
                          Binary->getOpcode(), Branch, Keys, Context);
    }
    // Tested as a boolean, what keeps zero, like `_Value & 1u`, is zero when
    // the argument is.
    if (Tested && isZeroWithArgument(E, Keys, Context))
      return Branch ? EZero::Excluded : EZero::Possible;
    // Arithmetic that can be other than zero when the argument is, like
    // `_Value | 1u` or `_Value - 1`, tells nothing of it, like comparing it
    // with zero.
    if (Binary->isAdditiveOp() || Binary->isMultiplicativeOp() ||
        Binary->isBitwiseOp() || Binary->isShiftOp())
      return EZero::Possible;
    return Tested ? EZero::Unknown : EZero::Possible;
  }
  // Whether an operand of a comparison writes what is tested other than as
  // what it compares: one stepped, `--_Bits != 0u`, or assigned, `(_Bits &=
  // _Bits - 1) != 0`, compares what it is stepped or assigned to.
  auto OperandWrites = [&](const Expr *Operand) {
    Operand = Operand->IgnoreParenImpCasts();
    if (const auto *Step = dyn_cast<CXXOperatorCallExpr>(Operand);
        Step && Step->getNumArgs() == 1 &&
        (Step->getOperator() == OO_PlusPlus ||
         Step->getOperator() == OO_MinusMinus))
      return writesAny(Step->getArg(0), Keys.Read);
    if (const auto *Step = dyn_cast<UnaryOperator>(Operand);
        Step && Step->isPrefix() && Step->isIncrementDecrementOp())
      return writesAny(Step->getSubExpr(), Keys.Read);
    if (const auto *Assign = dyn_cast<BinaryOperator>(Operand);
        Assign && Assign->isAssignmentOp())
      return writesAny(Assign->getLHS(), Keys.Read) ||
             writesAny(Assign->getRHS(), Keys.Read);
    // Also by an operator whose body can be seen, which writes only its
    // object, `operator &=` of an integer class.
    if (const auto *Assign = dyn_cast<CXXOperatorCallExpr>(Operand);
        Assign && Assign->isAssignmentOp() && Assign->getNumArgs() == 2 &&
        Assign->getDirectCallee() &&
        Assign->getDirectCallee()->getDefinition() &&
        isa<CXXMethodDecl>(Assign->getDirectCallee()) &&
        !writesAny(Assign->getDirectCallee()->getDefinition()->getBody(),
                   Keys.Read))
      return writesAny(Assign->getArg(0), Keys.Read) ||
             writesAny(Assign->getArg(1), Keys.Read);
    return writesAny(Operand, Keys.Read);
  };
  // A comparison that C++20 rewrites, like `Value != 0` through `operator ==`,
  // or reversed, `0 == Value`.
  if (const auto *Rewritten = dyn_cast<CXXRewrittenBinaryOperator>(E)) {
    // The operator it calls, like `operator==` of `!=`, which can change too.
    const Expr *Semantic = Rewritten->getSemanticForm()->IgnoreParenImpCasts();
    if (const auto *Not = dyn_cast<UnaryOperator>(Semantic))
      Semantic = Not->getSubExpr()->IgnoreParenImpCasts();
    auto MayChange = [](const Expr *Call) {
      const auto *Operator =
          dyn_cast<CXXOperatorCallExpr>(Call->IgnoreParenImpCasts());
      return Operator && mayChangeOperands(Operator);
    };
    if (MayChange(Semantic))
      return EZero::Possible;
    // Nor one whose body writes what is tested otherwise, like a global,
    // `g_Bits.m_Bits = 0` in `operator==`, nor operands that do, other than
    // as what they compare.
    {
      const CXXRewrittenBinaryOperator::DecomposedForm Operands =
          Rewritten->getDecomposedForm();
      const auto *Call = dyn_cast<CallExpr>(Semantic);
      const FunctionDecl *Callee = Call ? Call->getDirectCallee() : nullptr;
      const FunctionDecl *Definition =
          Callee ? Callee->getDefinition() : nullptr;
      if (!Call
              ? writesAny(Semantic, Keys.Read)
              : (Definition && writesAny(Definition->getBody(), Keys.Read)) ||
                    OperandWrites(Operands.LHS) || OperandWrites(Operands.RHS))
        return EZero::Possible;
    }
    // Also the `operator <=>` that an ordering compares the result of,
    // `(Value <=> 0) > 0`, not the operands, `--_Bits != 0`, which are told
    // above.
    auto OrderingMayChange = [&](const Expr *Side) {
      const auto *Ordering =
          dyn_cast<CXXOperatorCallExpr>(Side->IgnoreParenImpCasts());
      return Ordering && Ordering->getOperator() == OO_Spaceship &&
             MayChange(Ordering);
    };
    if (const auto *Outer = dyn_cast<BinaryOperator>(Semantic);
        Outer && (OrderingMayChange(Outer->getLHS()) ||
                  OrderingMayChange(Outer->getRHS())))
      return EZero::Possible;
    if (const auto *Outer = dyn_cast<CXXOperatorCallExpr>(Semantic);
        Outer && llvm::any_of(Outer->arguments(), OrderingMayChange))
      return EZero::Possible;
    const CXXRewrittenBinaryOperator::DecomposedForm Form =
        Rewritten->getDecomposedForm();
    if (Form.Opcode == BO_EQ || Form.Opcode == BO_NE) {
      const EZero Complement =
          comparedWithAllOnes(Form.LHS, Form.RHS, Form.Opcode == BO_NE, Branch,
                              Keys, Context);
      if (Complement != EZero::Unknown)
        return Complement;
      if (!Tested)
        return EZero::Possible;
      return comparedWith(Form.LHS, Form.RHS, Form.Opcode, Branch, Keys,
                          Context);
    }
    // Ordered, through `operator <=>`, like `Value <= 0`.
    if (BinaryOperator::isRelationalOp(Form.Opcode)) {
      if (!Tested)
        return EZero::Possible;
      return comparedWith(Form.LHS, Form.RHS, Form.Opcode, Branch, Keys,
                          Context);
    }
    return Tested ? EZero::Unknown : EZero::Possible;
  }
  // An overloaded comparison with all ones, `Entry == mcp_FullySet` of an
  // integer class, for its complement, by an operator that can't change what
  // it compares.
  if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(E);
      Operator &&
      (Operator->getOperator() == OO_EqualEqual ||
       Operator->getOperator() == OO_ExclaimEqual) &&
      Operator->getNumArgs() == 2 && !mayChangeOperands(Operator)) {
    const EZero Complement = comparedWithAllOnes(
        Operator->getArg(0), Operator->getArg(1),
        Operator->getOperator() == OO_ExclaimEqual, Branch, Keys, Context);
    if (Complement != EZero::Unknown)
      return Complement;
  }
  if (!Tested)
    return EZero::Possible;
  // Also through overloaded operators, like of an integer class, that can't
  // change what they are given: not a member that isn't const, nor one that
  // takes a reference to what isn't const.
  if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(E)) {
    if (mayChangeOperands(Operator))
      return EZero::Possible;
    // Nor one whose body writes what is tested otherwise, like a global,
    // `g_Bits = 0`, which it can test before it clears it. Of a comparison,
    // an operand that is what it is stepped to, `--_Bits != 0u`, is what it
    // compares.
    const bool Comparison =
        Operator->getNumArgs() == 2 &&
        llvm::is_contained({OO_EqualEqual, OO_ExclaimEqual, OO_Less, OO_Greater,
                            OO_LessEqual, OO_GreaterEqual},
                           Operator->getOperator());
    if (Comparison) {
      const FunctionDecl *Callee = Operator->getDirectCallee();
      const FunctionDecl *Definition =
          Callee ? Callee->getDefinition() : nullptr;
      if (OperandWrites(Operator->getArg(0)) ||
          OperandWrites(Operator->getArg(1)) ||
          (Definition && writesAny(Definition->getBody(), Keys.Read)))
        return EZero::Possible;
    } else if (writesAny(Operator, Keys.Read))
      return EZero::Possible;
    if (Operator->getOperator() == OO_Exclaim && Operator->getNumArgs() == 1)
      return zeroWhen(Operator->getArg(0), !Branch, Keys, Context);
    // A logical operator of an integer class, `_Bits || CInt(1)`, like the
    // built-in one: both sides are as the branch for `&&` true and `||`
    // false, either for the others.
    if ((Operator->getOperator() == OO_AmpAmp ||
         Operator->getOperator() == OO_PipePipe) &&
        Operator->getNumArgs() == 2) {
      // Both sides are evaluated, in either order, so what one writes can
      // change what the other tested, `_Value && fg_Clear(_Value)`.
      if (writesAny(Operator->getArg(0), Keys.Read) ||
          writesAny(Operator->getArg(1), Keys.Read))
        return EZero::Possible;
      const EZero Left = zeroWhen(Operator->getArg(0), Branch, Keys, Context);
      const EZero Right = zeroWhen(Operator->getArg(1), Branch, Keys, Context);
      if ((Operator->getOperator() == OO_AmpAmp) == Branch)
        return Left == EZero::Excluded || Right == EZero::Excluded
                   ? EZero::Excluded
                   : (Left == EZero::Unknown || Right == EZero::Unknown
                          ? EZero::Unknown
                          : EZero::Possible);
      return Left == EZero::Possible || Right == EZero::Possible
                 ? EZero::Possible
                 : (Left == EZero::Unknown || Right == EZero::Unknown
                        ? EZero::Unknown
                        : EZero::Excluded);
    }
    // Compared, also ordered, like `Value <= 0`.
    if (Operator->getNumArgs() == 2 &&
        llvm::is_contained({OO_EqualEqual, OO_ExclaimEqual, OO_Less,
                            OO_Greater, OO_LessEqual, OO_GreaterEqual},
                           Operator->getOperator()))
      return comparedWith(
          Operator->getArg(0), Operator->getArg(1),
          BinaryOperator::getOverloadedOpcode(Operator->getOperator()), Branch,
          Keys, Context);
  }
  // A reference bound to either of a conditional, `unsigned &Bits = bFirst ?
  // _First : _Second`, tells of the argument only through the values that are
  // it: the other one can be what is tested.
  if (const auto *Ref = dyn_cast<DeclRefExpr>(E))
    if (const auto *Conditional = dyn_cast_or_null<ConditionalOperator>(
            ReferenceCollector::aliasedValue(Ref->getDecl())))
      for (const Expr *Arm :
           {Conditional->getTrueExpr(), Conditional->getFalseExpr()})
        if (!readsAny(Arm, Keys.Read))
          return EZero::Possible;
  // The value tested as a boolean, like `if (Value)`, also through its
  // `operator bool`, but not a member of an element only its index reads it
  // in, `_Sizes[_Size].m_bEnabled`, which tells nothing of the index.
  if (isa<DeclRefExpr, MemberExpr>(E)) {
    const Expr *Root = E;
    while (const auto *Member = dyn_cast<MemberExpr>(Root))
      Root = Member->getBase()->IgnoreParenImpCasts();
    const Expr *Object = nullptr;
    if (const auto *Subscript = dyn_cast<ArraySubscriptExpr>(Root))
      Object = Subscript->getBase();
    else if (const auto *Element = dyn_cast<CXXOperatorCallExpr>(Root);
             Element && Element->getOperator() == OO_Subscript &&
             Element->getNumArgs() >= 1)
      Object = Element->getArg(0);
    else if (const auto *Call = dyn_cast<CallExpr>(Root))
      Object = isa<CXXMemberCallExpr>(Call)
                   ? cast<CXXMemberCallExpr>(Call)->getImplicitObjectArgument()
                   : nullptr;
    if (Root != E && isa<CallExpr, ArraySubscriptExpr>(Root) &&
        (!Object || !(readsAny(Object, Keys.Tested, &Context) ||
                      ReadsPointee(Object) || ReadsWord(Object))))
      return EZero::Possible;
    return Branch ? EZero::Excluded : EZero::Possible;
  }
  // A negation is zero only when what it negates is, `if (-_Value)`, but a
  // complement is zero when it is all ones, `if (~_Value)`.
  if (const auto *Unary = dyn_cast<UnaryOperator>(E)) {
    if (Unary->getOpcode() == UO_Minus || Unary->getOpcode() == UO_Plus)
      return zeroWhen(Unary->getSubExpr(), Branch, Keys, Context);
    if (Unary->getOpcode() == UO_Not)
      return EZero::Possible;
  }
  // Whether \p Object is assigned by an operator of its class that gives it,
  // `_Bits >>= 1` of `TCInt &operator>>=(...) { ...; return *this; }`.
  auto AssignedObject = [](const Expr *Object) {
    const auto *Assign =
        dyn_cast<CXXOperatorCallExpr>(Object->IgnoreParenImpCasts());
    const auto *Method =
        Assign && Assign->isAssignmentOp() && Assign->getNumArgs() == 2
            ? dyn_cast_or_null<CXXMethodDecl>(Assign->getDirectCallee())
            : nullptr;
    const FunctionDecl *Definition =
        Method && Method->getReturnType()->isLValueReferenceType()
            ? Method->getDefinition()
            : nullptr;
    if (!Definition || !Definition->getBody())
      return false;
    bool Any = false;
    bool Others = false;
    std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
      if (!S || Others || isa<LambdaExpr>(S))
        return;
      if (const auto *Return = dyn_cast<ReturnStmt>(S)) {
        const auto *Deref = Return->getRetValue()
                                ? dyn_cast<UnaryOperator>(
                                      Return->getRetValue()->IgnoreParenImpCasts())
                                : nullptr;
        if (Deref && Deref->getOpcode() == UO_Deref &&
            isa<CXXThisExpr>(Deref->getSubExpr()->IgnoreParenImpCasts()))
          Any = true;
        else
          Others = true;
        return;
      }
      for (const Stmt *Child : childrenOnce(S))
        Visit(Child);
    };
    Visit(Definition->getBody());
    return Any && !Others;
  };
  // One that isn't const can change the value it tells about.
  if (const auto *MemberCall = dyn_cast<CXXMemberCallExpr>(E))
    if (const auto *Conversion =
            dyn_cast_or_null<CXXConversionDecl>(MemberCall->getMethodDecl());
        Conversion && Conversion->getConversionType()->isBooleanType()) {
      // Nor one that is const but changes what is mutable, `m_Bits = 0`, or
      // the value through another name, like a global it is,
      // `g_Value.m_Bits = 0`.
      // Also what is assigned and then tested, `while (_Bits >>= 1)` of an
      // integer class, whose operator gives its object, as for comparisons.
      if (!Branch || !Conversion->isConst() || changesObject(MemberCall) ||
          (!AssignedObject(MemberCall->getImplicitObjectArgument())
               ? writesAny(MemberCall, Keys.Read)
               : OperandWrites(MemberCall->getImplicitObjectArgument())))
        return EZero::Possible;
      // Of what is zero when the argument is, like the argument itself or
      // `_Value & Mask`, not of what can be other than zero then, like
      // `_Value - CInt(1)` of an integer class.
      return isZeroWithArgument(MemberCall->getImplicitObjectArgument(), Keys,
                                Context)
                 ? EZero::Excluded
                 : EZero::Possible;
    }
  // What changes the value, like `_Value != 0 ? (_Value = 0, true) : false`,
  // tells only by what each of its results tells after it.
  if (writesAny(E, Keys.Read)) {
    if (const auto *Conditional = dyn_cast<ConditionalOperator>(E))
      return zeroWhen(Conditional->getTrueExpr(), Branch, Keys, Context) ==
                         EZero::Excluded &&
                     zeroWhen(Conditional->getFalseExpr(), Branch, Keys,
                              Context) == EZero::Excluded
                 ? EZero::Excluded
                 : EZero::Possible;
    return EZero::Possible;
  }
  // A conditional, like `_Value ? false : true`, takes the branch through
  // either result, when its condition chose it and it is as the branch, but
  // never through a constant that isn't, like `false` for the branch taken.
  if (const auto *Conditional = dyn_cast<ConditionalOperator>(E)) {
    // What a condition tells is told once while the outermost question is
    // answered, also of conditionals nested in conditions, which would
    // otherwise be told once per path through them.
    using CTold = llvm::DenseMap<std::pair<const Expr *, unsigned>, EZero>;
    static thread_local CTold *s_pTold = nullptr;
    std::optional<CTold> Owned;
    if (!s_pTold)
      s_pTold = &Owned.emplace();
    const llvm::scope_exit Release([&] {
      if (Owned)
        s_pTold = nullptr;
    });
    auto Tell = [&](const Expr *Told, bool Side) {
      const std::pair<const Expr *, unsigned> Key{Told, unsigned(Side)};
      if (auto It = s_pTold->find(Key); It != s_pTold->end())
        return It->second;
      const EZero Result = zeroWhen(Told, Side, Keys, Context);
      (*s_pTold)[Key] = Result;
      return Result;
    };
    auto Through = [&](const Expr *Result, bool Chosen) {
      if (bool Value = false;
          !Result->isValueDependent() &&
          Result->EvaluateAsBooleanCondition(Value, Context) &&
          Value != Branch)
        return EZero::Excluded;
      const EZero Condition = Tell(Conditional->getCond(), Chosen);
      const EZero Told = Tell(Result, Branch);
      if (Condition == EZero::Excluded || Told == EZero::Excluded)
        return EZero::Excluded;
      return Condition == EZero::Unknown || Told == EZero::Unknown
                 ? EZero::Unknown
                 : EZero::Possible;
    };
    const EZero True = Through(Conditional->getTrueExpr(), true);
    const EZero False = Through(Conditional->getFalseExpr(), false);
    if (True == EZero::Possible || False == EZero::Possible)
      return EZero::Possible;
    return True == EZero::Unknown || False == EZero::Unknown ? EZero::Unknown
                                                             : EZero::Excluded;
  }
  return EZero::Unknown;
}

/// Whether calling \p Method can change its object: one that isn't const,
/// unless it can be seen only to give access to it, like `operator[]`, or a
/// const one that can change what is mutable, `mutable unsigned m_Bits`, also
/// of a base, or what a member points to, unless its body can be seen not to,
/// also through the other const member functions of the object it calls.
static bool methodChangesObject(const CXXMethodDecl *Method,
                                bool OnlyShared) {
  if (Method->isStatic())
    return false;
  // With \p OnlyShared, what a copy of the object shares with it, through
  // its reference and pointer members, also by one that isn't const.
  if (!Method->isConst() && !OnlyShared && !onlyGivesAccess(Method))
    return true;
  // Each once while the outermost question is answered: one that calls
  // itself, also through others, changes nothing more.
  static thread_local llvm::SmallPtrSet<const CXXMethodDecl *, 8> *s_pVisiting =
      nullptr;
  std::optional<llvm::SmallPtrSet<const CXXMethodDecl *, 8>> Owned;
  if (!s_pVisiting)
    s_pVisiting = &Owned.emplace();
  const llvm::scope_exit Release([&] {
    if (Owned)
      s_pVisiting = nullptr;
  });
  if (!s_pVisiting->insert(Method).second)
    return false;
  // A const one can change what is mutable, `mutable unsigned m_Bits`,
  // also of a base, or what a member points to, unless its body can be
  // seen not to.
  bool Mutable = false;
  bool Pointers = false;
  llvm::SmallVector<const CXXRecordDecl *, 4> Records{Method->getParent()};
  llvm::SmallPtrSet<const CXXRecordDecl *, 8> Seen;
  while (!Records.empty()) {
    const CXXRecordDecl *Record = Records.pop_back_val();
    if (!Record || !Record->hasDefinition() ||
        !Seen.insert(Record->getDefinition()).second)
      continue;
    Record = Record->getDefinition();
    // Also of members that are objects, `m_Inner.m_pBits`, and through
    // a reference member, which a const member function can write.
    for (const FieldDecl *Field : Record->fields()) {
      // Also an array of pointers, `unsigned *m_pBits[2]`.
      Mutable |= Field->isMutable();
      Pointers |= Field->getType()->getBaseElementTypeUnsafe()->isPointerType() ||
                  Field->getType()->isReferenceType();
      Records.push_back(
          Field->getType()->getBaseElementTypeUnsafe()->getAsCXXRecordDecl());
    }
    for (const CXXBaseSpecifier &Base : Record->bases())
      Records.push_back(Base.getType()->getAsCXXRecordDecl());
  }
  // One whose body casts const away, `const_cast<CBits *>(this)->m_Bits =
  // 0`, can change anything of it.
  const FunctionDecl *Definition = Method->getDefinition();
  std::function<bool(const Stmt *)> CastsConstAway = [&](const Stmt *S) {
    if (!S || isa<LambdaExpr>(S))
      return false;
    if (const auto *Cast = dyn_cast<ExplicitCastExpr>(S);
        Cast && (isa<CXXConstCastExpr>(Cast) || isa<CStyleCastExpr>(Cast))) {
      auto Pointee = [](QualType Type) {
        return Type->isPointerType() ? Type->getPointeeType()
                                     : Type.getNonReferenceType();
      };
      const QualType To = Cast->getTypeAsWritten();
      const QualType From = Cast->getSubExpr()->getType();
      if ((To->isPointerType() || To->isReferenceType()) &&
          Pointee(From).isConstQualified() && !Pointee(To).isConstQualified())
        return true;
    }
    return llvm::any_of(childrenOnce(S), CastsConstAway);
  };
  if (Definition && Definition->getBody() &&
      CastsConstAway(Definition->getBody()))
    return true;
  if (!Mutable && !Pointers)
    return false;
  // Defined in another file, it can change either.
  if (!Definition || !Definition->getBody())
    return true;
  // What a target is of the object: a mutable member, also through a
  // local reference to it, `auto &Bits = m_Bits`, what a member points
  // to, `*m_pBits` or `m_pBits[0]`, or refers to, `m_Bits` of `unsigned
  // &m_Bits`.
  // Whether the pointer \p Pointer points where a member points: the member,
  // `m_pBits`, an element of an array member of them, `m_pBits[0]`, or a
  // local made of one, `auto pBits = m_pBits`.
  std::function<bool(const Expr *, int)> PointsWhereMember =
      [&](const Expr *Pointer, int Depth) {
        Pointer = Pointer->IgnoreParenImpCasts();
        if (Depth > 8)
          return false;
        if (const auto *Subscript = dyn_cast<ArraySubscriptExpr>(Pointer);
            Subscript &&
            Subscript->getBase()->IgnoreParenImpCasts()->getType()->isArrayType())
          Pointer = Subscript->getBase()->IgnoreParenImpCasts();
        if (const auto *Member = dyn_cast<MemberExpr>(Pointer))
          return isa<FieldDecl>(Member->getMemberDecl());
        const auto *Ref = dyn_cast<DeclRefExpr>(Pointer);
        const auto *Local = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
        return Local && Local->hasLocalStorage() && !isa<ParmVarDecl>(Local) &&
               Local->getInit() && PointsWhereMember(Local->getInit(), Depth + 1);
      };
  // Whether \p Pointer is what a member that is a smart pointer points to,
  // `m_pInfo->` or `*m_pInfo` of `TCUniquePointer<CInfo> m_pInfo`, whose
  // operators give access to it also when they are const.
  auto ThroughSmartMember = [](const Expr *Pointer) {
    Pointer = Pointer->IgnoreParenImpCasts();
    const Expr *Object = nullptr;
    if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(Pointer);
        Operator && Operator->getNumArgs() >= 1 &&
        (Operator->getOperator() == OO_Arrow ||
         Operator->getOperator() == OO_Star))
      Object = Operator->getArg(0);
    else if (const auto *Named = dyn_cast<CXXMemberCallExpr>(Pointer);
             Named && Named->getMethodDecl() &&
             (Named->getMethodDecl()->getOverloadedOperator() == OO_Arrow ||
              Named->getMethodDecl()->getOverloadedOperator() == OO_Star))
      Object = Named->getImplicitObjectArgument();
    const auto *Member =
        Object ? dyn_cast<MemberExpr>(Object->IgnoreParenImpCasts()) : nullptr;
    return Member && isa<FieldDecl>(Member->getMemberDecl()) &&
           isa<CXXThisExpr>(Member->getBase()->IgnoreParenImpCasts());
  };
  auto OfObject = [&](const Expr *Target) {
    for (int Depth = 0; Target && Depth < 8; ++Depth) {
      Target = Target->IgnoreParenImpCasts();
      if (const auto *Ref = dyn_cast<DeclRefExpr>(Target)) {
        const auto *Local = dyn_cast<VarDecl>(Ref->getDecl());
        if (!Local || !Local->hasLocalStorage() ||
            !Local->getType()->isReferenceType() || !Local->getInit())
          return false;
        Target = Local->getInit();
        continue;
      }
      if (const auto *Member = dyn_cast<MemberExpr>(Target)) {
        const auto *Field = dyn_cast<FieldDecl>(Member->getMemberDecl());
        if (Field &&
            (Field->isMutable() || Field->getType()->isReferenceType()))
          return true;
        // Through a pointer member, `m_pData->m_Bits`, also a smart one.
        if (Member->isArrow()) {
          const auto *Base = dyn_cast<MemberExpr>(
              Member->getBase()->IgnoreParenImpCasts());
          return (Base && isa<FieldDecl>(Base->getMemberDecl()) &&
                  isa<CXXThisExpr>(Base->getBase()->IgnoreParenImpCasts())) ||
                 ThroughSmartMember(Member->getBase());
        }
        Target = Member->getBase();
        continue;
      }
      if (const auto *Deref = dyn_cast<UnaryOperator>(Target);
          Deref && Deref->getOpcode() == UO_Deref)
        return PointsWhereMember(Deref->getSubExpr(), 0);
      // An element of what a member points to, `m_pBits[0]`, or of an
      // array member, `m_Bits[0]`, which is of the member.
      if (const auto *Subscript = dyn_cast<ArraySubscriptExpr>(Target)) {
        const Expr *Base = Subscript->getBase()->IgnoreParenImpCasts();
        if (Base->getType()->isPointerType())
          return PointsWhereMember(Base, 0);
        Target = Base;
        continue;
      }
      // What a smart pointer member points to, `(*m_pInfo).m_Bits`.
      return ThroughSmartMember(Target);
    }
    return false;
  };
  // Not what isn't evaluated, `sizeof(m_Bits = 0)`, nor a branch of an `if
  // constexpr` that is discarded.
  const ASTContext &Context = Method->getASTContext();
  // Whether \p Object is of the object, `this` or a member of it that is an
  // object, `m_Inner`.
  auto IsOwnObject = [](const Expr *Object) {
    for (int Depth = 0; Object && Depth < 8; ++Depth) {
      Object = Object->IgnoreParenImpCasts();
      if (isa<CXXThisExpr>(Object))
        return true;
      // Also through a local reference to it, `auto &State = m_State;`.
      if (const auto *Ref = dyn_cast<DeclRefExpr>(Object))
        if (const auto *Local = dyn_cast<VarDecl>(Ref->getDecl());
            Local && Local->isLocalVarDecl() &&
            Local->getType()->isReferenceType() && Local->getInit()) {
          Object = Local->getInit();
          continue;
        }
      const auto *Member = dyn_cast<MemberExpr>(Object);
      if (!Member || !isa<FieldDecl>(Member->getMemberDecl()) ||
          (Member->isArrow() &&
           !isa<CXXThisExpr>(Member->getBase()->IgnoreParenImpCasts())))
        return false;
      Object = Member->getBase();
    }
    return false;
  };
  // Whether the body of \p Callee is one return of what neither calls nor
  // writes anything, `return (CLink *)((umint)_pLink & ~umint(3));`.
  auto OnlyGivesBack = [](const FunctionDecl *Callee) {
    const FunctionDecl *Definition =
        Callee ? Callee->getDefinition() : nullptr;
    const auto *Body = Definition
                           ? dyn_cast_or_null<CompoundStmt>(Definition->getBody())
                           : nullptr;
    const auto *Return = Body && Body->size() == 1
                             ? dyn_cast<ReturnStmt>(Body->body_front())
                             : nullptr;
    std::function<bool(const Stmt *)> Does = [&](const Stmt *S) {
      if (!S)
        return false;
      if (isa<CallExpr, CXXConstructExpr, CXXNewExpr, CXXDeleteExpr,
              LambdaExpr>(S))
        return true;
      if (const auto *Binary = dyn_cast<BinaryOperator>(S);
          Binary && Binary->isAssignmentOp())
        return true;
      if (const auto *Unary = dyn_cast<UnaryOperator>(S);
          Unary && Unary->isIncrementDecrementOp())
        return true;
      return llvm::any_of(childrenOnce(S), Does);
    };
    return Return && Return->getRetValue() && !Does(Return->getRetValue());
  };
  std::function<bool(const Stmt *)> WritesMutable = [&](const Stmt *S) {
    if (!S || isUnevaluatedNode(S))
      return false;
    if (const auto *Choose = dyn_cast<ChooseExpr>(S);
        Choose && !Choose->isConditionDependent())
      return WritesMutable(Choose->getChosenSubExpr());
    // A lambda it only makes doesn't run, but one it calls does,
    // `[this] { m_Bits = 0; }()`.
    if (isa<LambdaExpr>(S))
      return false;
    if (const auto *Invoke = dyn_cast<CXXOperatorCallExpr>(S);
        Invoke && Invoke->getOperator() == OO_Call && Invoke->getNumArgs() >= 1)
      if (const LambdaExpr *Called = calledLambda(Invoke->getArg(0));
          Called && WritesMutable(Called->getBody()))
        return true;
    if (const auto *Typeid = dyn_cast<CXXTypeidExpr>(S);
        Typeid && !Typeid->isPotentiallyEvaluated())
      return false;
    if (const auto *If = dyn_cast<IfStmt>(S); If && If->isConstexpr())
      if (const std::optional<const Stmt *> Case =
              If->getNondiscardedCase(Context))
        return WritesMutable(If->getInit()) ||
               WritesMutable(If->getConditionVariableDeclStmt()) ||
               WritesMutable(*Case);
    const Expr *Target = nullptr;
    if (const auto *Assign = dyn_cast<BinaryOperator>(S);
        Assign && Assign->isAssignmentOp())
      Target = Assign->getLHS();
    else if (const auto *Step = dyn_cast<UnaryOperator>(S);
             Step && Step->isIncrementDecrementOp())
      Target = Step->getSubExpr();
    // Also an atomic operation other than a load, `__atomic_store_n(&m_Bits,
    // 0, ...)`, of what it is given the address of.
    else if (const auto *Atomic = dyn_cast<AtomicExpr>(S);
             Atomic && !isAtomicLoad(Atomic)) {
      Target = Atomic->getPtr()->IgnoreParenImpCasts();
      if (const auto *Address = dyn_cast<UnaryOperator>(Target);
          Address && Address->getOpcode() == UO_AddrOf)
        Target = Address->getSubExpr();
    } else if (const auto *Inner = dyn_cast<CXXMemberCallExpr>(S);
               Inner && Inner->getMethodDecl() &&
               !Inner->getMethodDecl()->isConst())
      Target = Inner->getImplicitObjectArgument();
    // Also through another const member function of the object that does,
    // `f_Clear()` of `void f_Helper() const { f_Clear(); }`, also of a member
    // that is an object, `m_Inner.f_Clear()`.
    else if (Inner && Inner->getMethodDecl() &&
             Inner->getImplicitObjectArgument() &&
             IsOwnObject(Inner->getImplicitObjectArgument()) &&
             methodChangesObject(Inner->getMethodDecl()))
      return true;
    else if (const auto *Call = dyn_cast<CallExpr>(S)) {
      // What it gives a mutable member to can change it, by a reference or
      // pointer that isn't to const, not by value, `fg_Observe(m_Bits)` of
      // `fg_Observe(unsigned _Value)`.
      const FunctionDecl *Callee = Call->getDirectCallee();
      const unsigned Offset =
          isa<CXXOperatorCallExpr>(Call) && hasImplicitObject(Callee) ? 1 : 0;
      for (unsigned iArgument = 0; iArgument < Call->getNumArgs();
           ++iArgument) {
        if (Callee && iArgument >= Offset &&
            iArgument - Offset < Callee->getNumParams()) {
          const QualType Type =
              Callee->getParamDecl(iArgument - Offset)->getType();
          if ((!Type->isReferenceType() && !Type->isPointerType()) ||
              Type->getPointeeType().isConstQualified())
            continue;
        }
        if (OfObject(Call->getArg(iArgument)))
          return true;
        // Also where a member points, given on, `fg_Clear(m_pBits)`, but not
        // to one whose body only gives it back, also decoded,
        // `CLink::fs_GetPtr(m_Root)` of `return (CLink *)((umint)_pLink &
        // ~umint(3));`, which is as if copied to a local.
        const QualType Given = Call->getArg(iArgument)->getType();
        if (Given->isPointerType() &&
            !Given->getPointeeType().isConstQualified() &&
            PointsWhereMember(Call->getArg(iArgument), 0) &&
            !(OnlyGivesBack(Callee) &&
              llvm::is_contained(returnedArguments(Call),
                                 Call->getArg(iArgument))))
          return true;
      }
    }
    if (Target && OfObject(Target))
      return true;
    return llvm::any_of(childrenOnce(S), WritesMutable);
  };
  return WritesMutable(Definition->getBody());
}

/// The mutable members that \p Method, a const member function whose body
/// can be looked into, writes, `++m_nQueries`, also through the const member
/// functions of the object it calls, or nothing when that can't be told, like
/// what it writes through a pointer member or gives on. With \p Own, of one
/// that isn't const, `void f_Clear() { m_bActive = false; }`, any members of
/// its own it writes.
static std::optional<llvm::SmallPtrSet<const FieldDecl *, 4>>
mutableMembersWritten(const CXXMethodDecl *Method,
                      llvm::SmallPtrSetImpl<const CXXMethodDecl *> &Visiting,
                      bool Own = false) {
  llvm::SmallPtrSet<const FieldDecl *, 4> Fields;
  if ((!Own && !Method->isConst()) || Method->isStatic())
    return std::nullopt;
  const FunctionDecl *Definition = Method->getDefinition();
  if (!Definition || !Definition->getBody())
    return std::nullopt;
  if (!Visiting.insert(Method).second)
    return Fields;
  // The mutable member \p Target is, of the object itself, `m_Bits` or
  // `this->m_Inner.m_Bits`, or null when it isn't one.
  auto MutableMember = [&](const Expr *Target) -> const FieldDecl * {
    const auto *Member = dyn_cast<MemberExpr>(Target->IgnoreParenImpCasts());
    const auto *Field =
        Member ? dyn_cast<FieldDecl>(Member->getMemberDecl()) : nullptr;
    if (!Field || (!Own && !Field->isMutable()) ||
        Field->getType()->isReferenceType())
      return nullptr;
    for (const Expr *Base = Member->getBase()->IgnoreParenImpCasts();;) {
      if (isa<CXXThisExpr>(Base))
        return Field;
      const auto *Inner = dyn_cast<MemberExpr>(Base);
      if (!Inner || Inner->isArrow())
        return nullptr;
      Base = Inner->getBase()->IgnoreParenImpCasts();
    }
  };
  // Whether \p Object is the object or a member of it that is an object,
  // also through a local reference to it, `auto &State = m_State;`.
  auto OfObject = [](const Expr *Object) {
    for (const Expr *Base = Object->IgnoreParenImpCasts();;) {
      if (isa<CXXThisExpr>(Base))
        return true;
      if (const auto *Ref = dyn_cast<DeclRefExpr>(Base))
        if (const auto *Local = dyn_cast<VarDecl>(Ref->getDecl());
            Local && Local->isLocalVarDecl() &&
            Local->getType()->isReferenceType() && Local->getInit()) {
          Base = Local->getInit()->IgnoreParenImpCasts();
          continue;
        }
      const auto *Member = dyn_cast<MemberExpr>(Base);
      if (!Member ||
          (Member->isArrow() &&
           !isa<CXXThisExpr>(Member->getBase()->IgnoreParenImpCasts())))
        return false;
      Base = Member->getBase()->IgnoreParenImpCasts();
    }
  };
  bool Known = true;
  std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
    if (!S || !Known)
      return;
    // A lambda that can reach the object, `[this] { m_Bits = 0; }()`, can
    // write what can't be told, and so can the destructor of a local.
    if (const auto *Lambda = dyn_cast<LambdaExpr>(S)) {
      if (llvm::any_of(Lambda->captures(), [](const LambdaCapture &Capture) {
            return Capture.capturesThis() ||
                   Capture.getCaptureKind() == LCK_ByRef;
          }))
        Known = false;
      return;
    }
    if (const auto *Declaration = dyn_cast<DeclStmt>(S))
      for (const Decl *D : Declaration->decls())
        if (const auto *Local = dyn_cast<VarDecl>(D);
            Local && Local->hasLocalStorage())
          if (const CXXRecordDecl *Record = Local->getType()
                                                ->getBaseElementTypeUnsafe()
                                                ->getAsCXXRecordDecl();
              Record && Record->hasDefinition() &&
              !Record->hasTrivialDestructor())
            Known = false;
    if (isUnevaluatedNode(S))
      return;
    // Not a branch of an `if constexpr` that is discarded.
    if (const auto *If = dyn_cast<IfStmt>(S); If && If->isConstexpr())
      if (const std::optional<const Stmt *> Case =
              If->getNondiscardedCase(Method->getASTContext())) {
        Visit(If->getInit());
        Visit(If->getConditionVariableDeclStmt());
        Visit(*Case);
        return;
      }
    const Expr *Target = nullptr;
    if (const auto *Assign = dyn_cast<BinaryOperator>(S);
        Assign && Assign->isAssignmentOp())
      Target = Assign->getLHS();
    else if (const auto *Step = dyn_cast<UnaryOperator>(S);
             Step && Step->isIncrementDecrementOp())
      Target = Step->getSubExpr();
    // Also an atomic operation other than a load, `__atomic_store_n(&m_Bits,
    // 0, ...)`, of what it is given the address of.
    else if (const auto *Atomic = dyn_cast<AtomicExpr>(S);
             Atomic && !isAtomicLoad(Atomic)) {
      Target = Atomic->getPtr()->IgnoreParenImpCasts();
      if (const auto *Address = dyn_cast<UnaryOperator>(Target);
          Address && Address->getOpcode() == UO_AddrOf)
        Target = Address->getSubExpr();
    }
    // Also through a local reference to it, `auto &Bits = m_Bits`.
    for (int Depth = 0; Target && Depth < 8; ++Depth) {
      const auto *Ref = dyn_cast<DeclRefExpr>(Target->IgnoreParenImpCasts());
      const auto *Alias = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
      if (!Alias || !Alias->getType()->isReferenceType())
        break;
      if (!Alias->hasLocalStorage() || isa<ParmVarDecl>(Alias) ||
          !Alias->getInit()) {
        Known = false;
        break;
      }
      Target = Alias->getInit();
    }
    if (Target && Known) {
      if (const FieldDecl *Field = MutableMember(Target))
        Fields.insert(Field);
      // Through a pointer or reference member, or anything else that can't
      // be told to be local.
      else if (!isa<DeclRefExpr>(Target->IgnoreParenImpCasts()))
        Known = false;
    }
    if (const auto *Call = dyn_cast<CallExpr>(S)) {
      const auto *Member = dyn_cast<CXXMemberCallExpr>(Call);
      const CXXMethodDecl *Called = Member ? Member->getMethodDecl() : nullptr;
      const Expr *Object =
          Member ? Member->getImplicitObjectArgument() : nullptr;
      if (Called && Object && MutableMember(Object) && !Called->isConst())
        Fields.insert(MutableMember(Object));
      // Of the object or a member of it that is an object, `m_Inner.f_Clear()`,
      // what that one writes.
      else if (Called && Object && OfObject(Object)) {
        const auto Inner = mutableMembersWritten(Called, Visiting, Own);
        if (!Inner)
          Known = false;
        else
          Fields.insert(Inner->begin(), Inner->end());
      } else if (Called && !Called->isConst() && !Called->isStatic())
        Known = false;
      // What is given on can be changed there, also through a local
      // reference to it, `fg_Clear(Bits)` of `auto &Bits = m_Bits`.
      for (const Expr *Argument : Call->arguments()) {
        const Expr *Given = Argument;
        for (int Depth = 0; Depth < 8; ++Depth) {
          const auto *Ref = dyn_cast<DeclRefExpr>(Given->IgnoreParenImpCasts());
          const auto *Alias = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
          if (!Alias || !Alias->getType()->isReferenceType() ||
              !Alias->hasLocalStorage() || isa<ParmVarDecl>(Alias) ||
              !Alias->getInit())
            break;
          Given = Alias->getInit();
        }
        if (MutableMember(Given) && !Argument->getType().isConstQualified())
          Known = false;
        // Also what a pointer or reference member refers to, given on where
        // it can be changed, `fg_Clear(m_pBits)`, which isn't told.
        const auto *Field = dyn_cast<MemberExpr>(Given->IgnoreParenImpCasts());
        const QualType Passed = Argument->getType();
        if (Field && isa<FieldDecl>(Field->getMemberDecl()) &&
            OfObject(Field->getBase()) &&
            ((Field->getMemberDecl()->getType()->isPointerType() &&
              Passed->isPointerType() &&
              !Passed->getPointeeType().isConstQualified()) ||
             (Field->getMemberDecl()->getType()->isReferenceType() &&
              !Passed.isConstQualified() && Argument->isLValue())))
          Known = false;
      }
    }
    for (const Stmt *Child : childrenOnce(S))
      Visit(Child);
  };
  Visit(Definition->getBody());
  if (!Known)
    return std::nullopt;
  return Fields;
}

/// Whether \p Call, of a member function, can change its object, like
/// `Value.f_Clear()`, also of the object of the function, `f_Clear()`, which
/// can change any of its members, also through a pointer to one,
/// `(Value.*fClear)()`.
static bool changesObject(const CXXMemberCallExpr *Call) {
  if (const CXXMethodDecl *Method = Call->getMethodDecl())
    return methodChangesObject(Method);
  const auto *Member = dyn_cast<BinaryOperator>(
      Call->getCallee()->IgnoreParens());
  const auto *Prototype =
      Member && Member->isPtrMemOp()
          ? Member->getRHS()
                ->getType()
                ->getAs<MemberPointerType>()
                ->getPointeeType()
                ->getAs<FunctionProtoType>()
          : nullptr;
  return !Prototype || !Prototype->isConst();
}

/// The context of the call being checked, which tells which branch of an
/// `if constexpr` is discarded.
static thread_local const ASTContext *s_CheckContext = nullptr;

/// \p References without what \p Lambda captures by copy, `[_Bits]() mutable
/// { _Bits = 0; }`, which it changes only its own copy of.
/// Whether \p Path, the members after a variable in a key, like
/// `.m_Bits` of `_View.m_Bits`, goes through what a copy of the variable
/// shares with it: a reference member, `unsigned &m_Bits`, or what a pointer
/// member points to, `m_pBits[0]`.
/// Whether a copy of an object of \p Record points to what the object points
/// to: a smart pointer whose `operator->` gives what a pointer member of it
/// points to, `return m_pObject;` or `return fg_GetPointer(
/// m_Data.m_pPointTo);`, unlike an optional, which gives its own value,
/// `return &m_Value;`.
static bool sharesPointee(const CXXRecordDecl *Record) {
  if (!Record || !Record->hasDefinition())
    return false;
  Record = Record->getDefinition();
  // Of the AST it is of, which another can reuse the address of after it
  // is gone.
  struct CShares {
    llvm::DenseMap<const CXXRecordDecl *, bool> Shares;
  };
  auto &Decided = astCache<CShares>(Record->getASTContext()).Shares;
  if (auto It = Decided.find(Record); It != Decided.end())
    return It->second;
  bool Shares = false;
  // Of `operator->`, or else of `operator*`, the one an instantiation of a
  // template uses, `*pCopy = 0`, or else the template's, which reads what it
  // does.
  llvm::SmallVector<const FunctionDecl *, 4> Instantiated, Patterns;
  for (const OverloadedOperatorKind Kind : {OO_Arrow, OO_Star})
    for (const NamedDecl *Found : Record->lookup(
             Record->getASTContext().DeclarationNames.getCXXOperatorName(
                 Kind))) {
      const auto *Method = dyn_cast<CXXMethodDecl>(Found);
      if (!Method || Method->isStatic() || Method->getNumParams() != 0)
        continue;
      if (const FunctionDecl *Definition = Method->getDefinition();
          Definition && Definition->getBody())
        Instantiated.push_back(Definition);
      else if (const FunctionDecl *Pattern =
                   Method->getTemplateInstantiationPattern();
               Pattern && Pattern->getDefinition() &&
               Pattern->getDefinition()->getBody())
        Patterns.push_back(Pattern->getDefinition());
    }
  // Also of a conversion to a pointer, `operator t_CData *()` of a
  // `TCVectorIterator`, which `*iCopy = 0` writes through.
  if (Instantiated.empty() && Patterns.empty())
    for (const NamedDecl *Found : Record->getVisibleConversionFunctions()) {
      const auto *Conversion =
          dyn_cast<CXXConversionDecl>(Found->getUnderlyingDecl());
      if (!Conversion || !Conversion->getConversionType()->isPointerType())
        continue;
      if (const FunctionDecl *Definition = Conversion->getDefinition();
          Definition && Definition->getBody())
        Instantiated.push_back(Definition);
      else if (const FunctionDecl *Pattern =
                   Conversion->getTemplateInstantiationPattern();
               Pattern && Pattern->getDefinition() &&
               Pattern->getDefinition()->getBody())
        Patterns.push_back(Pattern->getDefinition());
    }
  for (const FunctionDecl *Definition :
       llvm::concat<const FunctionDecl *const>(Instantiated, Patterns)) {
    // A pointer member of the object, also a member of a member,
    // `m_Data.m_pPointTo`, or an element of an array of them, `m_pStack[0]`
    // of a tree iterator, which a copy copies.
    auto IsPointerMember = [](const Expr *E) {
      E = E->IgnoreParens();
      if (const auto *Element = dyn_cast<ArraySubscriptExpr>(E);
          Element && Element->getType()->isPointerType() &&
          Element->getBase()->IgnoreParenImpCasts()->getType()->isArrayType())
        E = Element->getBase()->IgnoreParenImpCasts();
      const auto *Member = dyn_cast<MemberExpr>(E);
      if (!Member || !(Member->getType()->isPointerType() ||
                       (Member->getType()->isArrayType() &&
                        Member->getType()
                            ->getPointeeOrArrayElementType()
                            ->isPointerType())))
        return false;
      const Expr *Base = Member;
      while (const auto *Inner = dyn_cast<MemberExpr>(Base)) {
        Base = Inner->getBase()->IgnoreParenImpCasts();
        if (Inner->isArrow())
          break;
      }
      return isa<CXXThisExpr>(Base) && isa<FieldDecl>(Member->getMemberDecl());
    };
    // A read of one, also in the template, where what depends on its
    // parameters, `t_CType *m_pPointTo`, isn't read by a cast, also through a
    // local pointer made from one, `CMember *pMember = m_Iter;`.
    int nLocals = 0;
    std::function<bool(const FunctionDecl *, int)> GivesPointee;
    // Also what a getter of a member of it gives, `mp_Iter.f_GetCurrent()`,
    // when that gives what a pointer member of the member points to.
    int nGetters = 0;
    auto MemberGives = [&](const CXXMemberCallExpr *Getter) {
      const CXXMethodDecl *Method = Getter->getMethodDecl();
      if (!Method || Method->isVirtual() ||
          !Getter->getType()->isPointerType() || nGetters >= 4)
        return false;
      const Expr *Object = Getter->getImplicitObjectArgument();
      if (!Object)
        return false;
      Object = Object->IgnoreParenImpCasts();
      // A member held by value, through `this->` only.
      bool Member = false;
      while (const auto *Inner = dyn_cast<MemberExpr>(Object)) {
        if (!isa<FieldDecl>(Inner->getMemberDecl()))
          break;
        Member = true;
        Object = Inner->getBase()->IgnoreParenImpCasts();
        if (Inner->isArrow())
          break;
      }
      if (!Member || !isa<CXXThisExpr>(Object))
        return false;
      const FunctionDecl *Called = Method->getDefinition();
      if (!Called || !Called->getBody())
        if (const FunctionDecl *Pattern =
                Method->getTemplateInstantiationPattern())
          Called = Pattern->getDefinition();
      if (!Called || !Called->getBody())
        return false;
      ++nGetters;
      const bool Gives = GivesPointee(Called, 1);
      --nGetters;
      return Gives;
    };
    std::function<bool(const Stmt *)> ReadsPointerMember =
        [&](const Stmt *S) -> bool {
      if (!S)
        return false;
      if (const auto *Getter = dyn_cast<CXXMemberCallExpr>(S);
          Getter && MemberGives(Getter))
        return true;
      if (const auto *Read = dyn_cast<ImplicitCastExpr>(S);
          Read && Read->getCastKind() == CK_LValueToRValue &&
          Read->getType()->isPointerType() &&
          IsPointerMember(Read->getSubExpr()))
        return true;
      // Also a pointer it keeps in an integer member, which a function of it
      // decodes, `fsp_FromInt(mp_PointTo)` of a tagged pointer, that a copy
      // copies.
      if (const auto *Decode = dyn_cast<CallExpr>(S);
          Decode && !isa<CXXMemberCallExpr, CXXOperatorCallExpr>(Decode) &&
          Decode->getType()->isPointerType() && Decode->getNumArgs() == 1)
        if (const auto *Member =
                dyn_cast<MemberExpr>(Decode->getArg(0)->IgnoreParenImpCasts());
            Member && isa<FieldDecl>(Member->getMemberDecl()) &&
            Member->getType()->isIntegerType() &&
            isa<CXXThisExpr>(Member->getBase()->IgnoreParenImpCasts()))
          return true;
      if (const auto *Ref = dyn_cast<DeclRefExpr>(S))
        if (const auto *Local = dyn_cast<VarDecl>(Ref->getDecl());
            Local && Local->hasLocalStorage() && !isa<ParmVarDecl>(Local) &&
            Local->getType()->isPointerType() && Local->getInit() &&
            nLocals++ < 8)
          return ReadsPointerMember(Local->getInit());
      if (const auto *Member = dyn_cast<MemberExpr>(S);
          Member && Member->isTypeDependent() && IsPointerMember(Member))
        return true;
      return llvm::any_of(childrenOnce(S), ReadsPointerMember);
    };
    // Each return gives one, but not a null pointer, `return nullptr;`, which
    // points to nothing, also through a getter of the object that does,
    // `return fp_GetCurrent();` of an iterator.
    GivesPointee =
        [&](const FunctionDecl *Function, int Depth) {
          bool AllReturns = true;
          bool AnyReturn = false;
          std::function<void(const Stmt *)> Returns = [&](const Stmt *S) {
            if (!S || isa<LambdaExpr>(S) || !AllReturns)
              return;
            if (const auto *Return = dyn_cast<ReturnStmt>(S)) {
              const Expr *Value =
                  Return->getRetValue()
                      ? Return->getRetValue()->IgnoreParenCasts()
                      : nullptr;
              if (Value && !Value->isValueDependent() &&
                  Value->isNullPointerConstant(
                      Function->getASTContext(),
                      Expr::NPC_ValueDependentIsNotNull))
                return;
              AnyReturn = true;
              if (const auto *Getter =
                      dyn_cast_or_null<CXXMemberCallExpr>(Value);
                  Getter && Depth < 4 && Getter->getMethodDecl() &&
                  !Getter->getMethodDecl()->isVirtual() &&
                  isa<CXXThisExpr>(Getter->getImplicitObjectArgument()
                                       ->IgnoreParenImpCasts())) {
                const FunctionDecl *Called =
                    Getter->getMethodDecl()->getDefinition();
                if (!Called || !Called->getBody())
                  if (const FunctionDecl *Pattern =
                          Getter->getMethodDecl()
                              ->getTemplateInstantiationPattern())
                    Called = Pattern->getDefinition();
                if (!Called || !Called->getBody() ||
                    !GivesPointee(Called, Depth + 1))
                  AllReturns = false;
                return;
              }
              // Not the address of what is its own, `return &m_Value;` of
              // an optional, but of what a pointer member points to, `return
              // &pMember->m_Object;` of a linked list iterator, also what a
              // member function of that gives a reference to, `return
              // &pNode->f_Value();` of a map iterator.
              const auto *Address = dyn_cast_or_null<UnaryOperator>(Value);
              if (Address && Address->getOpcode() == UO_AddrOf) {
                const Expr *Through = Address->getSubExpr()->IgnoreParens();
                if (const auto *Call = dyn_cast<CXXMemberCallExpr>(Through);
                    Call && Call->isLValue() && Call->getMethodDecl() &&
                    !Call->getMethodDecl()->isVirtual())
                  Through = Call->getCallee()->IgnoreParens();
                while (const auto *Member = dyn_cast<MemberExpr>(Through)) {
                  if (Member->isArrow())
                    break;
                  Through = Member->getBase()->IgnoreParens();
                }
                const auto *Arrow = dyn_cast<MemberExpr>(Through);
                if (!Arrow || !Arrow->isArrow() ||
                    !ReadsPointerMember(Arrow->getBase()))
                  AllReturns = false;
                return;
              }
              if (!Value || !ReadsPointerMember(Return->getRetValue()))
                AllReturns = false;
              return;
            }
            for (const Stmt *Child : childrenOnce(S))
              Returns(Child);
          };
          Returns(Function->getBody());
          return AnyReturn && AllReturns;
        };
    Shares = GivesPointee(Definition, 0);
    break;
  }
  return Decided[Record] = Shares;
}

/// Whether the constructor \p Callee, whose body can be seen, only keeps
/// where its parameter at \p Index is: binds a reference member to it,
/// `m_Value(_Value)`, or points a pointer member at it, `m_pValue(&_Value)`
/// or `m_Data.m_pPointTo = &_Value;` in its body.
static bool onlyKeepsParameter(const FunctionDecl *Callee, unsigned Index) {
  const auto *Constructor = dyn_cast_or_null<CXXConstructorDecl>(
      Callee ? Callee->getDefinition() : nullptr);
  if (!Constructor || Index >= Constructor->getNumParams() ||
      !Constructor->getBody())
    return false;
  const ParmVarDecl *Parameter = Constructor->getParamDecl(Index);
  llvm::SmallPtrSet<const DeclRefExpr *, 4> Kept;
  auto Keeps = [&](const Expr *Init, QualType Member) {
    if (!Init)
      return;
    Init = Init->IgnoreParenImpCasts();
    const auto *Address = dyn_cast<UnaryOperator>(Init);
    if (Address && Address->getOpcode() == UO_AddrOf && Member->isPointerType())
      Init = Address->getSubExpr()->IgnoreParenImpCasts();
    else if (!Member->isReferenceType())
      return;
    if (const auto *Ref = dyn_cast<DeclRefExpr>(Init);
        Ref && Ref->getDecl() == Parameter)
      Kept.insert(Ref);
  };
  for (const CXXCtorInitializer *Initializer : Constructor->inits())
    if (const FieldDecl *Field = Initializer->getMember())
      Keeps(Initializer->getInit(), Field->getType());
  const auto *Body = dyn_cast<CompoundStmt>(Constructor->getBody());
  if (!Body)
    return false;
  for (const Stmt *Statement : Body->body())
    if (const auto *Assign = dyn_cast<BinaryOperator>(Statement);
        Assign && Assign->getOpcode() == BO_Assign)
      if (const auto *Target =
              dyn_cast<MemberExpr>(Assign->getLHS()->IgnoreParenImpCasts());
          Target && isa<FieldDecl>(Target->getMemberDecl()))
        Keeps(Assign->getRHS(), Target->getMemberDecl()->getType());
  // Each use of it is one of those.
  bool Other = false;
  std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
    if (!S || Other)
      return;
    if (const auto *Ref = dyn_cast<DeclRefExpr>(S);
        Ref && Ref->getDecl() == Parameter && !Kept.contains(Ref))
      Other = true;
    for (const Stmt *Child : childrenOnce(S))
      Visit(Child);
  };
  for (const CXXCtorInitializer *Initializer : Constructor->inits())
    Visit(Initializer->getInit());
  Visit(Body);
  return !Kept.empty() && !Other;
}

/// Whether an object of \p Record holds a pointer or a reference, also in a
/// member or a base, through which what it was made from can be reached.
static bool holdsIndirection(const CXXRecordDecl *Record) {
  llvm::SmallVector<const CXXRecordDecl *, 4> Records{Record};
  llvm::SmallPtrSet<const CXXRecordDecl *, 8> Seen;
  while (!Records.empty()) {
    const CXXRecordDecl *Next = Records.pop_back_val();
    if (!Next || !Next->hasDefinition() ||
        !Seen.insert(Next->getDefinition()).second || Seen.size() > 32)
      continue;
    Next = Next->getDefinition();
    for (const FieldDecl *Field : Next->fields()) {
      const QualType Type = Field->getType();
      if (Type->isReferenceType() ||
          Type->getBaseElementTypeUnsafe()->isPointerType())
        return true;
      Records.push_back(Type->getBaseElementTypeUnsafe()->getAsCXXRecordDecl());
    }
    for (const CXXBaseSpecifier &Base : Next->bases())
      Records.push_back(Base.getType()->getAsCXXRecordDecl());
  }
  return false;
}

static bool throughIndirection(StringRef Path) {
  while (Path.consume_front(".")) {
    if (Path.consume_front("~."))
      continue;
    const StringRef Segment = Path.take_until([](char C) { return C == '.'; });
    Path = Path.drop_front(Segment.size());
    StringRef Number = Segment.take_while(llvm::isDigit);
    uintptr_t Address = 0;
    if (Number.empty() || Number.getAsInteger(10, Address))
      return true;
    // The keys of members are their declarations.
    const auto *Field = dyn_cast<FieldDecl>(
        reinterpret_cast<const ValueDecl *>(Address));
    if (!Field)
      return true;
    if (Field->getType()->isReferenceType())
      return true;
    if (Field->getType()->isPointerType() &&
        Segment.size() > Number.size() && Segment[Number.size()] == '[')
      return true;
  }
  return false;
}

static llvm::StringSet<> notCopiedBy(const LambdaExpr *Lambda,
                                     const llvm::StringSet<> &References) {
  llvm::StringSet<> Kept;
  for (const auto &Reference : References) {
    const StringRef Key = Reference.getKey();
    const bool Copied =
        llvm::any_of(Lambda->captures(), [&](const LambdaCapture &Capture) {
          // A copy of the object, `[*this]`, copies its members, but not
          // what they point or refer to.
          if (Capture.capturesThis() &&
              Capture.getCaptureKind() == LCK_StarThis)
            return Key == "this" ||
                   (Key.starts_with("this.") &&
                    !throughIndirection(Key.drop_front(4)));
          if (Capture.getCaptureKind() != LCK_ByCopy ||
              !Capture.capturesVariable())
            return false;
          const auto *Variable = dyn_cast<VarDecl>(Capture.getCapturedVar());
          if (!Variable)
            return false;
          // But not through a reference or pointer member of it, which the
          // copy shares, `[_View] { _View.m_Bits = 0; }` of `unsigned
          // &m_Bits`.
          const std::string Root = ReferenceCollector::declKey(Variable);
          if (Key == Root ||
              (Key.starts_with(Root + ".") &&
               !throughIndirection(Key.drop_front(Root.size()))))
            return true;
          // Also a local reference, whose copy is of what it refers to,
          // `[Bits]` of `unsigned &Bits = _Bits`, unless the lambda captures
          // that by reference too.
          const Expr *Aliased = ReferenceCollector::aliasedValue(Variable);
          const auto *Target =
              Aliased ? dyn_cast<DeclRefExpr>(Aliased->IgnoreParenImpCasts())
                      : nullptr;
          if (!Target ||
              llvm::any_of(Lambda->captures(), [&](const LambdaCapture &Other) {
                return Other.capturesVariable() &&
                       Other.getCaptureKind() == LCK_ByRef &&
                       Other.getCapturedVar() == Target->getDecl();
              }))
            return false;
          const std::string TargetRoot =
              ReferenceCollector::declKey(Target->getDecl());
          return Key == TargetRoot ||
                 (Key.starts_with(TargetRoot + ".") &&
                  !throughIndirection(Key.drop_front(TargetRoot.size())));
        });
    if (!Copied)
      Kept.insert(Key);
  }
  return Kept;
}

/// Whether \p S writes one of \p References, like `Value = 0`.
/// The functions whose bodies writesAny looks into, for what the call of
/// each is given, while it does, each with the number looked into outside it.
static thread_local llvm::DenseMap<const FunctionDecl *, unsigned> s_InCallee;
/// The least of those numbers of one that was reached again while looking
/// into a function, which what was found there then depends on.
static thread_local unsigned s_ReachedInCallee = UINT_MAX;
/// How many functions onlyWritesElseThrough looks into, each in the other.
static thread_local int s_ThroughDepth = 0;

/// Starts looking into the body of \p Function, unless it is looked into
/// already, which it then reached again, or deeper than is looked into.
static bool enterCallee(const FunctionDecl *Function) {
  if (s_InCallee.size() >= 8)
    return false;
  const auto [Entry, Inserted] =
      s_InCallee.try_emplace(Function, s_InCallee.size());
  if (!Inserted)
    s_ReachedInCallee = std::min(s_ReachedInCallee, Entry->second);
  return Inserted;
}
/// Whether what is written is of a part of an expression that runs before a
/// call in the same full-expression, where its temporaries are destroyed
/// only after the call, at its end.
static thread_local bool s_BeforeInFullExpression = false;

/// Whether the destructor of \p Record, when its body can be seen, writes
/// through a reference or pointer member, `m_Bits = 0` of `unsigned &m_Bits`
/// or `*m_pBits = 0`, also by giving it to a function that can,
/// `fg_Clear(m_Bits)`, which is of what it was made with.
static bool destructorWritesThroughMemberOnce(const CXXRecordDecl *Record,
                                              int Depth);
static bool destructorWritesThroughMember(const CXXRecordDecl *Record,
                                          int Depth) {
  if (!Record || !Record->hasDefinition() || Depth > 16)
    return false;
  // Once for each record at each depth, of the AST it is of: a base that
  // is virtual is a base of each class between it and the most derived,
  // and would be looked at again through each path to it, which a chain of
  // classes that each have several bases with the same virtual base makes
  // exponential in its length.
  struct CDestructorWrites {
    llvm::DenseMap<std::pair<const CXXRecordDecl *, int>, bool> Writes;
  };
  auto &Decided = astCache<CDestructorWrites>(Record->getASTContext()).Writes;
  const auto Key = std::make_pair(Record->getDefinition(), Depth);
  if (auto It = Decided.find(Key); It != Decided.end())
    return It->second;
  const bool Writes = destructorWritesThroughMemberOnce(Record, Depth);
  Decided[Key] = Writes;
  return Writes;
}
static bool destructorWritesThroughMemberOnce(const CXXRecordDecl *Record,
                                              int Depth) {
  // Also the destructors of its members and bases, which run after its body,
  // `struct CWrapper { CReset m_Reset; };`.
  for (const FieldDecl *Field : Record->fields())
    if (!Field->getType()->isReferenceType() &&
        destructorWritesThroughMember(
            Field->getType()->getBaseElementTypeUnsafe()->getAsCXXRecordDecl(),
            Depth + 1))
      return true;
  for (const CXXBaseSpecifier &Base : Record->bases())
    if (destructorWritesThroughMember(Base.getType()->getAsCXXRecordDecl(),
                                      Depth + 1))
      return true;
  const CXXDestructorDecl *Destructor = Record->getDestructor();
  const FunctionDecl *Definition =
      Destructor ? Destructor->getDefinition() : nullptr;
  if (!Definition || !Definition->getBody())
    return false;
  // Also in a member function of it that it calls, `f_Clear()`, whose body
  // can be seen, each once.
  llvm::SmallPtrSet<const FunctionDecl *, 4> Helpers;
  // The member of the object \p E is, also of a member that is an object,
  // `m_Data.m_pPointTo`, or null.
  auto OwnField = [](const Expr *E) -> const FieldDecl * {
    const auto *Member = dyn_cast<MemberExpr>(E->IgnoreParenImpCasts());
    const FieldDecl *Field =
        Member ? dyn_cast<FieldDecl>(Member->getMemberDecl()) : nullptr;
    for (int Level = 0; Member && Level < 8; ++Level) {
      const Expr *Base = Member->getBase()->IgnoreParenImpCasts();
      if (isa<CXXThisExpr>(Base))
        return Field;
      Member = Member->isArrow() ? nullptr : dyn_cast<MemberExpr>(Base);
    }
    return nullptr;
  };
  std::function<bool(const Stmt *)> Writes = [&](const Stmt *S) {
    if (!S)
      return false;
    // Deleting what a pointer member owns, `delete m_pCleanup`, runs the
    // destructor of what it points to, like a shared pointer of a scope
    // guard whose last owner it is.
    if (const auto *Delete = dyn_cast<CXXDeleteExpr>(S))
      if (const FieldDecl *Field = OwnField(Delete->getArgument());
          Field && Field->getType()->isPointerType() &&
          destructorWritesThroughMember(
              Delete->getDestroyedType().isNull()
                  ? nullptr
                  : Delete->getDestroyedType()->getAsCXXRecordDecl(),
              Depth + 1))
        return true;
    // Also in a lambda that captures the object, which runs while it is
    // destroyed, like the cleanup of a scope guard, `g_OnScopeExit / [this] {
    // m_Bits = 0; }`, or called there.
    if (const auto *Lambda = dyn_cast<LambdaExpr>(S))
      return llvm::any_of(Lambda->captures(),
                          [](const LambdaCapture &Capture) {
                            return Capture.capturesThis();
                          }) &&
             Writes(Lambda->getBody());
    if (const auto *Call = dyn_cast<CXXMemberCallExpr>(S);
        Call && Call->getImplicitObjectArgument() &&
        isa<CXXThisExpr>(
            Call->getImplicitObjectArgument()->IgnoreParenImpCasts()))
      if (const CXXMethodDecl *Method = Call->getMethodDecl())
        if (const FunctionDecl *Helper = Method->getDefinition();
            Helper && Helper->getBody() && Helpers.size() < 16 &&
            Helpers.insert(Helper).second && Writes(Helper->getBody()))
          return true;
    // An atomic write through a reference or pointer member,
    // `__atomic_store_n(&m_Bits, 0u, __ATOMIC_RELAXED)`.
    if (const auto *Atomic = dyn_cast<AtomicExpr>(S);
        Atomic && !isAtomicLoad(Atomic)) {
      const Expr *Pointer = Atomic->getPtr()->IgnoreParenImpCasts();
      bool Addressed = false;
      if (const auto *Address = dyn_cast<UnaryOperator>(Pointer);
          Address && Address->getOpcode() == UO_AddrOf) {
        Pointer = Address->getSubExpr()->IgnoreParenImpCasts();
        Addressed = true;
      }
      if (const auto *Member = dyn_cast<MemberExpr>(Pointer);
          Member && isa<CXXThisExpr>(Member->getBase()->IgnoreParenImpCasts()))
        if (const auto *Field = dyn_cast<FieldDecl>(Member->getMemberDecl());
            Field && (Addressed ? Field->getType()->isReferenceType()
                                : Field->getType()->isPointerType()))
          return true;
    }
    // A callable it keeps, called, `m_fOnExit()`, like the lambda of a scope
    // guard, can write what it refers to, also through a local reference to
    // it, `auto &fCleanup = m_fCleanup; fCleanup();`.
    if (const auto *Call = dyn_cast<CXXOperatorCallExpr>(S);
        Call && Call->getOperator() == OO_Call && Call->getNumArgs() >= 1) {
      const Expr *Callee = Call->getArg(0)->IgnoreParenImpCasts();
      for (int Level = 0; Level < 8; ++Level) {
        const auto *Ref = dyn_cast<DeclRefExpr>(Callee);
        const auto *Local = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
        if (!Local || !Local->isLocalVarDecl() ||
            !Local->getType()->isReferenceType() || !Local->getInit())
          break;
        Callee = Local->getInit()->IgnoreParenImpCasts();
      }
      if (const auto *Member = dyn_cast<MemberExpr>(Callee);
          Member && isa<FieldDecl>(Member->getMemberDecl()) &&
          isa<CXXThisExpr>(Member->getBase()->IgnoreParenImpCasts()))
        return true;
    }
    // Given to what can write it, a reference member to a non-const
    // reference, `fg_Clear(m_Bits)`, or a pointer member, or the address of
    // a reference member, to a pointer to non-const; also to what can't be
    // told, like a call through a pointer.
    if (const auto *Call = dyn_cast<CallExpr>(S)) {
      const FunctionDecl *Callee = Call->getDirectCallee();
      const unsigned Offset = isa<CXXOperatorCallExpr>(Call) &&
                                      isa_and_nonnull<CXXMethodDecl>(Callee)
                                  ? 1
                                  : 0;
      for (unsigned iArgument = Offset; iArgument < Call->getNumArgs();
           ++iArgument) {
        const QualType Type =
            Callee && iArgument - Offset < Callee->getNumParams()
                ? Callee->getParamDecl(iArgument - Offset)->getType()
                : QualType();
        const bool ToReference =
            Type.isNull() || (Type->isReferenceType() &&
                              !Type.getNonReferenceType().isConstQualified());
        const bool ToPointer =
            Type.isNull() || (Type->isPointerType() &&
                              !Type->getPointeeType().isConstQualified());
        if (!ToReference && !ToPointer)
          continue;
        const Expr *Argument = Call->getArg(iArgument)->IgnoreParenImpCasts();
        bool Addressed = false;
        if (const auto *Address = dyn_cast<UnaryOperator>(Argument);
            Address && Address->getOpcode() == UO_AddrOf) {
          Argument = Address->getSubExpr()->IgnoreParenImpCasts();
          Addressed = true;
        }
        // Also a member of a member that is an object, `m_Data.m_pPointTo`
        // given to what destroys what it points to, `fg_DeleteObject(...)`.
        const FieldDecl *Field = OwnField(Argument);
        if (Field &&
            ((Field->getType()->isReferenceType() &&
              (Addressed ? ToPointer : ToReference)) ||
             (Field->getType()->isPointerType() && !Addressed && ToPointer)))
          return true;
      }
    }
    const Expr *Target = nullptr;
    if (const auto *Assign = dyn_cast<BinaryOperator>(S);
        Assign && Assign->isAssignmentOp())
      Target = Assign->getLHS();
    else if (const auto *Step = dyn_cast<UnaryOperator>(S);
             Step && Step->isIncrementDecrementOp())
      Target = Step->getSubExpr();
    // Also an overloaded one, of an integer class, `m_Bits = 0` of a `uint256
    // &m_Bits`, whose object is what it writes.
    else if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(S);
             Operator && Operator->getNumArgs() >= 1 &&
             (Operator->isAssignmentOp() ||
              Operator->getOperator() == OO_PlusPlus ||
              Operator->getOperator() == OO_MinusMinus))
      Target = Operator->getArg(0);
    if (Target) {
      Target = Target->IgnoreParenImpCasts();
      bool Through = false;
      // Through a pointer member, `*m_pBits = 0`, also by subscript,
      // `m_pBits[0] = 0`, or a member of what it points to, `m_pInfo->m_Bits
      // = 0`.
      for (int Level = 0; Level < 8; ++Level)
        if (const auto *Deref = dyn_cast<UnaryOperator>(Target);
            Deref && Deref->getOpcode() == UO_Deref) {
          Target = Deref->getSubExpr()->IgnoreParenImpCasts();
          Through = true;
        } else if (const auto *Subscript = dyn_cast<ArraySubscriptExpr>(Target);
                   Subscript &&
                   Subscript->getBase()->getType()->isPointerType()) {
          Target = Subscript->getBase()->IgnoreParenImpCasts();
          Through = true;
        } else if (const auto *Pointed = dyn_cast<MemberExpr>(Target);
                   Pointed && Pointed->isArrow() &&
                   !isa<CXXThisExpr>(
                       Pointed->getBase()->IgnoreParenImpCasts())) {
          Target = Pointed->getBase()->IgnoreParenImpCasts();
          Through = true;
        } else if (const auto *Inner = dyn_cast<MemberExpr>(Target);
                   Inner && !Inner->isArrow() &&
                   isa<MemberExpr>(Inner->getBase()->IgnoreParenImpCasts())) {
          // A member of what a reference member refers to, `m_Info.m_Bits`.
          Target = Inner->getBase()->IgnoreParenImpCasts();
        } else
          break;
      if (const auto *Member = dyn_cast<MemberExpr>(Target);
          Member && isa<CXXThisExpr>(Member->getBase()->IgnoreParenImpCasts()))
        if (const auto *Field = dyn_cast<FieldDecl>(Member->getMemberDecl());
            Field && (Field->getType()->isReferenceType() ||
                      (Through && Field->getType()->isPointerType())))
          return true;
    }
    // Not what doesn't run: a discarded branch of an `if constexpr`, or what
    // `sizeof`, `noexcept` or `decltype` only names.
    if (const auto *If = dyn_cast<IfStmt>(S);
        If && If->isConstexpr() && !If->getCond()->isValueDependent())
      if (const std::optional<const Stmt *> Case =
              If->getNondiscardedCase(Definition->getASTContext()))
        return Writes(If->getInit()) || Writes(*Case);
    if (isUnevaluatedNode(S))
      return false;
    if (const auto *Choose = dyn_cast<ChooseExpr>(S);
        Choose && !Choose->isConditionDependent())
      return Writes(Choose->getChosenSubExpr());
    return llvm::any_of(childrenOnce(S), Writes);
  };
  return Writes(Definition->getBody());
}

/// The function that a call through a local pointer to it calls, which is
/// only made from it, `auto fClear = &fg_Clear; fClear();`, never assigned
/// nor given away by address, or through a constant one that isn't local.
/// Null when that can't be told.
static const FunctionDecl *localPointerTarget(const DeclRefExpr *Ref,
                                              int Depth);

static const FunctionDecl *calledThroughLocal(const CallExpr *Call) {
  // Also dereferenced, `(*fClear)()`.
  const Expr *Callee = Call->getCallee()->IgnoreParenImpCasts();
  if (const auto *Deref = dyn_cast<UnaryOperator>(Callee);
      Deref && Deref->getOpcode() == UO_Deref)
    Callee = Deref->getSubExpr()->IgnoreParenImpCasts();
  const auto *Ref = dyn_cast<DeclRefExpr>(Callee);
  return Ref ? localPointerTarget(Ref, 0) : nullptr;
}

/// The function that the variable \p Ref names points to, like
/// calledThroughLocal, also through an unchanged local copy of another,
/// `auto fCopy = fClear`, \p Depth deep.
static const FunctionDecl *localPointerTarget(const DeclRefExpr *Ref,
                                              int Depth) {
  const auto *Local = dyn_cast<VarDecl>(Ref->getDecl());
  // A reference to one, `auto &fClear = fg_Clear;`, which can't be bound to
  // another.
  if (Local && Local->getType()->isReferenceType() &&
      Local->getType().getNonReferenceType()->isFunctionType())
    if (const VarDecl *Defined = Local->getDefinition();
        Defined && Defined->getInit())
      if (const auto *Named =
              dyn_cast<DeclRefExpr>(Defined->getInit()->IgnoreParenImpCasts()))
        return dyn_cast<FunctionDecl>(Named->getDecl());
  // Also a constant one that isn't local, `auto const g_fClear =
  // &fg_Clear;`, which can't be pointed elsewhere.
  if (Local && !Local->hasLocalStorage() &&
      Local->getType().isConstQualified() &&
      Local->getType()->isFunctionPointerType())
    if (const VarDecl *Defined = Local->getDefinition();
        Defined && Defined->getInit()) {
      const Expr *Init = Defined->getInit()->IgnoreParenImpCasts();
      if (const auto *Address = dyn_cast<UnaryOperator>(Init);
          Address && Address->getOpcode() == UO_AddrOf)
        Init = Address->getSubExpr()->IgnoreParenImpCasts();
      const auto *Named = dyn_cast<DeclRefExpr>(Init);
      return Named ? dyn_cast<FunctionDecl>(Named->getDecl()) : nullptr;
    }
  if (!Local || !Local->hasLocalStorage() || isa<ParmVarDecl>(Local) ||
      !Local->getInit() || Local->getType()->isReferenceType())
    return nullptr;
  const Expr *Init = Local->getInit()->IgnoreParenImpCasts();
  if (const auto *Address = dyn_cast<UnaryOperator>(Init);
      Address && Address->getOpcode() == UO_AddrOf)
    Init = Address->getSubExpr()->IgnoreParenImpCasts();
  const auto *Named = dyn_cast<DeclRefExpr>(Init);
  const FunctionDecl *Target =
      Named ? dyn_cast<FunctionDecl>(Named->getDecl()) : nullptr;
  if (!Target && Named && Depth < 16)
    Target = localPointerTarget(Named, Depth + 1);
  const auto *Function =
      dyn_cast_or_null<FunctionDecl>(Local->getParentFunctionOrMethod());
  if (!Target || !Function || !Function->getBody())
    return nullptr;
  bool Changed = false;
  std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
    if (!S || Changed)
      return;
    auto Names = [&](const Expr *E) {
      const auto *Use = dyn_cast<DeclRefExpr>(E->IgnoreParenImpCasts());
      return Use && Use->getDecl() == Local;
    };
    if (const auto *Assign = dyn_cast<BinaryOperator>(S);
        Assign && Assign->isAssignmentOp() && Names(Assign->getLHS()))
      Changed = true;
    if (const auto *Address = dyn_cast<UnaryOperator>(S);
        Address && Address->getOpcode() == UO_AddrOf &&
        Names(Address->getSubExpr()))
      Changed = true;
    // Bound to a reference, which can change it, `auto &fAlias = fClear`.
    if (const auto *Declared = dyn_cast<DeclStmt>(S))
      for (const Decl *D : Declared->decls())
        if (const auto *Alias = dyn_cast<VarDecl>(D);
            Alias && Alias->getType()->isReferenceType() &&
            !Alias->getType().getNonReferenceType().isConstQualified() &&
            Alias->getInit() && Names(Alias->getInit()))
          Changed = true;
    // Given to a reference that isn't to const, `fg_Reset(fClear)`.
    if (const auto *Given = dyn_cast<CallExpr>(S))
      if (const FunctionDecl *Callee = Given->getDirectCallee())
        for (unsigned iArgument = 0; iArgument < Given->getNumArgs() &&
                                     iArgument < Callee->getNumParams();
             ++iArgument) {
          const QualType Type = Callee->getParamDecl(iArgument)->getType();
          if (Type->isReferenceType() &&
              !Type.getNonReferenceType().isConstQualified() &&
              Names(Given->getArg(iArgument)))
            Changed = true;
        }
    for (const Stmt *Child : childrenOnce(S))
      Visit(Child);
  };
  Visit(Function->getBody());
  return Changed ? nullptr : Target;
}

/// The function \p Call calls: its direct callee, or the one it calls through
/// a local pointer to it, like calledThroughLocal.
static const FunctionDecl *calledFunction(const CallExpr *Call) {
  if (const FunctionDecl *Callee = Call->getDirectCallee())
    return Callee;
  return calledThroughLocal(Call);
}

/// Whether \p Construct makes a local that nothing names after, `CClear
/// Clear(_Bits);`, with a constructor that can be seen to only read what it
/// is given or bind a reference member to it, `m_Bits(_Bits)`: making it
/// changes nothing, and only its destructor, at the end of its scope, can
/// write through what it keeps.
static bool onlyKeepsUnnamedUncached(const CXXConstructExpr *Construct);
static bool onlyKeepsUnnamed(const CXXConstructExpr *Construct) {
  const CXXConstructorDecl *Constructor = Construct->getConstructor();
  if (!Constructor)
    return false;
  // Each once, which the writes of many statements ask.
  struct CMemo {
    llvm::DenseMap<const CXXConstructExpr *, bool> Kept;
  };
  auto &Kept = astCache<CMemo>(Constructor->getASTContext()).Kept;
  if (auto It = Kept.find(Construct); It != Kept.end())
    return It->second;
  const bool Result = onlyKeepsUnnamedUncached(Construct);
  Kept[Construct] = Result;
  return Result;
}

static bool onlyKeepsUnnamedUncached(const CXXConstructExpr *Construct) {
  const CXXConstructorDecl *Constructor = Construct->getConstructor();
  const FunctionDecl *Defined =
      Constructor ? Constructor->getDefinition() : nullptr;
  if (!Defined || !Defined->getBody())
    return false;
  const auto *Definition = cast<CXXConstructorDecl>(Defined);
  ASTContext &Context = Definition->getASTContext();
  // The local it makes, also a temporary a reference extends, `auto const
  // &Clear = CClear{_Bits};`.
  const VarDecl *Local = nullptr;
  DynTypedNode Node = DynTypedNode::create(*Construct);
  for (int Depth = 0; Depth < 16 && !Local; ++Depth) {
    const auto Parents = instantiatedParents(Node, Context);
    if (Parents.empty())
      break;
    if ((Local = Parents[0].get<VarDecl>()))
      break;
    if (const auto *Materialize = Parents[0].get<MaterializeTemporaryExpr>();
        Materialize && !Materialize->getExtendingDecl())
      break;
    if (!Parents[0].get<ImplicitCastExpr>() && !Parents[0].get<ParenExpr>() &&
        !Parents[0].get<CXXFunctionalCastExpr>() &&
        !Parents[0].get<CXXBindTemporaryExpr>() &&
        !Parents[0].get<MaterializeTemporaryExpr>() &&
        !Parents[0].get<ExprWithCleanups>())
      break;
    Node = Parents[0];
  }
  const auto *Function =
      Local ? dyn_cast_or_null<FunctionDecl>(Local->getParentFunctionOrMethod())
            : nullptr;
  if (!Local || !Local->hasLocalStorage() || !Function || !Function->getBody())
    return false;
  // Nothing names it.
  std::function<bool(const Stmt *)> Names = [&](const Stmt *S) {
    if (const auto *Ref = dyn_cast_or_null<DeclRefExpr>(S))
      return Ref->getDecl() == Local;
    return S && llvm::any_of(childrenOnce(S), Names);
  };
  if (Names(Function->getBody()))
    return false;
  // Each use of a parameter is a read or binds a reference member.
  // Not a pointer read, `*_pBits = 0`, which writes what it points to, nor
  // a write through a reference member bound to it, `m_Bits = 0`.
  llvm::SmallPtrSet<const Expr *, 4> Bound;
  llvm::SmallPtrSet<const FieldDecl *, 4> BoundFields;
  for (const CXXCtorInitializer *Initializer : Definition->inits())
    if (const FieldDecl *Field = Initializer->getMember();
        Field && Field->getType()->isReferenceType() &&
        Initializer->getInit()) {
      Bound.insert(Initializer->getInit()->IgnoreParenImpCasts());
      BoundFields.insert(Field);
    }
  bool Only = true;
  auto WritesBound = [&](const Expr *Target) {
    const auto *Member = dyn_cast<MemberExpr>(Target->IgnoreParenImpCasts());
    const auto *Field =
        Member ? dyn_cast<FieldDecl>(Member->getMemberDecl()) : nullptr;
    return Field && BoundFields.contains(Field);
  };
  std::function<void(const Stmt *, const Stmt *)> Visit =
      [&](const Stmt *S, const Stmt *Above) {
        if (!S || !Only)
          return;
        if (const auto *Assign = dyn_cast<BinaryOperator>(S);
            Assign && Assign->isAssignmentOp() && WritesBound(Assign->getLHS()))
          Only = false;
        if (const auto *Step = dyn_cast<UnaryOperator>(S);
            Step && Step->isIncrementDecrementOp() &&
            WritesBound(Step->getSubExpr()))
          Only = false;
        if (const auto *Ref = dyn_cast<DeclRefExpr>(S);
            Ref && isa<ParmVarDecl>(Ref->getDecl())) {
          const auto *Read = dyn_cast_or_null<ImplicitCastExpr>(Above);
          if (!Bound.contains(Ref) &&
              !(Read && Read->getCastKind() == CK_LValueToRValue &&
                !Ref->getType()->isPointerType()))
            Only = false;
          return;
        }
        for (const Stmt *Child : childrenOnce(S))
          Visit(Child, S);
      };
  for (const CXXCtorInitializer *Initializer : Definition->inits())
    Visit(Initializer->getInit(), nullptr);
  Visit(Definition->getBody(), nullptr);
  return Only;
}

/// Whether the local \p Holder, like a container, is given one of
/// \p References in the function it is of: made of it, or given it by a
/// call of a member of it, `Clears.f_Insert(CClear(_Bits))`, also of an
/// element of it, `Clears[0] = CClear(_Bits)`.
static bool givenTo(const VarDecl *Holder,
                    const llvm::StringSet<> &References) {
  if (givesAny(Holder->getInit(), References))
    return true;
  const auto *Function = dyn_cast<FunctionDecl>(Holder->getDeclContext());
  if (!Function || !Function->getBody())
    return false;
  auto IsHolder = [&](const Expr *Object) {
    for (int Level = 0; Object && Level < 8; ++Level) {
      Object = Object->IgnoreParenImpCasts();
      if (const auto *Ref = dyn_cast<DeclRefExpr>(Object))
        return Ref->getDecl() == Holder;
      const auto *Access = dyn_cast<CXXOperatorCallExpr>(Object);
      if (!Access || Access->getNumArgs() < 1 ||
          (Access->getOperator() != OO_Subscript &&
           Access->getOperator() != OO_Star))
        return false;
      Object = Access->getArg(0);
    }
    return false;
  };
  bool Given = false;
  std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
    if (!S || Given)
      return;
    llvm::ArrayRef<const Expr *> Arguments;
    if (const auto *Call = dyn_cast<CXXMemberCallExpr>(S);
        Call && IsHolder(Call->getImplicitObjectArgument()))
      Arguments = llvm::ArrayRef(Call->getArgs(), Call->getNumArgs());
    else if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(S);
             Operator && Operator->isAssignmentOp() &&
             Operator->getNumArgs() == 2 && IsHolder(Operator->getArg(0)))
      Arguments = llvm::ArrayRef(Operator->getArgs(), Operator->getNumArgs())
                      .drop_front();
    if (llvm::any_of(Arguments, [&](const Expr *Argument) {
          return givesAny(Argument, References);
        })) {
      Given = true;
      return;
    }
    for (const Stmt *Child : childrenOnce(S))
      Visit(Child);
  };
  Visit(Function->getBody());
  return Given;
}

static bool writesAny(const Stmt *S, const llvm::StringSet<> &References);

/// Whether the function \p Callee, whose body can be seen, given at
/// \p Index a pointer to what holds one of \p References, writes through it
/// only what isn't one of them, like the count of an intrusive pointer,
/// `_pPtr->m_RefCount.f_Increase()`: what is read through it, `[0].m_Bits`,
/// is looked for through its parameter.
static bool onlyWritesElseThrough(const FunctionDecl *Callee, unsigned Index,
                                  const llvm::StringSet<> &References) {
  const FunctionDecl *Definition = Callee ? Callee->getDefinition() : nullptr;
  if (!Definition || !Definition->getBody() ||
      Index >= Definition->getNumParams())
    return false;
  if (s_ThroughDepth >= 4)
    return false;
  const std::string Parameter =
      ReferenceCollector::declKey(Definition->getParamDecl(Index));
  llvm::StringSet<> Through;
  for (StringRef Reference : References.keys()) {
    const size_t Pointee = Reference.find("[0]");
    if (Pointee == StringRef::npos)
      continue;
    Through.insert(Parameter + Reference.substr(Pointee).str());
  }
  if (Through.empty())
    return false;
  ++s_ThroughDepth;
  const llvm::scope_exit Done([] { --s_ThroughDepth; });
  return !writesAny(Definition->getBody(), Through);
}

/// Whether \p Pointer is the address of a member function that can't change
/// its object or what it is given, `&CState::f_Read` of a const one.
static bool isReadOnlyMethodAddress(const Expr *Pointer) {
  const auto *Address = dyn_cast<UnaryOperator>(Pointer->IgnoreParenImpCasts());
  const auto *Named =
      Address && Address->getOpcode() == UO_AddrOf
          ? dyn_cast<DeclRefExpr>(Address->getSubExpr()->IgnoreParens())
          : nullptr;
  const auto *Method =
      Named ? dyn_cast<CXXMethodDecl>(Named->getDecl()) : nullptr;
  return Method && Method->isConst() && !Method->isVirtual() &&
         !methodChangesObject(Method) &&
         llvm::none_of(Method->parameters(), [](const ParmVarDecl *Parameter) {
           const QualType Type = Parameter->getType();
           return (Type->isReferenceType() || Type->isPointerType()) &&
                  !Type->getPointeeType().isConstQualified();
         });
}

/// Whether \p Made binds such a member function to an object, `_pState->*
/// &CState::f_Read`, also by a function that binds it,
/// `fg_MemberFunctionFunctor(&CState::f_Read, &_State)`.
static bool bindsReadOnlyMethod(const Expr *Made) {
  for (int Level = 0; Made && Level < 8; ++Level) {
    Made = Made->IgnoreImplicit()->IgnoreParens();
    if (const auto *Construct = dyn_cast<CXXConstructExpr>(Made);
        Construct && Construct->getNumArgs() >= 1 &&
        !isa<CXXDefaultArgExpr>(Construct->getArg(0)))
      Made = Construct->getArg(0);
    else if (const auto *Cast = dyn_cast<CXXFunctionalCastExpr>(Made))
      Made = Cast->getSubExpr();
    else
      break;
  }
  if (const auto *Bind = dyn_cast_or_null<CXXOperatorCallExpr>(Made))
    return Bind->getOperator() == OO_ArrowStar && Bind->getNumArgs() == 2 &&
           isReadOnlyMethodAddress(Bind->getArg(1));
  const auto *Bind = dyn_cast_or_null<CallExpr>(Made);
  return Bind && Bind->getNumArgs() == 2 &&
         isReadOnlyMethodAddress(Bind->getArg(0)) &&
         !isa<LambdaExpr>(Bind->getArg(1)->IgnoreImplicit());
}

static bool writesAny(const Stmt *S, const llvm::StringSet<> &References) {
  if (!S)
    return false;
  // A default argument or member initializer runs where it is used, `void
  // fg_Work(int _Unused = (g_Bits = 0))`, which isn't a child; each one once
  // while the outermost question is answered, since it is shared by its uses,
  // and nested ones, `int fg_B(int _A = fg_A(), int _B = fg_A())`, would be
  // looked at a number of times that doubles with each level.
  const Expr *Default = nullptr;
  if (const auto *Argument = dyn_cast<CXXDefaultArgExpr>(S))
    Default = Argument->getExpr();
  else if (const auto *Initializer = dyn_cast<CXXDefaultInitExpr>(S))
    Default = Initializer->getExpr();
  if (Default) {
    static thread_local llvm::SmallPtrSet<const Expr *, 8> *s_pFollowed =
        nullptr;
    std::optional<llvm::SmallPtrSet<const Expr *, 8>> Owned;
    if (!s_pFollowed)
      s_pFollowed = &Owned.emplace();
    const llvm::scope_exit Release([&] {
      if (Owned)
        s_pFollowed = nullptr;
    });
    return s_pFollowed->insert(Default).second &&
           writesAny(Default, References);
  }
  // What isn't evaluated, like `sizeof(_Value = 0)` or
  // `__builtin_constant_p(_Value = 0)`, doesn't write, nor the arm of
  // `__builtin_choose_expr` it doesn't choose.
  if (isUnevaluatedNode(S))
    return false;
  // A suspension of a coroutine, `co_await fg_Work()`, lets what else runs
  // meanwhile write what isn't its own: a global or static, a member of the
  // object, which other calls of an actor can change, or what a pointer or
  // reference refers to, `*_pBits`, but not a local or a parameter it holds
  // by value, also not a member of one.
  // Also not what a reference or pointer member of one refers to,
  // `_View.m_Bits` of `unsigned &m_Bits`, which is shared. Of a key of a
  // variable and its members, the address of each, separated by `.`, with
  // `~` for a union, not one of what else is keyed, which can't be told.
  auto OwnedByFrame = [](StringRef Reference) {
    llvm::SmallVector<StringRef, 4> Segments;
    Reference.split(Segments, '.');
    bool First = true;
    for (StringRef Segment : Segments) {
      if (Segment == "~")
        continue;
      // Also an element of an array of its own, `Values[1]`, which is in it,
      // unlike what a pointer points to.
      const size_t Index = Segment.find('[');
      const StringRef Elements =
          Index == StringRef::npos ? StringRef() : Segment.substr(Index);
      Segment = Segment.take_front(Index);
      uintptr_t Value = 0;
      if (Segment.empty() || !llvm::all_of(Segment, llvm::isDigit) ||
          Segment.getAsInteger(10, Value))
        return false;
      const auto *Decl = reinterpret_cast<const ValueDecl *>(Value);
      // A function named, like the operator of an integer class, `~_Bits`,
      // which nothing changes.
      if (First && isa<FunctionDecl>(Decl) && Segments.size() == 1)
        return true;
      if (!Elements.empty() && !Decl->getType()->isConstantArrayType())
        return false;
      if (First) {
        const auto *Variable = dyn_cast<VarDecl>(Decl);
        if (!Variable || !Variable->hasLocalStorage())
          return false;
        First = false;
      } else if (!isa<FieldDecl>(Decl))
        return false;
      if (Decl->getType()->isReferenceType() ||
          Decl->getType()->isPointerType())
        return false;
    }
    return !First;
  };
  // Not an await that can be seen not to suspend, of an awaiter whose
  // `await_ready()` returns true, like `std::suspend_never`.
  auto NeverSuspends = [](const CoroutineSuspendExpr *Suspend) {
    const auto *Ready = Suspend->getReadyExpr()
                            ? dyn_cast<CXXMemberCallExpr>(
                                  Suspend->getReadyExpr()->IgnoreImplicit())
                            : nullptr;
    const FunctionDecl *Definition =
        Ready && Ready->getMethodDecl()
            ? Ready->getMethodDecl()->getDefinition()
            : nullptr;
    const auto *Body =
        Definition ? dyn_cast_or_null<CompoundStmt>(Definition->getBody())
                   : nullptr;
    const auto *Return = Body && Body->size() == 1
                             ? dyn_cast<ReturnStmt>(Body->body_front())
                             : nullptr;
    bool Value = false;
    return Return && Return->getRetValue() &&
           !Return->getRetValue()->isValueDependent() &&
           Return->getRetValue()->EvaluateAsBooleanCondition(
               Value, Definition->getASTContext()) &&
           Value;
  };
  // What an expression of them is keyed by, `expr:...` of `~_Bits` of an
  // integer class, is of the variables in it, which are keyed too.
  auto AllOwnedByFrame = [&] {
    bool Any = false;
    for (StringRef Reference : References.keys()) {
      if (Reference.starts_with("expr:"))
        continue;
      if (!OwnedByFrame(Reference))
        return false;
      uintptr_t Value = 0;
      if (Reference.getAsInteger(10, Value) ||
          !isa<FunctionDecl>(reinterpret_cast<const ValueDecl *>(Value)))
        Any = true;
    }
    return Any;
  };
  if (const auto *Suspend = dyn_cast<CoroutineSuspendExpr>(S);
      Suspend && !NeverSuspends(Suspend) && !AllOwnedByFrame())
    return true;
  if (isa<DependentCoawaitExpr>(S) && !AllOwnedByFrame())
    return true;
  if (const auto *Choose = dyn_cast<ChooseExpr>(S);
      Choose && !Choose->isConditionDependent())
    return writesAny(Choose->getChosenSubExpr(), References);
  // Nor a branch of an `if constexpr` that is discarded.
  if (const auto *If = dyn_cast<IfStmt>(S);
      If && If->isConstexpr() && s_CheckContext &&
      !If->getCond()->isValueDependent())
    if (const std::optional<const Stmt *> Case =
            If->getNondiscardedCase(*s_CheckContext))
      return writesAny(If->getInit(), References) ||
             writesAny(If->getCond(), References) ||
             writesAny(*Case, References);
  // Making a lambda doesn't run its body, only what initializes its captures,
  // like `[Value = (_Value = 0)]`.
  if (const auto *Lambda = dyn_cast<LambdaExpr>(S))
    return llvm::any_of(Lambda->capture_inits(), [&](const Expr *Init) {
      return writesAny(Init, References);
    });
  // A local of a block destroyed at its end, whose destructor writes through
  // a reference or pointer member, `~CReset() { m_Bits = 0; }`, made of
  // what is read, also through copies of another, `CReset Copy = Reset`, also
  // a temporary a local reference extends, `auto const &Reset =
  // CReset{_Bits};`, and made of a local lambda that writes it, `TCGuard
  // Guard{fReset}`.
  auto Extends = [](const VarDecl *Local) {
    const Expr *Init = Local->getInit();
    if (const auto *Cleanups = dyn_cast_or_null<ExprWithCleanups>(Init))
      Init = Cleanups->getSubExpr();
    const auto *Temporary =
        Init ? dyn_cast<MaterializeTemporaryExpr>(Init->IgnoreParens())
             : nullptr;
    return Local->getType()->isReferenceType() && Temporary &&
           Temporary->getExtendingDecl() == Local;
  };
  auto MadeWithLambdas = [&](const Expr *Made) {
    bool Writes = false;
    std::function<void(const Stmt *)> Visit = [&](const Stmt *Inner) {
      if (!Inner || Writes)
        return;
      // Also a copy of it, `auto fCopy = fClear;`.
      if (const auto *Ref = dyn_cast<DeclRefExpr>(Inner))
        if (const auto *Callable = dyn_cast<VarDecl>(Ref->getDecl());
            Callable && Callable->hasLocalStorage())
          if (const LambdaExpr *Lambda = calledLambda(Ref))
            Writes = writesAny(Lambda->getBody(), References);
      for (const Stmt *Child : childrenOnce(Inner))
        Visit(Child);
    };
    Visit(Made);
    return Writes;
  };
  // One cancelled before the block ends, `Cleanup.f_Clear();` or
  // `Cleanup.f_ClearFunctor();` of a scope guard, as a statement of it, with
  // nothing before that can leave the block to where it goes on, `break` or
  // `goto`, or in a `try`, what can throw. Also one moved into another that is,
  // `auto Moved = fg_Move(Cleanup); Moved.f_Clear();`, whose move leaves
  // nothing for it to run.
  std::function<bool(const CompoundStmt *, const Stmt *, const VarDecl *, int)>
      Cancelled = [&](const CompoundStmt *Block, const Stmt *Declared,
                      const VarDecl *Local, int Depth) {
        bool InTry = false;
        if (s_CheckContext) {
          DynTypedNode Node = DynTypedNode::create(*Block);
          for (int Level = 0; Level < 64 && !InTry; ++Level) {
            const auto Parents = instantiatedParents(
                Node, *const_cast<ASTContext *>(s_CheckContext));
            if (Parents.empty() || Parents[0].get<LambdaExpr>() ||
                Parents[0].get<FunctionDecl>())
              break;
            InTry = Parents[0].get<CXXTryStmt>() != nullptr;
            Node = Parents[0];
          }
        }
        std::function<bool(const Stmt *)> Leaves = [&](const Stmt *Inner) {
          if (!Inner || isa<LambdaExpr>(Inner))
            return false;
          if (isa<BreakStmt, ContinueStmt, GotoStmt, IndirectGotoStmt>(Inner))
            return true;
          if (InTry && isa<CXXThrowExpr>(Inner))
            return true;
          if (const auto *Call = dyn_cast<CallExpr>(Inner); InTry && Call) {
            const auto *Type = Call->getCallee()
                                   ->getType()
                                   ->getPointeeOrArrayElementType()
                                   ->getAs<FunctionProtoType>();
            const FunctionDecl *Callee = Call->getDirectCallee();
            if (!(Callee && Callee->getType()->getAs<FunctionProtoType>() &&
                  Callee->getType()->getAs<FunctionProtoType>()->isNothrow()) &&
                !(Type && Type->isNothrow()))
              return true;
          }
          // Also a constructor that can throw, `TCVector<int> Values(16)`.
          if (const auto *Construct = dyn_cast<CXXConstructExpr>(Inner);
              InTry && Construct) {
            const CXXConstructorDecl *Constructor = Construct->getConstructor();
            const auto *Prototype =
                Constructor->getType()->getAs<FunctionProtoType>();
            if (!Constructor->isTrivial() &&
                !(Prototype && Prototype->isNothrow()))
              return true;
          }
          return llvm::any_of(childrenOnce(Inner), Leaves);
        };
        auto MovedFrom = [&](const VarDecl *Other) {
          const Expr *Init = Other->getInit();
          if (!Init || Other->getType()->isReferenceType())
            return false;
          Init = Init->IgnoreImplicit();
          if (const auto *Construct = dyn_cast<CXXConstructExpr>(Init);
              Construct && Construct->getNumArgs() == 1 &&
              Construct->getConstructor()->isMoveConstructor())
            Init = Construct->getArg(0)->IgnoreImplicit();
          // Moved, `fg_Move(Cleanup)` or `static_cast<CCleanup &&>(Cleanup)`.
          const Expr *Moved = nullptr;
          if (const auto *Move =
                  dyn_cast<CallExpr>(Init->IgnoreParenImpCasts())) {
            const FunctionDecl *Mover = Move->getDirectCallee();
            if (Mover && Move->getNumArgs() == 1 && Mover->getIdentifier() &&
                (isMoveFunction(Mover->getName())))
              Moved = Move->getArg(0);
          } else if (const auto *Cast = dyn_cast<CXXStaticCastExpr>(
                         Init->IgnoreParenImpCasts());
                     Cast && Cast->getTypeAsWritten()->isRValueReferenceType())
            Moved = Cast->getSubExpr();
          const auto *Ref =
              Moved ? dyn_cast<DeclRefExpr>(Moved->IgnoreParenImpCasts())
                    : nullptr;
          return Ref && Ref->getDecl() == Local;
        };
        // The local that \p Statement removes what it holds from, as the
        // options name what does, `f_Clear` and `f_ClearFunctor` of
        // `TCOnScopeExit`.
        auto ClearOf = [](const Stmt *Statement) -> const Decl * {
          const auto *E = dyn_cast<Expr>(Statement);
          const auto *Clear = E ? dyn_cast<CXXMemberCallExpr>(
                                      E->IgnoreImplicit()->IgnoreParens())
                                : nullptr;
          const auto *Object =
              Clear && Clear->getImplicitObjectArgument()
                  ? dyn_cast<DeclRefExpr>(Clear->getImplicitObjectArgument()
                                              ->IgnoreParenImpCasts())
                  : nullptr;
          auto Removes = [](StringRef Name) {
            return llvm::any_of(
                vocabulary().RemovingMethods.names(), [&](StringRef Pattern) {
                  return Pattern.consume_back("*") ? Name.starts_with(Pattern)
                                                   : Name == Pattern;
                });
          };
          if (!Object || !Clear->getMethodDecl() ||
              !Clear->getMethodDecl()->getIdentifier() ||
              !Removes(Clear->getMethodDecl()->getName()))
            return nullptr;
          // Of one whose body can be seen, only one that writes what the
          // destructor of its class reads, `mp_bIsValid = false;` of
          // `TCOnScopeExit`, not one that empties what else the object holds,
          // `f_ClearCache()` of a class with a scope guard member.
          const CXXMethodDecl *Method = Clear->getMethodDecl();
          const FunctionDecl *Definition = Method->getDefinition();
          if (!Definition || !Definition->getBody())
            return Object->getDecl();
          const CXXDestructorDecl *Destructor =
              Method->getParent()->getDestructor();
          const FunctionDecl *Destroys =
              Destructor ? Destructor->getDefinition() : nullptr;
          if (!Destroys || !Destroys->getBody())
            return nullptr;
          // The members of the object a body names, `mp_bIsValid`, with
          // \p WrittenOnly those it writes: assigns, steps, or calls what
          // isn't const of, `mp_fOnExitFunctor.f_Clear()`.
          auto Members = [](const Stmt *Body, bool WrittenOnly) {
            llvm::SmallPtrSet<const Decl *, 8> Found;
            auto Add = [&](const Expr *E) {
              if (const auto *Member =
                      dyn_cast_or_null<MemberExpr>(E ? E->IgnoreParenImpCasts()
                                                     : nullptr);
                  Member && isa<FieldDecl>(Member->getMemberDecl()) &&
                  isa<CXXThisExpr>(Member->getBase()->IgnoreParenImpCasts()))
                Found.insert(Member->getMemberDecl()->getCanonicalDecl());
            };
            std::function<void(const Stmt *)> Visit = [&](const Stmt *Inner) {
              if (!Inner || isa<LambdaExpr>(Inner))
                return;
              if (!WrittenOnly)
                Add(dyn_cast<Expr>(Inner));
              else if (const auto *Assign = dyn_cast<BinaryOperator>(Inner);
                       Assign && Assign->isAssignmentOp())
                Add(Assign->getLHS());
              else if (const auto *Step = dyn_cast<UnaryOperator>(Inner);
                       Step && Step->isIncrementDecrementOp())
                Add(Step->getSubExpr());
              else if (const auto *Operator =
                           dyn_cast<CXXOperatorCallExpr>(Inner);
                       Operator && Operator->getNumArgs() >= 1 &&
                       (Operator->isAssignmentOp() ||
                        Operator->getOperator() == OO_PlusPlus ||
                        Operator->getOperator() == OO_MinusMinus))
                Add(Operator->getArg(0));
              else if (const auto *Call = dyn_cast<CXXMemberCallExpr>(Inner);
                       Call && Call->getMethodDecl() &&
                       !Call->getMethodDecl()->isConst())
                Add(Call->getImplicitObjectArgument());
              for (const Stmt *Child : childrenOnce(Inner))
                Visit(Child);
            };
            Visit(Body);
            return Found;
          };
          const auto Read = Members(Destroys->getBody(), false);
          for (const Decl *Written : Members(Definition->getBody(), true))
            if (Read.contains(Written))
              return Object->getDecl();
          return nullptr;
        };
        // Whether what follows \p From in the block uses \p Guard again, like
        // to arm it again, `Cleanup = fg_OnScopeExit(...)`, other than to
        // cancel it again.
        auto UsedAfter = [&](const Stmt *From, const VarDecl *Guard) {
          std::function<bool(const Stmt *)> Mentions = [&](const Stmt *Inner) {
            if (!Inner || isUnevaluatedNode(Inner))
              return false;
            if (const auto *Ref = dyn_cast<DeclRefExpr>(Inner);
                Ref && Ref->getDecl() == Guard)
              return true;
            return llvm::any_of(childrenOnce(Inner), Mentions);
          };
          bool Past = false;
          for (const Stmt *Statement : Block->body()) {
            if (Statement == From) {
              Past = true;
              continue;
            }
            if (Past && ClearOf(Statement) != Guard && Mentions(Statement))
              return true;
          }
          return false;
        };
        bool After = false;
        for (const Stmt *Statement : Block->body()) {
          if (Statement == Declared) {
            After = true;
            continue;
          }
          if (!After)
            continue;
          if (const auto *Moving = dyn_cast<DeclStmt>(Statement);
              Moving && Depth < 8)
            for (const Decl *D : Moving->decls())
              if (const auto *Other = dyn_cast<VarDecl>(D);
                  Other && MovedFrom(Other) &&
                  Cancelled(Block, Statement, Other, Depth + 1))
                return !UsedAfter(Statement, Local);
          if (ClearOf(Statement) == Local)
            return !UsedAfter(Statement, Local);
          if (Leaves(Statement))
            return false;
        }
        return false;
      };
  // Destroying \p Local at the end of \p Scope, what it is made of tells.
  auto DestroyingChanges = [&](const VarDecl *Local, const Stmt *Scope) {
    const Expr *Made = Local->getInit();
    // Nor a copy of its own, `TCUniquePointer<CState> pCopy(fg_Copy(
    // _pState))`, whose destruction destroys only that.
    if (isOwnCopy(Made))
      return false;
    // Not a copy of a smart pointer that shares what it points to, made of
    // one that still has it, `auto pCopy = _pState`, whose destruction only
    // gives up its own share.
    if (const auto *Copy =
            Made ? dyn_cast<CXXConstructExpr>(Made->IgnoreImplicit()) : nullptr;
        Copy && Copy->getNumArgs() == 1 &&
        Copy->getConstructor()->isCopyConstructor() &&
        Copy->getArg(0)->isLValue() &&
        sharesPointee(
            Local->getType().getNonReferenceType()->getAsCXXRecordDecl()))
      return false;
    // Also a copy of an element of a local, `auto Clear = Clears[0]` or the
    // variable of a loop over it, `for (auto Clear : Clears)`, of what it was
    // given, `Clears.f_Insert(CClear(_Bits))`.
    const VarDecl *Holder = nullptr;
    bool OfElement = false;
    for (int Depth = 0; Made && Depth < 8; ++Depth) {
      const Expr *Source = Made->IgnoreImplicit();
      if (const auto *Copy = dyn_cast<CXXConstructExpr>(Source);
          Copy && Copy->getNumArgs() == 1 &&
          Copy->getConstructor()->isCopyOrMoveConstructor())
        Source = Copy->getArg(0)->IgnoreParenImpCasts();
      const Expr *Element = Source;
      for (int Level = 0; Level < 8; ++Level)
        if (const auto *Access = dyn_cast<CXXOperatorCallExpr>(Element);
            Access && Access->getNumArgs() >= 1 &&
            (Access->getOperator() == OO_Subscript ||
             Access->getOperator() == OO_Star))
          Element = Access->getArg(0)->IgnoreParenImpCasts();
        else if (const auto *Deref = dyn_cast<UnaryOperator>(Element);
                 Deref && Deref->getOpcode() == UO_Deref)
          Element = Deref->getSubExpr()->IgnoreParenImpCasts();
        else if (const auto *Begin = dyn_cast<CXXMemberCallExpr>(Element);
                 Begin && Begin->getImplicitObjectArgument() &&
                 Begin->getMethodDecl() &&
                 Begin->getMethodDecl()->getIdentifier() &&
                 Begin->getMethodDecl()->getName() == "begin")
          Element = Begin->getImplicitObjectArgument()->IgnoreParenImpCasts();
        else
          break;
      const auto *Ref = dyn_cast<DeclRefExpr>(Source);
      const auto *Other = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
      if (Element != Source) {
        const auto *Accessed = dyn_cast<DeclRefExpr>(Element);
        const auto *Of =
            Accessed ? dyn_cast<VarDecl>(Accessed->getDecl()) : nullptr;
        if (!Of || !Of->hasLocalStorage() || Of == Local)
          break;
        // The hidden ones of a loop, `__begin1` and `__range1`.
        if ((Of->isImplicit() || Of->getType()->isReferenceType()) &&
            Of->getInit()) {
          OfElement = true;
          Made = Of->getInit();
          continue;
        }
        Holder = Of;
        break;
      }
      // What the range of a loop is, `Clears` of `__range1`.
      if (OfElement && Other && Other->hasLocalStorage() &&
          !Other->isImplicit() && !Other->getType()->isReferenceType() &&
          Other != Local) {
        Holder = Other;
        break;
      }
      if (!Other || !Other->hasLocalStorage() || !Other->getInit() ||
          Other == Local)
        break;
      Made = Other->getInit();
    }
    if (Holder && givenTo(Holder, References))
      return true;
    // What it is given only by a lambda whose body can be seen,
    // `g_OnScopeExit / [&] { fg_Observe(_Bits); }`, it changes only
    // when that body does.
    std::function<bool(const Stmt *)> GivesChanged =
        [&](const Stmt *Part) -> bool {
      if (!Part)
        return false;
      // Also what it captures, which it destroys, `[Cleanup = g_OnScopeExit /
      // [&] { _Bits = 0; }] {}`, when that destructor writes through what it
      // holds.
      if (const auto *Lambda = dyn_cast<LambdaExpr>(Part))
        return (givesAny(Lambda, References) &&
                writesAny(Lambda->getBody(), References)) ||
               llvm::any_of(Lambda->capture_inits(), [&](const Expr *Init) {
                 return Init &&
                        destructorWritesThroughMember(
                            Init->getType()->getAsCXXRecordDecl()) &&
                        GivesChanged(Init);
               });
      std::function<bool(const Stmt *)> HasLambda = [&](const Stmt *Inner) {
        return isa<LambdaExpr>(Inner) ||
               llvm::any_of(childrenOnce(Inner), HasLambda);
      };
      if (!HasLambda(Part))
        return touchesAny(Part, References);
      return llvm::any_of(childrenOnce(Part), GivesChanged);
    };
    if (Made && (GivesChanged(Made) || MadeWithLambdas(Made)))
      return true;
    // Or given it after it is made, `Clear.m_pBits = &_Bits;`.
    bool Given = false;
    std::function<void(const Stmt *)> Visit = [&](const Stmt *Inner) {
      if (!Inner || Given || isa<LambdaExpr>(Inner))
        return;
      if (const auto *Assign = dyn_cast<BinaryOperator>(Inner);
          Assign && Assign->isAssignmentOp() &&
          touchesAny(Assign->getRHS(), References)) {
        const Expr *Target = Assign->getLHS()->IgnoreParenImpCasts();
        while (const auto *Member = dyn_cast<MemberExpr>(Target)) {
          if (Member->isArrow())
            break;
          Target = Member->getBase()->IgnoreParenImpCasts();
        }
        if (const auto *Ref = dyn_cast<DeclRefExpr>(Target);
            Ref && Ref->getDecl() == Local &&
            Target != Assign->getLHS()->IgnoreParenImpCasts())
          Given = true;
      }
      for (const Stmt *Child : childrenOnce(Inner))
        Visit(Child);
    };
    Visit(Scope);
    return Given;
  };
  auto DestroyedWrites = [&](const VarDecl *Local) {
    return Local->hasLocalStorage() &&
           (!Local->getType()->isReferenceType() || Extends(Local)) &&
           destructorWritesThroughMember(Local->getType()
                                             .getNonReferenceType()
                                             ->getBaseElementTypeUnsafe()
                                             ->getAsCXXRecordDecl());
  };
  if (const auto *Compound = dyn_cast<CompoundStmt>(S))
    for (const Stmt *Statement : Compound->body())
      if (const auto *Declaration = dyn_cast<DeclStmt>(Statement))
        for (const Decl *D : Declaration->decls())
          if (const auto *Local = dyn_cast<VarDecl>(D);
              Local && DestroyedWrites(Local) &&
              !Cancelled(Compound, Statement, Local, 0) &&
              DestroyingChanges(Local, Compound))
            return true;
  // Also the variable of a loop over a range, destroyed after each run of
  // its body.
  if (const auto *Loop = dyn_cast<CXXForRangeStmt>(S))
    if (const VarDecl *Local = Loop->getLoopVariable();
        Local && DestroyedWrites(Local) &&
        DestroyingChanges(Local, Loop->getBody()))
      return true;
  if (const auto *Binary = dyn_cast<BinaryOperator>(S);
      Binary && Binary->isAssignmentOp() &&
      assignsAny(Binary->getLHS(), References))
    return true;
  if (const auto *Unary = dyn_cast<UnaryOperator>(S);
      Unary && Unary->isIncrementDecrementOp() &&
      assignsAny(Unary->getSubExpr(), References))
    return true;
  // Also overloaded, like `--Value` of an integer class, but of a smart
  // pointer, `pCopy = nullptr`, only the pointer, not what it shares with
  // its copies.
  if (const auto *Call = dyn_cast<CXXOperatorCallExpr>(S);
      Call &&
      (Call->isAssignmentOp() || Call->getOperator() == OO_PlusPlus ||
       Call->getOperator() == OO_MinusMinus) &&
      Call->getNumArgs() >= 1 &&
      (onlyPointerChanged(Call->getArg(0))
           ? namesAny(Call->getArg(0), References)
           : touchesAny(Call->getArg(0), References)))
    return true;
  // An atomic operation other than a load, `__atomic_store_n(&Value, 0,
  // ...)`, writes what it is given a pointer to.
  if (const auto *Atomic = dyn_cast<AtomicExpr>(S);
      Atomic && !isAtomicLoad(Atomic) &&
      touchesAny(Atomic->getPtr(), References))
    return true;
  // Also where it puts what it reads, `__atomic_load(&Other, &_Bits, ...)` or
  // `__atomic_exchange(&Other, &New, &_Bits, ...)`, and what a compare and
  // exchange that fails gives back, `__atomic_compare_exchange_n(&Other,
  // &_Bits, ...)`.
  if (const auto *Atomic = dyn_cast<AtomicExpr>(S)) {
    const Expr *Output = nullptr;
    if (Atomic->isCmpXChg())
      Output = Atomic->getVal1();
    else if (Atomic->getOp() == AtomicExpr::AO__atomic_load ||
             Atomic->getOp() == AtomicExpr::AO__scoped_atomic_load)
      Output = Atomic->getVal1();
    else if (Atomic->getOp() == AtomicExpr::AO__atomic_exchange ||
             Atomic->getOp() == AtomicExpr::AO__scoped_atomic_exchange)
      Output = Atomic->getVal2();
    if (Output && touchesAny(Output, References))
      return true;
  }
  // An output of assembly, `asm("..." : "=r"(_Value))`, writes it.
  if (const auto *Assembly = dyn_cast<AsmStmt>(S))
    for (unsigned iOutput = 0; iOutput < Assembly->getNumOutputs(); ++iOutput)
      if (assignsAny(Assembly->getOutputExpr(iOutput), References))
        return true;
  // A placement new makes a new object where it is given, `new (&Value)
  // unsigned(0)`.
  if (const auto *New = dyn_cast<CXXNewExpr>(S))
    for (const Expr *Placement : New->placement_arguments())
      if (touchesAny(Placement, References))
        return true;
  // A lambda given to a function whose body can be seen to call it,
  // `fg_Invoke([&] { _Bits = 0; })`, writes what it writes, also called
  // through an unchanged local pointer to it, `auto fInvoke = &fg_Invoke<...>;
  // fInvoke(fClear)`.
  if (const auto *Call = dyn_cast<CallExpr>(S);
      Call && (Call->getDirectCallee() || calledThroughLocal(Call))) {
    const FunctionDecl *Callee = Call->getDirectCallee()
                                     ? Call->getDirectCallee()
                                     : calledThroughLocal(Call);
    const FunctionDecl *Definition = Callee->getDefinition();
    const unsigned Offset =
        isa<CXXOperatorCallExpr>(Call) && hasImplicitObject(Definition) ? 1 : 0;
    for (unsigned iArgument = Offset;
         Definition && Definition->getBody() &&
         iArgument < Call->getNumArgs() &&
         iArgument - Offset < Definition->getNumParams();
         ++iArgument) {
      // Also a function given by name, `fg_Invoke(fg_Clear)`, or by its
      // address, `fg_Invoke(&fg_Clear)`.
      const LambdaExpr *Given = calledLambda(Call->getArg(iArgument));
      const Stmt *GivenBody = Given ? Given->getBody() : nullptr;
      if (!Given) {
        const Expr *Named = Call->getArg(iArgument)->IgnoreParenImpCasts();
        if (const auto *Address = dyn_cast<UnaryOperator>(Named);
            Address && Address->getOpcode() == UO_AddrOf)
          Named = Address->getSubExpr()->IgnoreParenImpCasts();
        const auto *Ref = dyn_cast<DeclRefExpr>(Named);
        const auto *Function =
            Ref ? dyn_cast<FunctionDecl>(Ref->getDecl()) : nullptr;
        const FunctionDecl *FunctionDefinition =
            Function ? Function->getDefinition() : nullptr;
        GivenBody =
            FunctionDefinition ? FunctionDefinition->getBody() : nullptr;
      }
      if (!GivenBody || !writesAny(GivenBody, References))
        continue;
      llvm::SmallPtrSet<const ParmVarDecl *, 8> Visited;
      if (callsParameter(Definition,
                         Definition->getParamDecl(iArgument - Offset), Visited,
                         calledFunction, [](const CXXRecordDecl *Record) {
                           return destructorWritesThroughMember(Record);
                         }))
        return true;
    }
  }
  // A function whose body can be looked into, that writes a global or a
  // static member that is read, `void fg_Clear() { g_Bits = 0; }`, also
  // through the functions it calls.
  // Also a constructor, `CReset Reset;`, with its initializers, and the
  // destructor of a local whose scope ends in it, `{ CReset Reset; }`, or of
  // a temporary.
  {
    llvm::StringSet<> Globals;
    for (StringRef Reference : References.keys())
      if (Reference.starts_with("g"))
        Globals.insert(Reference);
    // Also of the elements of an array, `CReset Resets[1]`.
    auto DestructorOf = [](QualType Type) -> const FunctionDecl * {
      const CXXRecordDecl *Record =
          Type->getBaseElementTypeUnsafe()->getAsCXXRecordDecl();
      return Record && Record->hasDefinition() ? Record->getDestructor()
                                               : nullptr;
    };
    // Each function once for what is looked for, as deep as it is looked
    // into, also where it is called on many paths, `fg_Work<N - 1>();
    // fg_Work<N - 1>();`, while the outermost one is looked into, unless what
    // it writes depends on a function outside it that it reached again.
    using CCalleeKey =
        std::tuple<const FunctionDecl *, unsigned, int, bool, std::string>;
    static thread_local std::map<CCalleeKey, bool> *s_pCalleeWrites = nullptr;
    std::string GlobalsKey;
    std::function<bool(const FunctionDecl *)> CalleeWrites =
        [&](const FunctionDecl *Callee) {
          // One called deeper than is looked into can write them; one that
          // calls itself writes what its other calls do.
          if (!Callee)
            return false;
          const unsigned Depth = s_InCallee.size();
          if (Depth >= 8)
            return Callee->hasBody();
          if (GlobalsKey.empty()) {
            SmallVector<StringRef, 4> Sorted(Globals.keys());
            llvm::sort(Sorted);
            for (StringRef Global : Sorted)
              (GlobalsKey += Global) += ',';
          }
          std::optional<std::map<CCalleeKey, bool>> Owned;
          if (!s_pCalleeWrites)
            s_pCalleeWrites = &Owned.emplace();
          const llvm::scope_exit Release([&] {
            if (Owned)
              s_pCalleeWrites = nullptr;
          });
          CCalleeKey Key{Callee->getCanonicalDecl(), Depth, s_ThroughDepth,
                         s_BeforeInFullExpression, GlobalsKey};
          if (const auto Known = s_pCalleeWrites->find(Key);
              Known != s_pCalleeWrites->end())
            return Known->second;
          if (!enterCallee(Callee->getCanonicalDecl()))
            return false;
          const unsigned OuterReached =
              std::exchange(s_ReachedInCallee, UINT_MAX);
          const FunctionDecl *Definition = nullptr;
          bool Writes = Callee->hasBody(Definition) && Definition &&
                        writesAny(Definition->getBody(), Globals);
          if (const auto *Constructor =
                  dyn_cast_or_null<CXXConstructorDecl>(Definition))
            for (const CXXCtorInitializer *Initializer : Constructor->inits())
              Writes = Writes || writesAny(Initializer->getInit(), Globals);
          // A destructor destroys the members and bases after its body,
          // `struct CWrapper { CReset m_Reset; };`.
          // Not of a union, which destroys a member only when its body does.
          if (const auto *Destructor = dyn_cast<CXXDestructorDecl>(Callee)) {
            const CXXRecordDecl *Record = Destructor->getParent();
            for (const FieldDecl *Field : Record->fields())
              Writes = Writes || (!Record->isUnion() &&
                                  CalleeWrites(DestructorOf(Field->getType())));
            for (const CXXBaseSpecifier &Base : Record->bases())
              Writes = Writes || CalleeWrites(DestructorOf(Base.getType()));
            for (const CXXBaseSpecifier &Base : Record->vbases())
              Writes = Writes || CalleeWrites(DestructorOf(Base.getType()));
          }
          s_InCallee.erase(Callee->getCanonicalDecl());
          if (s_ReachedInCallee >= Depth)
            s_pCalleeWrites->try_emplace(std::move(Key), Writes);
          s_ReachedInCallee = std::min(OuterReached, s_ReachedInCallee);
          return Writes;
        };
    if (!Globals.empty()) {
      if (const auto *Call = dyn_cast<CallExpr>(S);
          Call &&
          CalleeWrites(Call->getDirectCallee() ? Call->getDirectCallee()
                                               : calledThroughLocal(Call)))
        return true;
      if (const auto *Construct = dyn_cast<CXXConstructExpr>(S);
          Construct && CalleeWrites(Construct->getConstructor()))
        return true;
      // Not one a reference extends, `CReset const &Reset = CReset{};`, which
      // is destroyed with it, at the end of its scope.
      auto Extended = [&](const CXXBindTemporaryExpr *Bind) {
        if (!s_CheckContext)
          return false;
        DynTypedNode Node = DynTypedNode::create(*Bind);
        for (int Depth = 0; Depth < 8; ++Depth) {
          const auto Parents = instantiatedParents(
              Node, *const_cast<ASTContext *>(s_CheckContext));
          if (Parents.empty())
            return false;
          if (const auto *Materialize =
                  Parents[0].get<MaterializeTemporaryExpr>())
            return Materialize->getExtendingDecl() != nullptr;
          // Nor one that is the object a variable is made as, `auto Cleanup =
          // g_OnScopeExit / [&] { ... };`, which is the variable.
          if (Parents[0].get<VarDecl>())
            return true;
          // Also through what names its type, `CReset{}`.
          if (!Parents[0].get<ImplicitCastExpr>() &&
              !Parents[0].get<ParenExpr>() &&
              !Parents[0].get<CXXFunctionalCastExpr>() &&
              !Parents[0].get<ExprWithCleanups>())
            return false;
          Node = Parents[0];
        }
        return false;
      };
      if (const auto *Bind = dyn_cast<CXXBindTemporaryExpr>(S);
          Bind && !s_BeforeInFullExpression && !Extended(Bind) &&
          CalleeWrites(DestructorOf(Bind->getType())))
        return true;
    }
    // A temporary made of it, `(void)CClear{_Bits};`, whose destructor can
    // write through what it keeps, destroyed at the end of the statement.
    if (const auto *Bind = dyn_cast<CXXBindTemporaryExpr>(S);
        Bind && !s_BeforeInFullExpression &&
        destructorWritesThroughMember(Bind->getType()->getAsCXXRecordDecl()) &&
        givesAny(Bind->getSubExpr(), References)) {
      bool Extended = false;
      if (s_CheckContext) {
        DynTypedNode Node = DynTypedNode::create(*Bind);
        for (int Depth = 0; Depth < 8; ++Depth) {
          const auto Parents = instantiatedParents(
              Node, *const_cast<ASTContext *>(s_CheckContext));
          if (Parents.empty())
            break;
          if (const auto *Materialize =
                  Parents[0].get<MaterializeTemporaryExpr>()) {
            Extended = Materialize->getExtendingDecl() != nullptr;
            break;
          }
          if (Parents[0].get<VarDecl>()) {
            Extended = true;
            break;
          }
          if (!Parents[0].get<ImplicitCastExpr>() &&
              !Parents[0].get<ParenExpr>() &&
              !Parents[0].get<CXXFunctionalCastExpr>() &&
              !Parents[0].get<ExprWithCleanups>())
            break;
          Node = Parents[0];
        }
      }
      if (!Extended)
        return true;
    }
    // An object made with it, `auto pClear = new CClear{_Bits};`, deleted,
    // `delete pClear`, whose destructor can write through what it keeps,
    // also through unchanged copies of the pointer.
    if (const auto *Delete = dyn_cast<CXXDeleteExpr>(S);
        Delete && !Delete->getDestroyedType().isNull() &&
        destructorWritesThroughMember(
            Delete->getDestroyedType()->getAsCXXRecordDecl())) {
      const Expr *Deleted = Delete->getArgument();
      llvm::SmallPtrSet<const VarDecl *, 4> Followed;
      while (Deleted) {
        Deleted = Deleted->IgnoreParenImpCasts();
        const auto *Ref = dyn_cast<DeclRefExpr>(Deleted);
        const auto *Local = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
        if (!Local || !Local->hasLocalStorage() ||
            !Followed.insert(Local).second)
          break;
        const Expr *Value = assignedValueAt(Local, Ref);
        Deleted = Value ? Value : Local->getInit();
      }
      if (const auto *New = dyn_cast_or_null<CXXNewExpr>(Deleted);
          New && New->getInitializer() &&
          givesAny(New->getInitializer(), References))
        return true;
    }
    // So does clearing an owning pointer made of what refers to it, `pClear
    // .f_Clear()` of `TCUniquePointer<CClear> pClear = fg_Construct(_Bits)`,
    // also by assigning it, `pClear = nullptr`, which destroys what it points
    // to, also of what is assigned to it later, `pClear = fg_Construct(_Bits)`,
    // and of what a call it is made with constructs, like the scope guard of
    // `g_OnScopeExitShared / [&] { _Bits = 0; }`.
    const Expr *ClearedOwner = nullptr;
    if (const auto *Clear = dyn_cast<CXXMemberCallExpr>(S);
        Clear && Clear->getMethodDecl() &&
        Clear->getMethodDecl()->getIdentifier() &&
        llvm::is_contained({StringRef("f_Clear"), StringRef("reset"),
                            StringRef("f_ForceDelete")},
                           Clear->getMethodDecl()->getName()))
      ClearedOwner = Clear->getImplicitObjectArgument();
    else if (const auto *Assign = dyn_cast<CXXOperatorCallExpr>(S);
             Assign && Assign->getOperator() == OO_Equal &&
             Assign->getNumArgs() == 2)
      ClearedOwner = Assign->getArg(0);
    if (const Stmt *Clear = ClearedOwner ? S : nullptr) {
      const auto *Ref = dyn_cast<DeclRefExpr>(ClearedOwner->IgnoreParenImpCasts());
      const auto *Owner = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
      // Also through a local reference to it, `auto &pAlias = pCleanup;`.
      for (int Depth = 0; Owner && Owner->getType()->isReferenceType() &&
                          Owner->hasLocalStorage() && !isa<ParmVarDecl>(Owner) &&
                          Owner->getInit() && Depth < 8;
           ++Depth) {
        const auto *Aliased =
            dyn_cast<DeclRefExpr>(Owner->getInit()->IgnoreParenImpCasts());
        Owner = Aliased ? dyn_cast<VarDecl>(Aliased->getDecl()) : nullptr;
      }
      const auto *Pointer =
          Owner && Owner->hasLocalStorage()
              ? dyn_cast_or_null<ClassTemplateSpecializationDecl>(
                    Owner->getType()->getAsCXXRecordDecl())
              : nullptr;
      const CXXRecordDecl *Pointee =
          Pointer && Pointer->getTemplateArgs().size() >= 1 &&
                  Pointer->getTemplateArgs()[0].getKind() ==
                      TemplateArgument::Type
              ? Pointer->getTemplateArgs()[0].getAsType()->getAsCXXRecordDecl()
              : nullptr;
      if (Pointee && destructorWritesThroughMember(Pointee)) {
        bool Gives = false;
        std::function<void(const Stmt *)> Made = [&](const Stmt *Inner) {
          if (!Inner || Gives || isa<LambdaExpr>(Inner))
            return;
          if (const auto *New = dyn_cast<CXXNewExpr>(Inner)) {
            Gives = New->getInitializer() &&
                    givesAny(New->getInitializer(), References);
            return;
          }
          if (const auto *Construct = dyn_cast<CallExpr>(Inner);
              Construct &&
              llvm::any_of(Construct->arguments(), [&](const Expr *Arg) {
                return givesAny(Arg, References);
              })) {
            Gives = true;
            return;
          }
          for (const Stmt *Child : childrenOnce(Inner))
            Made(Child);
        };
        Made(Owner->getInit());
        // Only one that can run before the clearing, written before it or in
        // a loop around both, not one after it, `pClear = fg_Construct(_Bits)`
        // once the scan is done.
        const auto *Function = dyn_cast<FunctionDecl>(Owner->getDeclContext());
        const SourceManager &SM = Owner->getASTContext().getSourceManager();
        auto Contains = [&](const Stmt *Outer, const Stmt *Inner) {
          return !SM.isBeforeInTranslationUnit(Inner->getBeginLoc(),
                                               Outer->getBeginLoc()) &&
                 !SM.isBeforeInTranslationUnit(Outer->getEndLoc(),
                                               Inner->getEndLoc());
        };
        llvm::SmallVector<const Stmt *, 4> Loops;
        std::function<void(const Stmt *)> Assigned = [&](const Stmt *Inner) {
          if (!Inner || Gives)
            return;
          if (const auto *Assign = dyn_cast<CXXOperatorCallExpr>(Inner);
              Assign && Assign->getOperator() == OO_Equal &&
              Assign->getNumArgs() == 2)
            if (const auto *To = dyn_cast<DeclRefExpr>(
                    Assign->getArg(0)->IgnoreParenImpCasts());
                To && To->getDecl() == Owner &&
                (SM.isBeforeInTranslationUnit(Assign->getBeginLoc(),
                                              Clear->getBeginLoc()) ||
                 llvm::any_of(Loops, [&](const Stmt *Loop) {
                   return Contains(Loop, Clear);
                 })))
              Made(Assign->getArg(1));
          const bool Loop =
              isa<ForStmt, WhileStmt, DoStmt, CXXForRangeStmt>(Inner);
          if (Loop)
            Loops.push_back(Inner);
          for (const Stmt *Child : childrenOnce(Inner))
            Assigned(Child);
          if (Loop)
            Loops.pop_back();
        };
        if (Function)
          Assigned(Function->getBody());
        if (Gives)
          return true;
      }
    }
    if (!Globals.empty()) {
      // And `delete pReset`, which runs the destructor of what it deletes,
      // and the `operator delete` that frees it.
      if (const auto *Delete = dyn_cast<CXXDeleteExpr>(S);
          Delete && ((!Delete->getDestroyedType().isNull() &&
                      CalleeWrites(DestructorOf(Delete->getDestroyedType()))) ||
                     CalleeWrites(Delete->getOperatorDelete())))
        return true;
      // Also the `operator new` that `new CReset` allocates with.
      if (const auto *New = dyn_cast<CXXNewExpr>(S);
          New && CalleeWrites(New->getOperatorNew()))
        return true;
      if (const auto *Compound = dyn_cast<CompoundStmt>(S))
        for (const Stmt *Statement : Compound->body())
          if (const auto *Declaration = dyn_cast<DeclStmt>(Statement))
            for (const Decl *D : Declaration->decls())
              if (const auto *Local = dyn_cast<VarDecl>(D);
                  Local && Local->hasLocalStorage() &&
                  CalleeWrites(DestructorOf(
                      Extends(Local) ? Local->getType().getNonReferenceType()
                                     : Local->getType())))
                return true;
    }
  }
  // A const member function that only writes mutable members of its own,
  // `++m_nQueries`, writes only what is read of those.
  // So does one that isn't const that only writes members of its own,
  // `Cleanup.f_Clear()` of `m_bActive = false`, not through what it holds.
  bool OnlyOwnMutable = false;
  if (const auto *Call = dyn_cast<CXXMemberCallExpr>(S);
      Call && Call->getMethodDecl() && Call->getImplicitObjectArgument() &&
      !isa<CXXThisExpr>(
          Call->getImplicitObjectArgument()->IgnoreParenImpCasts())) {
    llvm::SmallPtrSet<const CXXMethodDecl *, 8> Visiting;
    if (const auto Fields =
            mutableMembersWritten(Call->getMethodDecl(), Visiting,
                                  /*Own=*/!Call->getMethodDecl()->isConst())) {
      OnlyOwnMutable = true;
      for (const FieldDecl *Field : *Fields) {
        const std::string Segment = "." + ReferenceCollector::declKey(Field);
        for (StringRef Reference : References.keys()) {
          llvm::StringSet<> One;
          One.insert(Reference);
          if (Reference.contains(Segment) &&
              touchesAny(Call->getImplicitObjectArgument(), One))
            return true;
        }
      }
    }
  }
  if (const auto *Call = dyn_cast<CXXMemberCallExpr>(S);
      Call && !OnlyOwnMutable && changesObject(Call) &&
      Call->getImplicitObjectArgument()) {
    if (isa<CXXThisExpr>(
            Call->getImplicitObjectArgument()->IgnoreParenImpCasts()) &&
        llvm::any_of(References.keys(), [](StringRef Reference) {
          return Reference == "this" || Reference.starts_with("this.");
        }))
      return true;
    if (onlyPointerChanged(Call->getImplicitObjectArgument())
            ? namesAny(Call->getImplicitObjectArgument(), References)
            : touchesAny(Call->getImplicitObjectArgument(), References))
      return true;
  }
  // A function that gets it by a reference or a pointer that can change it,
  // like `fg_Clear(Value)` or `fg_Clear(&Value)`, also through a pointer to
  // the function.
  auto ParameterTypes = [](const FunctionDecl *Callee, const Expr *CalleeExpr) {
    llvm::SmallVector<QualType, 4> Types;
    if (Callee) {
      for (const ParmVarDecl *Parameter : Callee->parameters())
        Types.push_back(Parameter->getType());
      return Types;
    }
    if (!CalleeExpr)
      return Types;
    QualType Type = CalleeExpr->getType();
    // Through a pointer to a member function, `(_Worker.*fClear)(_Bits)`.
    if (const auto *Pointer =
            dyn_cast<BinaryOperator>(CalleeExpr->IgnoreParens());
        Pointer && Pointer->isPtrMemOp())
      if (const auto *Member =
              Pointer->getRHS()->getType()->getAs<MemberPointerType>())
        Type = Member->getPointeeType();
    if (Type->isPointerType() || Type->isReferenceType())
      Type = Type->getPointeeType();
    if (const auto *Prototype = Type->getAs<FunctionProtoType>())
      llvm::append_range(Types, Prototype->getParamTypes());
    return Types;
  };
  auto ChangesArguments = [&](llvm::ArrayRef<QualType> Types,
                              llvm::ArrayRef<const Expr *> Arguments,
                              const FunctionDecl *Callee) {
    for (unsigned iArgument = 0;
         iArgument < Arguments.size() && iArgument < Types.size();
         ++iArgument) {
      const QualType Type = Types[iArgument];
      const Expr *Argument = Arguments[iArgument]->IgnoreParenImpCasts();
      // A pointer to it that the function only points elsewhere,
      // `fg_Reset(pBits)` of `_pBits = nullptr`, also given by its address,
      // `fg_Reset(&pBits)` of `*_ppBits = nullptr`, doesn't change it.
      // Not what a pointer to it given as it is points to, `*_pBits = 0`.
      const auto *Address = dyn_cast<UnaryOperator>(Argument);
      const bool OfPointer =
          Address && Address->getOpcode() == UO_AddrOf
              ? Type->isPointerType() &&
                    Address->getSubExpr()->getType()->isPointerType()
              : Type->isReferenceType() && Argument->getType()->isPointerType();
      if (OfPointer && onlyAssignsThrough(Callee, iArgument))
        continue;
      // Nor a lambda given to a function whose body can be seen, which
      // changes what it captures only when it is called, which is told
      // above, not when it is kept, `g_OnScopeExit / [&] { _Bits = 0; }`.
      if (isa<LambdaExpr>(Argument) && Callee && Callee->getDefinition() &&
          Callee->getDefinition()->getBody())
        continue;
      // Nor a temporary made of it that has its own storage, `fg_Consume(
      // fg_TempCopy(_Bits))` of a vector, or `uint256(_Bits)`: one that
      // shares nothing with what it is made of, through a reference member,
      // or a pointer member that its copies don't copy what it points to of,
      // which a constructor of its own tells.
      if (const auto *Temporary =
              dyn_cast<Expr>(Argument->IgnoreImplicit()->IgnoreParens());
          Temporary && Temporary->isPRValue())
        if (const CXXRecordDecl *Made =
                Temporary->getType()->getAsCXXRecordDecl();
            Made && Made->hasDefinition() && !sharesPointee(Made)) {
          bool HasReferences = false;
          bool HasPointers = false;
          llvm::SmallVector<const CXXRecordDecl *, 4> Records{
              Made->getDefinition()};
          llvm::SmallPtrSet<const CXXRecordDecl *, 8> Seen;
          while (!Records.empty()) {
            const CXXRecordDecl *Record = Records.pop_back_val();
            if (!Record || !Record->hasDefinition() ||
                !Seen.insert(Record->getDefinition()).second)
              continue;
            Record = Record->getDefinition();
            for (const FieldDecl *Field : Record->fields()) {
              HasReferences |= Field->getType()->isReferenceType();
              HasPointers |=
                  Field->getType()->getBaseElementTypeUnsafe()->isPointerType();
              Records.push_back(Field->getType()
                                    ->getBaseElementTypeUnsafe()
                                    ->getAsCXXRecordDecl());
            }
            for (const CXXBaseSpecifier &Base : Record->bases())
              Records.push_back(Base.getType()->getAsCXXRecordDecl());
          }
          if (!HasReferences &&
              (!HasPointers || Made->hasUserDeclaredCopyConstructor()))
            continue;
        }
      // Nor one taken by a reference that isn't to const that the function
      // can be seen to only read, `TCVector(TCVector &_Other)` that copies
      // through `fg_Const(_Other)`.
      // Nor by a constructor that only keeps where it is, `TCReference(t_CType
      // &_Value) { m_Data.m_pPointTo = &_Value; }`, whose object is then
      // looked through, like any that refers to it.
      // A move, `fg_Move(_pState)`, also to what takes it by `&&`, changes
      // what is moved from, the pointer it holds, `_pState`, not what that
      // points to, which what it is moved to then has.
      const Expr *Moved = nullptr;
      if (Callee && Callee->getIdentifier() &&
          isMoveOrForwardFunction(Callee->getName()))
        Moved = Argument;
      else if (const auto *Move = dyn_cast<CallExpr>(Argument);
               Move && Move->getNumArgs() == 1 && Move->getDirectCallee() &&
               Move->getDirectCallee()->getIdentifier() &&
               isMoveOrForwardFunction(Move->getDirectCallee()->getName()))
        Moved = Move->getArg(0);
      // But moved on to a function, `fg_Consume(fg_Move(pCopy))`, it gives
      // that what it points to too, which it can write; not to a
      // constructor, which takes it over, `auto pOwned = fg_Move(_pState)`,
      // through whose object a write is looked for as through a copy.
      const bool MovedOn = Moved && Callee &&
                           !isa<CXXConstructorDecl>(Callee) &&
                           !(Callee->getIdentifier() &&
                             isMoveOrForwardFunction(Callee->getName()));
      if (Type->isReferenceType() &&
          !Type.getNonReferenceType().isConstQualified() &&
          (Moved ? namesAny(Moved, References) ||
                       (MovedOn && touchesAny(Moved, References))
                 : touchesAny(Argument, References)) &&
          !onlyReadsParameter(Callee, iArgument) &&
          !onlyKeepsParameter(Callee, iArgument))
        return true;
      if (const auto *Address = dyn_cast<UnaryOperator>(Argument);
          Address && Address->getOpcode() == UO_AddrOf &&
          Type->isPointerType() && !Type->getPointeeType().isConstQualified() &&
          touchesAny(Address->getSubExpr(), References))
        return true;
      // A pointer to it given on, `fg_Clear(_pValue)` of `*_pValue`, also
      // read through another, `fg_Clear(*_ppValue)`.
      const auto *Unary = dyn_cast<UnaryOperator>(Argument);
      if (Type->isPointerType() && !Type->getPointeeType().isConstQualified() &&
          Argument->getType()->isPointerType() &&
          (!Unary || Unary->getOpcode() == UO_Deref) &&
          touchesAny(Argument, References) &&
          !onlyWritesElseThrough(Callee, iArgument, References))
        return true;
    }
    return false;
  };
  // A member operator that can change its object, `_Bits()` with a
  // non-const `operator()`, also a const one that writes through what it
  // refers to, `m_Bits = 0` of `umint &m_Bits`, but not a lambda called where
  // it is made, whose body is looked at above, which changes only its own
  // copies, nor one that only gives access to it, like `operator[]`, also
  // one that inserts what it is asked for, `_Masks[_Key]` of a map, which
  // doesn't change the elements already in it.
  // Nor the call of a local made by binding a member function that can't
  // change its object, `fRead()` of `auto fRead = fg_MemberFunctionFunctor(
  // &CState::f_Read, &_State)`.
  auto CallsReadOnlyBinding = [](const CXXOperatorCallExpr *Operator) {
    const auto *Called =
        Operator->getOperator() == OO_Call && Operator->getNumArgs() >= 1
            ? dyn_cast<DeclRefExpr>(Operator->getArg(0)->IgnoreParenImpCasts())
            : nullptr;
    const auto *Bound = Called ? dyn_cast<VarDecl>(Called->getDecl()) : nullptr;
    if (!Bound || !Bound->hasLocalStorage() || isa<ParmVarDecl>(Bound) ||
        Bound->getType()->isReferenceType())
      return false;
    const Expr *Value = assignedValueAt(Bound, Called);
    return bindsReadOnlyMethod(Value ? Value : Bound->getInit());
  };
  if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(S))
    if (const auto *Method =
            dyn_cast_or_null<CXXMethodDecl>(Operator->getDirectCallee());
        Method && !Method->isStatic() && !CallsReadOnlyBinding(Operator) &&
        (!Method->isConst() || methodChangesObject(Method)) &&
        !onlyGivesAccess(Method) &&
        !(Operator->getOperator() == OO_Subscript && !Method->isConst() &&
          Operator->isLValue()) &&
        Operator->getNumArgs() >= 1 && Operator->getOperator() != OO_Equal &&
        !isa<LambdaExpr>(
            Operator->getArg(0)->IgnoreImplicit()->IgnoreParens())) {
      if (touchesAny(Operator->getArg(0), References))
        return true;
      // Also what the local it is called on was made with, like the lambda
      // of a scope guard called early, `Cleanup()` of `auto Cleanup =
      // g_OnScopeExit / [&] { _Bits = 0; }`, but not a lambda itself, whose
      // body is looked at above.
      // An element of a local container, `fWork()` of `for (auto &fWork :
      // Work)` or `Work[0]()`, runs what it was given, `Work.f_Insert([&] {
      // _Bits = 0; })`.
      if (Operator->getOperator() == OO_Call) {
        const Expr *Called = Operator->getArg(0)->IgnoreParenImpCasts();
        const VarDecl *Holder = nullptr;
        // Also what an optional holds, `(*fClear)()`, also through a getter,
        // `fClear.f_Get()()`, and a member of a local made with one,
        // `State.m_fClear()` of `CState State{[&] { _Bits = 0; }}`.
        bool Stepped = false;
        for (int Level = 0; Level < 8; ++Level) {
          if (const auto *Element = dyn_cast<CXXOperatorCallExpr>(Called);
              Element &&
              (Element->getOperator() == OO_Subscript ||
               Element->getOperator() == OO_Star) &&
              Element->getNumArgs() >= 1)
            Called = Element->getArg(0)->IgnoreParenImpCasts();
          else if (const auto *Getter = dyn_cast<CXXMemberCallExpr>(Called);
                   Getter && Getter->getNumArgs() == 0 && Getter->isLValue() &&
                   Getter->getImplicitObjectArgument())
            Called = Getter->getImplicitObjectArgument()->IgnoreParenImpCasts();
          else if (const auto *Member = dyn_cast<MemberExpr>(Called);
                   Member && !Member->isArrow() &&
                   isa<FieldDecl>(Member->getMemberDecl()))
            Called = Member->getBase()->IgnoreParenImpCasts();
          else
            break;
          Stepped = true;
        }
        if (!Stepped) {
          if (const auto *Ref = dyn_cast<DeclRefExpr>(Called);
              Ref && isa<VarDecl>(Ref->getDecl()) &&
              cast<VarDecl>(Ref->getDecl())->isCXXForRangeDecl()) {
            // Through the hidden ones of the loop, `*__begin1` of
            // `__range1.begin()` of `__range1 = Work`.
            const Expr *Through = cast<VarDecl>(Ref->getDecl())->getInit();
            for (int Level = 0; Through && Level < 8; ++Level) {
              Through = Through->IgnoreImplicit()->IgnoreParenImpCasts();
              if (const auto *Deref = dyn_cast<UnaryOperator>(Through);
                  Deref && Deref->getOpcode() == UO_Deref)
                Through = Deref->getSubExpr();
              else if (const auto *Star =
                           dyn_cast<CXXOperatorCallExpr>(Through);
                       Star && Star->getOperator() == OO_Star &&
                       Star->getNumArgs() == 1)
                Through = Star->getArg(0);
              else if (const auto *Begin = dyn_cast<CXXMemberCallExpr>(Through))
                Through = Begin->getImplicitObjectArgument();
              else if (const auto *Free = dyn_cast<CallExpr>(Through);
                       Free && Free->getNumArgs() == 1)
                Through = Free->getArg(0);
              else if (const auto *Hidden = dyn_cast<DeclRefExpr>(Through);
                       Hidden && isa<VarDecl>(Hidden->getDecl()) &&
                       cast<VarDecl>(Hidden->getDecl())->isImplicit())
                Through = cast<VarDecl>(Hidden->getDecl())->getInit();
              else
                break;
            }
            Called = Through ? Through->IgnoreParenImpCasts() : nullptr;
          } else
            Called = nullptr;
        }
        if (const auto *Ref = dyn_cast_or_null<DeclRefExpr>(Called))
          Holder = dyn_cast<VarDecl>(Ref->getDecl());
        if (Holder && Holder->hasLocalStorage() && !isa<ParmVarDecl>(Holder) &&
            !Holder->getType()->isReferenceType() && givenTo(Holder, References))
          return true;
      }
      // Of a function wrapper made of a lambda whose body can be seen,
      // `TCFunction<umint ()> fRead = [&] { return _Bits; }`, what that body
      // does, also of one assigned to it, `fRead = [&] { ... };`: what it has
      // where it is called, or else any it is given.
      if (const auto *Ref =
              dyn_cast<DeclRefExpr>(Operator->getArg(0)->IgnoreParenImpCasts());
          Ref && Operator->getOperator() == OO_Call)
        if (const auto *Local = dyn_cast<VarDecl>(Ref->getDecl());
            Local && Local->hasLocalStorage() &&
            !Local->getType()->isReferenceType() &&
            !(Local->getType()->getAsCXXRecordDecl() &&
              Local->getType()->getAsCXXRecordDecl()->isLambda())) {
          llvm::SmallVector<const Expr *, 2> Values =
              assignedValuesAt(Local, Ref);
          if (Values.empty()) {
            if (Local->getInit())
              Values.push_back(Local->getInit());
            const auto *Function = dyn_cast_or_null<FunctionDecl>(
                Local->getParentFunctionOrMethod());
            std::function<void(const Stmt *)> Given = [&](const Stmt *Inner) {
              if (!Inner)
                return;
              if (const auto *Assign = dyn_cast<CXXOperatorCallExpr>(Inner);
                  Assign && Assign->getOperator() == OO_Equal &&
                  Assign->getNumArgs() == 2)
                if (const auto *To = dyn_cast<DeclRefExpr>(
                        Assign->getArg(0)->IgnoreParenImpCasts());
                    To && To->getDecl() == Local)
                  Values.push_back(Assign->getArg(1));
              for (const Stmt *Child : childrenOnce(Inner))
                Given(Child);
            };
            if (Function)
              Given(Function->getBody());
          }
          for (const Expr *Value : Values) {
            if (!givesAny(Value, References))
              continue;
            const Expr *Made = Value;
            for (int Level = 0; Made && Level < 8; ++Level) {
              Made = Made->IgnoreImplicit()->IgnoreParens();
              if (const auto *Construct = dyn_cast<CXXConstructExpr>(Made);
                  Construct && Construct->getNumArgs() >= 1 &&
                  !isa<CXXDefaultArgExpr>(Construct->getArg(0)))
                Made = Construct->getArg(0);
              else if (const auto *Cast =
                           dyn_cast<CXXFunctionalCastExpr>(Made))
                Made = Cast->getSubExpr();
              else
                break;
            }
            // A member function bound to an object, `_pState->*&CState::
            // f_Read`, which changes what is of the object only when it can
            // change the object, also by a function that binds it.
            if (bindsReadOnlyMethod(Made))
              continue;
            const auto *Wrapped = dyn_cast_or_null<LambdaExpr>(Made);
            if (!Wrapped || writesAny(Wrapped->getBody(), References))
              return true;
          }
        }
    }
  if (const auto *Call = dyn_cast<CallExpr>(S);
      Call && !isa<CXXMemberCallExpr>(Call)) {
    llvm::ArrayRef<const Expr *> Arguments(Call->getArgs(), Call->getNumArgs());
    // The object of a member operator is its first argument.
    if (isa<CXXOperatorCallExpr>(Call) &&
        hasImplicitObject(Call->getDirectCallee()) && !Arguments.empty())
      Arguments = Arguments.drop_front();
    if (ChangesArguments(
            ParameterTypes(Call->getDirectCallee(), Call->getCallee()),
            Arguments, Call->getDirectCallee()))
      return true;
  }
  if (const auto *Call = dyn_cast<CXXMemberCallExpr>(S);
      Call &&
      ChangesArguments(
          ParameterTypes(Call->getDirectCallee(), Call->getCallee()),
          llvm::ArrayRef<const Expr *>(Call->getArgs(), Call->getNumArgs()),
          Call->getDirectCallee()))
    return true;
  if (const auto *Construct = dyn_cast<CXXConstructExpr>(S);
      Construct &&
      ChangesArguments(ParameterTypes(Construct->getConstructor(), nullptr),
                       llvm::ArrayRef<const Expr *>(Construct->getArgs(),
                                                    Construct->getNumArgs()),
                       Construct->getConstructor()) &&
      !onlyKeepsUnnamed(Construct))
    return true;
  // A call that depends on template parameters, in a template, can get it by
  // a reference that can change it, which its instantiations tell.
  if (const auto *Call = dyn_cast<CallExpr>(S);
      Call && !Call->getDirectCallee() && Call->isTypeDependent() &&
      llvm::any_of(Call->arguments(), [&](const Expr *Argument) {
        return touchesAny(Argument, References);
      }))
    return true;
  // A function given a copy of it, by value, that writes what the copy shares
  // with it, through a reference or pointer member, `_View.m_Bits = 0` of
  // `unsigned &m_Bits`, or `*_View.m_pBits = 0`, when its body can be looked
  // into, but not what is only of the copy, like its pointer, `_View.m_pBits
  // = nullptr`. Also a member function, `_Worker.f_Clear(View)`, and a
  // constructor, and the destructor of the copy, which runs when the function
  // returns, also when its body can't be looked into, and the functions it
  // gives the copy on to, `fg_Clear(_View)`, but not in a branch of an `if
  // constexpr` that is discarded.
  // Also through a local pointer to it, `auto fClear = &fg_Clear;`.
  const FunctionDecl *CopyCallee = nullptr;
  llvm::ArrayRef<const Expr *> CopyArguments;
  const auto *CopyCalleeCall = dyn_cast<CallExpr>(S);
  if (const FunctionDecl *Target =
          CopyCalleeCall ? calledFunction(CopyCalleeCall) : nullptr) {
    const CallExpr *Call = CopyCalleeCall;
    CopyCallee = Target;
    CopyArguments = llvm::ArrayRef(Call->getArgs(), Call->getNumArgs());
    if (isa<CXXOperatorCallExpr>(Call) && hasImplicitObject(CopyCallee) &&
        !CopyArguments.empty())
      CopyArguments = CopyArguments.drop_front();
  } else if (const auto *Construct = dyn_cast<CXXConstructExpr>(S)) {
    CopyCallee = Construct->getConstructor();
    CopyArguments =
        llvm::ArrayRef(Construct->getArgs(), Construct->getNumArgs());
  }
  if (CopyCallee) {
    const FunctionDecl *Definition = CopyCallee->getDefinition();
    for (unsigned iArgument = 0; iArgument < CopyArguments.size() &&
                                 iArgument < CopyCallee->getNumParams();
         ++iArgument) {
      const Expr *Given = CopyArguments[iArgument];
      const ParmVarDecl *Parameter =
          (Definition ? Definition : CopyCallee)->getParamDecl(iArgument);
      const QualType Type = Parameter->getType();
      if (Type->isReferenceType() || !Type->isRecordType() ||
          !touchesAny(Given, References))
        continue;
      // Destroyed, `~CClear() { m_Bits = 0; }`, where its object is the copy.
      const FunctionDecl *Destructor = nullptr;
      if (const CXXRecordDecl *Record = Type->getAsCXXRecordDecl();
          Record && Record->hasDefinition())
        if (const CXXDestructorDecl *Declared = Record->getDestructor();
            Declared && !Declared->isTrivial())
          Destructor = Declared->getDefinition();
      const bool HasBody = Definition && Definition->getBody();
      if (!HasBody && !(Destructor && Destructor->getBody()))
        continue;
      bool InDestructor = false;
      // What is checked, as of the parameter, for the functions the body
      // gives it on to.
      llvm::StringSet<> AsParameter;
      const std::string ParameterKey = ReferenceCollector::declKey(Parameter);
      const llvm::StringSet<> GivenKeys =
          referencesOf(Given, nullptr, /*Written=*/true);
      for (StringRef Reference : References.keys())
        for (StringRef Object : GivenKeys.keys()) {
          if (!Reference.starts_with(Object))
            continue;
          const StringRef Suffix = Reference.drop_front(Object.size());
          if (Suffix.empty() || Suffix.front() == '.' || Suffix.front() == '[')
            AsParameter.insert((ParameterKey + Suffix).str());
        }
      // Whether \p Base is the copy: the parameter, or the object of its
      // destructor.
      auto IsCopy = [&](const Expr *Base) {
        if (const auto *Ref = dyn_cast<DeclRefExpr>(Base))
          return Ref->getDecl() == Parameter;
        return InDestructor && isa<CXXThisExpr>(Base);
      };
      bool Writes = false;
      std::function<void(const Stmt *)> Visit = [&](const Stmt *Inner) {
        if (!Inner || Writes ||
            isa<LambdaExpr, UnaryExprOrTypeTraitExpr, CXXNoexceptExpr>(Inner))
          return;
        // Also what a lambda a local is made with does where the local's
        // destructor calls it, `auto Cleanup = g_OnScopeExit / [&] {
        // _View.m_Bits = 0; };`, when its scope ends.
        if (const auto *Declaration = dyn_cast<DeclStmt>(Inner))
          for (const Decl *D : Declaration->decls())
            if (const auto *Local = dyn_cast<VarDecl>(D);
                Local && Local->hasLocalStorage() && Local->getInit() &&
                destructorWritesThroughMember(Local->getType()
                                                  .getNonReferenceType()
                                                  ->getBaseElementTypeUnsafe()
                                                  ->getAsCXXRecordDecl())) {
              std::function<void(const Stmt *)> Bodies = [&](const Stmt *Part) {
                if (!Part)
                  return;
                if (const auto *Held = dyn_cast<LambdaExpr>(Part)) {
                  Visit(Held->getBody());
                  return;
                }
                for (const Stmt *Child : childrenOnce(Part))
                  Bodies(Child);
              };
              Bodies(Local->getInit());
            }
        if (const auto *If = dyn_cast<IfStmt>(Inner);
            If && If->isConstexpr() && !If->getCond()->isValueDependent())
          if (const std::optional<const Stmt *> Case =
                  If->getNondiscardedCase(Parameter->getASTContext())) {
            Visit(If->getInit());
            Visit(If->getCond());
            Visit(*Case);
            return;
          }
        // Given on to another function, `fg_Clear(_View)`, that writes it
        // there.
        if (!InDestructor && !AsParameter.empty() &&
            isa<CallExpr, CXXConstructExpr>(Inner) &&
            llvm::any_of(childrenOnce(Inner),
                         [&](const Stmt *Child) {
                           return touchesAny(Child, AsParameter);
                         }) &&
            writesAny(Inner, AsParameter)) {
          Writes = true;
          return;
        }
        const Expr *Target = nullptr;
        if (const auto *Assign = dyn_cast<BinaryOperator>(Inner);
            Assign && Assign->isAssignmentOp())
          Target = Assign->getLHS();
        else if (const auto *Step = dyn_cast<UnaryOperator>(Inner);
                 Step && Step->isIncrementDecrementOp())
          Target = Step->getSubExpr();
        else if (const auto *Member = dyn_cast<CXXMemberCallExpr>(Inner);
                 Member && Member->getMethodDecl() &&
                 !Member->getMethodDecl()->isConst())
          Target = Member->getImplicitObjectArgument();
        // A member function of it, also a const one, that writes what the
        // copy shares with it, `_View.f_Clear()` of `void f_Clear() const {
        // m_Bits = 0; }`.
        if (const auto *Member = dyn_cast<CXXMemberCallExpr>(Inner);
            Member && Member->getMethodDecl()) {
          const Expr *Object = Member->getImplicitObjectArgument();
          for (int Depth = 0; Object && Depth < 8; ++Depth) {
            Object = Object->IgnoreParenImpCasts();
            const auto *Outer = dyn_cast<MemberExpr>(Object);
            if (!Outer ||
                (Outer->isArrow() &&
                 !IsCopy(Outer->getBase()->IgnoreParenImpCasts())))
              break;
            Object = Outer->getBase();
          }
          if (Object && IsCopy(Object->IgnoreParenImpCasts()) &&
              methodChangesObject(Member->getMethodDecl(),
                                  /*OnlyShared=*/true)) {
            const llvm::StringSet<> Arguments = referencesOf(
                Given, nullptr, /*Written=*/true);
            for (StringRef Argument : Arguments.keys())
              if (llvm::any_of(References.keys(), [&](StringRef Reference) {
                    return isRelated(Argument, Reference);
                  }))
                Writes = true;
          }
        }
        // Also through a local reference to it, `unsigned &Bits =
        // _View.m_Bits; Bits = 0;`.
        for (int Depth = 0; Target && Depth < 8; ++Depth) {
          const auto *Ref = dyn_cast<DeclRefExpr>(Target->IgnoreParenImpCasts());
          const auto *Alias = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
          if (!Alias || isa<ParmVarDecl>(Alias) || !Alias->hasLocalStorage() ||
              !Alias->getType()->isReferenceType() || !Alias->getInit())
            break;
          Target = Alias->getInit();
        }
        // A member of the parameter, `_Info.m_Inner.m_Bits`, that is mutable,
        // a reference, or a pointer written through, `*_View.m_pBits` or
        // `_View.m_pBits[0]`.
        bool ThroughPointer = false;
        std::string Element = "[0]";
        if (const auto *Deref = Target ? dyn_cast<UnaryOperator>(
                                             Target->IgnoreParenImpCasts())
                                       : nullptr;
            Deref && Deref->getOpcode() == UO_Deref) {
          Target = Deref->getSubExpr();
          ThroughPointer = true;
        } else if (const auto *Subscript =
                       Target ? dyn_cast<ArraySubscriptExpr>(
                                    Target->IgnoreParenImpCasts())
                              : nullptr;
                   Subscript && Subscript->getBase()->getType()->isPointerType()) {
          Target = Subscript->getBase();
          ThroughPointer = true;
          Expr::EvalResult Index;
          if (!Subscript->getIdx()->isValueDependent() &&
              Subscript->getIdx()->EvaluateAsInt(Index,
                                                 Parameter->getASTContext()))
            Element = "[" + std::to_string(Index.Val.getInt().getExtValue()) +
                      "]";
          else
            Element.clear();
        }
        const auto *Written =
            Target ? dyn_cast<MemberExpr>(Target->IgnoreParenImpCasts())
                   : nullptr;
        if (const auto *Field = Written ? dyn_cast<FieldDecl>(
                                              Written->getMemberDecl())
                                        : nullptr;
            Field &&
            ((Field->getType()->isReferenceType() && !ThroughPointer) ||
             (Field->getType()->isPointerType() && ThroughPointer))) {
          // The members from the parameter to what is written.
          llvm::SmallVector<const ValueDecl *, 4> Path{Field};
          for (const Expr *Base = Written->getBase()->IgnoreParenImpCasts();;) {
            if (isa<DeclRefExpr, CXXThisExpr>(Base)) {
              if (!IsCopy(Base))
                break;
              // What it writes of what the argument is: only the data that
              // is checked, not another member, `++_Info.m_nQueries` of a
              // check of `_Info.m_Bits`.
              std::string Suffix;
              for (const ValueDecl *Member : llvm::reverse(Path)) {
                Suffix += ".";
                if (const auto *Record =
                        dyn_cast<RecordDecl>(Member->getDeclContext());
                    Record && Record->isUnion())
                  Suffix += "~.";
                Suffix += ReferenceCollector::declKey(Member);
              }
              if (ThroughPointer)
                Suffix += Element;
              const llvm::StringSet<> Arguments = referencesOf(
                  Given, nullptr, /*Written=*/true);
              for (StringRef Argument : Arguments.keys())
                if (llvm::any_of(References.keys(), [&](StringRef Reference) {
                      return isRelated((Argument + Suffix).str(), Reference);
                    }))
                  Writes = true;
              break;
            }
            const auto *Outer = dyn_cast<MemberExpr>(Base);
            if (!Outer ||
                (Outer->isArrow() &&
                 !IsCopy(Outer->getBase()->IgnoreParenImpCasts())))
              break;
            Path.push_back(Outer->getMemberDecl());
            Base = Outer->getBase()->IgnoreParenImpCasts();
          }
        }
        for (const Stmt *Child : childrenOnce(Inner))
          Visit(Child);
      };
      if (HasBody && enterCallee(Definition->getCanonicalDecl())) {
        Visit(Definition->getBody());
        s_InCallee.erase(Definition->getCanonicalDecl());
      }
      if (!Writes && Destructor && Destructor->getBody()) {
        InDestructor = true;
        Visit(Destructor->getBody());
      }
      if (Writes)
        return true;
    }
  }
  // A function given it by a reference or a pointer to const that writes it
  // there, when its body can be looked into: through a mutable member,
  // `_Info.m_Bits = 0` with `mutable unsigned m_Bits`, or a reference or
  // pointer member, also through the functions it gives it on to,
  // `fg_Clear(_Info)`. What is checked is looked for in the body as of the
  // parameter. Also a member function given it, `Worker.f_Clear(_Info)`, and
  // a constructor, also in its member initializers, and one called through a
  // local pointer to it, `auto fClear = &fg_Clear; fClear(_Info);`.
  const FunctionDecl *Callee = nullptr;
  llvm::ArrayRef<const Expr *> CallArguments;
  const auto *CalleeCall = dyn_cast<CallExpr>(S);
  if (const FunctionDecl *Target =
          CalleeCall ? calledFunction(CalleeCall) : nullptr) {
    const CallExpr *Call = CalleeCall;
    Callee = Target;
    CallArguments = llvm::ArrayRef(Call->getArgs(), Call->getNumArgs());
  } else if (const auto *Construct = dyn_cast<CXXConstructExpr>(S)) {
    Callee = Construct->getConstructor();
    CallArguments =
        llvm::ArrayRef(Construct->getArgs(), Construct->getNumArgs());
  }
  // A virtual one that isn't final runs the override of what it is called
  // on, which can write what is mutable of what it is given, whatever the
  // body that is named does.
  if (const auto *Method = dyn_cast_or_null<CXXMethodDecl>(Callee);
      Method && Method->isVirtual() && !Method->hasAttr<FinalAttr>() &&
      !Method->getParent()->hasAttr<FinalAttr>()) {
    const auto *Member = dyn_cast<CXXMemberCallExpr>(S);
    const auto *Named =
        Member ? dyn_cast<MemberExpr>(Member->getCallee()->IgnoreParens())
               : nullptr;
    if (!(Named && Named->hasQualifier()))
      for (const Expr *Argument : CallArguments) {
        QualType Type = Argument->getType();
        if (Type->isPointerType())
          Type = Type->getPointeeType();
        const CXXRecordDecl *Record = Type->getAsCXXRecordDecl();
        if (Record && Record->hasDefinition() && Record->hasMutableFields() &&
            touchesAny(Argument, References))
          return true;
      }
  }
  if (Callee) {
    const FunctionDecl *Definition = Callee->getDefinition();
    const unsigned Offset =
        isa<CXXOperatorCallExpr>(S) && hasImplicitObject(Callee) ? 1 : 0;
    llvm::StringSet<> AsParameters;
    for (unsigned iArgument = Offset;
         Definition && Definition->getBody() &&
         iArgument < CallArguments.size() &&
         iArgument - Offset < Definition->getNumParams();
         ++iArgument) {
      const ParmVarDecl *Parameter =
          Definition->getParamDecl(iArgument - Offset);
      const QualType Type = Parameter->getType();
      // Also a scalar, whose const can be cast away, `const_cast<unsigned
      // &>(_Bits) = 0`.
      const bool ByConstReference =
          Type->isReferenceType() &&
          Type.getNonReferenceType().isConstQualified();
      const bool ByConstPointer = Type->isPointerType() &&
                                  Type->getPointeeType().isConstQualified();
      if (!ByConstReference && !ByConstPointer)
        continue;
      // What the argument is, or for a pointer, what it points to.
      const Expr *Argument = CallArguments[iArgument]->IgnoreParenImpCasts();
      llvm::SmallVector<std::string, 2> Objects;
      std::string ParameterKey = ReferenceCollector::declKey(Parameter);
      if (ByConstPointer) {
        ParameterKey += "[0]";
        if (const auto *Address = dyn_cast<UnaryOperator>(Argument);
            Address && Address->getOpcode() == UO_AddrOf) {
          const llvm::StringSet<> Keys =
              referencesOf(Address->getSubExpr(), nullptr, /*Written=*/true);
          for (StringRef Key : Keys.keys())
            Objects.push_back(Key.str());
        } else {
          const llvm::StringSet<> Keys =
              referencesOf(Argument, nullptr, /*Written=*/true);
          for (StringRef Key : Keys.keys())
            Objects.push_back((Key + "[0]").str());
        }
      } else {
        const llvm::StringSet<> Keys =
            referencesOf(Argument, nullptr, /*Written=*/true);
        for (StringRef Key : Keys.keys())
          Objects.push_back(Key.str());
      }
      for (StringRef Reference : References.keys())
        for (const std::string &Object : Objects) {
          if (!Reference.starts_with(Object))
            continue;
          const StringRef Suffix = Reference.drop_front(Object.size());
          if (!Suffix.empty() && Suffix.front() != '.' && Suffix.front() != '[')
            continue;
          AsParameters.insert((ParameterKey + Suffix).str());
        }
    }
    // A constructor writes them also through a member it binds to them,
    // `m_Bits(_Other.m_Bits)` of a `unsigned &m_Bits`, or points at them,
    // `m_pBits(&_Other.m_Bits)`, as what is of its own object.
    if (const auto *Constructor =
            dyn_cast_or_null<CXXConstructorDecl>(Definition);
        Constructor && !AsParameters.empty()) {
      llvm::StringSet<> Through;
      for (const CXXCtorInitializer *Initializer : Constructor->inits()) {
        const FieldDecl *Field = Initializer->getMember();
        if (!Field || !Initializer->getInit() ||
            !(Field->getType()->isReferenceType() ||
              Field->getType()->isPointerType()))
          continue;
        const Expr *Init = Initializer->getInit()->IgnoreParenImpCasts();
        std::string Member = "this." + ReferenceCollector::declKey(Field);
        if (const auto *Address = dyn_cast<UnaryOperator>(Init);
            Address && Address->getOpcode() == UO_AddrOf &&
            Field->getType()->isPointerType()) {
          Init = Address->getSubExpr();
          Member += "[0]";
        }
        const llvm::StringSet<> Keys =
            referencesOf(Init, nullptr, /*Written=*/true);
        for (StringRef Key : Keys.keys())
          for (StringRef Parameter : AsParameters.keys()) {
            if (!Parameter.starts_with(Key))
              continue;
            const StringRef Suffix = Parameter.drop_front(Key.size());
            if (Suffix.empty() || Suffix.front() == '.' ||
                Suffix.front() == '[')
              Through.insert((Member + Suffix).str());
          }
      }
      for (StringRef Key : Through.keys())
        AsParameters.insert(Key);
    }
    if (!AsParameters.empty() && enterCallee(Definition)) {
      // Also through a local copy of a smart pointer it is given, `auto pCopy
      // = _pInfo; pCopy->m_Bits = 0;`, which points to what it points to.
      {
        llvm::StringSet<> Copies;
        std::function<void(const Stmt *)> FindCopies = [&](const Stmt *Inner) {
          if (!Inner || isa<LambdaExpr>(Inner))
            return;
          if (const auto *Declaration = dyn_cast<DeclStmt>(Inner))
            for (const Decl *D : Declaration->decls())
              if (const auto *Local = dyn_cast<VarDecl>(D);
                  Local && Local->hasLocalStorage() && Local->getInit() &&
                  !Local->getType()->isReferenceType() &&
                  sharesPointee(Local->getType()->getAsCXXRecordDecl()))
                if (const auto *Copied = dyn_cast<DeclRefExpr>([&] {
                      const Expr *Init = Local->getInit()
                                             ->IgnoreImplicit()
                                             ->IgnoreParenImpCasts();
                      if (const auto *Copy = dyn_cast<CXXConstructExpr>(Init);
                          Copy && Copy->getNumArgs() == 1 &&
                          Copy->getConstructor()->isCopyConstructor())
                        Init = Copy->getArg(0)->IgnoreParenImpCasts();
                      return Init;
                    }());
                    Copied && isa<ParmVarDecl>(Copied->getDecl())) {
                  const std::string From =
                      ReferenceCollector::declKey(Copied->getDecl()) + "[0]";
                  for (StringRef Key : AsParameters.keys())
                    if (Key.starts_with(From))
                      Copies.insert(ReferenceCollector::declKey(Local) + "[0]" +
                                    Key.drop_front(From.size()).str());
                }
          for (const Stmt *Child : childrenOnce(Inner))
            FindCopies(Child);
        };
        FindCopies(Definition->getBody());
        for (StringRef Key : Copies.keys())
          AsParameters.insert(Key);
      }
      bool Writes = writesAny(Definition->getBody(), AsParameters);
      if (const auto *Constructor = dyn_cast<CXXConstructorDecl>(Definition))
        for (const CXXCtorInitializer *Initializer : Constructor->inits())
          Writes = Writes || writesAny(Initializer->getInit(), AsParameters);
      s_InCallee.erase(Definition);
      if (Writes)
        return true;
    }
  }
  // An aggregate that refers to it, or points to it, `CClear Clear{_Value}`
  // or `CClear Clear(_Value)`, can change it, also when it is destroyed.
  llvm::ArrayRef<Expr *> AggregateInits;
  if (const auto *List = dyn_cast<InitListExpr>(S))
    AggregateInits = llvm::ArrayRef(List->getInits(), List->getNumInits());
  else if (const auto *List = dyn_cast<CXXParenListInitExpr>(S))
    AggregateInits = List->getInitExprs();
  // Not a local that is only read, `(void) Borrowed` or `Borrowed.m_Bits`
  // read as a value, which can't change it, also one with a destructor,
  // `CClear Clear{_Value};`, which changes it only at the end of its block,
  // which is told there.
  auto OnlyRead = [&](const Expr *Made) {
    if (!s_CheckContext)
      return false;
    ASTContext &Context = const_cast<ASTContext &>(*s_CheckContext);
    // Also a temporary a local reference extends, `const CView &View =
    // CView{_Bits}`, and an element of an array or a member of an aggregate
    // made with a list, `CView Views[]{{_Bits}}`.
    DynTypedNode Up = DynTypedNode::create(*Made);
    const VarDecl *Local = nullptr;
    for (int Depth = 0; Depth < 16 && !Local; ++Depth) {
      const auto Parents = instantiatedParents(Up, Context);
      if (Parents.empty())
        break;
      Local = Parents[0].get<VarDecl>();
      if (!Local && !(Parents[0].get<MaterializeTemporaryExpr>() ||
                      Parents[0].get<ExprWithCleanups>() ||
                      Parents[0].get<CXXBindTemporaryExpr>() ||
                      Parents[0].get<ImplicitCastExpr>() ||
                      Parents[0].get<CXXFunctionalCastExpr>() ||
                      Parents[0].get<InitListExpr>()))
        break;
      Up = Parents[0];
    }
    const CXXRecordDecl *Class =
        Local ? Context
                    .getBaseElementType(Local->getType().getNonReferenceType())
                    ->getAsCXXRecordDecl()
              : nullptr;
    const auto *Function =
        Local ? dyn_cast_or_null<FunctionDecl>(
                    Local->getParentFunctionOrMethod())
              : nullptr;
    if (!Local || !Local->hasLocalStorage() || !Class || !Function ||
        !Function->getBody())
      return false;
    // Also given to a function by a reference to const, `fg_Read(View)`,
    // whose body only reads its parameter the same way.
    std::function<bool(const VarDecl *, const FunctionDecl *, int)> ReadIn =
        [&](const VarDecl *Variable, const FunctionDecl *In, int Depth) {
          bool Read = true;
          std::function<void(const Stmt *)> Visit = [&](const Stmt *Inner) {
            if (!Inner || !Read)
              return;
            if (const auto *Ref = dyn_cast<DeclRefExpr>(Inner);
                Ref && Ref->getDecl() == Variable) {
              const auto Up = instantiatedParents(*Ref, Context);
              const auto *Parent = Up.empty() ? nullptr : Up[0].get<Expr>();
              const auto *Member = dyn_cast_or_null<MemberExpr>(Parent);
              const ImplicitCastExpr *Load = nullptr;
              if (Member)
                if (const auto AboveMember =
                        instantiatedParents(*Member, Context);
                    !AboveMember.empty())
                  Load = AboveMember[0].get<ImplicitCastExpr>();
              const auto *Discard = dyn_cast_or_null<CStyleCastExpr>(Parent);
              // Past the cast that makes it const, `CView const &`.
              const Expr *Argument = Parent;
              for (int Level = 0; Level < 4 && Argument &&
                                  isa<ImplicitCastExpr, ParenExpr>(Argument);
                   ++Level) {
                const auto Above = instantiatedParents(*Argument, Context);
                Argument = Above.empty() ? nullptr : Above[0].get<Expr>();
              }
              const auto *Call = dyn_cast_or_null<CallExpr>(Argument);
              const FunctionDecl *Callee =
                  Call && Call->getDirectCallee()
                      ? Call->getDirectCallee()->getDefinition()
                      : nullptr;
              bool Given = false;
              if (Callee && Callee->getBody() && Depth < 4 &&
                  !isa<CXXOperatorCallExpr>(Call))
                for (unsigned iArgument = 0; iArgument < Call->getNumArgs() &&
                                             iArgument < Callee->getNumParams();
                     ++iArgument) {
                  if (Call->getArg(iArgument)->IgnoreParenImpCasts() != Ref)
                    continue;
                  const ParmVarDecl *Parameter =
                      Callee->getParamDecl(iArgument);
                  const QualType Type = Parameter->getType();
                  Given = Type->isLValueReferenceType() &&
                          Type.getNonReferenceType().isConstQualified() &&
                          ReadIn(Parameter, Callee, Depth + 1);
                  if (!Given)
                    break;
                }
              // Also a pointer member of it pointed elsewhere, `Current.m_pWord
              // = &Other`, which writes nothing it pointed to: what is written
              // through it is told by what it points to where that is.
              const auto *Assign =
                  Member && Member->getType()->isPointerType() &&
                          !Member->isArrow()
                      ? dyn_cast_or_null<BinaryOperator>(
                            instantiatedParents(*Member, Context).empty()
                                ? nullptr
                                : instantiatedParents(*Member, Context)[0]
                                      .get<BinaryOperator>())
                      : nullptr;
              const bool Repointed = Assign &&
                                     Assign->getOpcode() == BO_Assign &&
                                     Assign->getLHS()->IgnoreParens() == Member;
              if (!(Load && Load->getCastKind() == CK_LValueToRValue) &&
                  !(Discard && Discard->getType()->isVoidType()) && !Given &&
                  !Repointed)
                Read = false;
            }
            for (const Stmt *Child : childrenOnce(Inner))
              Visit(Child);
          };
          Visit(In->getBody());
          return Read;
        };
    return ReadIn(Local, Function, 0);
  };
  // Nor one that is made and discarded, `(void)CClear{_Bits}`, which only
  // binds its references, and whose destructor is told where it runs.
  auto Discarded = [&](const Expr *Made) {
    if (!s_CheckContext)
      return false;
    ASTContext &Context = const_cast<ASTContext &>(*s_CheckContext);
    DynTypedNode Node = DynTypedNode::create(*Made);
    for (int Depth = 0; Depth < 8; ++Depth) {
      const auto Parents = instantiatedParents(Node, Context);
      if (Parents.empty())
        return false;
      const DynTypedNode &Parent = Parents[0];
      if (Parent.get<MaterializeTemporaryExpr>() ||
          Parent.get<CXXBindTemporaryExpr>() ||
          Parent.get<CXXFunctionalCastExpr>() || Parent.get<ParenExpr>() ||
          Parent.get<ExprWithCleanups>() ||
          (Parent.get<ImplicitCastExpr>() &&
           Parent.get<ImplicitCastExpr>()->getCastKind() == CK_NoOp)) {
        Node = Parent;
        continue;
      }
      if (const auto *Cast = Parent.get<CStyleCastExpr>())
        return Cast->getType()->isVoidType();
      if (const auto *Comma = Parent.get<BinaryOperator>())
        return Comma->getOpcode() == BO_Comma &&
               Comma->getLHS()->IgnoreParens() == Node.get<Expr>();
      return Parent.get<CompoundStmt>() != nullptr;
    }
    return false;
  };
  if (const auto *Record = !AggregateInits.empty() &&
                                   !OnlyRead(cast<Expr>(S)) &&
                                   !Discarded(cast<Expr>(S))
                               ? cast<Expr>(S)->getType()->getAsRecordDecl()
                               : nullptr) {
    unsigned iInit = 0;
    if (const auto *Class = dyn_cast<CXXRecordDecl>(Record))
      iInit = Class->getNumBases();
    for (const FieldDecl *Field : Record->fields()) {
      // An unnamed bit-field isn't initialized, `int : 0`.
      if (Field->isUnnamedBitField())
        continue;
      if (iInit >= AggregateInits.size())
        break;
      const QualType Type = Field->getType();
      const Expr *Init = AggregateInits[iInit++];
      if ((Type->isReferenceType() || Type->isPointerType()) &&
          !Type->getPointeeType().isConstQualified() &&
          touchesAny(Init, References))
        return true;
    }
  }
    // A loop over it by reference, `for (auto &Value : _Values)`, changes it
    // through the loop variable, and so does one over pointers to it, through
    // the copies, `for (auto pValue : Pointers) *pValue = 0;`.
    auto ChangesThrough = [](QualType Type) {
      return (Type->isReferenceType() &&
              !Type.getNonReferenceType().isConstQualified()) ||
             (Type->isPointerType() &&
              !Type->getPointeeType().isConstQualified());
    };
    if (const auto *Loop = dyn_cast<CXXForRangeStmt>(S);
        Loop && Loop->getLoopVariable() &&
        ChangesThrough(Loop->getLoopVariable()->getType()) &&
        touchesAny(Loop->getRangeInit(), References)) {
      llvm::StringSet<> Element;
      Element.insert(ReferenceCollector::declKey(Loop->getLoopVariable()));
      if (writesAny(Loop->getBody(), Element))
        return true;
    }
  // A lambda called where it is made, `[&] { Value = 0; }()`, runs its body,
  // and so does one held in a local, `fClear()`.
  if (const auto *Invoke = dyn_cast<CXXOperatorCallExpr>(S);
      Invoke && Invoke->getOperator() == OO_Call && Invoke->getNumArgs() >= 1) {
    // Also a copy of one, `auto fCopy = fClear`, which captures the same, or
    // through a pointer to one, `(*pClear)()`.
    if (const LambdaExpr *Lambda = calledLambda(Invoke->getArg(0));
        Lambda && writesAny(Lambda->getBody(), notCopiedBy(Lambda, References)))
      return true;
    // Also the call operator of a lambda that a class takes as its own,
    // `fWork(1)` of `fg_MergeFunctors([&](int) { _Bits = 0; }, ...)`, which
    // runs its body, but not on what it has its own copies of.
    if (const auto *Method =
            dyn_cast_or_null<CXXMethodDecl>(Invoke->getDirectCallee());
        Method && Method->getParent()->isLambda() &&
        !calledLambda(Invoke->getArg(0))) {
      const FunctionDecl *Definition = Method->getDefinition();
      llvm::StringSet<> Shared;
      for (StringRef Reference : References.keys())
        if (llvm::none_of(Method->getParent()->captures(),
                          [&](const LambdaCapture &Capture) {
                            if (!Capture.capturesVariable() ||
                                Capture.getCaptureKind() != LCK_ByCopy)
                              return false;
                            const std::string Copied =
                                ReferenceCollector::declKey(
                                    Capture.getCapturedVar());
                            return Reference == Copied ||
                                   Reference.starts_with(Copied + ".") ||
                                   Reference.starts_with(Copied + "[");
                          }))
          Shared.insert(Reference);
      if (Definition && Definition->getBody() && !Shared.empty() &&
          writesAny(Definition->getBody(), Shared))
        return true;
    }
    // Also one a function made and gave back, `auto fClear =
    // fg_MakeClear(_Bits);` of `return [&_Bits] { _Bits = 0; };`, given what
    // it can capture, when its body writes what it captures.
    const auto *Ref =
        dyn_cast<DeclRefExpr>(Invoke->getArg(0)->IgnoreParenImpCasts());
    const auto *Local = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
    const CXXRecordDecl *Closure =
        Local ? Local->getType().getNonReferenceType()->getAsCXXRecordDecl()
              : nullptr;
    if (Closure && Closure->isLambda() && !calledLambda(Invoke->getArg(0)) &&
        Local->hasLocalStorage()) {
      llvm::SmallVector<const Expr *, 2> Values =
          assignedValuesAt(Local, Ref);
      if (Values.empty() && Local->getInit())
        Values.push_back(Local->getInit());
      const CXXMethodDecl *Operator = Closure->getLambdaCallOperator();
      llvm::StringSet<> Captured;
      for (const LambdaCapture &Capture : Closure->captures())
        if (Capture.capturesVariable())
          Captured.insert(
              ReferenceCollector::declKey(Capture.getCapturedVar()));
      if (Operator && Operator->getBody() && !Captured.empty() &&
          llvm::any_of(Values,
                       [&](const Expr *Value) {
                         return givesAny(Value, References);
                       }) &&
          writesAny(Operator->getBody(), Captured))
        return true;
    }
  }
  // Also its call operator called by name, `fClear.operator()()`.
  if (const auto *Invoke = dyn_cast<CXXMemberCallExpr>(S);
      Invoke && Invoke->getMethodDecl() &&
      Invoke->getMethodDecl()->getOverloadedOperator() == OO_Call)
    if (const LambdaExpr *Lambda =
            calledLambda(Invoke->getImplicitObjectArgument());
        Lambda && writesAny(Lambda->getBody(), notCopiedBy(Lambda, References)))
      return true;
  return llvm::any_of(childrenOnce(S), [&](const Stmt *Child) {
    return writesAny(Child, References);
  });
}

/// Whether \p S changes one of \p References after reading it, like
/// `Value--`, so what it gives is the value before.
static bool postWritesAny(const Stmt *S, const llvm::StringSet<> &References) {
  // Not what isn't evaluated, `noexcept(_Value++)`.
  if (!S || isa<LambdaExpr>(S) || isUnevaluatedNode(S))
    return false;
  if (const auto *Unary = dyn_cast<UnaryOperator>(S);
      Unary && Unary->isPostfix() && Unary->isIncrementDecrementOp() &&
      touchesAny(Unary->getSubExpr(), References))
    return true;
  // Overloaded, `Value--` has a second argument.
  if (const auto *Call = dyn_cast<CXXOperatorCallExpr>(S);
      Call &&
      (Call->getOperator() == OO_PlusPlus ||
       Call->getOperator() == OO_MinusMinus) &&
      Call->getNumArgs() == 2 && touchesAny(Call->getArg(0), References))
    return true;
  return llvm::any_of(childrenOnce(S), [&](const Stmt *Child) {
    return postWritesAny(Child, References);
  });
}

/// Whether \p Child is \p Of, past parentheses and implicit nodes, which the
/// parent map skips.
static bool isSame(const Stmt *Child, const Stmt *Of) {
  if (!Child || !Of)
    return false;
  if (Child == Of)
    return true;
  const auto *ChildExpr = dyn_cast<Expr>(Child);
  const auto *OfExpr = dyn_cast<Expr>(Of);
  return ChildExpr && OfExpr &&
         ChildExpr->IgnoreParenImpCasts() == OfExpr->IgnoreParenImpCasts();
}

/// Whether \p S ends by leaving the code that follows it, so it doesn't reach
/// \p Call: return, throw, break, continue, a goto to a label after the
/// call, a call of a function that doesn't return, `[[noreturn]] void
/// fg_Fail()`, or an `if` whose branches all do.
static bool leaves(const Stmt *S, const CallExpr *Call,
                   const SourceManager &SM) {
  // Also with attributes, `[[likely]] return;`, and a throw of what has to
  // be destroyed, `throw CError{};`.
  if (const auto *Attributed = dyn_cast_or_null<AttributedStmt>(S))
    return leaves(Attributed->getSubStmt(), Call, SM);
  if (const auto *Cleanups = dyn_cast_or_null<ExprWithCleanups>(S))
    return leaves(Cleanups->getSubExpr(), Call, SM);
  // Also in parentheses or cast to void, `(fg_Fail());` or `(void)fg_Fail();`,
  // and after a comma, `(fg_Log(), fg_Fail());`.
  if (const auto *Paren = dyn_cast_or_null<ParenExpr>(S))
    return leaves(Paren->getSubExpr(), Call, SM);
  if (const auto *Cast = dyn_cast_or_null<CastExpr>(S);
      Cast && Cast->getType()->isVoidType())
    return leaves(Cast->getSubExpr(), Call, SM);
  if (const auto *Comma = dyn_cast_or_null<BinaryOperator>(S);
      Comma && Comma->getOpcode() == BO_Comma)
    return leaves(Comma->getRHS(), Call, SM);
  if (const auto *Compound = dyn_cast_or_null<CompoundStmt>(S))
    return !Compound->body_empty() && leaves(Compound->body_back(), Call, SM);
  // An `if` whose branches both leave, `if (bThrow) throw 1; else return
  // -1;`.
  if (const auto *If = dyn_cast_or_null<IfStmt>(S))
    return If->getElse() && leaves(If->getThen(), Call, SM) &&
           leaves(If->getElse(), Call, SM);
  if (const auto *Goto = dyn_cast_or_null<GotoStmt>(S)) {
    const LabelStmt *Label = Goto->getLabel()->getStmt();
    return Label && SM.isBeforeInTranslationUnit(
                        SM.getExpansionLoc(Call->getEndLoc()),
                        SM.getExpansionLoc(Label->getBeginLoc()));
  }
  if (const auto *Called = dyn_cast_or_null<CallExpr>(S))
    if (const auto *Callee =
            dyn_cast_or_null<FunctionDecl>(Called->getCalleeDecl()))
      return Callee->isNoReturn();
  return isa_and_nonnull<ReturnStmt, CXXThrowExpr, BreakStmt, ContinueStmt,
                         CoreturnStmt>(S);
}

/// Collects the outermost expressions of \p S that are spelled between
/// \p Begin and \p End: the arguments of a macro invoked there.
static void
spelledBetween(const Stmt *S, SourceLocation Begin, SourceLocation End,
               const SourceManager &SM,
               llvm::SmallVectorImpl<const Expr *> &Expressions) {
  if (!S)
    return;
  auto Within = [&](SourceLocation Loc) {
    Loc = SM.getSpellingLoc(Loc);
    return Loc.isValid() && !SM.isBeforeInTranslationUnit(Loc, Begin) &&
           !SM.isBeforeInTranslationUnit(End, Loc);
  };
  if (const auto *E = dyn_cast<Expr>(S);
      E && E->getBeginLoc().isMacroID() && Within(E->getBeginLoc()) &&
      Within(E->getEndLoc())) {
    Expressions.push_back(E);
    return;
  }
  for (const Stmt *Child : childrenOnce(S))
    spelledBetween(Child, Begin, End, SM, Expressions);
}

/// The condition of a fact for the analysis, `DMibAnalyzerAssume(Value >= 16)`,
/// which expands to `static_cast<void>(sizeof(!!(Value >= 16)))`.
static const Expr *analyzerAssumption(const Stmt *Statement) {
  const auto *Cast = dyn_cast<ExplicitCastExpr>(Statement);
  if (!Cast || !Cast->getType()->isVoidType())
    return nullptr;
  const auto *Sizeof = dyn_cast<UnaryExprOrTypeTraitExpr>(
      Cast->getSubExpr()->IgnoreParenImpCasts());
  if (!Sizeof || Sizeof->getKind() != UETT_SizeOf || Sizeof->isArgumentType())
    return nullptr;
  const Expr *Condition = Sizeof->getArgumentExpr()->IgnoreParenImpCasts();
  for (int iNot = 0; iNot < 2; ++iNot) {
    const auto *Not = dyn_cast<UnaryOperator>(Condition);
    if (!Not || Not->getOpcode() != UO_LNot)
      return nullptr;
    Condition = Not->getSubExpr()->IgnoreParenImpCasts();
  }
  return Condition;
}

/// Whether \p Zero tells that the value isn't zero, also when it can't be told
/// with \p UnknownChecks.
static bool tells(EZero Zero, bool UnknownChecks) {
  return Zero == EZero::Excluded || (UnknownChecks && Zero == EZero::Unknown);
}

/// Whether the assertion \p Statement, expanded from the macro that
/// \p MacroLoc is in, asserts that what \p Keys tests is not zero: its
/// condition, an argument of the macro, like `DMibCheck(Value != 0)`, not
/// `DMibCheck(Value == 0)`. An assertion whose condition can't be found, like
/// one through a macro that wraps it, is taken as a check.
static bool assertsNonZero(const Stmt *Statement, SourceLocation MacroLoc,
                           const CArgumentKeys &Keys, const ASTContext &Context,
                           bool UnknownChecks) {
  // Not one that changes the value as it checks it, like `DMibCheck(_Value
  // != 0 && (--_Value, true))`.
  if (writesAny(Statement, Keys.Read))
    return false;
  const SourceManager &SM = Context.getSourceManager();
  const CharSourceRange Invocation = SM.getImmediateExpansionRange(MacroLoc);
  // Only the condition, the first argument, like `_Value != 0` of
  // `DMibSafeCheck(_Value != 0, fg_Message(_Value))`, not the message.
  SourceLocation Begin = SM.getSpellingLoc(Invocation.getBegin());
  SourceLocation End = SM.getSpellingLoc(Invocation.getEnd());
  {
    const LangOptions &LangOpts = Context.getLangOpts();
    std::optional<Token> Next =
        Lexer::findNextToken(Begin, SM, LangOpts); // `(`
    if (Next && Next->is(tok::l_paren)) {
      int Depth = 0;
      for (std::optional<Token> Current = Lexer::findNextToken(
               Next->getLocation(), SM, LangOpts);
           Current && !SM.isBeforeInTranslationUnit(End, Current->getLocation());
           Current = Lexer::findNextToken(Current->getLocation(), SM, LangOpts)) {
        if (Current->isOneOf(tok::l_paren, tok::l_square, tok::l_brace))
          ++Depth;
        else if (Current->isOneOf(tok::r_paren, tok::r_square, tok::r_brace) &&
                 Depth > 0)
          --Depth;
        else if (Depth == 0 && Current->isOneOf(tok::comma, tok::r_paren)) {
          End = Current->getLocation();
          break;
        }
      }
    }
  }
  llvm::SmallVector<const Expr *> Conditions;
  spelledBetween(Statement, Begin, End, SM, Conditions);
  if (Conditions.empty())
    return true;
  return llvm::any_of(Conditions, [&](const Expr *Condition) {
    return (readsAny(Condition, Keys.Tested, &Context) ||
            readsAny(Condition, Keys.ComplementOf)) &&
           tells(zeroWhen(Condition, true, Keys, Context), UnknownChecks);
  });
}

/// Whether the argument of \p Call is checked for zero first.
/// What the const local \p E names is a copy of, when it is of a variable
/// that nothing in the function writes, and as wide as it, `const unsigned
/// Bits = _Bits;`; null otherwise.
static const Expr *constCopySource(const Expr *E, const ASTContext &Context) {
  const auto *Ref = dyn_cast<DeclRefExpr>(E->IgnoreParenImpCasts());
  const auto *Local = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
  if (!Local || !Local->hasLocalStorage() || isa<ParmVarDecl>(Local) ||
      !Local->getInit() || Local->getType()->isReferenceType() ||
      !Local->getType().isConstQualified() ||
      Local->getType().isVolatileQualified())
    return nullptr;
  // Also an integer class copied as it is, `const uint256 Bits = _Bits;`.
  const CXXRecordDecl *Record = Local->getType()->getAsCXXRecordDecl();
  if (!Local->getType()->isIntegralOrEnumerationType() &&
      !(Record && Record->hasDefinition() &&
        Record->getDefinition()->isTriviallyCopyable()))
    return nullptr;
  const Expr *Init = Local->getInit()->IgnoreParens();
  if (Record) {
    const auto *Copy = dyn_cast<CXXConstructExpr>(Init->IgnoreImplicit());
    if (!Copy || Copy->getNumArgs() != 1 ||
        !Copy->getConstructor()->isCopyOrMoveConstructor() ||
        !Copy->getConstructor()->isTrivial())
      return nullptr;
    Init = Copy->getArg(0)->IgnoreParenImpCasts();
  }
  // Through conversions that keep what isn't zero.
  while (const auto *Cast = dyn_cast<CastExpr>(Init)) {
    if (!keepsNonZero(Cast, Context))
      return nullptr;
    Init = Cast->getSubExpr()->IgnoreParens();
  }
  const auto *SourceRef = dyn_cast<DeclRefExpr>(Init);
  const auto *Source =
      SourceRef ? dyn_cast<VarDecl>(SourceRef->getDecl()) : nullptr;
  const auto *Function =
      dyn_cast_or_null<FunctionDecl>(Local->getParentFunctionOrMethod());
  // Not one made from itself, `const unsigned Bits = Bits;`. Also of a
  // parameter by reference to const, `uint256 const &_Bits`, which is
  // unchanged like one by value unless it is written where it is named, in a
  // function that doesn't suspend, during which what it refers to can be.
  if (!Source || Source == Local || Source->getType().isVolatileQualified() ||
      !Source->hasLocalStorage() || !Function || !Function->getBody())
    return nullptr;
  if (Source->getType()->isReferenceType() &&
      !(isa<ParmVarDecl>(Source) &&
        Source->getType().getNonReferenceType().isConstQualified() &&
        !Source->getType().getNonReferenceType().isVolatileQualified() &&
        !isa<CoroutineBodyStmt>(Function->getBody())))
    return nullptr;
  // Changed only where it is named, being a local or a parameter: assigned,
  // stepped, its address taken, or bound to a reference that isn't to
  // const, also in the lambdas of the function; what each function changes
  // so is found once.
  struct CWriteFinder : OncePerTypeVisitor<CWriteFinder> {
    llvm::DenseSet<const ValueDecl *> &Written;
    const ASTContext &Context;
    // The local references followed to what they refer to.
    int nAliases = 0;
    // Not what never runs, a branch an `if constexpr` discards, nor what
    // isn't evaluated, `noexcept(_Bits = 0)`.
    bool TraverseIfStmt(IfStmt *If) {
      if (If->isConstexpr() && !If->getCond()->isValueDependent())
        if (const std::optional<Stmt *> Case = If->getNondiscardedCase(Context))
          return TraverseStmt(If->getInit()) &&
                 TraverseStmt(If->getConditionVariableDeclStmt()) &&
                 TraverseStmt(If->getCond()) && TraverseStmt(*Case);
      return RecursiveASTVisitor::TraverseIfStmt(If);
    }
    bool TraverseUnaryExprOrTypeTraitExpr(UnaryExprOrTypeTraitExpr *) {
      return true;
    }
    bool TraverseCXXNoexceptExpr(CXXNoexceptExpr *) { return true; }
    bool TraverseCXXTypeidExpr(CXXTypeidExpr *Typeid) {
      return !Typeid->isPotentiallyEvaluated() ||
             RecursiveASTVisitor::TraverseCXXTypeidExpr(Typeid);
    }
    bool TraverseRequiresExpr(RequiresExpr *) { return true; }
    bool TraverseDecltypeTypeLoc(DecltypeTypeLoc, bool = true) { return true; }
    // Also through a cast to a reference, `static_cast<unsigned &>(_Bits)`,
    // the last of a comma, either of a conditional, and what an assignment
    // changes.
    void add(const Expr *E) {
      for (int Depth = 0; E && Depth < 16; ++Depth) {
        E = E->IgnoreParenImpCasts();
        if (const auto *Cast = dyn_cast<ExplicitCastExpr>(E);
            Cast && Cast->isGLValue())
          E = Cast->getSubExpr();
        else if (const auto *Binary = dyn_cast<BinaryOperator>(E);
                 Binary && Binary->getOpcode() == BO_Comma)
          E = Binary->getRHS();
        else if (Binary && Binary->isAssignmentOp())
          E = Binary->getLHS();
        else if (const auto *Conditional =
                     dyn_cast<AbstractConditionalOperator>(E)) {
          add(Conditional->getTrueExpr());
          E = Conditional->getFalseExpr();
        }
        // Also a member of it, `_Bits.m_Lower = 1` of an integer class.
        else if (const auto *Member = dyn_cast<MemberExpr>(E);
                 Member && !Member->isArrow())
          E = Member->getBase();
        else
          break;
      }
      if (const auto *Ref = E ? dyn_cast<DeclRefExpr>(E) : nullptr) {
        Written.insert(Ref->getDecl());
        // Also what a local reference written through refers to, `Alias` of
        // `umint const &Alias = _Bits;` in `fg_RemoveQualifiers(Alias) = 1`.
        if (const auto *Alias = dyn_cast<VarDecl>(Ref->getDecl());
            Alias && Alias->getType()->isReferenceType() &&
            Alias->hasLocalStorage() && !isa<ParmVarDecl>(Alias) &&
            Alias->getInit() && nAliases < 8) {
          ++nAliases;
          add(Alias->getInit());
          --nAliases;
        }
      }
    }
    bool VisitBinaryOperator(BinaryOperator *Binary) {
      if (Binary->isAssignmentOp())
        add(Binary->getLHS());
      return true;
    }
    bool VisitUnaryOperator(UnaryOperator *Unary) {
      if (Unary->isIncrementDecrementOp() || Unary->getOpcode() == UO_AddrOf)
        add(Unary->getSubExpr());
      return true;
    }
    // Also the object of a member function that can change it, of an
    // integer class, `_Bits.f_Clear()` or `_Bits += 1`.
    bool VisitCXXMemberCallExpr(CXXMemberCallExpr *Call) {
      if (const CXXMethodDecl *Method = Call->getMethodDecl();
          !Method || !Method->isConst())
        add(Call->getImplicitObjectArgument());
      return true;
    }
    // Also one given by reference to const to a function that casts the
    // const away, `fg_RemoveQualifiers(_Bits) = 1`.
    bool VisitCallExpr(CallExpr *Call) {
      const FunctionDecl *Callee = Call->getDirectCallee();
      if (const auto *Method = dyn_cast_or_null<CXXMethodDecl>(Callee);
          isa<CXXOperatorCallExpr>(Call) && Method &&
          Method->isImplicitObjectMemberFunction() && !Method->isConst() &&
          Call->getNumArgs() >= 1)
        add(Call->getArg(0));
      const unsigned Offset =
          isa<CXXOperatorCallExpr>(Call) && hasImplicitObject(Callee) ? 1 : 0;
      for (unsigned iArgument = Offset; iArgument < Call->getNumArgs();
           ++iArgument) {
        const unsigned iParameter = iArgument - Offset;
        const QualType Type = Callee && iParameter < Callee->getNumParams()
                                  ? Callee->getParamDecl(iParameter)->getType()
                                  : QualType();
        if (Type.isNull() ||
            (Type->isReferenceType() &&
             (!Type.getNonReferenceType().isConstQualified() ||
              callsOverride(Call) ||
              castsConstAway(calledFunctionOf(Call).first, iParameter))))
          add(Call->getArg(iArgument));
      }
      return true;
    }
    // Also given to a constructor, `CSet Set(_Bits)` of `CSet(unsigned
    // &_Bits)`.
    bool VisitCXXConstructExpr(CXXConstructExpr *Construct) {
      const CXXConstructorDecl *Constructor = Construct->getConstructor();
      for (unsigned iArgument = 0; iArgument < Construct->getNumArgs();
           ++iArgument) {
        const QualType Type =
            Constructor && iArgument < Constructor->getNumParams()
                ? Constructor->getParamDecl(iArgument)->getType()
                : QualType();
        if (Type.isNull() || (Type->isReferenceType() &&
                              !Type.getNonReferenceType().isConstQualified()))
          add(Construct->getArg(iArgument));
      }
      return true;
    }
    bool VisitVarDecl(VarDecl *Alias) {
      if (Alias->getType()->isReferenceType() &&
          !Alias->getType().getNonReferenceType().isConstQualified())
        add(Alias->getInit());
      return true;
    }
    // Also bound to a reference member of an aggregate, `CSet Setter{_Bits}`
    // of `unsigned &m_Bits`, which can write it.
    bool VisitInitListExpr(InitListExpr *List) {
      const auto *Record = List->getType()->getAsRecordDecl();
      if (!Record)
        return true;
      unsigned iInit = 0;
      for (const FieldDecl *Field : Record->fields()) {
        if (Field->isUnnamedBitField())
          continue;
        if (iInit >= List->getNumInits())
          break;
        const QualType Type = Field->getType();
        if (Type->isReferenceType() &&
            !Type.getNonReferenceType().isConstQualified())
          add(List->getInit(iInit));
        ++iInit;
      }
      return true;
    }
    // An output of inline assembly, `asm("" : "=r"(_Bits))`.
    bool VisitGCCAsmStmt(GCCAsmStmt *Assembly) {
      for (unsigned iOutput = 0; iOutput < Assembly->getNumOutputs(); ++iOutput)
        add(Assembly->getOutputExpr(iOutput));
      return true;
    }
  };
  struct CWrittenLocals {
    llvm::DenseMap<const FunctionDecl *, llvm::DenseSet<const ValueDecl *>>
        Functions;
  };
  auto [It, Inserted] =
      astCache<CWrittenLocals>(Context).Functions.try_emplace(Function);
  if (Inserted) {
    CWriteFinder Finder{{}, It->second, Context};
    Finder.TraverseStmt(Function->getBody());
  }
  return It->second.contains(Source) ? nullptr : SourceRef;
}

/// Whether \p Call, in the body of \p Switch, runs only for values of its
/// condition other than zero: it is reached, also by falling through, only
/// from cases of other values, `case 1:`, or from the default of a switch
/// that has `case 0:`, and the condition is zero when the argument is.
static bool switchExcludesZero(const SwitchStmt *Switch, const Expr *Call,
                               const CArgumentKeys &Keys,
                               const ASTContext &Context) {
  const auto *Body = dyn_cast_or_null<CompoundStmt>(Switch->getBody());
  if (!Body || !Switch->getCond() ||
      !isZeroWithArgument(Switch->getCond()->IgnoreParenImpCasts(), Keys,
                          Context))
    return false;
  auto ValueOf = [&](const CaseStmt *Case) -> std::optional<llvm::APSInt> {
    if (Case->caseStmtIsGNURange() || Case->getLHS()->isValueDependent())
      return std::nullopt;
    return Case->getLHS()->getIntegerConstantExpr(Context);
  };
  bool HasZero = false;
  llvm::SmallPtrSet<const SwitchCase *, 8> TopLevel;
  for (const Stmt *Statement : Body->body())
    for (const Stmt *Inner = Statement;
         const auto *Case = dyn_cast_or_null<SwitchCase>(Inner);
         Inner = Case->getSubStmt())
      TopLevel.insert(Case);
  for (const SwitchCase *Case = Switch->getSwitchCaseList(); Case;
       Case = Case->getNextSwitchCase()) {
    // Not a case in a statement of a case, which can be jumped into.
    if (!TopLevel.contains(Case))
      return false;
    if (const auto *Value = dyn_cast<CaseStmt>(Case))
      if (std::optional<llvm::APSInt> Constant = ValueOf(Value);
          Constant && Constant->isZero())
        HasZero = true;
  }
  auto Excludes = [&](const SwitchCase *Case) {
    if (isa<DefaultStmt>(Case))
      return HasZero;
    const std::optional<llvm::APSInt> Constant = ValueOf(cast<CaseStmt>(Case));
    return Constant && !Constant->isZero();
  };
  // Also a block that ends so, `case 0: { return -1; }`.
  std::function<bool(const Stmt *)> Leaves = [&](const Stmt *S) {
    if (const auto *Block = dyn_cast_or_null<CompoundStmt>(S))
      return !Block->body_empty() && Leaves(Block->body_back());
    if (const auto *E = dyn_cast_or_null<Expr>(S))
      return isa<CXXThrowExpr>(E->IgnoreImplicit());
    return isa_and_nonnull<ReturnStmt, BreakStmt, ContinueStmt, GotoStmt,
                           CoreturnStmt>(S);
  };
  const SourceRange Range = Call->getSourceRange();
  const SourceManager &SM = Context.getSourceManager();
  // The cases that reach where the statements are, by falling through too.
  llvm::SmallVector<const SwitchCase *, 4> Entering;
  for (const Stmt *Statement : Body->body()) {
    const Stmt *Inner = Statement;
    while (const auto *Case = dyn_cast_or_null<SwitchCase>(Inner)) {
      Entering.push_back(Case);
      Inner = Case->getSubStmt();
    }
    if (!Inner)
      continue;
    if (!SM.isBeforeInTranslationUnit(Range.getBegin(), Inner->getBeginLoc()) &&
        !SM.isBeforeInTranslationUnit(Inner->getEndLoc(), Range.getEnd()))
      return !Entering.empty() && llvm::all_of(Entering, Excludes);
    if (Leaves(Inner))
      Entering.clear();
  }
  return false;
}

/// Whether \p Function, whose body can be seen, reads what can change by
/// itself: an atomic operation, `__atomic_load_n(&m_Value, ...)` or a `load`
/// of an atomic, or what is volatile, also through the functions it calls,
/// each once.
static bool readsChangingValue(const FunctionDecl *Function) {
  llvm::SmallPtrSet<const FunctionDecl *, 8> Visited;
  std::function<bool(const FunctionDecl *, int)> Reads =
      [&](const FunctionDecl *Callee, int Depth) {
        const FunctionDecl *Definition =
            Callee ? Callee->getDefinition() : nullptr;
        if (!Definition || !Definition->getBody() || Depth > 8 ||
            !Visited.insert(Definition).second)
          return false;
        bool Changing = false;
        std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
          if (!S || Changing || isa<LambdaExpr>(S))
            return;
          if (isa<AtomicExpr>(S))
            Changing = true;
          else if (const auto *E = dyn_cast<Expr>(S);
                   E && E->isGLValue() && E->getType().isVolatileQualified())
            Changing = true;
          else if (const auto *Call = dyn_cast<CallExpr>(S)) {
            if (const auto *Member = dyn_cast<CXXMemberCallExpr>(Call);
                Member && Member->getMethodDecl() &&
                Member->getMethodDecl()->getIdentifier() &&
                (Member->getMethodDecl()->getName() == "load" ||
                 Member->getMethodDecl()->getName() == "f_Load"))
              Changing = true;
            else
              Changing = Reads(Call->getDirectCallee(), Depth + 1);
          }
          for (const Stmt *Child : childrenOnce(S))
            Visit(Child);
        };
        Visit(Definition->getBody());
        return Changing;
      };
  return Reads(Function, 0);
}

/// Whether the argument of \p Call is checked for zero first, or \p Chosen,
/// one of the values of a conditional that is the argument, also by its
/// condition, `_Bits ? _Bits : 1`.
bool NoZeroBitScanCheck::isChecked(const CallExpr *Call, ASTContext &Context,
                                   bool UnknownChecks,
                                   const Expr *Chosen) const {
  // A const local copy, `umint const Bits = _Bits;`, is checked by a check of
  // what it is a copy of, or of itself, `if (Bits == 0)`.
  bool Copied = false;
  if (isCheckedThrough(Call, Context, UnknownChecks, Chosen,
                       /*ThroughCopies=*/true, Copied))
    return true;
  bool Unused = false;
  return Copied && isCheckedThrough(Call, Context, UnknownChecks, Chosen,
                                    /*ThroughCopies=*/false, Unused);
}

bool NoZeroBitScanCheck::isCheckedThrough(
    const CallExpr *Call, ASTContext &Context, bool UnknownChecks,
    const Expr *Chosen, bool ThroughCopies, bool &Copied) const {
  const SourceManager &SM = Context.getSourceManager();
  const Expr *Argument = Chosen ? Chosen : Call->getArg(0);
  const Expr *Start = Chosen ? Chosen : Call;
  // What a check tests: the variable or member that is the argument, also
  // through conversions that keep it not zero, or else the argument's
  // expression itself, like `_Value - 1` or `static_cast<uint8>(_Value)`,
  // which can be zero when what it is computed from isn't.
  CArgumentKeys Keys;
  Keys.Read = referencesOf(Argument);
  // What a local pointer the argument reads through points to there, `_Bits`
  // of `*pBits` after `unsigned *pBits = &_Bits`, which writing `_Bits`
  // changes.
  {
    llvm::SmallVector<std::string, 2> Pointees;
    llvm::SmallPtrSet<const Stmt *, 32> Visited;
    std::function<void(const Stmt *)> Find = [&](const Stmt *S) {
      if (!S || !Visited.insert(S).second)
        return;
      if (const auto *Ref = dyn_cast<DeclRefExpr>(S)) {
        const auto *Pointer = dyn_cast<VarDecl>(Ref->getDecl());
        const Expr *Pointed =
            Pointer && Pointer->hasLocalStorage() &&
                    Pointer->getType()->isPointerType()
                ? assignedValueAt(Pointer, Ref)
                : nullptr;
        const auto *Address =
            Pointed ? dyn_cast<UnaryOperator>(Pointed->IgnoreParenImpCasts())
                    : nullptr;
        if (Address && Address->getOpcode() == UO_AddrOf) {
          const std::string Target =
              ReferenceCollector::valueKey(Address->getSubExpr());
          const std::string Through =
              ReferenceCollector::declKey(Pointer) + "[0]";
          if (!Target.empty())
            for (const auto &Entry : Keys.Read)
              if (Entry.getKey().starts_with(Through))
                Pointees.push_back(Target +
                                   Entry.getKey().substr(Through.size()).str());
        }
        // Also into the object a member function gives a pointer into,
        // `_Bits` of `*pBits` after `auto *pBits = _Bits.f_FindEqual(_Key)`,
        // which writing `_Bits`, `_Bits[_Key] = 0`, changes, all of it.
        const auto *Lookup =
            Pointed && !Address
                ? dyn_cast<CXXMemberCallExpr>(Pointed->IgnoreParenImpCasts())
                : nullptr;
        if (Lookup && Lookup->getImplicitObjectArgument()) {
          const std::string Target = ReferenceCollector::valueKey(
              Lookup->getImplicitObjectArgument());
          const std::string Through =
              ReferenceCollector::declKey(Pointer) + "[0]";
          if (!Target.empty() &&
              llvm::any_of(Keys.Read.keys(), [&](StringRef Key) {
                return Key.starts_with(Through);
              }))
            Pointees.push_back(Target);
        }
      }
      for (const Stmt *Child : childrenOnce(S))
        Find(Child);
    };
    Find(Argument);
    for (const std::string &Pointee : Pointees)
      Keys.Read.insert(Pointee);
  }
  const Expr *Value = Argument;
  for (;;) {
    Value = Value->IgnoreParens();
    if (const auto *Materialize = dyn_cast<MaterializeTemporaryExpr>(Value))
      Value = Materialize->getSubExpr();
    // `Value--` gives the value before it changes it, also of an integer
    // class, `operator--(int)` of a `TCInt`.
    else if (const auto *Postfix = dyn_cast<UnaryOperator>(Value);
             Postfix && Postfix->isPostfix() &&
             Postfix->isIncrementDecrementOp())
      Value = Postfix->getSubExpr();
    else if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(Value);
             Operator && Operator->getNumArgs() == 2 &&
             (Operator->getOperator() == OO_PlusPlus ||
              Operator->getOperator() == OO_MinusMinus))
      Value = Operator->getArg(0);
    else if (const auto *Cast = dyn_cast<CastExpr>(Value);
             Cast && keepsNonZero(Cast, Context))
      Value = Cast->getSubExpr();
    // A copy, like of an integer class given by value.
    else if (const auto *Copy = dyn_cast<CXXConstructExpr>(Value);
             Copy && Copy->getNumArgs() == 1 &&
             Copy->getConstructor()->isCopyOrMoveConstructor())
      Value = Copy->getArg(0);
    // Also one widened to an integer class at least as wide, of the same
    // template, `uint256(_Bits)` of a `uint128`, or of an integer,
    // `uint256(_Bits)` of a `uint64`, which keeps its bits.
    else if (const Expr *Widened =
                 widenedFrom(Value, Value->getType(), Context))
      Value = Widened;
    // Also written as a cast, `uint256(_Bits)`, of what it makes.
    else if (const auto *Written = dyn_cast<ExplicitCastExpr>(Value);
             Written && Written->getCastKind() == CK_ConstructorConversion &&
             widenedFrom(Written->getSubExpr()->IgnoreImplicit(),
                         Written->getType(), Context))
      Value = widenedFrom(Written->getSubExpr()->IgnoreImplicit(),
                          Written->getType(), Context);
    // A const local copy of a variable that nothing in the function writes,
    // `const unsigned Bits = _Bits;`, at least as wide, is that variable,
    // whatever is checked of it.
    else if (const Expr *Source =
                 ThroughCopies ? constCopySource(Value, Context) : nullptr) {
      Value = Source;
      Copied = true;
    } else
      break;
  }
  // The value itself, not the variables that find it, like the `_Index` of
  // `_pInfo[_Index].m_Bits`.
  // A call that can give another value each time, like `fg_NextBits()`, is
  // not what a test of another call of it tested: only an operator or a
  // conversion that can't change its object or what it is given, which gives
  // the same value for the same operands. A function, also a constexpr one,
  // can read or change other state.
  bool Stable = true;
  {
    std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
      if (!S || !Stable || isa<LambdaExpr>(S))
        return;
      // Nor what changes a value, like `--_Value` or `_Value -= 1`, whose
      // value is another each time.
      if (const auto *Unary = dyn_cast<UnaryOperator>(S);
          Unary && Unary->isIncrementDecrementOp())
        Stable = false;
      if (const auto *Binary = dyn_cast<BinaryOperator>(S);
          Binary && Binary->isAssignmentOp())
        Stable = false;
      if (const auto *Call = dyn_cast<CallExpr>(S)) {
        const FunctionDecl *Callee = Call->getDirectCallee();
        const auto *Member = dyn_cast<CXXMemberCallExpr>(Call);
        // Also a const one that changes what is mutable, `m_Bits = 0` of
        // `mutable unsigned m_Bits`, which the next evaluation sees.
        // Not an index operator that can insert what it is asked for, `_Masks
        // [_Key]` of a map that isn't const, which gives the same element for
        // the same key each time, which it doesn't change.
        const bool Index =
            isa<CXXOperatorCallExpr>(Call) &&
            cast<CXXOperatorCallExpr>(Call)->getOperator() == OO_Subscript;
        auto Changes = [&](const FunctionDecl *Function) {
          if (const auto *Method = dyn_cast<CXXMethodDecl>(Function);
              Method && Method->isImplicitObjectMemberFunction() &&
              !(Index && !Method->isConst()) && methodChangesObject(Method))
            return true;
          if (writesMutableOfParameters(Function->getDefinition(), 0))
            return true;
          // Nor what reads what can change by itself, atomically,
          // `__atomic_load_n(&m_Value, ...)`, or what is volatile, which can
          // give another value each time, also through what it calls.
          if (readsChangingValue(Function))
            return true;
          // Nor what is given by a reference to what isn't const, unless
          // the body can be seen not to write through it, a key forwarded,
          // `operator[](tf_CKey &&_Key) const` given an lvalue.
          const FunctionDecl *Definition = Function->getDefinition();
          return llvm::any_of(
              llvm::enumerate(Function->parameters()), [&](auto Entry) {
                const QualType Type = Entry.value()->getType();
                if (!(Type->isReferenceType() || Type->isPointerType()) ||
                    Type->getPointeeType().isConstQualified())
                  return false;
                if (!Type->isReferenceType() || !Definition ||
                    !Definition->getBody() ||
                    Entry.index() >= Definition->getNumParams())
                  return true;
                return !onlyReadsParameter(Definition, Entry.index());
              });
        };
        // Also a getter whose body can be seen only to give access to its
        // object, `_Values.f_GetFirst()` of `return (*this)[0];`, which
        // gives the same element each time.
        const CXXMethodDecl *Method = Member ? Member->getMethodDecl() : nullptr;
        const bool Getter = Method && !Method->isVirtual() &&
                            onlyGivesAccess(Method);
        if (!((isa<CXXOperatorCallExpr>(Call) ||
               (Member && isa_and_nonnull<CXXConversionDecl>(Method)) ||
               Getter) &&
              Callee && !Changes(Callee)) &&
            !Call->isTypeDependent())
          Stable = false;
      }
      for (const Stmt *Child : childrenOnce(S))
        Visit(Child);
    };
    Visit(Value);
  }
  // Also what a local pointer it reads through points to, `_Bits` of
  // `*pBits`, which a test of `_Bits` checks.
  if (const Expr *Pointee = pointeeOf(Value))
    if (const std::string Key = ReferenceCollector::valueKey(Pointee);
        !Key.empty())
      Keys.Tested.insert(Key);
  // A member of what a free function is given that it gives a reference to,
  // `fg_GetLower(_Bits)` of `return _Integer.m_Lower;`, is that member,
  // `_Bits.m_Lower`, which a scan of another call of it reads.
  const auto *GivesMember = dyn_cast<CallExpr>(Value->IgnoreParenImpCasts());
  if (const std::string Member =
          GivesMember && memberOfGivenReturned(GivesMember)
              ? ReferenceCollector::objectKey(GivesMember)
              : std::string();
      !Member.empty())
    Keys.Tested.insert(Member);
  else if (const std::string Value_ = ReferenceCollector::valueKey(Value);
           !Value_.empty())
    Keys.Tested.insert(Value_);
  else if (Stable) {
    Keys.Tested.insert(ReferenceCollector::expressionKey(Value, Context));
    // Also overloaded, of an integer class, `_Size - 1` of a `uint256`.
    const Expr *Minuend = nullptr;
    const Expr *Subtrahend = nullptr;
    // Not through what narrows it, `uint8(_Size - 1)`, which is zero at
    // other sizes too.
    const Expr *Kept = Value->IgnoreParens();
    while (const auto *Cast = dyn_cast<ImplicitCastExpr>(Kept)) {
      if (!keepsNonZero(Cast, Context))
        break;
      Kept = Cast->getSubExpr()->IgnoreParens();
    }
    if (const auto *Difference = dyn_cast<BinaryOperator>(Kept);
        Difference && Difference->getOpcode() == BO_Sub &&
        Difference->getType()->isIntegerType()) {
      Minuend = Difference->getLHS();
      Subtrahend = Difference->getRHS();
    } else if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(
                   Value->IgnoreImplicit()->IgnoreParenImpCasts());
               Operator && Operator->getOperator() == OO_Minus &&
               Operator->getNumArgs() == 2 &&
               isUnsignedInteger(Operator->getType(), 0)) {
      Minuend = Operator->getArg(0);
      Subtrahend = Operator->getArg(1);
    }
    if (Minuend)
      if (const std::string Base = ReferenceCollector::valueKey(
              Minuend->IgnoreImplicit()->IgnoreParenImpCasts());
          !Base.empty())
        if (const std::optional<llvm::APSInt> Constant =
                integerConstant(Subtrahend, Context)) {
          Keys.DifferenceOf = Base;
          Keys.DifferenceZeroAt = *Constant;
        }
  } else {
    Keys.Tested.insert("call:" +
                       std::to_string(reinterpret_cast<uintptr_t>(Value)));
  }
  // Also the object that a wrapper of an integer gives the only member of,
  // `_Bits` of `umint(_Bits)` of a `zmint`, whose tests, `_Bits == 0`, test
  // that member.
  if (const Expr *Wrapper = wrapperOf(Value))
    if (const std::string Key = ReferenceCollector::valueKey(Wrapper);
        !Key.empty())
      Keys.Tested.insert(Key);
  // Also the word of an integer class that its conversion to an integer at
  // least as wide as the word gives, `_Bits.m_Lower` of `uint64(_Bits)` of a
  // `uint96`, whose test, `_Bits.m_Lower != 0`, tells that it isn't zero.
  {
    const Expr *Converted = Value;
    for (int Depth = 0; Depth < 8; ++Depth) {
      Converted = Converted->IgnoreParens();
      const auto *Cast = dyn_cast<CastExpr>(Converted);
      if (!Cast || !llvm::is_contained({CK_UserDefinedConversion, CK_NoOp,
                                        CK_LValueToRValue},
                                       Cast->getCastKind()))
        break;
      Converted = Cast->getSubExpr();
    }
    const auto *Conversion = dyn_cast<CXXMemberCallExpr>(Converted);
    if (Conversion && Value->getType()->isIntegerType() &&
        isa_and_nonnull<CXXConversionDecl>(Conversion->getMethodDecl()) &&
        Conversion->getImplicitObjectArgument())
      if (const llvm::SmallVector<const FieldDecl *, 4> Given = givenMembers(
              Conversion->getMethodDecl()->getDefinition(), Context);
          !Given.empty() && Given.back()->getType()->isIntegerType() &&
          Context.getIntWidth(Value->getType()) >=
              Context.getIntWidth(Given.back()->getType()))
        if (std::string Word = ReferenceCollector::objectKey(
                Conversion->getImplicitObjectArgument());
            !Word.empty()) {
          for (const FieldDecl *Field : Given)
            Word += "." + ReferenceCollector::declKey(Field);
          Keys.Tested.insert(Word);
        }
  }
  // A local reference is also what it refers to, `unsigned const &Value =
  // _Value`, which tests of `_Value` check.
  // Also one bound to itself, `unsigned const &Value = Value`, ends.
  int nAliases = 0;
  for (const Expr *Aliased = Value->IgnoreParenImpCasts(), *Next = nullptr;
       Aliased && ++nAliases <= 8; Aliased = Next) {
    Next = nullptr;
    const auto *Ref = dyn_cast<DeclRefExpr>(Aliased);
    Next = Ref && Keys.Tested.size() <= 8
               ? ReferenceCollector::aliasedValue(Ref->getDecl())
               : nullptr;
    if (!Next)
      break;
    // Not what it was bound to through a pointer that is pointed elsewhere
    // after, `_pInfo->m_Bits` of `unsigned const &Bits = _pInfo->m_Bits`
    // with `_pInfo = _pOther`, which another object's test can't check.
    {
      llvm::StringSet<> Selectors;
      std::function<void(const Stmt *)> FindSelectors = [&](const Stmt *S) {
        if (!S)
          return;
        const Expr *Through = nullptr;
        if (const auto *Member = dyn_cast<MemberExpr>(S); Member && Member->isArrow())
          Through = Member->getBase();
        else if (const auto *Deref = dyn_cast<UnaryOperator>(S);
                 Deref && Deref->getOpcode() == UO_Deref)
          Through = Deref->getSubExpr();
        else if (const auto *Subscript = dyn_cast<ArraySubscriptExpr>(S);
                 Subscript && Subscript->getBase()->getType()->isPointerType())
          Through = Subscript->getBase();
        // And what selects the element, `Values[Index]`, also by an index
        // operator, `_Bits[_Index]` of a vector, which changing selects
        // another.
        std::function<void(const Stmt *)> AddIndexes = [&](const Stmt *Index) {
          if (const auto *Named = dyn_cast_or_null<DeclRefExpr>(Index);
              Named && isa<VarDecl>(Named->getDecl()))
            Selectors.insert(ReferenceCollector::declKey(Named->getDecl()));
          if (Index)
            for (const Stmt *Child : childrenOnce(Index))
              AddIndexes(Child);
        };
        if (const auto *Subscript = dyn_cast<ArraySubscriptExpr>(S))
          AddIndexes(Subscript->getIdx());
        else if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(S);
                 Operator && Operator->getOperator() == OO_Subscript &&
                 Operator->getNumArgs() == 2)
          AddIndexes(Operator->getArg(1));
        if (const auto *Pointer = Through ? dyn_cast<DeclRefExpr>(
                                                Through->IgnoreParenImpCasts())
                                          : nullptr) {
          Selectors.insert(ReferenceCollector::selectorKey(Pointer->getDecl()));
          Selectors.insert(ReferenceCollector::declKey(Pointer->getDecl()));
        }
        // Also a pointer member, `_View.m_pInfo->m_Bits`, which assigning it,
        // or the object it is in, points elsewhere, but writing what it
        // points to doesn't.
        else if (const auto *Member =
                     Through ? dyn_cast<MemberExpr>(Through->IgnoreParenImpCasts())
                             : nullptr;
                 Member && Member->getType()->isPointerType())
          if (const std::string Key = ReferenceCollector::valueKey(Member);
              !Key.empty())
            Selectors.insert(Key + "*");
        for (const Stmt *Child : childrenOnce(S))
          FindSelectors(Child);
      };
      FindSelectors(Next);
      const auto *Function = dyn_cast_or_null<FunctionDecl>(
          Ref->getDecl()->getParentFunctionOrMethod());
      // Pointed elsewhere between where the reference is bound and the call,
      // or after the call in a loop around it that the reference is bound
      // outside of, which runs before the call again.
      const SourceManager &SM = Context.getSourceManager();
      const SourceLocation Bound = Ref->getDecl()->getEndLoc();
      const SourceLocation At = Call->getBeginLoc();
      auto Before = [&](SourceLocation A, SourceLocation B) {
        return SM.isBeforeInTranslationUnit(A, B);
      };
      bool Repointed = false;
      std::function<void(const Stmt *)> FindWrites = [&](const Stmt *S) {
        if (!S || Repointed)
          return;
        const SourceRange Range = S->getSourceRange();
        if (Range.isInvalid() || Range.getBegin().isMacroID() ||
            Range.getEnd().isMacroID()) {
          Repointed = writesAny(S, Selectors) &&
                      !(Range.isValid() && Before(Range.getEnd(), Bound));
          return;
        }
        // Only before it is bound.
        if (Before(Range.getEnd(), Bound))
          return;
        // A loop around the call that the reference is bound outside of.
        if (isa<WhileStmt, ForStmt, DoStmt, CXXForRangeStmt>(S) &&
            Before(Range.getBegin(), At) && Before(At, Range.getEnd()) &&
            Before(Range.getBegin(), Bound) == false &&
            writesAny(S, Selectors)) {
          Repointed = true;
          return;
        }
        // Only after the call.
        if (Before(At, Range.getBegin()))
          return;
        // Between them.
        if (Before(Bound, Range.getBegin()) && Before(Range.getEnd(), At)) {
          Repointed = writesAny(S, Selectors);
          return;
        }
        for (const Stmt *Child : childrenOnce(S))
          FindWrites(Child);
      };
      if (!Selectors.empty()) {
        if (!Function)
          break;
        FindWrites(Function->getBody());
        if (Repointed)
          break;
      }
    }
    // An element, `_pValues[0]`, is tested by its expression, also one a
    // getter gives a reference to, `_Bits.f_GetFirst()` of `return
    // (*this)[0];`, by the element it is, which tests of `_Bits[0]` check.
    if (ReferenceCollector::valueKey(Next).empty()) {
      Keys.Tested.insert(ReferenceCollector::expressionKey(Next, Context));
      if (const std::string Element = ReferenceCollector::objectKey(Next);
          !Element.empty() && isa<CXXMemberCallExpr>(Next->IgnoreParens()))
        Keys.Tested.insert(Element);
    }
    if (const std::string Key = ReferenceCollector::valueKey(Next);
        !Key.empty())
      Keys.Tested.insert(Key);
  }
  // The complement of a value of the argument's type, like
  // `CEntryType(~Entry)`, also promoted and converted back.
  {
    const Expr *Complemented = Value;
    QualType Type = Complemented->getType();
    if (const auto *Cast = dyn_cast<CastExpr>(Complemented)) {
      Type = Cast->getType();
      Complemented = Cast->getSubExpr();
    }
    // Past the copies of an integer class, like `CInt128(~Entry)`.
    for (int Depth = 0; Depth < 8; ++Depth) {
      Complemented = Complemented->IgnoreParenImpCasts();
      if (const auto *Materialize =
              dyn_cast<MaterializeTemporaryExpr>(Complemented))
        Complemented = Materialize->getSubExpr();
      else if (const auto *Bind = dyn_cast<CXXBindTemporaryExpr>(Complemented))
        Complemented = Bind->getSubExpr();
      else if (const auto *Cast = dyn_cast<CXXFunctionalCastExpr>(Complemented))
        Complemented = Cast->getSubExpr();
      else if (const auto *Copy = dyn_cast<CXXConstructExpr>(Complemented);
               Copy && Copy->getNumArgs() == 1 &&
               Copy->getConstructor()->isCopyOrMoveConstructor())
        Complemented = Copy->getArg(0);
      else
        break;
    }
    const Expr *Operand = nullptr;
    if (const auto *Not = dyn_cast<UnaryOperator>(Complemented);
        Not && Not->getOpcode() == UO_Not)
      Operand = Not->getSubExpr()->IgnoreParenImpCasts();
    // Also overloaded, of an integer class.
    else if (const auto *Tilde = dyn_cast<CXXOperatorCallExpr>(Complemented);
             Tilde && Tilde->getOperator() == OO_Tilde &&
             Tilde->getNumArgs() == 1)
      Operand = Tilde->getArg(0)->IgnoreParenImpCasts();
    if (Operand && Context.hasSameUnqualifiedType(Operand->getType(), Type)) {
      Keys.ComplementOf = referencesOf(Operand);
      Keys.ComplementType = Type;
    }
  }
  // A local pointer to what it reads, `unsigned *pValue = &_Value` or
  // `pValue = &_Value`, or a copy of one, writes it through what it points to,
  // `*pValue = 0`, anywhere in the function.
  {
    const Stmt *Body = nullptr;
    for (DynTypedNode Node = DynTypedNode::create(*Start); !Body;) {
      const auto Parents = instantiatedParents(Node, Context);
      if (Parents.empty())
        break;
      Node = Parents[0];
      if (const auto *Function = Node.get<FunctionDecl>())
        Body = Function->getBody();
      else if (const auto *Lambda = Node.get<LambdaExpr>())
        Body = Lambda->getBody();
    }
    // Whether \p Value is the address of what is read, or a pointer to it.
    // What a pointer made from \p Value reads through of what is read, as
    // what follows the pointer's key: `[0]` of `&_Value`, or of a copy of a
    // pointer the argument reads through, like `[1]` of `_pValues[1]` or
    // `[0].m_Bits` of `_pInfo->m_Bits`.
    std::function<llvm::SmallVector<std::string, 2>(const Expr *)> ReadThrough =
        [&](const Expr *Value) {
      llvm::SmallVector<std::string, 2> Suffixes;
      Value = Value->IgnoreParenImpCasts();
      // Either value of a conditional, `bChoose ? &_Value : pOther`.
      if (const auto *Conditional = dyn_cast<AbstractConditionalOperator>(Value)) {
        Suffixes = ReadThrough(Conditional->getTrueExpr());
        for (std::string &Suffix : ReadThrough(Conditional->getFalseExpr()))
          if (!llvm::is_contained(Suffixes, Suffix))
            Suffixes.push_back(std::move(Suffix));
        return Suffixes;
      }
      if (const auto *Address = dyn_cast<UnaryOperator>(Value);
          Address && Address->getOpcode() == UO_AddrOf) {
        // Of an object a member of which is read, `&_Info` of `_Info.m_Bits`,
        // that member through it, `[0].m_Bits`, which writing another member
        // through it, `pInfo->m_Other = 0`, doesn't touch.
        const std::string Object =
            ReferenceCollector::valueKey(Address->getSubExpr());
        if (!Object.empty()) {
          for (StringRef Read : Keys.Read.keys())
            if (Read == Object) {
              if (!llvm::is_contained(Suffixes, "[0]"))
                Suffixes.push_back("[0]");
            } else if (Read.size() > Object.size() &&
                       Read.starts_with(Object) &&
                       (Read[Object.size()] == '.' ||
                        Read[Object.size()] == '[')) {
              std::string Suffix =
                  ("[0]" + Read.drop_front(Object.size())).str();
              if (!llvm::is_contained(Suffixes, Suffix))
                Suffixes.push_back(std::move(Suffix));
            }
        }
        if (Suffixes.empty() && touchesAny(Address->getSubExpr(), Keys.Read))
          Suffixes.push_back("[0]");
        return Suffixes;
      }
      // Also what a member function whose body can be seen gives back of a
      // pointer member of its object, `f_GetStrWritable()` of `return
      // m_pData;`, like that member.
      if (const auto *Call = dyn_cast<CXXMemberCallExpr>(Value);
          Call && Call->getType()->isPointerType() && Call->getMethodDecl() &&
          Call->getImplicitObjectArgument())
        if (const FunctionDecl *Definition =
                Call->getMethodDecl()->getDefinition();
            Definition && Definition->getBody()) {
          const FieldDecl *Given = nullptr;
          bool Only = true;
          std::function<void(const Stmt *)> Returns = [&](const Stmt *S) {
            if (!S || !Only || isa<LambdaExpr>(S))
              return;
            if (const auto *Return = dyn_cast<ReturnStmt>(S)) {
              const auto *Member =
                  Return->getRetValue()
                      ? dyn_cast<MemberExpr>(
                            Return->getRetValue()->IgnoreParenImpCasts())
                      : nullptr;
              const auto *Field =
                  Member && isa<CXXThisExpr>(
                                Member->getBase()->IgnoreParenImpCasts())
                      ? dyn_cast<FieldDecl>(Member->getMemberDecl())
                      : nullptr;
              if (!Field || !Field->getType()->isPointerType() ||
                  (Given && Given != Field))
                Only = false;
              Given = Field;
              return;
            }
            for (const Stmt *Child : childrenOnce(S))
              Returns(Child);
          };
          Returns(Definition->getBody());
          const auto *Callee =
              dyn_cast<MemberExpr>(Call->getCallee()->IgnoreParens());
          std::string Object =
              ReferenceCollector::objectKey(Call->getImplicitObjectArgument());
          if (Callee && Callee->isArrow() && !Object.empty() &&
              !isa<CXXThisExpr>(
                  Call->getImplicitObjectArgument()->IgnoreParenImpCasts()))
            Object += "[0]";
          if (Only && Given && !Object.empty()) {
            const std::string Key =
                Object + "." + ReferenceCollector::declKey(Given);
            for (const auto &Entry : Keys.Read) {
              const StringRef Read = Entry.getKey();
              if (Read.starts_with(Key + "["))
                Suffixes.push_back(Read.drop_front(Key.size()).str());
            }
            if (!Suffixes.empty())
              return Suffixes;
          }
        }
      // Also what a container gives of its elements, `_Bits.f_GetArray()`: a
      // member function that gives a pointer to what its `operator[]` gives a
      // reference to, which points to the first.
      if (const auto *Call = dyn_cast<CXXMemberCallExpr>(Value);
          Call && Call->getImplicitObjectArgument() && Call->getRecordDecl() &&
          Call->getType()->isPointerType()) {
        const CXXRecordDecl *Record = Call->getRecordDecl();
        const QualType Element = Call->getType()->getPointeeType();
        const bool Indexes = llvm::any_of(
            Record->lookup(
                Context.DeclarationNames.getCXXOperatorName(OO_Subscript)),
            [&](const NamedDecl *Found) {
              const auto *Subscript = dyn_cast<CXXMethodDecl>(Found);
              return Subscript &&
                     Subscript->getReturnType()->isLValueReferenceType() &&
                     Context.hasSameUnqualifiedType(Subscript->getReturnType()
                                                        .getNonReferenceType()
                                                        .getCanonicalType(),
                                                    Element.getCanonicalType());
            });
        const auto *Callee =
            dyn_cast<MemberExpr>(Call->getCallee()->IgnoreParens());
        std::string Key =
            ReferenceCollector::objectKey(Call->getImplicitObjectArgument());
        if (Callee && Callee->isArrow() && !Key.empty() &&
            !isa<CXXThisExpr>(
                Call->getImplicitObjectArgument()->IgnoreParenImpCasts()))
          Key += "[0]";
        if (Indexes && !Key.empty()) {
          for (const auto &Entry : Keys.Read) {
            const StringRef Read = Entry.getKey();
            if (Read.starts_with(Key + "[") &&
                !llvm::is_contained(Suffixes, Read.drop_front(Key.size())))
              Suffixes.push_back(Read.drop_front(Key.size()).str());
          }
          if (!Suffixes.empty())
            return Suffixes;
        }
      }
      // Also what a smart pointer gives of what it points to, `_pInfo.f_Get()`:
      // a member function of a class with an `operator->` that gives the same
      // pointer.
      if (const auto *Call = dyn_cast<CXXMemberCallExpr>(Value);
          Call && Call->getImplicitObjectArgument() && Call->getRecordDecl() &&
          Call->getType()->isPointerType()) {
        const CXXRecordDecl *Record = Call->getRecordDecl();
        const bool LikeArrow = llvm::any_of(
            Record->lookup(
                Context.DeclarationNames.getCXXOperatorName(OO_Arrow)),
            [&](const NamedDecl *Found) {
              const auto *Arrow = dyn_cast<CXXMethodDecl>(Found);
              return Arrow && Context.hasSameUnqualifiedType(
                                  Arrow->getReturnType().getCanonicalType(),
                                  Call->getType().getCanonicalType());
            });
        if (const std::string Key = ReferenceCollector::objectKey(
                Call->getImplicitObjectArgument());
            LikeArrow && !Key.empty()) {
          for (const auto &Entry : Keys.Read) {
            const StringRef Read = Entry.getKey();
            if (Read.starts_with(Key + "["))
              Suffixes.push_back(Read.drop_front(Key.size()).str());
          }
          return Suffixes;
        }
      }
      // Also what a helper whose body can be seen gives back, the address of
      // what it is given by reference, `unsigned *fg_Pointer(unsigned &_Bits)
      // { return &_Bits; }`, or a pointer as it is.
      if (const auto *Helper = dyn_cast<CallExpr>(Value);
          Helper && Helper->getType()->isPointerType()) {
        for (const Expr *Argument :
             returnedArguments(Helper, /*Addressed=*/true))
          if (touchesAny(Argument, Keys.Read) &&
              !llvm::is_contained(Suffixes, "[0]"))
            Suffixes.push_back("[0]");
        for (const Expr *Argument : returnedArguments(Helper))
          for (std::string &Suffix : ReadThrough(Argument))
            if (!llvm::is_contained(Suffixes, Suffix))
              Suffixes.push_back(std::move(Suffix));
        // Also what a smart pointer it is given by reference points to,
        // `return _pInfo.f_Get();` of a getter that gives what its
        // `operator->` does, on each of its returns.
        if (const FunctionDecl *Definition =
                Helper->getDirectCallee()
                    ? Helper->getDirectCallee()->getDefinition()
                    : nullptr;
            Definition && Definition->getBody() &&
            !isa<CXXMemberCallExpr, CXXOperatorCallExpr>(Helper)) {
          const ParmVarDecl *Given = nullptr;
          bool Only = true;
          std::function<void(const Stmt *)> Returns = [&](const Stmt *S) {
            if (!S || !Only || isa<LambdaExpr>(S))
              return;
            if (const auto *Return = dyn_cast<ReturnStmt>(S)) {
              const auto *Get =
                  Return->getRetValue()
                      ? dyn_cast<CXXMemberCallExpr>(
                            Return->getRetValue()->IgnoreParenImpCasts())
                      : nullptr;
              const auto *Object =
                  Get && Get->getImplicitObjectArgument()
                      ? dyn_cast<DeclRefExpr>(Get->getImplicitObjectArgument()
                                                  ->IgnoreParenImpCasts())
                      : nullptr;
              const auto *Parameter =
                  Object ? dyn_cast<ParmVarDecl>(Object->getDecl()) : nullptr;
              const CXXRecordDecl *Record =
                  Get ? Get->getRecordDecl() : nullptr;
              const bool LikeArrow =
                  Record &&
                  llvm::any_of(
                      Record->lookup(
                          Context.DeclarationNames.getCXXOperatorName(
                              OO_Arrow)),
                      [&](const NamedDecl *Found) {
                        const auto *Arrow = dyn_cast<CXXMethodDecl>(Found);
                        return Arrow &&
                               Context.hasSameUnqualifiedType(
                                   Arrow->getReturnType().getCanonicalType(),
                                   Get->getType().getCanonicalType());
                      });
              if (!Parameter || !Parameter->getType()->isReferenceType() ||
                  !LikeArrow || (Given && Given != Parameter))
                Only = false;
              Given = Parameter;
              return;
            }
            for (const Stmt *Child : childrenOnce(S))
              Returns(Child);
          };
          Returns(Definition->getBody());
          if (Only && Given &&
              Given->getFunctionScopeIndex() < Helper->getNumArgs())
            if (const std::string Key = ReferenceCollector::objectKey(
                    Helper->getArg(Given->getFunctionScopeIndex()));
                !Key.empty())
              for (const auto &Entry : Keys.Read) {
                const StringRef Read = Entry.getKey();
                if (Read.starts_with(Key + "[") &&
                    !llvm::is_contained(Suffixes, Read.drop_front(Key.size())))
                  Suffixes.push_back(Read.drop_front(Key.size()).str());
              }
        }
        // And what a lambda that captures by reference returns, `[&] {
        // return &_Bits; }()`.
        if (const LambdaExpr *Lambda = capturingLambdaCalled(Helper))
          for (const Expr *Returned : lambdaReturns(Lambda))
            for (std::string &Suffix : ReadThrough(Returned))
              if (!llvm::is_contained(Suffixes, Suffix))
                Suffixes.push_back(std::move(Suffix));
        return Suffixes;
      }
      // Also through a cast to a pointer, `static_cast<unsigned *>(&_Bits)`,
      // also to elements of another size, whose indexes are of those,
      // `reinterpret_cast<uint8 *>(&_Bits)`, whose `[1]` is in `[0]` of a
      // `uint32`.
      if (const auto *Cast = dyn_cast<ExplicitCastExpr>(Value);
          Cast && Cast->getType()->isPointerType() &&
          Cast->getSubExpr()->getType()->isPointerType()) {
        llvm::SmallVector<std::string, 2> Through =
            ReadThrough(Cast->getSubExpr());
        const QualType To = Cast->getType()->getPointeeType();
        const QualType From = Cast->getSubExpr()->getType()->getPointeeType();
        if (To->isIncompleteType() || From->isIncompleteType() ||
            To->isDependentType() || From->isDependentType() ||
            To->isVoidType() || From->isVoidType())
          return Through;
        const int64_t ToSize = Context.getTypeSizeInChars(To).getQuantity();
        const int64_t FromSize = Context.getTypeSizeInChars(From).getQuantity();
        if (ToSize <= 0 || FromSize <= 0 || ToSize == FromSize)
          return Through;
        llvm::SmallVector<std::string, 2> Resized;
        auto Add = [&](std::string Suffix) {
          if (!llvm::is_contained(Resized, Suffix))
            Resized.push_back(std::move(Suffix));
        };
        for (const std::string &Suffix : Through) {
          int64_t Index = 0;
          StringRef Rest(Suffix);
          if (!Rest.consume_front("[") || Rest.consumeInteger(10, Index) ||
              Rest != "]") {
            Add(Suffix);
            continue;
          }
          // The elements of the new size that overlap the old one.
          const int64_t Begin = Index * FromSize;
          const int64_t End = Begin + FromSize;
          for (int64_t iTo = llvm::divideFloorSigned(Begin, ToSize);
               iTo * ToSize < End; ++iTo)
            Add("[" + std::to_string(iTo) + "]");
        }
        return Resized;
      }
      // Moved by a constant, `_pBits + 1`, whose `[0]` is `[1]` of what it
      // is moved from, also of what a cast gives, `static_cast<unsigned *>(
      // _pBits) + 1`.
      if (const auto *Offset = dyn_cast<BinaryOperator>(Value);
          Offset && Offset->isAdditiveOp() &&
          Offset->getType()->isPointerType()) {
        const bool LeftBase = Offset->getLHS()->getType()->isPointerType();
        const Expr *Base = LeftBase ? Offset->getLHS() : Offset->getRHS();
        const Expr *Amount = LeftBase ? Offset->getRHS() : Offset->getLHS();
        const std::optional<llvm::APSInt> Moved =
            Amount->isValueDependent()
                ? std::nullopt
                : Amount->getIntegerConstantExpr(Context);
        // Moved by what isn't constant, `_pBits + _Offset`, which can be
        // zero, at least where it is.
        if (!Moved)
          return ReadThrough(Base);
        const int64_t Shift = Offset->getOpcode() == BO_Sub
                                  ? -Moved->getExtValue()
                                  : Moved->getExtValue();
        // Also back from where it is moved to, `pEnd[-1]` of `pEnd = _pBits +
        // 1`.
        for (const std::string &Suffix : ReadThrough(Base)) {
          StringRef Rest(Suffix);
          int64_t Index = 0;
          if (!Rest.consume_front("[") || Rest.consumeInteger(10, Index) ||
              !Rest.consume_front("]"))
            continue;
          Suffixes.push_back("[" + std::to_string(Index - Shift) + "]" +
                             Rest.str());
        }
        return Suffixes;
      }
      if (const auto *Ref = dyn_cast<DeclRefExpr>(Value)) {
        const std::string Key = ReferenceCollector::declKey(Ref->getDecl());
        if (Ref->getType()->isPointerType() && Keys.Read.contains(Key))
          Suffixes.push_back("[0]");
        for (const auto &Entry : Keys.Read) {
          const StringRef Read = Entry.getKey();
          if (Read.starts_with(Key + "["))
            Suffixes.push_back(Read.drop_front(Key.size()).str());
        }
      }
      // Also a pointer member of a local, `View.m_pBits` of `CView View{
      // &_Bits}`, which points where it does.
      if (const auto *Member = dyn_cast<MemberExpr>(Value);
          Member && Member->getType()->isPointerType()) {
        const llvm::StringSet<> MemberKeys =
            referencesOf(Member, nullptr, /*Written=*/true);
        for (StringRef Key : MemberKeys.keys())
          for (const auto &Entry : Keys.Read) {
            const StringRef Read = Entry.getKey();
            if (Read.starts_with((Key + "[").str()))
              Suffixes.push_back(Read.drop_front(Key.size()).str());
          }
      }
      return Suffixes;
    };
    auto PointsToRead = [&](const Expr *Value) {
      return !ReadThrough(Value).empty();
    };
    auto AddAlias = [&](const ValueDecl *Alias, const Expr *Value) {
      bool AddedAny = false;
      for (const std::string &Suffix : ReadThrough(Value))
        AddedAny |= Keys.Read
                        .insert(ReferenceCollector::declKey(Alias) + Suffix)
                        .second;
      return AddedAny;
    };
    // Also a pointer to const, through which a `const_cast` can write,
    // `*const_cast<unsigned *>(pBits) = 0`.
    // Also a static local, `static unsigned *const pBits = &g_Bits;`.
    auto IsPointer = [](const ValueDecl *Decl) {
      const auto *Variable = dyn_cast_or_null<VarDecl>(Decl);
      return Variable &&
             (Variable->hasLocalStorage() || Variable->isStaticLocal()) &&
             Variable->getType()->isPointerType();
    };
    // Whether a use of \p Pointer can point at what is read: a use after it
    // was pointed elsewhere on every path, `pValue = &_Other; *pValue = 0`,
    // doesn't.
    // Whether \p Ref only reads what it points to: through `*`, `->` or a
    // subscript, as a value.
    auto OnlyReadThrough = [&](const DeclRefExpr *Ref) {
      DynTypedNode Node = DynTypedNode::create(*Ref);
      bool Through = false;
      for (int Depth = 0; Depth < 8; ++Depth) {
        const auto Parents = instantiatedParents(Node, Context);
        if (Parents.empty())
          return false;
        const DynTypedNode &Parent = Parents[0];
        const auto *Child = Node.get<Expr>();
        // Read as a value: converted, copied into a variable that isn't a
        // reference, or an operand of an operator that only reads it, which a
        // parent map that skips implicit nodes shows.
        if (Through) {
          if (const auto *Variable = Parent.get<VarDecl>())
            return !Variable->getType()->isReferenceType();
          if (const auto *Binary = Parent.get<BinaryOperator>())
            return !Binary->isAssignmentOp() || Binary->getRHS() == Child;
        }
        if (const auto *Cast = Parent.get<ImplicitCastExpr>()) {
          // A pointer read through it, `*ppBits`, can in turn be written
          // through, `**ppBits = 0`.
          if (Through && Cast->getCastKind() == CK_LValueToRValue) {
            if (!Cast->getType()->isPointerType())
              return true;
            Through = false;
          }
        } else if (const auto *Unary = Parent.get<UnaryOperator>();
                   Unary && Unary->getOpcode() == UO_Deref)
          Through = true;
        else if (const auto *Member = Parent.get<MemberExpr>();
                 Member && Member->isArrow())
          Through = true;
        else if (Parent.get<ArraySubscriptExpr>())
          Through = true;
        else if (!Parent.get<ParenExpr>() &&
                 !(Through && Parent.get<MemberExpr>()))
          return false;
        Node = Parent;
      }
      return false;
    };
    auto UsedOnRead = [&](const VarDecl *Pointer) {
      bool Used = false;
      std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
        if (!S || Used)
          return;
        if (const auto *Assignment = dyn_cast<BinaryOperator>(S);
            Assignment && Assignment->getOpcode() == BO_Assign)
          if (const auto *Target = dyn_cast<DeclRefExpr>(
                  Assignment->getLHS()->IgnoreParenImpCasts());
              Target && Target->getDecl() == Pointer) {
            Visit(Assignment->getRHS());
            return;
          }
        // Only a use that can write through it, not one that only reads what
        // it points to, `Copy = *pValue`.
        if (const auto *Ref = dyn_cast<DeclRefExpr>(S);
            Ref && Ref->getDecl() == Pointer && !OnlyReadThrough(Ref)) {
          // In a lambda that captures it, `[&pBits] { *pBits = 0; }`, what
          // it holds where the lambda is called: any value it is given.
          if (Ref->refersToEnclosingVariableOrCapture()) {
            llvm::SmallVector<const Expr *, 4> Values{Pointer->getInit()};
            std::function<void(const Stmt *)> Assigned = [&](const Stmt *In) {
              if (!In)
                return;
              if (const auto *Assignment = dyn_cast<BinaryOperator>(In);
                  Assignment && Assignment->getOpcode() == BO_Assign)
                if (const auto *Target = dyn_cast<DeclRefExpr>(
                        Assignment->getLHS()->IgnoreParenImpCasts());
                    Target && Target->getDecl() == Pointer)
                  Values.push_back(Assignment->getRHS());
              for (const Stmt *Child : childrenOnce(In))
                Assigned(Child);
            };
            Assigned(Body);
            Used = llvm::any_of(Values, [&](const Expr *Value) {
              return !Value || PointsToRead(Value);
            });
          } else {
            const Expr *Value = assignedValueAt(Pointer, Ref);
            Used = !Value || PointsToRead(Value);
          }
        }
        for (const Stmt *Child : childrenOnce(S))
          Visit(Child);
      };
      Visit(Body);
      return Used;
    };
    // Also of a pointer member of a local, `Current.m_pWord` by its key \p Key:
    // not a use after an assignment in its block that points it elsewhere,
    // `Current.m_pWord = &Other;`, with nothing else of the local between
    // them. One of an element, `Pointers[0]`, is taken as it is.
    auto MemberUsedOnRead = [&](StringRef Key) {
      if (Key.contains('['))
        return true;
      const StringRef Root = Key.take_until([](char C) { return C == '.'; });
      std::function<bool(const Stmt *)> MentionsRoot = [&](const Stmt *S) {
        if (!S)
          return false;
        if (const auto *Ref = dyn_cast<DeclRefExpr>(S);
            Ref && ReferenceCollector::declKey(Ref->getDecl()) == Root)
          return true;
        return llvm::any_of(childrenOnce(S), MentionsRoot);
      };
      auto AssignsMember = [&](const Stmt *S) -> const BinaryOperator * {
        const auto *E = dyn_cast_or_null<Expr>(S);
        const auto *Assign =
            E ? dyn_cast<BinaryOperator>(E->IgnoreParenImpCasts()) : nullptr;
        const auto *Target =
            Assign && Assign->getOpcode() == BO_Assign
                ? dyn_cast<MemberExpr>(Assign->getLHS()->IgnoreParenImpCasts())
                : nullptr;
        return Target && ReferenceCollector::valueKey(Target) == Key ? Assign
                                                                     : nullptr;
      };
      // Whether the assignment before \p Use in its block points it at what
      // is read; none when that isn't known.
      auto PointedBefore = [&](const MemberExpr *Use) -> std::optional<bool> {
        DynTypedNode Node = DynTypedNode::create(*Use);
        for (int Depth = 0; Depth < 64; ++Depth) {
          const auto Parents = instantiatedParents(Node, Context);
          if (Parents.empty() || Parents[0].get<FunctionDecl>() ||
              Parents[0].get<LambdaExpr>())
            return std::nullopt;
          if (const auto *Block = Parents[0].get<CompoundStmt>()) {
            const Stmt *Statement = Node.get<Stmt>();
            const auto *It = llvm::find(Block->body(), Statement);
            while (It != Block->body_begin()) {
              --It;
              if (const BinaryOperator *Assign = AssignsMember(*It))
                return PointsToRead(Assign->getRHS());
              if (MentionsRoot(*It))
                return std::nullopt;
            }
            return std::nullopt;
          }
          Node = Parents[0];
        }
        return std::nullopt;
      };
      bool Used = false;
      std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
        if (!S || Used)
          return;
        if (const BinaryOperator *Assign = AssignsMember(S)) {
          Visit(Assign->getRHS());
          return;
        }
        if (const auto *Member = dyn_cast<MemberExpr>(S);
            Member && ReferenceCollector::valueKey(Member) == Key) {
          const std::optional<bool> Points = PointedBefore(Member);
          Used = !Points || *Points;
          return;
        }
        for (const Stmt *Child : childrenOnce(S))
          Visit(Child);
      };
      Visit(Body);
      return Used;
    };
    bool Added = Body != nullptr;
    for (int Pass = 0; Added && Pass < 64; ++Pass) {
      Added = false;
      std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
        if (!S)
          return;
        // Each of a declaration, `unsigned *pFirst = &_Value, *pSecond =
        // &_Value`.
        if (const auto *Declaration = dyn_cast<DeclStmt>(S))
          for (const Decl *Declared : Declaration->decls())
            if (const auto *Variable = dyn_cast<VarDecl>(Declared);
                IsPointer(Variable) && Variable->getInit() &&
                PointsToRead(Variable->getInit()) && UsedOnRead(Variable))
              Added |= AddAlias(Variable, Variable->getInit());
        // The reference of a loop over a container, `for (auto &Word :
        // _Words)`, is one of its elements, which a write of the container,
        // `_Words[0] = 0`, can change.
        if (const auto *Loop = dyn_cast<CXXForRangeStmt>(S);
            Loop && Loop->getLoopVariable() &&
            Loop->getLoopVariable()->getType()->isReferenceType() &&
            !Loop->getLoopVariable()
                 ->getType()
                 .getNonReferenceType()
                 .isConstQualified() &&
            Keys.Read.contains(
                ReferenceCollector::declKey(Loop->getLoopVariable())))
          if (const std::string Container =
                  ReferenceCollector::valueKey(Loop->getRangeInit());
              !Container.empty())
            Added |= Keys.Read.insert(Container).second;
        // An iterator a container gives at its first element, `auto iValue =
        // _Values.f_GetIterator()`, through which `*iValue = 0` writes it.
        if (const auto *Declaration = dyn_cast<DeclStmt>(S))
          for (const Decl *Declared : Declaration->decls()) {
            const auto *Iterator = dyn_cast<VarDecl>(Declared);
            const CXXRecordDecl *Record =
                Iterator && Iterator->hasLocalStorage() &&
                        Iterator->getInit() &&
                        !Iterator->getType()->isReferenceType()
                    ? Iterator->getType()->getAsCXXRecordDecl()
                    : nullptr;
            // One with `*` or `->`, or that converts to a pointer, `operator
            // t_CData *()` of a `TCVectorIterator`.
            auto Has = [&](OverloadedOperatorKind Kind) {
              return !Record->getDefinition()
                          ->lookup(
                              Context.DeclarationNames.getCXXOperatorName(Kind))
                          .empty();
            };
            if (!Record || !Record->hasDefinition() ||
                !(Has(OO_Star) || Has(OO_Arrow) ||
                  llvm::any_of(
                      Record->getDefinition()->getVisibleConversionFunctions(),
                      [](const NamedDecl *Conversion) {
                        const auto *Function = dyn_cast<CXXConversionDecl>(
                            Conversion->getUnderlyingDecl());
                        return Function &&
                               Function->getConversionType()->isPointerType();
                      })))
              continue;
            const Expr *Init = Iterator->getInit()->IgnoreImplicit();
            if (const auto *Copy = dyn_cast<CXXConstructExpr>(Init);
                Copy && Copy->getNumArgs() == 1 &&
                Copy->getConstructor()->isCopyOrMoveConstructor())
              Init = Copy->getArg(0)->IgnoreImplicit();
            const auto *Begin = dyn_cast<CXXMemberCallExpr>(Init);
            const CXXMethodDecl *Method =
                Begin ? Begin->getMethodDecl() : nullptr;
            // Also a reverse one, `f_GetIteratorReverse()`, which is at any
            // element as far as is told here.
            const bool Reverse =
                Method && Method->getIdentifier() &&
                llvm::is_contained(
                    {StringRef("f_GetIteratorReverse"), StringRef("rbegin")},
                    Method->getName());
            if (!Method || Begin->getNumArgs() != 0 ||
                !Method->getIdentifier() ||
                (!Reverse && !llvm::is_contained({StringRef("f_GetIterator"),
                                                  StringRef("begin")},
                                                 Method->getName())) ||
                !Begin->getImplicitObjectArgument())
              continue;
            const std::string Container = ReferenceCollector::valueKey(
                Begin->getImplicitObjectArgument());
            if (Container.empty())
              continue;
            // At the element it is advanced to where it is used, `++iValue`
            // before `*iValue = 0`, or at any when that isn't known.
            const std::optional<uint64_t> Position =
                Reverse ? std::nullopt
                        : iteratorPosition(Iterator, Declaration, Context);
            // And what is read through the iterator is the container's element
            // there, which `_Values[0] = 0` writes: of the pointer member its
            // index operator gives an element of, `return m_pData[_Index];`,
            // or else, or where the iterator is isn't known, any of it.
            std::string ElementMember;
            if (const CXXRecordDecl *Of = Begin->getImplicitObjectArgument()
                                              ->getType()
                                              .getNonReferenceType()
                                              ->getAsCXXRecordDecl();
                Of && Of->hasDefinition())
              for (const NamedDecl *Found : Of->getDefinition()->lookup(
                       Context.DeclarationNames.getCXXOperatorName(
                           OO_Subscript)))
                if (const auto *Index = dyn_cast<CXXMethodDecl>(Found))
                  if (const FunctionDecl *Definition = Index->getDefinition();
                      Definition && Definition->getNumParams() == 1)
                    if (const auto *Body = dyn_cast_or_null<CompoundStmt>(
                            Definition->getBody());
                        Body && Body->size() == 1)
                      if (const auto *Return =
                              dyn_cast<ReturnStmt>(Body->body_front());
                          Return && Return->getRetValue())
                        if (const auto *Element = dyn_cast<ArraySubscriptExpr>(
                                Return->getRetValue()->IgnoreParenImpCasts()))
                          if (const auto *Member = dyn_cast<MemberExpr>(
                                  Element->getBase()->IgnoreParenImpCasts());
                              Member &&
                              isa<FieldDecl>(Member->getMemberDecl()) &&
                              isa<CXXThisExpr>(
                                  Member->getBase()->IgnoreParenImpCasts()))
                            ElementMember = ReferenceCollector::declKey(
                                Member->getMemberDecl());
            llvm::SmallVector<std::string, 2> Elements;
            for (const auto &Entry : Keys.Read) {
              const StringRef Read = Entry.getKey();
              if (!Read.starts_with(Container))
                continue;
              // Past the members that hold the elements, `.m_pData` of
              // `_Values[0]`.
              const StringRef Rest = Read.drop_front(Container.size());
              const size_t Element = Rest.find('[');
              if (Element == StringRef::npos ||
                  (Element && !Rest.starts_with(".")))
                continue;
              StringRef Index = Rest.substr(Element + 1);
              const size_t End = Index.find(']');
              uint64_t At = 0;
              // Not by where it is of a container whose index is no position,
              // like the key of a map, `_Bits[7]`, which any can be, also one
              // not known here, `Masks[_Key]`, but not an element known to be
              // at another position than the iterator.
              if (End == StringRef::npos || End == 0)
                continue;
              const bool Known = !Index.take_front(End).getAsInteger(10, At);
              if (Known && Position && !ElementMember.empty() &&
                  At != *Position)
                continue;
              Elements.push_back(ReferenceCollector::declKey(Iterator) + "[0]" +
                                 Index.drop_front(End + 1).str());
            }
            const std::string Through =
                ReferenceCollector::declKey(Iterator) + "[0]";
            for (const auto &Entry : Keys.Read) {
              const StringRef Read = Entry.getKey();
              if (!Read.starts_with(Through))
                continue;
              const StringRef Rest = Read.drop_front(Through.size());
              if (!Rest.empty() && Rest[0] != '.' && Rest[0] != '[')
                continue;
              Elements.push_back(Position && !ElementMember.empty()
                                     ? Container + "." + ElementMember + "[" +
                                           std::to_string(*Position) + "]" +
                                           Rest.str()
                                     : Container);
            }
            for (const std::string &Element : Elements)
              Added |= Keys.Read.insert(Element).second;
          }
        // Also a const pointer at namespace scope the body uses, `unsigned
        // *const gc_pBits = &g_Bits`, which can't be pointed elsewhere.
        if (const auto *Ref = dyn_cast<DeclRefExpr>(S))
          if (const auto *Global = dyn_cast<VarDecl>(Ref->getDecl());
              Global && Global->hasGlobalStorage() &&
              !Global->isStaticLocal() && Global->getType()->isPointerType() &&
              Global->getType().isConstQualified() && Global->getInit() &&
              PointsToRead(Global->getInit()))
            Added |= AddAlias(Global, Global->getInit());
        // A member of a local aggregate that refers or points to what is
        // read, `CBorrowed Borrowed{_Value}` with `unsigned &m_Bits`, through
        // which `Borrowed.m_Bits = 0` writes it, also initialized with
        // parentheses, `CBorrowed Borrowed(_Value)`.
        // Also of its bases, `CDerived Borrowed{{_Value}}`, and of an
        // aggregate member, `COuter Outer{{_Value}}` with `Outer.m_Inner
        // .m_Bits`.
        // A copy of one, `CBorrowed Copy = Borrowed`, or one assigned it,
        // `Copy = Borrowed`, has the same members.
        // As a copy of what \p FromKey names, with \p SharesPointee when that
        // is a smart pointer.
        auto CopyMembersOf = [&](const std::string &Prefix,
                                 const std::string &FromKey, bool SharesPointee,
                                 const CXXRecordDecl *Record = nullptr) {
          // Not what an element of \p Record, a container, that isn't a
          // pointer holds, `Copy[0][0]` of a copy of a vector of vectors,
          // which copies them.
          bool ElementsShare = true;
          if (const CXXRecordDecl *Definition =
                  Record ? Record->getDefinition() : nullptr)
            for (const NamedDecl *Found : Definition->lookup(
                     Definition->getASTContext()
                         .DeclarationNames.getCXXOperatorName(OO_Subscript)))
              // Also a template, `operator[](tf_CKey &&)` of a map.
              if (const auto *Index = dyn_cast<CXXMethodDecl>(
                      isa<FunctionTemplateDecl>(Found->getUnderlyingDecl())
                          ? cast<FunctionTemplateDecl>(
                                Found->getUnderlyingDecl())
                                ->getTemplatedDecl()
                          : Found->getUnderlyingDecl())) {
                const QualType Element =
                    Index->getReturnType().getNonReferenceType();
                ElementsShare = Element->isPointerType() ||
                                (!Element->isDependentType() &&
                                 sharesPointee(Element->getAsCXXRecordDecl()));
                break;
              }
          // Also what an element that is a pointer points to, `Copy[0]->
          // m_Bits` of a copy of a vector of pointers, which copies the
          // pointers.
          auto ThroughElement = [&](StringRef Path) {
            if (!Path.starts_with("["))
              return false;
            const size_t End = Path.find(']');
            if (End == StringRef::npos)
              return false;
            const StringRef Rest = Path.substr(End + 1);
            return (Rest.starts_with("[") && ElementsShare) ||
                   (Rest.starts_with(".") && throughIndirection(Rest));
          };
          llvm::SmallVector<std::string, 4> Copied;
          for (const auto &Entry : Keys.Read) {
            const StringRef Key = Entry.getKey();
            if (!Key.starts_with(FromKey))
              continue;
            const StringRef Path = Key.substr(FromKey.size());
            if ((Path.starts_with(".") && throughIndirection(Path)) ||
                (SharesPointee && Path.starts_with("[")) ||
                ThroughElement(Path))
              Copied.push_back(Prefix + Path.str());
          }
          // And what is read through the copy is read through what it was
          // made from, `_pInfo->m_Bits` of `pCopy->m_Bits`, which writes
          // through either change.
          for (const auto &Entry : Keys.Read) {
            const StringRef Key = Entry.getKey();
            if (!Key.starts_with(Prefix))
              continue;
            const StringRef Path = Key.substr(Prefix.size());
            if ((Path.starts_with(".") && throughIndirection(Path)) ||
                (SharesPointee && Path.starts_with("[")) ||
                ThroughElement(Path))
              Copied.push_back(FromKey + Path.str());
          }
          for (const std::string &Key : Copied)
            Added |= Keys.Read.insert(Key).second;
        };
        auto CopyMembers = [&](const std::string &Prefix, const Expr *From) {
          // Also moved from, `auto pMoved = fg_Move(pCopy)`, which then
          // shares what it did.
          for (int Level = 0; From && Level < 4; ++Level) {
            const auto *Moved = dyn_cast<CallExpr>(From->IgnoreParenImpCasts());
            const FunctionDecl *Callee =
                Moved ? Moved->getDirectCallee() : nullptr;
            if (!Callee || !Callee->getIdentifier() ||
                Moved->getNumArgs() != 1 ||
                !isMoveOrForwardFunction(Callee->getName()))
              break;
            From = Moved->getArg(0);
          }
          const auto *Ref =
              From ? dyn_cast<DeclRefExpr>(From->IgnoreParenImpCasts())
                   : nullptr;
          // Also of a member, `auto iBits = m_iBits`, by its path.
          if (const auto *Member =
                  From && !Ref
                      ? dyn_cast<MemberExpr>(From->IgnoreParenImpCasts())
                      : nullptr;
              Member && isa<FieldDecl>(Member->getMemberDecl())) {
            const std::string FromKey = ReferenceCollector::objectKey(Member);
            if (FromKey.empty())
              return false;
            const CXXRecordDecl *Copied =
                Member->getType().getNonReferenceType()->getAsCXXRecordDecl();
            CopyMembersOf(Prefix, FromKey, sharesPointee(Copied), Copied);
            return true;
          }
          if (!Ref)
            return false;
          // Of what a local reference is bound to, `pAlias` of `auto const
          // &pAlias = _pBits`.
          for (int Depth = 0; Depth < 8; ++Depth) {
            const Expr *Bound =
                ReferenceCollector::aliasedValue(Ref->getDecl());
            const auto *Of =
                Bound ? dyn_cast<DeclRefExpr>(Bound->IgnoreParenImpCasts())
                      : nullptr;
            if (!Of || Of == Ref)
              break;
            Ref = Of;
          }
          // Only what the copy shares with it, through a reference member or
          // what a pointer member points to: a member of its own, `Copy.m_Bits`
          // of `CInfo Copy = _Info`, is other storage. Also what a smart
          // pointer points to, `pCopy->m_Bits` of `auto pCopy = _pInfo`.
          const CXXRecordDecl *Copied =
              Ref->getType().getNonReferenceType()->getAsCXXRecordDecl();
          CopyMembersOf(Prefix, ReferenceCollector::declKey(Ref->getDecl()),
                        sharesPointee(Copied), Copied);
          return true;
        };
        std::function<void(const std::string &, const Expr *, int)> Aggregate =
            [&](const std::string &Prefix, const Expr *Initializer,
                int Depth) {
              if (!Initializer || Depth > 8)
                return;
              // Also written as what it makes, `CBorrowed Borrowed =
              // CBorrowed{_Value}`, or `auto [Bits] = CBorrowed{_Value}`.
              Initializer = Initializer->IgnoreImplicit();
              for (int Level = 0; Level < 8; ++Level)
                if (const auto *Cast =
                        dyn_cast<CXXFunctionalCastExpr>(Initializer);
                    Cast && (Cast->getCastKind() == CK_NoOp ||
                             Cast->getCastKind() == CK_ConstructorConversion))
                  Initializer = Cast->getSubExpr()->IgnoreImplicit();
                else
                  break;
              // Also converted from another specialization of its template,
              // `TCSharedPointer<CBase> pBase = _pState` of a
              // `TCSharedPointer<CDerived>`, which copies like it.
              auto Converts = [](const CXXConstructExpr *Copy) {
                const auto *To = dyn_cast<ClassTemplateSpecializationDecl>(
                    Copy->getConstructor()->getParent());
                const auto *From =
                    dyn_cast_or_null<ClassTemplateSpecializationDecl>(
                        Copy->getArg(0)
                            ->getType()
                            .getNonReferenceType()
                            ->getAsCXXRecordDecl());
                return To && From &&
                       To->getSpecializedTemplate()->getCanonicalDecl() ==
                           From->getSpecializedTemplate()->getCanonicalDecl();
              };
              if (const auto *Copy = dyn_cast<CXXConstructExpr>(Initializer);
                  Copy && Copy->getNumArgs() >= 1 &&
                  llvm::all_of(llvm::drop_begin(Copy->arguments()),
                               [](const Expr *Argument) {
                                 return isa<CXXDefaultArgExpr>(Argument);
                               }) &&
                  (Copy->getConstructor()->isCopyOrMoveConstructor() ||
                   Converts(Copy)) &&
                  CopyMembers(Prefix, Copy->getArg(0)))
                return;
              // Made by a constructor whose body can be seen, that binds a
              // reference member to what it is given, `CBorrowed(unsigned
              // &_Bits) : m_Bits(_Bits) {}`, or points a pointer member at it.
              // Or by one that can't be seen to, like that of an optional,
              // `TCOptional<TCReference<umint>> Bits{TCReference<umint>(
              // _Bits)}`, which constructs what it holds in its storage of
              // what it is given: what holds what is read, a pointer to it, or
              // the reference a parameter binds to it, which the object then
              // holds, through which writes can change it. Where: in the
              // members its constructor makes of what it is given,
              // `m_fCleanup(_fCleanup)`, also those of the bases it gives it
              // to in turn, `CVariant(fg_Move(_Value))`, or, when it keeps it
              // otherwise, like in storage it constructs it in, anywhere in it.
              // What an object that gives access to what it holds, through
              // `operator->` or `operator*`, holds is reached through them,
              // `Bits->f_Get()`, which its other members aren't, like the flag
              // of a guard that a move clears.
              bool Accessed = false;
              const std::string Anywhere = Prefix;
              std::function<void(const CXXConstructorDecl *, int)> Places =
                  [&](const CXXConstructorDecl *Called, int Depth) {
                    const auto *Definition =
                        Called ? dyn_cast_or_null<CXXConstructorDecl>(
                                     Called->getDefinition())
                               : nullptr;
                    if (!Definition || Depth > 8) {
                      if (!Accessed)
                        Added |= Keys.Read.insert(Anywhere).second;
                      return;
                    }
                    std::function<bool(const Stmt *)> NamesParameter =
                        [&](const Stmt *S) {
                          if (const auto *Ref =
                                  dyn_cast_or_null<DeclRefExpr>(S);
                              Ref && isa<ParmVarDecl>(Ref->getDecl()) &&
                              Ref->getDecl()->getDeclContext() == Definition)
                            return true;
                          return S &&
                                 llvm::any_of(childrenOnce(S), NamesParameter);
                        };
                    bool Placed = false;
                    for (const CXXCtorInitializer *Initializer :
                         Definition->inits()) {
                      if (!Initializer->isWritten() ||
                          !NamesParameter(Initializer->getInit()))
                        continue;
                      Placed = true;
                      // Not a callable one, like the lambda of a guard,
                      // `m_fCleanup(_fCleanup)`, whose calls tell what they
                      // change.
                      if (const FieldDecl *Field = Initializer->getMember()) {
                        const CXXRecordDecl *Held =
                            Field->getType()->getAsCXXRecordDecl();
                        if (!(Held && Held->hasDefinition() &&
                              (Held->isLambda() ||
                               llvm::any_of(
                                   Held->getDefinition()->methods(),
                                   [](const CXXMethodDecl *Method) {
                                     return Method->getOverloadedOperator() ==
                                            OO_Call;
                                   }))))
                          Added |=
                              Keys.Read
                                  .insert(Prefix + "." +
                                          ReferenceCollector::declKey(Field))
                                  .second;
                      } else if (const auto *Base = dyn_cast<CXXConstructExpr>(
                                     Initializer->getInit()->IgnoreImplicit()))
                        Places(Base->getConstructor(), Depth + 1);
                      else if (!Accessed)
                        Added |= Keys.Read.insert(Anywhere).second;
                    }
                    if (!Placed && !Accessed)
                      Added |= Keys.Read.insert(Anywhere).second;
                  };
              auto Holds = [&](const CXXConstructExpr *Construct) {
                for (unsigned iArgument = 0;
                     iArgument < Construct->getNumArgs(); ++iArgument) {
                  const Expr *Argument = Construct->getArg(iArgument);
                  if (isa<CXXDefaultArgExpr>(Argument) ||
                      !touchesAny(Argument, Keys.Read) || isOwnCopy(Construct))
                    continue;
                  const QualType Given = Argument->getType();
                  const CXXConstructorDecl *Called =
                      Construct->getConstructor();
                  const QualType Parameter =
                      iArgument < Called->getNumParams()
                          ? Called->getParamDecl(iArgument)->getType()
                          : QualType();
                  const CXXRecordDecl *Record = Given->getAsCXXRecordDecl();
                  if (Given->isPointerType() ||
                      (!Parameter.isNull() && Parameter->isReferenceType() &&
                       !Parameter.getNonReferenceType().isConstQualified() &&
                       !Record) ||
                      (Record && Record->hasDefinition() &&
                       holdsIndirection(Record))) {
                    const CXXRecordDecl *Holder =
                        Construct->getType()->getAsCXXRecordDecl();
                    // Not a function wrapper, `TCFunction<umint ()> fRead =
                    // [&] { return _Bits; }`, whose calls tell what they
                    // change.
                    // Also through a base, which can have it, each once: a
                    // virtual one is a base through each path to it.
                    llvm::SmallPtrSet<const CXXRecordDecl *, 8> Searched;
                    std::function<bool(const CXXRecordDecl *, int)> Callable =
                        [&](const CXXRecordDecl *Record, int Depth) {
                          if (!Record || !Record->hasDefinition() || Depth > 8)
                            return false;
                          Record = Record->getDefinition();
                          if (!Searched.insert(Record).second)
                            return false;
                          if (llvm::any_of(
                                  Record->methods(),
                                  [](const CXXMethodDecl *Method) {
                                    return Method->getOverloadedOperator() ==
                                           OO_Call;
                                  }) ||
                              llvm::any_of(Record->decls(), [](const Decl *D) {
                                const auto *Template =
                                    dyn_cast<FunctionTemplateDecl>(D);
                                return Template &&
                                       Template->getTemplatedDecl()
                                               ->getOverloadedOperator() ==
                                           OO_Call;
                              }))
                            return true;
                          return llvm::any_of(
                              Record->bases(),
                              [&](const CXXBaseSpecifier &Base) {
                                return Callable(
                                    Base.getType()->getAsCXXRecordDecl(),
                                    Depth + 1);
                              });
                        };
                    if (Callable(Holder, 0))
                      return;
                    Accessed = false;
                    for (const CXXRecordDecl *Class = Holder;
                         Class && Class->hasDefinition() && !Accessed;) {
                      for (const CXXMethodDecl *Method :
                           Class->getDefinition()->methods())
                        Accessed |=
                            Method->getOverloadedOperator() == OO_Arrow ||
                            (Method->getOverloadedOperator() == OO_Star &&
                             Method->getNumParams() == 0);
                      Class = Class->getDefinition()->getNumBases() == 1
                                  ? Class->getDefinition()
                                        ->bases_begin()
                                        ->getType()
                                        ->getAsCXXRecordDecl()
                                  : nullptr;
                    }
                    if (Accessed)
                      Added |= Keys.Read.insert(Prefix + "[0]").second;
                    Places(Called, 0);
                    return;
                  }
                }
              };
              if (const auto *Construct =
                      dyn_cast<CXXConstructExpr>(Initializer);
                  Construct &&
                  !Construct->getConstructor()->isCopyOrMoveConstructor() &&
                  !Construct->getConstructor()->getDefinition()) {
                Holds(Construct);
                return;
              }
              if (const auto *Construct = dyn_cast<CXXConstructExpr>(Initializer);
                  Construct &&
                  !Construct->getConstructor()->isCopyOrMoveConstructor())
                if (const auto *Constructor = dyn_cast_or_null<CXXConstructorDecl>(
                        Construct->getConstructor()->getDefinition())) {
                  const size_t Before = Keys.Read.size();
                  auto AddMember = [&](const std::string &Key,
                                       const FieldDecl *Field,
                                       const Expr *Init) {
                    const auto *Address =
                        Init ? dyn_cast<UnaryOperator>(Init) : nullptr;
                    const bool Addressed =
                        Address && Address->getOpcode() == UO_AddrOf;
                    if (Addressed)
                      Init = Address->getSubExpr()->IgnoreParenImpCasts();
                    const auto *Given =
                        Init ? dyn_cast<DeclRefExpr>(Init) : nullptr;
                    const auto *Parameter =
                        Given ? dyn_cast<ParmVarDecl>(Given->getDecl()) : nullptr;
                    if (!Parameter ||
                        Parameter->getFunctionScopeIndex() >=
                            Construct->getNumArgs())
                      return;
                    const Expr *Argument =
                        Construct->getArg(Parameter->getFunctionScopeIndex());
                    if (Addressed) {
                      if (Field->getType()->isPointerType() &&
                          Parameter->getType()->isReferenceType() &&
                          touchesAny(Argument, Keys.Read))
                        Added |= Keys.Read.insert(Key + "[0]").second;
                      return;
                    }
                    if (Field->getType()->isReferenceType() &&
                        Parameter->getType()->isReferenceType() &&
                        touchesAny(Argument, Keys.Read))
                      Added |= Keys.Read.insert(Key).second;
                    else if (Field->getType()->isPointerType())
                      for (const std::string &Suffix : ReadThrough(Argument))
                        Added |= Keys.Read.insert(Key + Suffix).second;
                  };
                  for (const CXXCtorInitializer *Member : Constructor->inits())
                    if (const FieldDecl *Field = Member->getMember();
                        Field && Member->getInit())
                      AddMember(
                          Prefix + "." + ReferenceCollector::declKey(Field),
                          Field, Member->getInit()->IgnoreParenImpCasts());
                  // Also by an assignment in its body to a member, also of a
                  // struct member, `m_Data.m_pPointTo = &_Value;` of
                  // `TCReference`, in a statement of its own.
                  if (const auto *Body = dyn_cast_or_null<CompoundStmt>(
                          Constructor->getBody()))
                    for (const Stmt *Statement : Body->body()) {
                      const auto *Assign = dyn_cast<BinaryOperator>(Statement);
                      if (!Assign || Assign->getOpcode() != BO_Assign)
                        continue;
                      const auto *Target = dyn_cast<MemberExpr>(
                          Assign->getLHS()->IgnoreParenImpCasts());
                      const auto *Field =
                          Target ? dyn_cast<FieldDecl>(Target->getMemberDecl())
                                 : nullptr;
                      if (!Field)
                        continue;
                      std::string Path;
                      const Expr *Base = Target;
                      while (const auto *Member = dyn_cast<MemberExpr>(Base)) {
                        const auto *Part =
                            dyn_cast<FieldDecl>(Member->getMemberDecl());
                        if (!Part ||
                            (Member->isArrow() &&
                             !isa<CXXThisExpr>(
                                 Member->getBase()->IgnoreParenImpCasts())))
                          break;
                        Path = "." + ReferenceCollector::declKey(Part) + Path;
                        Base = Member->getBase()->IgnoreParenImpCasts();
                        if (Member->isArrow())
                          break;
                      }
                      if (isa<CXXThisExpr>(Base))
                        AddMember(Prefix + Path, Field,
                                  Assign->getRHS()->IgnoreParenImpCasts());
                    }
                  if (Keys.Read.size() == Before)
                    Holds(Construct);
                  return;
                }
              // A smart pointer a member function of an object gives without
              // arguments, `_pWeak.f_Lock()`, points to what each call of it
              // on the object gives: each is like a copy of one that object
              // has.
              if (const auto *Getter = dyn_cast<CXXMemberCallExpr>(Initializer);
                  Getter && Getter->getNumArgs() == 0 &&
                  Getter->getMethodDecl() &&
                  Getter->getImplicitObjectArgument() &&
                  sharesPointee(Getter->getType()->getAsCXXRecordDecl())) {
                // Also of a local copy of the object, `pWeakCopy.f_Lock()` of
                // `auto pWeakCopy = _pWeak`, which gives what it gives, as it
                // holds where it is called.
                // And of one made from a smart pointer of the type it gives,
                // `pWeak.f_Lock()` of `TCWeakPointer<CInfo> pWeak(_pInfo)`,
                // which gives a copy of that one.
                bool CopyOfMadeFrom = false;
                // A smart pointer of the type it gives, also of the same
                // template to the same type with other options, like the
                // `TCSharedPointer<CState, CSupportWeakTag, CAllocator>` the
                // weak pointer of a `TCSharedPointer<CState, CSupportWeakTag>`
                // gives.
                auto OfGiven = [&](QualType Type) {
                  Type = Type.getNonReferenceType();
                  if (Context.hasSameUnqualifiedType(Type, Getter->getType()))
                    return true;
                  const auto *Pointer =
                      dyn_cast_or_null<ClassTemplateSpecializationDecl>(
                          Type->getAsCXXRecordDecl());
                  const auto *Given =
                      dyn_cast_or_null<ClassTemplateSpecializationDecl>(
                          Getter->getType()->getAsCXXRecordDecl());
                  return Pointer && Given &&
                         Pointer->getSpecializedTemplate()->getCanonicalDecl() ==
                             Given->getSpecializedTemplate()
                                 ->getCanonicalDecl() &&
                         Pointer->getTemplateArgs().size() > 0 &&
                         Given->getTemplateArgs().size() > 0 &&
                         Pointer->getTemplateArgs()[0].getKind() ==
                             TemplateArgument::Type &&
                         Given->getTemplateArgs()[0].getKind() ==
                             TemplateArgument::Type &&
                         Context.hasSameType(
                             Pointer->getTemplateArgs()[0].getAsType(),
                             Given->getTemplateArgs()[0].getAsType());
                };
                if (const std::string Object =
                        [&] {
                          const Expr *Of = Getter->getImplicitObjectArgument()
                                               ->IgnoreParenImpCasts();
                          for (int Level = 0; Level < 8; ++Level) {
                            const auto *Ref = dyn_cast<DeclRefExpr>(Of);
                            const auto *Local =
                                Ref ? dyn_cast<VarDecl>(Ref->getDecl())
                                    : nullptr;
                            if (!Local || !Local->hasLocalStorage() ||
                                isa<ParmVarDecl>(Local) ||
                                Local->getType()->isReferenceType())
                              break;
                            const Expr *Value = assignedValueAt(Local, Ref);
                            if (!Value)
                              break;
                            Value =
                                Value->IgnoreImplicit()->IgnoreParenImpCasts();
                            if (const auto *Copy =
                                    dyn_cast<CXXConstructExpr>(Value);
                                Copy && Copy->getNumArgs() == 1 &&
                                Copy->getConstructor()
                                    ->isCopyOrMoveConstructor())
                              Value = Copy->getArg(0)->IgnoreParenImpCasts();
                            else if (const auto *Made =
                                         dyn_cast<CXXConstructExpr>(Value);
                                     Made && Made->getNumArgs() == 1 &&
                                     isa<DeclRefExpr>(
                                         Made->getArg(0)
                                             ->IgnoreParenImpCasts()) &&
                                     OfGiven(Made->getArg(0)->getType())) {
                              CopyOfMadeFrom = true;
                              return ReferenceCollector::valueKey(
                                  Made->getArg(0)->IgnoreParenImpCasts());
                            }
                            // Also one a smart pointer of that type gives
                            // without arguments, `_pState.f_Weak()`.
                            Value =
                                Value->IgnoreImplicit()->IgnoreParenImpCasts();
                            if (const auto *Given =
                                    dyn_cast<CXXMemberCallExpr>(Value);
                                Given && Given->getNumArgs() == 0 &&
                                Given->getImplicitObjectArgument() &&
                                isa<DeclRefExpr>(
                                    Given->getImplicitObjectArgument()
                                        ->IgnoreParenImpCasts()) &&
                                OfGiven(Given->getImplicitObjectArgument()
                                            ->getType())) {
                              CopyOfMadeFrom = true;
                              return ReferenceCollector::valueKey(
                                  Given->getImplicitObjectArgument()
                                      ->IgnoreParenImpCasts());
                            }
                            if (!isa<DeclRefExpr>(Value))
                              break;
                            Of = Value;
                          }
                          return ReferenceCollector::valueKey(Of);
                        }();
                    !Object.empty()) {
                  // Also one of the same template as the object, only of
                  // what is const, `_pState.f_ShareAsConst()`, which points
                  // to what the object does.
                  const auto *Gives =
                      dyn_cast_or_null<ClassTemplateSpecializationDecl>(
                          Getter->getType()->getAsCXXRecordDecl());
                  const auto *Of =
                      dyn_cast_or_null<ClassTemplateSpecializationDecl>(
                          Getter->getImplicitObjectArgument()
                              ->getType()
                              .getNonReferenceType()
                              ->getAsCXXRecordDecl());
                  const bool AsConst =
                      Gives && Of &&
                      Gives->getSpecializedTemplate()->getCanonicalDecl() ==
                          Of->getSpecializedTemplate()->getCanonicalDecl() &&
                      Gives->getTemplateArgs().size() >= 1 &&
                      Of->getTemplateArgs().size() >= 1 &&
                      Gives->getTemplateArgs()[0].getKind() ==
                          TemplateArgument::Type &&
                      Of->getTemplateArgs()[0].getKind() ==
                          TemplateArgument::Type &&
                      Context.hasSameUnqualifiedType(
                          Gives->getTemplateArgs()[0].getAsType(),
                          Of->getTemplateArgs()[0].getAsType());
                  CopyMembersOf(Prefix,
                                CopyOfMadeFrom || AsConst
                                    ? Object
                                    : Object + "@" +
                                          ReferenceCollector::declKey(
                                              Getter->getMethodDecl()),
                                true);
                  return;
                }
              }
              // Returned by a function given what is read, `CBorrowed
              // Borrowed = fg_Borrow(_Bits)`, whose members can refer or point
              // to it: each that can, as far as can be told.
              if (const auto *Call = dyn_cast<CallExpr>(Initializer);
                  Call && !isa<CXXOperatorCallExpr>(Call))
                if (const CXXRecordDecl *Returned =
                        Call->getType()->getAsCXXRecordDecl();
                    Returned && Returned->hasDefinition() &&
                    !Returned->isUnion()) {
                  const FunctionDecl *Callee = Call->getDirectCallee();
                  // A copy of what it is given, `fg_TempCopy(_pInfo)` of
                  // `return _Value;`, which shares with it what a copy does,
                  // when each of its returns gives the same parameter.
                  if (const FunctionDecl *Definition =
                          Callee ? Callee->getDefinition() : nullptr;
                      Definition && Definition->getBody()) {
                    const ParmVarDecl *Copied = nullptr;
                    bool OnlyCopies = true;
                    std::function<void(const Stmt *)> Returns = [&](const Stmt
                                                                        *S) {
                      if (!S || !OnlyCopies || isa<LambdaExpr>(S))
                        return;
                      if (const auto *Return = dyn_cast<ReturnStmt>(S)) {
                        const Expr *Value =
                            Return->getRetValue()
                                ? Return->getRetValue()->IgnoreImplicit()
                                : nullptr;
                        if (const auto *Copy =
                                dyn_cast_or_null<CXXConstructExpr>(Value);
                            Copy && Copy->getNumArgs() == 1 &&
                            Copy->getConstructor()->isCopyOrMoveConstructor())
                          Value = Copy->getArg(0)->IgnoreImplicit();
                        const auto *Ref = dyn_cast_or_null<DeclRefExpr>(Value);
                        const auto *Parameter =
                            Ref ? dyn_cast<ParmVarDecl>(Ref->getDecl())
                                : nullptr;
                        if (!Parameter || (Copied && Copied != Parameter))
                          OnlyCopies = false;
                        Copied = Parameter;
                        return;
                      }
                      for (const Stmt *Child : childrenOnce(S))
                        Returns(Child);
                    };
                    Returns(Definition->getBody());
                    if (OnlyCopies && Copied &&
                        Copied->getFunctionScopeIndex() < Call->getNumArgs() &&
                        CopyMembers(
                            Prefix,
                            Call->getArg(Copied->getFunctionScopeIndex())))
                      return;
                  }
                  // Also those of its members that are structs, which
                  // `TCReference` keeps its pointer in, `m_Data.m_pPointTo`.
                  llvm::SmallVector<std::pair<const FieldDecl *, std::string>,
                                    4>
                      Fields;
                  std::function<void(const CXXRecordDecl *, const std::string &,
                                     int)>
                      AddFields = [&](const CXXRecordDecl *Record,
                                      const std::string &Of, int Level) {
                        for (const FieldDecl *Field : Record->fields()) {
                          const std::string Key =
                              Of + "." + ReferenceCollector::declKey(Field);
                          Fields.push_back({Field, Key});
                          if (const CXXRecordDecl *Inner =
                                  Field->getType()->getAsCXXRecordDecl();
                              Inner && Level < 4 && Inner->hasDefinition() &&
                              !Inner->isUnion())
                            AddFields(Inner->getDefinition(), Key, Level + 1);
                        }
                      };
                  AddFields(Returned->getDefinition(), Prefix, 0);
                  for (unsigned iArgument = 0;
                       Callee && iArgument < Call->getNumArgs() &&
                       iArgument < Callee->getNumParams();
                       ++iArgument) {
                    const Expr *Argument = Call->getArg(iArgument);
                    const QualType Type =
                        Callee->getParamDecl(iArgument)->getType();
                    for (const auto &[Field, Key] : Fields) {
                      if (Field->getType()->isReferenceType() &&
                          Type->isReferenceType() &&
                          touchesAny(Argument, Keys.Read))
                        Added |= Keys.Read.insert(Key).second;
                      else if (Field->getType()->isPointerType() &&
                               Type->isPointerType())
                        for (const std::string &Suffix : ReadThrough(Argument))
                          Added |= Keys.Read.insert(Key + Suffix).second;
                      // Or a pointer to what it is given by reference,
                      // `return {&_Bits};`, whose `[0]` is that.
                      else if (Field->getType()->isPointerType() &&
                               Type->isReferenceType())
                        if (const std::string Given =
                                ReferenceCollector::valueKey(Argument);
                            !Given.empty()) {
                          llvm::SmallVector<std::string, 2> Through;
                          for (const auto &Entry : Keys.Read) {
                            const StringRef Read = Entry.getKey();
                            if (Read == Given ||
                                Read.starts_with(Given + ".") ||
                                Read.starts_with(Given + "["))
                              Through.push_back(
                                  Key + "[0]" +
                                  Read.drop_front(Given.size()).str());
                          }
                          for (const std::string &Alias : Through)
                            Added |= Keys.Read.insert(Alias).second;
                        }
                    }
                  }
                  return;
                }
              llvm::ArrayRef<Expr *> Inits;
              if (const auto *List = dyn_cast<InitListExpr>(Initializer))
                Inits = List->inits();
              else if (const auto *Parens =
                           dyn_cast<CXXParenListInitExpr>(Initializer))
                Inits = Parens->getInitExprs();
              // The elements of a local array, `unsigned *Pointers[] =
              // {&_Bits}`, through which `*Pointers[0] = 0` writes it, also
              // of pointers to const, through which a `const_cast` can write,
              // `*const_cast<unsigned *>(pPointers[0]) = 0`.
              if (const ArrayType *Array =
                      Initializer->getType()->getAsArrayTypeUnsafe();
                  Array && !Inits.empty()) {
                const QualType Element = Array->getElementType();
                for (unsigned iElement = 0; iElement < Inits.size();
                     ++iElement) {
                  const std::string Key =
                      Prefix + "[" + std::to_string(iElement) + "]";
                  if (Element->isPointerType())
                    for (const std::string &Suffix :
                         ReadThrough(Inits[iElement]))
                      Added |= Keys.Read.insert(Key + Suffix).second;
                  else if (Element->isRecordType() || Element->isArrayType())
                    Aggregate(Key, Inits[iElement], Depth + 1);
                }
                return;
              }
              const auto *Record =
                  !Inits.empty() ? Initializer->getType()->getAsCXXRecordDecl()
                                 : nullptr;
              if (!Record)
                return;
              // What a member of a union gives, the one it is made with,
              // `CPointer Pointer{&_Bits}`.
              auto AddField = [&](const FieldDecl *Field, const Expr *Init) {
                const QualType Type = Field->getType();
                // A member of a union is marked at it, like `path` does.
                const std::string Key =
                    Prefix + "." + (Field->getParent()->isUnion() ? "~." : "") +
                    ReferenceCollector::declKey(Field);
                // Also to const, through which a `const_cast` can write,
                // `const_cast<unsigned &>(Borrowed.m_Bits) = 0`.
                if (Type->isReferenceType() && touchesAny(Init, Keys.Read))
                  Added |= Keys.Read.insert(Key).second;
                else if (Type->isPointerType() && MemberUsedOnRead(Key))
                  for (const std::string &Suffix : ReadThrough(Init))
                    Added |= Keys.Read.insert(Key + Suffix).second;
                // Also an array member, `unsigned *m_pBits[1]`.
                else if (Type->isRecordType() || Type->isArrayType())
                  Aggregate(Key, Init, Depth + 1);
              };
              if (Record->isUnion()) {
                const auto *List = dyn_cast<InitListExpr>(Initializer);
                if (const FieldDecl *Field =
                        List ? List->getInitializedFieldInUnion() : nullptr)
                  AddField(Field, Inits.front());
                return;
              }
              unsigned iInit = 0;
              for (unsigned iBase = 0; iBase < Record->getNumBases(); ++iBase) {
                if (iInit >= Inits.size())
                  return;
                Aggregate(Prefix, Inits[iInit++], Depth + 1);
              }
              for (const FieldDecl *Field : Record->fields()) {
                // An unnamed bit-field isn't initialized, `int : 0`.
                if (Field->isUnnamedBitField())
                  continue;
                if (iInit >= Inits.size())
                  break;
                AddField(Field, Inits[iInit++]);
              }
            };
        if (const auto *Declaration = dyn_cast<DeclStmt>(S))
          for (const Decl *Declared : Declaration->decls())
            if (const auto *Variable = dyn_cast<VarDecl>(Declared);
                Variable && Variable->hasLocalStorage() && Variable->getInit())
              Aggregate(ReferenceCollector::declKey(Variable),
                        Variable->getInit(), 0);
        // An init capture of a lambda, `[pValue = &_Value]`, also a copy of
        // what shares, like a local is, `[pState = _pState]`.
        if (const auto *Lambda = dyn_cast<LambdaExpr>(S))
          for (const LambdaCapture &Capture : Lambda->captures())
            if (const auto *Variable = Capture.capturesVariable()
                                           ? dyn_cast<VarDecl>(
                                                 Capture.getCapturedVar())
                                           : nullptr;
                Variable && Variable->isInitCapture() && Variable->getInit()) {
              if (IsPointer(Variable)) {
                if (PointsToRead(Variable->getInit()) && UsedOnRead(Variable))
                  Added |= AddAlias(Variable, Variable->getInit());
              } else {
                Aggregate(ReferenceCollector::declKey(Variable),
                          Variable->getInit(), 0);
              }
            }
        if (const auto *Assignment = dyn_cast<CXXOperatorCallExpr>(S);
            Assignment && Assignment->getOperator() == OO_Equal &&
            Assignment->getNumArgs() == 2)
          if (const auto *Ref = dyn_cast<DeclRefExpr>(
                  Assignment->getArg(0)->IgnoreParenImpCasts());
              Ref && isa<VarDecl>(Ref->getDecl()) &&
              cast<VarDecl>(Ref->getDecl())->hasLocalStorage())
            CopyMembers(ReferenceCollector::declKey(Ref->getDecl()),
                        Assignment->getArg(1));
        // Also through a local reference to it, `unsigned *&pAlias = pBits;
        // pAlias = &_Bits`, or a local pointer to it, `*ppBits = &_Bits`
        // with `unsigned **ppBits = &pBits`.
        auto AssignedPointer = [&](const Expr *Target) -> const DeclRefExpr * {
          for (int Depth = 0; Target && Depth < 8; ++Depth) {
            Target = Target->IgnoreParenImpCasts();
            if (const auto *Deref = dyn_cast<UnaryOperator>(Target);
                Deref && Deref->getOpcode() == UO_Deref) {
              const auto *Ref = dyn_cast<DeclRefExpr>(
                  Deref->getSubExpr()->IgnoreParenImpCasts());
              const auto *Local =
                  Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
              const auto *Address =
                  Local && Local->hasLocalStorage() && Local->getInit()
                      ? dyn_cast<UnaryOperator>(
                            Local->getInit()->IgnoreParenImpCasts())
                      : nullptr;
              if (!Address || Address->getOpcode() != UO_AddrOf ||
                  assignedValueAt(Local, Ref) != Local->getInit())
                return nullptr;
              Target = Address->getSubExpr();
              continue;
            }
            const auto *Ref = dyn_cast<DeclRefExpr>(Target);
            if (!Ref)
              return nullptr;
            const auto *Alias = dyn_cast<VarDecl>(Ref->getDecl());
            if (!Alias || !Alias->getType()->isReferenceType() ||
                !Alias->hasLocalStorage() || !Alias->getInit())
              return Ref;
            Target = Alias->getInit();
          }
          return nullptr;
        };
        if (const auto *Assignment = dyn_cast<BinaryOperator>(S);
            Assignment && Assignment->getOpcode() == BO_Assign)
          if (const DeclRefExpr *Ref = AssignedPointer(Assignment->getLHS());
              Ref && IsPointer(Ref->getDecl()) &&
              PointsToRead(Assignment->getRHS()) &&
              UsedOnRead(cast<VarDecl>(Ref->getDecl())))
            Added |= AddAlias(Ref->getDecl(), Assignment->getRHS());
        // A pointer member or element of a local assigned it,
        // `Borrowed.m_pBits = &_Bits` or `Pointers[0] = &_Bits`, like one it
        // is made with.
        if (const auto *Assignment = dyn_cast<BinaryOperator>(S);
            Assignment && Assignment->getOpcode() == BO_Assign &&
            Assignment->getLHS()->getType()->isPointerType()) {
          std::string Path;
          const Expr *Base = Assignment->getLHS()->IgnoreParenImpCasts();
          bool Members = true;
          for (int Depth = 0; Depth < 16; ++Depth) {
            if (const auto *Member = dyn_cast<MemberExpr>(Base)) {
              const auto *Field = dyn_cast<FieldDecl>(Member->getMemberDecl());
              if (Member->isArrow() || !Field ||
                  Field->getParent()->isUnion()) {
                Members = false;
                break;
              }
              Path = "." + ReferenceCollector::declKey(Field) + Path;
              Base = Member->getBase()->IgnoreParenImpCasts();
              continue;
            }
            if (const auto *Subscript = dyn_cast<ArraySubscriptExpr>(Base)) {
              const Expr *Array = Subscript->getBase()->IgnoreParenImpCasts();
              std::optional<llvm::APSInt> Index =
                  Subscript->getIdx()->isValueDependent()
                      ? std::nullopt
                      : Subscript->getIdx()->getIntegerConstantExpr(Context);
              if (!Array->getType()->isConstantArrayType() || !Index) {
                Members = false;
                break;
              }
              Path = "[" + std::to_string(Index->getExtValue()) + "]" + Path;
              Base = Array;
              continue;
            }
            // Also by an index operator, `Pointers[umint(0)] = &_Bits` of a
            // map of pointers, the element it gives.
            if (const auto *Element = dyn_cast<CXXOperatorCallExpr>(Base);
                Element && Element->getOperator() == OO_Subscript &&
                Element->getNumArgs() == 2) {
              const std::string Key = ReferenceCollector::objectKey(Element);
              const Expr *Container = Element->getArg(0)->IgnoreParenImpCasts();
              const std::string ContainerKey =
                  ReferenceCollector::valueKey(Container);
              if (Key.empty() || ContainerKey.empty() ||
                  !StringRef(Key).starts_with(ContainerKey)) {
                Members = false;
                break;
              }
              Path = Key.substr(ContainerKey.size()) + Path;
              Base = Container;
              continue;
            }
            break;
          }
          if (const auto *Ref = dyn_cast<DeclRefExpr>(Base);
              Members && !Path.empty() && Ref && isa<VarDecl>(Ref->getDecl()) &&
              cast<VarDecl>(Ref->getDecl())->hasLocalStorage() &&
              MemberUsedOnRead(ReferenceCollector::declKey(Ref->getDecl()) +
                               Path))
            for (const std::string &Suffix : ReadThrough(Assignment->getRHS()))
              Added |= Keys.Read
                           .insert(ReferenceCollector::declKey(Ref->getDecl()) +
                                   Path + Suffix)
                           .second;
        }
        // Not a branch of an `if constexpr` that is discarded.
        for (const Stmt *Child : keptChildren(S, Context))
          Visit(Child);
      };
      Visit(Body);
    }
  }
  const llvm::StringSet<> &References = Keys.Read;

  // Look for a check of the argument on the way out of the function: a
  // condition the call depends on, or an earlier statement that leaves or
  // asserts when the value is zero.
  DynTypedNode Child = DynTypedNode::create(*Start);
  // Whether what runs between the code looked at and the call can write what
  // the argument reads, so a check before it doesn't hold at the call.
  bool Written = false;
  // Whether a jump can reach the call past what is looked at: to a label,
  // from anywhere in the function, or to a case, from the switch it is in.
  bool JumpedToLabel = false;
  bool JumpedToCase = false;
  // Whether \p Lambda is called where it is made, `[&] { ... }()`, so its
  // body runs there.
  auto CalledWhereMade = [&](const LambdaExpr *Lambda) {
    DynTypedNode Up = DynTypedNode::create(*Lambda);
    for (int Depth = 0; Depth < 8; ++Depth) {
      const auto Above = instantiatedParents(Up, Context);
      if (Above.empty())
        return false;
      // Also given arguments, `[&](int _Ignored) { ... }(0)`, that change
      // nothing before its body runs.
      if (const auto *Invoke = Above[0].get<CXXOperatorCallExpr>())
        return Invoke->getOperator() == OO_Call && Invoke->getNumArgs() >= 1 &&
               Invoke->getArg(0)->IgnoreImplicit()->IgnoreParens() == Lambda &&
               llvm::none_of(llvm::drop_begin(Invoke->arguments()),
                             [&](const Expr *Argument) {
                               return Argument->HasSideEffects(Context);
                             });
      if (!(Above[0].get<ImplicitCastExpr>() ||
            Above[0].get<MaterializeTemporaryExpr>() ||
            Above[0].get<CXXBindTemporaryExpr>() || Above[0].get<ParenExpr>()))
        return false;
      Up = Above[0];
    }
    return false;
  };
  for (;;) {
    const auto Parents = instantiatedParents(Child, Context);
    if (Parents.empty())
      break;
    const DynTypedNode &Parent = Parents[0];
    // Not past a lambda, but past what it captures, `[Bit =
    // fg_GetLowestBitSetNoZero(_Bits)]`, which runs where it is made, and
    // past one called where it is made, `[&] { ... }()`, whose body runs
    // there.
    if (const auto *Lambda = Parent.get<LambdaExpr>()) {
      const auto *Captured = Child.get<VarDecl>();
      const Stmt *Init = Child.get<Stmt>();
      const bool Capture = (Captured && Captured->isInitCapture()) ||
                           (Init && Init != Lambda->getBody() &&
                            llvm::is_contained(Lambda->capture_inits(), Init));
      if (!Capture && (!CalledWhereMade(Lambda) || Init != Lambda->getBody()))
        break;
    } else if (const auto *Function = Parent.get<FunctionDecl>()) {
      // Also from the body of its call operator, which is its parent too.
      const LambdaExpr *Made = nullptr;
      if (isLambdaCallOperator(Function))
        for (DynTypedNode Up = Parent; !Made;) {
          const auto Above = instantiatedParents(Up, Context);
          if (Above.empty() ||
              !(Above[0].get<CXXRecordDecl>() || Above[0].get<LambdaExpr>()))
            break;
          Up = Above[0];
          Made = Up.get<LambdaExpr>();
        }
      if (!Made || !CalledWhereMade(Made))
        break;
      Child = DynTypedNode::create(*Made);
      continue;
    } else if (Parent.get<BlockDecl>())
      break;
    // A local is its initializer in the declaration around it.
    const Stmt *ChildStmt = Child.get<Stmt>();
    if (const auto *Local = Child.get<VarDecl>(); !ChildStmt && Local)
      ChildStmt = Local->getInit();
    if (isa_and_nonnull<LabelStmt>(ChildStmt))
      JumpedToLabel = true;
    if (isa_and_nonnull<SwitchCase>(ChildStmt))
      JumpedToCase = true;
    // A switch on the argument whose cases that reach the call are each of
    // another value than zero, `case 0: return -1; default: ...`, checks it.
    if (const auto *Switch = Parent.get<SwitchStmt>();
        Switch && JumpedToCase && !JumpedToLabel && !Written &&
        !writesAny(Switch->getCond(), References) &&
        !writesAny(Switch->getInit(), References) &&
        switchExcludesZero(Switch, Call, Keys, Context))
      return true;
    // What is around the switch runs before it jumps to a case.
    if (Parent.get<SwitchStmt>())
      JumpedToCase = false;
    const bool Jumped = JumpedToLabel || JumpedToCase;

    auto Checks = [&](const Expr *Condition, bool Branch) {
      return Condition && !Written && !Jumped &&
             tells(zeroWhen(Condition, Branch, Keys, Context), UnknownChecks);
    };
    auto Writes = [&](const Stmt *S) { return writesAny(S, References); };
    // A loop that runs its body once, `do { ... } while (false)` or one
    // whose body ends with a `break` and doesn't `continue`, has no later
    // iteration.
    auto RunsOnce = [&](const Stmt *Loop, const Stmt *Body) {
      if (const auto *Do = dyn_cast<DoStmt>(Loop))
        if (std::optional<llvm::APSInt> Value =
                Do->getCond()->getIntegerConstantExpr(Context);
            Value && Value->isZero())
          return true;
      // One whose body ends leaving it, with a `break`, `return` or `throw`,
      // unless it continues before.
      const auto *Block = dyn_cast_or_null<CompoundStmt>(Body);
      const Stmt *Last = Block && !Block->body_empty() ? Block->body_back()
                                                       : Body;
      // Also with attributes, `[[unlikely]] break;`, or a throw of what has
      // to be destroyed, `throw CError{};`.
      for (int Depth = 0; Last && Depth < 4; ++Depth)
        if (const auto *Attributed = dyn_cast<AttributedStmt>(Last))
          Last = Attributed->getSubStmt();
        else if (const auto *Cleanups = dyn_cast<ExprWithCleanups>(Last))
          Last = Cleanups->getSubExpr();
        else
          break;
      if (!Last || !(isa<BreakStmt, ReturnStmt, CoreturnStmt>(Last) ||
                     isa<CXXThrowExpr>(Last)))
        return false;
      if (!Block)
        return true;
      std::function<bool(const Stmt *)> Continues = [&](const Stmt *S) {
        if (!S || isa<ForStmt, WhileStmt, DoStmt, CXXForRangeStmt, LambdaExpr>(S))
          return false;
        return isa<ContinueStmt>(S) || llvm::any_of(childrenOnce(S), Continues);
      };
      return !llvm::any_of(Block->body(), Continues);
    };
    if (const auto *If = Parent.get<IfStmt>()) {
      // The initializer and the condition run before the condition is
      // tested, so it doesn't check them.
      const bool InElse = isSame(ChildStmt, If->getElse());
      if (InElse || isSame(ChildStmt, If->getThen())) {
        if (Checks(If->getCond(), !InElse))
          return true;
        Written = Written || Writes(If->getInit()) ||
                  Writes(If->getConditionVariableDeclStmt()) ||
                  Writes(If->getCond());
      }
    } else if (const auto *Conditional =
                   Parent.get<AbstractConditionalOperator>()) {
      if (!isSame(ChildStmt, Conditional->getCond())) {
        if (Checks(Conditional->getCond(),
                   isSame(ChildStmt, Conditional->getTrueExpr())))
          return true;
        Written = Written || Writes(Conditional->getCond());
      }
    } else if (const auto *Binary = Parent.get<BinaryOperator>();
               Binary && (Binary->isLogicalOp() ||
                          Binary->getOpcode() == BO_Comma)) {
      if (isSame(ChildStmt, Binary->getRHS())) {
        if (Binary->isLogicalOp() &&
            Checks(Binary->getLHS(), Binary->getOpcode() == BO_LAnd))
          return true;
        {
          // Its temporaries are destroyed at the end of the full-expression,
          // after the call.
          const bool SavedBefore = s_BeforeInFullExpression;
          s_BeforeInFullExpression = true;
          Written = Written || Writes(Binary->getLHS());
          s_BeforeInFullExpression = SavedBefore;
        }
      }
    } else if (const auto *While = Parent.get<WhileStmt>()) {
      if (isSame(ChildStmt, While->getBody()) && Checks(While->getCond(), true))
        return true;
      // A later iteration runs after the whole loop; one that runs once
      // still runs its condition first.
      Written = Written || (RunsOnce(While, While->getBody())
                                ? Writes(While->getConditionVariableDeclStmt()) ||
                                      Writes(While->getCond())
                                : Writes(While));
    } else if (const auto *For = Parent.get<ForStmt>()) {
      if (isSame(ChildStmt, For->getBody()) && Checks(For->getCond(), true))
        return true;
      // The increment runs after the condition held and the body ran,
      // `for (; _Bits; _Bits &= ~(1u << fg_GetLowestBitSetNoZero(_Bits)))`.
      if (isSame(ChildStmt, For->getInc()) && !Written &&
          !writesAny(For->getBody(), References) &&
          Checks(For->getCond(), true))
        return true;
      // The initializer runs once, before the rest of the loop.
      if (!isSame(ChildStmt, For->getInit()))
        Written = Written || (RunsOnce(For, For->getBody())
                                ? Writes(For->getInit()) ||
                                      Writes(For->getConditionVariableDeclStmt()) ||
                                      Writes(For->getCond())
                                : Writes(For));
    } else if (const auto *Do = Parent.get<DoStmt>();
               Do && isSame(ChildStmt, Do->getBody()) &&
               ((!Writes(Do->getCond()) && Checks(Do->getCond(), true)) ||
                checksWhatItWrites(Do->getCond(), References, Context))) {
      // A later iteration runs after the condition held, `do { ... } while
      // (_Bits)`, also one that tests what it writes, `do { ... } while
      // ((_Bits >>= 1) != 0)`, and the first after what is before the loop,
      // which is looked at next.
    } else if (Parent.get<DoStmt>() || Parent.get<CXXForRangeStmt>()) {
      const Stmt *Loop = Parent.get<Stmt>();
      const Stmt *Body = Parent.get<DoStmt>()
                             ? Parent.get<DoStmt>()->getBody()
                             : Parent.get<CXXForRangeStmt>()->getBody();
      // One that runs once, a range-based `for`, still runs what makes the
      // range and its variable first.
      const auto *Range = Parent.get<CXXForRangeStmt>();
      // What makes the range runs once, before the rest of the loop.
      if (!(Range && (isSame(ChildStmt, Range->getInit()) ||
                      isSame(ChildStmt, Range->getRangeStmt()))))
        Written = Written || (RunsOnce(Loop, Body)
                                ? Range && (Writes(Range->getInit()) ||
                                            Writes(Range->getRangeStmt()) ||
                                            Writes(Range->getLoopVarStmt()))
                                : Writes(Loop));
    } else if (const auto *Compound = Parent.get<CompoundStmt>()) {
      // An earlier check, unless the value is written after it.
      bool Checked = false;
      bool WrittenHere = false;
      bool LabeledHere = false;
      bool CaseHere = false;
      for (const Stmt *Statement : Compound->body()) {
        // A label, like a `case` of a switch, can be jumped to past the
        // checks before it.
        // Also one in a statement before, `{ Target:; }`; one in the
        // statement of the call is looked at in it.
        const bool NestedLabel = !isa<LabelStmt>(Statement) &&
                                 !isSame(Statement, ChildStmt) &&
                                 containsLabel(Statement);
        const bool Labeled =
            isa<SwitchCase, LabelStmt>(Statement) || NestedLabel;
        if (Labeled)
          Checked = false;
        LabeledHere |= isa<LabelStmt>(Statement) || NestedLabel;
        CaseHere |= isa<SwitchCase>(Statement);
        if (isSame(Statement, ChildStmt))
          break;
        // What is labeled, like the check of `case 0: if (!Value) return;`.
        while (true) {
          if (const auto *Case = dyn_cast<SwitchCase>(Statement))
            Statement = Case->getSubStmt();
          else if (const auto *Label = dyn_cast<LabelStmt>(Statement))
            Statement = Label->getSubStmt();
          else
            break;
        }
        // Also by destroying what its initializer made of the value, `if
        // (CClear Clear{_Bits}; !_Bits) return;`, after the check.
        auto DestroysWithValue = [&](const Stmt *Init) {
          const auto *Declaration = dyn_cast_or_null<DeclStmt>(Init);
          return Declaration &&
                 llvm::any_of(Declaration->decls(), [&](const Decl *D) {
                   // Also of the elements of an array, `CClear Clear[1]`.
                   const auto *Local = dyn_cast<VarDecl>(D);
                   const CXXRecordDecl *Record =
                       Local ? Local->getType()
                                   ->getBaseElementTypeUnsafe()
                                   ->getAsCXXRecordDecl()
                             : nullptr;
                   if (!Record || !Record->hasDefinition() ||
                       Record->hasTrivialDestructor())
                     return false;
                   // Made of the value, by a destructor that can write
                   // through what it keeps of it: not one that can be seen
                   // not to, `~CObserve() {}`.
                   const CXXDestructorDecl *Destructor =
                       Record->getDestructor();
                   const FunctionDecl *Definition =
                       Destructor ? Destructor->getDefinition() : nullptr;
                   const bool MayWrite = !Definition ||
                                         !Definition->getBody() ||
                                         destructorWritesThroughMember(Record);
                   return (Local->getInit() && MayWrite &&
                           givesAny(Local->getInit(), Keys.Read)) ||
                          destructorWritesAny(Record, Keys.Read);
                 });
        };
        // Also of a `for`, `while` or `switch`, destroyed when it ends, also
        // when its body never runs, `for (CClear Clear{_Value}; false;)`.
        const auto *DestroyingIf = dyn_cast<IfStmt>(Statement);
        bool Destroys =
            DestroyingIf &&
            (DestroysWithValue(DestroyingIf->getInit()) ||
             DestroysWithValue(DestroyingIf->getConditionVariableDeclStmt()));
        if (const auto *For = dyn_cast<ForStmt>(Statement))
          Destroys |= DestroysWithValue(For->getInit()) ||
                      DestroysWithValue(For->getConditionVariableDeclStmt());
        else if (const auto *While = dyn_cast<WhileStmt>(Statement))
          Destroys |= DestroysWithValue(While->getConditionVariableDeclStmt());
        else if (const auto *Switch = dyn_cast<SwitchStmt>(Statement))
          Destroys |= DestroysWithValue(Switch->getInit()) ||
                      DestroysWithValue(Switch->getConditionVariableDeclStmt());
        // Not a write in a branch that leaves before the call, `if (_bClear)
        // { o_Bits = 0; return -1; }`, which no path to it runs.
        auto WritesReaching = [&](const Stmt *Earlier) {
          const auto *Branching = dyn_cast<IfStmt>(Earlier);
          if (!Branching || Branching->isConsteval() ||
              Branching->isConstexpr())
            return Writes(Earlier);
          auto Reaches = [&](const Stmt *Branch) {
            return Branch && Writes(Branch) && !leaves(Branch, Call, SM);
          };
          return Writes(Branching->getInit()) ||
                 Writes(Branching->getConditionVariableDeclStmt()) ||
                 Writes(Branching->getCond()) ||
                 Reaches(Branching->getThen()) || Reaches(Branching->getElse());
        };
        if (WritesReaching(Statement) || Destroys) {
          Checked = false;
          WrittenHere = true;
        }
        // What the other branch of the check does runs before the call. The
        // branch that leaves can be either.
        if (const auto *EarlierIf = dyn_cast<IfStmt>(Statement);
            EarlierIf && !Destroys) {
          if (leaves(EarlierIf->getThen(), Call, SM) &&
              tells(zeroWhen(EarlierIf->getCond(), false, Keys, Context),
                    UnknownChecks) &&
              !Writes(EarlierIf->getElse()))
            Checked = true;
          if (EarlierIf->getElse() &&
              leaves(EarlierIf->getElse(), Call, SM) &&
              tells(zeroWhen(EarlierIf->getCond(), true, Keys, Context),
                    UnknownChecks) &&
              !Writes(EarlierIf->getThen()))
            Checked = true;
        }
        if (const auto *Attributed = dyn_cast<AttributedStmt>(Statement);
            Attributed &&
            llvm::any_of(Attributed->getAttrs(), [&](const Attr *A) {
              // Not one with side effects, `[[assume((_Bits = 1))]]`, which
              // the compiler ignores.
              const auto *Assume = dyn_cast<CXXAssumeAttr>(A);
              return Assume &&
                     !Assume->getAssumption()->HasSideEffects(Context) &&
                     tells(
                         zeroWhen(Assume->getAssumption(), true, Keys, Context),
                         UnknownChecks);
            }))
          Checked = true;
        // An assertion, like `DMibCheck(Value != 0)`, or a fact for the
        // analysis, `DMibAnalyzerAssume(Value >= 16)`, whose condition is the
        // operand of a `sizeof`, which is never evaluated, like `[[assume]]`.
        // An assertion that is compiled out, like `DMibFastCheck` in a release
        // build, tells nothing: a fact beside it tells it there.
        // Also in parentheses, `(DMibAnalyzerAssume(_Bits != 0));`.
        const Stmt *Bare = Statement;
        if (const auto *Expression = dyn_cast<Expr>(Statement))
          Bare = Expression->IgnoreParens();
        SourceLocation MacroLoc = Bare->getBeginLoc();
        if (MacroLoc.isMacroID()) {
          for (; MacroLoc.isMacroID();
               MacroLoc = SM.getImmediateMacroCallerLoc(MacroLoc)) {
            const StringRef MacroName = Lexer::getImmediateMacroName(
                MacroLoc, SM, Context.getLangOpts());
            if (llvm::is_contained(AssumeMacros, MacroName)) {
              // Not one that would change it, `(_Bits = 1)`, which isn't
              // evaluated, so it doesn't, but a comparison that only reads it,
              // also of an integer class, `_Bits != 0`.
              if (const Expr *Assumption = analyzerAssumption(Bare);
                  Assumption && !writesAny(Assumption, Keys.Read) &&
                  tells(zeroWhen(Assumption, true, Keys, Context),
                        UnknownChecks))
                Checked = true;
              break;
            }
            if (llvm::is_contained(CheckMacros, MacroName)) {
              if (readsAny(Bare, Keys.Read) &&
                  assertsNonZero(Bare, MacroLoc, Keys, Context, UnknownChecks))
                Checked = true;
              break;
            }
          }
        }
      }
      if (Checked && !Written && !Jumped)
        return true;
      Written = Written || WrittenHere;
      JumpedToLabel |= LabeledHere;
      JumpedToCase |= CaseHere;
    } else if (const auto *ParentStmt = Parent.get<Stmt>()) {
      // What else runs in an expression can run before the call, but the
      // elements of a braced list, `{fg_GetLowestBitSetNoZero(_Bits), _Bits
      // &= _Bits - 1}`, of an aggregate initialized with parentheses, and the
      // variables of a declaration, run in order: only those before it.
      // So do the operands of a shift, a subscript and a member through a
      // pointer to it, also overloaded, `fg_GetLowestBitSetNoZero(_Bits) <<
      // (_Bits = 0)`, left first since C++17. And of a `try`, its block
      // before its handlers, which don't run before what is in it.
      const auto *Construct = dyn_cast<CXXConstructExpr>(ParentStmt);
      const auto *Binary = dyn_cast<BinaryOperator>(ParentStmt);
      const auto *Overloaded = dyn_cast<CXXOperatorCallExpr>(ParentStmt);
      const bool InOrder =
          isa<InitListExpr, CXXParenListInitExpr, DeclStmt, ArraySubscriptExpr,
              CXXTryStmt>(ParentStmt) ||
          (Construct && Construct->isListInitialization()) ||
          (Binary && (Binary->isShiftOp() || Binary->isPtrMemOp())) ||
          (Overloaded && llvm::is_contained({OO_LessLess, OO_GreaterGreater,
                                             OO_ArrowStar, OO_Subscript},
                                            Overloaded->getOperator()));
      // The temporaries of an expression around the call are destroyed
      // after it, `((void)CReset{}, fg_GetLowestBitSetNoZero(g_Bits))`.
      const bool SavedBefore = s_BeforeInFullExpression;
      s_BeforeInFullExpression = isa<Expr>(ParentStmt);
      const llvm::scope_exit Restore(
          [&] { s_BeforeInFullExpression = SavedBefore; });
      // The right side of an assignment, also an overloaded one, runs before
      // its left side, `fg_Slot(_Bits) = fg_GetLowestBitSetNoZero(_Bits)`.
      const Expr *Right = nullptr;
      if (const auto *Assign = dyn_cast<BinaryOperator>(ParentStmt);
          Assign && Assign->isAssignmentOp())
        Right = Assign->getRHS();
      else if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(ParentStmt);
               Operator && Operator->isAssignmentOp() &&
               Operator->getNumArgs() == 2)
        Right = Operator->getArg(1);
      const bool RunsFirst = Right && isSame(Right, ChildStmt);
      for (const Stmt *Sibling : ParentStmt->children()) {
        if (isSame(Sibling, ChildStmt)) {
          if (InOrder)
            break;
          continue;
        }
        if (!RunsFirst && Writes(Sibling))
          Written = true;
      }
    }
    Child = Parent;
  }

  return false;
}

/// Whether \p S is not evaluated: in `sizeof`, `alignof`, `noexcept`,
/// `typeid`, `decltype` or a requires expression. What a template argument
/// or an array bound is made of is evaluated, at compile time.
static bool isUnevaluated(const Stmt *S, ASTContext &Context) {
  DynTypedNode Node = DynTypedNode::create(*S);
  for (int Depth = 0; Depth < 256; ++Depth) {
    const auto Parents = instantiatedParents(Node, Context);
    if (Parents.empty())
      return false;
    // Nor the arm of `__builtin_choose_expr` it doesn't choose.
    if (const auto *Choose = Parents[0].get<ChooseExpr>();
        Choose && !Choose->isConditionDependent() && Node.get<Stmt>() &&
        Node.get<Stmt>() != Choose->getChosenSubExpr())
      return true;
    Node = Parents[0];
    // A `typeid` of a polymorphic object evaluates it, unless what is around
    // it isn't, like `noexcept(typeid(...))`.
    if (const auto *Typeid = Node.get<CXXTypeidExpr>();
        Typeid && !Typeid->isPotentiallyEvaluated())
      return true;
    if (Node.get<UnaryExprOrTypeTraitExpr>() || Node.get<CXXNoexceptExpr>() ||
        Node.get<RequiresExpr>())
      return true;
    // Nor what a builtin only looks at the type or constness of,
    // `__builtin_constant_p(...)`.
    if (const auto *Call = Node.get<CallExpr>();
        Call && (Call->getBuiltinCallee() == Builtin::BI__builtin_constant_p ||
                 Call->getBuiltinCallee() == Builtin::BI__builtin_object_size))
      return true;
    if (const auto *Loc = Node.get<TypeLoc>();
        Loc && isa<DecltypeType, TypeOfExprType>(Loc->getTypePtr()))
      return true;
    // The body of a lambda runs when it is called, wherever it is made.
    if (Node.get<FunctionDecl>() || Node.get<TranslationUnitDecl>() ||
        Node.get<LambdaExpr>())
      return false;
  }
  return false;
}

void NoZeroBitScanCheck::check(const MatchFinder::MatchResult &Result) {
  if (Repeated.repeated(Result))
    return;
  ASTContext &Context = *Result.Context;
  const ASTContext *const SavedContext = s_CheckContext;
  s_CheckContext = &Context;
  const llvm::scope_exit Restore([&] { s_CheckContext = SavedContext; });
  const SourceManager &SM = *Result.SourceManager;
  const auto *Call = Result.Nodes.getNodeAs<CallExpr>("call");
  // Through a local pointer, what it is made from, when it is one of them.
  const FunctionDecl *Scan = nullptr;
  if (!Call) {
    Call = Result.Nodes.getNodeAs<CallExpr>("local");
    Scan = Call ? calledThroughLocal(Call) : nullptr;
    if (!Scan || !Scan->getIdentifier() ||
        llvm::none_of(Functions, [&](StringRef Name) {
          return hasQualifiedName(Scan, Name) ||
                 Name.ltrim(':') == Scan->getName();
        }))
      return;
  }
  // What a template is instantiated as, not the template, whose types and
  // constants its instantiations tell, and not what doesn't run, also in a
  // branch of an `if constexpr` that is discarded.
  if (isInTemplatePattern(DynTypedNode::create(*Call), Context) ||
      isUnevaluated(Call, Context) || isInDiscardedBranch(Call, Context))
    return;
  const Expr *Argument = Call->getArg(0);

  // A specialization is named like its template.
  const NamedDecl *Callee = Scan ? Scan : Call->getDirectCallee();
  if (const auto *Function = dyn_cast<FunctionDecl>(Callee))
    if (const FunctionTemplateDecl *Template = Function->getPrimaryTemplate())
      Callee = Template->getTemplatedDecl();
  // Where it is written, also when that is in a macro, `DScan(_Bits)`, whose
  // argument and checks around tell, and not in the macro.
  const SourceLocation Loc =
      reportLocation(SM.getExpansionLoc(Call->getBeginLoc()), SM);
  if (Loc.isInvalid())
    return;
  if (std::optional<llvm::APSInt> Value =
          Argument->getIntegerConstantExpr(Context)) {
    if (Value->isZero())
      diag(Loc, "%0 is called with zero") << Callee;
    return;
  }
  if (isNonZeroByConstruction(Argument, Context))
    return;
  // An argument that reads what is volatile, `unsigned volatile &_Bits`,
  // reads it again, which a check of an earlier read doesn't tell.
  bool ReadsVolatile = false;
  std::function<void(const Stmt *)> FindVolatile = [&](const Stmt *S) {
    if (!S || ReadsVolatile || isa<LambdaExpr>(S))
      return;
    if (const auto *Read = dyn_cast<ImplicitCastExpr>(S);
        Read && Read->getCastKind() == CK_LValueToRValue &&
        Read->getSubExpr()->getType().isVolatileQualified())
      ReadsVolatile = true;
    for (const Stmt *Child : childrenOnce(S))
      FindVolatile(Child);
  };
  FindVolatile(Argument);
  // Also given by reference, `fg_Scan(T const &_Value)` with a volatile `T`.
  ReadsVolatile |= Argument->IgnoreParenImpCasts()->isGLValue() &&
                   Argument->IgnoreParenImpCasts()->getType().isVolatileQualified();
  if (ReadsVolatile)
    report(Loc, Callee);
  // Of a conditional, each of its values, `_Bits ? _Bits : 1`, which its
  // condition can check, through conversions that keep what isn't zero, and
  // one that narrows it when each value, before it is promoted, fits, `uint16(
  // _Bits ? _Bits : 1)` of a `uint8`, but not when it doesn't, `uint8(_Bits ?
  // _Bits : 1)` of a `uint16`, which can give zero.
  else if (const auto [Conditional, Width] =
               [&]() -> std::pair<const ConditionalOperator *, unsigned> {
             const Expr *Inner = Argument;
             unsigned Narrowest = 0;
             for (int Depth = 0; Depth < 16; ++Depth) {
               Inner = Inner->IgnoreParens();
               if (const auto *Materialize =
                       dyn_cast<MaterializeTemporaryExpr>(Inner)) {
                 Inner = Materialize->getSubExpr();
                 continue;
               }
               if (const auto *Bind = dyn_cast<CXXBindTemporaryExpr>(Inner)) {
                 Inner = Bind->getSubExpr();
                 continue;
               }
               // Also widened to an integer class, `uint256(_Bits ? _Bits :
               // 1)` of a `uint128`, which keeps its bits.
               if (const auto *Functional =
                       dyn_cast<CXXFunctionalCastExpr>(Inner);
                   Functional && isa<CXXConstructExpr>(
                                     Functional->getSubExpr()->IgnoreImplicit())) {
                 Inner = Functional->getSubExpr()->IgnoreImplicit();
                 continue;
               }
               if (const Expr *Widened =
                       widenedFrom(Inner, Inner->getType(), Context)) {
                 Inner = Widened;
                 continue;
               }
               const auto *Cast = dyn_cast<CastExpr>(Inner);
               if (!Cast)
                 break;
               if (!keepsNonZero(Cast, Context)) {
                 if (!Cast->getType()->isIntegerType() ||
                     !Cast->getSubExpr()->getType()->isIntegerType())
                   break;
                 const unsigned To = Context.getIntWidth(Cast->getType());
                 Narrowest = Narrowest ? std::min(Narrowest, To) : To;
               }
               Inner = Cast->getSubExpr();
             }
             return {dyn_cast<ConditionalOperator>(Inner), Narrowest};
           }();
           Conditional) {
    if (llvm::any_of(
            std::initializer_list<const Expr *>{Conditional->getTrueExpr(),
                                                Conditional->getFalseExpr()},
            [&, Width = Width](const Expr *Chosen) {
              const Expr *Unpromoted = Chosen->IgnoreParenImpCasts();
              if (Width &&
                  !(Unpromoted->getType()->isIntegerType() &&
                    Context.getIntWidth(Unpromoted->getType()) <= Width) &&
                  (knownSetBits(Chosen, Context) &
                   llvm::APInt::getLowBitsSet(KnownBitsWidth,
                                              std::min(Width, KnownBitsWidth)))
                      .isZero())
                return true;
              return !isNonZeroByConstruction(Chosen, Context) &&
                     !isChecked(Call, Context, true, Chosen);
            }))
      report(Loc, Callee);
  } else if (!isChecked(Call, Context))
    report(Loc, Callee);
}

void NoZeroBitScanCheck::report(SourceLocation Loc, const NamedDecl *Callee) {
  StringRef Alternative = Callee->getName();
  Alternative.consume_back("NoZero");
  diag(Loc, "the argument of %0 is not checked for zero first; check it, or "
            "call '%1', which handles zero")
      << Callee << Alternative;
}

} // namespace clang::tidy::malterlib
