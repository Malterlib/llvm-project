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
#include "StackSegments.h"
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
#include <optional>
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

/// Whether the token at \p Loc is written where it is used: in a file, or as
/// an argument of a macro written there, not in the body of a macro, also not
/// given as an argument in the body of another.
bool isWrittenWhereExpanded(SourceLocation Loc, const SourceManager &SM);

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

/// Whether \p D, a type, a template or another declaration, is external: its
/// name is a library's, not this code's to name or rewrite. It is when its
/// definition is in a system header, or, when it has none, any of its
/// declarations is, also one this code redeclares before or after it, `namespace
/// NLibrary { template <typename t_CType> class TCSerializer; }`. The one
/// answer of every check that asks whether a declaration is the code's own.
bool isExternalDeclaration(const Decl *D, const SourceManager &SM);

/// Whether \p D is declared in a specialization of a template from a system
/// header, like `std::tuple_size<CType>`, whose member names are given by the
/// template (isExternalDeclaration).
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

/// Whether the operands of \p S aren't evaluated, so that what is written in
/// them neither runs nor changes anything: every kind of `sizeof`-like trait,
/// `sizeof`, `alignof`, `__datasizeof` and the others, but the size of a
/// variable length array, `noexcept`, a `typeid` that doesn't look at the
/// dynamic type, `__uuidof`, the traits of types and expressions,
/// `__is_lvalue_expr(...)`, a requires expression and a concept, and what a
/// builtin doesn't evaluate, as clang marks its arguments,
/// `__builtin_constant_p(Value)`, `__builtin_object_size(...)` or
/// `__builtin_classify_type(...)`, also what `__builtin_assume(...)` or
/// `__assume(...)` is given. The one classifier of what isn't evaluated.
bool hasUnevaluatedOperands(const Stmt *S);

/// Whether \p Current is in an unevaluated operand of \p Parent, its parent:
/// a type, what hasUnevaluatedOperands tells of, not the callee of a builtin,
/// or an arm, or the condition, of `__builtin_choose_expr` or `_Generic` that
/// isn't chosen. Each step up of isInUnevaluatedOperand.
bool isUnevaluatedOperandOf(const DynTypedNode &Current,
                            const DynTypedNode &Parent);

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
  if (stackNearlyExhausted())
    return withStack([&] { return instantiatedParents(Node, Context); });
  return instantiatedParents(DynTypedNode::create(Node), Context);
}

/// Whether \p D is, or is in, an instantiation that an explicit instantiation
/// declaration, `extern template struct TCBox<int>;`, says another file
/// makes with its explicit instantiation definition, which checks it: also
/// what this file instantiates of it, like its inline members.
bool isExternInstantiation(const Decl *D);

/// Whether the translation unit of \p Context has an error that stops it
/// from compiling, as it had before the checks reported anything
/// (noteUncompilableErrors): clang's own analyses build no CFG of its
/// functions then, and the checks build none either (buildGraph).
bool hasUncompilableErrors(const ASTContext &Context);

/// Reads now whether the translation unit of \p Context has an error that
/// stops it from compiling (hasUncompilableErrors). Where the checks report
/// through the compilation's diagnostics, as the plugin's do, this is read
/// before they run: a warning they report as an error isn't one of the code.
void noteUncompilableErrors(const ASTContext &Context);

/// What a diagnostic names for \p Declaration: the template an instantiation
/// is made from, so that what its instantiations report is reported once.
const NamedDecl *diagnosticDecl(const NamedDecl *Declaration);

/// \p Type as a diagnostic names it, quoted, as it is written, also with what
/// it is an alias of, `'CStrSpan' (aka 'TCStrSpan<char>')`, unless that has
/// too many parts, which can be exponentially many, like `TCMap<CPrevious,
/// CPrevious>` of an alias of each level.
std::string diagnosticType(QualType Type, const ASTContext &Context);

/// \p Declaration as a diagnostic names it, quoted, as clang names a
/// declaration given to a diagnostic, with its template arguments, \p
/// Qualified also with what it is in, `'TCReader<int>::f_Read<bool>'`, unless
/// they are too large to print (hasTooLargeTemplateArguments), which can be
/// exponentially many: then without them, `'TCReader<...>::f_Read<...>'`.
/// Empty for none.
/// Every type and declaration a diagnostic prints goes through this or
/// diagnosticType.
std::string diagnosticName(const NamedDecl *Declaration,
                           bool Qualified = false);

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

/// Whether \p Answer, a classification that can be unknown, is known to hold:
/// what a name is required to follow. A classification a naming rule reads is
/// three-valued, and folds into a yes or a no only through this or
/// possiblyTrue, so that an unknown answer never rejects a name.
inline bool knownTrue(std::optional<bool> Answer) {
  return Answer.has_value() && *Answer;
}

/// Whether \p Answer, a classification that can be unknown, can hold, also
/// where that can't be told: what a name is allowed to follow (knownTrue).
inline bool possiblyTrue(std::optional<bool> Answer) {
  return !Answer.has_value() || *Answer;
}

/// Whether either of \p Left and \p Right holds: known when one is known to
/// hold or both are known not to, else unknown (knownTrue).
inline std::optional<bool> eitherHolds(std::optional<bool> Left,
                                       std::optional<bool> Right) {
  if (knownTrue(Left) || knownTrue(Right))
    return true;
  if (!Left.has_value() || !Right.has_value())
    return std::nullopt;
  return false;
}

/// Whether \p Record declares what can make it called, a function object: a
/// call operator, or a conversion to a pointer or reference to a function,
/// also in a base, and whatever they are, also deleted or constrained, as
/// candidateGives reads them declared, so
/// that a name says what its author means it to be. Not of what isn't a class.
/// None when that can't be told: of a class that isn't defined in the translation unit, or through
/// a base that depends on parameters of a template.
std::optional<bool> declaresCallable(const CXXRecordDecl *Record);

/// What an object of a class can be used for that a concept rests on: a
/// unary `operator*`, `operator->`, `operator++`, a call, a conversion to
/// `bool`, a conversion to a function or a pointer to one.
enum class ECapability {
  Dereference,
  Arrow,
  Increment,
  Call,
  ConvertToBool,
  ConvertToFunction
};

/// How a concept reads a capability: by what a class declares, whatever its
/// state, deleted or constrained, or by what code can use.
enum class EReading { Declared, Usable };

/// What \p Candidate, a declaration found for \p Capability (a function, a
/// function template, what a using declaration brings in), gives an object, as
/// \p Reading reads it: one of the wrong shape, a binary `operator*` or a
/// conversion to another type, nothing; read as usable, not one that is
/// deleted or whose constraint doesn't hold, and none, unknown, for one whose
/// constraint clang never evaluated, `requires t_bEnabled` that nothing uses, or
/// a template nothing instantiates whose constraint depends on its own
/// parameters or that a use can't give a parameter, or a using declaration of
/// what depends on parameters, but the call operator's as declared. A
/// conversion to what depends on template parameters, and of ConvertToBool to
/// what converts to `bool` in a condition, a pointer or a number, is none. The
/// one place that reads the state of such a declaration.
std::optional<bool> candidateGives(const Decl *Candidate,
                                   ECapability Capability, EReading Reading);

/// Whether an object of \p Record can be converted to `bool`: by a conversion
/// it or a base declares, one of a base hidden by one to the same type it
/// declares, as candidateGives reads it as usable. None when that can't be told: of a class
/// not defined in the translation unit, through a base that depends on
/// parameters of a template, or of a conversion candidateGives can't tell.
std::optional<bool> convertsToBool(const CXXRecordDecl *Record);

/// Whether \p Record, or a base of it, declares one of \p Operators that can
/// be used, also as a template or brought in with `using`: not one that is
/// deleted, or whose constraint doesn't hold, which hides those of the bases.
/// None when that can't be told: of a class that isn't defined in the
/// translation unit, like a specialization of a template that isn't
/// instantiated, which isn't made until it is used, through a base that
/// depends on parameters of a template, of an operator whose constraint
/// does, or of a non-member operator template that nothing instantiates for
/// it whose operand can be it, `operator++(TCIterator<tf_CType> &)`, which
/// clang deduces only where it is used.
std::optional<bool>
hasOverloadedOperator(const CXXRecordDecl *Record,
                      llvm::ArrayRef<OverloadedOperatorKind> Operators);

/// Like hasOverloadedOperator, of the whole search a name of a concept rests
/// on (decided Q7): also a non-member operator whose operand is a base,
/// declared where the class is, and those of the namespaces the class is in,
/// around the one argument-dependent lookup looks in. None found there is
/// none.
std::optional<bool>
hasUsableOperator(const CXXRecordDecl *Record,
                  llvm::ArrayRef<OverloadedOperatorKind> Operators);

/// The pointer that \p Call gives back as it is given it, of what the
/// compiler only looks at, `__builtin_launder(pValue)`,
/// `__builtin_assume_aligned(pValue, 16)`, `__builtin_align_up(pValue, 16)`
/// or `std::launder(pValue)`. Null for
/// other calls.
const Expr *keptPointerArgument(const CallExpr *Call);

/// The key of the element that the index \p Index selects when clang
/// evaluates it to a constant: its value, the same however wide it is
/// written, `umint(256)` and `256`, and as the offset of the pointer width
/// when it fits 64 bits, `pBits[umint(-1)]` and `pBits[-1]`. None when it
/// isn't a constant.
std::optional<std::string> constantKey(const Expr *Index,
                                       const ASTContext &Context);

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

/// The declaration as written that clang instantiated \p Instance from, a
/// function, class, variable or enum: through each level it is instantiated
/// at, a member of an instantiation of a class template, a member template of
/// one, the call operator of a lambda in an instantiation, to the declaration
/// in the template as written, or a partial specialization it is an instance
/// of, `TCBox<t_CType *>` of `TCBox<int *>`. Null when \p Instance, or what it
/// is instantiated from on the way, is written out: an explicit
/// specialization, `template <> void fg_Run<int>(int _Value)`, which has its
/// own declaration, also of a member, `template <> void
/// TCBox<int>::f_Run(int _Value)`, and a declaration that isn't
/// instantiated. What clang tells (getTemplateInstantiationPattern), which
/// stops at a member template written for an instantiation of its class,
/// `template <> template <typename tf_CType> void TCBox<int>::f_Run()`.
const Decl *writtenPatternOf(const Decl *Instance);

/// Whether clang instantiated \p Instance from \p Pattern, a declaration as
/// written (writtenPatternOf), or from what \p Pattern is instantiated from,
/// when it is an instantiation itself. Not an explicit specialization, nor an
/// instantiation of a partial specialization of the template of \p Pattern,
/// which is an instance of that partial specialization.
bool isInstanceOf(const Decl *Instance, const Decl *Pattern);

/// The instantiations of \p Function, a function in a template: of its
/// template, of the member function of each instantiation of the class it is
/// in, or of a lambda, the call operator of the lambda each instantiation of
/// the function it is in makes, also of a generic one, its specializations.
/// Only those clang instantiated from it (isInstanceOf).
llvm::SmallVector<const FunctionDecl *, 4>
functionInstances(const FunctionDecl *Function);

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
bool onlyReadsParameter(const FunctionDecl *Function, unsigned iParameter);

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
        Call) {
      // The value is the first argument, after the promise of an explicit
      // object parameter, `return_value(this promise_type &, int)`.
      const auto *Method =
          dyn_cast_or_null<CXXMethodDecl>(Call->getDirectCallee());
      const unsigned iValue =
          Method && Method->isExplicitObjectMemberFunction() &&
                  !isa<CXXMemberCallExpr>(Call)
              ? 1
              : 0;
      // Also with default arguments after it, `return_value(int, int = 0)`.
      if (Call->getNumArgs() <= iValue ||
          !std::all_of(std::next(Call->arg_begin() + iValue), Call->arg_end(),
                       [](const Expr *Default) {
                         return isa<CXXDefaultArgExpr>(Default);
                       }))
        return Children;
      // Not of `co_return fg_Void();`, whose `return_void` isn't given it.
      // Also converted or moved, which wraps it in implicit nodes.
      const Expr *Argument = Call->getArg(iValue);
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

llvm::SmallVector<const Stmt *, 4> keptChildren(const Stmt *S);

/// A walk of statements that visits each once: also one that others share,
/// which a coroutine's nodes do, the operand of each co_await in its common
/// expression and in the source of each OpaqueValueExpr of its protocol
/// calls, nested awaits each time, so that a walk that went into each would
/// be exponential. What to visit next the walker adds itself: the children
/// that can run (keptChildren), not what is statically dead, a branch whose
/// condition is constant in the instantiation, `if (t_bSelect)` of a false
/// `t_bSelect`, a discarded `if constexpr` branch, `while (false)`, nor what
/// isn't evaluated; the operands of the effect table, or some of them.
class CStmtWalk {
public:
  explicit CStmtWalk(const Stmt *Root) { add(Root); }

  /// The next statement to visit, null when none is left or it is stopped.
  const Stmt *next() {
    while (!bStopped && !Work.empty())
      if (const Stmt *S = Work.pop_back_val())
        return S;
    return nullptr;
  }
  /// Adds \p S to visit, unless it is added already.
  void add(const Stmt *S) {
    if (S && Seen.insert(S).second)
      Work.push_back(S);
  }
  /// Adds the children of \p S that can run (keptChildren).
  void addChildren(const Stmt *S) {
    for (const Stmt *Child : keptChildren(S))
      add(Child);
  }
  /// Ends the walk.
  void stop() { bStopped = true; }

private:
  llvm::SmallVector<const Stmt *, 32> Work;
  llvm::SmallPtrSet<const Stmt *, 32> Seen;
  bool bStopped = false;
};

/// What a visit of forEachStmtOnce does next.
enum class EWalk {
  /// Goes on into the children of what is visited.
  Descend,
  /// Goes on, not into its children.
  Skip,
  /// Ends the walk.
  Stop,
};

/// Visits \p Root and what is in it, each statement once (CStmtWalk), through
/// the children of each that can run (keptChildren), as \p Visit tells.
inline void forEachStmtOnce(const Stmt *Root,
                            llvm::function_ref<EWalk(const Stmt *)> Visit) {
  CStmtWalk Walk(Root);
  while (const Stmt *S = Walk.next())
    switch (Visit(S)) {
    case EWalk::Descend:
      Walk.addChildren(S);
      break;
    case EWalk::Skip:
      break;
    case EWalk::Stop:
      Walk.stop();
      break;
    }
}

/// The children of \p S that can run: of an `if constexpr` whose condition
/// is known, not the branch it discards, of an `if`, a loop or a `?:` whose
/// condition is constant, not what it never runs, `if (false)`, of `&&` or
/// `||` whose left side is constant and decides it, `false && ...`, not the
/// right side, and of what isn't evaluated, like `sizeof(Value)`,
/// `__builtin_constant_p(Value)` or `__builtin_assume(Value)`, none.
llvm::SmallVector<const Stmt *, 4> evaluatedChildren(const Stmt *S,
                                                     const ASTContext &Context);

/// The same without the context, by the value Sema keeps of the condition of
/// an `if constexpr`.
llvm::SmallVector<const Stmt *, 4> evaluatedChildren(const Stmt *S);

/// The children of \p S that can run (evaluatedChildren), what every walk
/// of what runs in an instantiation goes through.
llvm::SmallVector<const Stmt *, 4> keptChildren(const Stmt *S,
                                                const ASTContext &Context);

/// keptChildren in the context of the translation unit the checks run on
/// (setWalkContext), the way every walk of what runs descends; past the
/// syntax only, labels, jumps and what a type names, which childrenOnce
/// goes through.
llvm::SmallVector<const Stmt *, 4> keptChildren(const Stmt *S);

/// The arms of \p Conditional that run, the way every walk of what runs
/// goes through a conditional: of one whose condition is constant, `true ?
/// pSafe : pLocal`, only the one it chooses, as keptChildren; both otherwise.
/// Told in \p Context, or the context the checks run in (setWalkContext),
/// or, outside a check, that of a declaration the condition names.
llvm::SmallVector<const Expr *, 2>
keptArms(const AbstractConditionalOperator *Conditional,
         const ASTContext *Context = nullptr);

/// The branches of \p If that can run, the way every walk of what runs goes
/// through an `if`: of an `if constexpr` or `if consteval`, the one it keeps,
/// and of a condition that is constant, `if (false)`, the one it takes,
/// unless a jump to a label in the other can reach it, as keptChildren;
/// both otherwise. A missing `else` that can run is null. Told in \p
/// Context, or the context the checks run in (setWalkContext).
llvm::SmallVector<const Stmt *, 2>
keptBranches(const IfStmt *If, const ASTContext *Context = nullptr);

/// The context keptChildren tells constant conditions in, set where a check
/// runs on a translation unit (MalterlibCheck::run).
void setWalkContext(const ASTContext *Context);

/// Whether \p S has a statement that a jump from outside it can reach: a
/// label, or a case of a switch around it, not of a switch in it nor in a
/// lambda.
bool hasJumpTarget(const Stmt *S, bool InSwitch = false);

/// Whether \p S always leaves what follows it: a return or a jump, also as
/// the end of a block, or in each branch of an `if` that runs, like the one an
/// `if constexpr` keeps.
bool alwaysLeaves(const Stmt *S, const ASTContext &Context);

/// Whether \p Variable is declared const, or as a reference to const, and
/// defined by another declaration: a global, `extern const int gc_Value;`,
/// or a static member without its value, `static CStr const &mc_Empty;`,
/// which is a constant where its definition is constexpr. The one answer of
/// variable-naming and member-order to whether such a member is a constant.
bool isDefinedElsewhere(const VarDecl *Variable);

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

/// The call \p Callee is the callee of, past parentheses and implicit casts,
/// or null, like where the address of the function is taken.
const CallExpr *callOfCallee(const Expr *Callee, ASTContext &Context);

/// A template type parameter whose default argument clang used at a use
/// (defaultedTemplateParameters, forEachDefaultedTemplateParameter): Index its
/// place, and Parameter the parameter at that place of the template's most
/// recent declaration, which has every default of its declarations, also one
/// added after the specialization was first named, `template <typename
/// t_CActor = CActor> struct TCActor {};` after `TCActor<CActor> *g_pEarly;`,
/// and whose default is the one the use took.
struct CDefaultedParameter {
  unsigned Index = 0;
  const TemplateTypeParmDecl *Parameter = nullptr;
};

/// The template parameters whose default argument clang used where \p
/// Reference, a DeclRefExpr or MemberExpr naming a specialization of a
/// function template, `fg_Run<CTraits>`, names it, the callee of \p Call or of
/// no call: each after the template arguments it gives explicitly that no
/// parameter of the function the call gives an argument for, not its default,
/// names where clang deduces it from, a deduced context ([temp.deduct.type]),
/// or, where it isn't called, like where its address is taken, that the type
/// of the function doesn't name there. None after a template parameter pack,
/// to which explicit arguments can be given. Each has a default
/// (CDefaultedParameter) and a type argument in the specialization.
llvm::SmallVector<CDefaultedParameter, 2>
defaultedTemplateParameters(const Expr *Reference, const CallExpr *Call);

/// Calls \p Each with the specialization of a class template that \p Use, a
/// use of it as written, `TCBox<CTraits>`, makes and each template parameter
/// whose default argument clang used there (CDefaultedParameter), one it
/// leaves out, whose argument in the specialization is a type, also of what an
/// alias template stands for, `TCAlias<CTraits>` of `template <typename
/// t_CTraits> using TCAlias = TCBox<t_CTraits>;`.
void forEachDefaultedTemplateParameter(
    const TemplateSpecializationType *Use,
    llvm::function_ref<void(const ClassTemplateSpecializationDecl *,
                            const CDefaultedParameter &)>
        Each);

/// Whether clang used the default argument of the template parameter at \p
/// Index of \p Specialization, of a function or class template, at a use of it
/// in the translation unit, also in an instantiation, as
/// defaultedTemplateParameters and forEachDefaultedTemplateParameter tell.
bool usesDefaultArgument(const Decl *Specialization, unsigned Index);

/// The types of the parameters functionTypeParameterInstances gives, and
/// also of those at the place of \p Parameter in what the uses of an alias
/// template whose type it is in make of it, `TCCallback<CTraits>` of
/// `template <typename t_CTraits> using TCCallback = TCFunction<void(typename
/// t_CTraits::CBool _bReady)>;`, which has no declarations of them. With
/// \p Adjusted, the types the parameters have, `int *` of `int _Values[2]`,
/// which those of the uses of alias templates are anyway, and else the types
/// they are written with.
/// Each with what made it, which a report decided by it names: the
/// instantiation, or, of a use of an alias template, which makes none, the
/// use and the alias template.
struct CMadeParameterType {
  QualType Type;
  const Decl *Instance = nullptr;
  SourceLocation Use;
  const NamedDecl *Alias = nullptr;
};
llvm::SmallVector<CMadeParameterType, 4>
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
/// template of each, `TCOuter<int>::TCInner<int>`. Only those clang
/// instantiated from it (isInstanceOf).
llvm::SmallVector<const CXXRecordDecl *, 4>
instancesOf(const CXXRecordDecl *Pattern);

/// Whether \p Callee is a member function whose operator calls give the
/// object as their first argument: one with an implicit object, and a static
/// one, `static void operator()(...)`, whose calls give the object they are
/// called on too, not one with an explicit one, `bool operator!(this CType
/// const &_Self)`, whose object is its first parameter.
inline bool hasImplicitObject(const Decl *Callee) {
  const auto *Method = dyn_cast_or_null<CXXMethodDecl>(Callee);
  return Method && !Method->isExplicitObjectMemberFunction();
}

/// How many of the arguments of \p Call come before those the parameters of
/// \p Callee, the function it calls, are given, the one mapping of arguments
/// to parameters: 1 of an operator call whose first argument is the object
/// it is called on (hasImplicitObject), also of a static one, `static int
/// operator()(CApp *_pApp)`, else 0, also of an explicit object parameter,
/// which the first argument is given.
inline unsigned argumentShift(const Stmt *Call, const Decl *Callee) {
  return isa_and_nonnull<CXXOperatorCallExpr>(Call) && hasImplicitObject(Callee)
             ? 1
             : 0;
}

/// The receiver of a call of a member function, the object it is called on:
/// the object of a member call, `Value` of `Value.f_Get()`, or the first
/// argument of an operator call, which with an implicit object is `this` in
/// the body and no parameter is given (argumentShift), of a static operator
/// evaluated and dropped; with an explicit object parameter, the first
/// argument, which that parameter is given, `_This` of `umint
/// &operator*(this CBits &_This)`.
struct CReceiver {
  const Expr *Object = nullptr;
  /// Of an explicit object parameter, that parameter, else null.
  const ParmVarDecl *Parameter = nullptr;
  /// Of a receiver that is an argument of the call, its index.
  std::optional<unsigned> Argument;
  /// Whether the member function is static, which doesn't use it.
  bool bStatic = false;
};

/// The receiver of \p Call of \p Callee (CReceiver); none of what isn't a
/// call of a member function, also not of one through a pointer to a member
/// function.
inline CReceiver receiverOf(const CallExpr *Call, const Decl *Callee) {
  CReceiver Receiver;
  const auto *Method = dyn_cast_or_null<CXXMethodDecl>(Callee);
  if (!Call || !Method)
    return Receiver;
  Receiver.bStatic = Method->isStatic();
  if (const auto *Member = dyn_cast<CXXMemberCallExpr>(Call)) {
    Receiver.Object = Member->getImplicitObjectArgument();
    return Receiver;
  }
  // Of an explicit object, also called by its name, `fg_Get(Value)` of a
  // member function named through `&CType::f_Get`, the first argument.
  if ((isa<CXXOperatorCallExpr>(Call) ||
       Method->isExplicitObjectMemberFunction()) &&
      Call->getNumArgs() > 0) {
    Receiver.Object = Call->getArg(0);
    Receiver.Argument = 0;
    if (Method->isExplicitObjectMemberFunction() && Method->getNumParams() > 0)
      Receiver.Parameter = Method->getParamDecl(0);
  }
  return Receiver;
}

/// A call of a member function on an object, in each of the forms clang
/// makes of one: a named call with an implicit object, `Value.f_Get()` or
/// `pValue->f_Get()` (CXXMemberCallExpr), also through a pointer to a member
/// function; a named call with an explicit object, `Value.f_Get()` of `auto
/// &f_Get(this tf_CThis &&_This)`, which clang makes a plain call whose first
/// argument is the object (`*pValue` of `pValue->f_Get()`); and, where asked
/// for, an operator call of a member function, `Value[1]`, whose first
/// argument is the object (receiverOf). What it gives is of the call as
/// written: the object as it is written before `.` or `->`, the arguments
/// without the object, and the index of the parameter the first of them is
/// given, past an explicit object parameter (argumentShift). Null when the
/// statement is none of them. It stands for the CXXMemberCallExpr API the
/// checks use, `Call->getImplicitObjectArgument()`, also through `->`.
class CMemberCall {
public:
  CMemberCall() = default;

  /// \p S as a member call; with \p Operators also an operator call of a
  /// member function.
  static CMemberCall of(const Stmt *S, bool Operators = false) {
    CMemberCall Member;
    const auto *Call = dyn_cast_or_null<CallExpr>(S);
    if (!Call)
      return Member;
    if (const auto *Named = dyn_cast<CXXMemberCallExpr>(Call)) {
      Member.Call = Named;
      Member.Method = Named->getMethodDecl();
      Member.Object = Named->getImplicitObjectArgument();
      Member.Arguments =
          llvm::ArrayRef<const Expr *>(Named->getArgs(), Named->getNumArgs());
      if (const auto *Callee =
              dyn_cast<MemberExpr>(Named->getCallee()->IgnoreParens()))
        Member.bArrow = Callee->isArrow();
      return Member;
    }
    const auto *Method =
        dyn_cast_or_null<CXXMethodDecl>(Call->getDirectCallee());
    const bool Operator = isa<CXXOperatorCallExpr>(Call);
    if (!Method || Call->getNumArgs() == 0 ||
        (Operator ? !Operators : !Method->isExplicitObjectMemberFunction()))
      return Member;
    Member.Call = Call;
    Member.Method = Method;
    Member.Object = Call->getArg(0);
    Member.Arguments =
        llvm::ArrayRef<const Expr *>(Call->getArgs(), Call->getNumArgs())
            .drop_front();
    Member.bOperator = Operator;
    Member.iFirstParameter =
        Method->isExplicitObjectMemberFunction() ? 1 : 0;
    // `pValue->f_Get()` with an explicit object gives it `*pValue`, which
    // isn't written.
    if (const auto *Deref =
            dyn_cast<UnaryOperator>(Member.Object->IgnoreImplicit());
        !Operator && Deref && Deref->getOpcode() == UO_Deref &&
        Deref->getOperatorLoc().isInvalid()) {
      Member.Object = Deref->getSubExpr();
      Member.bArrow = true;
    }
    return Member;
  }

  operator const CallExpr *() const { return Call; }
  const CMemberCall *operator->() const { return this; }

  const CallExpr *call() const { return Call; }
  const CXXMethodDecl *getMethodDecl() const { return Method; }
  const FunctionDecl *getDirectCallee() const {
    return Call->getDirectCallee();
  }
  /// The object as it is written, the pointer of `pValue->f_Get()`.
  const Expr *getImplicitObjectArgument() const { return Object; }
  bool isArrow() const { return bArrow; }
  bool isExplicitObject() const { return iFirstParameter == 1; }
  /// Whether the object is copied into the call, of an explicit object
  /// parameter taken by value, `this CMask _This`, whose writes are to the
  /// copy, not to the object it is called on; bound by reference otherwise.
  bool isObjectCopied() const {
    return isExplicitObject() && Method && Method->getNumParams() > 0 &&
           !Method->getParamDecl(0)->getType()->isReferenceType();
  }
  bool isOperator() const { return bOperator; }
  /// The index of the parameter that the first of the arguments is given.
  unsigned firstParameter() const { return iFirstParameter; }
  /// The member it names, `f_Get` of `Value.CBase::f_Get()`, of a named call
  /// with an implicit object, else null.
  const MemberExpr *memberExpr() const {
    return isa<CXXMemberCallExpr>(Call)
               ? dyn_cast<MemberExpr>(Call->getCallee()->IgnoreParens())
               : nullptr;
  }
  /// Whether the member function is named with a qualifier,
  /// `Value.CBase::f_Get()`, which calls it, not an override.
  bool hasQualifier() const {
    if (const MemberExpr *Member = memberExpr())
      return Member->hasQualifier();
    const auto *Named = dyn_cast<DeclRefExpr>(Call->getCallee()->IgnoreImpCasts());
    return Named && Named->hasQualifier();
  }
  /// The class of the object, of the member function.
  const CXXRecordDecl *getRecordDecl() const {
    if (const auto *Named = dyn_cast<CXXMemberCallExpr>(Call))
      return Named->getRecordDecl();
    return Method ? Method->getParent() : nullptr;
  }
  const Expr *getCallee() const { return Call->getCallee(); }
  /// What \p Parameter of the member function is given, the object of an
  /// explicit object parameter as it is given, `*pValue` of
  /// `pValue->f_Get()`; null of a parameter past the arguments.
  const Expr *parameterArgument(const ParmVarDecl *Parameter) const {
    const unsigned iParameter = Parameter->getFunctionScopeIndex();
    if (iParameter < iFirstParameter)
      return Call->getArg(0);
    const unsigned iArgument = iParameter - iFirstParameter;
    return iArgument < Arguments.size() ? Arguments[iArgument] : nullptr;
  }
  unsigned getNumArgs() const { return Arguments.size(); }
  const Expr *getArg(unsigned iArgument) const {
    return Arguments[iArgument];
  }
  llvm::ArrayRef<const Expr *> arguments() const { return Arguments; }
  const Expr *const *getArgs() const { return Arguments.data(); }
  QualType getType() const { return Call->getType(); }
  bool isGLValue() const { return Call->isGLValue(); }
  bool isPRValue() const { return Call->isPRValue(); }
  bool isLValue() const { return Call->isLValue(); }
  bool isTypeDependent() const { return Call->isTypeDependent(); }
  SourceLocation getBeginLoc() const { return Call->getBeginLoc(); }
  SourceLocation getEndLoc() const { return Call->getEndLoc(); }
  SourceLocation getExprLoc() const { return Call->getExprLoc(); }
  SourceRange getSourceRange() const { return Call->getSourceRange(); }
  const Expr *IgnoreParenImpCasts() const { return Call->IgnoreParenImpCasts(); }

private:
  const CallExpr *Call = nullptr;
  const CXXMethodDecl *Method = nullptr;
  const Expr *Object = nullptr;
  llvm::ArrayRef<const Expr *> Arguments;
  unsigned iFirstParameter = 0;
  bool bArrow = false;
  bool bOperator = false;
};

/// The explicit object parameter of \p Function, `_This` of `auto
/// &f_Get(this tf_CThis &&_This)`, else null.
inline const ParmVarDecl *explicitObjectOf(const FunctionDecl *Function) {
  const auto *Method = dyn_cast_or_null<CXXMethodDecl>(Function);
  return Method && Method->isExplicitObjectMemberFunction() &&
                 Method->getNumParams() > 0
             ? Method->getParamDecl(0)
             : nullptr;
}

/// Whether \p Member is a member of the object of \p Function, the member
/// function whose body it is in: `this->m_Value` or `m_Value`, `(*this).m_Value`,
/// or with an explicit object, `_This.m_Value`.
inline bool isOwnMember(const MemberExpr *Member, const FunctionDecl *Function) {
  const Expr *Base = Member->getBase()->IgnoreParenImpCasts();
  if (Member->isArrow())
    return isa<CXXThisExpr>(Base);
  if (const auto *Deref = dyn_cast<UnaryOperator>(Base);
      Deref && Deref->getOpcode() == UO_Deref)
    return isa<CXXThisExpr>(Deref->getSubExpr()->IgnoreParenImpCasts());
  const auto *Named = dyn_cast<DeclRefExpr>(Base);
  return Named && Named->getDecl() == explicitObjectOf(Function);
}

/// Whether \p E is the object of \p Function, the member function whose
/// body it is in, as members of it are reached from: `this`, or an explicit
/// object parameter, `_This` of `_This.m_Value`.
inline bool isOwnObject(const Expr *E, const FunctionDecl *Function) {
  E = E->IgnoreParenImpCasts();
  if (isa<CXXThisExpr>(E))
    return true;
  const auto *Named = dyn_cast<DeclRefExpr>(E);
  return Named && Named->getDecl() == explicitObjectOf(Function);
}

/// What storage an expression designates is in (designatedRoot).
enum class ERoot {
  /// Nothing it can be followed to: a temporary, what a call gives back, a
  /// value that isn't a pointer.
  None,
  /// The object of the member function it is in: `this`, `*this`, or an
  /// explicit object parameter by reference.
  Object,
  /// A local variable.
  Local,
  /// A variable with static or thread storage, also a static data member.
  Global,
  /// A parameter.
  Parameter,
};

/// The root of the storage an expression designates (designatedRoot).
struct CDesignatedRoot {
  ERoot Root = ERoot::None;
  /// The variable of a Local, Global or Parameter root.
  const ValueDecl *Variable = nullptr;
  /// Whether the way from the root goes through a pointer, a reference member
  /// or a reference parameter, `m_pState->m_Value`, so that what it
  /// designates isn't in the root's own storage.
  bool bThrough = false;

  /// Whether it is the object's own storage: the object, or what is in it by
  /// value, at any depth.
  bool isOwn() const { return Root == ERoot::Object && !bThrough; }
};

/// The root of the storage \p E designates, a glvalue, or that \p E points
/// to, a pointer: the object of \p Function, the member function it is in,
/// `*this` or `this`, a variable, or none, and whether the way there goes
/// through a pointer or a reference. A member by value, of a base or at any
/// depth, `m_State.m_Value`, an element of an array, `m_Bytes[1]` or
/// `m_Bytes + 1`, a base, what a local reference is bound to, and either
/// value of a conditional that can run, when both have one root, are of the
/// root they are in; what is read from a pointer, `m_pState->m_Value` or
/// `*m_pState`, is through it. What it can't follow, a call, a temporary, a
/// cast that makes another object, is none. One classification of every
/// expression class.
CDesignatedRoot designatedRoot(const Expr *E, const FunctionDecl *Function);

/// \p S as a member call (CMemberCall::of).
inline CMemberCall memberCallOf(const Stmt *S, bool Operators = false) {
  return CMemberCall::of(S, Operators);
}

/// Whether \p S is a named member call (CMemberCall), also with an explicit
/// object.
inline bool isMemberCall(const Stmt *S) {
  return static_cast<const CallExpr *>(CMemberCall::of(S)) != nullptr;
}

/// The member call that \p Object, a child of \p Parent, is the object of,
/// the one notion of a receiver for walks that go up from an object: through
/// the member function it names, `Object.f_Get()` or `pObject->f_Get()`
/// (\p Parent the MemberExpr), or as the first argument of a call with an
/// explicit object, past the implicit nodes clang puts between, the
/// conversion to `const &` and the `*pObject` of `pObject->f_Get()`
/// (\p Parent the first of them or the call); with \p Operators also of a
/// member operator. Null when it isn't one.
inline CMemberCall receiverCallOf(DynTypedNode Parent, const Expr *Object,
                                  ASTContext &Context,
                                  bool Operators = false) {
  if (!Object)
    return CMemberCall();
  // Past parentheses and every implicit node, temporaries too.
  auto Strip = [](const Expr *E) {
    for (const Expr *Was = nullptr; E != Was;) {
      Was = E;
      E = E->IgnoreImplicit()->IgnoreParens();
    }
    return E;
  };
  const Expr *Bare = Strip(Object);
  if (const auto *Member = Parent.get<MemberExpr>()) {
    if (!isa<CXXMethodDecl>(Member->getMemberDecl()) ||
        Strip(Member->getBase()) != Bare)
      return CMemberCall();
    const auto Above = instantiatedParents(*Member, Context);
    const CMemberCall Call =
        Above.empty() ? CMemberCall() : memberCallOf(Above[0].get<Stmt>());
    return Call && Strip(Call->getImplicitObjectArgument()) == Bare
               ? Call
               : CMemberCall();
  }
  for (int Depth = 0; Depth < 8; ++Depth) {
    if (const CMemberCall Call = memberCallOf(Parent.get<Stmt>(), Operators);
        Call && (Call->isExplicitObject() || Call->isOperator()) &&
        Strip(Call->getImplicitObjectArgument()) == Bare)
      return Call;
    const auto *Implicit = Parent.get<Expr>();
    const auto *Deref = dyn_cast_or_null<UnaryOperator>(Implicit);
    if (!Implicit ||
        !(isa<ImplicitCastExpr, MaterializeTemporaryExpr, ParenExpr,
              CXXBindTemporaryExpr>(Implicit) ||
          (Deref && Deref->getOpcode() == UO_Deref &&
           Deref->getOperatorLoc().isInvalid())))
      return CMemberCall();
    const auto Above = instantiatedParents(Parent, Context);
    if (Above.empty())
      return CMemberCall();
    Parent = Above[0];
  }
  return CMemberCall();
}

/// Matches a named member call (CMemberCall), also with an explicit object,
/// the matcher every member call is matched with instead of
/// cxxMemberCallExpr.
AST_MATCHER(CallExpr, isMemberCallExpr) { return isMemberCall(&Node); }

/// Matches a named member call (CMemberCall) whose object, past parentheses
/// and implicit casts, matches \p InnerMatcher, like `on` of
/// cxxMemberCallExpr.
AST_MATCHER_P(CallExpr, onMemberObject, ast_matchers::internal::Matcher<Expr>,
              InnerMatcher) {
  const CMemberCall Member = memberCallOf(&Node);
  const Expr *Object =
      Member ? Member->getImplicitObjectArgument() : nullptr;
  return Object &&
         InnerMatcher.matches(*Object->IgnoreParenImpCasts(), Finder, Builder);
}

/// Whether \p S is a call of a member function in any form, or an operator
/// call, also of a free operator: what isn't a call of a free function by
/// name.
inline bool isMemberOrOperatorCall(const Stmt *S) {
  return isa_and_nonnull<CXXOperatorCallExpr>(S) || isMemberCall(S);
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

/// The class whose constructor or destructor \p Call, a call of the member
/// function \p Method on the object it runs for, `f_Clear()` or
/// `(*this).f_Clear()`, is in, in its body or a member initializer, also a
/// default member initializer: while it runs, the object is of that class,
/// whatever class it is made as, so the call runs that class's final
/// overrider. Not in a closure, which can be kept and run after it. Null
/// otherwise.
inline const CXXRecordDecl *constructingClassOf(const CallExpr *Call,
                                                const CXXMethodDecl *Method) {
  // Each call once, by the walk up to the function it is in.
  struct CConstructing {
    llvm::DenseMap<const CallExpr *, const CXXRecordDecl *> Of;
  };
  ASTContext &Context = Method->getASTContext();
  auto &Cache = astCache<CConstructing>(Context).Of;
  if (const auto It = Cache.find(Call); It != Cache.end())
    return It->second;
  const CXXRecordDecl *Class = nullptr;
  DynTypedNode Node = DynTypedNode::create(*Call);
  for (int Depth = 0; Depth < 4096; ++Depth) {
    const auto Parents = instantiatedParents(Node, Context);
    if (Parents.empty())
      break;
    Node = Parents[0];
    if (Node.get<LambdaExpr>() || Node.get<BlockExpr>())
      break;
    if (const auto *Function = Node.get<FunctionDecl>()) {
      if (isa<CXXConstructorDecl, CXXDestructorDecl>(Function))
        Class = cast<CXXMethodDecl>(Function)->getParent();
      break;
    }
    if (const auto *Field = Node.get<FieldDecl>()) {
      Class = dyn_cast<CXXRecordDecl>(Field->getParent());
      break;
    }
  }
  if (Class)
    Class = Class->getDefinition();
  Cache[Call] = Class;
  return Class;
}

/// The class of the object \p Call, a call of a member function, is on when
/// that is exactly what it is, which no class derived from it can be: a
/// variable or a member that isn't a reference, `Value.f_Get()`, also a
/// member operator's object, `mp_Items()`, an object of a final class, also
/// through a pointer or reference, and the object a constructor or a
/// destructor runs for, in it (constructingClassOf). Null otherwise.
inline const CXXRecordDecl *exactReceiverClass(const CallExpr *Call) {
  auto Defined = [](const CXXRecordDecl *Record) -> const CXXRecordDecl * {
    return Record && Record->hasDefinition() ? Record->getDefinition()
                                             : nullptr;
  };
  if (const auto *MemberCall = dyn_cast<CXXMemberCallExpr>(Call))
    if (const CXXRecordDecl *Object = Defined(MemberCall->getRecordDecl());
        Object && Object->hasAttr<FinalAttr>())
      return Object;
  const auto *Method = dyn_cast_or_null<CXXMethodDecl>(Call->getDirectCallee());
  const auto *Member =
      dyn_cast<MemberExpr>(Call->getCallee()->IgnoreParenImpCasts());
  const Expr *Receiver = nullptr;
  if (Member)
    Receiver = Member->getBase();
  else if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(Call);
           Operator && Operator->getNumArgs() >= 1 && Method &&
           !Method->isExplicitObjectMemberFunction())
    Receiver = Operator->getArg(0);
  if (!Receiver)
    return nullptr;
  const Expr *Object = Receiver->IgnoreParenImpCasts();
  // The object a constructor or a destructor runs for, `this->` or
  // `(*this).`, in it (constructingClassOf).
  const auto *Dereference = dyn_cast<UnaryOperator>(Object);
  if (Method && Method->isVirtual() &&
      ((Member && Member->isArrow() && isa<CXXThisExpr>(Object)) ||
       (!(Member && Member->isArrow()) && Dereference &&
        Dereference->getOpcode() == UO_Deref &&
        isa<CXXThisExpr>(Dereference->getSubExpr()))))
    if (const CXXRecordDecl *Constructing = constructingClassOf(Call, Method))
      return Constructing;
  if (Member && Member->isArrow())
    return nullptr;
  const CXXRecordDecl *Record = Defined(Object->getType()->getAsCXXRecordDecl());
  if (!Record)
    return nullptr;
  if (Record->hasAttr<FinalAttr>())
    return Record;
  const ValueDecl *Named = nullptr;
  if (const auto *Ref = dyn_cast<DeclRefExpr>(Object))
    Named = dyn_cast<VarDecl>(Ref->getDecl());
  else if (const auto *Field = dyn_cast<MemberExpr>(Object))
    Named = dyn_cast<FieldDecl>(Field->getMemberDecl());
  return Named && !Named->getType()->isReferenceType() ? Record : nullptr;
}

/// Whether \p Call can run an override of what it names, whose body isn't
/// the one seen: of a virtual member function that isn't final, not called
/// by its name, `Reset.CReset::f_Reset(...)`, nor on an object whose class
/// is exactly what it is (exactReceiverClass), whose final overrider of it
/// is the one it names. Of one named through a `using` of a base's, `using
/// CBase::f_Selected;`, whose class or a base between overrides it, the
/// override runs, not the body seen.
inline bool callsOverride(const CallExpr *Call) {
  const auto *Method = dyn_cast_or_null<CXXMethodDecl>(Call->getDirectCallee());
  if (!Method || !Method->isVirtual())
    return false;
  const auto *Member =
      dyn_cast<MemberExpr>(Call->getCallee()->IgnoreParenImpCasts());
  if (Member && Member->hasQualifier())
    return false;
  if (Method->hasAttr<FinalAttr>() || Method->getParent()->hasAttr<FinalAttr>())
    return false;
  if (const CXXRecordDecl *Exact = exactReceiverClass(Call)) {
    const CXXMethodDecl *Final = Method->getCorrespondingMethodInClass(Exact);
    return !(Final && Final->getCanonicalDecl() == Method->getCanonicalDecl());
  }
  return true;
}

/// The definition whose body \p Call runs, when that can be trusted: of its
/// callee, unless an override can run instead (callsOverride). Null when it
/// has none with a body, or what runs can't be told, which is then as one
/// that can't be seen.
inline const FunctionDecl *calledDefinition(const CallExpr *Call) {
  const FunctionDecl *Callee = Call->getDirectCallee();
  if (!Callee || callsOverride(Call))
    return nullptr;
  const FunctionDecl *Definition = Callee->getDefinition();
  return Definition && Definition->getBody() ? Definition : nullptr;
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
