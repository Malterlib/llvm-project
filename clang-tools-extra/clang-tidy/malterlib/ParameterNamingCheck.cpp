//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ParameterNamingCheck.h"
#include "NamingUtils.h"
#include "OncePerTypeVisitor.h"
#include "clang/AST/ASTLambda.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include <functional>

using namespace clang::ast_matchers;

namespace clang::tidy::malterlib {

void ParameterNamingCheck::registerMatchers(MatchFinder *Finder) {
  Finder->addMatcher(namedDecl().bind("declaration"), this);
  Finder->addMatcher(lambdaExpr().bind("lambda"), this);
  // What the templates that are used are instantiated with, which tells what
  // the parameters of a dependent type are.
  Finder->addMatcher(
      traverse(TK_AsIs,
               functionDecl(ast_matchers::isTemplateInstantiation())
                   .bind("instance")),
      this);
}

/// Whether a parameter of \p Type can be written through, or, for a type
/// that depends on the parameters of a template, which can be a reference,
/// unknown until it is instantiated.
static std::optional<bool> canBeOutputType(QualType Type);

void ParameterNamingCheck::check(const MatchFinder::MatchResult &Result) {
  if (const auto *Instance =
          Result.Nodes.getNodeAs<FunctionDecl>("instance")) {
    const FunctionDecl *Pattern = Instance->getTemplateInstantiationPattern();
    if (!Pattern)
      return;
    // The written one: of a member template of an instantiation, like the
    // call operator of a generic lambda in an instantiated function, what it
    // is instantiated from.
    for (int Depth = 0; Depth < 16; ++Depth) {
      const FunctionTemplateDecl *Template =
          Pattern->getDescribedFunctionTemplate();
      const FunctionTemplateDecl *From =
          Template ? Template->getInstantiatedFromMemberTemplate() : nullptr;
      const FunctionDecl *Next =
          From ? From->getTemplatedDecl()
               : Pattern->getInstantiatedFromMemberFunction();
      if (!Next || Next == Pattern)
        break;
      Pattern = Next;
    }
    const unsigned nPacks =
        llvm::count_if(Pattern->parameters(), [](const ParmVarDecl *Parameter) {
          return Parameter->isParameterPack();
        });
    // As many as a pack expands to: the rest, when it is the only one, or as
    // many as the pack of the function template it expands has, `tfp_CFirst
    // ...po_First`, when it can be told.
    auto ExpandedCount = [&](const ParmVarDecl *Parameter)
        -> std::optional<unsigned> {
      if (!Parameter->isParameterPack())
        return 1;
      if (nPacks == 1)
        return Instance->getNumParams() + 1 - Pattern->getNumParams();
      // The pack it expands, also inside another type, `TCBox<tfp_CFirst>
      // ...p_Boxes`, also a pack of values, `decltype(tfp_Indices)
      // ...p_Indices`.
      const auto *Expansion =
          Parameter->getType()->getAs<PackExpansionType>();
      struct CPackFinder : OncePerTypeVisitor<CPackFinder> {
        std::optional<std::pair<unsigned, unsigned>> Found;
        bool VisitTemplateTypeParmType(TemplateTypeParmType *Type) {
          if (!Type->isParameterPack())
            return true;
          Found = {Type->getDepth(), Type->getIndex()};
          return false;
        }
        bool VisitDeclRefExpr(DeclRefExpr *Ref) {
          const auto *Value =
              dyn_cast<NonTypeTemplateParmDecl>(Ref->getDecl());
          if (!Value || !Value->isParameterPack())
            return true;
          Found = {Value->getDepth(), Value->getIndex()};
          return false;
        }
        // Also a pack of templates, `tfp_TCBoxes<int> ...p_Boxes`.
        bool VisitTemplateSpecializationType(TemplateSpecializationType *Type) {
          const auto *Template = dyn_cast_or_null<TemplateTemplateParmDecl>(
              Type->getTemplateName().getAsTemplateDecl());
          if (!Template || !Template->isParameterPack())
            return true;
          Found = {Template->getDepth(), Template->getIndex()};
          return false;
        }
      } Finder;
      if (Expansion)
        Finder.TraverseType(Expansion->getPattern());
      if (!Finder.Found)
        return std::nullopt;
      // The arguments of the template whose parameters are at the depth of
      // the pack: the function template, or a class template it is a member
      // of, `template <typename ...tp_CTypes> struct TCBox`.
      const TemplateArgumentList *Arguments = nullptr;
      if (const FunctionTemplateDecl *Template =
              Pattern->getDescribedFunctionTemplate();
          Template && Finder.Found->first ==
                          Template->getTemplateParameters()->getDepth())
        Arguments = Instance->getTemplateSpecializationArgs();
      for (const DeclContext *Context = Instance->getDeclContext();
           Context && !Arguments; Context = Context->getParent()) {
        const auto *Class = dyn_cast<ClassTemplateSpecializationDecl>(Context);
        if (!Class)
          continue;
        // Of the template it is in the pattern, not the member template of an
        // instantiation of the class around it, `TCOuter<int>::TCInner`,
        // whose parameters are one level less deep.
        const auto Specialized = Class->getSpecializedTemplateOrPartial();
        const TemplateParameterList *Parameters = nullptr;
        if (auto *Partial =
                Specialized
                    .dyn_cast<ClassTemplatePartialSpecializationDecl *>()) {
          while (ClassTemplatePartialSpecializationDecl *From =
                     Partial->getInstantiatedFromMember())
            Partial = From;
          Parameters = Partial->getTemplateParameters();
        } else {
          auto *Template = cast<ClassTemplateDecl *>(Specialized);
          while (ClassTemplateDecl *From =
                     Template->getInstantiatedFromMemberTemplate())
            Template = From;
          Parameters = Template->getTemplateParameters();
        }
        if (Parameters->getDepth() == Finder.Found->first)
          Arguments = &Class->getTemplateInstantiationArgs();
      }
      if (!Arguments ||
          Finder.Found->second >= Arguments->size() ||
          Arguments->get(Finder.Found->second).getKind() !=
              TemplateArgument::Pack)
        return std::nullopt;
      return Arguments->get(Finder.Found->second).pack_size();
    };
    // Each parameter of the pattern, a pack as as many as it expands to; past
    // one that can't be told, they can't be told either, and can be
    // outputs.
    unsigned iInstance = 0;
    bool Known = true;
    for (const ParmVarDecl *Parameter : Pattern->parameters()) {
      const std::optional<unsigned> Expanded =
          Known ? ExpandedCount(Parameter) : std::nullopt;
      Known = Expanded.has_value();
      const unsigned Count = Expanded.value_or(0);
      // A pack that expands to nothing tells nothing of what it can be.
      if (Known && Count == 0)
        continue;
      bool &Writable =
          Instantiated[{keyOf(Pattern), Parameter->getFunctionScopeIndex()}];
      if (!Known)
        Writable = true;
      for (unsigned iExpanded = 0;
           iExpanded < Count && iInstance < Instance->getNumParams();
           ++iExpanded, ++iInstance)
        Writable |= canBeOutputType(
                        Instance->getParamDecl(iInstance)->getType())
                        .value_or(true);
    }
    return;
  }
  if (Repeated.repeated(Result))
    return;
  const SourceManager &SM = *Result.SourceManager;
  if (const auto *Declaration = Result.Nodes.getNodeAs<NamedDecl>("declaration"))
    checkDeclaration(Declaration, SM);
  if (const auto *Lambda = Result.Nodes.getNodeAs<LambdaExpr>("lambda")) {
    if (const TemplateParameterList *Parameters =
            Lambda->getTemplateParameterList())
      checkTemplateParameters(Parameters, true, SM);
  }
}

void ParameterNamingCheck::checkDeclaration(const NamedDecl *Declaration,
                                            const SourceManager &SM) {
  if (const auto *Parameter = dyn_cast<ParmVarDecl>(Declaration)) {
    checkFunctionParameter(Parameter, SM);
    return;
  }
  if (Declaration->isImplicit())
    return;

  if (const auto *Template = dyn_cast<TemplateDecl>(Declaration)) {
    // Template template parameters are checked with the list they are in.
    // A deduction guide is named for its class template, like
    // `template <typename t_CType> TCFuture(t_CType) -> TCFuture<t_CType>`.
    if (!isa<TemplateTemplateParmDecl>(Template) &&
        !isa<BuiltinTemplateDecl>(Template))
      checkTemplateParameters(
          Template->getTemplateParameters(),
          isa<FunctionTemplateDecl>(Template) &&
              !isa<CXXDeductionGuideDecl>(Template->getTemplatedDecl()),
          SM);
    return;
  }
  if (const auto *Class =
          dyn_cast<ClassTemplatePartialSpecializationDecl>(Declaration))
    checkTemplateParameters(Class->getTemplateParameters(), false, SM);
  else if (const auto *Var =
               dyn_cast<VarTemplatePartialSpecializationDecl>(Declaration))
    checkTemplateParameters(Var->getTemplateParameters(), false, SM);

  // Out of line definitions of the members of class templates repeat the
  // template parameters of their classes.
  ArrayRef<TemplateParameterList *> Outer;
  if (const auto *Declarator = dyn_cast<DeclaratorDecl>(Declaration))
    Outer = Declarator->getTemplateParameterLists();
  else if (const auto *Tag = dyn_cast<TagDecl>(Declaration))
    Outer = Tag->getTemplateParameterLists();
  for (const TemplateParameterList *Parameters : Outer)
    checkTemplateParameters(Parameters, false, SM);
}

/// Whether what a reference or a pointer refers to, \p Pointee, can be written
/// through: what isn't const, also an array of it, an object of a class,
/// through its const member functions, like a promise given its result through
/// `TCPromise<tf_CType> const &o_Promise`, and what a pointer reaches through
/// const pointers, `int *const *o_pValue`, but not a function nor a const
/// scalar.
static bool canWriteThrough(QualType Pointee) {
  if (Pointee->isFunctionType())
    return false;
  // Also of an alias of an array made const, `CData const &` of `using
  // CData = int[4]`, whose elements are.
  bool Const = Pointee.isConstQualified();
  while (const ArrayType *Array = Pointee->getAsArrayTypeUnsafe()) {
    Pointee = Array->getElementType();
    Const |= Pointee.isConstQualified();
  }
  if (Const)
    Pointee.addConst();
  if (!Pointee.isConstQualified())
    return true;
  // Nor a const vector of scalars, `int __attribute__((ext_vector_type(4)))`,
  // nor const `void`, nor a const atomic, `_Atomic(int) const`, other than
  // of a pointer to what can be written, `_Atomic(int *) const`.
  if (const auto *Atomic = Pointee->getAs<AtomicType>())
    return Atomic->getValueType()->isPointerType() &&
           canWriteThrough(Atomic->getValueType()->getPointeeType());
  if (Pointee->isVectorType() || Pointee->isVoidType())
    return false;
  if (!Pointee->isScalarType())
    return true;
  // Also through a const Objective-C object pointer, `CObject *const &`.
  if (Pointee->isObjCObjectPointerType())
    return true;
  return Pointee->isPointerType() && canWriteThrough(Pointee->getPointeeType());
}

/// Whether a parameter of type \p Type can be an output parameter, named
/// with the `o_` prefix: whether the function can write through it. Whether
/// it is one is what its author means by it, also of one that the function
/// writes, so it can be named either way; only what can't be written, a
/// value or what is const, can't be an output.
/// Whether an object of \p Record taken by value can be written through: one
/// of the const member functions of it or of a base, whatever its access,
/// gives what can be written through, as a parameter of its type can be. As
/// `o_` is
/// only rejected for what definitely can't be written, none when that can't
/// be told from what clang instantiated: a specialization a declaration names
/// without needing it complete, `TCReference<int const> o_Value`, a result
/// that depends on what isn't known, or a member function template, which can
/// be named either way. Each base once, at the least depth it is reached at,
/// kept in \p Explored, as one that several paths lead to, of a diamond,
/// would otherwise be gone through for each.
static std::optional<bool>
refersToWritable(const CXXRecordDecl *Record, int Depth = 0,
                 llvm::DenseMap<const CXXRecordDecl *, int> *Explored = nullptr) {
  if (!Record || Depth > 8)
    return std::nullopt;
  llvm::DenseMap<const CXXRecordDecl *, int> Own;
  if (!Explored)
    Explored = &Own;
  if (auto [It, Inserted] = Explored->try_emplace(Record, Depth); !Inserted) {
    if (It->second <= Depth)
      return false;
    It->second = Depth;
  }
  Record = Record->getDefinition();
  if (!Record)
    return std::nullopt;
  bool Unknown = false;
  for (const Decl *Member : Record->decls()) {
    if (const auto *Template = dyn_cast<FunctionTemplateDecl>(Member)) {
      // Also one whose explicit object depends on its parameters, `int
      // &f_Get(this tf_CSelf &&_Self)`, which can be const.
      if (const auto *Method =
              dyn_cast<CXXMethodDecl>(Template->getTemplatedDecl());
          Method && !Method->isStatic() &&
          (hasConstObject(Method) ||
           (Method->isExplicitObjectMemberFunction() &&
            Method->getParamDecl(0)->getType()->isDependentType())))
        Unknown = true;
      continue;
    }
    // Const, also through an explicit object, `int &f_Get(this CWrapper const
    // &_Self)`.
    const auto *Method = dyn_cast<CXXMethodDecl>(Member);
    if (!Method || Method->isStatic() || !hasConstObject(Method) ||
        isa<CXXConstructorDecl, CXXDestructorDecl>(Method))
      continue;
    // A result that isn't known, also one deduced only when the function is
    // used, `decltype(auto) f_Get() const`.
    const QualType Result = Method->getReturnType();
    if (Result->isDependentType() || Result->isUndeducedType()) {
      Unknown = true;
      continue;
    }
    // What it gives is written through as a parameter of its type is: an
    // Objective-C object pointer, what a reference, also an rvalue one, `int
    // &&f_Get() const`, or a pointer refers to, also through const pointers,
    // `int *const &f_Get() const`, and an object of a class that refers to
    // what can be written, `TCReference<int> f_Get() const`.
    if (Result->isObjCObjectPointerType())
      return true;
    if ((Result->isReferenceType() || Result->isPointerType()) &&
        canWriteThrough(Result->getPointeeType()))
      return true;
    if (const CXXRecordDecl *Given = Result->getAsCXXRecordDecl();
        Given && !Result->isReferenceType()) {
      const std::optional<bool> Writable =
          refersToWritable(Given, Depth + 1, Explored);
      if (Writable.value_or(false))
        return true;
      Unknown |= !Writable;
    }
  }
  for (const CXXBaseSpecifier &Base : Record->bases()) {
    const std::optional<bool> Writable = refersToWritable(
        Base.getType()->getAsCXXRecordDecl(), Depth + 1, Explored);
    if (Writable.value_or(false))
      return true;
    Unknown |= !Writable;
  }
  if (Unknown)
    return std::nullopt;
  return false;
}

static std::optional<bool> canBeOutputType(QualType Type) {
  if (const auto *Expansion = Type->getAs<PackExpansionType>())
    Type = Expansion->getPattern();
  // A reference or a pointer to what can be written, `int &o_Value`, `int
  // *const &&o_pValue` or `CStr *o_pError`, but not a value. One to what a
  // template is given, `tf_CType const &o_Value`, can be to a const scalar,
  // or a function, which is told by its instantiations.
  // An Objective-C object, which is always written through a pointer,
  // `CObject *o_pObject`.
  if (Type->isObjCObjectPointerType())
    return true;
  if (Type->isReferenceType() || Type->isPointerType()) {
    QualType Pointee = Type->getPointeeType();
    while (Pointee->isPointerType() && Pointee.isConstQualified())
      Pointee = Pointee->getPointeeType();
    // Also of an alias of an array made const, `CData const &` of `using
    // CData = int[4]`, whose elements are.
    bool Const = Pointee.isConstQualified();
    while (const ArrayType *Array = Pointee->getAsArrayTypeUnsafe()) {
      Pointee = Array->getElementType();
      Const |= Pointee.isConstQualified();
    }
    if (Const)
      Pointee.addConst();
    if (Pointee->isDependentType())
      return std::nullopt;
    return canWriteThrough(Type->getPointeeType());
  }
  // What a template is given can be a reference, `typename
  // tf_CTraits::CReference o_Value` or `t_CReference o_Value` of a class
  // template given `int &`.
  if (Type->isDependentType())
    return std::nullopt;
  // An object of a class that refers to what it can be written through, as a
  // reference wrapper does, `TCReference<CActorSubscription> o_Subscription`
  // with `f_Get() const` giving a non-const reference: one of its const
  // member functions, also of a base, gives a reference or a pointer to what
  // can be written.
  if (const CXXRecordDecl *Record = Type->getAsCXXRecordDecl())
    return refersToWritable(Record);
  return false;
}

/// Whether \p Parameter is one of the function an explicit instantiation
/// names, in the type it is declared with, `template void fg_Function<int>(int);`,
/// not one of a function type among its template arguments, `template struct
/// TCFunction<void (int _Value)>;`, which is named like any other.
static bool isExplicitInstantiationParameter(const ParmVarDecl *Parameter) {
  ASTContext &Context = Parameter->getASTContext();
  DynTypedNodeList Parents = Context.getParents(*Parameter);
  for (int Depth = 0; Depth < 32 && !Parents.empty(); ++Depth) {
    const DynTypedNode &Parent = Parents[0];
    if (const auto *Instantiation = Parent.get<ExplicitInstantiationDecl>()) {
      if (!isa<FunctionDecl>(Instantiation->getSpecialization()))
        return false;
      const TypeSourceInfo *Written = Instantiation->getTypeAsWritten();
      if (!Written)
        return false;
      // Past parentheses and attributes, also a calling convention, `template
      // void __cdecl fg_Function<int>(int);`.
      TypeLoc Loc = Written->getTypeLoc();
      for (int Steps = 0; Steps < 16; ++Steps) {
        Loc = Loc.IgnoreParens();
        if (const auto Attributed = Loc.getAs<AttributedTypeLoc>())
          Loc = Attributed.getModifiedLoc();
        else if (const auto Macro = Loc.getAs<MacroQualifiedTypeLoc>())
          Loc = Macro.getInnerLoc();
        else
          break;
      }
      const FunctionTypeLoc Function = Loc.getAs<FunctionTypeLoc>();
      return Function && llvm::is_contained(Function.getParams(), Parameter);
    }
    if (!Parent.get<TypeLoc>())
      return false;
    Parents = Context.getParents(Parent);
  }
  return false;
}

void ParameterNamingCheck::onEndOfTranslationUnit() {
  // Can't be an output when no instantiation of it can be written through;
  // one that nothing instantiates can be, also taken by value as a type
  // parameter of the function template itself, `tf_CType o_pValue`, which can
  // be deduced as a pointer.
  for (const CDeferred &Parameter : Deferred) {
    const auto *Function =
        dyn_cast<FunctionDecl>(Parameter.Parameter->getDeclContext());
    if (Function &&
        llvm::is_contained(Function->parameters(), Parameter.Parameter)) {
      auto It = Instantiated.find(
          {keyOf(Function), Parameter.Parameter->getFunctionScopeIndex()});
      if (It == Instantiated.end() || It->second)
        continue;
    } else {
      // Of a requires expression in a function template, `requires(typename
      // tf_CTraits::CValue &o_Value)`, or of a function type in the type of a
      // declaration.
      llvm::SmallVector<QualType, 4> Instances;
      if (isa<RequiresExprBodyDecl>(Parameter.Parameter->getDeclContext())) {
        for (const Decl *Instance : localInstances(Parameter.Parameter))
          if (const auto *Instantiated = dyn_cast<ParmVarDecl>(Instance))
            Instances.push_back(Instantiated->getType());
      } else {
        Instances = functionTypeParameterTypes(
            Parameter.Parameter, Parameter.Parameter->getASTContext(),
            /*Adjusted=*/true);
      }
      if (Instances.empty() || llvm::any_of(Instances, [](QualType Instance) {
            return canBeOutputType(Instance).value_or(true);
          }))
        continue;
    }
    const bool Pack = Parameter.Parameter->isParameterPack();
    StringRef Prefix, Rest;
    splitStoragePrefix(Parameter.Parameter->getName(), Prefix, Rest);
    diag(Parameter.Loc, "%0 %1 has the %2 prefix of an output, but can't be "
                        "written through, being a value or const; name it "
                        "with the %3 prefix")
        << (Pack ? "parameter pack" : "parameter") << Parameter.Parameter
        << ("'" + Prefix + "'").str() << (Pack ? "'p_'" : "'_'");
  }
  Deferred.clear();
  Instantiated.clear();
  LambdaKeys.clear();
}

const FunctionDecl *ParameterNamingCheck::keyOf(const FunctionDecl *Function) {
  Function = Function->getCanonicalDecl();
  if (!isLambdaCallOperator(Function))
    return Function;
  return LambdaKeys
      .try_emplace(Function->getLocation().getRawEncoding(), Function)
      .first->second;
}

void ParameterNamingCheck::checkFunctionParameter(const ParmVarDecl *Parameter,
                                                  const SourceManager &SM) {
  if (Parameter->isImplicit())
    return;
  // An explicit instantiation only names a specialization that exists,
  // `template TCValue<long>::TCValue(long &&);`, also with `extern`, so the
  // names of its parameters would tell nothing.
  if (isExplicitInstantiationParameter(Parameter))
    return;
  // One of a function type in an instantiation of a template, also of a class,
  // `void (tp_CParams ...p_Params)` in a static member, which the pattern
  // declares and is checked in: there a pack expands to parameters of the
  // pack's name. Not one of an explicit specialization, also of a member of
  // an instantiated class, `template <> void TCBox<int>::f_Run(int _Value)`,
  // which is written as it is.
  for (const DeclContext *Context = Parameter->getDeclContext(); Context;
       Context = Context->getParent()) {
    if (const auto *Function = dyn_cast<FunctionDecl>(Context)) {
      // Also a member template specialized for an instantiated class,
      // `template <> template <typename tf_CValue> void
      // TCBox<int>::f_Run(tf_CValue _Value)`.
      if (const FunctionTemplateDecl *Template =
              Function->getDescribedFunctionTemplate();
          Function->getTemplateSpecializationKind() ==
              TSK_ExplicitSpecialization ||
          (Template && Template->isMemberSpecialization()))
        break;
      if (Function->isTemplateInstantiation())
        return;
    }
    // Also a requirement in the initializer of a static data member
    // specialized for an instantiated class, `template <> bool
    // TCBox<int>::ms_bValue = requires(int _Value) { ... };`, which is
    // written as it is.
    if (const auto *Requirement = dyn_cast<RequiresExprBodyDecl>(Context)) {
      ASTContext &Context = Requirement->getASTContext();
      DynTypedNode Node = DynTypedNode::create(*Requirement);
      const VarDecl *Variable = nullptr;
      for (int Depth = 0; Depth < 64 && !Variable; ++Depth) {
        const auto Parents = Context.getParents(Node);
        if (Parents.empty() || Parents[0].get<FunctionDecl>())
          break;
        Node = Parents[0];
        if (const auto *Found = Node.get<VarDecl>();
            Found && !Found->isInitCapture())
          Variable = Found;
      }
      const VarTemplateDecl *Template =
          Variable ? Variable->getDescribedVarTemplate() : nullptr;
      if (Variable && (Variable->getTemplateSpecializationKind() ==
                           TSK_ExplicitSpecialization ||
                       (Template && Template->isMemberSpecialization())))
        break;
    }
    // Also a member enum specialized for an instantiated class.
    if (const auto *Enum = dyn_cast<EnumDecl>(Context);
        Enum &&
        Enum->getTemplateSpecializationKind() == TSK_ExplicitSpecialization)
      break;
    if (const auto *Record = dyn_cast<CXXRecordDecl>(Context)) {
      // Also a lambda in the initializer of a static data member specialized
      // for an instantiated class, `template <> int TCBox<int>::ms_Value =
      // [](int _Value) { ... }(0);`, which is written as it is.
      if (Record->isLambda()) {
        // Also in the initializer of an enumerator of a member enum
        // specialized for an instantiated class, `template <> enum
        // TCBox<int>::EMode : int { EMode_Value = [](int _Value) { ... }(0)
        // };`.
        if (const auto *Enumerator = dyn_cast_or_null<EnumConstantDecl>(
                Record->getLambdaContextDecl());
            Enumerator &&
            cast<EnumDecl>(Enumerator->getDeclContext())
                    ->getTemplateSpecializationKind() ==
                TSK_ExplicitSpecialization)
          break;
        // Past the variables of init captures, `[Value = [](int _Value) {
        // ... }(0)]`, which are the lambda's, to the variable it is made in.
        const VarDecl *Variable =
            dyn_cast_or_null<VarDecl>(Record->getLambdaContextDecl());
        if (Variable && Variable->isInitCapture())
          Variable = nullptr;
        ASTContext &Context = Record->getASTContext();
        DynTypedNode Node = DynTypedNode::create(*Record);
        for (int Depth = 0; Depth < 64 && !Variable; ++Depth) {
          const auto Parents = Context.getParents(Node);
          if (Parents.empty())
            break;
          Node = Parents[0];
          if (const auto *Found = Node.get<VarDecl>();
              Found && !Found->isInitCapture())
            Variable = Found;
          if (Node.get<FunctionDecl>())
            break;
        }
        // Also a member variable template specialized for an instantiated
        // class, `template <> template <typename t_CValue> int
        // TCBox<int>::ms_Value = ...;`.
        const VarTemplateDecl *Template =
            Variable ? Variable->getDescribedVarTemplate() : nullptr;
        if (Variable && (Variable->getTemplateSpecializationKind() ==
                             TSK_ExplicitSpecialization ||
                         (Template && Template->isMemberSpecialization())))
          break;
      }
      const TemplateSpecializationKind Kind =
          Record->getTemplateSpecializationKind();
      if (const ClassTemplateDecl *Template =
              Record->getDescribedClassTemplate();
          Kind == TSK_ExplicitSpecialization ||
          (Template && Template->isMemberSpecialization()))
        break;
      if (Kind == TSK_ImplicitInstantiation ||
          Kind == TSK_ExplicitInstantiationDeclaration ||
          Kind == TSK_ExplicitInstantiationDefinition)
        return;
    }
  }
  const auto *Function =
      dyn_cast_or_null<FunctionDecl>(Parameter->getDeclContext());
  if (Function) {
    // Also those of a function with C linkage: their names are neither its
    // symbol nor its ABI, and only the sources of checked repositories are,
    // so a system's declarations aren't.
    if (Function->isTemplateInstantiation())
      return;
  }
  const SourceLocation Loc = nameLocation(Parameter, SM);
  if (Loc.isInvalid())
    return;
  // Each parameter is named, also one that is unused, which tells what it is,
  // but not the `int` that tells a postfix `operator ++` or `--` apart.
  if (!Parameter->getIdentifier()) {
    const bool Postfix =
        Function &&
        (Function->getOverloadedOperator() == OO_PlusPlus ||
         Function->getOverloadedOperator() == OO_MinusMinus) &&
        Parameter->getFunctionScopeIndex() + 1 == Function->getNumParams() &&
        // Not counting an explicit object parameter, `this CCounter &_Self`.
        Function->getNumNonObjectParams() ==
            (isa<CXXMethodDecl>(Function) ? 1u : 2u) &&
        Parameter->getType()->isSpecificBuiltinType(BuiltinType::Int);
    if (!Postfix)
      diag(Loc, "parameter should be named, also when it is unused, so that "
                "what it is can be told");
    return;
  }

  const StringRef Name = Parameter->getName();
  const bool Pack = Parameter->isParameterPack();
  const std::optional<bool> Output = canBeOutputType(Parameter->getType());
  const bool CanBeOutput = Output.value_or(true);

  StringRef Prefix, Rest;
  const bool NamedOutput = splitStoragePrefix(Name, Prefix, Rest) &&
                           Prefix == (Pack ? "po_" : "o_");
  // One of a type a template is given is told by its instantiations, at the
  // end of the translation unit, also one of a function type in the type of a
  // declaration, `TCFunction<void (typename t_CTraits::CBool o_bReady)>
  // m_fCallback`, by those of the declaration.
  const bool Defer = NamedOutput && !Output;
  if (Defer)
    Deferred.push_back({Parameter, Loc});

  llvm::SmallVector<StringRef, 2> Expected;
  if (CanBeOutput || Defer || (NamedOutput && !Output))
    Expected.push_back(Pack ? "po_" : "o_");
  Expected.push_back(Pack ? "p_" : "_");

  const StringRef Kind = Pack ? "parameter pack" : "parameter";
  if (NamedOutput && !llvm::is_contained(Expected, Prefix)) {
    diag(Loc, "%0 %1 has the %2 prefix of an output, but can't be written "
              "through, being a value or const; name it with the %3 prefix")
        << Kind << Parameter << ("'" + Prefix + "'").str()
        << quotedList(Expected);
    return;
  }
  if (!splitStoragePrefix(Name, Prefix, Rest) ||
      !llvm::is_contained(Expected, Prefix)) {
    diag(Loc, "%0 %1 should have the %2 prefix")
        << Kind << Parameter << quotedList(Expected);
    // The rest of the name is checked too, so a name with several problems is
    // reported for all of them at once: after the storage prefix it has, or
    // all of it.
    if (!splitStoragePrefix(Name, Prefix, Rest))
      Rest = Name;
  }
  if (const StringRef Wrong = wrongConceptPrefix(Rest); !Wrong.empty())
    diag(Loc, "%0 %1 has %select{the prefix '%2', which isn't a concept prefix; remove "
              "it or use %3|the concept prefixes '%2', of which only the "
              "outermost, '%3', is written}4")
        << "parameter" << Parameter << Wrong
        << (Wrong.size() == 1 ? quotedConceptPrefixes() : Wrong.take_front(1).str())
        << (Wrong.size() == 2);
  else if (!isUpperCamelWithConcepts(Rest))
    diag(Loc, "parameter %0 should be upper camel case after its prefix")
        << Parameter;
  else if (withoutConcept(Rest).size() < MinimumNameLength)
    diag(Loc, "parameter %0 should have at least %1 characters after its "
              "prefixes")
        << Parameter << MinimumNameLength;
}

void ParameterNamingCheck::checkTemplateParameters(
    const TemplateParameterList *Parameters, bool OfFunction,
    const SourceManager &SM) {
  if (!Parameters)
    return;
  for (const NamedDecl *Parameter : *Parameters) {
    // The parameters of a template template parameter are named like those
    // of a class template.
    if (const auto *Template = dyn_cast<TemplateTemplateParmDecl>(Parameter))
      checkTemplateParameters(Template->getTemplateParameters(), false, SM);
    if (!Parameter->getIdentifier() || Parameter->isImplicit())
      continue;
    const SourceLocation Loc = nameLocation(Parameter, SM);
    if (Loc.isInvalid())
      continue;

    const bool Pack = Parameter->isParameterPack();
    std::string Prefix = OfFunction ? "tf" : "t";
    if (Pack)
      Prefix += "p";
    Prefix += "_";

    const StringRef Name = Parameter->getName();
    StringRef Kind = OfFunction ? "function template parameter"
                                : "template parameter";
    StringRef Rest;
    if (Name.starts_with(Prefix))
      Rest = Name.drop_front(Prefix.size());
    else {
      diag(Loc, "%0 %1 should have the '%2' prefix")
          << Kind << Parameter << Prefix;
      // The rest of the name is checked too, so a name with several problems
      // is reported for all of them at once, `t_Func` of a function template
      // for both `tf_` and its type prefix: after the template prefix it
      // has, or all of it.
      Rest = Name;
      for (StringRef Other : {"tfp_", "tp_", "tf_", "t_"})
        if (Name.starts_with(Other)) {
          Rest = Name.drop_front(Other.size());
          break;
        }
    }
    // The length of the name after a type prefix, the longest that it has.
    auto ShortAfter = [&](llvm::ArrayRef<StringRef> TypePrefixes) {
      size_t Longest = 0;
      for (StringRef TypePrefix : TypePrefixes)
        if (Rest.starts_with(TypePrefix))
          Longest = std::max(Longest, TypePrefix.size());
      return Rest.size() - Longest < MinimumNameLength;
    };
    auto ReportShort = [&] {
      diag(Loc, "%0 %1 should have at least %2 characters after its prefixes")
          << Kind << Parameter << MinimumNameLength;
    };
    if (isa<TemplateTypeParmDecl>(Parameter)) {
      // A type, `t_CType`, also an interface, a function type or an enum;
      // the prefixes of templates are of template template parameters.
      if (hasTypePrefix(Rest, {"TC", "TIC", "TF"}))
        diag(Loc, "type %0 %1 has the prefix of a template template "
                  "parameter; use '%2C', '%2IC', '%2F' or '%2E'")
            << Kind << Parameter << Prefix;
      else if (!hasTypePrefix(Rest, {"C", "IC", "F", "E"}))
        diag(Loc, "type %0 %1 should have a type prefix after '%2', like "
                  "'%2C' or '%2F'")
            << Kind << Parameter << Prefix;
      else if (ShortAfter({"C", "IC", "F", "E"}))
        ReportShort();
    } else if (isa<TemplateTemplateParmDecl>(Parameter)) {
      // A class template, `t_TCVector`, also an interface, or an alias
      // template of a function type, `t_TFCallback`, not a bare `T`.
      if (!hasTypePrefix(Rest, {"TC", "TIC", "TF"}))
        diag(Loc, "template %0 %1 should have a template type prefix after "
                  "'%2', like '%2TC'")
            << Kind << Parameter << Prefix;
      else if (ShortAfter({"TC", "TIC", "TF"}))
        ReportShort();
    } else if (const StringRef Wrong = wrongConceptPrefix(Rest);
               !Wrong.empty()) {
      diag(Loc, "%0 %1 has %select{the prefix '%2', which isn't a concept prefix; remove "
              "it or use %3|the concept prefixes '%2', of which only the "
              "outermost, '%3', is written}4")
          << Kind << Parameter << Wrong
          << (Wrong.size() == 1 ? quotedConceptPrefixes()
                                : Wrong.take_front(1).str())
          << (Wrong.size() == 2);
    } else if (!isUpperCamelWithConcepts(Rest)) {
      diag(Loc, "%0 %1 should be upper camel case after its prefix")
          << Kind << Parameter;
    } else if (withoutConcept(Rest).size() < MinimumNameLength) {
      diag(Loc, "%0 %1 should have at least %2 characters after its prefixes")
          << Kind << Parameter << MinimumNameLength;
    }
  }
}

} // namespace clang::tidy::malterlib
