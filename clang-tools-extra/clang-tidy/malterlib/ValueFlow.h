//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_VALUEFLOW_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_VALUEFLOW_H

#include "ExprEffects.h"
#include "Summaries.h"
#include "clang/AST/ASTContext.h"
#include "clang/Analysis/CFG.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallVector.h"
#include <memory>
#include <optional>
#include <vector>

namespace clang::tidy::malterlib {

class CLocalFlow;
struct CFlowSummaries;

/// What an element of a CFG is to the flow (eventOf).
enum class EEvent {
  /// A statement is evaluated, Statement, with its operands evaluated before
  /// it, each an element of its own.
  Statement,
  /// A constructor initializes a base or a member, Initializer.
  Initializer,
  /// The allocation function of `new`, Statement, is called.
  NewAllocator,
  /// A loop, Statement, is left.
  LoopExit,
  /// The temporaries of a full expression, Statement, end, Temporaries.
  FullExpressionCleanup,
  /// The scope of Variable starts.
  ScopeBegin,
  /// The scope of Variable ends.
  ScopeEnd,
  /// The life of Variable ends.
  LifetimeEnds,
  /// Variable is destroyed where its scope ends.
  AutomaticDestructor,
  /// What `delete`, Statement, deletes is destroyed.
  DeleteDestructor,
  /// A base, Base, of the object is destroyed.
  BaseDestructor,
  /// A member, Field, of the object is destroyed.
  MemberDestructor,
  /// A temporary, Statement, is destroyed.
  TemporaryDestructor,
  /// The cleanup function of Variable is called.
  CleanupFunction,
};

/// One element of a CFG, as the flow sees it.
struct CFlowEvent {
  EEvent Kind = EEvent::Statement;
  const Stmt *Statement = nullptr;
  const VarDecl *Variable = nullptr;
  const CXXCtorInitializer *Initializer = nullptr;
  const CXXBaseSpecifier *Base = nullptr;
  const FieldDecl *Field = nullptr;
  llvm::ArrayRef<const MaterializeTemporaryExpr *> Temporaries;
};

/// What \p Element is to the flow, a switch over every kind of CFG element.
CFlowEvent eventOf(const CFGElement &Element);

/// The name of \p Kind, for what tests show.
llvm::StringRef eventName(EEvent Kind);

/// The CFG the flow is told by, of \p Body of \p Owner, a function, or an
/// initializer of a variable or a member outside one: each expression an
/// element, with the ends of lifetimes, destructors of temporaries,
/// initializers and allocations, and edges to the handlers of each `try`
/// (addExceptionEdges). Null of templated code, or when clang can't build
/// it (buildGraph).
std::unique_ptr<CFG> buildFlowGraph(const Decl *Owner, const Stmt *Body,
                                    ASTContext &Context);

/// What the statements under a root do with their operands (effectsOf), by
/// operand: each statement is the operand of what it is in. Not into the
/// body of a lambda or a block, which is a function of its own.
class CEffectIndex {
public:
  /// One use of an operand: Parent, the statement it is an operand of, and
  /// what that does with it, of the effects effectsOf keeps for the
  /// translation unit.
  struct CUse {
    const Stmt *Parent = nullptr;
    const COperandEffect *Effect = nullptr;
  };

  CEffectIndex(const Stmt *Root, const ASTContext &Context);

  /// What the statements \p Operand is an operand of do with it.
  llvm::ArrayRef<CUse> usesOf(const Stmt *Operand) const;

  /// What is done with the value of \p E: the uses of it, past what only
  /// gives it on (EUse::Forward), of what gives it on, also several, of a
  /// conditional, or a node that is in two places, like the operand of an
  /// awaiter.
  llvm::SmallVector<CUse, 2> consumersOf(const Stmt *E) const;

  /// Each statement under the root, the root first, then each before its
  /// operands, in the order of the operands.
  llvm::ArrayRef<const Stmt *> statements() const { return Statements; }

  /// The effects of \p S, as effectsOf tells.
  const CEffects *effectsOf(const Stmt *S) const;

  /// The children of \p S that its effects leave out: none, unless a class
  /// is classified wrongly.
  llvm::SmallVector<const Stmt *, 2> unclassifiedChildren(const Stmt *S) const;

  /// The statements a default argument or a default member initializer, \p
  /// Default (EStatement::Default), evaluates where it is used, in the order
  /// they run, each after its operands, by their sequence (effectsOf), not
  /// what isn't evaluated; the CFG has only the default itself.
  llvm::SmallVector<const Stmt *, 8> evaluationOf(const Stmt *Default) const;

  /// Whether \p S is evaluated: it isn't in an operand that isn't
  /// (EUse::Unevaluated), as each statement it is in tells.
  bool evaluated(const Stmt *S) const;

  /// Where to report what \p S does: where the default argument or default
  /// member initializer it is evaluated for, or that it gives on, is used,
  /// else its own location, else that of the first statement it is in that
  /// has one; never invalid when one of those has one.
  SourceLocation locationOf(const Stmt *S) const;

private:
  void evaluate(const Stmt *S, llvm::SmallVectorImpl<const Stmt *> &Order,
                llvm::SmallPtrSetImpl<const Stmt *> &Seen, int Depth) const;

  const Stmt *Root = nullptr;
  llvm::DenseMap<const Stmt *, llvm::SmallVector<CUse, 1>> Uses;
  // The operands that aren't walked into, which aren't under the root: the
  // body of a lambda, the operands of a block.
  llvm::DenseSet<const Stmt *> Unindexed;
  // The evaluation of each default argument or default member initializer
  // expression, which every use of it shares (evaluationOf).
  mutable llvm::DenseMap<const Stmt *, llvm::SmallVector<const Stmt *, 8>>
      Evaluations;
  mutable llvm::DenseMap<const Stmt *, bool> Evaluated;
  llvm::SmallVector<const Stmt *, 64> Statements;
};

/// What one use of a closure value is to whether the closure stays local
/// (closureUseOf): ends with what makes it, unless what it is given to keeps
/// it.
enum class EClosureUse {
  /// It is called: by its call operator, also named or with it as the
  /// explicit object, as a function, or by `std::invoke`.
  Call,
  /// It is looked at, not kept: a conversion to `bool`, a presence check,
  /// `f_IsEmpty()`, a comparison, a condition, its value dropped, or an
  /// operand of `sizeof` or `decltype`; or, of a function object it is in,
  /// that object given another callable.
  Inspect,
  /// It is in what its uses are its own: a local or a temporary it is, a
  /// local reference to it, a local or temporary function object of
  /// SynchronousFunctionTypes made of it or assigned it, what only gives it
  /// back, `fg_Move` or `fg_Forward` (givesItsArgument), or a load of it.
  Alias,
  /// It is given to a parameter of a function that only calls it
  /// (onlyCallsParameter).
  Pass,
  /// It is captured by another closure, whose uses of the capture in its
  /// body are its own, and which stays local itself.
  Capture,
  /// Anything else, which can keep it or call it anywhere: a copy of a
  /// closure, a return, what is stored, given on or can't be told.
  Escape,
};

/// One use of a closure value, in a body.
struct CClosureSite {
  EClosureUse Kind = EClosureUse::Escape;
  const Decl *Owner = nullptr;
  const Stmt *Body = nullptr;
  CEffectIndex::CUse Use;
  /// Of a call of a closure that captures it, which runs it, that closure.
  const LambdaExpr *Through = nullptr;
  bool operator==(const CClosureSite &Other) const {
    return Kind == Other.Kind && Owner == Other.Owner && Body == Other.Body &&
           Use.Parent == Other.Use.Parent &&
           (Use.Effect == Other.Use.Effect ||
            (Use.Effect && Other.Use.Effect &&
             Use.Effect->Operand == Other.Use.Effect->Operand &&
             Use.Effect->Use == Other.Use.Effect->Use)) &&
           Through == Other.Through;
  }
};

/// A local a closure value is in (CClosureUses::Locals).
struct CClosureLocal {
  /// The function or closure it is a local of.
  const Decl *Owner = nullptr;
  const VarDecl *Variable = nullptr;
  /// Of a function object of SynchronousFunctionTypes whose values are told
  /// (CWrapperFlow), the definition that gives it the value, which it holds
  /// until another definition of it; null when it holds the value as long as
  /// it lives.
  const Stmt *Given = nullptr;
  bool operator==(const CClosureLocal &Other) const {
    return Owner == Other.Owner && Variable == Other.Variable &&
           Given == Other.Given;
  }
};

/// What the uses of a closure value are, each classified (EClosureUse),
/// through what it is an alias of and the closures that capture it
/// (CFlowCache::closureUses).
struct CClosureUses {
  /// Whether it stays local: no use of it escapes, also of an alias of it
  /// or in the body of a closure that captures it.
  bool bLocal = true;
  /// The uses that call it or give it to what only calls it (Call, Pass),
  /// also those of the closures that capture it, which run it.
  llvm::SmallVector<CClosureSite, 4> Calls;
  /// The locals it is in.
  llvm::SmallVector<CClosureLocal, 2> Locals;
  /// The closures that capture it.
  llvm::SmallVector<const LambdaExpr *, 2> Closures;
  /// The uses that escape.
  llvm::SmallVector<CClosureSite, 2> Escapes;
  /// Whether a function object it is in can hold another value where a use
  /// of it is, where a call of it may run another callable: another
  /// definition of the object can reach there too, or its values aren't told
  /// and it is given another or cleared.
  bool bReplaced = false;
  bool operator==(const CClosureUses &Other) const {
    return bLocal == Other.bLocal && Calls == Other.Calls &&
           Locals == Other.Locals && Closures == Other.Closures &&
           Escapes == Other.Escapes && bReplaced == Other.bReplaced;
  }
};

/// The order the flows over a CFG visit its blocks in, and the `try` of each
/// dispatch to handlers (CFlowCache::orderOf).
struct CFlowOrder {
  /// The blocks that can be reached, each before those it reaches, but loops
  /// (forwardOrder), then those only an edge the order doesn't follow
  /// reaches.
  llvm::SmallVector<const CFGBlock *, 16> Order;
  /// Of each block, for each dispatch to handlers after it, the elements in
  /// that dispatch's `try` (inDispatchTry).
  std::vector<llvm::SmallVector<std::pair<unsigned, llvm::BitVector>, 1>> InTry;
};

/// Where the statements of a CFG are and which blocks a way from each leads
/// to (CFlowCache::positionsOf), for the questions of order between two
/// elements (canRunAfter).
struct CFlowPositions {
  /// Of each statement an element of a block that can be reached is, where
  /// each such element is, by block and index, in the order of the blocks.
  llvm::DenseMap<const Stmt *, llvm::SmallVector<std::pair<unsigned, unsigned>, 1>>
      Of;
  /// Of each block asked, the blocks a way of one edge or more from its end
  /// leads to through blocks that can be reached (CFlowCache::reachedFrom).
  llvm::DenseMap<unsigned, llvm::BitVector> Reached;
};

/// The values a local function object of SynchronousFunctionTypes holds
/// where it is named, a slot whose values are what its definitions give it
/// (CFlowCache::wrapperFlow).
struct CWrapperFlow {
  /// A definition: its declaration, an assignment to it (`operator =`), or a
  /// clear of it, each of which ends the value it held.
  struct CDefinition {
    const Stmt *Statement = nullptr;
    unsigned Block = 0;
    unsigned Index = 0;
    /// What it gives it: what it is declared with or assigned; null of a
    /// clear.
    const Expr *Value = nullptr;
  };
  /// Whether the definitions are told: it is a local that isn't a reference,
  /// declared in the body, and no closure that refers to it gives it a value
  /// or clears it. Else none are kept and each value it is given can be what
  /// each name of it holds.
  bool bTold = false;
  llvm::SmallVector<CDefinition, 4> Definitions;
  /// The local references bound to it, `auto &fAlias = fRead;`, also to one
  /// of those, second names of the slot: what is done through them is done
  /// to it.
  llvm::SmallVector<const VarDecl *, 2> Aliases;
  /// The names of it and of its aliases in the body.
  llvm::SmallVector<const DeclRefExpr *, 8> Names;
  /// Of each name, the definitions that can reach it, by index; of a name
  /// the CFG doesn't have, each.
  llvm::DenseMap<const Stmt *, llvm::BitVector> Reaching;
  /// Of one whose definitions aren't told, each value it is given in the
  /// body, what it is declared with and assigned, which any name of it can
  /// hold.
  llvm::SmallVector<const Expr *, 4> Given;
  /// Of one whose definitions aren't told, each value the body of a closure
  /// that refers to it gives it, which any name of it can hold too.
  llvm::SmallVector<const Expr *, 2> GivenInClosures;

  /// The index of the definition \p Statement is, none of what isn't one.
  std::optional<unsigned> definitionOf(const Stmt *Statement) const;
};

/// The CFGs and effect indexes of a translation unit, made once for each
/// body, which the checks share.
class CFlowCache {
public:
  /// The CFG of \p Body of \p Owner (buildFlowGraph), null when it can't be
  /// built, also of templated code.
  const CFG *graphOf(const Decl *Owner, const Stmt *Body, ASTContext &Context);

  /// The blocks of the CFG of \p Body that can be reached from its entry.
  const llvm::BitVector &reachableOf(const Decl *Owner, const Stmt *Body,
                                     ASTContext &Context);

  /// The effect index of \p Root.
  const CEffectIndex &indexOf(const Stmt *Root, ASTContext &Context);

  /// The statements of \p Body of \p Owner that can run: the statements,
  /// allocations and initializers of the blocks of its CFG that can be
  /// reached, also what a default argument or a default member initializer
  /// evaluates where it is used (CEffectIndex::evaluationOf), and the
  /// declaration a declaration the CFG makes of one of its variables is of
  /// (originalOf); null when it can't be built, of which each can.
  const llvm::DenseSet<const Stmt *> *running(const Decl *Owner,
                                              const Stmt *Body,
                                              ASTContext &Context);

  /// The declaration statement of \p Body that \p S, one the CFG makes for
  /// each variable of a declaration of several, `int *p = ..., *q = p;`, is
  /// of; null of what isn't one.
  const Stmt *originalOf(const Decl *Owner, const Stmt *Body, const Stmt *S,
                         ASTContext &Context);

private:

  uint64_t IndexedAST = 0;
  struct CGraph {
    std::unique_ptr<CFG> Graph;
    llvm::BitVector Reachable;
    std::unique_ptr<llvm::DenseSet<const Stmt *>> Running;
    llvm::DenseMap<const Stmt *, const Stmt *> Synthetic;
    std::unique_ptr<CFlowOrder> Order;
    std::unique_ptr<CFlowPositions> Positions;
  };
  /// Each kept apart: what is given out of one, like the blocks that can be
  /// reached, stays where it is while others are added.
  llvm::DenseMap<const Stmt *, std::unique_ptr<CGraph>> Graphs;
  llvm::DenseMap<const Stmt *, std::unique_ptr<CEffectIndex>> Indexes;

public:
  // The values the returns of \p Function that can run give (writtenReturns),
  // found once for it; what it gives stays where it is while others are
  // added, also by what finding them asks.
  llvm::ArrayRef<const Expr *> returnsOf(const FunctionDecl *Function,
                                         ASTContext &Context);

  bool onlyCallsParameter(const FunctionDecl *Callee, unsigned iParameter,
                          ASTContext &Context);

  /// What the uses of the closure value \p Value, a statement of \p Body of
  /// \p Owner, are (closureUses).
  const CClosureUses &closureUses(const Decl *Owner, const Stmt *Body,
                                  const Stmt *Value, ASTContext &Context);
  /// What the uses of the names of \p Named in \p Body of \p Owner, a
  /// parameter or a local that holds a closure, are (closureUses).
  const CClosureUses &closureUses(const Decl *Owner, const Stmt *Body,
                                  const ValueDecl *Named, ASTContext &Context);

  /// The names of \p Named in \p Body of \p Owner that can run
  /// (forEachRunning), also those of the structured bindings it decomposes
  /// into, each a part of it.
  llvm::ArrayRef<const DeclRefExpr *> namesIn(const Decl *Owner,
                                              const Stmt *Body,
                                              const ValueDecl *Named,
                                              ASTContext &Context);

  /// The analysis of the local slots of \p Function (localSlotValues), made
  /// once for it (TCSummaries).
  const CLocalFlow &localFlowOf(const FunctionDecl *Function,
                                ASTContext &Context);

  /// The summaries of the translation unit (TCSummaries).
  CFlowSummaries &summaries(ASTContext &Context);

  /// The block order and the `try`s of the CFG of \p Body of \p Owner, null
  /// when it can't be built.
  const CFlowOrder *orderOf(const Decl *Owner, const Stmt *Body,
                            ASTContext &Context);

  /// Where the statements of the CFG of \p Body of \p Owner are
  /// (CFlowPositions), made once for it; null when it can't be built.
  CFlowPositions *positionsOf(const Decl *Owner, const Stmt *Body,
                              ASTContext &Context);

  /// The blocks a way of one edge or more from the end of \p Block of the CFG
  /// of \p Body leads to, through blocks that can be reached, as the flows
  /// over it carry what reaches (reachingIn); found once for each block.
  const llvm::BitVector &reachedFrom(const Decl *Owner, const Stmt *Body,
                                     unsigned Block, ASTContext &Context);

  /// What the local \p Variable of \p Body of \p Owner, a function object
  /// of SynchronousFunctionTypes, holds where each name of it is
  /// (CWrapperFlow), made once for it. Not by the slot analysis of its
  /// function (localSlotValues), which asks the closure classifier
  /// (closureUses) that asks this.
  const CWrapperFlow &wrapperFlow(const Decl *Owner, const Stmt *Body,
                                  const VarDecl *Variable, ASTContext &Context);

  /// The local wrapper whose values are told (wrapperFlow) that the local
  /// reference \p Variable of \p Body is a second name of, `auto &fAlias =
  /// fRead;`; null of another.
  const VarDecl *aliasedWrapper(const Decl *Owner, const Stmt *Body,
                                const VarDecl *Variable, ASTContext &Context);

private:
  // The summaries of the translation unit, made again for another.
  std::unique_ptr<CFlowSummaries> Summaries;
  llvm::DenseMap<const FunctionDecl *,
                 std::unique_ptr<llvm::SmallVector<const Expr *, 4>>>
      Returns;
  // Of each wrapper and the body it is a local of (wrapperFlow).
  llvm::DenseMap<std::pair<const VarDecl *, const Stmt *>,
                 std::unique_ptr<CWrapperFlow>>
      WrapperFlows;
  // Of each body, the names of each declaration in it (namesIn).
  llvm::DenseMap<const Stmt *,
                 std::unique_ptr<llvm::DenseMap<
                     const ValueDecl *,
                     llvm::SmallVector<const DeclRefExpr *, 2>>>>
      Names;

public:
  // Starts again for another translation unit.
  void check(ASTContext &Context);
  ~CFlowCache();
};


/// The cache the checks share, made again for each translation unit.
CFlowCache &sharedFlowCache();

// Calls \p Visit with each statement of the effect index of \p Root, a part
// of the body \p Body of \p Owner, that can run (CFlowCache::running), in the
// order of the index; where the CFG can't be built, each that is evaluated.
// What summarizes a body sees of it, its returns, writes, escapes and stores.
void forEachRunning(const Decl *Owner, const Stmt *Body, const Stmt *Root,
                    ASTContext &Context,
                    llvm::function_ref<void(const Stmt *)> Visit);

// The values the return statements of \p Function that can run give as
// written (forEachRunning), not those of the lambdas in it.
llvm::ArrayRef<const Expr *> writtenReturns(const FunctionDecl *Function,
                                            ASTContext &Context);

// Whether what \p Call runs can be an override of \p Method instead: as
// callsOverride tells, also through a pointer to a member function, which
// dispatches, `(Base.*pGet)()`.
bool mayRunOverride(const CallExpr *Call, const CXXMethodDecl *Method);

// What the members \p Path of what \p Made makes are made with: of an
// aggregate, what it is made with; of a constructor whose body can be seen,
// the argument it gives the member of a parameter, and of one whose body
// does nothing but initialize, a member made of nothing it is given, a null
// pointer or a default construction; else null.
const Expr *projected(const Expr *Made, llvm::ArrayRef<const FieldDecl *> Path);

/// The member of the record a structured binding \p Binding names, of a
/// record decomposed member by member; null otherwise.
const FieldDecl *boundMember(const BindingDecl *Binding);

// Whether \p Callee only calls what its parameter \p iParameter is given
// (CFlowCache::onlyCallsParameter).
bool onlyCallsParameter(const FunctionDecl *Callee, unsigned iParameter,
                        ASTContext &Context);

// What the uses of the closure value \p Value, a statement of \p Body of \p
// Owner, are (CFlowCache::closureUses).
const CClosureUses &closureUses(const Decl *Owner, const Stmt *Body,
                                const Stmt *Value, ASTContext &Context);

// The call of a function that runs what it is given (DMibRunsGivenFunction)
// that the closure whose uses are \p Uses is given to as its first callable
// argument, all that is done with it, and the index of that argument, \p
// iGiven; null otherwise, with \p bRunning where it is given to such a
// function as another argument.
const CallExpr *runningCallOf(const CClosureUses &Uses, unsigned &iGiven,
                              bool &bRunning, ASTContext &Context);

// Whether \p S, of \p Body of \p Owner, can run (forEachRunning).
bool canRun(const Decl *Owner, const Stmt *Body, const Stmt *S,
            ASTContext &Context);

const MaterializeTemporaryExpr *extendedSlot(const VarDecl *Variable);

/// What a place of a function's flow is of (CSlot::Place).
enum class EPlace : unsigned char {
  /// A local variable that isn't a reference, also a parameter held by value
  /// and a copy a closure keeps (Variable).
  Variable,
  /// The object of the function, `this` or a reference explicit object
  /// parameter; no Variable.
  This,
  /// What the reference or pointer parameter Variable refers or points to.
  Referent,
};

/// A place of a function's flow: a local slot, a local variable that isn't a
/// reference, also a parameter held by value and a copy a closure keeps, or
/// the object of the function or what a parameter refers to or points to
/// (Place), or a member of one, by the members it is reached through,
/// `View.m_pValue`; of the object of the function and what a parameter
/// refers to, also through what a member points to, `_Batch.m_pContext->m_pJob`
/// (crossesPointer); Element of what is designated, only an element of it,
/// `pValues[1]`, Index its index where that is a constant.
struct CSlot {
  const VarDecl *Variable = nullptr;
  llvm::SmallVector<const FieldDecl *, 2> Path;
  bool Element = false;
  EPlace Place = EPlace::Variable;
  std::optional<int64_t> Index = std::nullopt;
  bool operator==(const CSlot &Other) const {
    return Variable == Other.Variable && Path == Other.Path &&
           Element == Other.Element && Place == Other.Place &&
           Index == Other.Index;
  }
};

/// Whether a member of \p Path from \p From on, past the first, is reached
/// through what the one before it points to, refers to or owns, not held in
/// it: a place through it is a part of what that points to, not of it.
bool crossesPointer(llvm::ArrayRef<const FieldDecl *> Path, size_t From);

/// Whether \p Place, of the object of a member function or of what a
/// parameter refers to, is what a pointer or reference member on the way to
/// it points or refers to, `m_pSelf->m_pJob`, `*m_ppJob` or what `m_rpJob`
/// refers to, not a part of the object itself (crossesPointer).
bool throughMember(const CSlot &Place);

/// Whether \p Place, an element, is one of storage of its own, of an array
/// or of a record, a container that holds it, which no other way to it
/// reaches but through it: not of what a pointer or a reference on the way
/// points or refers to. \p Function is the function of a place that isn't a
/// local slot.
bool ownedElement(const CSlot &Place, const FunctionDecl *Function);

/// Whether an object of \p Written can be in an object of the record
/// \p Holder, as its part held by value at any depth, or be it: of a record,
/// the same, a base or derived class or one it holds (holdsObjectOf); of what
/// isn't one, a member of that type, also of an array's element; of bytes
/// (`void`, a character type), any.
bool canBeInObject(QualType Written, QualType Holder, ASTContext &Context);

/// A member of the object of the member function Function, reached from its
/// `this` by members, the outermost first, `mp_Context.m_pJob`, or an element
/// of one, Element the subscript that designates it, `mp_Jobs[Key]`
/// (thisMemberOf); an empty Path of what isn't one.
struct CThisMember {
  const FunctionDecl *Function = nullptr;
  llvm::SmallVector<const FieldDecl *, 2> Path;
  const Expr *Element = nullptr;
  bool operator==(const CThisMember &Other) const {
    return Function == Other.Function && Path == Other.Path &&
           Element == Other.Element;
  }
};

/// The member of the object of \p Function that \p E designates there
/// (CThisMember): through members from `this`, an operator `[]` of one, and a
/// local reference bound to one, `auto &pJob = mp_Jobs[Key];`.
CThisMember thisMemberOf(const Expr *E, const FunctionDecl *Function);

/// Whether what \p Member holds where \p To is, or at the exit of its
/// function where \p To is null, is what it holds where \p From is: each way
/// through the flow of its function to \p To passes \p From after the last
/// change of it there. A change is a definition the flow of its function
/// (localSlotValues) has of the member, a place of the object
/// (EPlace::This), or of a part of it: what assigns or steps it or what it is
/// in, a member function that can change it called on either or on the
/// object (changedMembers), what gives either where the body it is given to
/// can change it, and a suspension, after which other code can have run on
/// the object. False where the flow isn't made.
bool thisMemberUnchangedBetween(const CThisMember &Member, const Stmt *From,
                                const Stmt *To, ASTContext &Context);

/// A definition of a member of the object that reaches a place
/// (thisMemberDefinitions): what it holds where its function is entered
/// (Entry), what is assigned to it, Value, or a change whose value can't be
/// told, Called when that is a member function called on the object or
/// what it is given to.
struct CThisDefinition {
  const Expr *Value = nullptr;
  bool Entry = false;
  bool Called = false;
  /// Of Called, the call that calls a member function on the object or
  /// gives it.
  const Stmt *Call = nullptr;
};

/// The definitions of \p Member that reach \p At, or the exit of its
/// function where \p At is null, on the ways that don't end in a `throw` or
/// a call of a function that doesn't return (the changes of
/// thisMemberUnchangedBetween); none told where the flow
/// isn't made or \p At isn't an element of it.
std::optional<llvm::SmallVector<CThisDefinition, 2>>
thisMemberDefinitions(const CThisMember &Member, const Stmt *At,
                      ASTContext &Context);

/// What \p Call, a call of a copy or a move assignment operator the compiler
/// defines, copies or moves, `User` of `Info.m_User = User;`, which gives the
/// object, member by member, what a copy or a move constructor the compiler
/// defines gives a new one; null of another call.
const Expr *copiedByDefinedAssignment(const Expr *Call, ASTContext &Context);

/// A value a local slot can hold where it is read (localSlotValues).
struct CSlotValue {
  enum class EKind {
    /// What Value gives, also the value given to an element.
    Value,
    /// What is stored where Value, a location, is, as it is there: what an
    /// array is made with, or what a slot held before it is stepped.
    Stored,
    /// What it is given at the start, of a parameter held by value.
    Given,
    /// The address of what Value, a location, designates, of a pointer a
    /// setter assigns it (setParameters), `o_pValue = &_Value;`.
    Address,
  };
  EKind Kind = EKind::Value;
  const Expr *Value = nullptr;
  /// Of Address, whether it is the address of a part of what Value
  /// designates, a member of it (CSetParameter::Part).
  bool Part = false;
  bool operator==(const CSlotValue &Other) const {
    return Kind == Other.Kind && Value == Other.Value && Part == Other.Part;
  }
};

/// What a local slot can hold where it is read (localSlotValues).
struct CSlotValues {
  llvm::SmallVector<CSlotValue, 4> Values;
  /// Whether they are what reaches there: not of a slot whose address can
  /// be used where the flow can't follow it, given to what can keep it,
  /// which is each value it is given anywhere instead.
  bool Told = true;
  bool operator==(const CSlotValues &Other) const {
    return Values == Other.Values && Told == Other.Told;
  }
};

/// The slots \p Target, a location, designates where it is: a local, a
/// member of one, an element of one; also through a local reference, a name
/// of a structured binding, a reference member of a local aggregate, what a
/// helper or a getter gives a reference or a pointer to (designateReturned),
/// and through what a local pointer can hold there (localSlotValues),
/// `*ppValue`; several when it can be each of them. \p Told, when given, is
/// cleared when it can be what isn't a slot as well.
llvm::SmallVector<CSlot, 2> designatedSlots(const Expr *Target,
                                            ASTContext &Context,
                                            bool *Told = nullptr);

/// The slots what the pointer \p Pointer points to designates, as `*Pointer`
/// does (designatedSlots).
llvm::SmallVector<CSlot, 2> pointedSlots(const Expr *Pointer,
                                         ASTContext &Context,
                                         bool *Told = nullptr);

/// The places \p Target, a location or a pointer, designates or points to, as
/// designatedSlots and pointedSlots tell, also the object of the function it
/// is in, `this` and a member of it (EPlace::This), and what a reference
/// parameter refers to and a member of it (EPlace::Referent).
llvm::SmallVector<CSlot, 2> designatedPlaces(const Expr *Target,
                                             ASTContext &Context,
                                             bool *Told = nullptr);

/// The values \p Slot can hold where \p At is, by the definitions of the
/// slots of its function that reach there in its flow (buildFlowGraph), one
/// analysis for all of them, by the destinations of the effect table
/// (effectsOf): what declares it, what is assigned to what designates it
/// (designatedSlots), only maybe of several or of an element, which adds to
/// what it holds, else replaces it; what steps it, by what it held; of a
/// parameter what it is given, of a copy a closure keeps what it copies
/// where the closure is made and, of a mutable one, what it assigns it
/// anywhere, also what a closure that refers to it assigns it where it can
/// be called. A member is what its object is made with.
CSlotValues localSlotValues(const CSlot &Slot, const Stmt *At,
                            ASTContext &Context);

/// What \p Slot can hold where the scope of its variable ends, by the
/// definitions that reach each automatic destructor of the variable in the
/// flow of its function, as localSlotValues; not told where none is in the
/// flow.
CSlotValues localSlotValuesAtEnd(const CSlot &Slot, ASTContext &Context);

/// Whether the address of the local \p Variable, or a reference to it, can
/// be used where the flow of its function can't follow it (findEscapes);
/// so of a function the flow can't be made of.
bool localSlotEscapes(const VarDecl *Variable, ASTContext &Context);

/// A definition of a local slot that reaches where it is read
/// (localSlotReaching): the value it gives, unless it isn't Known, a change
/// the flow can't tell the value of, like a member function that isn't const
/// called on it or a reference to it given where it can be changed.
struct CSlotDefinition {
  CSlotValue Value;
  bool Known = true;
  /// Whether it is such a change, not a value that can't be told, like a
  /// member of what a declaration is made with that can't be projected.
  bool Changed = false;
  /// Of such a change, what it designates as it is written there, the
  /// object of a member function called on it.
  const Expr *Target = nullptr;
  bool operator==(const CSlotDefinition &Other) const {
    return Value == Other.Value && Known == Other.Known &&
           Changed == Other.Changed && Target == Other.Target;
  }
};

/// The definitions of a local slot that reach where it is read
/// (localSlotReaching).
struct CSlotReaching {
  llvm::SmallVector<CSlotDefinition, 4> Definitions;
  /// Whether they are what reaches there (CSlotValues::Told), and where it is
  /// read is in the flow, not in the body of a closure made there.
  bool Told = true;
  bool operator==(const CSlotReaching &Other) const {
    return Definitions == Other.Definitions && Told == Other.Told;
  }
};

/// The expression whose owner value the owner \p E gives (OwnerTypes):
/// past parentheses and implicit steps, what a copy or a move assignment the
/// compiler defines copies, a copy or a move of a record, a conversion to an
/// owner of the same kind, a cast between owners, a move, a forward or a copy
/// function, what
/// an exchange held before in the first it is given, and a member function
/// of a shared owner that gives by value an owner of the same class
/// template, its object, `pJob.f_ShareAsConst()`, also through a pointer to
/// it, `ppJob->f_ShareAsConst()`; \p E itself otherwise.
const Expr *ownerValueSource(const Expr *E, ASTContext &Context);

/// The definitions of \p Slot that reach where \p At is, each with whether
/// its value is known, by the same analysis as localSlotValues.
CSlotReaching localSlotReaching(const CSlot &Slot, const Stmt *At,
                                ASTContext &Context);

/// The slot the structured binding \p Binding names: the member of the
/// decomposed variable it is bound to, `{Keep, m_pThis}` of `pThis` of `auto
/// [pThis, ID] = CKeep{...}`, where that variable is a local slot; none
/// otherwise, of a tuple-like binding or an array element.
std::optional<CSlot> bindingSlotOf(const BindingDecl *Binding);

/// The structured binding \p Made copies or names, past implicit steps and
/// a copy or a move constructor, the initializer of `[pThis]` of a binding
/// `pThis`; null otherwise.
const BindingDecl *bindingCopiedBy(const Expr *Made);

/// The list of elements \p Construct is given, as a `std::initializer_list`,
/// its only argument other than defaults, `TCList<CJob> Jobs{_Job}`; null
/// otherwise.
const InitListExpr *elementListGivenTo(const CXXConstructExpr *Construct);

/// What a call can change of a local slot through what it is given
/// (writtenPartsOf).
struct CWrittenParts {
  /// Whether it is told: else it can change any part.
  bool Told = false;
  /// Whether it can change the slot, a part of it or what it is in.
  bool Writes = true;
  /// Of an element slot, the constant indices of the only elements it can
  /// change; empty where which can't be told.
  llvm::SmallVector<int64_t, 2> Indices;
};

/// What the call that \p Given, a pointer to or a location of a local slot,
/// is given to as an argument can change of \p Slot, a part of that slot or
/// that slot itself, through the parameter it is given to, by its bodies:
/// the members, and of an element of an array member, the constant indices,
/// its body assigns, steps, or calls a member function that can change on,
/// or gives where it can be changed, through that parameter. Not told of a
/// body that can't be seen, an override that can run, or a parameter used
/// another way: kept, given on, its address taken.
CWrittenParts writtenPartsOf(const Expr *Given, const CSlot &Slot,
                             ASTContext &Context);

/// Whether the code of \p Function can give \p Slot, one of its slots, a
/// value other than what it starts with: a definition of it that isn't at the
/// entry of its flow (localSlotValues), or a use of its address that the flow
/// can't follow; also where the flow isn't made.
bool localSlotChangedIn(const FunctionDecl *Function, const CSlot &Slot,
                        ASTContext &Context);

/// Whether what \p Slot holds where \p To is, is what it holds where \p From
/// is: each way to \p To passes \p From after the last definition of the
/// slot before it, by the definitions of localSlotValues, in the flow of the
/// one function both are in.
bool localSlotUnchangedBetween(const CSlot &Slot, const Stmt *From,
                               const Stmt *To, ASTContext &Context);

/// A definition of a local slot (localSlotDefinitionsReaching), and where it
/// is made: the element of the flow, the declaration, the assignment, or the
/// call that swaps, exchanges or sets it or runs a closure that assigns it;
/// and what it designates the slot by, the target of the assignment, an
/// argument of the call, or the target in the closure's body; null of what
/// the slot starts with.
struct CSlotDefinitionAt {
  CSlotDefinition Definition;
  const Stmt *Element = nullptr;
  const Expr *Target = nullptr;
  bool operator==(const CSlotDefinitionAt &Other) const {
    return Definition == Other.Definition && Element == Other.Element &&
           Target == Other.Target;
  }
};

/// The definitions of \p Slot, a slot of \p Function, made where a way from
/// the entry of its flow (buildFlowGraph) that passes no element \p Stops
/// picks (eventOf) reaches, that reach an element \p Reached picks on a way
/// that passes no such element either, nor a definition of the slot that
/// \p Ends picks, which replaces what it holds: of what it holds there,
/// what was given since such a definition; another definition only adds to
/// it. What an element defines reaches the elements after it. Each once, in
/// the order of the slot analysis (localSlotValues); none told where the
/// flow isn't made.
std::optional<llvm::SmallVector<CSlotDefinitionAt, 2>>
localSlotDefinitionsReaching(
    const FunctionDecl *Function, const CSlot &Slot,
    llvm::function_ref<bool(const CSlotDefinition &)> Ends,
    llvm::function_ref<bool(const CFlowEvent &)> Stops,
    llvm::function_ref<bool(const CFlowEvent &)> Reached, ASTContext &Context);

/// Whether each way through the flow of \p Body of \p Owner
/// (buildFlowGraph), from its entry to the element of \p To, or else to its
/// exit, passes an element \p Passes picks (eventOf) before it; false where
/// the flow isn't made or \p To isn't an element of it.
bool passesOnEachWay(const Decl *Owner, const Stmt *Body,
                     llvm::function_ref<bool(const CFlowEvent &)> Passes,
                     ASTContext &Context, const Stmt *To = nullptr);

/// Whether a way through the flow of \p Body of \p Owner (buildFlowGraph)
/// leads from the element of \p From to the element of \p To, past \p From;
/// also where the flow isn't made or either isn't an element of it, which
/// can't be told.
bool canRunAfter(const Decl *Owner, const Stmt *Body, const Stmt *From,
                 const Stmt *To, ASTContext &Context);

/// A part of what a reference parameter refers to (referentPartOf): its
/// members Path, the outermost first, a pointer member among them read
/// through, and of Element an element of what Path designates, Index the
/// index or key that designates it, null where none is given.
struct CReferentPart {
  const ParmVarDecl *Parameter = nullptr;
  llvm::SmallVector<const FieldDecl *, 2> Path;
  bool Element = false;
  const Expr *Index = nullptr;
};

/// The part of what a reference parameter refers to that \p E designates:
/// past local references, each bound once, members, also through a pointer
/// member, `_Batch.m_pContext->m_pJob`, and an element that a member function
/// seen only to give access gives a reference or a pointer to, of an object
/// that owns its elements (elementAccessObject), `_Jobs[0]`,
/// `*_Jobs.f_FindEqual(_Key)`; of \p Pointer also members of what a pointer
/// parameter points to, `_pContext->m_pJob`; no Parameter where it is
/// something else, a member of an element, or an element of an element. At
/// most 16 steps.
CReferentPart referentPartOf(const Expr *E, ASTContext &Context,
                             bool Pointer = false);

/// Whether a body that can be seen of each function \p Call can run, given
/// what its parameter \p iParameter, a pointer or a reference, points to or
/// refers to, doesn't change it (pointedParameterWritten).
bool parameterOnlyRead(const CallExpr *Call, const FunctionDecl *Callee,
                       unsigned iParameter, ASTContext &Context);

/// The index or key of the element that \p E designates, or that \p E, a
/// pointer an element access gives, points to, the argument of the member
/// function seen only to give access that gives it (elementAccessObject),
/// past local references and a local pointer whose one definition that
/// reaches there is known (localSlotReaching), `*ppJob` of `ppJob =
/// Jobs.f_FindEqual(_Key)`; null where none is told. At most 16 steps.
const Expr *elementIndexOf(const Expr *E, ASTContext &Context);

/// Whether what \p Part, a part of what a reference parameter refers to
/// (referentPartOf), holds is unchanged between \p From, or the entry of its
/// function where it is null, and \p To, in the flow of its function, where
/// it is a place (EPlace::Referent): each way to \p To passes \p From after
/// the last definition of it or of a part of it, also through what a member
/// points to, by any way a location designates it (designatedPlaces), not of
/// an element of it only, nor, of an element of a constant index, one of
/// another; nor of a member a member function called on it can't change
/// (CWrite::Members); a write of another place of a type that can be the
/// same object is one of it (mayAlias). Not of one whose address escapes.
bool localReferentUnchangedBetween(const CReferentPart &Part,
                                   const Stmt *From, const Stmt *To,
                                   ASTContext &Context);

/// What the slot of \p Parameter, held by value, is assigned in its function
/// that can reach where \p At is (localSlotValues), not what it is given;
/// \p Given, when given, is set to whether what it is given can reach there
/// too.
llvm::SmallVector<CSlotValue, 2> assignedAt(const ParmVarDecl *Parameter,
                                            const Stmt *At,
                                            ASTContext &Context,
                                            bool *Given = nullptr);

CSlotValues localSlotValuesFrom(const CSlot &Slot, const Stmt *At,
                                ASTContext &Context);

/// The parts of \p Slot, members of it or elements of an array member,
/// that its function assigns on their own, `Result.m_pValue = &Value;`,
/// whose values are part of what it holds (localSlotValues).
llvm::SmallVector<CSlot, 2> localSlotParts(const CSlot &Slot, const Stmt *At,
                                           ASTContext &Context);

/// Whether what the whole of what \p Part, a part of a slot (localSlotParts),
/// is in is given can be what the part holds where \p At is, not only what
/// it is assigned on its own there.
bool localSlotWholeReaches(const CSlot &Part, const Stmt *At,
                           ASTContext &Context);

/// Each value \p Slot is given anywhere in its function (localSlotValues).
CSlotValues localSlotAssignments(const CSlot &Slot, ASTContext &Context);

// What a local closure assigns to a slot of the function it is made in where
// a call can run it (closureWritesAt): the slot, and the value, only maybe.
struct CClosureWrite {
  CSlot Slot;
  CSlotValue Value;
  // Whether it is assigned there each time, the slot only: of a closure only
  // called, one write of its own that runs each time it runs.
  bool Definite = false;
};

// What the local closures that \p Call, a call in \p Function, can run assign
// to the slots of \p Function, as its slot analysis has them
// (localSlotValues): what each writes that can run, by what it designates
// (designatedSlots), not a copy it keeps itself; a closure only called
// directly or through the local it is in, at those calls, else at each call
// after it is made.
llvm::SmallVector<CClosureWrite, 2>
closureWritesAt(const FunctionDecl *Function, const Stmt *Call,
                ASTContext &Context);

// What the local closures that the local object \p Guard of \p Function runs
// where it ends, the guard of a scope exit, assign there (closureWritesAt).
llvm::SmallVector<CClosureWrite, 2>
closureWritesAtEnd(const FunctionDecl *Function, const VarDecl *Guard,
                   ASTContext &Context);

bool closureRunsAtEnd(const FunctionDecl *Function, unsigned Block,
                      unsigned Index, ASTContext &Context);

/// Whether the closure \p Lambda, made in the body of \p Function, runs
/// only while \p Function runs: it is called there, also through the locals
/// it is in and the closures that capture it, given to what only calls it,
/// or to a local guard of a scope exit that runs it where it ends, which
/// isn't shared (OwnerTypes) and whose own value stays in the function; not
/// given where it can be run later.
bool closureRunsInCall(const FunctionDecl *Function, const LambdaExpr *Lambda,
                       ASTContext &Context);

/// Whether the closure of the class \p Closure, made in the initializer of
/// \p Holder, a member or a variable, runs only while that initializer runs:
/// it is only called there (closureUses); false where it isn't made there.
bool closureRunsInInitializer(const Decl *Holder, const CXXRecordDecl *Closure,
                              ASTContext &Context);

/// Whether the closure of the class \p Closure, made in the member initializer
/// \p Initializer of a constructor, also a default one it uses, runs only
/// while that runs (closureRunsInInitializer).
bool closureRunsInInitializer(const CXXCtorInitializer *Initializer,
                              const CXXRecordDecl *Closure,
                              ASTContext &Context);

/// What an expression is where provenance can't go on past it (leafOf).
enum class ELeaf {
  /// It refers to nothing, which merges with other ways as the identity.
  Nothing,
  /// It is static storage; what that holds refers to nothing.
  Static,
  /// What it refers to is what provenance follows it to; where it follows
  /// it to nothing, it can't be told.
  Followed,
};

/// What \p E is as a leaf of provenance, by its kind, each kind of
/// expression classified: a null pointer constant in any spelling, `nullptr`,
/// `0`, a cast of one, a value-initialized scalar, and a literal or a value
/// that isn't a pointer refer to nothing; a string literal and what names one
/// are static storage; what else provenance follows.
ELeaf leafOf(const Expr *E);

/// Whether \p E, reached at \p Level, refers to nothing (leafOf): of static
/// storage, what it holds, above level 0.
bool refersToNothing(const Expr *E, unsigned Level);

/// What provenance does at an expression it reaches (CProvenance::reach).
enum class EReach {
  /// It isn't what is looked for: go on by its effects.
  Continue,
  /// It is what is looked for: a root.
  Root,
  /// It ends there: what is looked for isn't past it.
  Stop,
};

// What a way of provenance takes of what it is at, at one level
// (CProvenance::reach): the members it is reached through, the outermost
// last, and whether only an element of what they designate, or a part that
// can't be told, which no member is taken of after it.
struct CProjection {
  llvm::SmallVector<const FieldDecl *, 2> Path;
  bool Element = false;
  // Of an element, its index where that is a constant.
  std::optional<int64_t> Index = std::nullopt;
  // Of an element, the size in bytes of what the step that takes it steps
  // over, `1` of `pBytes[4]` of an `unsigned char *`; 0 where unknown.
  uint64_t Stride = 0;
  bool operator==(const CProjection &Other) const {
    return Path == Other.Path && Element == Other.Element &&
           Index == Other.Index && Stride == Other.Stride;
  }
};

// What a value a function gives back is made of where a way of provenance
// from it ends (CProvenance::returnedRoots): its object, a parameter, or a
// name of what isn't of the function, a capture by reference, `return p;` of
// `[&]() -> int *& { return p; }`.
struct CReturnedRoot {
  // Of Static, what has static storage, as named in the function, `return
  // s_Config;` or `return "Default";`, which the way goes on at.
  enum class EKind { Object, Parameter, Outer, Static };
  EKind Kind = EKind::Object;
  // Of a parameter, its index.
  unsigned Index = 0;
  // Of what isn't of the function, its name.
  const Expr *Name = nullptr;
  unsigned Level = 0;
  bool Part = false;
  // The members to a reference member whose referent is reached, the last.
  llvm::SmallVector<const FieldDecl *, 1> Bound;
  // What the way takes of each level where it ends, the value's first.
  llvm::SmallVector<CProjection, 1> Projections;
  bool operator==(const CReturnedRoot &Other) const {
    return Kind == Other.Kind && Index == Other.Index && Name == Other.Name &&
           Level == Other.Level && Part == Other.Part && Bound == Other.Bound &&
           Projections == Other.Projections;
  }
};

// What the returns of a function give, each way of each (returnedRoots).
struct CReturnedRoots {
  llvm::SmallVector<CReturnedRoot, 2> Roots;
  // Whether a way ends at what isn't one of them, or can't be told.
  bool Other = false;
  bool operator==(const CReturnedRoots &Of) const {
    return Roots == Of.Roots && Other == Of.Other;
  }
};

// The bodies a call can run, by what their returns give
// (CProvenance::calledRoots): of the callee, or, where an override can run
// instead (mayRunOverride), of each override in the translation unit that
// isn't pure (dispatchedMethods).
struct CCalledRoots {
  // Each body that can be seen, with what its returns give.
  llvm::SmallVector<std::pair<const FunctionDecl *, const CReturnedRoots *>, 2>
      Bodies;
  // Whether a body that can't be seen can run: of the callee or of an
  // override in the translation unit.
  bool bUnseen = false;
  // Whether an override can run instead, also one in another translation
  // unit.
  bool bOverridable = false;
  // Whether what can run can't all be seen.
  bool open() const { return bUnseen || bOverridable || Bodies.empty(); }
};

// The one model of what an expression is made of, at a level (reach): at
// level 0, a glvalue is the storage it designates, a pointer what it points
// to; at each level above, what the value stored there, loaded, points to.
// Each transition by the effects of what it is at (effectsOf, step), which
// every walk shares: what the thread-local checks follow, what the slots a
// location designates are (designatedSlots), and what a function gives back
// (returnedRoots).
class CProvenance {
public:
  explicit CProvenance(ASTContext &Context) : Context(Context) {}

  bool reach(const Expr *E, unsigned Level,
             llvm::function_ref<EReach(const Expr *At, unsigned Level,
                                       bool Part)>
                 Visit,
             bool *Ended = nullptr) const;

  void step(const Expr *At, unsigned AtLevel, bool Part,
            llvm::function_ref<void(const Expr *Next, unsigned Level,
                                    bool Part)>
                Next,
            bool &Complete) const;

  // What the way of reach at what its Visit is told takes of each level, that
  // of the level it is at last (CProjection); empty outside reach.
  llvm::ArrayRef<CProjection> projections() const {
    return Projections ? llvm::ArrayRef<CProjection>(*Projections)
                       : llvm::ArrayRef<CProjection>();
  }

  const CReturnedRoots *returnedRoots(const FunctionDecl *Function) const;

  // What the returns of each body \p Call of \p Callee can run give
  // (returnedRoots, CCalledRoots).
  CCalledRoots calledRoots(const CallExpr *Call,
                           const FunctionDecl *Callee) const;

  // Whether what a member function gives by reference or pointer where a
  // body it can run can't be seen is a part of its object that can't be told
  // (the default), or ends there, of a walk that tells it itself.
  void setUnseenObjectPart(bool Part) { bUnseenObjectPart = Part; }

  void setOtherEnds(bool Ends) { bOtherEnds = Ends; }

  // Whether reach stops at the first way that ends where nothing it is told
  // of is (its Ended), of a walk whose answer that settles.
  void setStopAtEnd(bool Stop) { bStopAtEnd = Stop; }

  ASTContext &context() const { return Context; }

private:
  friend class CReachClosures;
  // A way of reach (reach).
  struct CWay;
  bool holdsOnlyReplaced(const CWay &Way) const;
  void expandWay(const CWay &Way, llvm::SmallVectorImpl<CWay> &Into,
                 bool &Complete) const;

  ASTContext &Context;
  // Whether a way of a return of a function that ends at what isn't one of
  // its roots (CReturnedRoots::Other) ends the way through a call of it too,
  // of a walk that must tell each alternative, the slots designated; else
  // only the roots are followed.
  bool bOtherEnds = false;
  bool bStopAtEnd = false;
  bool bUnseenObjectPart = true;
  // Where the slot step reads next is read, the last read of the way (CReads).
  mutable const Stmt *ReadAt = nullptr;
  // What step's transition is to the reads of the way (reach).
  enum class EHop { None, Load, Held, HeldBefore, Binding };
  mutable EHop Hop = EHop::None;
  // What step's transition takes of what it is at (CProjection, reach).
  enum class EProject {
    None,
    Field,
    Element,
    Consume,
    Clear,
    Stepped,
    Returned,
    // The part of the slot read, as a whole (ProjectPath).
    Path
  };
  mutable EProject Project = EProject::None;
  // The member of EProject::Field.
  mutable const FieldDecl *ProjectField = nullptr;
  // The constant index of EProject::Element.
  mutable std::optional<int64_t> ProjectIndex;
  // The stride of EProject::Element (CProjection::Stride).
  mutable uint64_t ProjectStride = 0;
  // The members of EProject::Path, a copy: what step takes them from, a
  // local list of the members a whole holds, can end while Project is still
  // Path.
  mutable llvm::SmallVector<const FieldDecl *, 2> ProjectPath;
  // The root of EProject::Returned.
  mutable const CReturnedRoot *ProjectRoot = nullptr;
  // What the way at what Visit and step are told takes of each level.
  mutable const llvm::SmallVectorImpl<CProjection> *Projections = nullptr;
};

/// What CProvenance::reach visits, worked out once for each way and shared by
/// the walks of a body that ask it with the same Visit: of each way, the
/// records Visit gives it, and of what reach visits from a start, the records
/// of each way it visits, each once, in increasing order. Visit is told no
/// part (it must not depend on it), gives the records of a way and tells
/// whether reach goes on past it (EReach::Continue); Reached is told each
/// expression a way is at as it is first visited, by which a caller tells
/// whether a question of its own about what is visited holds. What reach
/// returns isn't told but by visits; what it tells of a way that ends isn't.
class CReachClosures {
public:
  using FVisit = llvm::function_ref<EReach(
      const Expr *At, unsigned Level, llvm::SmallVectorImpl<unsigned> &Records)>;
  using FReached = llvm::function_ref<void(const Expr *At)>;

  /// With \p bVisits, it is asked only by visits, which tells of each start
  /// the expressions reach visits from it instead of records.
  explicit CReachClosures(const CProvenance &Walk, bool bVisits = false);
  ~CReachClosures();

  /// The records of each way reach from \p E at \p Level visits; null where
  /// that can't be shared with what was asked before: reach is asked inside
  /// another walk, the ways are past reach's budget, or two ways reach keeps
  /// apart only by what its Seen doesn't tell; then reach is asked instead.
  /// \p Incomplete, when given, is set where reach with the same Visit can't
  /// tell all it reaches (it returns false).
  const llvm::SmallVectorImpl<unsigned> *closure(const Expr *E, unsigned Level,
                                                 FVisit Visit, FReached Reached,
                                                 bool *Incomplete = nullptr);

  /// Whether reach from \p E at \p Level, whose Visit goes on everywhere,
  /// tells Visit a way at \p Target, or can't tell all it reaches (reach
  /// returns false); none where closure would give null, or the expressions
  /// visited would take more than a bound of memory.
  std::optional<bool> visits(const Expr *E, unsigned Level,
                             const Expr *Target);

private:
  struct CNode;
  struct CData;
  // The component of what reach from \p E at \p Level visits (closure).
  std::optional<unsigned> component(const Expr *E, unsigned Level,
                                    FVisit Visit, FReached Reached);
  const CProvenance &Walk;
  std::unique_ptr<CData> Data;
};

} // namespace clang::tidy::malterlib

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_VALUEFLOW_H
