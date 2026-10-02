//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "HeaderDeclarationsOnlyCheck.h"
#include "../utils/OptionsUtils.h"
#include "clang/AST/ASTLambda.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/StmtCXX.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Lex/Lexer.h"
#include "llvm/Support/Path.h"

using namespace clang::ast_matchers;

namespace clang::tidy::malterlib {

HeaderDeclarationsOnlyCheck::HeaderDeclarationsOnlyCheck(
    StringRef Name, ClangTidyContext *Context)
    : MalterlibCheck(Name, Context),
      DeclarationFileExtensions(utils::options::parseStringList(
          listOption("DeclarationFileExtensions", "h"))),
      OutOfClassFileExtensions(utils::options::parseStringList(
          listOption("OutOfClassFileExtensions", "cpp;cc;cxx;mm"))) {}

void HeaderDeclarationsOnlyCheck::storeCheckOptions(
    ClangTidyOptions::OptionMap &Opts) {
  Options.store(Opts, "DeclarationFileExtensions",
                utils::options::serializeStringList(DeclarationFileExtensions));
  Options.store(Opts, "OutOfClassFileExtensions",
                utils::options::serializeStringList(OutOfClassFileExtensions));
}

void HeaderDeclarationsOnlyCheck::registerMatchers(MatchFinder *Finder) {
  Finder->addMatcher(functionDecl(isDefinition()).bind("function"), this);
}

/// Whether \p S only declares, like the types a `.h` included in a body
/// defines, `struct CLocal { ... };`, which aren't implementation, also
/// functions, `void fg_Work();`, and variables defined elsewhere, `extern
/// unsigned g_Bits;`.
static bool declaresOnly(const Stmt *S) {
  const auto *Declaration = dyn_cast<DeclStmt>(S);
  return Declaration &&
         llvm::all_of(Declaration->decls(), [](const Decl *Declared) {
           if (const auto *Variable = dyn_cast<VarDecl>(Declared))
             return Variable->hasExternalStorage() && !Variable->hasInit();
           return isa<TagDecl, TypedefNameDecl, UsingDecl, UsingDirectiveDecl,
                      NamespaceAliasDecl, StaticAssertDecl, FunctionDecl>(
               Declared);
         });
}

namespace {
/// The tokens that name a type in what a macro argument gives a function
/// made by a macro: where each type, and each qualifier naming one, of its
/// body and initializers is spelled in a file, `d_CType` of `return new
/// d_CType;`, also as the location of an implicit node, the constructor call
/// that names no more than the type.
class CTypeTokens : public RecursiveASTVisitor<CTypeTokens> {
public:
  explicit CTypeTokens(const SourceManager &TheSM) : SM(TheSM) {}

  bool shouldVisitTemplateInstantiations() const { return false; }
  bool shouldVisitImplicitCode() const { return false; }

  bool VisitTypeLoc(TypeLoc Type) {
    add(Type.getSourceRange());
    return true;
  }
  bool TraverseNestedNameSpecifierLoc(NestedNameSpecifierLoc Qualifier) {
    if (Qualifier)
      add(Qualifier.getSourceRange());
    return RecursiveASTVisitor::TraverseNestedNameSpecifierLoc(Qualifier);
  }

  /// Whether the token at \p Offset of \p File is in a type.
  bool contains(FileID File, unsigned Offset) const {
    return llvm::any_of(Ranges, [&](const CRange &Range) {
      return Range.File == File && Range.Begin <= Offset && Offset <= Range.Last;
    });
  }

private:
  struct CRange {
    FileID File;
    unsigned Begin;
    unsigned Last;
  };
  void add(SourceRange Range) {
    if (Range.isInvalid())
      return;
    const SourceLocation Begin = SM.getSpellingLoc(Range.getBegin());
    const SourceLocation Last = SM.getSpellingLoc(Range.getEnd());
    if (!Begin.isFileID() || !Last.isFileID() ||
        SM.getFileID(Begin) != SM.getFileID(Last) ||
        SM.getFileOffset(Last) < SM.getFileOffset(Begin))
      return;
    Ranges.push_back(
        {SM.getFileID(Begin), SM.getFileOffset(Begin), SM.getFileOffset(Last)});
  }

  const SourceManager &SM;
  llvm::SmallVector<CRange, 8> Ranges;
};
} // namespace

void HeaderDeclarationsOnlyCheck::check(
    const MatchFinder::MatchResult &Result) {
  if (Repeated.repeated(Result))
    return;
  const auto *Function = Result.Nodes.getNodeAs<FunctionDecl>("function");
  // Defaulted and deleted functions have no bodies, and lambdas are
  // expressions.
  if (Function->isImplicit() || Function->isDefaulted() ||
      Function->isDeleted() || !Function->doesThisDeclarationHaveABody() ||
      Function->isTemplateInstantiation() || isLambdaCallOperator(Function))
    return;
  const Stmt *Body = Function->getBody();
  if (!Body)
    return;

  // A body that comes from a macro is part of what the macro declares, but
  // one written in an argument of the macro, like `DFunction(fg_Name, {
  // ... })`, is written where the macro is used.
  const SourceManager &SM = *Result.SourceManager;
  // Not one that an argument forwards from another macro, `DFunction(fg_Run,
  // DBody)`, whose tokens are spelled in the definition of that macro.
  SourceLocation BodyLoc = Body->getBeginLoc();
  if (BodyLoc.isMacroID() && SM.isMacroArgExpansion(BodyLoc) &&
      SM.getSpellingLoc(BodyLoc) == SM.getFileLoc(BodyLoc))
    BodyLoc = SM.getFileLoc(BodyLoc);
  // A macro that gives only the braces of the body, like `{`, leaves the rest
  // written where it is used.
  if (BodyLoc.isMacroID() && Body->getEndLoc().isFileID())
    BodyLoc = Body->getEndLoc();
  // Also written in an argument of the macro, `DDefine(fg_Run, return 1;)`,
  // also nested in what the macro writes, `{ if (true) { d_Code } }`, or in a
  // function try block, `DTry(return 1;, return 0;)`: a statement spelled
  // where the macro is used, not in its definition.
  // Of a coroutine, the body as written.
  const Stmt *Written = Body;
  if (const auto *Coroutine = dyn_cast<CoroutineBodyStmt>(Written))
    Written = Coroutine->getBody();
  auto WrittenWhereUsed = [&](SourceLocation Loc) {
    return Loc.isValid() &&
           (Loc.isFileID() || (SM.isMacroArgExpansion(Loc) &&
                               SM.getSpellingLoc(Loc) == SM.getFileLoc(Loc)));
  };
  // The tokens of the body and initializers that name types (CTypeTokens),
  // told once they are asked.
  std::optional<CTypeTokens> TypeTokens;
  auto IsTypeToken = [&](FileID File, unsigned Offset) {
    if (!TypeTokens) {
      TypeTokens.emplace(SM);
      TypeTokens->TraverseStmt(const_cast<Stmt *>(Written));
      if (const auto *Constructor = dyn_cast<CXXConstructorDecl>(Function))
        for (const CXXCtorInitializer *Initializer : Constructor->inits())
          if (Initializer->isWritten())
            TypeTokens->TraverseConstructorInitializer(
                const_cast<CXXCtorInitializer *>(Initializer));
    }
    return TypeTokens->contains(File, Offset);
  };
  // From its first to its last token, which the caller wrote, of which one
  // doesn't name a type, as the lexer reads them: not only a type an argument
  // names, `d_CParent::f_Value()` of `DDerived(CClass, CParent)`, nor an
  // implicit node at it, the constructor call of `return new d_CType;`,
  // which aren't implementation.
  auto RangeWrittenWhereUsed = [&](SourceRange Range) {
    if (!WrittenWhereUsed(Range.getBegin()) || !WrittenWhereUsed(Range.getEnd()))
      return false;
    const SourceLocation Begin = SM.getSpellingLoc(Range.getBegin());
    const SourceLocation Last = SM.getSpellingLoc(Range.getEnd());
    const FileID File = SM.getFileID(Begin);
    if (SM.getFileID(Last) != File)
      return true;
    const StringRef Buffer = SM.getBufferData(File);
    const LangOptions &LangOpts = Result.Context->getLangOpts();
    Lexer Raw(SM.getLocForStartOfFile(File), LangOpts, Buffer.begin(),
              Buffer.begin() + SM.getFileOffset(Begin), Buffer.end());
    Token Next;
    for (;;) {
      const bool Done = Raw.LexFromRawLexer(Next);
      const unsigned Offset = SM.getFileOffset(Next.getLocation());
      if (Next.is(tok::eof) || Offset > SM.getFileOffset(Last))
        return false;
      if (!IsTypeToken(File, Offset))
        return true;
      if (Done)
        return false;
    }
  };
  TCRecursive<bool(const Stmt *, int)> FindWritten = [&](const Stmt *S,
                                                           int Depth) {
    if (!S || Depth > 64 || declaresOnly(S))
      return false;
    // Not what isn't written, like the `this` of `d_CParent::f_Value()`, at
    // the type that qualifies it, nor the name of an operator an argument
    // gives, `==` of `DImplementOperator(==)`, which isn't implementation.
    if (const auto *This = dyn_cast<CXXThisExpr>(S); This && This->isImplicit())
      return false;
    if (const auto *Name = dyn_cast<DeclRefExpr>(S);
        Name && Name->getNameInfo().getName().getNameKind() ==
                    DeclarationName::CXXOperatorName)
      return false;
    if (const auto *Name = dyn_cast<OverloadExpr>(S);
        Name && Name->getName().getNameKind() ==
                    DeclarationName::CXXOperatorName)
      return false;
    if (S != Written && RangeWrittenWhereUsed(S->getSourceRange())) {
      BodyLoc = SM.getFileLoc(S->getBeginLoc());
      return true;
    }
    for (const Stmt *Child : childrenOnce(S))
      if (FindWritten(Child, Depth + 1))
        return true;
    return false;
  };
  if (BodyLoc.isMacroID())
    FindWritten(Written, 0);
  // Or the initializers of a constructor, `CExample() : m_Value(fg_Make())
  // DEmptyBody`.
  if (const auto *Constructor = dyn_cast<CXXConstructorDecl>(Function);
      Constructor && BodyLoc.isMacroID())
    for (const CXXCtorInitializer *Initializer : Constructor->inits()) {
      if (!Initializer->isWritten())
        continue;
      // Also written in an argument of the macro, `DCtor(m_Value(fg_Make()))`,
      // also only what initializes it, `DCtor(fg_Make())`.
      // Not only the base it names, `d_CParent(...)`.
      bool Written = false;
      for (const SourceRange Range :
           {Initializer->getSourceRange(),
            Initializer->getInit() ? Initializer->getInit()->getSourceRange()
                                   : SourceRange()})
        if (RangeWrittenWhereUsed(Range)) {
          BodyLoc = SM.getFileLoc(Range.getBegin());
          Written = true;
          break;
        }
      // Also a part of what initializes it, `DCtor(fg_Make())` of `m_Value(1 +
      // d_Value)`.
      if (!Written && Initializer->getInit())
        Written = FindWritten(Initializer->getInit(), 0);
      if (Written)
        break;
    }
  auto IsDeclarationFile = [&](SourceLocation Loc) {
    auto [Cached, Inserted] =
        DeclarationFiles.try_emplace(SM.getFileID(Loc), false);
    if (Inserted)
      Cached->second = llvm::is_contained(
          DeclarationFileExtensions,
          llvm::sys::path::extension(SM.getFilename(Loc)).ltrim('.'));
    return Cached->second;
  };
  // In a source, a class declares its members and the bodies follow it, like
  // a header and its `.hpp`: a member function, or a friend, defined in the
  // class definition is reported, but not one of a class local to a
  // function, also an Objective-C method or a block, whose members can't be
  // defined out of it, nor one defaulted or deleted. The source is the one
  // the body is in, also one a class includes its members from, and the body
  // is where it is reported unless its name is in the same file.
  // The body is in a source by where its braces are only: its opening brace,
  // or, when an include gives that, `#include "Opening.hpp"`, its closing
  // brace, of the last handler of a function try block; not by what is
  // written between them.
  // A file that is also one of the declaration files is left to that rule,
  // which wants no definitions in it at all.
  auto InSource = [&](SourceLocation Loc) {
    return Loc.isValid() && !SM.isInSystemHeader(Loc) &&
           !IsDeclarationFile(Loc) &&
           llvm::is_contained(
               OutOfClassFileExtensions,
               llvm::sys::path::extension(SM.getFilename(Loc)).ltrim('.'));
  };
  // The braces of the body as written, of a coroutine too, and of a function
  // try block those of its block and of its last handler, not its `try`.
  // Those written where they are used, also in an argument of a macro, not
  // what a macro's definition gives, `#define DClose }`.
  // Before what the rule of the declaration files looks at, which leaves out
  // a body a macro gives and one of a system header.
  SourceLocation Opening;
  SourceLocation Closing;
  if (const auto *Block = dyn_cast<CompoundStmt>(Written)) {
    Opening = Block->getLBracLoc();
    Closing = Block->getRBracLoc();
  } else if (const auto *Try = dyn_cast<CXXTryStmt>(Written)) {
    Opening = Try->getTryBlock()->getLBracLoc();
    if (const unsigned Handlers = Try->getNumHandlers())
      if (const auto *Last = dyn_cast_or_null<CompoundStmt>(
              Try->getHandler(Handlers - 1)->getHandlerBlock()))
        Closing = Last->getRBracLoc();
  }
  SourceLocation SourceLoc;
  if (WrittenWhereUsed(Opening) && InSource(SM.getFileLoc(Opening)))
    SourceLoc = SM.getFileLoc(Opening);
  else if (WrittenWhereUsed(Closing) && InSource(SM.getFileLoc(Closing)))
    SourceLoc = SM.getFileLoc(Closing);
  if (const auto *Record =
          dyn_cast<CXXRecordDecl>(Function->getLexicalDeclContext());
      Record && !Record->isLambda() && SourceLoc.isValid()) {
    bool Local = false;
    for (const DeclContext *Context = Record->getDeclContext(); Context;
         Context = Context->getParent())
      Local |= Context->isFunctionOrMethod();
    if (!Local) {
      const SourceLocation Loc =
          Function->getLocation().isFileID() &&
                  SM.getFileID(Function->getLocation()) ==
                      SM.getFileID(SourceLoc)
              ? Function->getLocation()
              : SourceLoc;
      diag(Loc, "%0 is defined in its class in a '.%1' file; declare it in the "
                "class and define it after it")
          << diagnosticName(Function)
          << llvm::sys::path::extension(SM.getFilename(SourceLoc)).ltrim('.');
      // And still what of it a declaration file has, which defining it
      // after the class would leave there.
    }
  }
  if (BodyLoc.isInvalid() || BodyLoc.isMacroID() || SM.isInSystemHeader(BodyLoc))
    return;
  // Also what is written of it in another file than where it opens, like the
  // statements and the closing brace in a `.h` that a body opened in a
  // `.hpp` includes.
  // Also in nested statements, `if (...) { #include "Fragment.h" }`, the
  // statements and their closing braces, and in expressions, `return 1 +
  // #include "Fragment.h"`.
  if (!IsDeclarationFile(BodyLoc)) {
    const FileID BodyFile = SM.getFileID(BodyLoc);
    auto InDeclarationFile = [&](SourceLocation Loc) {
      if (Loc.isInvalid())
        return false;
      Loc = SM.getFileLoc(Loc);
      if (SM.getFileID(Loc) == BodyFile || SM.isInSystemHeader(Loc) ||
          !IsDeclarationFile(Loc))
        return false;
      BodyLoc = Loc;
      return true;
    };
    // In the order they are written, the closing brace of the body last,
    // after the initializers of a constructor, `CExample() : m_Value(42)`
    // in a `.h` that includes its body.
    // The initializers are looked into like the body, before it, also
    // inside, `m_Value(1 + #include "Fragment.h" + 3)`.
    llvm::SmallVector<const Stmt *, 16> Worklist{Written};
    bool Found = false;
    if (const auto *Constructor = dyn_cast<CXXConstructorDecl>(Function)) {
      llvm::SmallVector<const Stmt *, 4> Initializers;
      for (const CXXCtorInitializer *Initializer : Constructor->inits()) {
        if (!Initializer->isWritten())
          continue;
        if (InDeclarationFile(Initializer->getSourceLocation())) {
          Found = true;
          break;
        }
        if (Initializer->getInit())
          Initializers.push_back(Initializer->getInit());
      }
      Worklist.append(Initializers.rbegin(), Initializers.rend());
    }
    while (!Worklist.empty() && !Found) {
      const Stmt *S = Worklist.pop_back_val();
      // Not declarations only, like the types a `.h` included in the body
      // defines, `struct CLocal { ... };`, which aren't implementation.
      if (declaresOnly(S))
        continue;
      // What one invocation of a macro writes, which can't include a file,
      // is written where it is invoked, nested invocations too: told there,
      // without going into what it expands to, which deeply nested ones,
      // `DAwait(DAwait(...))`, would otherwise go through again for each.
      if (const SourceLocation Begin = S->getBeginLoc(), End = S->getEndLoc();
          S != Written && Begin.isMacroID() && End.isMacroID()) {
        const SourceLocation Invoked = SM.getExpansionLoc(Begin);
        if (Invoked == SM.getExpansionLoc(End)) {
          if (InDeclarationFile(Invoked))
            Found = true;
          continue;
        }
      }
      // Also the closing brace of a nested block, `if (...) {` closed in an
      // included `.h`.
      // Also in an expression, `1 + #include "Fragment.h"`, which an include
      // can be in too, also at its end, like a postfix `++`, or its operator,
      // `1 #include "Plus.h" 2`, also of a conditional.
      const auto *E = dyn_cast<Expr>(S);
      const auto *Conditional = dyn_cast<ConditionalOperator>(S);
      if (S != Written &&
          (InDeclarationFile(S->getBeginLoc()) ||
           (isa<CompoundStmt, Expr>(S) && InDeclarationFile(S->getEndLoc())) ||
           (E && InDeclarationFile(E->getExprLoc())) ||
           (Conditional && (InDeclarationFile(Conditional->getQuestionLoc()) ||
                            InDeclarationFile(Conditional->getColonLoc())))))
        Found = true;
      else {
        const size_t First = Worklist.size();
        for (const Stmt *Child : childrenOnce(S))
          if (Child)
            Worklist.push_back(Child);
        std::reverse(Worklist.begin() + First, Worklist.end());
      }
    }
    if (!Found)
      InDeclarationFile(Body->getEndLoc());
  }
  if (!IsDeclarationFile(BodyLoc))
    return;
  const FileID File = SM.getFileID(BodyLoc);
  const StringRef Extension =
      llvm::sys::path::extension(SM.getFilename(BodyLoc)).ltrim('.');

  // At the name, unless that is in another file than the body, like a
  // signature in an included file that the body follows.
  const SourceLocation Loc = Function->getLocation().isFileID() &&
                                     SM.getFileID(Function->getLocation()) ==
                                         File
                                 ? Function->getLocation()
                                 : BodyLoc;
  // Where Malterlib defines functions, as far as DeclarationFileExtensions
  // doesn't make those files declarations only too.
  const bool Hpp = !llvm::is_contained(DeclarationFileExtensions, "hpp");
  const bool Cpp = !llvm::is_contained(DeclarationFileExtensions, "cpp");
  diag(Loc, "%0 is defined in a '.%1' file, which holds declarations only; "
            "define it in %select{a file that holds definitions|a '.hpp' "
            "file|a '.cpp' file|a '.hpp' or '.cpp' file}2")
      << diagnosticName(Function) << Extension << (Hpp + 2 * Cpp);
}

} // namespace clang::tidy::malterlib
