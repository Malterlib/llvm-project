//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_NAMINGUTILS_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_NAMINGUTILS_H

#include "MalterlibOptions.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/ExprCXX.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace clang::tidy::malterlib {

/// A number of the AST of \p Context that no other AST has, also one at the
/// same address after this one is gone, which another thread than the one
/// that analyzes it can destroy, like a clangd worker that evicts the AST of
/// another from the cache. The facts a thread keeps of an AST are of the AST
/// of this number.
uint64_t astIdentity(const ASTContext &Context);

/// The \p t_CFacts this thread keeps of the AST of \p Context, the one it
/// analyzes, whose declarations can be at the same addresses in the next one.
template <typename t_CFacts> t_CFacts &astCache(const ASTContext &Context) {
  struct CCache {
    uint64_t AST = 0;
    std::unique_ptr<t_CFacts> Facts;
  };
  static thread_local CCache s_Cache;
  if (const uint64_t AST = astIdentity(Context); s_Cache.AST != AST) {
    s_Cache.AST = AST;
    s_Cache.Facts = std::make_unique<t_CFacts>();
  }
  return *s_Cache.Facts;
}

/// The concept prefixes that can follow a storage prefix: boolean, iterator
/// or index, number of, pointer, function object and range. Not `c`, which is
/// the prefix of C++ concepts, `cIterable`, rather than of containers.
inline constexpr llvm::StringLiteral ConceptPrefixes = "binpfr";

/// Splits \p Name into its storage prefix (the lower case letters in front of
/// the first underscore, and the underscore) and the rest. Returns false when
/// the name has no storage prefix.
bool splitStoragePrefix(StringRef Name, StringRef &Prefix, StringRef &Rest);

/// Whether \p Name has no storage prefix.
inline bool hasStoragePrefix(StringRef Name) {
  StringRef Prefix, Rest;
  return splitStoragePrefix(Name, Prefix, Rest);
}

/// What leads \p Rest, a name after its storage prefix, as a concept prefix
/// that isn't one: a lower case letter that isn't a concept prefix, `l` of
/// `lParses`, or more than one concept prefix, `pb` of `pbValue`, of which
/// only the outermost is written. Empty when it is neither.
StringRef wrongConceptPrefix(StringRef Rest);

/// The concept prefixes, quoted, as a diagnostic lists them.
std::string quotedConceptPrefixes();

/// Whether \p Rest, a name after its storage prefix, is upper camel case,
/// optionally led by concept prefixes (`bValue`, `pValue`).
/// Whether the groups that underscores separate in \p Rest, like
/// `Category_Value`, start with an upper case letter or a digit.
bool hasUpperGroups(StringRef Rest);

/// Whether \p Name is one of the built-in types of Malterlib, like `uint32`,
/// which a group of a name can be, like `DMibPCanDo_fp32`.
bool isBuiltinTypeName(StringRef Name);

bool isUpperCamelWithConcepts(StringRef Rest);

/// What \p Rest, a name after its storage prefix, names: without its outer
/// concept prefix, like `Value` of `pValue`.
StringRef withoutConcept(StringRef Rest);

/// The least number of characters a name has after its prefixes by default,
/// like `Value` of `m_pValue`.
inline constexpr unsigned DefaultMinimumNameLength = 2;

/// Whether \p Name starts with one of \p TypePrefixes followed by an upper case
/// letter or a digit, like `CName` for the prefix `C`.
bool hasTypePrefix(StringRef Name, llvm::ArrayRef<StringRef> TypePrefixes);

/// Whether \p Name has no upper case letters. Malterlib writes names that
/// emulate language features (`inline_always`, `uint32`) in lower case, and
/// protocols of the language and of external libraries use lower case names
/// (`await_ready`, `begin`, `promise_type`), so these names are not checked.
bool isLowerCaseName(StringRef Name);

/// The lower case types that are allowed by default, besides the built-in
/// types of the vocabulary, `uint32` of Malterlib: the names of the protocols
/// of the language and the standard library, like `value_type`,
/// `promise_type` and the `rebind` and `other` of an allocator.
inline constexpr llvm::StringLiteral DefaultLowerCaseTypes =
    "allocator_type;argument_type;char_type;const_iterator;"
    "const_local_iterator;const_pointer;const_reference;"
    "const_reverse_iterator;const_void_pointer;container_type;"
    "difference_type;element_type;first_type;hasher;int_type;"
    "is_always_equal;is_transparent;iterator;iterator_category;"
    "iterator_concept;key_compare;"
    "key_equal;key_type;local_iterator;mapped_type;off_type;other;pointer;"
    "pos_type;promise_type;propagate_on_container_copy_assignment;"
    "propagate_on_container_move_assignment;propagate_on_container_swap;"
    "rebind;reference;result_type;reverse_iterator;second_type;size_type;"
    "state_type;traits_type;type;value_compare;value_type;void_pointer";

/// The lower case functions that are allowed by default: the protocols of the
/// language and the standard library, like `begin`, `swap`, `lock` and
/// `await_ready`,
/// and the member functions of standard containers, for compatible types.
inline constexpr llvm::StringLiteral DefaultLowerCaseFunctions =
    "allocate;allocate_at_least;assign;at;await_ready;await_resume;await_"
    "suspend;"
    "await_transform;back;before_begin;begin;bucket;bucket_count;bucket_size;"
    "c_str;capacity;cbefore_begin;cbegin;cend;clear;compare;"
    "compare_exchange_strong;compare_exchange_weak;construct;copy;count;"
    "crbegin;crend;data;deallocate;destroy;emplace;emplace_after;"
    "emplace_back;emplace_front;emplace_hint;empty;end;equal_range;erase;"
    "erase_after;exchange;fetch_add;fetch_and;fetch_or;fetch_sub;fetch_xor;"
    "fill;final_suspend;find;find_first_not_of;find_first_of;"
    "find_last_not_of;find_last_of;front;get;get_allocator;get_return_object;"
    "get_return_object_on_allocation_failure;hash_function;initial_suspend;"
    "insert;insert_after;is_lock_free;iter_move;iter_swap;key_comp;key_eq;"
    "length;load;"
    "load_factor;lock;lock_shared;lower_bound;max_bucket_count;"
    "max_load_factor;max_size;merge;pointer_to;pop;pop_back;pop_front;push;"
    "push_back;"
    "push_front;rbegin;rehash;remove;remove_if;rend;reserve;resize;"
    "return_value;return_void;reverse;rfind;"
    "select_on_container_copy_construction;shrink_to_fit;size;sort;splice;"
    "splice_after;store;substr;swap;top;try_lock;try_lock_for;"
    "try_lock_shared;try_lock_shared_for;try_lock_shared_until;"
    "try_lock_until;unhandled_exception;unique;unlock;unlock_shared;"
    "upper_bound;value_comp;yield_value";

/// The lower case free functions that are allowed by default: those of the
/// protocols of the language and the standard library that find them by
/// argument dependent lookup, like `swap` and `begin` of a range.
inline constexpr llvm::StringLiteral DefaultLowerCaseFreeFunctions =
    "begin;end;cbegin;cend;rbegin;rend;crbegin;crend;swap;iter_move;"
    "iter_swap;get";

/// The location to report a diagnostic about the token at \p Loc at, or an
/// invalid location when it is not reported: the token was not written in the
/// source (token pasting) or comes from a system header. A token written in
/// the body of a macro is reported in the macro definition.
SourceLocation reportLocation(SourceLocation Loc, const SourceManager &SM);

/// Like reportLocation, for \p Loc in what ends at \p End, like a lambda: a
/// token of the body of a macro, like the `[&]` of `DMibTestCategory("Name")
/// % [&] () { ... }`, whose lambda the code that uses the macro writes the
/// rest of, is reported where the macro is used, in that code.
SourceLocation reportLocationIn(SourceLocation Loc, SourceLocation End,
                                const SourceManager &SM);

/// The location to report the name of \p D at, or an invalid location when the
/// name is not checked: it was not written in the source (token pasting) or it
/// comes from a system header, like a name declared by an external macro. A
/// name written in the body of a macro is reported in the macro definition.
SourceLocation nameLocation(const NamedDecl *D, const SourceManager &SM);

/// Whether \p D is declared in a specialization of a template from a system
/// header, like `std::tuple_size<CType>`, whose member names are given by the
/// template.
bool isInExternalSpecialization(const Decl *D, const SourceManager &SM);

/// Matches a `typeid` whose operand is not evaluated: a type, or an
/// expression that is not a glvalue of a polymorphic class.
AST_MATCHER(Expr, isUnevaluatedTypeid) {
  const auto *Typeid = dyn_cast<CXXTypeidExpr>(&Node);
  return Typeid && !Typeid->isPotentiallyEvaluated();
}

/// Whether \p Node is not evaluated: in `decltype` or another type, `sizeof`,
/// `alignof`, `noexcept`, `typeid` of a non-polymorphic operand or a requires
/// expression. It walks the parents itself, which is much cheaper than
/// `hasAncestor`, whose results the match finder memoizes for each node.
bool isInUnevaluatedOperand(const DynTypedNode &Node, ASTContext &Context);

/// Whether \p Node is in a branch of an `if constexpr` that is discarded,
/// which never runs.
bool isInDiscardedBranch(const DynTypedNode &Node, ASTContext &Context);

/// Whether \p Node is in a template, also in a member of a class template or
/// in a generic lambda, which is checked as its instantiations make it, or in
/// an instantiation that another file makes, `isExternInstantiation`, which
/// is checked there. Not one that an instantiation shares with its template,
/// `mp_Items` of `mp_Items.f_Remove(_Key)`, which has a parent in each.
bool isInTemplatePattern(const DynTypedNode &Node, ASTContext &Context);

/// The parents of \p Node, those in an instantiation first: of a node that an
/// instantiation shares with its template, the one in the instantiation, not
/// the dependent one in the template, `mp_Items.f_Remove` of `_Key` that
/// depends on a parameter.
llvm::SmallVector<DynTypedNode, 2> instantiatedParents(const DynTypedNode &Node,
                                                       ASTContext &Context);
template <typename t_CNode>
llvm::SmallVector<DynTypedNode, 2> instantiatedParents(const t_CNode &Node,
                                                       ASTContext &Context) {
  return instantiatedParents(DynTypedNode::create(Node), Context);
}

/// Whether \p D is, or is in, an instantiation that an explicit instantiation
/// declaration, `extern template struct TCBox<int>;`, says another file
/// makes with its explicit instantiation definition, which checks it: also
/// what this file instantiates of it, like its inline members.
bool isExternInstantiation(const Decl *D);

/// What a diagnostic names for \p Declaration: the template an instantiation
/// is made from, so that what its instantiations report is reported once.
const NamedDecl *diagnosticDecl(const NamedDecl *Declaration);

/// \p Type as a diagnostic names it, quoted, as it is written, also with what
/// it is an alias of, `'CStrSpan' (aka 'TCStrSpan<char>')`, unless that has
/// too many parts, which can be exponentially many, like `TCMap<CPrevious,
/// CPrevious>` of an alias of each level.
std::string diagnosticType(QualType Type, const ASTContext &Context);

/// Whether \p Type has too many parts to print in a diagnostic, like
/// `TCFunction<CPrevious (CPrevious)>` nested many times, whose printed form
/// can grow exponentially.
bool isTooLargeToPrint(QualType Type);

/// Whether the template arguments of \p Declaration, or of the classes it is
/// in, are too large to print, like isTooLargeToPrint tells of a type.
bool hasTooLargeTemplateArguments(const NamedDecl *Declaration);

/// \p Declaration qualified, without the template arguments of it and of the
/// classes it is in, `TCReader<...>::f_Read<...>`, for when they are too large
/// to print.
std::string abbreviatedName(const NamedDecl *Declaration);

/// Whether \p Declaration is named \p Name, qualified, like
/// `NMib::NConcurrency::CActor`, also with a leading `::`: each part against
/// the name and the contexts it is in, innermost first, past the inline
/// namespaces the name leaves out, without printing them, which repeats the
/// arguments of the templates they are in and can be exponentially long.
bool hasQualifiedName(const NamedDecl *Declaration, StringRef Name);

/// Matches expressions that are not evaluated, like `isInUnevaluatedOperand`.
AST_MATCHER(Stmt, isInUnevaluatedContext) {
  return isInUnevaluatedOperand(DynTypedNode::create(Node),
                                Finder->getASTContext());
}

/// Joins \p Prefixes into a quoted list for a diagnostic: `'m_'` or
/// `'mc_' or 'ms_'`.
std::string quotedList(llvm::ArrayRef<StringRef> Prefixes);

/// Whether \p Record declares what can make it called, a function object: a
/// call operator, or a conversion to a pointer or reference to a function,
/// also in a base, and whatever they are, also deleted or constrained, so
/// that a name says what its author means it to be. Not of what isn't a class.
/// None when that can't be told: of a class that isn't defined in the translation unit, or through
/// a base that depends on parameters of a template.
std::optional<bool> declaresCallable(const CXXRecordDecl *Record);

/// Whether \p Function can be used: not deleted, `void operator++() =
/// delete;`, nor with a constraint that doesn't hold, `void operator()()
/// requires false;`. None for one whose constraint depends on parameters of a
/// template, `requires t_bEnable`, which can't be told.
std::optional<bool> isUsableFunction(const FunctionDecl *Function);

/// Whether \p Record, or a base of it, declares one of \p Operators that can
/// be used, also as a template or brought in with `using`: not one that is
/// deleted, or whose constraint doesn't hold, which hides those of the bases.
/// None when that can't be told: of a class that isn't defined in the
/// translation unit, like a specialization of a template that isn't
/// instantiated, which isn't made until it is used, through a base that
/// depends on parameters of a template, or of an operator whose constraint
/// does.
std::optional<bool>
hasOverloadedOperator(const CXXRecordDecl *Record,
                      llvm::ArrayRef<OverloadedOperatorKind> Operators);

/// The pointer that \p Call gives back as it is given it, of what the
/// compiler only looks at, `__builtin_launder(pValue)`,
/// `__builtin_assume_aligned(pValue, 16)` or `std::launder(pValue)`. Null for
/// other calls.
const Expr *keptPointerArgument(const CallExpr *Call);

/// The arguments of \p Call that the function it calls returns as they are,
/// on any of its returns, like `_pData` of `int const *fg_Get(int const
/// *_pData) { return _pData; }`, also after an early `return nullptr;`, both
/// of `_pFirst ? _pFirst : _pSecond`. With \p Addressed, instead those whose
/// reference parameter it returns the address of, `return &_Value;`. None
/// when it can't be looked into.
llvm::SmallVector<const Expr *, 2> returnedArguments(const CallExpr *Call,
                                                     bool Addressed = false);

/// Whether the body of \p Definition calls its callable parameter \p
/// Parameter while it runs: `_fWork()`, also by name, `_fWork.operator()()`,
/// through a local copy of it or one it is moved to, or by giving it on to
/// another function that does, `fg_Invoke(_fWork)`, but not in what isn't
/// evaluated nor from a lambda that is only made. \p CalledFunction tells
/// what a call calls, and \p DestructorCalls whether the destructor of a local
/// made with it can call it at the end of its scope.
bool callsParameter(
    const FunctionDecl *Definition, const ParmVarDecl *Parameter,
    llvm::SmallPtrSetImpl<const ParmVarDecl *> &Visited,
    llvm::function_ref<const FunctionDecl *(const CallExpr *)> CalledFunction,
    llvm::function_ref<bool(const CXXRecordDecl *)> DestructorCalls);

/// Whether the function \p Call calls, when its body can be seen, calls the
/// argument \p Index before it returns, like callsParameter, `fg_Read(fRead)`
/// with `int fg_Read(auto &&_fRead) { return _fRead(); }`.
bool callsArgument(const CallExpr *Call, unsigned Index);

/// The value that \p Ref, a use of the local \p Local, reads: what the last
/// assignment before it gives, like `pCopy = new int` or `fCallback = [this]
/// { ... }`, also an `operator=`, when it runs on every path to the use, as a
/// statement of a block around it, or the initializer when nothing assigns
/// it before. Null when that can't be told, like after an assignment in a
/// branch, or when a loop around the use assigns it after the use, which the
/// next time around reads.
const Expr *assignedValueAt(const VarDecl *Local, const DeclRefExpr *Ref);

/// The function whose body \p Call runs, and the index of the argument of
/// its first parameter: 1 of a member operator, whose object is first,
/// `Reset{}(pThis)`, else 0. Null when its body can't be seen.
std::pair<const FunctionDecl *, unsigned>
calledFunctionOf(const CallExpr *Call);

/// Whether \p Function, whose body can be seen, casts the const of its
/// reference parameter \p iParameter away, `const_cast<CState &>(_State)`,
/// also through local references to it and the functions it gives it to.
bool castsConstAway(const FunctionDecl *Function, unsigned iParameter);

/// The instantiations of \p Function, a function in a template: of its
/// template, of the member function of each instantiation of the class it is
/// in, or of a lambda, the call operator of the lambda each instantiation of
/// the function it is in makes, also of a generic one, its specializations.
llvm::SmallVector<const FunctionDecl *, 4>
functionInstances(const FunctionDecl *Function, int Depth = 0);

/// Whether \p Type is a value that refers to nothing: a number, an enum, or
/// a trivially copyable class or array of them, `struct CState { int m_Value;
/// }`, whose copy shares nothing with what it is copied from.
bool holdsOnlyValues(QualType Type, const ASTContext &Context, int Depth = 0);

/// The values that \p Ref, a use of the local \p Local, can read, like
/// assignedValueAt, also those of both branches of an `if` that each end
/// assigning it, `if (bChoose) pValue = &First; else pValue = &Second;`.
/// Empty when that can't be told, also after what changes it otherwise,
/// `++pValue` or `fg_Reset(pValue)`.
llvm::SmallVector<const Expr *, 2> assignedValuesAt(const VarDecl *Local,
                                                    const DeclRefExpr *Ref);

/// The two objects \p Call swaps, `fg_Swap(A, B)`, `std::swap(A, B)`, also
/// found by its arguments, or `std::ranges::swap(A, B)`, a call of the object
/// that customizes it, or nulls when it isn't a swap. Not a member function,
/// `A.swap(B)`.
std::pair<const Expr *, const Expr *> swappedArguments(const CallExpr *Call);

/// The lambda that calling \p Callee runs: a lambda expression, one held in a
/// local, `fClear()`, a copy of one, `auto fCopy = fClear`, also moved or
/// copied by a helper, `fg_Move(fClear)` or `fg_TempCopy(fClear)`, or one that
/// a local pointer points to, `(*pClear)()` of `auto pClear = &fClear`. Null
/// when that can't be told.
const LambdaExpr *calledLambda(const Expr *Callee);

/// Whether \p Function, whose body can be seen, only assigns what its
/// parameter at \p iParameter refers to, with values that don't use it: a
/// reference, `_pValue = nullptr` of `int *&_pValue`, also a member of it,
/// `_Copy.m_pData = nullptr`, or what a pointer points to, `*_ppValue =
/// nullptr`, with the built-in assignment, which runs nothing else.
bool onlyAssignsThrough(const FunctionDecl *Function, unsigned iParameter);

/// Whether the body of \p Function, and the initializers of a constructor,
/// can be seen to only read its parameter \p iParameter, which it takes by a
/// reference that isn't to const: as a value, through a member function that
/// is const, or given to what takes it as const or by value, or only reads it
/// in turn, like `TCVector(TCVector &_Other) : TCVector(fg_Const(_Other))`.
bool onlyReadsParameter(const FunctionDecl *Function, unsigned iParameter,
                        int Depth = 0);

/// Whether \p S isn't evaluated, nor what is in it: `sizeof`, `alignof`,
/// `noexcept(...)`, a `typeid` that doesn't look at the dynamic type, a
/// requires expression, or what a builtin only looks at the type or constness
/// of, `__builtin_constant_p(...)` or `__builtin_object_size(...)`. Of
/// `__builtin_choose_expr`, only the arm it chooses runs.
bool isUnevaluatedNode(const Stmt *S);

/// Whether \p Initialization initializes with the braced list \p Operand: a
/// constructor called with its elements, `co_return {Value};` of a class with
/// a constructor, which are the same expressions, also with default arguments
/// after them, or another form of the list.
inline bool isListOf(const Expr *Initialization, const Expr *Operand) {
  const auto *List = dyn_cast<InitListExpr>(Operand);
  if (!List || !Initialization)
    return false;
  if (const auto *Other = dyn_cast<InitListExpr>(Initialization))
    return Other->getSyntacticForm() == List ||
           List->getSyntacticForm() == Other ||
           (Other->getSyntacticForm() &&
            Other->getSyntacticForm() == List->getSyntacticForm());
  const auto *Construct = dyn_cast<CXXConstructExpr>(Initialization);
  if (!Construct || !Construct->isListInitialization())
    return false;
  unsigned iArgument = 0;
  for (const Expr *Element : List->inits()) {
    if (iArgument == Construct->getNumArgs() ||
        Construct->getArg(iArgument)->IgnoreImplicit() !=
            Element->IgnoreImplicit())
      return false;
    ++iArgument;
  }
  for (; iArgument < Construct->getNumArgs(); ++iArgument)
    if (!isa<CXXDefaultArgExpr>(Construct->getArg(iArgument)))
      return false;
  return true;
}

/// The children of \p S, without the operand of a co_await or co_yield, which
/// its common expression evaluates too, nor of a co_return, which its call of
/// `return_value` is given: a walk that went into both would go through the
/// operand twice, and through nested awaits, or nested coroutine lambdas that
/// return, exponentially often.
inline Stmt::const_child_range childrenOnce(const Stmt *S) {
  Stmt::const_child_range Children = S->children();
  if (const auto *Suspend = dyn_cast<CoroutineSuspendExpr>(S);
      Suspend && Suspend->getCommonExpr())
    return {std::next(Children.begin()), Children.end()};
  if (const auto *Return = dyn_cast<CoreturnStmt>(S);
      Return && Return->getOperand() && Return->getPromiseCall())
    if (const auto *Call =
            dyn_cast<CallExpr>(Return->getPromiseCall()->IgnoreImplicit());
        Call && Call->getNumArgs() >= 1 &&
        // Also with default arguments after it, `return_value(int, int = 0)`.
        std::all_of(std::next(Call->arg_begin()), Call->arg_end(),
                    [](const Expr *Default) {
                      return isa<CXXDefaultArgExpr>(Default);
                    })) {
      // Not of `co_return fg_Void();`, whose `return_void` isn't given it.
      // Also converted or moved, which wraps it in implicit nodes.
      const Expr *Argument = Call->getArg(0);
      for (int Depth = 0; Depth < 8 && Argument != Return->getOperand() &&
                          !isListOf(Argument, Return->getOperand());
           ++Depth) {
        // Also with default arguments after it, `CResult(int, int = 0)`.
        if (const auto *Construct = dyn_cast<CXXConstructExpr>(Argument);
            Construct && Construct->getNumArgs() >= 1 &&
            std::all_of(std::next(Construct->arg_begin()),
                        Construct->arg_end(), [](const Expr *Default) {
                          return isa<CXXDefaultArgExpr>(Default);
                        }))
          Argument = Construct->getArg(0);
        // Also by a conversion function of it, `operator int()`.
        else if (const auto *Conversion = dyn_cast<CXXMemberCallExpr>(Argument);
                 Conversion &&
                 isa_and_nonnull<CXXConversionDecl>(Conversion->getMethodDecl()))
          Argument = Conversion->getImplicitObjectArgument();
        else if (const Expr *Inner = Argument->IgnoreImplicit();
                 Inner != Argument)
          Argument = Inner;
        else
          break;
      }
      if (Argument == Return->getOperand() ||
          isListOf(Argument, Return->getOperand()))
        return {std::next(Children.begin()), Children.end()};
    }
  return Children;
}

/// The children of \p S as childrenOnce, but of an `if constexpr` whose
/// condition is known, not the branch it discards, which doesn't run.
llvm::SmallVector<const Stmt *, 4> keptChildren(const Stmt *S,
                                                const ASTContext &Context);

/// Whether \p Member is a member of a template given a parameter that gives
/// what it is given, `typename std::enable_if<t_bEnable, t_CType>::type`, also
/// one of a Malterlib one, `typename TCIdentity<t_CType>::CType` or `typename
/// TCMap<int, t_CValue>::CValue`: its member, in the template, gives what it
/// is given as it is, unlike `using type = void ();` or `using CType = t_CType
/// *;`, and is generic, whatever it is given. The one it gives, when the
/// template's own member is one of its parameters, must be the one given. With
/// \p Nested, also given one in another type, `typename TCGet<t_iIndex - 1,
/// TCTypeList<tp_CRest...>>::CType`, but not a member of one, `typename
/// t_CTraits::FSignature`, which is what that is.
bool isTypeOfGivenParameter(const clang::Type *Member, bool Nested);

/// The argument that \p Member, a member of a template that gives what it is
/// given as isTypeOfGivenParameter tells, gives, `tf_FCallback` of `typename
/// TCMap<int, tf_FCallback>::CValue` whose `CValue` is its `t_CValue`, also
/// one that isn't a parameter, `typename tf_CTraits::FSignature`. Null when
/// that can't be told, like when a partial specialization that can be what
/// is written defines it.
const clang::TemplateArgument *forwardedArgument(const clang::Type *Member);

/// Whether \p Type is a template parameter or gives one as it is, which is
/// generic: a transformation of it, also through others,
/// `TCRemoveQualifiers<TCRemoveReference<t_CType>>`, alias templates, and the
/// `type` of a template given one, as isTypeOfGivenParameter tells with \p
/// Nested.
bool givesParameter(QualType Type, bool Nested);

/// Whether \p Type, an instantiation of a member of what a parameter is
/// given, `typename t_CMap::CKey` of `TCMap<EMode, int>`, gives an argument
/// of that as it is, `using CKey = t_CKey;`, also by reference, `using
/// CValueRef = t_CValue const &;`, and through members of others that do and
/// traits, `TCRemoveReference<t_CValue>`, which is generic, whatever it is
/// given.
bool forwardsArgument(QualType Type);

/// Whether \p Written is a member of what a parameter is given, `typename
/// t_CMap::CValue`, also through a template given it that defines its member
/// as one of that for every argument, `typename TCAdapter<t_CMap>::CValue` of
/// `using CValue = typename t_CContainer::CValue;`, which what it is given can
/// define as one of its arguments, as forwardsArgument tells of what an
/// instantiation makes it.
bool isMemberOfGivenParameter(const clang::Type *Written, int Depth = 0);

/// Whether the member function \p Method can be seen only to give access to
/// its object, like `operator[]` that checks the index and returns a reference
/// into it, or `fp_GetAsStorage()`: its body changes nothing but its own
/// locals, and calls only what can't change anything of the object either,
/// member functions that are const or only give access in turn, and functions
/// given only values or pointers and references to const. What changes the
/// object through what it gives is where that is used.
bool onlyGivesAccess(const CXXMethodDecl *Method);

/// The parameters at the place of \p Parameter, of a function type written in
/// the type of a declaration, `TCFunction<void(typename t_CTraits::CPointer
/// _pValue)> m_fCallback`, in the instantiations of that declaration: a
/// member of a class template, or a local or a parameter of a function
/// template.
llvm::SmallVector<const ParmVarDecl *, 4>
functionTypeParameterInstances(const ParmVarDecl *Parameter,
                               ASTContext &Context);

/// The types of the parameters functionTypeParameterInstances gives, and
/// also of those at the place of \p Parameter in what the uses of an alias
/// template whose type it is in make of it, `TCCallback<CTraits>` of
/// `template <typename t_CTraits> using TCCallback = TCFunction<void(typename
/// t_CTraits::CBool _bReady)>;`, which has no declarations of them. With
/// \p Adjusted, the types the parameters have, `int *` of `int _Values[2]`,
/// which those of the uses of alias templates are anyway, and else the types
/// they are written with.
llvm::SmallVector<QualType, 4>
functionTypeParameterTypes(const ParmVarDecl *Parameter, ASTContext &Context,
                           bool Adjusted = false);

/// The declarations that the instantiations of the function template \p
/// Pattern is in make of it, a local of the function, like `auto Value =
/// static_cast<tf_CType *>(nullptr)`, `using FValue = tf_CType` or a local
/// class, or a parameter of it: the declaration of each instantiation of the
/// function, also of a
/// member function of an instantiation of a class template, at the place of
/// \p Pattern.
llvm::SmallVector<const Decl *, 4> localInstances(const Decl *Pattern);

/// Whether \p Atomic only loads, `__atomic_load_n(&Value, ...)`, unlike the
/// other atomic operations, which write what they are given a pointer to.
inline bool isAtomicLoad(const AtomicExpr *Atomic) {
  switch (Atomic->getOp()) {
  case AtomicExpr::AO__c11_atomic_load:
  case AtomicExpr::AO__atomic_load:
  case AtomicExpr::AO__atomic_load_n:
  case AtomicExpr::AO__hip_atomic_load:
  case AtomicExpr::AO__opencl_atomic_load:
  case AtomicExpr::AO__scoped_atomic_load:
  case AtomicExpr::AO__scoped_atomic_load_n:
    return true;
  default:
    return false;
  }
}

/// The classes instantiated from \p Pattern, a class in a template: of its
/// template, of a partial specialization it is, or the members of the
/// instantiations of the class it is in, also the instantiations of a member
/// template of each, `TCOuter<int>::TCInner<int>`.
llvm::SmallVector<const CXXRecordDecl *, 4>
instancesOf(const CXXRecordDecl *Pattern, int Depth = 0);

/// Whether \p Callee is a member function whose operator calls give the
/// object as their first argument: one with an implicit object, and a static
/// one, `static void operator()(...)`, whose calls give the object they are
/// called on too, not one with an explicit one, `bool operator!(this CType
/// const &_Self)`, whose object is its first parameter.
inline bool hasImplicitObject(const Decl *Callee) {
  const auto *Method = dyn_cast_or_null<CXXMethodDecl>(Callee);
  return Method && !Method->isExplicitObjectMemberFunction();
}

/// Whether \p Method can't change the object it is called on: a const one,
/// `f_Get() const`, also one whose explicit object is a reference to const or
/// a copy, `operator&(this CSet const &_Self, CSet const &_Other)`.
inline bool hasConstObject(const CXXMethodDecl *Method) {
  if (!Method->isExplicitObjectMemberFunction())
    return Method->isConst();
  const QualType Type = Method->getParamDecl(0)->getType();
  return !Type->isReferenceType() ||
         Type.getNonReferenceType().isConstQualified();
}

/// Whether \p Call can run an override of what it names, whose body isn't
/// the one seen: of a virtual member function that isn't final, not called
/// by its name, `Reset.CReset::f_Reset(...)`, nor on an object whose type is
/// what it is declared as, a variable or a member that isn't a reference,
/// `Value.f_Get()`.
inline bool callsOverride(const CallExpr *Call) {
  const auto *Method = dyn_cast_or_null<CXXMethodDecl>(Call->getDirectCallee());
  if (!Method || !Method->isVirtual() || Method->hasAttr<FinalAttr>() ||
      Method->getParent()->hasAttr<FinalAttr>())
    return false;
  const auto *Member =
      dyn_cast<MemberExpr>(Call->getCallee()->IgnoreParenImpCasts());
  if (Member && Member->hasQualifier())
    return false;
  // Nor on an object of a final class, also through a pointer or reference,
  // whose type is what it is.
  if (const auto *MemberCall = dyn_cast<CXXMemberCallExpr>(Call))
    if (const CXXRecordDecl *Object = MemberCall->getRecordDecl();
        Object && Object->hasDefinition() &&
        Object->getDefinition()->hasAttr<FinalAttr>())
      return false;
  if (Member && !Member->isArrow()) {
    const Expr *Object = Member->getBase()->IgnoreParenImpCasts();
    const ValueDecl *Named = nullptr;
    if (const auto *Ref = dyn_cast<DeclRefExpr>(Object))
      Named = dyn_cast<VarDecl>(Ref->getDecl());
    else if (const auto *Field = dyn_cast<MemberExpr>(Object))
      Named = dyn_cast<FieldDecl>(Field->getMemberDecl());
    if (Named && !Named->getType()->isReferenceType())
      return false;
  }
  return true;
}

/// The nodes a check was given in a translation unit, which the matchers
/// can find again, like the parts of `co_await` that each refer to its
/// operand, which nested awaits make exponentially many, so that each is
/// checked once.
class CRepeatedMatches {
public:
  /// Whether the nodes of \p Result were given before.
  bool repeated(const ast_matchers::MatchFinder::MatchResult &Result);
  /// Forgets them, at the start of a translation unit, whose nodes can be at
  /// the addresses of the one before.
  void invalidate() {
    SeenOne.clear();
    SeenMany.clear();
  }

private:
  /// The names nodes are bound as, by a number each.
  llvm::StringMap<unsigned> Names;
  /// What matches with one node gave, most of them, and with more.
  llvm::DenseSet<std::pair<unsigned, const void *>> SeenOne;
  std::set<std::vector<std::pair<unsigned, const void *>>> SeenMany;
};

} // namespace clang::tidy::malterlib

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_NAMINGUTILS_H
