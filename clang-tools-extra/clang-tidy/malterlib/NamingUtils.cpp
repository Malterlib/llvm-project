//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "NamingUtils.h"
#include "OncePerTypeVisitor.h"
#include "clang/AST/ASTDiagnostic.h"
#include "clang/AST/ASTLambda.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/ExprConcepts.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/StmtCXX.h"
#include "clang/Basic/Builtins.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringSet.h"
#include <algorithm>
#include <atomic>
#include <deque>
#include <functional>
#include <mutex>

namespace clang::tidy::malterlib {

uint64_t astIdentity(const ASTContext &Context) {
  static std::mutex Mutex;
  static llvm::DenseMap<const ASTContext *, uint64_t> Identities;
  static uint64_t Next = 0;
  // Each AST that is gone changes the epoch, which a thread's last answer is
  // of, so it asks again for an AST at the address of one that is gone.
  static std::atomic<uint64_t> Epoch = 0;
  struct CLast {
    const ASTContext *Context = nullptr;
    uint64_t Identity = 0;
    uint64_t Epoch = 0;
  };
  static thread_local CLast s_Last;
  if (s_Last.Context == &Context && s_Last.Epoch == Epoch.load())
    return s_Last.Identity;
  std::lock_guard<std::mutex> Lock(Mutex);
  auto [It, Inserted] = Identities.try_emplace(&Context, 0);
  if (Inserted) {
    It->second = ++Next;
    const_cast<ASTContext &>(Context).AddDeallocation(
        [](void *Gone) {
          std::lock_guard<std::mutex> Lock(Mutex);
          Identities.erase(static_cast<const ASTContext *>(Gone));
          ++Epoch;
        },
        const_cast<ASTContext *>(&Context));
  }
  s_Last = {&Context, It->second, Epoch.load()};
  return It->second;
}

bool splitStoragePrefix(StringRef Name, StringRef &Prefix, StringRef &Rest) {
  const size_t Underscore = Name.find('_');
  if (Underscore == StringRef::npos)
    return false;
  if (!llvm::all_of(Name.take_front(Underscore), llvm::isLower))
    return false;
  Prefix = Name.take_front(Underscore + 1);
  Rest = Name.drop_front(Underscore + 1);
  return true;
}

bool isBuiltinTypeName(StringRef Name) {
  // Of the language, like `int` of `DMibPBits_int`, and those of the
  // vocabulary, `uint32` of Malterlib.
  return vocabulary().BuiltinTypes.contains(Name);
}

bool hasUpperGroups(StringRef Rest) {
  // An underscore separates groups, so it doesn't end a name.
  if (Rest.ends_with("_"))
    return false;
  for (size_t I = 0; I + 1 < Rest.size(); ++I) {
    if (Rest[I] != '_' || llvm::isUpper(Rest[I + 1]) ||
        llvm::isDigit(Rest[I + 1]))
      continue;
    // A built-in type, like `fp32` of `DMibPCanDo_fp32`, also one with an
    // underscore, `char8_t` of `DMibPBits_char8_t`: the longest that ends
    // where a group does.
    const StringRef After = Rest.drop_front(I + 1);
    size_t Length = 0;
    for (size_t End = 0; End != StringRef::npos;) {
      End = After.find('_', End + 1);
      const size_t Group = End == StringRef::npos ? After.size() : End;
      if (isBuiltinTypeName(After.take_front(Group)))
        Length = Group;
    }
    if (!Length)
      return false;
    I += Length;
  }
  return true;
}

bool isUpperCamelWithConcepts(StringRef Rest) {
  // Only the outer concept is a prefix, like `p` of a pointer to a boolean,
  // not `pb`.
  size_t I = 0;
  if (I < Rest.size() && ConceptPrefixes.contains(Rest[I]))
    ++I;
  return I < Rest.size() &&
         (llvm::isUpper(Rest[I]) || llvm::isDigit(Rest[I])) &&
         hasUpperGroups(Rest.drop_front(I));
}

StringRef wrongConceptPrefix(StringRef Rest) {
  auto Starts = [&](size_t I) {
    return I < Rest.size() && (llvm::isUpper(Rest[I]) || llvm::isDigit(Rest[I]));
  };
  if (Rest.size() >= 2 && llvm::isLower(Rest[0]) &&
      !ConceptPrefixes.contains(Rest[0]) && Starts(1))
    return Rest.take_front(1);
  if (Rest.size() >= 3 && ConceptPrefixes.contains(Rest[0]) &&
      ConceptPrefixes.contains(Rest[1]) && Starts(2))
    return Rest.take_front(2);
  return {};
}

std::string quotedConceptPrefixes() {
  llvm::SmallVector<StringRef, 8> Letters;
  for (size_t I = 0; I < ConceptPrefixes.size(); ++I)
    Letters.push_back(ConceptPrefixes.substr(I, 1));
  return quotedList(Letters);
}

StringRef withoutConcept(StringRef Rest) {
  if (Rest.size() >= 2 && ConceptPrefixes.contains(Rest[0]) &&
      (llvm::isUpper(Rest[1]) || llvm::isDigit(Rest[1])))
    return Rest.drop_front();
  return Rest;
}

bool hasTypePrefix(StringRef Name, llvm::ArrayRef<StringRef> TypePrefixes) {
  return llvm::any_of(TypePrefixes, [&](StringRef Prefix) {
    return Name.size() > Prefix.size() && Name.starts_with(Prefix) &&
           (llvm::isUpper(Name[Prefix.size()]) ||
            llvm::isDigit(Name[Prefix.size()])) &&
           hasUpperGroups(Name.drop_front(Prefix.size()));
  });
}

bool isLowerCaseName(StringRef Name) {
  return llvm::none_of(Name, llvm::isUpper);
}

SourceLocation reportLocation(SourceLocation Loc, const SourceManager &SM) {
  if (Loc.isInvalid())
    return {};
  const SourceLocation SpellingLoc = SM.getSpellingLoc(Loc);
  if (SM.isWrittenInScratchSpace(SpellingLoc) ||
      SM.isInSystemHeader(SpellingLoc) || SM.isInSystemHeader(Loc))
    return {};
  // A token written in the body of a macro is reported once, in the macro,
  // instead of at every expansion, also one given there as an argument of
  // another macro, `DDeclare(Wrong)` in the body of `DFields`, which isn't
  // written where it is expanded.
  if (Loc.isMacroID() && SpellingLoc != SM.getFileLoc(Loc))
    return SpellingLoc;
  return Loc;
}

SourceLocation reportLocationIn(SourceLocation Loc, SourceLocation End,
                                const SourceManager &SM) {
  if (Loc.isMacroID() && !SM.isMacroArgExpansion(Loc) && End.isValid() &&
      End.isFileID())
    return reportLocation(SM.getExpansionLoc(Loc), SM);
  return reportLocation(Loc, SM);
}

SourceLocation nameLocation(const NamedDecl *D, const SourceManager &SM) {
  return reportLocation(D->getLocation(), SM);
}

bool isInExternalSpecialization(const Decl *D, const SourceManager &SM) {
  // A template of a system header, also redeclared elsewhere before it is
  // specialized, `namespace std { template <class T> struct tuple_size; }`.
  auto IsExternal = [&](const RedeclarableTemplateDecl *Template) {
    return llvm::any_of(Template->redecls(), [&](const Decl *Redeclaration) {
      return SM.isInSystemHeader(Redeclaration->getLocation());
    });
  };
  // A specialization of a variable template of a system header is named by
  // it, `std::ranges::enable_borrowed_range<CRange>`.
  if (const auto *Variable = dyn_cast<VarTemplateSpecializationDecl>(D);
      Variable && IsExternal(Variable->getSpecializedTemplate()))
    return true;
  for (const DeclContext *Context = D->getDeclContext(); Context;
       Context = Context->getParent()) {
    // What a function declares in its body is named here, not by the
    // external template, and so are its parameters, which the specialization
    // names itself.
    if (Context->isFunctionOrMethod())
      return false;
    const auto *Specialization =
        dyn_cast<ClassTemplateSpecializationDecl>(Context);
    if (Specialization && IsExternal(Specialization->getSpecializedTemplate()))
      return true;
  }
  return false;
}

llvm::SmallVector<const CXXRecordDecl *, 4>
instancesOf(const CXXRecordDecl *Pattern, int Depth) {
  llvm::SmallVector<const CXXRecordDecl *, 4> Instances;
  if (!Pattern || Depth > 8)
    return Instances;
  // A local class of a function template, `struct CLocal` in `fg_Run<int>`.
  if (Pattern->getDeclContext()->isFunctionOrMethod()) {
    for (const Decl *Instance : localInstances(Pattern))
      Instances.push_back(cast<CXXRecordDecl>(Instance));
    return Instances;
  }
  if (const auto *Partial =
          dyn_cast<ClassTemplatePartialSpecializationDecl>(Pattern)) {
    for (const ClassTemplateSpecializationDecl *Instance :
         Partial->getSpecializedTemplate()->specializations())
      if (const auto *From = Instance->getSpecializedTemplateOrPartial()
                                 .dyn_cast<ClassTemplatePartialSpecializationDecl *>();
          From == Partial && !Instance->isExplicitSpecialization())
        Instances.push_back(Instance);
    // Also one of a member template of a class template, `template
    // <typename t_CType> struct TCInner<t_CType *>` in `TCOuter`, which each
    // instantiation of the class instantiates.
    if (const auto *Outer = dyn_cast<CXXRecordDecl>(Partial->getParent());
        Outer && Outer->isTemplated())
      for (const CXXRecordDecl *OuterInstance : instancesOf(Outer, Depth + 1))
        for (const Decl *Member : OuterInstance->decls())
          if (const auto *MemberTemplate = dyn_cast<ClassTemplateDecl>(Member);
              MemberTemplate &&
              MemberTemplate->getInstantiatedFromMemberTemplate() &&
              MemberTemplate->getInstantiatedFromMemberTemplate()
                      ->getCanonicalDecl() ==
                  Partial->getSpecializedTemplate()->getCanonicalDecl())
            for (const ClassTemplateSpecializationDecl *Instance :
                 MemberTemplate->specializations())
              if (const auto *From =
                      Instance->getSpecializedTemplateOrPartial()
                          .dyn_cast<ClassTemplatePartialSpecializationDecl *>();
                  From && From->getInstantiatedFromMember() &&
                  From->getInstantiatedFromMember()->getCanonicalDecl() ==
                      Partial->getCanonicalDecl() &&
                  !Instance->isExplicitSpecialization())
                Instances.push_back(Instance);
    return Instances;
  }
  // Of the template, not of its partial specializations, `TCBox<int *>` of
  // `TCBox<t_CType *>`, which are instances of those.
  auto OfTemplate = [](const ClassTemplateSpecializationDecl *Instance) {
    return !Instance->isExplicitSpecialization() &&
           isa<ClassTemplateDecl *>(
               Instance->getSpecializedTemplateOrPartial());
  };
  if (const ClassTemplateDecl *ClassTemplate =
          Pattern->getDescribedClassTemplate()) {
    for (const ClassTemplateSpecializationDecl *Instance :
         ClassTemplate->specializations())
      if (OfTemplate(Instance))
        Instances.push_back(Instance);
    // A member template of a class template is instantiated with each
    // instantiation of the class, `TCOuter<int>::TCInner`, whose
    // instantiations are of that one.
    if (const auto *Outer = dyn_cast<CXXRecordDecl>(Pattern->getParent()))
      for (const CXXRecordDecl *OuterInstance : instancesOf(Outer, Depth + 1))
        for (const NamedDecl *Found :
             OuterInstance->lookup(Pattern->getDeclName()))
          // Of any of its declarations, also one ahead of the definition,
          // `template <typename t_CValue> struct TCInner;`.
          if (const auto *Member = dyn_cast<ClassTemplateDecl>(Found);
              Member && Member->getInstantiatedFromMemberTemplate() &&
              Member->getInstantiatedFromMemberTemplate()->getCanonicalDecl() ==
                  ClassTemplate->getCanonicalDecl())
            for (const ClassTemplateSpecializationDecl *Instance :
                 Member->specializations())
              if (OfTemplate(Instance))
                Instances.push_back(Instance);
    return Instances;
  }
  // A member class, found by its name, or among the members when it has none,
  // like an anonymous union, `union { t_CTraits::CPointer m_pValue; };`.
  if (const auto *Outer = dyn_cast<CXXRecordDecl>(Pattern->getParent()))
    for (const CXXRecordDecl *OuterInstance : instancesOf(Outer, Depth + 1)) {
      auto Add = [&](const Decl *Found) {
        if (const auto *Member = dyn_cast<CXXRecordDecl>(Found);
            Member && Member->getInstantiatedFromMemberClass() &&
            Member->getInstantiatedFromMemberClass()->getCanonicalDecl() ==
                Pattern->getCanonicalDecl())
          Instances.push_back(Member);
      };
      if (Pattern->getDeclName())
        for (const NamedDecl *Found :
             OuterInstance->lookup(Pattern->getDeclName()))
          Add(Found);
      else
        for (const Decl *Found : OuterInstance->decls())
          Add(Found);
    }
  return Instances;
}

/// The instantiations of \p Function, a function in a template: of its
/// template, of the member function of each instantiation of the class it is
/// in, or of a lambda, the call operator of the lambda each instantiation of
/// the function it is in makes, also of a generic one, its specializations.
llvm::SmallVector<const FunctionDecl *, 4>
functionInstances(const FunctionDecl *Function, int Depth) {
  llvm::SmallVector<const FunctionDecl *, 4> Functions;
  if (!Function || Depth > 8)
    return Functions;
  // Of a lambda in a template; one in other code is instantiated by itself.
  const CXXRecordDecl *Class = isLambdaCallOperator(Function)
                                   ? cast<CXXMethodDecl>(Function)->getParent()
                                   : nullptr;
  const auto *Outer =
      Class ? dyn_cast_or_null<FunctionDecl>(Class->getParentFunctionOrMethod())
            : nullptr;
  if (Outer && Outer->isTemplated()) {
    const bool Generic = Function->getDescribedFunctionTemplate() != nullptr;
    for (const FunctionDecl *OuterInstance : functionInstances(Outer, Depth + 1)) {
      llvm::SmallPtrSet<const Stmt *, 32> Visited;
      std::function<void(const Stmt *)> Find = [&](const Stmt *S) {
        if (!S || !Visited.insert(S).second)
          return;
        if (const auto *Lambda = dyn_cast<LambdaExpr>(S);
            Lambda && Lambda->getLambdaClass()->getLocation() ==
                          Class->getLocation()) {
          if (!Generic)
            Functions.push_back(Lambda->getCallOperator());
          else if (const FunctionTemplateDecl *Template =
                       Lambda->getDependentCallOperator())
            for (const FunctionDecl *Instance : Template->specializations())
              Functions.push_back(Instance);
          return;
        }
        for (const Stmt *Child : childrenOnce(S))
          Find(Child);
      };
      Find(OuterInstance->getBody());
      if (const auto *Constructor = dyn_cast<CXXConstructorDecl>(OuterInstance))
        for (const CXXCtorInitializer *Initializer : Constructor->inits())
          Find(Initializer->getInit());
    }
    return Functions;
  }
  if (const FunctionTemplateDecl *Template =
          Function->getDescribedFunctionTemplate()) {
    for (const FunctionDecl *Instance : Template->specializations())
      Functions.push_back(Instance);
    // A member template of a class template, `TCOuter<t_COuter>::f_Use`, is
    // instantiated with each instantiation of the class, whose member
    // template has the instantiations.
    if (const auto *Method = dyn_cast<CXXMethodDecl>(Function))
      for (const CXXRecordDecl *Class : instancesOf(Method->getParent()))
        for (const Decl *Member : Class->decls())
          if (const auto *MemberTemplate = dyn_cast<FunctionTemplateDecl>(Member);
              MemberTemplate &&
              MemberTemplate->getInstantiatedFromMemberTemplate() &&
              MemberTemplate->getInstantiatedFromMemberTemplate()
                      ->getCanonicalDecl() == Template->getCanonicalDecl())
            for (const FunctionDecl *Instance :
                 MemberTemplate->specializations())
              Functions.push_back(Instance);
    // So is a friend template defined in a class template, `template
    // <typename tf_CValue> friend void fg_Run(TCBox _Box, tf_CValue _Value)
    // {...}`.
    if (const auto *Class =
            dyn_cast<CXXRecordDecl>(Function->getLexicalDeclContext());
        Class && Template->getFriendObjectKind() != Decl::FOK_None)
      for (const CXXRecordDecl *Instance : instancesOf(Class))
        // Of those defined, not one only declared, `TCBox<int> *`, which has
        // no friends.
        for (const FriendDecl *Friend :
             Instance->hasDefinition() ? Instance->getDefinition()->friends()
                                       : CXXRecordDecl::friend_range(
                                             CXXRecordDecl::friend_iterator(),
                                             CXXRecordDecl::friend_iterator()))
          if (const auto *FriendTemplate =
                  dyn_cast_or_null<FunctionTemplateDecl>(
                      Friend->getFriendDecl());
              FriendTemplate &&
              FriendTemplate->getInstantiatedFromMemberTemplate() &&
              FriendTemplate->getInstantiatedFromMemberTemplate()
                      ->getCanonicalDecl() == Template->getCanonicalDecl())
            for (const FunctionDecl *Instance :
                 FriendTemplate->specializations()) {
              const FunctionDecl *Definition = Instance->getDefinition();
              Functions.push_back(Definition ? Definition : Instance);
            }
  } else if (const auto *Method = dyn_cast<CXXMethodDecl>(Function)) {
    // Also one defined outside of its class, `void TCBox<t_CType>::f_Run()
    // {...}`, whose instantiation is from the declaration in the class, and
    // has its body and parameters in its own definition.
    for (const CXXRecordDecl *Class : instancesOf(Method->getParent()))
      for (const CXXMethodDecl *Member : Class->methods())
        if (const FunctionDecl *From =
                Member->getInstantiatedFromMemberFunction();
            From && From->getCanonicalDecl() == Method->getCanonicalDecl()) {
          const FunctionDecl *Definition = Member->getDefinition();
          Functions.push_back(Definition ? Definition : Member);
        }
  }
  // And a friend defined in a class template, `friend void fg_Use(TCBox
  // _Box) {...}`, is instantiated with each instantiation of the class.
  else if (const auto *Class =
               dyn_cast<CXXRecordDecl>(Function->getLexicalDeclContext());
           Class && Function->getFriendObjectKind() != Decl::FOK_None)
    for (const CXXRecordDecl *Instance : instancesOf(Class))
      for (const FriendDecl *Friend :
           Instance->hasDefinition()
               ? Instance->getDefinition()->friends()
               : CXXRecordDecl::friend_range(CXXRecordDecl::friend_iterator(),
                                             CXXRecordDecl::friend_iterator()))
        if (const auto *Member =
                dyn_cast_or_null<FunctionDecl>(Friend->getFriendDecl()))
          if (const FunctionDecl *From =
                  Member->getInstantiatedFromDecl()
                      ? Member->getInstantiatedFromDecl()
                      : Member->getInstantiatedFromMemberFunction();
              From &&
              From->getCanonicalDecl() == Function->getCanonicalDecl()) {
            const FunctionDecl *Definition = Member->getDefinition();
            Functions.push_back(Definition ? Definition : Member);
          }
  return Functions;
}

/// Like onlyGivesAccess, of \p Function, also a static member function or a
/// free one, which changes nothing it isn't given, like a helper that gives
/// back what a pointer it is given points to, `return *_pValue;`.
static bool onlyGivesAccess(const FunctionDecl *Function, int Depth) {
  const FunctionDecl *Definition =
      Function ? Function->getDefinition() : nullptr;
  if (!Definition || !Definition->getBody() || Depth > 4 ||
      isa<CXXConstructorDecl, CXXDestructorDecl>(Definition))
    return false;
  struct COnly {
    llvm::DenseMap<const FunctionDecl *, bool> Decided;
  };
  auto &Decided = astCache<COnly>(Definition->getASTContext()).Decided;
  if (auto It = Decided.find(Definition); It != Decided.end())
    return It->second;
  // One that calls itself, also through others, is answered no meanwhile.
  Decided[Definition] = false;
  auto IsOwnLocal = [&](const Expr *E) {
    const auto *Ref = dyn_cast<DeclRefExpr>(E->IgnoreParenImpCasts());
    const auto *Local = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
    return Local && Local->hasLocalStorage() &&
           !Local->getType()->isReferenceType() &&
           Local->getDeclContext() == Definition;
  };
  // What a function is given that it can't change anything through.
  auto OnlyReads = [](QualType Type) {
    if (Type.isNull())
      return false;
    if (Type->isReferenceType() || Type->isPointerType())
      return Type->getPointeeType().isConstQualified();
    return !Type->isMemberPointerType();
  };
  // Not what fails a check by throwing, `DMibError(...)`, nor a branch of an
  // `if constexpr` that is discarded.
  std::function<bool(const Stmt *)> Changes = [&](const Stmt *S) {
    if (!S || isUnevaluatedNode(S) || isa<CXXThrowExpr>(S))
      return false;
    if (isa<CXXNewExpr, CXXDeleteExpr, LambdaExpr, AsmStmt, CoroutineBodyStmt>(
            S))
      return true;
    if (const auto *Binary = dyn_cast<BinaryOperator>(S);
        Binary && Binary->isAssignmentOp() && !IsOwnLocal(Binary->getLHS()))
      return true;
    if (const auto *Unary = dyn_cast<UnaryOperator>(S);
        Unary && Unary->isIncrementDecrementOp() &&
        !IsOwnLocal(Unary->getSubExpr()))
      return true;
    if (const auto *Construct = dyn_cast<CXXConstructExpr>(S);
        Construct && !Construct->getConstructor()->isTrivial())
      return true;
    if (const auto *Call = dyn_cast<CallExpr>(S)) {
      const FunctionDecl *Callee = Call->getDirectCallee();
      if (!Callee)
        return true;
      const auto *Member = dyn_cast<CXXMethodDecl>(Callee);
      const bool Object = Member && !Member->isStatic();
      if (Object && !Member->isConst() && !onlyGivesAccess(Member, Depth + 1))
        return true;
      // What it gives a function that changes nothing, also through it,
      // `TCEvalReturn<CType>::fs_Value(pRet)` of `return *_pValue;`, isn't
      // changed by it.
      if (!Object && onlyGivesAccess(Callee, Depth + 1))
        return llvm::any_of(keptChildren(S, Definition->getASTContext()),
                            Changes);
      const unsigned Offset = isa<CXXOperatorCallExpr>(Call) && Object ? 1 : 0;
      for (unsigned iArgument = Offset; iArgument < Call->getNumArgs();
           ++iArgument)
        if (!OnlyReads(iArgument - Offset < Callee->getNumParams()
                           ? Callee->getParamDecl(iArgument - Offset)->getType()
                           : Call->getArg(iArgument)->getType()))
          return true;
    }
    return llvm::any_of(keptChildren(S, Definition->getASTContext()), Changes);
  };
  const bool Only = !Changes(Definition->getBody());
  astCache<COnly>(Definition->getASTContext()).Decided[Definition] = Only;
  return Only;
}

bool onlyGivesAccess(const CXXMethodDecl *Method) {
  return onlyGivesAccess(Method, 0);
}

static bool
isTypeOfGivenParameter(const clang::Type *Member, bool Nested, int Depth,
                       const TemplateArgument **Forwarded = nullptr,
                       bool AnyArgument = false);

/// Whether \p Type gives a template parameter as it is, also through
/// transformations of it, `__remove_reference_t(t_CType)`, a reference to it,
/// `t_CType const &`, alias templates, the type of a variable declared with
/// one, `decltype(_Value)`, and the `type` of another template given one,
/// unlike `t_CType *`.
static bool givesParameter(QualType Type, bool Nested, int Depth) {
  for (int Step = 0; Step < 32 && !Type.isNull(); ++Step) {
    const clang::Type *Written = Type.getTypePtr();
    if (isa<TemplateTypeParmType>(Type.getCanonicalType().getTypePtr()))
      return true;
    // A transformation that keeps what it is given, `__remove_cvref(t_CType)`,
    // not one that makes another kind of type of it, `__add_pointer(t_CType)`
    // or `__underlying_type(t_CEnum)`.
    if (const auto *Transform = dyn_cast<UnaryTransformType>(Written)) {
      switch (Transform->getUTTKind()) {
      case UnaryTransformType::AddLvalueReference:
      case UnaryTransformType::AddRvalueReference:
      case UnaryTransformType::RemoveConst:
      case UnaryTransformType::RemoveCV:
      case UnaryTransformType::RemoveCVRef:
      case UnaryTransformType::RemoveReference:
      case UnaryTransformType::RemoveRestrict:
      case UnaryTransformType::RemoveVolatile:
        Type = Transform->getBaseType();
        continue;
      default:
        return false;
      }
    }
    // Also a reference to it, `t_CType const &`.
    else if (const auto *Reference = dyn_cast<ReferenceType>(Written))
      Type = Reference->getPointeeTypeAsWritten();
    else if (const auto *Alias = dyn_cast<TemplateSpecializationType>(Written);
             Alias && Alias->isTypeAlias())
      Type = Alias->getAliasedType();
    else if (const auto *Parens = dyn_cast<ParenType>(Written))
      Type = Parens->getInnerType();
    else if (const auto *Other = dyn_cast<TypedefType>(Written);
             Other && Other->isDependentType())
      Type = Other->desugar();
    else if (const auto *Substituted =
                 dyn_cast<SubstTemplateTypeParmType>(Written))
      Type = Substituted->getReplacementType();
    // The type a variable is declared with, `decltype(_Value)` of
    // `tf_CValue &&_Value`.
    else if (const auto *Decltype = dyn_cast<DecltypeType>(Written);
             Decltype && isa<DeclRefExpr>(Decltype->getUnderlyingExpr()) &&
             isa<VarDecl>(
                 cast<DeclRefExpr>(Decltype->getUnderlyingExpr())->getDecl()))
      Type = cast<DeclRefExpr>(Decltype->getUnderlyingExpr())
                 ->getDecl()
                 ->getType();
    else
      return Depth < 8 && isTypeOfGivenParameter(Written, Nested, Depth + 1);
  }
  return false;
}

namespace {
/// The parameters of the function types written in a type, in order.
class CFunctionTypeParameters
    : public OncePerTypeVisitor<CFunctionTypeParameters> {
public:
  bool VisitFunctionProtoTypeLoc(FunctionProtoTypeLoc Function) {
    llvm::SmallVector<const ParmVarDecl *, 4> &Parameters =
        Functions.emplace_back();
    for (const ParmVarDecl *Parameter : Function.getParams())
      Parameters.push_back(Parameter);
    Types.push_back(Function.getTypePtr());
    return true;
  }
  /// The parameters of each function type, in order.
  llvm::SmallVector<llvm::SmallVector<const ParmVarDecl *, 4>, 2> Functions;
  /// And the type of each, which is one for those written the same.
  llvm::SmallVector<const clang::Type *, 2> Types;
};
} // namespace

/// Adds to \p Instances what the parameter at \p iPosition of \p Pattern, a
/// function type as written, is in \p Instantiated, the parameters of the
/// function type made of it. Around the packs it expands, `void (typename
/// tf_CTraits::CBool o_bReady, tfp_CArgs ...p_Args)`, those before them are
/// found by their place from the start, and those after them from the end,
/// and a pack, the only one, is the parameters it expands to, of which there
/// may be another number. Not what is between packs, whose places depend on
/// how many each expands to.
static void
addParameterInstances(llvm::ArrayRef<const ParmVarDecl *> Pattern,
                      llvm::ArrayRef<const ParmVarDecl *> Instantiated,
                      size_t iPosition,
                      llvm::SmallVectorImpl<const ParmVarDecl *> &Instances) {
  auto IsPack = [](const ParmVarDecl *Each) { return Each->isParameterPack(); };
  const size_t nPacks = llvm::count_if(Pattern, IsPack);
  if (!nPacks) {
    if (Instantiated.size() == Pattern.size())
      Instances.push_back(Instantiated[iPosition]);
    return;
  }
  if (Instantiated.size() + nPacks < Pattern.size())
    return;
  const size_t iFirstPack = llvm::find_if(Pattern, IsPack) - Pattern.begin();
  const size_t iLastPack =
      Pattern.rend() - llvm::find_if(llvm::reverse(Pattern), IsPack) - 1;
  // How many the packs expand to.
  const size_t nExpanded = Instantiated.size() + nPacks - Pattern.size();
  if (iPosition < iFirstPack)
    Instances.push_back(Instantiated[iPosition]);
  else if (iPosition > iLastPack)
    Instances.push_back(Instantiated[iPosition + nExpanded - nPacks]);
  else if (nPacks == 1)
    for (size_t iMade = iPosition; iMade < iPosition + nExpanded; ++iMade)
      Instances.push_back(Instantiated[iMade]);
}

llvm::SmallVector<const ParmVarDecl *, 4>
functionTypeParameterInstances(const ParmVarDecl *Parameter,
                               ASTContext &Context) {
  llvm::SmallVector<const ParmVarDecl *, 4> Instances;
  if (const auto *Function =
          dyn_cast<FunctionDecl>(Parameter->getDeclContext());
      Function && llvm::is_contained(Function->parameters(), Parameter))
    return Instances;
  // The declaration whose type it is in.
  const Decl *Owner = nullptr;
  DynTypedNode Node = DynTypedNode::create(*Parameter);
  // Also a class whose base it is in, `struct TCHandler :
  // TCFunction<void (typename t_CTraits::CBool _bReady)>`, by the place of
  // the base.
  std::optional<unsigned> iBase;
  // Also a cast whose type it is in, `reinterpret_cast<TCFunctionNoAlloc<void
  // (typename tf_CTraits::CBool o_bReady)> *>(_pContext)`, or the template
  // arguments of a name, `fg_Construct<TCFunction<void (typename
  // tf_CTraits::CBool o_bReady)>>()`, of the instantiations of the function
  // it is in, by where they are written. Also a temporary made of it,
  // `TCFunction<void (typename tf_CTraits::CBool o_bReady)>{}`.
  const Expr *OwnerCast = nullptr;
  auto CastHas = [&](const Stmt *S) {
    CFunctionTypeParameters Visitor;
    llvm::ArrayRef<TemplateArgumentLoc> Arguments;
    if (const auto *Cast = dyn_cast<ExplicitCastExpr>(S)) {
      if (const TypeSourceInfo *Written = Cast->getTypeInfoAsWritten())
        Visitor.TraverseTypeLoc(Written->getTypeLoc());
    } else if (const auto *Temporary = dyn_cast<CXXTemporaryObjectExpr>(S)) {
      if (const TypeSourceInfo *Written = Temporary->getTypeSourceInfo())
        Visitor.TraverseTypeLoc(Written->getTypeLoc());
    } else if (const auto *Unresolved =
                   dyn_cast<CXXUnresolvedConstructExpr>(S)) {
      if (const TypeSourceInfo *Written = Unresolved->getTypeSourceInfo())
        Visitor.TraverseTypeLoc(Written->getTypeLoc());
    } else if (const auto *Size = dyn_cast<UnaryExprOrTypeTraitExpr>(S)) {
      // Also the type of `sizeof` or `alignof`, `sizeof(TCFunction<void
      // (typename tf_CTraits::CBool _bReady)>)`.
      if (Size->isArgumentType())
        if (const TypeSourceInfo *Written = Size->getArgumentTypeInfo())
          Visitor.TraverseTypeLoc(Written->getTypeLoc());
    } else if (const auto *New = dyn_cast<CXXNewExpr>(S)) {
      // Also what a `new` makes, `new (_pStorage) TCFunction<void (typename
      // tf_CTraits::CType _Result)>`.
      if (const TypeSourceInfo *Written = New->getAllocatedTypeSourceInfo())
        Visitor.TraverseTypeLoc(Written->getTypeLoc());
    } else if (const auto *Ref = dyn_cast<DeclRefExpr>(S))
      Arguments = Ref->template_arguments();
    else if (const auto *Overloaded = dyn_cast<OverloadExpr>(S))
      Arguments = Overloaded->template_arguments();
    else if (const auto *Member = dyn_cast<MemberExpr>(S))
      Arguments = Member->template_arguments();
    else if (const auto *Dependent = dyn_cast<CXXDependentScopeMemberExpr>(S))
      Arguments = Dependent->template_arguments();
    for (const TemplateArgumentLoc &Argument : Arguments)
      if (Argument.getArgument().getKind() == TemplateArgument::Type)
        if (const TypeSourceInfo *Written = Argument.getTypeSourceInfo())
          Visitor.TraverseTypeLoc(Written->getTypeLoc());
    return Visitor.Functions;
  };
  for (int Depth = 0; Depth < 64 && !Owner; ++Depth) {
    const auto Parents = Context.getParentMapContext().getParents(Node);
    if (Parents.empty())
      break;
    Node = Parents[0];
    if (const auto *Cast = Node.get<Expr>(); Cast && !OwnerCast)
      if (llvm::any_of(CastHas(Cast), [&](const auto &Function) {
            return llvm::is_contained(Function, Parameter);
          }))
        OwnerCast = Cast;
    if (const auto *Function = Node.get<FunctionDecl>();
        Function && OwnerCast) {
      // Of its template, of the class it is in, also a member template of
      // one, or of the function a lambda is in.
      const llvm::SmallVector<const FunctionDecl *, 4> Instantiated =
          functionInstances(Function);
      const auto PatternFunctions = CastHas(OwnerCast);
      size_t iFunction = 0;
      for (; iFunction < PatternFunctions.size(); ++iFunction)
        if (llvm::is_contained(PatternFunctions[iFunction], Parameter))
          break;
      const size_t iPosition =
          llvm::find(PatternFunctions[iFunction], Parameter) -
          PatternFunctions[iFunction].begin();
      for (const FunctionDecl *Instance : Instantiated) {
        const FunctionDecl *Definition = Instance->getDefinition();
        if (!Definition || !Definition->getBody())
          continue;
        // What it makes of the one written, at its place: the same cast, a
        // temporary of the type, which one depends on what the type is, or a
        // name with those template arguments, resolved.
        const Expr *Made = nullptr;
        auto KindOf = [](const Expr *E) -> std::optional<Stmt::StmtClass> {
          if (isa<CXXFunctionalCastExpr, CXXTemporaryObjectExpr,
                  CXXUnresolvedConstructExpr>(E))
            return Stmt::CXXTemporaryObjectExprClass;
          if (isa<ExplicitCastExpr, CXXNewExpr, UnaryExprOrTypeTraitExpr>(E))
            return E->getStmtClass();
          return std::nullopt;
        };
        std::function<void(const Stmt *)> Find = [&](const Stmt *S) {
          if (!S || Made)
            return;
          if (const auto *Cast = dyn_cast<Expr>(S);
              Cast && Cast->getBeginLoc() == OwnerCast->getBeginLoc() &&
              KindOf(Cast) == KindOf(OwnerCast) && !CastHas(Cast).empty()) {
            Made = Cast;
            return;
          }
          // The operand of a co_await once, which the expressions that make
          // and call its awaiter have too.
          for (const Stmt *Child : childrenOnce(S))
            Find(Child);
        };
        Find(Definition->getBody());
        // Also in what a constructor initializes, `m_fCallback(TCFunction<void
        // (typename t_CTraits::CBool _bReady)>{})`.
        if (const auto *Constructor = dyn_cast<CXXConstructorDecl>(Definition))
          for (const CXXCtorInitializer *Initializer : Constructor->inits())
            Find(Initializer->getInit());
        if (!Made)
          continue;
        const auto MadeFunctions = CastHas(Made);
        if (MadeFunctions.size() != PatternFunctions.size())
          continue;
        addParameterInstances(PatternFunctions[iFunction],
                              MadeFunctions[iFunction], iPosition, Instances);
      }
      return Instances;
    }
    if (const auto *Found = Node.get<Decl>(); Found && !OwnerCast)
      Owner = isa<DeclaratorDecl, TypedefNameDecl>(Found) ? Found : nullptr;
    if (const auto *Record = Node.get<CXXRecordDecl>();
        Record && !Owner && Record->hasDefinition() &&
        Record->getDefinition() == Record)
      for (unsigned iEach = 0; iEach < Record->getNumBases(); ++iEach) {
        const TypeSourceInfo *Base =
            (Record->bases_begin() + iEach)->getTypeSourceInfo();
        CFunctionTypeParameters Visitor;
        if (Base)
          Visitor.TraverseTypeLoc(Base->getTypeLoc());
        if (llvm::any_of(Visitor.Functions, [&](const auto &Function) {
              return llvm::is_contained(Function, Parameter);
            })) {
          Owner = Record;
          iBase = iEach;
          break;
        }
      }
    if (Node.get<Decl>() && !Owner && !OwnerCast)
      break;
  }
  auto TypeOf = [&](const Decl *Declaration) -> const TypeSourceInfo * {
    if (const auto *Record = dyn_cast<CXXRecordDecl>(Declaration)) {
      Record = Record->getDefinition();
      return iBase && Record && *iBase < Record->getNumBases()
                 ? (Record->bases_begin() + *iBase)->getTypeSourceInfo()
                 : nullptr;
    }
    if (const auto *Declarator = dyn_cast<DeclaratorDecl>(Declaration))
      return Declarator->getTypeSourceInfo();
    return cast<TypedefNameDecl>(Declaration)->getTypeSourceInfo();
  };
  auto FunctionsOf = [&](const Decl *Declaration) {
    CFunctionTypeParameters Visitor;
    if (const TypeSourceInfo *Type = TypeOf(Declaration))
      Visitor.TraverseTypeLoc(Type->getTypeLoc());
    return Visitor.Functions;
  };
  if (!Owner)
    return Instances;
  // The function type it is of, and its place in it.
  const auto PatternFunctions = FunctionsOf(Owner);
  size_t iFunction = 0;
  for (; iFunction < PatternFunctions.size(); ++iFunction)
    if (llvm::is_contained(PatternFunctions[iFunction], Parameter))
      break;
  if (iFunction == PatternFunctions.size())
    return Instances;
  const llvm::SmallVector<const ParmVarDecl *, 4> &Pattern =
      PatternFunctions[iFunction];
  const auto *Position = llvm::find(Pattern, Parameter);
  llvm::SmallVector<const Decl *, 4> Owners;
  // Also a variable template, `TCFunction<void (typename t_CTraits::CPointer
  // _pValue)> g_fCallback`, also a member of a class template, of its
  // specializations: those of the primary template, or of \p Partial, the
  // partial specialization they are instantiated from.
  auto AddSpecializations =
      [&](const VarTemplateDecl *Template,
          const VarTemplatePartialSpecializationDecl *Partial = nullptr) {
        for (const VarTemplateSpecializationDecl *Specialization :
             Template->specializations()) {
          const auto From = Specialization->getSpecializedTemplateOrPartial();
          const auto *FromPartial =
              dyn_cast_if_present<VarTemplatePartialSpecializationDecl *>(From);
          if (isTemplateInstantiation(
                  Specialization->getTemplateSpecializationKind()) &&
              (Partial ? FromPartial == Partial : !FromPartial))
            Owners.push_back(Specialization);
        }
      };
  // And a function template, of its return type, `TCFunction<void (typename
  // tf_CTraits::CBool _bReady)> fg_Make()`, of its instantiations.
  auto AddFunctionSpecializations = [&](const FunctionTemplateDecl *Template) {
    for (const FunctionDecl *Specialization : Template->specializations())
      if (isTemplateInstantiation(
              Specialization->getTemplateSpecializationKind()))
        Owners.push_back(Specialization);
  };
  const auto *Variable = dyn_cast<VarDecl>(Owner);
  if (const auto *Partial =
          dyn_cast<VarTemplatePartialSpecializationDecl>(Owner))
    AddSpecializations(Partial->getSpecializedTemplate(), Partial);
  else if (const VarTemplateDecl *Template =
               Variable ? Variable->getDescribedVarTemplate() : nullptr)
    AddSpecializations(Template);
  const auto *OwnerFunction = dyn_cast<FunctionDecl>(Owner);
  if (const FunctionTemplateDecl *Template =
          OwnerFunction ? OwnerFunction->getDescribedFunctionTemplate()
                        : nullptr)
    AddFunctionSpecializations(Template);
  if (iBase) {
    for (const CXXRecordDecl *Instance :
         instancesOf(cast<CXXRecordDecl>(Owner)))
      if (Instance->hasDefinition())
        Owners.push_back(Instance->getDefinition());
  } else if (Owner->getParentFunctionOrMethod()) {
    Owners = localInstances(Owner);
  } else if (const auto *Class =
                 dyn_cast<CXXRecordDecl>(Owner->getDeclContext())) {
    for (const CXXRecordDecl *Instance : instancesOf(Class))
      if (Instance->hasDefinition())
        for (const NamedDecl *Found :
             Instance->lookup(cast<NamedDecl>(Owner)->getDeclName())) {
          // The one instantiated from it, not another overload, `f_Make()`
          // beside `f_Make(int _Tag)`.
          auto Same = [](const Decl *Instantiated, const Decl *From) {
            return Instantiated && From &&
                   Instantiated->getCanonicalDecl() == From->getCanonicalDecl();
          };
          if (Found->getKind() != Owner->getKind()) {
            if (const auto *Template = dyn_cast<VarTemplateDecl>(Found);
                Template && Variable &&
                Same(Template->getInstantiatedFromMemberTemplate(),
                     Variable->getDescribedVarTemplate()))
              AddSpecializations(Template);
            else if (const auto *Template =
                         dyn_cast<FunctionTemplateDecl>(Found);
                     Template && OwnerFunction &&
                     Same(Template->getInstantiatedFromMemberTemplate(),
                          OwnerFunction->getDescribedFunctionTemplate()))
              AddFunctionSpecializations(Template);
          } else if (const auto *Method = dyn_cast<FunctionDecl>(Found)) {
            if (Same(Method->getInstantiatedFromMemberFunction(), Owner))
              Owners.push_back(Found);
          } else if (const auto *Member = dyn_cast<VarDecl>(Found)) {
            if (Same(Member->getInstantiatedFromStaticDataMember(), Owner))
              Owners.push_back(Found);
          } else
            Owners.push_back(Found);
        }
  }
  // A pack, `typename tp_CTraits::CBool... p_bFlags`, is the parameters it
  // expands to, as addParameterInstances tells.
  const size_t iPosition = Position - Pattern.begin();
  auto AddFrom =
      [&](const llvm::SmallVector<const ParmVarDecl *, 4> &Instantiated) {
        addParameterInstances(Pattern, Instantiated, iPosition, Instances);
      };
  for (const Decl *Instance : Owners) {
    if (!isa<DeclaratorDecl, TypedefNameDecl, CXXRecordDecl>(Instance))
      continue;
    const auto InstanceFunctions = FunctionsOf(Instance);
    if (InstanceFunctions.size() == PatternFunctions.size()) {
      AddFrom(InstanceFunctions[iFunction]);
      continue;
    }
    // The one function type of a signature expanded from a pack,
    // `TCFunction<void (typename tp_CTraits::CValue _Value)...>`, is each of
    // the function types it expands to.
    if (PatternFunctions.size() == 1)
      for (const auto &Instantiated : InstanceFunctions)
        AddFrom(Instantiated);
  }
  return Instances;
}

namespace {
/// The function types in a type, in order, as the types written in a
/// declaration have them: not in what a template parameter is replaced
/// with, which the declaration names by the parameter.
class CFunctionTypes : public OncePerTypeVisitor<CFunctionTypes> {
public:
  bool TraverseSubstTemplateTypeParmType(SubstTemplateTypeParmType *,
                                         bool = false) {
    return true;
  }
  // Also in the arguments of a specialization that is canonical, like a
  // template argument has, `TCFunction<void (bool)>`.
  bool TraverseRecordType(RecordType *Record, bool = false) {
    if (const auto *Specialization =
            dyn_cast<ClassTemplateSpecializationDecl>(Record->getDecl()))
      for (const TemplateArgument &Argument :
           Specialization->getTemplateArgs().asArray())
        if (!TraverseTemplateArgument(Argument))
          return false;
    return true;
  }
  bool VisitFunctionProtoType(FunctionProtoType *Function) {
    Functions.push_back(Function);
    return true;
  }
  llvm::SmallVector<const FunctionProtoType *, 2> Functions;
};

/// The alias template \p Template is of as written: a member one of a class
/// template that it is an instantiation of, `TCBox<CTraits>::TFCallback` of
/// `TCBox`'s.
const TypeAliasTemplateDecl *
writtenAlias(const TypeAliasTemplateDecl *Template) {
  for (int Depth = 0; Depth < 16; ++Depth) {
    const TypeAliasTemplateDecl *From =
        Template->getInstantiatedFromMemberTemplate();
    if (!From)
      break;
    Template = From;
  }
  return Template->getCanonicalDecl();
}

/// The uses of the alias templates of a translation unit, by what each is
/// an alias of, which a use of one makes, unlike other templates, of the
/// types only.
struct CAliasUses {
  llvm::DenseMap<const TypeAliasTemplateDecl *, llvm::SmallVector<QualType, 2>>
      Uses;
  bool Collected = false;
};

class CAliasUseCollector : public OncePerTypeVisitor<CAliasUseCollector> {
public:
  explicit CAliasUseCollector(CAliasUses &Uses) : Uses(Uses) {}
  bool shouldVisitTemplateInstantiations() const { return true; }

  // Also of what it is an alias of in turn, `TFCallback<CTraits>` of
  // `TFAlias<CTraits>` with `template <...> using TFAlias =
  // TFCallback<t_CTraits>;`, which the substitution makes.
  bool VisitTemplateSpecializationTypeLoc(TemplateSpecializationTypeLoc Use) {
    const auto *Specialization = Use.getTypePtr();
    for (int Depth = 0; Specialization && Depth < 16; ++Depth) {
      if (!Specialization->isTypeAlias() || Specialization->isDependentType())
        return true;
      const QualType Aliased = Specialization->getAliasedType();
      if (const auto *Template = dyn_cast_or_null<TypeAliasTemplateDecl>(
              Specialization->getTemplateName().getAsTemplateDecl())) {
        llvm::SmallVector<QualType, 2> &Of = Uses.Uses[writtenAlias(Template)];
        if (!llvm::is_contained(Of, Aliased))
          Of.push_back(Aliased);
      }
      Specialization = Aliased->getAs<TemplateSpecializationType>();
    }
    return true;
  }

private:
  CAliasUses &Uses;
};
} // namespace

llvm::SmallVector<QualType, 4>
functionTypeParameterTypes(const ParmVarDecl *Parameter, ASTContext &Context,
                           bool Adjusted) {
  llvm::SmallVector<QualType, 4> Types;
  for (const ParmVarDecl *Instance :
       functionTypeParameterInstances(Parameter, Context))
    Types.push_back(Adjusted ? Instance->getType()
                             : Instance->getOriginalType());
  // Also of the uses of an alias template whose type it is in, `template
  // <typename t_CTraits> using TCCallback = TCFunction<void(typename
  // t_CTraits::CBool _bReady)>;`, by the type at its place.
  // Also of the default argument of a template parameter, `typename
  // tf_FCallback = TCFunction<void (typename tf_CTraits::CBool _bReady)>`, by
  // the argument at its place of each specialization.
  const TypeAliasDecl *Alias = nullptr;
  const TemplateTypeParmDecl *Defaulted = nullptr;
  DynTypedNode Node = DynTypedNode::create(*Parameter);
  for (int Depth = 0; Depth < 64; ++Depth) {
    const auto Parents = Context.getParentMapContext().getParents(Node);
    if (Parents.empty())
      break;
    Node = Parents[0];
    if (const auto *Found = Node.get<Decl>()) {
      Alias = dyn_cast<TypeAliasDecl>(Found);
      Defaulted = dyn_cast<TemplateTypeParmDecl>(Found);
      break;
    }
  }
  const TypeAliasTemplateDecl *Template =
      Alias ? Alias->getDescribedAliasTemplate() : nullptr;
  const TypeSourceInfo *WrittenType =
      Template ? Alias->getTypeSourceInfo()
      : Defaulted && Defaulted->hasDefaultArgument()
          ? Defaulted->getDefaultArgument().getTypeSourceInfo()
          : nullptr;
  if (!WrittenType)
    return Types;
  CFunctionTypeParameters Written;
  Written.TraverseTypeLoc(WrittenType->getTypeLoc());
  size_t iFunction = 0;
  for (; iFunction < Written.Functions.size(); ++iFunction)
    if (llvm::is_contained(Written.Functions[iFunction], Parameter))
      break;
  if (iFunction == Written.Functions.size())
    return Types;
  const auto &Pattern = Written.Functions[iFunction];
  // With one pack, which can expand to another number of them, those before
  // it by their place from the start, those after it from the end, and the
  // pack those it expands to.
  const size_t iPosition = llvm::find(Pattern, Parameter) - Pattern.begin();
  const auto *Pack = llvm::find_if(
      Pattern, [](const ParmVarDecl *Each) { return Each->isParameterPack(); });
  if (Pack != Pattern.end() &&
      std::find_if(std::next(Pack), Pattern.end(), [](const ParmVarDecl *Each) {
        return Each->isParameterPack();
      }) != Pattern.end())
    return Types;
  const bool Packed = Pack != Pattern.end();
  const size_t iPack = Pack - Pattern.begin();
  llvm::SmallVector<QualType, 2> Made;
  if (Template) {
    CAliasUses &Uses = astCache<CAliasUses>(Context);
    if (!Uses.Collected) {
      Uses.Collected = true;
      // Of the whole translation unit, also the headers the plugin doesn't
      // report in, which it leaves out of the traversal scope: the uses
      // there tell too.
      CAliasUseCollector Collector(Uses);
      for (Decl *Declared : Context.getTranslationUnitDecl()->decls())
        Collector.TraverseDecl(Declared);
    }
    llvm::append_range(Made, Uses.Uses.lookup(writtenAlias(Template)));
  } else {
    // The template whose parameter it is, of a function or a class.
    const unsigned Index = Defaulted->getIndex();
    const TemplateDecl *Owner = nullptr;
    if (const DynTypedNodeList Parents =
            Context.getParentMapContext().getParents(*Defaulted);
        !Parents.empty())
      Owner = Parents[0].get<TemplateDecl>();
    // Also through what it is declared in, the templated function or class.
    if (!Owner) {
      const DeclContext *In = Defaulted->getDeclContext();
      if (const auto *Function = dyn_cast_or_null<FunctionDecl>(In))
        Owner = Function->getDescribedFunctionTemplate();
      else if (const auto *Record = dyn_cast_or_null<CXXRecordDecl>(In))
        Owner = Record->getDescribedClassTemplate();
    }
    // Only where the default was used, or what is the same: what it is made
    // of with the arguments of the specialization, also what they hold,
    // `typename tf_CTraits::CType`, is the argument, not another a class is
    // given, `TCBox<CTraits, TCFunction<void (bool _bReady)>>`, or a call
    // deduces it as, `fg_Run<CTraits>(TCFunction<void (bool)>{})`.
    const unsigned Level = Defaulted->getDepth();
    const TemplateArgumentList *Arguments = nullptr;
    // Those of the classes a member template is in, `TCBox<CTraits>` of
    // `template <typename t_CTraits> struct TCBox { template <...> void
    // f_Run(); };`, by their depth.
    llvm::SmallVector<const TemplateArgumentList *, 2> Outer;
    std::function<bool(QualType, QualType, int)> Matches;
    std::function<bool(QualType, const TemplateSpecializationType *, int)>
        MatchesList;
    // Each of Resolve, Substitute and Sugared makes what it makes of a type
    // at a depth once with the arguments of a specialization: a type that
    // names another twice, `F (*)(F, F)`, would be made again through each
    // path to it, a number of times exponential in its depth.
    using CMadeTypes = llvm::DenseMap<std::pair<void *, int>, QualType>;
    CMadeTypes Resolved, Substituted, SugaredTypes;
    std::function<QualType(QualType, int)> ResolveOnce, SubstituteOnce,
        SugaredOnce;
    auto Once = [](CMadeTypes &Made, QualType Written, int Depth,
                   const std::function<QualType(QualType, int)> &Make) {
      const auto Key = std::make_pair(Written.getAsOpaquePtr(), Depth);
      if (auto It = Made.find(Key); It != Made.end())
        return It->second;
      const QualType Result = Make(Written, Depth);
      Made[Key] = Result;
      return Result;
    };
    auto Resolve = [&](QualType Written, int Depth) {
      return Once(Resolved, Written, Depth, ResolveOnce);
    };
    auto Substitute = [&](QualType Written, int Depth) {
      return Once(Substituted, Written, Depth, SubstituteOnce);
    };
    auto Sugared = [&](QualType Written, int Depth) {
      return Once(SugaredTypes, Written, Depth, SugaredOnce);
    };
    // What a dependent type is with the arguments: a parameter, or a type a
    // class it is made of holds.
    ResolveOnce = [&](QualType Written, int Depth) {
      Written = Written.getCanonicalType();
      if (Depth > 16)
        return QualType();
      if (!Written->isDependentType())
        return Written;
      if (const auto *Parameter = Written->getAs<TemplateTypeParmType>()) {
        const TemplateArgumentList *Of = Parameter->getDepth() == Level
                                             ? Arguments
                                         : Parameter->getDepth() < Outer.size()
                                             ? Outer[Parameter->getDepth()]
                                             : nullptr;
        if (!Of || Parameter->getIndex() >= Of->size() ||
            Of->get(Parameter->getIndex()).getKind() != TemplateArgument::Type)
          return QualType();
        return Context.getQualifiedType(
            Of->get(Parameter->getIndex()).getAsType(),
            Written.getLocalQualifiers());
      }
      if (const auto *List = Written->getAs<TemplateSpecializationType>()) {
        const auto *Template = dyn_cast_or_null<ClassTemplateDecl>(
            List->getTemplateName().getAsTemplateDecl());
        if (!Template)
          return QualType();
        for (const ClassTemplateSpecializationDecl *Specialization :
             Template->specializations()) {
          const QualType Made = Context.getCanonicalTagType(Specialization);
          if (MatchesList(Made, List, Depth + 1))
            return Context.getQualifiedType(Made, Written.getLocalQualifiers());
        }
        return QualType();
      }
      const auto *Member = Written->getAs<DependentNameType>();
      const clang::Type *Of =
          Member ? Member->getQualifier().getAsType() : nullptr;
      if (!Of || !Member->getIdentifier())
        return QualType();
      const QualType Holder = Resolve(QualType(Of, 0), Depth + 1);
      const CXXRecordDecl *Record =
          Holder.isNull() ? nullptr : Holder->getAsCXXRecordDecl();
      // Also one it inherits, through any of its bases, which lookup finds
      // when it isn't ambiguous: the first found hides those of its bases,
      // and of other bases they must agree.
      llvm::SmallVector<const CXXRecordDecl *, 4> Classes;
      if (Record)
        Classes.push_back(Record);
      llvm::SmallPtrSet<const CXXRecordDecl *, 8> Looked;
      QualType Inherited;
      for (size_t iClass = 0; iClass < Classes.size() && iClass < 32;
           ++iClass) {
        const CXXRecordDecl *Class = Classes[iClass];
        if (!Class || !Class->hasDefinition() ||
            !Looked.insert(Class->getDefinition()).second)
          continue;
        Class = Class->getDefinition();
        QualType Here;
        for (const NamedDecl *Found : Class->lookup(Member->getIdentifier()))
          if (const auto *Alias = dyn_cast<TypedefNameDecl>(Found))
            Here = Alias->getUnderlyingType().getCanonicalType();
          else if (const auto *Nested = dyn_cast<CXXRecordDecl>(Found))
            Here = Context.getCanonicalTagType(Nested);
        if (!Here.isNull()) {
          if (iClass == 0)
            return Context.getQualifiedType(Here, Written.getLocalQualifiers());
          if (!Inherited.isNull() && !Context.hasSameType(Inherited, Here))
            return QualType();
          Inherited = Here;
          continue;
        }
        for (const CXXBaseSpecifier &Base : Class->bases())
          Classes.push_back(Base.getType()->getAsCXXRecordDecl());
      }
      if (!Inherited.isNull())
        return Context.getQualifiedType(Inherited,
                                        Written.getLocalQualifiers());
      return QualType();
    };
    // What a dependent type is made with the arguments, as substituting them
    // makes it, which collapses references, `tf_CValue const &` of `int const
    // &`, and drops what qualifies one.
    SubstituteOnce = [&](QualType Written, int Depth) {
      Written = Written.getCanonicalType();
      if (Depth > 16)
        return QualType();
      if (!Written->isDependentType())
        return Written;
      const Qualifiers Quals = Written.getLocalQualifiers();
      const QualType Bare = Written.getLocalUnqualifiedType();
      QualType Made;
      if (isa<TemplateTypeParmType, DependentNameType,
              TemplateSpecializationType>(Bare.getTypePtr()))
        Made = Resolve(Bare, Depth + 1);
      else if (const auto *Pointer = Bare->getAs<PointerType>()) {
        const QualType To = Substitute(Pointer->getPointeeType(), Depth + 1);
        Made = To.isNull() ? QualType() : Context.getPointerType(To);
      } else if (const auto *Reference = Bare->getAs<ReferenceType>()) {
        const QualType To = Substitute(Reference->getPointeeType(), Depth + 1);
        if (!To.isNull())
          Made =
              Reference->isLValueReferenceType() || To->isLValueReferenceType()
                  ? Context.getLValueReferenceType(To)
                  : Context.getRValueReferenceType(To);
      } else if (const auto *Function = Bare->getAs<FunctionProtoType>()) {
        const QualType Result =
            Substitute(Function->getReturnType(), Depth + 1);
        llvm::SmallVector<QualType, 4> Parameters;
        for (const QualType Parameter : Function->getParamTypes()) {
          const QualType Of = Substitute(Parameter, Depth + 1);
          if (Of.isNull())
            return QualType();
          Parameters.push_back(Of.getUnqualifiedType());
        }
        if (!Result.isNull())
          Made = Context.getFunctionType(Result, Parameters,
                                         Function->getExtProtoInfo());
      }
      if (Made.isNull() || Made->isReferenceType())
        return Made.isNull() ? Made : Made.getCanonicalType();
      return Context.getQualifiedType(Made, Quals).getCanonicalType();
    };
    // The specialization \p Given of a class template is the one that \p
    // Written names with the arguments.
    MatchesList = [&](QualType Given, const TemplateSpecializationType *List,
                      int Depth) {
      const auto *Record = dyn_cast_or_null<ClassTemplateSpecializationDecl>(
          Given->getAsCXXRecordDecl());
      const TemplateDecl *Template =
          List->getTemplateName().getAsTemplateDecl();
      if (!Record || !Template ||
          Record->getSpecializedTemplate()->getCanonicalDecl() !=
              Template->getCanonicalDecl())
        return false;
      const auto Listed = List->template_arguments();
      const auto Taken = Record->getTemplateArgs().asArray();
      if (Listed.size() > Taken.size())
        return false;
      for (size_t iArgument = 0; iArgument < Listed.size(); ++iArgument) {
        const TemplateArgument &Argument = Listed[iArgument];
        if (Argument.getKind() == TemplateArgument::Type) {
          if (Taken[iArgument].getKind() != TemplateArgument::Type ||
              !Matches(Taken[iArgument].getAsType(), Argument.getAsType(),
                       Depth + 1))
            return false;
        } else if (Argument.isDependent()) {
          // A value of what a parameter is given, `tf_CTraits::mc_bCoalesced`,
          // also a parameter itself, as it is made with the arguments.
          if (Argument.getKind() != TemplateArgument::Expression ||
              Taken[iArgument].getKind() != TemplateArgument::Integral)
            return false;
          const Expr *Value = Argument.getAsExpr()->IgnoreParenImpCasts();
          std::optional<llvm::APSInt> Made;
          if (const auto *Member = dyn_cast<DependentScopeDeclRefExpr>(Value);
              Member && Member->getDeclName().getAsIdentifierInfo()) {
            const clang::Type *Of = Member->getQualifier().getAsType();
            const QualType Holder =
                Of ? Resolve(QualType(Of, 0), Depth + 1) : QualType();
            const CXXRecordDecl *Record =
                Holder.isNull() ? nullptr : Holder->getAsCXXRecordDecl();
            if (Record && Record->hasDefinition())
              for (const NamedDecl *Found :
                   Record->getDefinition()->lookup(Member->getDeclName()))
                if (const auto *Constant = dyn_cast<VarDecl>(Found);
                    Constant && Constant->getInit() &&
                    !Constant->getInit()->isValueDependent())
                  Made = Constant->getInit()->getIntegerConstantExpr(Context);
          } else if (const auto *Ref = dyn_cast<DeclRefExpr>(Value)) {
            if (const auto *Parameter =
                    dyn_cast<NonTypeTemplateParmDecl>(Ref->getDecl());
                Parameter && Parameter->getDepth() == Level && Arguments &&
                Parameter->getIndex() < Arguments->size() &&
                Arguments->get(Parameter->getIndex()).getKind() ==
                    TemplateArgument::Integral)
              Made = Arguments->get(Parameter->getIndex()).getAsIntegral();
          }
          if (!Made || !llvm::APSInt::isSameValue(
                           *Made, Taken[iArgument].getAsIntegral()))
            return false;
        } else if (!Argument.structurallyEquals(Taken[iArgument]))
          return false;
      }
      // Those it doesn't name are its defaults, made of those it does.
      return true;
    };
    Matches = [&](QualType Given, QualType Written, int Depth) {
      if (Depth > 16)
        return false;
      const QualType Made = Substitute(Written, Depth);
      return !Made.isNull() && Context.hasSameType(Given, Made);
    };
    const QualType Default =
        Defaulted->getDefaultArgument().getArgument().getKind() ==
                TemplateArgument::Type
            ? Defaulted->getDefaultArgument().getArgument().getAsType()
            : QualType();
    // What the default is made with them, as written, keeping what names
    // it, `TCMap<int, bool>::CValue` of `typename tf_CMap::CValue`, which
    // tells what is generic, like the type of a parameter does; or else as
    // substituting makes it.
    SugaredOnce = [&](QualType Written, int Depth) {
      if (Depth > 16)
        return QualType();
      if (!Written->isDependentType())
        return Written;
      const Qualifiers Quals = Written.getLocalQualifiers();
      const QualType Bare = Written.getLocalUnqualifiedType();
      auto Qualify = [&](QualType Made) {
        return Made.isNull() || Made->isReferenceType()
                   ? Made
                   : Context.getQualifiedType(Made, Quals);
      };
      if (const auto *Parens = dyn_cast<ParenType>(Bare.getTypePtr()))
        return Qualify(Sugared(Parens->getInnerType(), Depth + 1));
      if (const auto *Member = dyn_cast<DependentNameType>(Bare.getTypePtr());
          Member && Member->getIdentifier()) {
        const clang::Type *Of = Member->getQualifier().getAsType();
        const QualType Holder =
            Of ? Resolve(QualType(Of, 0), Depth + 1) : QualType();
        const CXXRecordDecl *Record =
            Holder.isNull() ? nullptr : Holder->getAsCXXRecordDecl();
        if (Record && Record->hasDefinition())
          for (const NamedDecl *Found :
               Record->getDefinition()->lookup(Member->getIdentifier()))
            if (const auto *Alias = dyn_cast<TypedefNameDecl>(Found))
              return Qualify(Context.getTypedefType(ElaboratedTypeKeyword::None,
                                                    /*Qualifier=*/std::nullopt,
                                                    Alias));
        return Substitute(Written, Depth + 1);
      }
      if (const auto *List =
              dyn_cast<TemplateSpecializationType>(Bare.getTypePtr());
          List && !List->isTypeAlias()) {
        const QualType Made = Resolve(Bare, Depth + 1);
        const auto *Record =
            Made.isNull() ? nullptr
                          : dyn_cast_or_null<ClassTemplateSpecializationDecl>(
                                Made->getAsCXXRecordDecl());
        if (!Record)
          return QualType();
        llvm::SmallVector<TemplateArgument, 4> Given;
        for (const TemplateArgument &Argument : List->template_arguments()) {
          if (Argument.getKind() != TemplateArgument::Type) {
            Given.push_back(Argument);
            continue;
          }
          const QualType Type = Sugared(Argument.getAsType(), Depth + 1);
          if (Type.isNull())
            return QualType();
          Given.push_back(TemplateArgument(Type));
        }
        return Qualify(Context.getTemplateSpecializationType(
            ElaboratedTypeKeyword::None, List->getTemplateName(), Given,
            Record->getTemplateArgs().asArray(),
            Context.getCanonicalTagType(Record)));
      }
      if (const auto *Function = Bare->getAs<FunctionProtoType>()) {
        const QualType Result = Sugared(Function->getReturnType(), Depth + 1);
        llvm::SmallVector<QualType, 4> Parameters;
        for (const QualType Parameter : Function->getParamTypes()) {
          const QualType Of = Sugared(Parameter, Depth + 1);
          if (Of.isNull())
            return QualType();
          Parameters.push_back(Of);
        }
        return Result.isNull()
                   ? Result
                   : Context.getFunctionType(Result, Parameters,
                                             Function->getExtProtoInfo());
      }
      return Substitute(Written, Depth + 1);
    };
    auto AddArgument = [&](const Decl *Specialization,
                           const TemplateArgumentList *Of) {
      Arguments = Of;
      Outer.clear();
      Resolved.clear();
      Substituted.clear();
      SugaredTypes.clear();
      for (const DeclContext *In = Specialization->getDeclContext();
           In && !In->isFileContext(); In = In->getParent()) {
        if (const auto *Class = dyn_cast<ClassTemplateSpecializationDecl>(In)) {
          if (isa<ClassTemplatePartialSpecializationDecl>(Class))
            return;
          Outer.push_back(&Class->getTemplateArgs());
        } else if (In->isDependentContext() || isa<FunctionDecl>(In))
          return;
      }
      std::reverse(Outer.begin(), Outer.end());
      if (Outer.size() != Level)
        return;
      if (!Default.isNull() && Arguments && Index < Arguments->size() &&
          Arguments->get(Index).getKind() == TemplateArgument::Type &&
          Matches(Arguments->get(Index).getAsType(), Default, 0)) {
        const QualType Written = Sugared(Default, 0);
        Made.push_back(!Written.isNull() &&
                               Context.hasSameType(
                                   Written, Arguments->get(Index).getAsType())
                           ? Written
                           : Arguments->get(Index).getAsType());
      }
    };
    // Also of the member templates instantiated from it, of the
    // specializations of the class template it is in.
    llvm::SmallVector<const TemplateDecl *, 4> Owners;
    if (Owner) {
      Owners.push_back(Owner);
      const auto *In = dyn_cast<CXXRecordDecl>(Owner->getDeclContext());
      if (const ClassTemplateDecl *Enclosing =
              In ? In->getDescribedClassTemplate() : nullptr)
        for (const ClassTemplateSpecializationDecl *Specialization :
             Enclosing->specializations())
          for (const NamedDecl *Found :
               Specialization->lookup(Owner->getDeclName())) {
            const TemplateDecl *From = nullptr;
            if (const auto *Function = dyn_cast<FunctionTemplateDecl>(Found))
              From = Function->getInstantiatedFromMemberTemplate();
            else if (const auto *Class = dyn_cast<ClassTemplateDecl>(Found))
              From = Class->getInstantiatedFromMemberTemplate();
            if (From && From->getCanonicalDecl() == Owner->getCanonicalDecl())
              Owners.push_back(cast<TemplateDecl>(Found));
          }
    }
    for (const TemplateDecl *Each : Owners) {
      if (const auto *Function = dyn_cast<FunctionTemplateDecl>(Each))
        for (const FunctionDecl *Specialization : Function->specializations())
          AddArgument(Specialization,
                      Specialization->getTemplateSpecializationArgs());
      else if (const auto *Class = dyn_cast<ClassTemplateDecl>(Each))
        for (const ClassTemplateSpecializationDecl *Specialization :
             Class->specializations())
          AddArgument(Specialization, &Specialization->getTemplateArgs());
    }
  }
  auto AddFrom = [&](const FunctionProtoType *Function) {
    const size_t nParams = Function->getNumParams();
    if (!Packed) {
      if (nParams == Pattern.size())
        Types.push_back(Function->getParamType(iPosition));
      return;
    }
    if (nParams + 1 < Pattern.size())
      return;
    const size_t nAfter = Pattern.size() - iPack - 1;
    if (iPosition < iPack)
      Types.push_back(Function->getParamType(iPosition));
    else if (iPosition > iPack)
      Types.push_back(
          Function->getParamType(nParams - (Pattern.size() - iPosition)));
    else
      for (size_t iExpanded = iPack; iExpanded < nParams - nAfter; ++iExpanded)
        Types.push_back(Function->getParamType(iExpanded));
  };
  for (const QualType Use : Made) {
    CFunctionTypes Functions;
    Functions.TraverseType(Use);
    if (Functions.Functions.size() == Written.Functions.size()) {
      AddFrom(Functions.Functions[iFunction]);
      continue;
    }
    // Function types written the same are one type, which a use of them
    // visits once, `TCPair<TCFunction<void (typename t_CTraits::CBool
    // o_Ready)>, TCFunction<void (typename t_CTraits::CBool o_Done)>>`: each
    // is the one at the place of the first written like it.
    {
      llvm::SmallVector<const clang::Type *, 4> Unique;
      size_t iUnique = 0;
      for (size_t iWritten = 0; iWritten < Written.Types.size(); ++iWritten) {
        const auto *Found = llvm::find(Unique, Written.Types[iWritten]);
        if (iWritten == iFunction)
          iUnique = Found - Unique.begin();
        if (Found == Unique.end())
          Unique.push_back(Written.Types[iWritten]);
      }
      if (Unique.size() < Written.Types.size() &&
          Functions.Functions.size() == Unique.size()) {
        AddFrom(Functions.Functions[iUnique]);
        continue;
      }
    }
    // The one function type of a signature expanded from a pack,
    // `TCFunction<void(typename tp_CTraits::CBool _bReady)...>`, is each of
    // those it expands to.
    if (Written.Functions.size() == 1)
      for (const FunctionProtoType *Function : Functions.Functions)
        AddFrom(Function);
  }
  return Types;
}

bool givesParameter(QualType Type, bool Nested) {
  return givesParameter(Type, Nested, 0);
}

static QualType pastKeepingTransform(QualType Type);

bool isMemberOfGivenParameter(const clang::Type *Written, int Depth) {
  // Also without its reference, `TCRemoveReference<typename
  // tf_CMap::CValue>`.
  for (int Level = 0; Written && Level < 8; ++Level) {
    const QualType Kept = pastKeepingTransform(QualType(Written, 0));
    if (Kept.isNull())
      break;
    Written = Kept.getNonReferenceType().getTypePtr();
  }
  // Also one a class imports from a base, `using typename t_CMap::CValue;`.
  const clang::Type *Of = nullptr;
  const IdentifierInfo *Name = nullptr;
  if (const auto *Member = dyn_cast_or_null<DependentNameType>(Written)) {
    Of = Member->getQualifier().getAsType();
    Name = Member->getIdentifier();
  } else if (const auto *Imported =
                 dyn_cast_or_null<UnresolvedUsingType>(Written)) {
    Of = Imported->getDecl()->getQualifier().getAsType();
    Name = Imported->getDecl()->getIdentifier();
  }
  if (!Of || !Name || Depth > 8)
    return false;
  if (isa<TemplateTypeParmType>(Of->getCanonicalTypeInternal().getTypePtr()))
    return true;
  // Also of a member of one in turn, `typename CNodePublic::CValue` of `using
  // CNodePublic = typename t_CMap::CNode;`.
  if (const auto *Outer = dyn_cast<DependentNameType>(
          Of->getCanonicalTypeInternal().getTypePtr()))
    return isMemberOfGivenParameter(Outer, Depth + 1);
  const auto *Specialization = Of->getAs<TemplateSpecializationType>();
  const auto *Template =
      Specialization
          ? dyn_cast_or_null<ClassTemplateDecl>(
                Specialization->getTemplateName().getAsTemplateDecl())
          : nullptr;
  if (!Template)
    return false;
  // Not of one that a specialization can define otherwise, `template <>
  // struct TCTraits<int> { using CType = bool; };`.
  llvm::SmallVector<ClassTemplatePartialSpecializationDecl *, 2> Partials;
  const_cast<ClassTemplateDecl *>(Template)->getPartialSpecializations(
      Partials);
  if (!Partials.empty() ||
      llvm::any_of(Template->specializations(),
                   [](const ClassTemplateSpecializationDecl *Specialized) {
                     return Specialized->isExplicitSpecialization();
                   }))
    return false;
  const CXXRecordDecl *Pattern = Template->getTemplatedDecl()->getDefinition();
  if (!Pattern)
    return false;
  for (const NamedDecl *Found : Pattern->lookup(Name))
    if (const auto *Alias = dyn_cast<TypedefNameDecl>(Found)) {
      // One that is a parameter of the template, `using CValue = t_CValue;`,
      // is the argument at its place, of a parameter, `t_CValue` of `using
      // typename TCMap<int, t_CValue>::CValue;` in a class given it, but not
      // a member of what one is given, `typename t_CTraits::EValue`, which is
      // what that is.
      if (const auto *Parameter = dyn_cast<TemplateTypeParmType>(
              Alias->getUnderlyingType().getCanonicalType().getTypePtr());
          Parameter &&
          Parameter->getDepth() ==
              Template->getTemplateParameters()->getDepth() &&
          !Parameter->isParameterPack() &&
          Parameter->getIndex() < Specialization->template_arguments().size()) {
        const TemplateArgument &Given =
            Specialization->template_arguments()[Parameter->getIndex()];
        return Given.getKind() == TemplateArgument::Type &&
               isa<TemplateTypeParmType>(
                   Given.getAsType().getCanonicalType().getTypePtr());
      }
      return isMemberOfGivenParameter(Alias->getUnderlyingType().getTypePtr(),
                                      Depth + 1);
    }
  return false;
}

bool forwardsArgument(QualType Type) {
  const clang::Type *Made = Type.getTypePtr();
  const Decl *Owner = nullptr;
  for (int Depth = 0; Made && Depth < 16; ++Depth) {
    if (const auto *Parens = dyn_cast<ParenType>(Made))
      Made = Parens->getInnerType().getTypePtr();
    else if (const auto *Attributed = dyn_cast<AttributedType>(Made))
      Made = Attributed->getModifiedType().getTypePtr();
    else if (const auto *Alias = dyn_cast<TemplateSpecializationType>(Made);
             Alias && Alias->isTypeAlias())
      Made = Alias->getAliasedType().getTypePtr();
    // Also without its reference, `__remove_reference_t(...)`.
    else if (const auto *Transform = dyn_cast<UnaryTransformType>(Made))
      Made = Transform->getBaseType().getTypePtr();
    // Also a reference to it, `using CValueRef = t_CValue const &;`.
    else if (const auto *Reference = dyn_cast<ReferenceType>(Made))
      Made = Reference->getPointeeTypeAsWritten().getTypePtr();
    // Also one a class imports, `using typename t_CMap::CValue;`.
    else if (const auto *Imported = dyn_cast<UsingType>(Made))
      Made = Imported->desugar().getTypePtr();
    else if (const auto *Local = dyn_cast<TypedefType>(Made);
             Local && Local->getDecl()->getDeclContext()->isFunctionOrMethod())
      // A local alias, `using CValue = typename tf_CMap::CValue;`, is what it
      // is an alias of.
      Made = Local->getDecl()->getUnderlyingType().getTypePtr();
    else if (const auto *Typedef = dyn_cast<TypedefType>(Made)) {
      // Of a specialization of a template, or of a class in one, `typename
      // TCMap<bool>::CNode::CValue`, whose template's are the parameters.
      const auto *Class =
          dyn_cast<CXXRecordDecl>(Typedef->getDecl()->getDeclContext());
      const ClassTemplateSpecializationDecl *Specialization = nullptr;
      for (const DeclContext *In = Class; In && !Specialization;
           In = In->getParent())
        Specialization = dyn_cast<ClassTemplateSpecializationDecl>(In);
      if (!Class || !Specialization)
        return false;
      // Its member as the template writes it gives one of its parameters,
      // also through a trait, `using CValueNoRef =
      // TCRemoveReference<t_CValue>;`.
      if (const CXXRecordDecl *Pattern =
              Class->getTemplateInstantiationPattern())
        for (const NamedDecl *Found :
             Pattern->lookup(Typedef->getDecl()->getDeclName()))
          if (const auto *Member = dyn_cast<TypedefNameDecl>(Found);
              Member && givesParameter(Member->getUnderlyingType(),
                                       /*Nested=*/true))
            return true;
      Owner = Specialization;
      Made = Typedef->getDecl()->getUnderlyingType().getTypePtr();
    } else if (const auto *Substituted =
                   dyn_cast<SubstTemplateTypeParmType>(Made)) {
      // What an alias template, `TCRemoveReference<...>`, was given, which is
      // looked into.
      if (!Owner) {
        Made = Substituted->getReplacementType().getTypePtr();
        continue;
      }
      return Substituted->getAssociatedDecl() &&
             Substituted->getAssociatedDecl()->getCanonicalDecl() ==
                 Owner->getCanonicalDecl();
    } else
      return false;
  }
  return false;
}

bool isTypeOfGivenParameter(const clang::Type *Member, bool Nested) {
  return isTypeOfGivenParameter(Member, Nested, 0);
}

const TemplateArgument *forwardedArgument(const clang::Type *Member) {
  const TemplateArgument *Forwarded = nullptr;
  if (!isTypeOfGivenParameter(Member, /*Nested=*/false, 0, &Forwarded,
                              /*AnyArgument=*/true))
    return nullptr;
  return Forwarded;
}

/// What \p Type is without a reference or qualifiers it removes,
/// `__remove_reference_t(t_CValue)` of `std::remove_reference_t`, or
/// `typename std::remove_reference<t_CValue>::type` of one that a library
/// defines with a trait, which keeps what it is, or null.
static QualType pastKeepingTransform(QualType Type) {
  const clang::Type *Canonical =
      Type.getNonReferenceType().getCanonicalType().getTypePtr();
  if (const auto *Transform = dyn_cast<UnaryTransformType>(Canonical)) {
    switch (Transform->getUTTKind()) {
    case UnaryTransformType::RemoveReference:
    case UnaryTransformType::RemoveCVRef:
    case UnaryTransformType::RemoveCV:
    case UnaryTransformType::RemoveConst:
    case UnaryTransformType::RemoveVolatile:
      return Transform->getBaseType();
    default:
      return QualType();
    }
  }
  const auto *Member = dyn_cast<DependentNameType>(Canonical);
  const clang::Type *Of = Member && Member->getIdentifier() &&
                                  Member->getIdentifier()->getName() == "type"
                              ? Member->getQualifier().getAsType()
                              : nullptr;
  const auto *Trait = Of ? Of->getAs<TemplateSpecializationType>() : nullptr;
  const TemplateDecl *Template =
      Trait ? Trait->getTemplateName().getAsTemplateDecl() : nullptr;
  if (!Template || !Template->getIdentifier() ||
      !llvm::is_contained({StringRef("remove_reference"),
                           StringRef("remove_cvref"), StringRef("remove_cv"),
                           StringRef("remove_const"),
                           StringRef("remove_volatile")},
                          Template->getName()) ||
      Trait->template_arguments().size() != 1 ||
      Trait->template_arguments()[0].getKind() != TemplateArgument::Type)
    return QualType();
  return Trait->template_arguments()[0].getAsType();
}

/// The parameter that \p Type is, also qualified or referred to, `t_CValue
/// const &`, or without its reference or qualifiers, `TCRemoveReference<
/// t_CValue>`, which is what the argument at its place is, or null. Not one
/// pointed to, `t_CValue *`, which is a pointer whatever it is given.
static const TemplateTypeParmType *builtFromParameter(QualType Type) {
  for (int Depth = 0; Depth < 8 && !Type.isNull(); ++Depth) {
    if (const auto *Parameter = dyn_cast<TemplateTypeParmType>(
            Type.getNonReferenceType().getCanonicalType().getTypePtr()))
      return Parameter;
    Type = pastKeepingTransform(Type);
  }
  return nullptr;
}

/// With \p Forwarded, also the argument that the member of the template
/// itself gives, when no partial specialization that can be what is written
/// defines it. With \p AnyArgument, whatever it is given, also a member of
/// what depends on the parameters, `typename tf_CTraits::FSignature` of
/// `typename TCIdentity<typename tf_CTraits::FSignature>::CType`.
static bool isTypeOfGivenParameter(const clang::Type *Member, bool Nested,
                                   int Depth,
                                   const TemplateArgument **Forwarded,
                                   bool AnyArgument) {
  if (Forwarded)
    *Forwarded = nullptr;
  const auto *Dependent = dyn_cast<DependentNameType>(Member);
  if (!Dependent || !Dependent->getIdentifier())
    return false;
  const NestedNameSpecifier Qualifier = Dependent->getQualifier();
  const clang::Type *Of = Qualifier.getAsType();
  const auto *Specialization =
      Of ? Of->getAs<TemplateSpecializationType>() : nullptr;
  if (!Specialization)
    return false;
  // A parameter, also by reference, or with Nested, what is given in another
  // type,
  // `TCTypeList<tp_CRest...>`, but not a member of one, `typename
  // t_CTraits::FSignature`, which is what that is rather than generic, unless
  // it gives one in turn, `typename TCIdentity<t_CType>::CType`.
  auto IsGeneric = [&](const TemplateArgument &Argument) {
    if (Argument.getKind() != TemplateArgument::Type)
      return false;
    if (AnyArgument)
      return true;
    const QualType Given = Argument.getAsType();
    // Also a reference to one, `tf_CType const &`, which is named as it is.
    if (isa<TemplateTypeParmType>(
            Given.getNonReferenceType().getCanonicalType().getTypePtr()))
      return true;
    if (!Nested || !Given->isDependentType())
      return false;
    const clang::Type *Canonical = Given.getCanonicalType().getTypePtr();
    const auto *Template = dyn_cast<TemplateSpecializationType>(Canonical);
    if (!isa<DependentNameType, UnresolvedUsingType, DecltypeType,
             TypeOfExprType, TypeOfType, UnaryTransformType, PackIndexingType>(
            Canonical) &&
        !(Template && Template->getTemplateName().getAsDependentTemplateName()))
      return true;
    return Depth < 8 && givesParameter(Given, Nested, Depth + 1);
  };
  // Also one given in a list that a partial specialization takes apart,
  // `TCFirst<TCTypeList<t_CType>>`, which is told below.
  auto InList = [](const TemplateArgument &Argument) {
    if (Argument.getKind() != TemplateArgument::Type)
      return false;
    const auto *List =
        Argument.getAsType()->getAs<TemplateSpecializationType>();
    return List && !List->isTypeAlias() &&
           llvm::any_of(
               List->template_arguments(), [](const TemplateArgument &Inner) {
                 return Inner.getKind() == TemplateArgument::Type &&
                        isa<TemplateTypeParmType>(
                            Inner.getAsType().getCanonicalType().getTypePtr());
               });
  };
  const bool NoneGeneric =
      llvm::none_of(Specialization->template_arguments(), IsGeneric);
  if (NoneGeneric &&
      llvm::none_of(Specialization->template_arguments(), InList))
    return false;
  // A member of the template that doesn't depend on its parameters, also
  // through a base, isn't what it is given; one that can't be found can be.
  // That of each of its definitions, the template's and those of its partial
  // specializations, `TCResult<TCVector<t_CType>>`, which one that isn't
  // defined leaves to them. One that refers to a template it is looked into
  // for, `typename TCGet<t_iIndex - 1, TCTypeList<tp_CRest...>>::CType`,
  // gives what the others do. Generic, whatever its name, `type`, `CType` or
  // `CValue`, when what it gives is. A member template of what depends on the
  // parameters, `typename t_CTraits::template TCResult<t_CValue>::CType`, is a
  // member of that, like `typename t_CTraits::CType`, what each instantiation
  // makes it.
  // So is one of a template template parameter, `typename
  // tf_TCTransform<tf_CType>::CType`, which each argument for it makes.
  if (Specialization->getTemplateName().getAsDependentTemplateName() ||
      isa_and_nonnull<TemplateTemplateParmDecl>(
          Specialization->getTemplateName().getAsTemplateDecl()))
    return false;
  const auto *Template = dyn_cast_or_null<ClassTemplateDecl>(
      Specialization->getTemplateName().getAsTemplateDecl());
  if (!Template)
    return true;
  static thread_local llvm::SmallPtrSet<const ClassTemplateDecl *, 4>
      s_Visiting;
  if (!s_Visiting.insert(Template->getCanonicalDecl()).second)
    return true;
  const llvm::scope_exit Done(
      [&] { s_Visiting.erase(Template->getCanonicalDecl()); });
  // The member of the template itself that is one of its parameters,
  // `using CValue = t_CValue;`, gives the argument at its place, which must
  // then be generic, `TCMap<int, t_CValue>::CValue` but not `TCMap<int,
  // typename t_CTraits::FSignature>::CValue`.
  const unsigned TemplateDepth = Template->getTemplateParameters()->getDepth();
  // With \p Primary, what is written is surely of \p Pattern, whose
  // parameters, \p ParameterDepth deep, \p WrittenAt gives the argument
  // written at the place of.
  auto Gives = [&](const CXXRecordDecl *Pattern, bool Primary,
                   unsigned ParameterDepth,
                   llvm::function_ref<const TemplateArgument *(unsigned)>
                       WrittenAt) {
    const bool OfTemplate = Primary;
    // In it, or else in a base, each once: one that isn't dependent, `struct
    // TCTraits : CSignature`, and the template of one that is, `struct
    // TCTraits : TCProvider<t_CType>`, what its own member is written as;
    // another dependent one can't be told.
    llvm::SmallVector<const CXXRecordDecl *, 4> Classes{Pattern};
    llvm::SmallPtrSet<const CXXRecordDecl *, 4> Seen;
    // What the base is given, by the template of the base, of which a member
    // that is a parameter, `using CType = t_CType;` of
    // `TCCompileTimeConstant<bool, ...>`, is what is given at its place.
    llvm::DenseMap<
        const CXXRecordDecl *,
        std::pair<const ClassTemplateDecl *, llvm::ArrayRef<TemplateArgument>>>
        OfBaseTemplate;
    // What those of a base of a base are substituted into, which they refer
    // to.
    std::deque<llvm::SmallVector<TemplateArgument, 4>> SubstitutedArguments;
    while (!Classes.empty()) {
      const CXXRecordDecl *Class = Classes.pop_back_val();
      if (!Seen.insert(Class).second)
        continue;
      const auto BaseGiven = OfBaseTemplate.find(Class);
      if (BaseGiven != OfBaseTemplate.end()) {
        Primary = false;
        for (const NamedDecl *Found : Class->lookup(Dependent->getIdentifier()))
          if (const auto *Type = dyn_cast<TypedefNameDecl>(Found)) {
            const auto *Parameter =
                builtFromParameter(Type->getUnderlyingType());
            const auto &[BaseTemplate, Given] = BaseGiven->second;
            if (Parameter && !Parameter->isParameterPack() &&
                Parameter->getDepth() ==
                    BaseTemplate->getTemplateParameters()->getDepth() &&
                Parameter->getIndex() < Given.size() &&
                Given[Parameter->getIndex()].getKind() ==
                    TemplateArgument::Type) {
              const QualType Argument =
                  Given[Parameter->getIndex()].getAsType();
              // One of the template's own parameters, `TCMap<t_CKey,
              // t_CValue>` of `TCMapWithPool<t_CKey, t_CValue>`, gives the
              // argument written at its place.
              if (const auto *Own = dyn_cast<TemplateTypeParmType>(
                      Argument.getCanonicalType().getTypePtr());
                  OfTemplate && Own && !Own->isParameterPack() &&
                  Own->getDepth() == ParameterDepth) {
                if (const TemplateArgument *Written =
                        WrittenAt(Own->getIndex())) {
                  if (Forwarded)
                    *Forwarded = Written;
                  return IsGeneric(*Written);
                }
              }
              return Argument->isDependentType() &&
                     givesParameter(Argument, Nested, Depth);
            }
          }
      }
      // A class of its own, `struct CIterator { ... };`, is what it is,
      // whatever the template is given.
      for (const NamedDecl *Found : Class->lookup(Dependent->getIdentifier()))
        if (isa<TagDecl, ClassTemplateDecl>(Found))
          return false;
      for (const NamedDecl *Found : Class->lookup(Dependent->getIdentifier()))
        if (const auto *Type = dyn_cast<TypedefNameDecl>(Found)) {
          const auto *Parameter = builtFromParameter(Type->getUnderlyingType());
          if (Primary && Parameter && !Parameter->isParameterPack() &&
              Parameter->getDepth() == ParameterDepth)
            if (const TemplateArgument *Given =
                    WrittenAt(Parameter->getIndex())) {
              if (Forwarded)
                *Forwarded = Given;
              return IsGeneric(*Given);
            }
          return givesParameter(Type->getUnderlyingType(), Nested, Depth);
        }
      for (const CXXBaseSpecifier &Base : Class->bases()) {
        if (Base.getType()->isDependentType()) {
          // A base that is a parameter, `struct TCAdapter : t_CTraits`, gives
          // a member of what is given, like `typename t_CTraits::CType`, which
          // is what that is rather than generic.
          if (const auto *Given = dyn_cast<TemplateTypeParmType>(
                  Base.getType().getCanonicalType().getTypePtr());
              Given && OfTemplate && !Given->isParameterPack() &&
              Given->getDepth() == ParameterDepth)
            return false;
          const auto *Of = Base.getType()->getAs<TemplateSpecializationType>();
          const auto *BaseTemplate =
              Of ? dyn_cast_or_null<ClassTemplateDecl>(
                       Of->getTemplateName().getAsTemplateDecl())
                 : nullptr;
          const CXXRecordDecl *BasePattern =
              BaseTemplate ? BaseTemplate->getTemplatedDecl()->getDefinition()
                           : nullptr;
          // Of one that only its specializations define, `template <typename
          // t_CType> struct TCBase<t_CType *> { using CType = bool; };`, what
          // each of them defines it as: generic only when one of them gives a
          // parameter, or doesn't define it.
          if (!BasePattern && BaseTemplate) {
            llvm::SmallVector<ClassTemplatePartialSpecializationDecl *, 4>
                BasePartials;
            const_cast<ClassTemplateDecl *>(BaseTemplate)
                ->getPartialSpecializations(BasePartials);
            llvm::SmallVector<const CXXRecordDecl *, 4> Defined(
                BasePartials.begin(), BasePartials.end());
            for (const ClassTemplateSpecializationDecl *Explicit :
                 const_cast<ClassTemplateDecl *>(BaseTemplate)
                     ->specializations())
              if (Explicit->getSpecializationKind() ==
                  TSK_ExplicitSpecialization)
                Defined.push_back(Explicit);
            if (Defined.empty())
              return true;
            for (const CXXRecordDecl *Specialized : Defined) {
              const CXXRecordDecl *Definition = Specialized->getDefinition();
              if (!Definition)
                return true;
              bool Found = false;
              for (const NamedDecl *Member :
                   Definition->lookup(Dependent->getIdentifier()))
                if (const auto *Type = dyn_cast<TypedefNameDecl>(Member)) {
                  Found = true;
                  if (Type->getUnderlyingType()->isDependentType())
                    return true;
                }
              if (!Found)
                return true;
            }
            return false;
          }
          if (!BasePattern)
            return true;
          // Not of one that a specialization can define otherwise, `template
          // <typename t_CType> struct TCDecayBase<t_CType[]> { using CType =
          // t_CType *; };`, which the instantiations tell.
          {
            llvm::SmallVector<ClassTemplatePartialSpecializationDecl *, 2>
                BasePartials;
            const_cast<ClassTemplateDecl *>(BaseTemplate)
                ->getPartialSpecializations(BasePartials);
            if (!BasePartials.empty() ||
                llvm::any_of(
                    BaseTemplate->specializations(),
                    [](const ClassTemplateSpecializationDecl *Specialized) {
                      return Specialized->isExplicitSpecialization();
                    }))
              return false;
          }
          // As what this class is given, `TCIntermediate<t_CType, bool>`
          // given `TCBase<t_CType>`, which is then `TCBase<bool>`.
          const auto Given = OfBaseTemplate.find(Class);
          if (Given == OfBaseTemplate.end()) {
            OfBaseTemplate.try_emplace(BasePattern, BaseTemplate,
                                       Of->template_arguments());
          } else {
            const auto &[ClassTemplate, ClassGiven] = Given->second;
            const unsigned ClassDepth =
                ClassTemplate->getTemplateParameters()->getDepth();
            llvm::SmallVector<TemplateArgument, 4> &Substituted =
                SubstitutedArguments.emplace_back();
            for (const TemplateArgument &Argument : Of->template_arguments()) {
              const auto *Parameter =
                  Argument.getKind() == TemplateArgument::Type
                      ? dyn_cast<TemplateTypeParmType>(Argument.getAsType()
                                                           .getCanonicalType()
                                                           .getTypePtr())
                      : nullptr;
              Substituted.push_back(
                  Parameter && !Parameter->isParameterPack() &&
                          Parameter->getDepth() == ClassDepth &&
                          Parameter->getIndex() < ClassGiven.size()
                      ? ClassGiven[Parameter->getIndex()]
                      : Argument);
            }
            OfBaseTemplate.try_emplace(BasePattern, BaseTemplate,
                                       llvm::ArrayRef(Substituted));
          }
          Classes.push_back(BasePattern);
          continue;
        }
        if (const CXXRecordDecl *Record = Base.getType()->getAsCXXRecordDecl();
            Record && Record->hasDefinition())
          Classes.push_back(Record->getDefinition());
      }
    }
    return true;
  };
  llvm::SmallVector<ClassTemplatePartialSpecializationDecl *, 4> Partials;
  const_cast<ClassTemplateDecl *>(Template)->getPartialSpecializations(
      Partials);
  // Of those that can be what is written, not `TCSelect<t_CType, false>` of
  // `TCSelect<tf_CType, true>`: one whose argument is another constant, or
  // another type that depends on nothing.
  const ASTContext &Context = Template->getASTContext();
  auto ValueOf =
      [&](const TemplateArgument &Argument) -> std::optional<llvm::APSInt> {
    if (Argument.getKind() == TemplateArgument::Integral)
      return Argument.getAsIntegral();
    if (Argument.getKind() == TemplateArgument::Expression &&
        !Argument.getAsExpr()->isValueDependent())
      return Argument.getAsExpr()->getIntegerConstantExpr(Context);
    return std::nullopt;
  };
  // Whether what is written is surely one of \p Partial, which is then what
  // it is rather than the template itself, `TCConditionalHelper<false, int,
  // tf_CType>` of `TCConditionalHelper<false, t_CFirst, t_CSecond>`: each of
  // its arguments is a parameter of its own, once, or the same constant or
  // type that depends on nothing. Then what is written at the place of each
  // of its parameters, by their index.
  using CWrittenAt = llvm::SmallVector<const TemplateArgument *, 4>;
  auto MustBe = [&](const ClassTemplatePartialSpecializationDecl *Partial)
      -> std::optional<CWrittenAt> {
    // Not one with constraints, `requires (sizeof(t_CType) > sizeof(int))`,
    // which what it is given can fail.
    if (Partial->hasAssociatedConstraints())
      return std::nullopt;
    const llvm::ArrayRef<TemplateArgument> Written =
        Specialization->template_arguments();
    const llvm::ArrayRef<TemplateArgument> Matched =
        Partial->getTemplateArgs().asArray();
    if (Written.size() != Matched.size())
      return std::nullopt;
    const unsigned PartialDepth = Partial->getTemplateParameters()->getDepth();
    CWrittenAt WrittenAt(Partial->getTemplateParameters()->size(), nullptr);
    for (size_t iArgument = 0; iArgument < Written.size(); ++iArgument) {
      const TemplateArgument &Taken = Matched[iArgument];
      if (Written[iArgument].isPackExpansion() || Taken.isPackExpansion() ||
          Taken.getKind() == TemplateArgument::Pack)
        return std::nullopt;
      std::optional<unsigned> Parameter;
      if (Taken.getKind() == TemplateArgument::Type)
        if (const auto *Type = dyn_cast<TemplateTypeParmType>(
                Taken.getAsType().getCanonicalType().getTypePtr());
            Type && Type->getDepth() == PartialDepth)
          Parameter = Type->getIndex();
      if (Taken.getKind() == TemplateArgument::Expression)
        if (const auto *Ref =
                dyn_cast<DeclRefExpr>(Taken.getAsExpr()->IgnoreParenImpCasts()))
          if (const auto *Value =
                  dyn_cast<NonTypeTemplateParmDecl>(Ref->getDecl());
              Value && Value->getDepth() == PartialDepth)
            Parameter = Value->getIndex();
      if (Parameter) {
        if (*Parameter >= WrittenAt.size() || WrittenAt[*Parameter])
          return std::nullopt;
        WrittenAt[*Parameter] = &Written[iArgument];
        continue;
      }
      // Also a list of the same template that it takes apart,
      // `TCTypeList<t_CHead, tp_CTail...>` of `TCTypeList<t_CType>`, of
      // parameters each once, or the same types, and a pack last, which
      // takes the rest.
      if (Written[iArgument].getKind() == TemplateArgument::Type &&
          Taken.getKind() == TemplateArgument::Type) {
        const auto *WrittenList =
            Written[iArgument].getAsType()->getAs<TemplateSpecializationType>();
        const auto *TakenList =
            Taken.getAsType()->getAs<TemplateSpecializationType>();
        const TemplateDecl *WrittenTemplate =
            WrittenList ? WrittenList->getTemplateName().getAsTemplateDecl()
                        : nullptr;
        const TemplateDecl *TakenTemplate =
            TakenList ? TakenList->getTemplateName().getAsTemplateDecl()
                      : nullptr;
        if (WrittenTemplate && TakenTemplate &&
            WrittenTemplate->getCanonicalDecl() ==
                TakenTemplate->getCanonicalDecl() &&
            !WrittenList->isTypeAlias()) {
          // As the arguments of the parameters, of a pack its elements,
          // `[t_CHead, tp_CTail...]` of a converted `TCTypeList`.
          auto Flattened = [](llvm::ArrayRef<TemplateArgument> Arguments) {
            llvm::SmallVector<TemplateArgument, 4> Flat;
            for (const TemplateArgument &Argument : Arguments)
              if (Argument.getKind() == TemplateArgument::Pack)
                llvm::append_range(Flat, Argument.pack_elements());
              else
                Flat.push_back(Argument);
            return Flat;
          };
          // What is written is taken as it is, which what is found points
          // into: not one with a pack as converted.
          const llvm::ArrayRef<TemplateArgument> WrittenInner =
              WrittenList->template_arguments();
          const auto TakenFlat = Flattened(TakenList->template_arguments());
          const llvm::ArrayRef<TemplateArgument> TakenInner = TakenFlat;
          if (llvm::any_of(WrittenInner, [](const TemplateArgument &Argument) {
                return Argument.getKind() == TemplateArgument::Pack;
              }))
            return std::nullopt;
          bool Matches = true;
          size_t iInner = 0;
          for (; Matches && iInner < TakenInner.size(); ++iInner) {
            const TemplateArgument &Pattern = TakenInner[iInner];
            // A pack, as converted, of the expansion of one, `tp_CTail...`.
            if (Pattern.isPackExpansion() ||
                (Pattern.getKind() == TemplateArgument::Pack &&
                 llvm::all_of(Pattern.pack_elements(),
                              [](const TemplateArgument &Element) {
                                return Element.isPackExpansion();
                              }))) {
              Matches = iInner + 1 == TakenInner.size();
              iInner = TakenInner.size();
              break;
            }
            if (iInner >= WrittenInner.size() ||
                WrittenInner[iInner].isPackExpansion() ||
                Pattern.getKind() != TemplateArgument::Type ||
                WrittenInner[iInner].getKind() != TemplateArgument::Type) {
              Matches = false;
              break;
            }
            if (const auto *Inner = dyn_cast<TemplateTypeParmType>(
                    Pattern.getAsType().getCanonicalType().getTypePtr());
                Inner && Inner->getDepth() == PartialDepth &&
                !Inner->isParameterPack()) {
              if (Inner->getIndex() >= WrittenAt.size() ||
                  WrittenAt[Inner->getIndex()])
                Matches = false;
              else
                WrittenAt[Inner->getIndex()] = &WrittenInner[iInner];
              continue;
            }
            Matches = !WrittenInner[iInner].getAsType()->isDependentType() &&
                      !Pattern.getAsType()->isDependentType() &&
                      Context.hasSameType(WrittenInner[iInner].getAsType(),
                                          Pattern.getAsType());
          }
          const bool PackLast =
              !TakenInner.empty() &&
              (TakenInner.back().isPackExpansion() ||
               TakenInner.back().getKind() == TemplateArgument::Pack);
          if (Matches && iInner >= TakenInner.size() &&
              (WrittenInner.size() == TakenInner.size() ||
               (PackLast && WrittenInner.size() + 1 >= TakenInner.size())))
            continue;
        }
      }
      const std::optional<llvm::APSInt> WrittenValue =
          ValueOf(Written[iArgument]);
      const std::optional<llvm::APSInt> MatchedValue = ValueOf(Taken);
      if (WrittenValue && MatchedValue &&
          llvm::APSInt::isSameValue(*WrittenValue, *MatchedValue))
        continue;
      if (Written[iArgument].getKind() == TemplateArgument::Type &&
          Taken.getKind() == TemplateArgument::Type &&
          !Written[iArgument].getAsType()->isDependentType() &&
          !Taken.getAsType()->isDependentType() &&
          Context.hasSameType(Written[iArgument].getAsType(),
                              Taken.getAsType()))
        continue;
      return std::nullopt;
    }
    return WrittenAt;
  };
  // Of a list, only through a partial specialization that what is written
  // surely is, which takes it apart.
  if (NoneGeneric &&
      llvm::none_of(Partials,
                    [&](const ClassTemplatePartialSpecializationDecl *Partial) {
                      return MustBe(Partial).has_value();
                    }))
    return false;
  // What is written at the place of each parameter of the template.
  const bool WrittenPack =
      llvm::any_of(Specialization->template_arguments(),
                   [](const TemplateArgument &Written) {
                     return Written.getKind() == TemplateArgument::Pack;
                   });
  auto WrittenAtOfTemplate = [&](unsigned Index) -> const TemplateArgument * {
    return !WrittenPack && Index < Specialization->template_arguments().size()
               ? &Specialization->template_arguments()[Index]
               : nullptr;
  };
  if (const CXXRecordDecl *Pattern =
          Template->getTemplatedDecl()->getDefinition();
      Pattern &&
      llvm::none_of(Partials,
                    [&](const ClassTemplatePartialSpecializationDecl *Partial) {
                      return MustBe(Partial).has_value();
                    }) &&
      !Gives(Pattern, /*Primary=*/true, TemplateDepth, WrittenAtOfTemplate))
    return false;
  // Whether a type written, `TCTypeList<t_CType>`, can be one a
  // specialization matches, not `TCTypeList<>`: one of the same template,
  // with as many arguments, or at least as many as those not of a pack it
  // expands, also of each of them, or another template.
  // Each pair of types once at each depth: a type that names another many
  // times, `TCTypeList<TCLevel7<T>, TCLevel7<T>, ...>`, compares the same
  // pairs again for each path to them, a number of times exponential in the
  // depth of such types.
  llvm::DenseMap<std::tuple<const void *, const void *, int>, bool>
      CanBeTypeResults;
  std::function<bool(QualType, QualType, int)> CanBeType;
  std::function<bool(QualType, QualType, int)> CanBeTypeOnce =
      [&](QualType WrittenType, QualType MatchedType, int Depth) {
        // As converted, with the arguments that are left to their defaults,
        // also of a specialization that depends on nothing, `TCTypeList<>`.
        auto Of =
            [](QualType Type) -> std::pair<const TemplateDecl *,
                                           llvm::ArrayRef<TemplateArgument>> {
          const clang::Type *Canonical = Type.getCanonicalType().getTypePtr();
          if (const auto *List =
                  dyn_cast<TemplateSpecializationType>(Canonical))
            return {List->isTypeAlias()
                        ? nullptr
                        : List->getTemplateName().getAsTemplateDecl(),
                    List->template_arguments()};
          if (const auto *Specialization =
                  dyn_cast_or_null<ClassTemplateSpecializationDecl>(
                      Canonical->getAsCXXRecordDecl()))
            return {Specialization->getSpecializedTemplate(),
                    Specialization->getTemplateArgs().asArray()};
          return {nullptr, {}};
        };
        const auto [WrittenTemplate, WrittenArguments] = Of(WrittenType);
        const auto [MatchedTemplate, MatchedArguments] = Of(MatchedType);
        if (!WrittenTemplate || !MatchedTemplate || Depth > 8 ||
            isa<TemplateTemplateParmDecl>(WrittenTemplate) ||
            isa<TemplateTemplateParmDecl>(MatchedTemplate))
          return true;
        if (WrittenTemplate->getCanonicalDecl() !=
            MatchedTemplate->getCanonicalDecl())
          return false;
        auto Expands = [](const TemplateArgument &Argument) {
          return Argument.isPackExpansion();
        };
        // The arguments of a pack, as converted, `TCTypeList<t_CType>` of a
        // variadic one, at the places of the parameters.
        auto Flattened = [](llvm::ArrayRef<TemplateArgument> Arguments) {
          llvm::SmallVector<TemplateArgument, 4> Flat;
          for (const TemplateArgument &Argument : Arguments)
            if (Argument.getKind() == TemplateArgument::Pack)
              llvm::append_range(Flat, Argument.pack_elements());
            else
              Flat.push_back(Argument);
          return Flat;
        };
        const llvm::SmallVector<TemplateArgument, 4> Given =
            Flattened(WrittenArguments);
        const llvm::SmallVector<TemplateArgument, 4> Taken =
            Flattened(MatchedArguments);
        if (llvm::any_of(Given, Expands))
          return true;
        const size_t nFixed =
            llvm::count_if(Taken, [&](const TemplateArgument &Argument) {
              return !Expands(Argument);
            });
        if (nFixed == Taken.size() ? Given.size() != Taken.size()
                                   : Given.size() < nFixed)
          return false;
        for (size_t iArgument = 0;
             iArgument < Given.size() && iArgument < Taken.size() &&
             !Expands(Taken[iArgument]);
             ++iArgument)
          if (Given[iArgument].getKind() == TemplateArgument::Type &&
              Taken[iArgument].getKind() == TemplateArgument::Type &&
              !CanBeType(Given[iArgument].getAsType(),
                         Taken[iArgument].getAsType(), Depth + 1))
            return false;
        return true;
      };
  CanBeType = [&](QualType WrittenType, QualType MatchedType, int Depth) {
    const auto Key =
        std::make_tuple(WrittenType.getCanonicalType().getAsOpaquePtr(),
                        MatchedType.getCanonicalType().getAsOpaquePtr(), Depth);
    if (auto It = CanBeTypeResults.find(Key); It != CanBeTypeResults.end())
      return It->second;
    const bool Result = CanBeTypeOnce(WrittenType, MatchedType, Depth);
    CanBeTypeResults[Key] = Result;
    return Result;
  };
  auto CanBe = [&](const ClassTemplateSpecializationDecl *Partial) {
    const llvm::ArrayRef<TemplateArgument> Written =
        Specialization->template_arguments();
    const llvm::ArrayRef<TemplateArgument> Matched =
        Partial->getTemplateArgs().asArray();
    if (Written.size() != Matched.size())
      return true;
    for (size_t iArgument = 0; iArgument < Written.size(); ++iArgument) {
      const std::optional<llvm::APSInt> WrittenValue =
          ValueOf(Written[iArgument]);
      const std::optional<llvm::APSInt> MatchedValue =
          ValueOf(Matched[iArgument]);
      if (WrittenValue && MatchedValue &&
          !llvm::APSInt::isSameValue(*WrittenValue, *MatchedValue))
        return false;
      if (Written[iArgument].getKind() == TemplateArgument::Type &&
          Matched[iArgument].getKind() == TemplateArgument::Type &&
          !Written[iArgument].getAsType()->isDependentType() &&
          !Matched[iArgument].getAsType()->isDependentType() &&
          !Context.hasSameType(Written[iArgument].getAsType(),
                               Matched[iArgument].getAsType()))
        return false;
      if (Written[iArgument].getKind() == TemplateArgument::Type &&
          Matched[iArgument].getKind() == TemplateArgument::Type &&
          !CanBeType(Written[iArgument].getAsType(),
                     Matched[iArgument].getAsType(), 0))
        return false;
    }
    return true;
  };
  // Also of an explicit specialization, `template <> struct TCTraits<int> {
  // using CType = bool; };`, which gives what it defines.
  llvm::SmallVector<const ClassTemplateSpecializationDecl *, 4> Specializations(
      Partials.begin(), Partials.end());
  for (const ClassTemplateSpecializationDecl *Explicit :
       const_cast<ClassTemplateDecl *>(Template)->specializations())
    if (Explicit->getSpecializationKind() == TSK_ExplicitSpecialization)
      Specializations.push_back(Explicit);
  return llvm::all_of(
      Specializations, [&](const ClassTemplateSpecializationDecl *Partial) {
        const CXXRecordDecl *Definition = Partial->getDefinition();
        if (!Definition || !CanBe(Partial))
          return true;
        if (Forwarded)
          *Forwarded = nullptr;
        // One that what is written surely is gives what is written at the
        // places of its parameters, `bool` of `TCConditionalHelper<false,
        // tf_CType, bool>`.
        if (const auto *Selected =
                dyn_cast<ClassTemplatePartialSpecializationDecl>(Partial))
          if (const std::optional<CWrittenAt> WrittenAt = MustBe(Selected))
            return Gives(Definition, /*Primary=*/true,
                         Selected->getTemplateParameters()->getDepth(),
                         [&](unsigned Index) -> const TemplateArgument * {
                           return Index < WrittenAt->size()
                                      ? (*WrittenAt)[Index]
                                      : nullptr;
                         });
        // One that only takes a reference or qualifiers off what is written,
        // `TCRemoveReferenceHelper<t_CType &>`, gives at the place of its
        // parameter what is written without them, `using CType = t_CType;`,
        // which is named as what is written.
        if (const auto *Selected =
                dyn_cast<ClassTemplatePartialSpecializationDecl>(Partial);
            Selected && !Selected->hasAssociatedConstraints()) {
          const llvm::ArrayRef<TemplateArgument> Written =
              Specialization->template_arguments();
          const llvm::ArrayRef<TemplateArgument> Matched =
              Selected->getTemplateArgs().asArray();
          const unsigned PartialDepth =
              Selected->getTemplateParameters()->getDepth();
          CWrittenAt WrittenAt(Selected->getTemplateParameters()->size(),
                               nullptr);
          bool Peels = Written.size() == Matched.size();
          for (size_t iArgument = 0; Peels && iArgument < Written.size();
               ++iArgument) {
            const auto *Parameter =
                Matched[iArgument].getKind() == TemplateArgument::Type &&
                        Written[iArgument].getKind() == TemplateArgument::Type &&
                        !Written[iArgument].isPackExpansion()
                    ? dyn_cast<TemplateTypeParmType>(
                          Matched[iArgument]
                              .getAsType()
                              .getNonReferenceType()
                              .getCanonicalType()
                              .getUnqualifiedType()
                              .getTypePtr())
                    : nullptr;
            Peels = Parameter && !Parameter->isParameterPack() &&
                    Parameter->getDepth() == PartialDepth &&
                    Parameter->getIndex() < WrittenAt.size() &&
                    !WrittenAt[Parameter->getIndex()];
            if (Peels)
              WrittenAt[Parameter->getIndex()] = &Written[iArgument];
          }
          if (Peels)
            return Gives(Definition, /*Primary=*/true, PartialDepth,
                         [&](unsigned Index) -> const TemplateArgument * {
                           return Index < WrittenAt.size() ? WrittenAt[Index]
                                                           : nullptr;
                         });
        }
        return Gives(
            Definition, /*Primary=*/false, 0,
            [](unsigned) -> const TemplateArgument * { return nullptr; });
      });
}

llvm::SmallVector<const Decl *, 4> localInstances(const Decl *Pattern) {
  llvm::SmallVector<const Decl *, 4> Instances;
  const auto *Function =
      dyn_cast_or_null<FunctionDecl>(Pattern->getParentFunctionOrMethod());
  // A lambda is instantiated with the function it is in: what is declared in
  // one that isn't generic, and its init captures, `[Value = _pValue]`, which
  // its call operator has, are found in the instantiations of that function.
  const auto *PatternVariable = dyn_cast<VarDecl>(Pattern);
  const bool InitCapture = PatternVariable && PatternVariable->isInitCapture();
  const FunctionDecl *Innermost = nullptr;
  // Also a generic one in an initializer, `auto g_fCallback =
  // []<typename tf_CType>() { ... }`, whose call operator's instantiations are
  // of the lambdas the initializer is instantiated with.
  auto InInitializer = [](const FunctionDecl *Operator) {
    return !cast<CXXMethodDecl>(Operator)
                ->getParent()
                ->getParentFunctionOrMethod();
  };
  for (int Depth = 0;
       Function && isLambdaCallOperator(Function) &&
       (InitCapture || !Function->getDescribedFunctionTemplate() ||
        InInitializer(Function)) &&
       Depth < 16;
       ++Depth) {
    Innermost = Function;
    Function = dyn_cast_or_null<FunctionDecl>(
        cast<CXXMethodDecl>(Function)->getParent()->getParentFunctionOrMethod());
  }
  llvm::SmallVector<const FunctionDecl *, 4> Functions;
  // The call operators of the lambdas made at the place of the one
  // \p Innermost is of, in \p S.
  auto FindLambdas = [&](const Stmt *S) {
    const CXXRecordDecl *LambdaClass =
        cast<CXXMethodDecl>(Innermost)->getParent();
    llvm::SmallPtrSet<const Stmt *, 32> Visited;
    std::function<void(const Stmt *)> Find = [&](const Stmt *Inner) {
      if (!Inner || !Visited.insert(Inner).second)
        return;
      if (const auto *Lambda = dyn_cast<LambdaExpr>(Inner);
          Lambda && Lambda->getLambdaClass()->getLocation() ==
                        LambdaClass->getLocation()) {
        Functions.push_back(Lambda->getCallOperator());
        return;
      }
      for (const Stmt *Child : Inner->children())
        Find(Child);
    };
    Find(S);
  };
  // A lambda in the initializer of a variable template, `int g_Value = [] {
  // ... }();`, or in a default argument of a function template, is
  // instantiated with it: found in the initializers of its specializations,
  // or the default arguments of its instantiations.
  const auto *Owner =
      Innermost ? dyn_cast_or_null<VarDecl>(cast<CXXMethodDecl>(Innermost)
                                                ->getParent()
                                                ->getLambdaContextDecl())
                : nullptr;
  // Clang doesn't tell it for a default argument of a function that isn't a
  // member, which is found as what the lambda is in, nor for one in the type
  // of a function, `noexcept([](...) { ... }(...))`, which is instantiated
  // with it.
  if (Innermost && !Owner && !Function) {
    ASTContext &Context = const_cast<ASTContext &>(Innermost->getASTContext());
    DynTypedNode Node = DynTypedNode::create(*Innermost);
    for (int Depth = 0; Depth < 256 && !Owner; ++Depth) {
      const auto Parents = Context.getParentMapContext().getParents(Node);
      if (Parents.empty() || Parents[0].get<TranslationUnitDecl>())
        break;
      if (const auto *Enclosing = Parents[0].get<FunctionDecl>()) {
        Function = Enclosing;
        break;
      }
      Node = Parents[0];
      Owner = Node.get<ParmVarDecl>();
    }
  }
  const auto *DefaultOf = Owner && isa<ParmVarDecl>(Owner)
                              ? dyn_cast<FunctionDecl>(Owner->getDeclContext())
                              : nullptr;
  // Also of a partial specialization of a variable template, `int
  // g_Value<t_CType *> = [] { ... }();`, of what is instantiated from it.
  const auto *Partial =
      dyn_cast_or_null<VarTemplatePartialSpecializationDecl>(Owner);
  if (const VarTemplateDecl *Template =
          Partial ? Partial->getSpecializedTemplate()
          : Owner ? Owner->getDescribedVarTemplate()
                  : nullptr) {
    // Also a member of a class template, `template <typename t_CValue> static
    // constexpr bool mc_bSupports = [] { ... }();`, of the member templates
    // of its instantiations.
    llvm::SmallVector<const VarTemplateDecl *, 4> Templates{Template};
    if (const auto *Class = dyn_cast<CXXRecordDecl>(Template->getDeclContext());
        Class && Class->isTemplated())
      for (const CXXRecordDecl *Instance : instancesOf(Class, 0))
        if (Instance->hasDefinition())
          for (const NamedDecl *Found :
               Instance->lookup(Template->getDeclName()))
            if (const auto *Member = dyn_cast<VarTemplateDecl>(Found);
                Member && Member->getInstantiatedFromMemberTemplate() &&
                Member->getInstantiatedFromMemberTemplate()
                        ->getCanonicalDecl() == Template->getCanonicalDecl())
              Templates.push_back(Member);
    for (const VarTemplateDecl *Each : Templates)
      for (const VarTemplateSpecializationDecl *Specialization :
           Each->specializations()) {
        const auto *From =
            Specialization->getSpecializedTemplateOrPartial()
                .dyn_cast<VarTemplatePartialSpecializationDecl *>();
        if (isTemplateInstantiation(
                Specialization->getTemplateSpecializationKind()) &&
            (!Partial || From == Partial ||
             (From && From->getInstantiatedFromMember() &&
              From->getInstantiatedFromMember()->getCanonicalDecl() ==
                  Partial->getCanonicalDecl())))
          FindLambdas(Specialization->getInit());
      }
  } else if (DefaultOf && DefaultOf->isTemplated()) {
    for (const FunctionDecl *Instance : functionInstances(DefaultOf, 0))
      // By its index, also of a definition apart from the declaration that
      // has the default, whose parameters are elsewhere.
      for (const ParmVarDecl *Parameter : Instance->parameters())
        if (isa<ParmVarDecl>(Owner) &&
            Parameter->getFunctionScopeIndex() ==
                cast<ParmVarDecl>(Owner)->getFunctionScopeIndex() &&
            Parameter->hasDefaultArg() &&
            !Parameter->hasUninstantiatedDefaultArg() &&
            !Parameter->hasUnparsedDefaultArg())
          FindLambdas(Parameter->getDefaultArg());
  }
  // A lambda in the initializer of a member of a class template, `static
  // constexpr int mc_Value = [] { ... }();`, or in a static assertion of it,
  // is instantiated with the class: it is found in the initializers of the
  // members of its instantiations.
  else if (!Function && Innermost) {
    const CXXRecordDecl *LambdaClass =
        cast<CXXMethodDecl>(Innermost)->getParent();
    const auto *Class =
        dyn_cast_or_null<CXXRecordDecl>(LambdaClass->getDeclContext());
    if (!Class || !Class->isTemplated())
      return Instances;
    for (const CXXRecordDecl *Instance : instancesOf(Class, 0)) {
      const size_t nBefore = Functions.size();
      llvm::SmallPtrSet<const Stmt *, 32> Visited;
      std::function<void(const Stmt *)> Find = [&](const Stmt *S) {
        if (!S || !Visited.insert(S).second)
          return;
        if (const auto *Lambda = dyn_cast<LambdaExpr>(S);
            Lambda && Lambda->getLambdaClass()->getLocation() ==
                          LambdaClass->getLocation()) {
          Functions.push_back(Lambda->getCallOperator());
          return;
        }
        for (const Stmt *Child : S->children())
          Find(Child);
      };
      // Also in a static assertion of it, `static_assert([] { ... }());`, and
      // in the bound of a member array, `int m_Data[[] { ... }()];`.
      for (const Decl *Member : Instance->decls()) {
        if (const auto *Variable = dyn_cast<VarDecl>(Member))
          Find(Variable->getInit());
        else if (const auto *Field = dyn_cast<FieldDecl>(Member)) {
          Find(Field->getInClassInitializer());
          if (const TypeSourceInfo *Written = Field->getTypeSourceInfo())
            for (TypeLoc Type = Written->getTypeLoc(); !Type.isNull();
                 Type = Type.getNextTypeLoc())
              if (const auto Array = Type.getAs<ArrayTypeLoc>())
                Find(Array.getSizeExpr());
        } else if (const auto *Assert = dyn_cast<StaticAssertDecl>(Member))
          Find(Assert->getAssertExpr());
      }
      // Elsewhere, like in a base, `struct TCBox : decltype([](...) {}) {}`,
      // by the closure class, which the instantiation has as a member.
      if (Functions.size() == nBefore)
        for (const Decl *Member : Instance->decls())
          if (const auto *Closure = dyn_cast<CXXRecordDecl>(Member);
              Closure && Closure->isLambda() &&
              Closure->getLocation() == LambdaClass->getLocation())
            if (const CXXMethodDecl *Operator =
                    Closure->getLambdaCallOperator())
              Functions.push_back(Operator);
    }
  } else if (!Function ||
             // Also a friend defined in a class template, which is declared
             // in the namespace.
             !(Function->isTemplated() ||
               Function->getLexicalDeclContext()->isDependentContext()))
    return Instances;
  else
    Functions = functionInstances(Function, 0);
  // Of a generic lambda, the instantiations of its call operator.
  {
    llvm::SmallVector<const FunctionDecl *, 4> Expanded;
    for (const FunctionDecl *Found : Functions) {
      const FunctionTemplateDecl *Generic =
          Found->getDescribedFunctionTemplate();
      if (!Generic) {
        Expanded.push_back(Found);
        continue;
      }
      for (const FunctionDecl *Specialization : Generic->specializations()) {
        const FunctionDecl *Definition = Specialization->getDefinition();
        Expanded.push_back(Definition ? Definition : Specialization);
      }
    }
    Functions = std::move(Expanded);
  }
  // The declaration at the place of the pattern, which an instantiation keeps,
  // also the parameters it is expanded to of a pack.
  for (const FunctionDecl *Instance : Functions) {
    // A parameter of the function, or of a lambda in it that isn't generic,
    // `[](typename tf_CTraits::CPointer _pValue) {}`, which its call operator
    // has.
    // A parameter of the function, not of a requires expression in it,
    // `requires(typename tf_CTraits::CPointer _pValue) { ... }`, which is
    // found in its body below.
    // Not of a generic lambda around the one it is of, `[]<typename
    // tf_CTraits>() { auto fCallback = [](typename tf_CTraits::CPointer
    // _pValue) {}; }`, whose body has that one.
    const bool OfInnermost =
        !Innermost ||
        (isLambdaCallOperator(Instance) &&
         cast<CXXMethodDecl>(Instance)->getParent()->getLocation() ==
             cast<CXXMethodDecl>(Innermost)->getParent()->getLocation());
    if (isa<ParmVarDecl>(Pattern) &&
        !isa<RequiresExprBodyDecl>(Pattern->getDeclContext()) && OfInnermost) {
      const size_t nBefore = Instances.size();
      for (const ParmVarDecl *Parameter : Instance->parameters())
        if (Parameter->getLocation() == Pattern->getLocation())
          Instances.push_back(Parameter);
      // Also of a definition outside of the class, `void
      // TCBox<t_CType>::f_Run(...)`, whose instantiation has the parameters
      // where the declaration in the class has them: the one at its index,
      // not of a pack, which can expand to several.
      // Not of a requires expression, `requires(typename tf_CTraits::CPointer
      // _pValue)`, which isn't of the function.
      const auto *PatternParameter = cast<ParmVarDecl>(Pattern);
      if (Instances.size() == nBefore && !PatternParameter->isParameterPack() &&
          isa<FunctionDecl>(PatternParameter->getDeclContext()) && !Innermost &&
          Function && Function->getNumParams() == Instance->getNumParams() &&
          PatternParameter->getFunctionScopeIndex() <
              Instance->getNumParams() &&
          llvm::none_of(Function->parameters(), [](const ParmVarDecl *Other) {
            return Other->isParameterPack();
          }))
        Instances.push_back(
            Instance->getParamDecl(PatternParameter->getFunctionScopeIndex()));
      // A pack, when it is the only one, at its index, expanded to as many
      // as the instantiation has more.
      if (Instances.size() == nBefore && PatternParameter->isParameterPack() &&
          isa<FunctionDecl>(PatternParameter->getDeclContext()) && !Innermost &&
          Function &&
          llvm::count_if(Function->parameters(),
                         [](const ParmVarDecl *Other) {
                           return Other->isParameterPack();
                         }) == 1 &&
          Function->getNumParams() - 1 <= Instance->getNumParams()) {
        const unsigned iFirst = PatternParameter->getFunctionScopeIndex();
        const unsigned nExpanded =
            Instance->getNumParams() - (Function->getNumParams() - 1);
        for (unsigned iExpanded = 0; iExpanded < nExpanded; ++iExpanded)
          Instances.push_back(Instance->getParamDecl(iFirst + iExpanded));
      }
      continue;
    }
    const Stmt *Body = Instance->getBody();
    // Each statement once: the operand of a co_await is met again in the
    // expressions that make and call its awaiter.
    llvm::SmallPtrSet<const Stmt *, 32> Visited;
    std::function<void(const Stmt *)> Find = [&](const Stmt *S) {
      if (!S || !Visited.insert(S).second)
        return;
      // Also in a local class, the initializers of its members and the
      // bodies of its member functions, also of a class in it, which are
      // instantiated with the function.
      std::function<void(const CXXRecordDecl *)> FindInClass =
          [&](const CXXRecordDecl *Local) {
            for (const Decl *Member : Local->decls())
              if (const auto *Field = dyn_cast<FieldDecl>(Member))
                Find(Field->getInClassInitializer());
              else if (const auto *Method = dyn_cast<CXXMethodDecl>(Member))
                Find(Method->getBody());
              else if (const auto *Inner = dyn_cast<CXXRecordDecl>(Member);
                       Inner && Inner->isThisDeclarationADefinition())
                FindInClass(Inner);
          };
      if (const auto *Declaration = dyn_cast<DeclStmt>(S))
        for (const Decl *D : Declaration->decls()) {
          if (D->getKind() == Pattern->getKind() &&
              D->getLocation() == Pattern->getLocation())
            Instances.push_back(D);
          // Also in the condition of a static assertion.
          if (const auto *Assertion = dyn_cast<StaticAssertDecl>(D))
            Find(Assertion->getAssertExpr());
          if (const auto *Local = dyn_cast<CXXRecordDecl>(D);
              Local && Local->isThisDeclarationADefinition())
            FindInClass(Local);
        }
      // Also the variable of a `catch`, `catch (tf_CType *_pValue)`.
      if (const auto *Catch = dyn_cast<CXXCatchStmt>(S))
        if (const VarDecl *Caught = Catch->getExceptionDecl();
            Caught && Caught->getLocation() == Pattern->getLocation())
          Instances.push_back(Caught);
      // Also a parameter of a requires expression.
      if (const auto *Requires = dyn_cast<RequiresExpr>(S);
          Requires && isa<ParmVarDecl>(Pattern))
        for (const ParmVarDecl *Parameter : Requires->getLocalParameters())
          if (Parameter->getLocation() == Pattern->getLocation())
            Instances.push_back(Parameter);
      // Also a parameter of a lambda.
      if (const auto *Lambda = dyn_cast<LambdaExpr>(S);
          Lambda && isa<ParmVarDecl>(Pattern))
        for (const ParmVarDecl *Parameter :
             Lambda->getCallOperator()->parameters())
          if (Parameter->getLocation() == Pattern->getLocation())
            Instances.push_back(Parameter);
      // Also an init capture of a lambda, `[Value = _pValue]`.
      if (const auto *Lambda = dyn_cast<LambdaExpr>(S))
        for (const LambdaCapture &Capture : Lambda->captures())
          if (const auto *Variable =
                  Capture.capturesVariable()
                      ? dyn_cast<VarDecl>(Capture.getCapturedVar())
                      : nullptr;
              Variable && Variable->isInitCapture() &&
              Variable->getLocation() == Pattern->getLocation())
            Instances.push_back(Variable);
      for (const Stmt *Child : childrenOnce(S))
        Find(Child);
    };
    Find(Body);
    // Also in its exception specification as it is instantiated,
    // `noexcept([](...) { ... }(...))`, and in its trailing requires clause
    // when it is kept instantiated.
    if (const auto *Prototype = Instance->getType()->getAs<FunctionProtoType>())
      Find(Prototype->getNoexceptExpr());
    if (const AssociatedConstraint &Constraint =
            Instance->getTrailingRequiresClause())
      Find(Constraint.ConstraintExpr);
    // Also in its return type, `-> decltype([](...) {})`, as it is
    // instantiated, also through a pointer or reference to it.
    for (QualType Returned = Instance->getReturnType(); !Returned.isNull();) {
      if (const auto *Decltype = Returned->getAs<DecltypeType>()) {
        Find(Decltype->getUnderlyingExpr());
        break;
      }
      if (!Returned->isPointerType() && !Returned->isReferenceType())
        break;
      Returned = Returned->getPointeeType();
    }
    // Also in the initializers of a constructor, `m_Value([] { ... }())`.
    if (const auto *Constructor = dyn_cast<CXXConstructorDecl>(Instance))
      for (const CXXCtorInitializer *Initializer : Constructor->inits())
        Find(Initializer->getInit());
  }
  return Instances;
}

std::string quotedList(llvm::ArrayRef<StringRef> Prefixes) {
  std::string Result;
  for (size_t I = 0; I < Prefixes.size(); ++I) {
    if (I > 0)
      Result += I + 1 == Prefixes.size() ? " or " : ", ";
    Result += "'" + Prefixes[I].str() + "'";
  }
  return Result;
}

std::optional<bool> declaresCallable(const CXXRecordDecl *Record) {
  if (!Record)
    return false;
  if (Record->isLambda())
    return true;
  bool Unknown = false;
  llvm::SmallPtrSet<const CXXRecordDecl *, 8> Visited;
  llvm::SmallVector<const CXXRecordDecl *, 8> Worklist{Record};
  while (!Worklist.empty()) {
    const CXXRecordDecl *Current = Worklist.pop_back_val();
    if (!Current || !Visited.insert(Current->getCanonicalDecl()).second)
      continue;
    const CXXRecordDecl *Definition = Current->getDefinition();
    if (!Definition) {
      Unknown = true;
      continue;
    }
    for (const Decl *Member : Definition->decls()) {
      const auto *Named = dyn_cast<NamedDecl>(Member);
      if (!Named)
        continue;
      // A call operator, also a template, deleted, constrained or brought in
      // with `using`.
      if (Named->getDeclName().getNameKind() ==
              DeclarationName::CXXOperatorName &&
          Named->getDeclName().getCXXOverloadedOperator() == OO_Call)
        return true;
      // A conversion to a pointer or reference to a function, which a call
      // goes through, but not a template, which a call never uses, like
      // clang's surrogate calls.
      if (const auto *Conversion =
              dyn_cast<CXXConversionDecl>(Named->getUnderlyingDecl())) {
        const QualType To =
            Conversion->getConversionType().getNonReferenceType();
        if (To->isFunctionType() || To->isFunctionPointerType())
          return true;
        // One to a type that depends on template parameters, `operator
        // t_CType()`, can't be told.
        if (To->isDependentType())
          Unknown = true;
      }
    }
    for (const CXXBaseSpecifier &Base : Definition->bases()) {
      if (const CXXRecordDecl *BaseRecord = Base.getType()->getAsCXXRecordDecl())
        Worklist.push_back(BaseRecord);
      else
        Unknown = true;
    }
  }
  return Unknown ? std::nullopt : std::optional<bool>(false);
}

std::optional<bool> isUsableFunction(const FunctionDecl *Function) {
  if (Function->isDeleted())
    return false;
  llvm::SmallVector<const Expr *, 2> Constraints{
      Function->getTrailingRequiresClause().ConstraintExpr};
  if (const FunctionTemplateDecl *Template =
          Function->getDescribedFunctionTemplate())
    Constraints.push_back(
        Template->getTemplateParameters()->getRequiresClause());
  for (const Expr *Constraint : Constraints) {
    bool Holds = true;
    if (!Constraint)
      continue;
    // One that clang kept as written, of a member of a specialization of a
    // class template, `operator++() requires t_bEnabled`, holds where clang
    // used it, which it checked.
    if (Constraint->isValueDependent()) {
      if (Function->isUsed())
        continue;
      return std::nullopt;
    }
    if (!Constraint->EvaluateAsBooleanCondition(Holds,
                                                Function->getASTContext()))
      return std::nullopt;
    if (!Holds)
      return false;
  }
  return true;
}

/// `hasOverloadedOperator`, looking at each class once, in \p Visited.
static std::optional<bool>
hasOverloadedOperator(const CXXRecordDecl *Record,
                      llvm::ArrayRef<OverloadedOperatorKind> Operators,
                      llvm::SmallPtrSetImpl<const CXXRecordDecl *> &Visited) {
  if (!Record)
    return false;
  if (Record->isLambda())
    return llvm::is_contained(Operators, OO_Call);
  // Not defined in the translation unit, like a specialization of a template
  // that isn't instantiated, which isn't made until it is used: what it has
  // can't be told.
  const CXXRecordDecl *Definition = Record->getDefinition();
  if (!Definition)
    return std::nullopt;
  if (!Visited.insert(Definition).second)
    return false;
  bool Unknown = false;
  auto Usable = isUsableFunction;
  auto IsOperator = [&](const NamedDecl *Named) {
    return Named->getDeclName().getNameKind() ==
               DeclarationName::CXXOperatorName &&
           llvm::is_contained(Operators,
                              Named->getDeclName().getCXXOverloadedOperator());
  };
  auto Consider = [&](const NamedDecl *Named) {
    // A template that is instantiated, which its constraints allowed,
    // `requires (sizeof(tf_CType) > 0) CIterator &operator++()`.
    if (const auto *Template = dyn_cast_or_null<FunctionTemplateDecl>(Named)) {
      if (llvm::any_of(Template->specializations(),
                       [](const FunctionDecl *Specialization) {
                         const auto *Method =
                             dyn_cast<CXXMethodDecl>(Specialization);
                         return !Specialization->isDeleted() &&
                                !(Method &&
                                  Method->getOverloadedOperator() == OO_Star &&
                                  Method->getNumExplicitParams() > 0);
                       }))
        return true;
      Named = Template->getTemplatedDecl();
    }
    const auto *Function = dyn_cast_or_null<FunctionDecl>(Named);
    // `operator*` dereferences only as a unary operator: one that takes an
    // operand multiplies, `int operator*(int _Factor) const`.
    if (const auto *Method = dyn_cast_or_null<CXXMethodDecl>(Function);
        Method && Method->getOverloadedOperator() == OO_Star &&
        Method->getNumExplicitParams() > 0)
      return false;
    const std::optional<bool> Can =
        Function ? Usable(Function) : std::optional<bool>();
    Unknown |= !Can;
    return Can.value_or(false);
  };
  for (const Decl *Member : Definition->decls()) {
    const auto *Named = dyn_cast<NamedDecl>(Member);
    if (!Named || !IsOperator(Named))
      continue;
    if (isa<FunctionDecl, FunctionTemplateDecl>(Member) && Consider(Named))
      return true;
    // Brought in from a base, `using CBase::operator();`, unless what it
    // brings in can't be used; from a base that depends on parameters of a
    // template, what can't be told.
    if (const auto *Using = dyn_cast<UsingDecl>(Member)) {
      if (Using->shadow_size() == 0)
        Unknown = true;
      for (const UsingShadowDecl *Shadow : Using->shadows())
        if (Consider(Shadow->getTargetDecl()))
          return true;
    }
    if (isa<UnresolvedUsingValueDecl>(Member))
      Unknown = true;
  }
  // A non-member one that takes it as its operand, a hidden friend or one in
  // its namespace, `int &operator*(CIterator const &_Iterator)`, which only
  // the operators that can be non-members are.
  std::function<bool(const NamedDecl *)> ConsiderFree = [&](const NamedDecl
                                                                *Named) {
    // One brought in by a using declaration, `using NOps::operator*;`, is
    // what it names.
    if (const auto *Shadow = dyn_cast_or_null<UsingShadowDecl>(Named))
      Named = Shadow->getTargetDecl();
    if (!Named || !IsOperator(Named))
      return false;
    // Of a template, what it instantiates for it, `operator*<int>` of
    // `TCIterator<int>`.
    if (const auto *Template = dyn_cast<FunctionTemplateDecl>(Named))
      return llvm::any_of(Template->specializations(),
                          [&](const FunctionDecl *Specialization) {
                            return ConsiderFree(Specialization);
                          });
    const auto *Function = dyn_cast<FunctionDecl>(Named);
    if (!Function || isa<CXXMethodDecl>(Function) ||
        Function->getNumParams() == 0 ||
        // `operator*` dereferences only as a unary operator.
        (Function->getOverloadedOperator() == OO_Star &&
         Function->getNumParams() != 1))
      return false;
    const CXXRecordDecl *Operand = Function->getParamDecl(0)
                                       ->getType()
                                       .getNonReferenceType()
                                       ->getAsCXXRecordDecl();
    if (!Operand ||
        Operand->getCanonicalDecl() != Definition->getCanonicalDecl())
      return false;
    const std::optional<bool> Can = Usable(Function);
    Unknown |= !Can;
    return Can.value_or(false);
  };
  for (const Decl *Member : Definition->decls())
    if (const auto *Friend = dyn_cast<FriendDecl>(Member);
        Friend && ConsiderFree(Friend->getFriendDecl()))
      return true;
  // The namespaces argument-dependent lookup looks in: of the class, of its
  // bases, and of the classes its template arguments are, `NTag` of
  // `NRange::TCIterator<NTag::CTag>`, also of theirs, each once.
  llvm::SmallVector<const DeclContext *, 4> Namespaces;
  {
    llvm::SmallPtrSet<const CXXRecordDecl *, 8> Seen;
    llvm::SmallVector<const CXXRecordDecl *, 4> Classes{Definition};
    while (!Classes.empty() && Seen.size() < 64) {
      const CXXRecordDecl *Class = Classes.pop_back_val();
      if (!Seen.insert(Class).second)
        continue;
      if (const DeclContext *Namespace = Class->getEnclosingNamespaceContext();
          !llvm::is_contained(Namespaces, Namespace))
        Namespaces.push_back(Namespace);
      // Also of its bases, `NBase` of `struct CIterator : NBase::CBase`.
      if (const CXXRecordDecl *ClassDefinition = Class->getDefinition())
        for (const CXXBaseSpecifier &Base : ClassDefinition->bases())
          if (const CXXRecordDecl *Record =
                  Base.getType()->getAsCXXRecordDecl())
            Classes.push_back(Record);
      const auto *Specialization =
          dyn_cast<ClassTemplateSpecializationDecl>(Class);
      if (!Specialization)
        continue;
      // The namespace of what \p Declaration is declared in, and the class,
      // when it is a member of one.
      auto AddOwner = [&](const Decl *Declaration) {
        const DeclContext *Owner = Declaration->getDeclContext();
        if (const auto *Record = dyn_cast<CXXRecordDecl>(Owner))
          Classes.push_back(Record);
        else if (const DeclContext *Namespace =
                     Owner->getEnclosingNamespaceContext();
                 !llvm::is_contained(Namespaces, Namespace))
          Namespaces.push_back(Namespace);
      };
      // Also of an enum, `TCIterator<NData::EItem>`, and of the parameters
      // and result of a function type, `TCIterator<NData::CItem ()>`. Each
      // type once, at the least depth it is met at, of which what is in it is
      // looked at the furthest: a function type that names another several
      // times would be looked at again through each path to it.
      llvm::DenseMap<const Type *, int> LookedTypes;
      std::function<void(QualType, int)> AddType = [&](QualType Type,
                                                       int Depth) {
        if (Depth > 8)
          return;
        if (auto [It, Inserted] = LookedTypes.try_emplace(
                Type.getCanonicalType().getTypePtr(), Depth);
            !Inserted) {
          if (It->second <= Depth)
            return;
          It->second = Depth;
        }
        while (Type->isPointerType() || Type->isReferenceType() ||
               Type->isArrayType())
          Type = Type->isArrayType()
                     ? QualType(Type->getArrayElementTypeNoTypeQual(), 0)
                     : Type->getPointeeType();
        if (const CXXRecordDecl *Record = Type->getAsCXXRecordDecl())
          Classes.push_back(Record);
        else if (const auto *Enum = Type->getAs<EnumType>())
          AddOwner(Enum->getDecl());
        else if (const auto *Function = Type->getAs<FunctionProtoType>()) {
          AddType(Function->getReturnType(), Depth + 1);
          for (const QualType Parameter : Function->getParamTypes())
            AddType(Parameter, Depth + 1);
        }
        // And of a pointer to a member, `int NData::CNode::*`, of its class
        // and of what it points to.
        else if (const auto *Member = Type->getAs<MemberPointerType>()) {
          if (const CXXRecordDecl *Of = Member->getMostRecentCXXRecordDecl())
            Classes.push_back(Of);
          AddType(Member->getPointeeType(), Depth + 1);
        }
      };
      // Also those of a pack, `TCIterator<CTag, CMore>` of `typename
      // ...tp_CTags`, and of a template, `TCIterator<NData::TCItem>`.
      std::function<void(const TemplateArgument &)> Add =
          [&](const TemplateArgument &Argument) {
            if (Argument.getKind() == TemplateArgument::Pack) {
              for (const TemplateArgument &Element : Argument.pack_elements())
                Add(Element);
              return;
            }
            if (Argument.getKind() == TemplateArgument::Template) {
              if (const TemplateDecl *Template =
                      Argument.getAsTemplate().getAsTemplateDecl())
                AddOwner(Template);
              return;
            }
            if (Argument.getKind() == TemplateArgument::Type)
              AddType(Argument.getAsType(), 0);
          };
      for (const TemplateArgument &Argument :
           Specialization->getTemplateArgs().asArray())
        Add(Argument);
    }
  }
  // Also the parent of an inline namespace, which argument-dependent lookup
  // looks in too.
  for (const DeclContext *Associated : Namespaces)
    for (const DeclContext *Namespace = Associated; Namespace;) {
      DeclarationNameTable &Names =
          Definition->getASTContext().DeclarationNames;
      for (const OverloadedOperatorKind Operator : Operators) {
        if (Operator == OO_Call || Operator == OO_Arrow ||
            Operator == OO_Subscript || Operator == OO_Equal)
          continue;
        for (const NamedDecl *Named :
             Namespace->lookup(Names.getCXXOperatorName(Operator)))
          if (ConsiderFree(Named))
            return true;
      }
      const auto *Inline = dyn_cast<NamespaceDecl>(Namespace);
      Namespace = Inline && Inline->isInline() ? Inline->getParent() : nullptr;
    }
  // An operator it declares, also one that can't be used, hides those of its
  // bases, `void operator()() = delete;` of a class derived from a function
  // object.
  llvm::SmallVector<OverloadedOperatorKind, 4> Remaining;
  for (const OverloadedOperatorKind Operator : Operators)
    if (llvm::none_of(Definition->decls(), [&](const Decl *Member) {
          const auto *Named = dyn_cast<NamedDecl>(Member);
          return Named &&
                 Named->getDeclName().getNameKind() ==
                     DeclarationName::CXXOperatorName &&
                 Named->getDeclName().getCXXOverloadedOperator() == Operator;
        }))
      Remaining.push_back(Operator);
  if (!Remaining.empty())
    for (const CXXBaseSpecifier &Base : Definition->bases()) {
      // A base that depends on parameters of a template can be anything.
      if (Base.getType()->isDependentType()) {
        Unknown = true;
        continue;
      }
      const std::optional<bool> Has = hasOverloadedOperator(
          Base.getType()->getAsCXXRecordDecl(), Remaining, Visited);
      if (Has && *Has)
        return true;
      Unknown |= !Has;
    }
  if (Unknown)
    return std::nullopt;
  return false;
}

const NamedDecl *diagnosticDecl(const NamedDecl *Declaration) {
  if (const auto *Function = dyn_cast_or_null<FunctionDecl>(Declaration))
    if (const FunctionDecl *Pattern = Function->getTemplateInstantiationPattern())
      return Pattern;
  if (const auto *Record = dyn_cast_or_null<CXXRecordDecl>(Declaration))
    if (const CXXRecordDecl *Pattern = Record->getTemplateInstantiationPattern())
      return Pattern;
  return Declaration;
}

static bool hasAtMostParts(QualType Type, int &Budget);

/// Whether the template arguments \p Arguments, also those of the packs among
/// them, have at most \p Budget parts, of the types among them.
static bool argumentsHaveAtMostParts(ArrayRef<TemplateArgument> Arguments,
                                     int &Budget) {
  return llvm::all_of(Arguments, [&](const TemplateArgument &Argument) {
    if (Argument.getKind() == TemplateArgument::Pack)
      return argumentsHaveAtMostParts(Argument.pack_elements(), Budget);
    return Argument.getKind() != TemplateArgument::Type ||
           hasAtMostParts(Argument.getAsType(), Budget);
  });
}

/// Whether the template arguments of the specializations that \p Context is,
/// or is nested in, `Box<...>::Value`, have at most \p Budget parts.
static bool contextHasAtMostParts(const DeclContext *Context, int &Budget) {
  for (; Context; Context = Context->getParent())
    if (const auto *Specialization =
            dyn_cast<ClassTemplateSpecializationDecl>(Context);
        Specialization &&
        !argumentsHaveAtMostParts(Specialization->getTemplateArgs().asArray(),
                                  Budget))
      return false;
  return true;
}

/// Whether \p Type, as its canonical type, has at most \p Budget parts: of
/// what it points or refers to, the elements of an array, the template
/// arguments of a specialization, also of those a class is nested in,
/// `Box<...>::Value`, and in packs, the parameters and result of a function
/// type, and the class and type of a pointer to a member.
static bool hasAtMostParts(QualType Type, int &Budget) {
  if (Type.isNull())
    return true;
  if (--Budget < 0)
    return false;
  const clang::Type *Canonical = Type.getCanonicalType().getTypePtr();
  if (const auto *Pointer = dyn_cast<PointerType>(Canonical))
    return hasAtMostParts(Pointer->getPointeeType(), Budget);
  if (const auto *Reference = dyn_cast<ReferenceType>(Canonical))
    return hasAtMostParts(Reference->getPointeeType(), Budget);
  if (const auto *Array = dyn_cast<ArrayType>(Canonical))
    return hasAtMostParts(Array->getElementType(), Budget);
  if (const auto *MemberPointer = dyn_cast<MemberPointerType>(Canonical))
    return contextHasAtMostParts(MemberPointer->getMostRecentCXXRecordDecl(),
                                 Budget) &&
           hasAtMostParts(MemberPointer->getPointeeType(), Budget);
  if (const auto *Function = dyn_cast<FunctionProtoType>(Canonical)) {
    if (!hasAtMostParts(Function->getReturnType(), Budget))
      return false;
    return llvm::all_of(Function->param_types(), [&](QualType Parameter) {
      return hasAtMostParts(Parameter, Budget);
    });
  }
  return contextHasAtMostParts(Canonical->getAsCXXRecordDecl(), Budget);
}

bool isTooLargeToPrint(QualType Type) {
  int Budget = 64;
  return !hasAtMostParts(Type, Budget);
}

/// Whether the template arguments of a pack, \p Arguments, are too large to
/// print.
static bool hasTooLargeTemplateArguments(ArrayRef<TemplateArgument> Arguments) {
  return llvm::any_of(Arguments, [](const TemplateArgument &Argument) {
    if (Argument.getKind() == TemplateArgument::Pack)
      return hasTooLargeTemplateArguments(Argument.pack_elements());
    return Argument.getKind() == TemplateArgument::Type &&
           isTooLargeToPrint(Argument.getAsType());
  });
}

bool hasTooLargeTemplateArguments(const NamedDecl *Declaration) {
  if (const auto *Function = dyn_cast<FunctionDecl>(Declaration))
    if (const TemplateArgumentList *Arguments =
            Function->getTemplateSpecializationArgs();
        Arguments && hasTooLargeTemplateArguments(Arguments->asArray()))
      return true;
  for (const DeclContext *Context = isa<DeclContext>(Declaration)
                                        ? cast<DeclContext>(Declaration)
                                        : Declaration->getDeclContext();
       Context; Context = Context->getParent())
    if (const auto *Specialization =
            dyn_cast<ClassTemplateSpecializationDecl>(Context);
        Specialization && hasTooLargeTemplateArguments(
                              Specialization->getTemplateArgs().asArray()))
      return true;
  return false;
}

std::string abbreviatedName(const NamedDecl *Declaration) {
  std::string Name = Declaration->getNameAsString();
  const auto *Function = dyn_cast<FunctionDecl>(Declaration);
  if (isa<ClassTemplateSpecializationDecl>(Declaration) ||
      (Function && Function->getTemplateSpecializationArgs()))
    Name += "<...>";
  for (const DeclContext *Context = Declaration->getDeclContext(); Context;
       Context = Context->getParent()) {
    // Of the namespaces and classes it is in, but an inline namespace, like
    // std::__1, as clang prints it.
    const auto *Named = dyn_cast<NamedDecl>(Context);
    if (!isa<NamespaceDecl, RecordDecl>(Context) || !Named ||
        Named->getName().empty() ||
        (isa<NamespaceDecl>(Context) &&
         cast<NamespaceDecl>(Context)->isInline()))
      continue;
    std::string Part = Named->getNameAsString();
    if (isa<ClassTemplateSpecializationDecl>(Context))
      Part += "<...>";
    Name = Part + "::" + Name;
  }
  return Name;
}

bool hasQualifiedName(const NamedDecl *Declaration, StringRef Name) {
  if (!Declaration->getIdentifier())
    return false;
  SmallVector<StringRef, 4> Parts;
  Name.ltrim(':').split(Parts, "::");
  if (Parts.pop_back_val() != Declaration->getName())
    return false;
  const DeclContext *Context = Declaration->getDeclContext();
  // Past the transparent contexts, and the inline namespaces the name leaves
  // out, like std::__1 of `std::function`.
  auto SkipUnnamed = [&] {
    while (Context &&
           (Context->isTransparentContext() ||
            (Context->isInlineNamespace() &&
             (Parts.empty() ||
              cast<NamespaceDecl>(Context)->getName() != Parts.back()))))
      Context = Context->getParent();
  };
  while (!Parts.empty()) {
    SkipUnnamed();
    const auto *Named = dyn_cast_or_null<NamedDecl>(Context);
    if (!Named || !Named->getIdentifier() ||
        Named->getName() != Parts.pop_back_val())
      return false;
    Context = Context->getParent();
  }
  SkipUnnamed();
  return !Context || Context->isTranslationUnit();
}

/// \p Type without its parts, which are too many to print, `const
/// TCFunction<...> &`: what it is a reference or a pointer to, and the
/// template it specializes.
static std::string abbreviatedType(QualType Type) {
  QualType Canonical = Type.getCanonicalType();
  std::string Suffix;
  for (int Depth = 0; Depth < 8; ++Depth)
    if (const auto *Reference = dyn_cast<ReferenceType>(Canonical)) {
      Suffix = (isa<LValueReferenceType>(Reference) ? " &" : " &&") + Suffix;
      Canonical = Reference->getPointeeType();
    } else if (const auto *Pointer = dyn_cast<PointerType>(Canonical)) {
      std::string Qualifiers = Canonical.getLocalQualifiers().getAsString();
      Suffix = " *" + (Qualifiers.empty() ? "" : " " + Qualifiers) + Suffix;
      Canonical = Pointer->getPointeeType();
    } else
      break;
  // The name of a class, without the template arguments of it and of those
  // it is nested in, `Box<...>::Value`.
  std::string Base = "...";
  if (const CXXRecordDecl *Record = Canonical->getAsCXXRecordDecl();
      Record && !Record->getName().empty())
    Base = abbreviatedName(Record);
  const std::string Qualifiers = Canonical.getLocalQualifiers().getAsString();
  return Qualifiers.empty() ? Base : Qualifiers + " " + Base;
}

/// Whether \p Type, as it is written, an alias by its name, has at most \p
/// Budget parts, like hasAtMostParts tells of what it is.
static bool hasAtMostWrittenParts(QualType Type, int &Budget) {
  for (int Depth = 0; !Type.isNull() && Depth < 64; ++Depth) {
    if (--Budget < 0)
      return false;
    const clang::Type *Written = Type.getTypePtr();
    // An alias is printed by its name, but a specialization of a class
    // template with its arguments, below.
    if (isa<TypedefType, UsingType, EnumType, BuiltinType, TemplateTypeParmType,
            InjectedClassNameType>(Written))
      return true;
    if (const auto *Substituted = dyn_cast<SubstTemplateTypeParmType>(Written))
      Type = Substituted->getReplacementType();
    else if (const auto *Pointer = dyn_cast<PointerType>(Written))
      Type = Pointer->getPointeeType();
    else if (const auto *Reference = dyn_cast<ReferenceType>(Written))
      Type = Reference->getPointeeTypeAsWritten();
    else if (const auto *Paren = dyn_cast<ParenType>(Written))
      Type = Paren->getInnerType();
    else if (const auto *Function = dyn_cast<FunctionProtoType>(Written)) {
      if (!hasAtMostWrittenParts(Function->getReturnType(), Budget))
        return false;
      return llvm::all_of(Function->param_types(), [&](QualType Parameter) {
        return hasAtMostWrittenParts(Parameter, Budget);
      });
    } else if (const auto *Specialization =
                   dyn_cast<TemplateSpecializationType>(Written)) {
      if (Specialization->isTypeAlias())
        return true;
      return llvm::all_of(
          Specialization->template_arguments(),
          [&](const TemplateArgument &Argument) {
            return Argument.getKind() != TemplateArgument::Type ||
                   hasAtMostWrittenParts(Argument.getAsType(), Budget);
          });
    } else {
      // What the rest is, which can't be told, by what it is.
      return hasAtMostParts(Type, Budget);
    }
  }
  return true;
}

std::string diagnosticType(QualType Type, const ASTContext &Context) {
  const PrintingPolicy &Policy = Context.getPrintingPolicy();
  // Not one too large to print, whose parts can be many more than its size,
  // `TCFunction<CPrevious (CPrevious)>` of the one before, many times: as
  // written, by the name of an alias, when that isn't too large, without
  // what it is an alias of.
  int WrittenBudget = 64;
  if (!hasAtMostWrittenParts(Type, WrittenBudget))
    return "'" + abbreviatedType(Type) + "'";
  const std::string Written = "'" + Type.getAsString(Policy) + "'";
  if (isTooLargeToPrint(Type))
    return Written;
  // What it is an alias of where a diagnostic of clang would say it.
  bool ShouldAKA = false;
  const QualType Desugared =
      desugarForDiagnostic(const_cast<ASTContext &>(Context), Type, ShouldAKA);
  if (!ShouldAKA)
    return Written;
  const std::string Aka = "'" + Desugared.getAsString(Policy) + "'";
  return Aka == Written ? Written : Written + " (aka " + Aka + ")";
}

bool isExternInstantiation(const Decl *D) {
  // The explicit instantiation definition of a class instantiates its
  // members, also of its nested classes, but not its member templates, like
  // `operator<< <TCStr>` of `TCFormat<CStrTraits_CStr>`: a specialization of
  // one is covered only by an explicit instantiation of its own.
  auto Covered = [](TemplateSpecializationKind Kind) {
    return Kind == TSK_ExplicitInstantiationDeclaration;
  };
  for (int Depth = 0; D && Depth < 64; ++Depth) {
    if (const auto *Function = dyn_cast<FunctionDecl>(D)) {
      if (Covered(Function->getTemplateSpecializationKind()))
        return true;
      // Not the call operator of a generic lambda, which is instantiated
      // with the function it is in.
      if (Function->getPrimaryTemplate() && !isLambdaCallOperator(Function))
        return false;
    }
    if (const auto *Record = dyn_cast<CXXRecordDecl>(D)) {
      if (Covered(Record->getTemplateSpecializationKind()))
        return true;
      if (const auto *Specialization =
              dyn_cast<ClassTemplateSpecializationDecl>(Record);
          Specialization && Specialization->getSpecializationKind() ==
                                TSK_ImplicitInstantiation)
        return false;
    }
    if (const auto *Variable = dyn_cast<VarDecl>(D)) {
      if (Covered(Variable->getTemplateSpecializationKind()))
        return true;
      if (isa<VarTemplateSpecializationDecl>(Variable))
        return false;
    }
    const DeclContext *Context = D->getDeclContext();
    if (!Context || isa<TranslationUnitDecl, NamespaceDecl>(Context))
      return false;
    D = Decl::castFromDeclContext(Context);
  }
  return false;
}

static bool isInTemplatePatternUncached(const DynTypedNode &Node,
                                        ASTContext &Context);

bool isInTemplatePattern(const DynTypedNode &Node, ASTContext &Context) {
  // Of a node with one parent, what that parent is in, decided once for each
  // node of the chain up: nested expressions, like deeply nested awaits, would
  // otherwise go up the same chain again for each of them.
  struct CPatterns {
    llvm::DenseMap<const void *, bool> InPattern;
  };
  auto &Decided = astCache<CPatterns>(Context).InPattern;
  llvm::SmallVector<const void *, 16> Chain;
  DynTypedNode Current = Node;
  std::optional<bool> Result;
  for (int Steps = 0; Steps < 4096 && !Result; ++Steps) {
    const void *Key = Current.getMemoizationData();
    if (Key)
      if (auto It = Decided.find(Key); It != Decided.end()) {
        Result = It->second;
        break;
      }
    auto DecideHere = [&] {
      Result = isInTemplatePatternUncached(Current, Context);
      if (Key)
        Decided[Key] = *Result;
    };
    if (Current.get<Decl>()) {
      DecideHere();
      break;
    }
    const DynTypedNodeList Parents =
        Context.getParentMapContext().getParents(Current);
    if (Parents.size() != 1) {
      DecideHere();
      break;
    }
    if (Key)
      Chain.push_back(Key);
    Current = Parents[0];
  }
  if (!Result)
    return isInTemplatePatternUncached(Node, Context);
  for (const void *Key : Chain)
    Decided[Key] = *Result;
  return *Result;
}

static bool isInTemplatePatternUncached(const DynTypedNode &Node,
                                        ASTContext &Context) {
  // Each path up, past a node that an instantiation shares with its template,
  // to the declaration it is in: in a template only when each is.
  llvm::SmallVector<DynTypedNode, 4> Pending{Node};
  llvm::SmallPtrSet<const void *, 8> Seen;
  for (int Steps = 0; !Pending.empty(); ++Steps) {
    if (Steps >= 4096)
      return false;
    const DynTypedNode Current = Pending.pop_back_val();
    if (const auto *Enclosing = Current.get<Decl>()) {
      if (!Enclosing->getDeclContext() ||
          (!Enclosing->getDeclContext()->isDependentContext() &&
           !(isa<DeclContext>(Enclosing) &&
             cast<DeclContext>(Enclosing)->isDependentContext()) &&
           !isExternInstantiation(Enclosing)))
        return false;
      continue;
    }
    const DynTypedNodeList Parents =
        Context.getParentMapContext().getParents(Current);
    if (Parents.empty())
      return false;
    if (Parents.size() == 1) {
      Pending.push_back(Parents[0]);
      continue;
    }
    for (const DynTypedNode &Parent : Parents)
      if (!Parent.getMemoizationData() ||
          Seen.insert(Parent.getMemoizationData()).second)
        Pending.push_back(Parent);
  }
  return true;
}

llvm::SmallVector<DynTypedNode, 2> instantiatedParents(const DynTypedNode &Node,
                                                       ASTContext &Context) {
  const DynTypedNodeList Parents =
      Context.getParentMapContext().getParents(Node);
  llvm::SmallVector<DynTypedNode, 2> Ordered(Parents.begin(), Parents.end());
  if (Ordered.size() > 1)
    std::stable_partition(Ordered.begin(), Ordered.end(),
                          [&](const DynTypedNode &Parent) {
                            return !isInTemplatePattern(Parent, Context);
                          });
  return Ordered;
}

bool isInDiscardedBranch(const DynTypedNode &Node, ASTContext &Context) {
  DynTypedNode Current = Node;
  for (int Depth = 0; Depth < 256; ++Depth) {
    const auto Parents = instantiatedParents(Current, Context);
    if (Parents.empty())
      return false;
    const DynTypedNode &Parent = Parents[0];
    if (const auto *If = Parent.get<IfStmt>();
        If && If->isConstexpr() && !If->getCond()->isValueDependent()) {
      // Also past implicit nodes, like the cleanups of an unbraced branch,
      // which a parent map can skip.
      auto Same = [&](const Stmt *Branch) {
        const Stmt *Child = Current.get<Stmt>();
        if (!Branch || !Child)
          return false;
        if (Branch == Child)
          return true;
        const auto *BranchExpr = dyn_cast<Expr>(Branch);
        const auto *ChildExpr = dyn_cast<Expr>(Child);
        return BranchExpr && ChildExpr &&
               BranchExpr->IgnoreUnlessSpelledInSource() ==
                   ChildExpr->IgnoreUnlessSpelledInSource();
      };
      const Stmt *Branch = Same(If->getThen())   ? If->getThen()
                           : Same(If->getElse()) ? If->getElse()
                                                 : nullptr;
      if (Branch)
        if (const std::optional<const Stmt *> Case =
                If->getNondiscardedCase(Context);
            Case && *Case != Branch)
          return true;
    }
    // Up to the function it is in.
    if (Parent.get<FunctionDecl>() || Parent.get<TranslationUnitDecl>())
      return false;
    Current = Parent;
  }
  return false;
}

bool isInUnevaluatedOperand(const DynTypedNode &Node, ASTContext &Context) {
  // Each path up, past nodes with several parents, like the expressions an
  // instantiation shares with its template, and a default argument its uses
  // share: in an unevaluated operand only when each is, not when another use
  // of a default argument than one in a `decltype` evaluates it.
  llvm::SmallVector<DynTypedNode, 8> Work{Node};
  llvm::SmallPtrSet<const void *, 16> Seen;
  // Whether the node a path is at is in an unevaluated operand as its parent.
  auto Unevaluated = [&](const DynTypedNode &Current,
                         const DynTypedNode &Parent) {
    if (Parent.get<TypeLoc>())
      return true;
    const auto *E = Parent.get<Expr>();
    if (!E)
      return false;
    if (isa<CXXNoexceptExpr, RequiresExpr>(E))
      return true;
    if (const auto *Typeid = dyn_cast<CXXTypeidExpr>(E);
        Typeid && !Typeid->isPotentiallyEvaluated())
      return true;
    if (const auto *Trait = dyn_cast<UnaryExprOrTypeTraitExpr>(E);
        Trait &&
        (Trait->getKind() == UETT_SizeOf || Trait->getKind() == UETT_AlignOf))
      return true;
    // An arm that isn't chosen, of `__builtin_choose_expr` or `_Generic`,
    // and their conditions, which are told at compile time.
    const auto *Child = Current.get<Expr>();
    if (const auto *Choose = dyn_cast<ChooseExpr>(E);
        Choose && !Choose->isConditionDependent() &&
        Child != Choose->getChosenSubExpr())
      return true;
    if (const auto *Selection = dyn_cast<GenericSelectionExpr>(E);
        Selection && !Selection->isResultDependent() &&
        Child != Selection->getResultExpr())
      return true;
    // What a builtin only looks at the type or constness of,
    // `__builtin_constant_p(Value)`, but not
    // `__builtin_dynamic_object_size`, which can evaluate it.
    if (const auto *Call = dyn_cast<CallExpr>(E);
        Call &&
        (Call->getBuiltinCallee() == Builtin::BI__builtin_constant_p ||
         Call->getBuiltinCallee() == Builtin::BI__builtin_object_size) &&
        Child != Call->getCallee())
      return true;
    return false;
  };
  for (int Steps = 0; !Work.empty() && Steps < 4096; ++Steps) {
    const DynTypedNode Current = Work.pop_back_val();
    bool Continues = false;
    bool Ends = false;
    for (const DynTypedNode &Parent : instantiatedParents(Current, Context)) {
      // A default argument, `TCFuture<int> _Future = fg_Borrow(...)`, runs at
      // each call that leaves it out, also where the parameter is in a type.
      if (const auto *Parameter = Parent.get<ParmVarDecl>();
          Parameter && Current.get<Expr>() && Parameter->hasDefaultArg() &&
          !Parameter->hasUnparsedDefaultArg() &&
          !Parameter->hasUninstantiatedDefaultArg() &&
          (Parameter->getDefaultArg() == Current.get<Expr>() ||
           Parameter->getInit() == Current.get<Expr>()))
        continue;
      if (Unevaluated(Current, Parent)) {
        Ends = true;
        continue;
      }
      // Up to the declaration a statement is in and on, through those that
      // are in other declarations, like a local class, where it is evaluated.
      if (Parent.get<TranslationUnitDecl>())
        return false;
      Continues = true;
      const void *Key = Parent.getMemoizationData();
      if (!Key || Seen.insert(Key).second)
        Work.push_back(Parent);
    }
    // A path that ends without an unevaluated operand, like that of a default
    // argument no call uses, is evaluated.
    if (!Continues && !Ends)
      return false;
  }
  return Work.empty();
}

std::optional<bool>
hasOverloadedOperator(const CXXRecordDecl *Record,
                      llvm::ArrayRef<OverloadedOperatorKind> Operators) {
  llvm::SmallPtrSet<const CXXRecordDecl *, 8> Visited;
  return hasOverloadedOperator(Record, Operators, Visited);
}

bool isUnevaluatedNode(const Stmt *S) {
  if (isa_and_nonnull<UnaryExprOrTypeTraitExpr, CXXNoexceptExpr, RequiresExpr>(
          S))
    return true;
  if (const auto *Typeid = dyn_cast_or_null<CXXTypeidExpr>(S))
    return !Typeid->isPotentiallyEvaluated();
  const auto *Call = dyn_cast_or_null<CallExpr>(S);
  return Call &&
         (Call->getBuiltinCallee() == Builtin::BI__builtin_constant_p ||
          Call->getBuiltinCallee() == Builtin::BI__builtin_object_size ||
          Call->getBuiltinCallee() == Builtin::BI__builtin_dynamic_object_size);
}

bool onlyReadsParameter(const FunctionDecl *Function, unsigned iParameter,
                        int Depth) {
  // A copy constructor that is defaulted, also of what isn't const,
  // `TCOptional(TCOptional &) = default;`, copies each base and member, which
  // reads them, when each can be copied from what is const.
  if (const auto *Constructor = dyn_cast_or_null<CXXConstructorDecl>(Function);
      Constructor && Constructor->isDefaulted() &&
      Constructor->isCopyConstructor() &&
      Constructor->getParent()->hasDefinition() &&
      Constructor->getParent()
          ->getDefinition()
          ->implicitCopyConstructorHasConstParam())
    return true;
  const FunctionDecl *Definition =
      Function ? Function->getDefinition() : nullptr;
  if (!Definition || !Definition->getBody() || Depth > 4 ||
      iParameter >= Definition->getNumParams())
    return false;
  const ParmVarDecl *Parameter = Definition->getParamDecl(iParameter);
  ASTContext &Context = const_cast<ASTContext &>(Definition->getASTContext());
  // Whether what \p Use gives of it, the parameter or a member of it, is only
  // read where it is used.
  auto OnlyRead = [&](const Expr *Use) {
    for (int Level = 0; Level < 16; ++Level) {
      const DynTypedNodeList Parents =
          Context.getParentMapContext().getParents(*Use);
      const auto *Up = Parents.empty() ? nullptr : Parents[0].get<Expr>();
      if (!Up)
        return false;
      if (const auto *Cast = dyn_cast<ImplicitCastExpr>(Up)) {
        if (Cast->getCastKind() == CK_LValueToRValue)
          return true;
        if (Cast->getCastKind() != CK_NoOp &&
            Cast->getCastKind() != CK_DerivedToBase &&
            Cast->getCastKind() != CK_UncheckedDerivedToBase)
          return false;
        Use = Cast;
        continue;
      }
      if (isa<ParenExpr>(Up)) {
        Use = Up;
        continue;
      }
      // A member of it, `_Other.m_Length`, read in turn. Not a const member
      // function that gives what can be written through, `f_Get()` of a
      // shared pointer, `_pState.f_Get()->m_Bits = 0`.
      if (const auto *Member = dyn_cast<MemberExpr>(Up);
          Member && Member->getBase()->IgnoreParenImpCasts() ==
                        Use->IgnoreParenImpCasts()) {
        if (const auto *Method =
                dyn_cast<CXXMethodDecl>(Member->getMemberDecl())) {
          const QualType Gives = Method->getReturnType();
          const bool Writable =
              (Gives->isPointerType() || Gives->isReferenceType()) &&
              !Gives->getPointeeType().isConstQualified();
          return (Method->isConst() && !Writable) || Method->isStatic();
        }
        Use = Member;
        continue;
      }
      // Given to what takes it as const or by value, or only reads it.
      auto Given = [&](const FunctionDecl *Callee,
                       llvm::ArrayRef<const Expr *> Arguments,
                       unsigned Offset) {
        for (unsigned iArgument = 0; iArgument < Arguments.size();
             ++iArgument) {
          if (Arguments[iArgument]->IgnoreParenImpCasts() !=
              Use->IgnoreParenImpCasts())
            continue;
          if (!Callee || iArgument < Offset ||
              iArgument - Offset >= Callee->getNumParams())
            return false;
          const QualType Type =
              Callee->getParamDecl(iArgument - Offset)->getType();
          return !Type->isReferenceType() ||
                 Type.getNonReferenceType().isConstQualified() ||
                 onlyReadsParameter(Callee, iArgument - Offset, Depth + 1);
        }
        return false;
      };
      // Captured by a lambda as it is, `[&] { return CNode(_Key); }`, whose
      // body is looked at in turn, or copied into it, but not given another
      // name it is written through, `[&Key = _Key]`.
      if (const auto *Lambda = dyn_cast<LambdaExpr>(Up)) {
        for (const auto &[Capture, Init] :
             llvm::zip(Lambda->captures(), Lambda->capture_inits()))
          if (Init && Init->IgnoreParenImpCasts() == Use->IgnoreParenImpCasts())
            return Capture.capturesVariable() &&
                   (Capture.getCaptureKind() == LCK_ByCopy ||
                    !Capture.getCapturedVar()->isInitCapture());
        return false;
      }
      // Forwarded or moved, `fg_Forward<tf_CKey>(_Key)`, read where what that
      // gives is.
      if (const auto *Forward = dyn_cast<CallExpr>(Up);
          Forward && Forward->getNumArgs() == 1 &&
          Forward->getArg(0)->IgnoreParenImpCasts() ==
              Use->IgnoreParenImpCasts() &&
          Forward->getDirectCallee() &&
          Forward->getDirectCallee()->getIdentifier() &&
          isMoveOrForwardFunction(Forward->getDirectCallee()->getName()) &&
          Forward->isGLValue()) {
        Use = Forward;
        continue;
      }
      if (const auto *Construct = dyn_cast<CXXConstructExpr>(Up))
        return Given(
            Construct->getConstructor(),
            llvm::ArrayRef(Construct->getArgs(), Construct->getNumArgs()), 0);
      // Also to a member function, `fp_Find(_Key)`, whose object is looked at
      // above.
      if (const auto *Call = dyn_cast<CallExpr>(Up))
        return Given(Call->getDirectCallee(),
                     llvm::ArrayRef(Call->getArgs(), Call->getNumArgs()),
                     isa<CXXOperatorCallExpr>(Call) &&
                             hasImplicitObject(Call->getDirectCallee())
                         ? 1
                         : 0);
      return false;
    }
    return false;
  };
  bool Reads = true;
  std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
    if (!S || !Reads)
      return;
    if (const auto *Ref = dyn_cast<DeclRefExpr>(S);
        Ref && Ref->getDecl() == Parameter && !OnlyRead(Ref))
      Reads = false;
    for (const Stmt *Child : childrenOnce(S))
      Visit(Child);
  };
  if (const auto *Constructor = dyn_cast<CXXConstructorDecl>(Definition))
    for (const CXXCtorInitializer *Initializer : Constructor->inits())
      Visit(Initializer->getInit());
  Visit(Definition->getBody());
  return Reads;
}

bool onlyAssignsThrough(const FunctionDecl *Function, unsigned iParameter) {
  const FunctionDecl *Definition =
      Function ? Function->getDefinition() : nullptr;
  if (!Definition || !Definition->getBody() ||
      iParameter >= Definition->getNumParams())
    return false;
  const ParmVarDecl *Parameter = Definition->getParamDecl(iParameter);
  const bool ThroughPointer = Parameter->getType()->isPointerType();
  if (!ThroughPointer && !Parameter->getType()->isReferenceType())
    return false;
  bool Only = true;
  std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
    if (!S || !Only)
      return;
    if (const auto *Assign = dyn_cast<BinaryOperator>(S);
        Assign && Assign->getOpcode() == BO_Assign) {
      const Expr *Target = Assign->getLHS()->IgnoreParens();
      while (const auto *Member = dyn_cast<MemberExpr>(Target)) {
        if (Member->isArrow())
          break;
        Target = Member->getBase()->IgnoreParens();
      }
      if (ThroughPointer) {
        const auto *Deref = dyn_cast<UnaryOperator>(Target);
        Target = Deref && Deref->getOpcode() == UO_Deref
                     ? Deref->getSubExpr()->IgnoreParenImpCasts()
                     : nullptr;
      }
      if (const auto *Ref = dyn_cast_or_null<DeclRefExpr>(Target);
          Ref && Ref->getDecl() == Parameter) {
        Visit(Assign->getRHS());
        return;
      }
    }
    if (const auto *Ref = dyn_cast<DeclRefExpr>(S);
        Ref && Ref->getDecl() == Parameter) {
      Only = false;
      return;
    }
    for (const Stmt *Child : childrenOnce(S))
      Visit(Child);
  };
  Visit(Definition->getBody());
  return Only;
}

const LambdaExpr *calledLambda(const Expr *Callee) {
  for (int Step = 0; Callee && Step < 8; ++Step) {
    Callee = Callee->IgnoreImplicit()->IgnoreParens();
    if (const auto *Lambda = dyn_cast<LambdaExpr>(Callee))
      return Lambda;
    if (const auto *Copy = dyn_cast<CXXConstructExpr>(Callee);
        Copy && Copy->getNumArgs() == 1) {
      Callee = Copy->getArg(0);
      continue;
    }
    // Also moved, forwarded or copied by a helper that gives it back,
    // `fg_Move(fClear)` or `fg_TempCopy(fClear)`.
    if (const auto *Helper = dyn_cast<CallExpr>(Callee);
        Helper && Helper->getNumArgs() == 1 && Helper->getDirectCallee() &&
        Helper->getDirectCallee()->getIdentifier() &&
        (isMoveOrForwardFunction(Helper->getDirectCallee()->getName()) ||
         isCopyFunction(Helper->getDirectCallee()->getName()))) {
      Callee = Helper->getArg(0);
      continue;
    }
    if (const auto *Unary = dyn_cast<UnaryOperator>(Callee);
        Unary && (Unary->getOpcode() == UO_Deref ||
                  Unary->getOpcode() == UO_AddrOf)) {
      Callee = Unary->getSubExpr();
      continue;
    }
    const auto *Ref = dyn_cast<DeclRefExpr>(Callee);
    const auto *Local = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
    if (!Local || !Local->hasLocalStorage())
      return nullptr;
    // What it has where it is called, also a function object assigned
    // another, `fWork = [&] { _Bits = 0; };`, and a pointer pointed
    // elsewhere.
    Callee = assignedValueAt(Local, Ref);
  }
  return nullptr;
}

/// The arguments of \p Call that the function it calls returns as they are,
/// on any of its returns, like `_pData` of `int const *fg_Get(int const
/// *_pData) { return _pData; }`, also after an early `return nullptr;`, both
/// of `_pFirst ? _pFirst : _pSecond`.
const Expr *keptPointerArgument(const CallExpr *Call) {
  if (!Call || Call->getNumArgs() < 1)
    return nullptr;
  const unsigned Builtin = Call->getBuiltinCallee();
  if (Builtin == Builtin::BI__builtin_launder ||
      Builtin == Builtin::BI__builtin_assume_aligned)
    return Call->getArg(0);
  const FunctionDecl *Callee = Call->getDirectCallee();
  if (Callee && Call->getNumArgs() == 1 && Callee->isInStdNamespace() &&
      Callee->getIdentifier() && Callee->getName() == "launder")
    return Call->getArg(0);
  return nullptr;
}

llvm::SmallVector<const Expr *, 2> returnedArguments(const CallExpr *Call,
                                                     bool Addressed) {
  llvm::SmallVector<const Expr *, 2> Arguments;
  if (const Expr *Kept = keptPointerArgument(Call)) {
    if (!Addressed)
      Arguments.push_back(Kept);
    return Arguments;
  }
  const FunctionDecl *Callee = Call->getDirectCallee();
  // Also through a pointer to it that can't be pointed elsewhere, an init
  // capture of a lambda that isn't mutable, `[Get = &fg_Address]`, or a
  // const local, `auto const fGet = &fg_Address`.
  if (!Callee)
    if (const auto *Ref =
            dyn_cast<DeclRefExpr>(Call->getCallee()->IgnoreParenImpCasts()))
      if (const auto *Pointer = dyn_cast<VarDecl>(Ref->getDecl());
          Pointer && Pointer->getInit() &&
          Pointer->getType()->isFunctionPointerType() &&
          (Pointer->getType().isConstQualified() ||
           (Pointer->isInitCapture() && Ref->getType().isConstQualified()))) {
        const Expr *Init = Pointer->getInit()->IgnoreParenImpCasts();
        if (const auto *Address = dyn_cast<UnaryOperator>(Init);
            Address && Address->getOpcode() == UO_AddrOf)
          Init = Address->getSubExpr()->IgnoreParenImpCasts();
        if (const auto *Named = dyn_cast<DeclRefExpr>(Init))
          Callee = dyn_cast<FunctionDecl>(Named->getDecl());
      }
  const FunctionDecl *Definition = Callee ? Callee->getDefinition() : nullptr;
  const Stmt *Body = Definition ? Definition->getBody() : nullptr;
  if (!Body || isa<CoroutineBodyStmt>(Body))
    return Arguments;
  // The argument of each parameter: of an operator of an object, like the
  // call operator of a lambda, `[](int const &_Value) { return &_Value;
  // }(Value)`, after the object.
  const auto *Method = dyn_cast<CXXMethodDecl>(Callee);
  const unsigned Offset = isa<CXXOperatorCallExpr>(Call) && Method &&
                                  Method->isImplicitObjectMemberFunction()
                              ? 1
                              : 0;
  auto ArgumentOf = [&](const ParmVarDecl *Parameter) -> const Expr * {
    const unsigned iArgument = Parameter->getFunctionScopeIndex() + Offset;
    return iArgument < Call->getNumArgs() ? Call->getArg(iArgument) : nullptr;
  };
  // What each return gives, also either value of a conditional, `_pData ?
  // _pData : nullptr`; not those of the lambdas in it.
  llvm::SmallVector<const Expr *, 2> Values;
  std::function<void(const Stmt *)> Returns = [&](const Stmt *S) {
    if (!S || isa<LambdaExpr>(S))
      return;
    if (const auto *Return = dyn_cast<ReturnStmt>(S)) {
      // Also of conditionals in conditionals, `bA ? (bB ? _pData : nullptr) :
      // nullptr`.
      std::function<void(const Expr *, int)> Add = [&](const Expr *Value,
                                                       int Depth) {
        if (!Value)
          return;
        Value = Value->IgnoreParenImpCasts();
        if (const auto *Conditional =
                dyn_cast<AbstractConditionalOperator>(Value);
            Conditional && Depth < 64) {
          Add(Conditional->getTrueExpr(), Depth + 1);
          Add(Conditional->getFalseExpr(), Depth + 1);
        } else
          Values.push_back(Value);
      };
      Add(Return->getRetValue(), 0);
      return;
    }
    for (const Stmt *Child : childrenOnce(S))
      Returns(Child);
  };
  Returns(Body);
  // Also what is assigned to a parameter that is returned, which is what it
  // gives after the assignment, `_pFallback = _pEntry; return _pFallback;`,
  // and, on the paths that assign it, `if (...) _pFallback = _pEntry;`.
  llvm::SmallPtrSet<const ParmVarDecl *, 2> Reassigned;
  for (size_t iValue = 0; iValue < Values.size() && iValue < 64; ++iValue) {
    const Expr *Returned = Values[iValue];
    // Also through a local, `int *pResult = _pData; return pResult;`, a
    // cast of a pointer, `static_cast<int *>(_pData)`, and pointer
    // arithmetic, `_pData + 1`, which stays in what it points into.
    for (int Depth = 0; Depth < 8; ++Depth) {
      // Also a cast to a reference, `static_cast<t_CType &&>(_Value)` of
      // `std::move` and `std::forward`, which is of what it is given.
      if (const auto *Cast = dyn_cast_or_null<ExplicitCastExpr>(Returned);
          Cast && (Cast->getType()->isPointerType() || Cast->isGLValue())) {
        Returned = Cast->getSubExpr()->IgnoreParenImpCasts();
        continue;
      }
      // Also its bits with tag bits masked away, `(CLink *)((umint)_pLink &
      // ~umint(1))`: a pointer made an integer and masked with a constant.
      if (const auto *ToInteger = dyn_cast_or_null<ExplicitCastExpr>(Returned);
          ToInteger && ToInteger->getType()->isIntegerType() &&
          ToInteger->getSubExpr()->getType()->isPointerType()) {
        Returned = ToInteger->getSubExpr()->IgnoreParenImpCasts();
        continue;
      }
      if (const auto *Mask = dyn_cast_or_null<BinaryOperator>(Returned);
          Mask &&
          (Mask->getOpcode() == BO_And || Mask->getOpcode() == BO_Or ||
           Mask->getOpcode() == BO_Xor) &&
          Mask->getType()->isIntegerType()) {
        const ASTContext &Context = Definition->getASTContext();
        auto Constant = [&](const Expr *Side) {
          return !Side->isValueDependent() && Side->isEvaluatable(Context);
        };
        if (Constant(Mask->getRHS()) && !Constant(Mask->getLHS())) {
          Returned = Mask->getLHS()->IgnoreParenImpCasts();
          continue;
        }
        if (Constant(Mask->getLHS()) && !Constant(Mask->getRHS())) {
          Returned = Mask->getRHS()->IgnoreParenImpCasts();
          continue;
        }
      }
      // Also the bits of a pointer as another pointer,
      // `std::bit_cast<tf_CTo>(_From)` or `__builtin_bit_cast`.
      if (const auto *Bits = dyn_cast_or_null<CallExpr>(Returned);
          Bits && Bits->getType()->isPointerType() && Bits->getNumArgs() == 1 &&
          Bits->getArg(0)->getType()->isPointerType() &&
          Bits->getDirectCallee() && Bits->getDirectCallee()->getIdentifier() &&
          Bits->getDirectCallee()->getName() == "bit_cast" &&
          Bits->getDirectCallee()->isInStdNamespace()) {
        Returned = Bits->getArg(0)->IgnoreParenImpCasts();
        continue;
      }
      if (const auto *Bits = dyn_cast_or_null<BuiltinBitCastExpr>(Returned);
          Bits && Bits->getType()->isPointerType() &&
          Bits->getSubExpr()->getType()->isPointerType()) {
        Returned = Bits->getSubExpr()->IgnoreParenImpCasts();
        continue;
      }
      // Also the address of a member of what a pointer points to,
      // `&_pValue->m_X`, or of an element of it, `&_pValue[1]`, which is in
      // what it points into.
      if (const auto *Address = dyn_cast_or_null<UnaryOperator>(Returned);
          Address && Address->getOpcode() == UO_AddrOf) {
        const Expr *Part = Address->getSubExpr()->IgnoreParenImpCasts();
        const Expr *Pointer = nullptr;
        for (int Level = 0; Part && !Pointer && Level < 64; ++Level) {
          if (const auto *Member = dyn_cast<MemberExpr>(Part))
            if (Member->isArrow())
              Pointer = Member->getBase();
            else
              Part = Member->getBase()->IgnoreParenImpCasts();
          else if (const auto *Subscript = dyn_cast<ArraySubscriptExpr>(Part))
            if (Subscript->getBase()
                    ->IgnoreParenImpCasts()
                    ->getType()
                    ->isArrayType())
              Part = Subscript->getBase()->IgnoreParenImpCasts();
            else
              Pointer = Subscript->getBase();
          else
            break;
        }
        if (Pointer) {
          Returned = Pointer->IgnoreParenImpCasts();
          continue;
        }
      }
      if (const auto *Offset = dyn_cast_or_null<BinaryOperator>(Returned);
          Offset && Offset->isAdditiveOp() &&
          Offset->getType()->isPointerType()) {
        Returned =
            (Offset->getLHS()->getType()->isPointerType() ? Offset->getLHS()
                                                          : Offset->getRHS())
                ->IgnoreParenImpCasts();
        continue;
      }
      // Also what is moved or forwarded, `auto Temp = fg_Move(_Destination);
      // return Temp;` of `fg_Exchange`.
      if (const auto *Move = dyn_cast_or_null<CallExpr>(Returned);
          Move && Move->getNumArgs() == 1 && Move->getDirectCallee() &&
          Move->getDirectCallee()->getIdentifier() &&
          isMoveOrForwardFunction(Move->getDirectCallee()->getName())) {
        Returned = Move->getArg(0)->IgnoreParenImpCasts();
        continue;
      }
      const auto *Local = dyn_cast_or_null<DeclRefExpr>(Returned);
      const auto *Variable =
          Local ? dyn_cast<VarDecl>(Local->getDecl()) : nullptr;
      if (const auto *Parameter = dyn_cast_or_null<ParmVarDecl>(Variable)) {
        // Not its default argument, which `assignedValueAt` gives when
        // nothing assigns it, but what it is given is.
        if (const Expr *Value = assignedValueAt(Parameter, Local);
            Value && Value != Parameter->getInit()) {
          Returned = Value->IgnoreParenImpCasts();
          continue;
        }
        if (Reassigned.insert(Parameter).second) {
          std::function<void(const Stmt *)> Assignments = [&](const Stmt *S) {
            if (!S || isa<LambdaExpr>(S))
              return;
            // Before where it is read, not after it is moved away, `auto
            // Temp = fg_Move(_Destination); _Destination = _SetTo;`.
            if (const auto *Assign = dyn_cast<BinaryOperator>(S);
                Assign && Assign->getOpcode() == BO_Assign)
              if (const auto *Target = dyn_cast<DeclRefExpr>(
                      Assign->getLHS()->IgnoreParenImpCasts());
                  Target && Target->getDecl() == Parameter &&
                  Definition->getASTContext()
                      .getSourceManager()
                      .isBeforeInTranslationUnit(Assign->getBeginLoc(),
                                                 Local->getBeginLoc()))
                Values.push_back(Assign->getRHS()->IgnoreParenImpCasts());
            for (const Stmt *Child : childrenOnce(S))
              Assignments(Child);
          };
          Assignments(Body);
        }
        break;
      }
      if (!Variable || !Variable->hasLocalStorage())
        break;
      const Expr *Value = assignedValueAt(Variable, Local);
      if (!Value)
        break;
      Returned = Value->IgnoreParenImpCasts();
    }
    // With \p Addressed, the address of a reference parameter, `return
    // &_Value;`, which is of what the argument designates, also of a member
    // or an element of an array of it, `return &_Value.m_Value;`.
    if (const auto *Address = dyn_cast_or_null<UnaryOperator>(Returned);
        Address && Address->getOpcode() == UO_AddrOf) {
      const Expr *Part = Address->getSubExpr()->IgnoreParenImpCasts();
      for (int Level = 0; Level < 64; ++Level)
        if (const auto *Member = dyn_cast<MemberExpr>(Part);
            Member && !Member->isArrow())
          Part = Member->getBase()->IgnoreParenImpCasts();
        else if (const auto *Subscript = dyn_cast<ArraySubscriptExpr>(Part);
                 Subscript && Subscript->getBase()
                                  ->IgnoreParenImpCasts()
                                  ->getType()
                                  ->isArrayType())
          Part = Subscript->getBase()->IgnoreParenImpCasts();
        else
          break;
      const auto *Ref = dyn_cast<DeclRefExpr>(Part);
      const auto *Parameter =
          Ref ? dyn_cast<ParmVarDecl>(Ref->getDecl()) : nullptr;
      if (Addressed && Parameter && Parameter->getType()->isReferenceType() &&
          ArgumentOf(Parameter) &&
          !llvm::is_contained(Arguments, ArgumentOf(Parameter)))
        Arguments.push_back(ArgumentOf(Parameter));
      continue;
    }
    if (Addressed)
      continue;
    const auto *Ref = dyn_cast_or_null<DeclRefExpr>(Returned);
    const auto *Parameter =
        Ref ? dyn_cast<ParmVarDecl>(Ref->getDecl()) : nullptr;
    if (Parameter && ArgumentOf(Parameter) &&
        !llvm::is_contained(Arguments, ArgumentOf(Parameter)))
      Arguments.push_back(ArgumentOf(Parameter));
  }
  return Arguments;
}

/// Whether the body of \p Definition calls its callable parameter \p
/// Parameter, `_fWork()`, also by name, `_fWork.operator()()`, a function it
/// is given by reference or a pointer, through a local
/// copy of it, `auto fCopy = _fWork`, or moved, `fg_Move(_fWork)`, which has
/// what it captures by reference, also by giving it on to another function
/// that does, `fg_Invoke(_fWork)`, but not in what isn't evaluated,
/// `noexcept(_fWork())`, nor from a lambda that is only made, `return
/// [_fWork] { _fWork(); };`, unless it is called where it is made.
bool callsParameter(
    const FunctionDecl *Definition, const ParmVarDecl *Parameter,
    llvm::SmallPtrSetImpl<const ParmVarDecl *> &Visited,
    llvm::function_ref<const FunctionDecl *(const CallExpr *)> CalledFunction,
    llvm::function_ref<bool(const CXXRecordDecl *)> DestructorCalls) {
  if (!Definition || !Definition->getBody() || !Parameter ||
      Visited.size() >= 32 || !Visited.insert(Parameter).second)
    return false;
  auto IsParameter = [&](const Expr *Callable) {
    for (int Depth = 0; Callable && Depth < 16; ++Depth) {
      Callable = Callable->IgnoreParenImpCasts();
      if (const auto *Copy = dyn_cast<CXXConstructExpr>(Callable);
          Copy && Copy->getNumArgs() == 1) {
        Callable = Copy->getArg(0);
        continue;
      }
      if (const auto *Move = dyn_cast<CallExpr>(Callable)) {
        const llvm::SmallVector<const Expr *, 2> Returned =
            returnedArguments(Move);
        if (Returned.size() != 1)
          return false;
        Callable = Returned[0];
        continue;
      }
      const auto *Ref = dyn_cast<DeclRefExpr>(Callable);
      if (!Ref)
        return false;
      if (Ref->getDecl() == Parameter)
        return true;
      const auto *Local = dyn_cast<VarDecl>(Ref->getDecl());
      if (!Local || isa<ParmVarDecl>(Local) || !Local->hasLocalStorage())
        return false;
      Callable = Local->getInit();
    }
    return false;
  };
  std::function<bool(const Stmt *)> Calls = [&](const Stmt *Inner) {
    if (!Inner ||
        isa<CXXNoexceptExpr, UnaryExprOrTypeTraitExpr, RequiresExpr>(Inner))
      return false;
    if (const auto *Typeid = dyn_cast<CXXTypeidExpr>(Inner);
        Typeid && !Typeid->isPotentiallyEvaluated())
      return false;
    if (const auto *Invoke = dyn_cast<CXXOperatorCallExpr>(Inner);
        Invoke && Invoke->getOperator() == OO_Call &&
        Invoke->getNumArgs() >= 1) {
      if (IsParameter(Invoke->getArg(0)))
        return true;
      if (const auto *Immediate =
              dyn_cast<LambdaExpr>(Invoke->getArg(0)->IgnoreParenImpCasts());
          Immediate && Calls(Immediate->getBody()))
        return true;
    }
    if (const auto *Named = dyn_cast<CXXMemberCallExpr>(Inner);
        Named && Named->getMethodDecl() &&
        Named->getMethodDecl()->getOverloadedOperator() == OO_Call &&
        IsParameter(Named->getImplicitObjectArgument()))
      return true;
    if (const auto *Made = dyn_cast<LambdaExpr>(Inner))
      return llvm::any_of(Made->capture_inits(), Calls);
    // Also by a local made with it whose destructor can call what it holds,
    // `auto Cleanup = fg_OnScopeExit(fg_Forward<tf_FCleanup>(_fCleanup));`,
    // when its scope ends, as \p DestructorCalls tells.
    if (const auto *Declaration = dyn_cast<DeclStmt>(Inner))
      for (const Decl *D : Declaration->decls())
        if (const auto *Local = dyn_cast<VarDecl>(D);
            Local && Local->hasLocalStorage() && Local->getInit() &&
            !Local->getType()->isReferenceType() &&
            DestructorCalls(Local->getType()
                                ->getBaseElementTypeUnsafe()
                                ->getAsCXXRecordDecl())) {
          std::function<bool(const Stmt *)> Gives = [&](const Stmt *Part) {
            if (!Part || isa<LambdaExpr>(Part))
              return false;
            if (const auto *E = dyn_cast<Expr>(Part); E && IsParameter(E))
              return true;
            return llvm::any_of(childrenOnce(Part), Gives);
          };
          if (Gives(Local->getInit()))
            return true;
        }
    // Also a function it is given, by reference or a pointer, `_fWork()` or
    // `(*_pWork)()`.
    if (const auto *Direct = dyn_cast<CallExpr>(Inner);
        Direct && !isa<CXXOperatorCallExpr, CXXMemberCallExpr>(Direct)) {
      const Expr *Callee = Direct->getCallee()->IgnoreParenImpCasts();
      if (const auto *Deref = dyn_cast<UnaryOperator>(Callee);
          Deref && Deref->getOpcode() == UO_Deref)
        Callee = Deref->getSubExpr();
      if (IsParameter(Callee))
        return true;
    }
    if (const auto *Given = dyn_cast<CallExpr>(Inner)) {
      const FunctionDecl *Callee = CalledFunction(Given);
      const FunctionDecl *Next = Callee ? Callee->getDefinition() : nullptr;
      const unsigned Offset =
          isa<CXXOperatorCallExpr>(Given) && hasImplicitObject(Next) ? 1 : 0;
      for (unsigned iArgument = Offset;
           Next && iArgument < Given->getNumArgs() &&
           iArgument - Offset < Next->getNumParams();
           ++iArgument)
        if (IsParameter(Given->getArg(iArgument)) &&
            callsParameter(Next, Next->getParamDecl(iArgument - Offset),
                           Visited, CalledFunction, DestructorCalls))
          return true;
    }
    return llvm::any_of(childrenOnce(Inner), Calls);
  };
  return Calls(Definition->getBody());
}

bool callsArgument(const CallExpr *Call, unsigned Index) {
  const FunctionDecl *Callee = Call->getDirectCallee();
  const FunctionDecl *Definition = Callee ? Callee->getDefinition() : nullptr;
  const unsigned Offset =
      isa<CXXOperatorCallExpr>(Call) && hasImplicitObject(Definition) ? 1 : 0;
  if (!Definition || Index < Offset || Index >= Call->getNumArgs() ||
      Index - Offset >= Definition->getNumParams())
    return false;
  llvm::SmallPtrSet<const ParmVarDecl *, 8> Visited;
  return callsParameter(
      Definition, Definition->getParamDecl(Index - Offset), Visited,
      [](const CallExpr *Given) { return Given->getDirectCallee(); },
      [](const CXXRecordDecl *) { return false; });
}

llvm::SmallVector<const Stmt *, 4> keptChildren(const Stmt *S,
                                                const ASTContext &Context) {
  llvm::SmallVector<const Stmt *, 4> Children;
  if (const auto *If = dyn_cast<IfStmt>(S);
      If && If->isConstexpr() && !If->getCond()->isValueDependent())
    if (const std::optional<const Stmt *> Case =
            If->getNondiscardedCase(Context)) {
      for (const Stmt *Child :
           {If->getInit(),
            static_cast<const Stmt *>(If->getConditionVariableDeclStmt()),
            static_cast<const Stmt *>(If->getCond()), *Case})
        if (Child)
          Children.push_back(Child);
      return Children;
    }
  for (const Stmt *Child : childrenOnce(S))
    Children.push_back(Child);
  return Children;
}

/// Whether the local pointer \p Pointer is changed in \p Function: assigned,
/// stepped, or given by address or by a reference that isn't const.
static bool isChangedPointer(const VarDecl *Pointer,
                             const FunctionDecl *Function) {
  bool Changed = false;
  std::function<void(const Stmt *, const Stmt *)> Visit =
      [&](const Stmt *S, const Stmt *Parent) {
        if (!S || Changed)
          return;
        if (const auto *Ref = dyn_cast<DeclRefExpr>(S);
            Ref && Ref->getDecl() == Pointer) {
          // Read, `*ppValue`, or its value copied, keeps it.
          const auto *Read = dyn_cast_or_null<ImplicitCastExpr>(Parent);
          Changed = !(Read && Read->getCastKind() == CK_LValueToRValue);
          return;
        }
        for (const Stmt *Child : childrenOnce(S))
          Visit(Child, S);
      };
  Visit(Function->getBody(), nullptr);
  return Changed;
}

/// Whether \p Type is a value that refers to nothing: a number, an enum, or
/// a trivially copyable class or array of them, `struct CState { int m_Value;
/// }`, whose copy shares nothing with what it is copied from.
bool holdsOnlyValues(QualType Type, const ASTContext &Context, int Depth) {
  Type = Type.getCanonicalType().getUnqualifiedType();
  if (Depth > 256 || Type->isDependentType())
    return false;
  if (Type->isArithmeticType() || Type->isEnumeralType())
    return true;
  // Each type once, also one that is the type of many members, at each
  // level, `struct CValue { CPart m_A, m_B; }`, which would take exponential
  // time.
  struct CValues {
    llvm::DenseMap<const clang::Type *, bool> Decided;
  };
  auto &Decided = astCache<CValues>(Context).Decided;
  if (auto It = Decided.find(Type.getTypePtr()); It != Decided.end())
    return It->second;
  bool Values = false;
  if (const ArrayType *Array = Context.getAsArrayType(Type))
    Values = holdsOnlyValues(Array->getElementType(), Context, Depth + 1);
  else if (const auto *Record = Type->getAsCXXRecordDecl();
           Record && Record->hasDefinition() &&
           Type.isTriviallyCopyableType(Context))
    Values = llvm::all_of(Record->bases(),
                          [&](const CXXBaseSpecifier &Base) {
                            return holdsOnlyValues(Base.getType(), Context,
                                                   Depth + 1);
                          }) &&
             llvm::all_of(Record->fields(), [&](const FieldDecl *Field) {
               return holdsOnlyValues(Field->getType(), Context, Depth + 1);
             });
  Decided[Type.getTypePtr()] = Values;
  return Values;
}

/// The function whose body \p Call runs, and the index of the argument of
/// its first parameter: 1 of a member operator, whose object is first,
/// `Reset{}(pThis)`, else 0. Null when its body can't be seen.
std::pair<const FunctionDecl *, unsigned>
calledFunctionOf(const CallExpr *Call) {
  const FunctionDecl *Callee = Call->getDirectCallee();
  const FunctionDecl *Definition = Callee ? Callee->getDefinition() : nullptr;
  const unsigned Offset =
      isa<CXXOperatorCallExpr>(Call) && hasImplicitObject(Callee) ? 1 : 0;
  return {Definition, Offset};
}

/// Whether \p Cast, of what is const, gives what can change it: a reference
/// or a glvalue of a type that isn't const, `const_cast<CState &>(_State)`
/// or `(CState &)_State`, not a copy or a discard, `(void)_State`.
static bool castsToChangeable(const ExplicitCastExpr *Cast) {
  if (!isa<CXXConstCastExpr, CStyleCastExpr>(Cast))
    return false;
  const QualType Type = Cast->getTypeAsWritten();
  if (Type->isReferenceType())
    return !Type.getNonReferenceType().isConstQualified();
  return Cast->isGLValue() && !Cast->getType().isConstQualified();
}

/// Whether \p Function, whose body can be seen, casts the const of its
/// reference parameter \p iParameter away, `const_cast<CState &>(_State)`,
/// also through local references to it, `auto const &Alias = _State`, and
/// through the functions it gives it to, each function and parameter once,
/// one that can't be looked into not.
bool castsConstAway(const FunctionDecl *Function, unsigned iParameter) {
  if (!Function || !Function->getBody() ||
      iParameter >= Function->getNumParams())
    return false;
  struct CCasts {
    llvm::DenseMap<std::pair<const FunctionDecl *, unsigned>, bool> Results;
  };
  auto &Results = astCache<CCasts>(Function->getASTContext()).Results;
  const std::pair<const FunctionDecl *, unsigned> Key(Function, iParameter);
  if (auto It = Results.find(Key); It != Results.end())
    return It->second;
  // One that reaches itself casts what its other uses do.
  Results[Key] = false;
  llvm::SmallPtrSet<const ValueDecl *, 4> Names{
      Function->getParamDecl(iParameter)};
  // The local references bound to it, also to those, found in order.
  std::function<void(const Stmt *)> FindAliases = [&](const Stmt *S) {
    if (!S || isa<LambdaExpr>(S))
      return;
    if (const auto *Declaration = dyn_cast<DeclStmt>(S))
      for (const Decl *D : Declaration->decls())
        if (const auto *Alias = dyn_cast<VarDecl>(D);
            Alias && Alias->getType()->isReferenceType() && Alias->getInit())
          if (const auto *Bound = dyn_cast<DeclRefExpr>(
                  Alias->getInit()->IgnoreParenImpCasts());
              Bound && Names.contains(Bound->getDecl()))
            Names.insert(Alias);
    for (const Stmt *Child : childrenOnce(S))
      FindAliases(Child);
  };
  FindAliases(Function->getBody());
  auto IsNamed = [&](const Expr *E) {
    const auto *Ref = dyn_cast<DeclRefExpr>(E->IgnoreParenImpCasts());
    return Ref && Names.contains(Ref->getDecl());
  };
  bool Casts = false;
  std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
    if (!S || Casts)
      return;
    if (const auto *Cast = dyn_cast<ExplicitCastExpr>(S);
        Cast && castsToChangeable(Cast) && IsNamed(Cast->getSubExpr()))
      Casts = true;
    if (const auto *Call = dyn_cast<CallExpr>(S)) {
      const auto [Called, Offset] = calledFunctionOf(Call);
      for (unsigned iArgument = Offset; iArgument < Call->getNumArgs();
           ++iArgument)
        if (IsNamed(Call->getArg(iArgument)) &&
            (callsOverride(Call) || castsConstAway(Called, iArgument - Offset)))
          Casts = true;
    }
    for (const Stmt *Child : childrenOnce(S))
      Visit(Child);
  };
  Visit(Function->getBody());
  astCache<CCasts>(Function->getASTContext()).Results[Key] = Casts;
  return Casts;
}

llvm::SmallVector<const Expr *, 2> assignedValuesAt(const VarDecl *Local,
                                                    const DeclRefExpr *Ref) {
  const auto *Function =
      dyn_cast_or_null<FunctionDecl>(Local->getParentFunctionOrMethod());
  if (!Function || !Function->getBody())
    return {};
  const SourceManager &SM = Function->getASTContext().getSourceManager();
  auto Before = [&](SourceLocation Loc) {
    return SM.isBeforeInTranslationUnit(Loc, Ref->getBeginLoc());
  };
  // The local, also through a local reference bound to it, `int *&pAlias =
  // pValue`, or what an unchanged local pointer to it points to, `*ppValue`
  // with `int **ppValue = &pValue`, each through others like it.
  auto IsLocal = [&](const Expr *Target) {
    for (int Depth = 0; Target && Depth < 8; ++Depth) {
      Target = Target->IgnoreParenImpCasts();
      if (const auto *Deref = dyn_cast<UnaryOperator>(Target);
          Deref && Deref->getOpcode() == UO_Deref) {
        const auto *Ref =
            dyn_cast<DeclRefExpr>(Deref->getSubExpr()->IgnoreParenImpCasts());
        const auto *Pointer = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
        const auto *Address =
            Pointer && Pointer->hasLocalStorage() &&
                    !isa<ParmVarDecl>(Pointer) &&
                    Pointer->getType()->isPointerType() && Pointer->getInit()
                ? dyn_cast<UnaryOperator>(
                      Pointer->getInit()->IgnoreParenImpCasts())
                : nullptr;
        if (!Address || Address->getOpcode() != UO_AddrOf ||
            isChangedPointer(Pointer, Function))
          return false;
        Target = Address->getSubExpr();
        continue;
      }
      const auto *Ref = dyn_cast<DeclRefExpr>(Target);
      if (!Ref)
        return false;
      if (Ref->getDecl() == Local)
        return true;
      const auto *Alias = dyn_cast<VarDecl>(Ref->getDecl());
      if (!Alias || !Alias->hasLocalStorage() || isa<ParmVarDecl>(Alias) ||
          !Alias->getType()->isReferenceType() || !Alias->getInit())
        return false;
      Target = Alias->getInit();
    }
    return false;
  };
  // An assignment to the local, and what it assigns.
  auto Assigned = [&](const Stmt *S) -> const Expr * {
    const Expr *Target = nullptr;
    const Expr *Value = nullptr;
    if (const auto *Assign = dyn_cast<BinaryOperator>(S);
        Assign && Assign->getOpcode() == BO_Assign) {
      Target = Assign->getLHS();
      Value = Assign->getRHS();
    } else if (const auto *Call = dyn_cast<CXXOperatorCallExpr>(S);
               Call && Call->getOperator() == OO_Equal &&
               Call->getNumArgs() == 2) {
      Target = Call->getArg(0);
      Value = Call->getArg(1);
    }
    return Target && IsLocal(Target) ? Value : nullptr;
  };
  // An `if` and its `else` that each end by assigning it, also nested, `if
  // (bChoose) pValue = &First; else pValue = &Second;`, and what they assign.
  std::function<bool(const Stmt *, llvm::SmallVectorImpl<const Expr *> &, int)>
      BranchesAssign = [&](const Stmt *S,
                           llvm::SmallVectorImpl<const Expr *> &Values,
                           int Depth) {
        if (!S || Depth > 16)
          return false;
        if (const auto *E = dyn_cast<Expr>(S))
          S = E->IgnoreImplicit()->IgnoreParens();
        if (const Expr *Value = Assigned(S)) {
          Values.push_back(Value);
          return true;
        }
        if (const auto *Block = dyn_cast<CompoundStmt>(S))
          return !Block->body_empty() &&
                 BranchesAssign(Block->body_back(), Values, Depth + 1);
        const auto *If = dyn_cast<IfStmt>(S);
        return If && If->getElse() && !If->getInit() &&
               !If->getConditionVariable() && !If->isConsteval() &&
               BranchesAssign(If->getThen(), Values, Depth + 1) &&
               BranchesAssign(If->getElse(), Values, Depth + 1);
      };
  // What can change it in a way that isn't read here: `++pValue`, `pValue +=
  // 1`, or giving it to what can change it, `fg_Reset(pValue)` or
  // `fg_Reset(&pValue)`.
  auto Changes = [&](const Stmt *S) {
    if (const auto *Unary = dyn_cast<UnaryOperator>(S))
      return Unary->isIncrementDecrementOp() && IsLocal(Unary->getSubExpr());
    if (const auto *Compound = dyn_cast<CompoundAssignOperator>(S))
      return IsLocal(Compound->getLHS());
    const auto *Call = dyn_cast<CallExpr>(S);
    if (!Call)
      return false;
    const FunctionDecl *Callee = Call->getDirectCallee();
    const unsigned Offset =
        isa<CXXOperatorCallExpr>(Call) && hasImplicitObject(Callee) ? 1 : 0;
    for (unsigned iArgument = 0; iArgument < Call->getNumArgs(); ++iArgument) {
      const Expr *Argument = Call->getArg(iArgument)->IgnoreParenImpCasts();
      const auto *Address = dyn_cast<UnaryOperator>(Argument);
      const bool ByAddress = Address && Address->getOpcode() == UO_AddrOf &&
                             IsLocal(Address->getSubExpr());
      if (!ByAddress && !IsLocal(Argument))
        continue;
      // Not one of an operator that is the object, `pValue->f_Read()`.
      if (Offset && iArgument == 0)
        continue;
      // Of one whose parameters can't be told, given by reference, not a
      // copy.
      if (!Callee || iArgument - Offset >= Callee->getNumParams()) {
        if (ByAddress || Call->getArg(iArgument)->isGLValue())
          return true;
        continue;
      }
      const QualType Type = Callee->getParamDecl(iArgument - Offset)->getType();
      if (ByAddress ? Type->isPointerType() &&
                          !Type->getPointeeType().isConstQualified()
                    : Type->isReferenceType() &&
                          !Type.getNonReferenceType().isConstQualified())
        return true;
    }
    return false;
  };
  const Stmt *Last = nullptr;
  llvm::SmallVector<const Expr *, 2> LastValues;
  bool LastUnconditional = false;
  // The last change before the use that isn't an assignment.
  const Stmt *LastChange = nullptr;
  // The loops around the use that assign it after the use, `pValue = &B` of
  // `while (...) { *pValue = 0; pValue = &B; }`, which the next time around
  // reads.
  llvm::SmallVector<const Stmt *, 2> AssignedAfterInLoops;
  // The innermost loop around both the use and what is visited.
  const Stmt *Loop = nullptr;
  std::function<void(const Stmt *, bool)> Visit = [&](const Stmt *S,
                                                      bool Unconditional) {
    if (!S || isa<LambdaExpr>(S))
      return;
    if (const Expr *Value = Assigned(S)) {
      if (Before(S->getEndLoc()) &&
          (!Last || SM.isBeforeInTranslationUnit(Last->getBeginLoc(),
                                                 S->getBeginLoc()))) {
        Last = S;
        LastValues = {Value};
        LastUnconditional = Unconditional;
      } else if (Loop && !Before(S->getEndLoc()))
        AssignedAfterInLoops.push_back(Loop);
    } else if (Changes(S)) {
      if (Before(S->getEndLoc()) &&
          (!LastChange || SM.isBeforeInTranslationUnit(
                              LastChange->getBeginLoc(), S->getBeginLoc())))
        LastChange = S;
      else if (Loop && !Before(S->getEndLoc()))
        AssignedAfterInLoops.push_back(Loop);
    }
    // Both branches of one before the use, which runs on every path to it,
    // what they assign, and not each one alone.
    if (llvm::SmallVector<const Expr *, 2> Values;
        Unconditional && isa<IfStmt>(S) && Before(S->getEndLoc()) &&
        BranchesAssign(S, Values, 0)) {
      Last = S;
      LastValues = std::move(Values);
      LastUnconditional = true;
      // Nor a change in them, which comes before what they end assigning.
      return;
    }
    const Stmt *Outer = Loop;
    if (isa<ForStmt, WhileStmt, DoStmt, CXXForRangeStmt>(S) &&
        Before(S->getBeginLoc()) &&
        SM.isBeforeInTranslationUnit(Ref->getEndLoc(), S->getEndLoc()))
      Loop = S;
    const llvm::scope_exit Restore([&] { Loop = Outer; });
    // Only the statements of a block that holds the use run on every path to
    // it, as far as this tells, also of one that only runs in a branch, like
    // `if (bChoose) { pValue = &Other; *pValue = 0; }`, but not when a label
    // in it can be jumped to, like a `case`, also in a block in it, `{ Skip:
    // ... }`, but not a `case` of a `switch` in it.
    std::function<bool(const Stmt *, bool)> HasLabel = [&](const Stmt *Inner,
                                                           bool InSwitch) {
      if (!Inner || isa<LambdaExpr>(Inner))
        return false;
      if (isa<LabelStmt>(Inner) || (isa<SwitchCase>(Inner) && !InSwitch))
        return true;
      const bool Switch = InSwitch || isa<SwitchStmt>(Inner);
      return llvm::any_of(childrenOnce(Inner), [&](const Stmt *Child) {
        return HasLabel(Child, Switch);
      });
    };
    if (const auto *Block = dyn_cast<CompoundStmt>(S)) {
      const bool Holds =
          Before(Block->getBeginLoc()) &&
          SM.isBeforeInTranslationUnit(Ref->getEndLoc(), Block->getEndLoc()) &&
          llvm::none_of(Block->body(), [&](const Stmt *Statement) {
            return HasLabel(Statement, false);
          });
      for (const Stmt *Statement : Block->body())
        Visit(Statement,
              Holds &&
                  isa<BinaryOperator, CXXOperatorCallExpr, ExprWithCleanups,
                      CompoundStmt, IfStmt>(Statement) &&
                  Before(Statement->getEndLoc()));
      return;
    }
    // Not a branch of an `if constexpr` that is discarded, which doesn't
    // run, and the one that isn't runs each time it does.
    if (const auto *If = dyn_cast<IfStmt>(S); If && If->isConstexpr())
      if (const std::optional<const Stmt *> Taken =
              If->getNondiscardedCase(Function->getASTContext())) {
        Visit(If->getInit(), false);
        Visit(If->getConditionVariableDeclStmt(), false);
        Visit(If->getCond(), false);
        Visit(*Taken, Unconditional);
        return;
      }
    for (const Stmt *Child : childrenOnce(S))
      Visit(Child, Unconditional && isa<ExprWithCleanups>(S));
  };
  // Of a coroutine, its body.
  const Stmt *Body = Function->getBody();
  if (const auto *Coroutine = dyn_cast<CoroutineBodyStmt>(Body))
    Body = Coroutine->getBody();
  Visit(Body, true);
  // What the loop assigns after the use can be what it reads, unless it is
  // assigned before the use in the loop, each time around.
  if (llvm::any_of(AssignedAfterInLoops, [&](const Stmt *AssignedLoop) {
        return !Last || SM.isBeforeInTranslationUnit(Last->getBeginLoc(),
                                                     AssignedLoop->getBeginLoc());
      }))
    return {};
  // Changed after what it was last assigned, or after it is made.
  if (LastChange &&
      (!Last || SM.isBeforeInTranslationUnit(Last->getBeginLoc(),
                                             LastChange->getBeginLoc())))
    return {};
  if (!Last)
    return Local->getInit()
               ? llvm::SmallVector<const Expr *, 2>{Local->getInit()}
               : llvm::SmallVector<const Expr *, 2>{};
  return LastUnconditional ? LastValues : llvm::SmallVector<const Expr *, 2>{};
}

const Expr *assignedValueAt(const VarDecl *Local, const DeclRefExpr *Ref) {
  const llvm::SmallVector<const Expr *, 2> Values =
      assignedValuesAt(Local, Ref);
  return Values.size() == 1 ? Values[0] : nullptr;
}

bool CRepeatedMatches::repeated(
    const ast_matchers::MatchFinder::MatchResult &Result) {
  // Not one with a node that can't be told apart from another of its kind,
  // like a TypeLoc.
  // Also by what they are bound as, which tells the matcher.
  std::vector<std::pair<unsigned, const void *>> Key;
  for (const auto &[Name, Node] : Result.Nodes.getMap()) {
    const void *Identity = Node.getMemoizationData();
    if (!Identity)
      return false;
    Key.emplace_back(Names.try_emplace(Name, Names.size()).first->second,
                     Identity);
  }
  if (Key.size() == 1)
    return !SeenOne.insert(Key.front()).second;
  return !SeenMany.insert(std::move(Key)).second;
}

std::pair<const Expr *, const Expr *> swappedArguments(const CallExpr *Call) {
  // Of the object that customizes it, `std::ranges::swap`, whose call
  // operator is given them after the object.
  if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(Call)) {
    if (Operator->getOperator() != OO_Call || Operator->getNumArgs() != 3)
      return {};
    const auto *Ref =
        dyn_cast<DeclRefExpr>(Operator->getArg(0)->IgnoreParenImpCasts());
    const auto *Object = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
    if (!Object || !Object->getIdentifier() || Object->getName() != "swap")
      return {};
    return {Operator->getArg(1), Operator->getArg(2)};
  }
  if (isa<CXXMemberCallExpr>(Call) || Call->getNumArgs() != 2)
    return {};
  const auto *Callee = dyn_cast_or_null<NamedDecl>(Call->getCalleeDecl());
  if (!Callee || !Callee->getIdentifier() ||
      (!isSwapFunction(Callee->getName())))
    return {};
  return {Call->getArg(0), Call->getArg(1)};
}

} // namespace clang::tidy::malterlib
