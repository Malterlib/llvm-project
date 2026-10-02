//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_CHECKEDACCESS_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_CHECKEDACCESS_H

// What clang asserts before it answers, asked where it holds: each accessor
// of clang that asserts a precondition the code it analyzes can break (valid
// code: a wide literal, an index past 64 bits, a dependent capture) is
// called here, which answers "can't tell" (nullopt, null) where it doesn't
// hold. lbt/accessor-check.sh lists a direct call elsewhere.
//
// An operation on two APInt or APSInt values asserts that both have the same
// width (and signedness), which two constants of the analyzed code needn't
// have: `0` and `0ULL`. Two constants are compared by their numbers, with
// llvm::APSInt::isSameValue and compareValues, and their bits are combined
// here. lbt/apint-check.sh lists such an operation elsewhere.
//
// A number taken from the analyzed code (an index, an offset, a size) can be
// any 64-bit one, `p[INT64_MAX]`: arithmetic on it is checked here, and
// answers none where the result doesn't fit. lbt/overflow-check.sh lists
// unchecked arithmetic on such a number elsewhere.
//
// Clang's constant evaluator runs what the analyzed code runs: a constexpr
// constructor that loops when it is constant evaluated runs up to
// -fconstexpr-steps, without end with 0. A check never evaluates what clang
// didn't: it reads the value clang stored, or folds an expression with
// nothing to run, here, or can't tell. lbt/evaluate-check.sh lists a use of
// the evaluator elsewhere.

#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/NestedNameSpecifier.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/TemplateBase.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/SmallVector.h"
#include <optional>

namespace clang::tidy::malterlib {

/// \p Value as a signed 64-bit number, when it is one.
std::optional<int64_t> signedValueOf(const llvm::APSInt &Value);

/// \p Value as an index, a number from 0 that fits 64 bits.
std::optional<uint64_t> indexValueOf(const llvm::APSInt &Value);

/// Whether \p Left and \p Right have a bit set in common, each extended by
/// its signedness to the width of the wider.
bool haveBitInCommon(const llvm::APSInt &Left, const llvm::APSInt &Right);

/// The bits set in \p Left or \p Right, each zero-extended to the width of the
/// wider.
llvm::APInt unionOfBits(const llvm::APInt &Left, const llvm::APInt &Right);

/// The bits set in both \p Left and \p Right, each zero-extended to the width
/// of the wider.
llvm::APInt commonBitsOf(const llvm::APInt &Left, const llvm::APInt &Right);

/// The value of \p Literal, signed as its type is: Microsoft's `0xFFi8` is -1
/// where `char` is signed.
llvm::APSInt integerLiteralValueOf(const IntegerLiteral *Literal);

/// \p Left + \p Right, when it fits 64 bits.
std::optional<int64_t> checkedAdd(int64_t Left, int64_t Right);

/// \p Left - \p Right, when it fits 64 bits.
std::optional<int64_t> checkedSub(int64_t Left, int64_t Right);

/// -\p Value, when it fits 64 bits: not of INT64_MIN.
std::optional<int64_t> checkedNegate(int64_t Value);

/// The text of \p E, a string literal of narrow characters (ordinary or
/// UTF-8), past parentheses and implicit casts; none of another one, wide or
/// UTF-16/32, whose bytes aren't its characters.
std::optional<StringRef> literalTextOf(const Expr *E);

/// What initializes the capture \p Capture of \p Lambda, of a variable or
/// `this`; null when it has none, or in a template, where it has no type.
const Expr *captureInitOf(const LambdaExpr *Lambda,
                          const LambdaCapture &Capture);
/// That of the capture at \p Index.
const Expr *captureInitAt(const LambdaExpr *Lambda, size_t Index);

/// The initializers of the captures of \p Lambda, in their order, each as
/// captureInitAt gives it.
llvm::SmallVector<const Expr *, 4> captureInitsOf(const LambdaExpr *Lambda);

/// The variable \p Capture captures, by copy or by reference, or that it
/// declares, `[Copy = Value]`; null for `this`, `*this`, the bound of a
/// variable-length array and a structured binding, `[First]` of `auto [First,
/// Second]`, whose captured declaration is a BindingDecl.
const VarDecl *capturedVariableOf(const LambdaCapture &Capture);

/// The initializer of \p Variable; null for none and for one without a
/// type, the ParenListExpr of a parenthesized initializer in a template, `T
/// Value(1, 2)`, whose type can't be read.
const Expr *initializerOf(const VarDecl *Variable);
/// That of the member or base \p Initializer of a constructor, the same way:
/// null for `m_Value(1, 2)` of a member of a type that depends on a template
/// parameter.
const Expr *initializerOf(const CXXCtorInitializer *Initializer);

/// Whether \p E can be evaluated: not value-dependent and without errors.
bool isEvaluable(const Expr *E);

/// The integer \p E is a constant of, as clang folds it, when it can be
/// evaluated (isEvaluable) and is written with nothing to run (runsNothing).
std::optional<llvm::APSInt> constantIntegerOf(const Expr *E,
                                              const ASTContext &Context);

/// The integer constant expression \p E is, when it can be evaluated
/// (isEvaluable) and is written with nothing to run (runsNothing).
std::optional<llvm::APSInt> integerConstantExprOf(const Expr *E,
                                                  const ASTContext &Context);

/// The value \p E is a constant of, of any type, when it can be evaluated
/// (isEvaluable) and is written with nothing to run (runsNothing): also a
/// constant clang stored the value of, `c_One` of `constexpr CInt c_One(1)`,
/// and a construction by a constructor that runs nothing, `CInt(1)` of
/// `constexpr CInt(umint _Value) : m_Value(_Value) {}`; none otherwise, of a
/// call or a constructor that runs something, which clang never evaluated
/// where it is.
std::optional<APValue> foldedValueOf(const Expr *E, const ASTContext &Context);

/// Whether \p E is a null pointer constant, as Expr::isNullPointerConstant
/// tells: in C, before C++11 and with Microsoft's rules it evaluates an
/// integer constant expression, and an expression with something to run is
/// none here.
Expr::NullPointerConstantKind
nullPointerConstantOf(const Expr *E, ASTContext &Context,
                      Expr::NullPointerConstantValueDependence Dependence);

/// The branch of the `if constexpr` \p If that isn't discarded, by the value
/// clang stored of its condition; none for another `if`, one whose condition
/// depends on template parameters and one without a stored value.
std::optional<const Stmt *> nondiscardedCaseOf(const IfStmt *If);
/// That of a mutable \p If.
std::optional<Stmt *> nondiscardedCaseOf(IfStmt *If);

/// What \p Type points or refers to, of a pointer, a reference, a member
/// pointer or a block pointer; null otherwise.
QualType pointeeOf(QualType Type);

/// Whether what \p Type points or refers to (pointeeOf) is const; not when
/// it doesn't point, like the type of `&Value` in a template.
bool pointsToConst(QualType Type);

/// The type \p Argument is, when it is a type; null otherwise.
QualType typeArgumentOf(const TemplateArgument &Argument);

/// The type the qualifier \p Qualifier names, `TCOuter<t_CType>::` of
/// `TCOuter<t_CType>::CInner`; null for one that names no type: none, `::`, a
/// namespace, or Microsoft's `__super`, which names a base the class can't
/// tell.
const clang::Type *qualifierTypeOf(NestedNameSpecifier Qualifier);

/// The bases of \p Record, of its definition; none for a class without one
/// or none.
llvm::ArrayRef<CXXBaseSpecifier> basesOf(const CXXRecordDecl *Record);

/// The text of the identifier \p Declaration is named by; empty for a name
/// that isn't an identifier (an operator, a conversion function, a
/// constructor, a destructor, a literal operator, a deduction guide) and for
/// none.
StringRef identifierTextOf(const NamedDecl *Declaration);

/// Whether the constant condition \p E holds, when clang decided it or it is
/// written with nothing to run (runsNothing); none otherwise, also for one
/// that calls a function clang never evaluated it with.
std::optional<bool> foldedConditionOf(const Expr *E, const ASTContext &Context);
/// Whether \p E folds as foldedConditionOf tells, with whether it holds in
/// \p Holds when it does.
bool foldsCondition(const Expr *E, bool &Holds, const ASTContext &Context);

/// The integer constant expression \p E is, when clang stored its value or it
/// is written with nothing to run (runsNothing); none otherwise.
std::optional<llvm::APSInt> foldedIntegerOf(const Expr *E,
                                            const ASTContext &Context);

/// The value clang stored of the initializer of \p Variable, or else
/// foldedIntegerOf of its initializer; none otherwise.
std::optional<llvm::APSInt> constantValueOf(const VarDecl *Variable,
                                            const ASTContext &Context);

/// The value of the template argument \p Argument: the one clang converted it
/// to, or of an expression, foldedIntegerOf; none otherwise.
std::optional<llvm::APSInt>
templateArgumentValueOf(const TemplateArgument &Argument,
                        const ASTContext &Context);

} // namespace clang::tidy::malterlib

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_CHECKEDACCESS_H
