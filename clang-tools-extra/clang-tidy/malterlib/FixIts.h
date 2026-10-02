//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_FIXITS_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_FIXITS_H

#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/PPCallbacks.h"
#include "llvm/ADT/SmallVector.h"
#include <optional>
#include <string>

namespace clang {
class Preprocessor;
} // namespace clang

namespace clang::tidy::malterlib {

/// Where the preprocessing of a translation unit happens in its files: the
/// conditional directives, the uses of macros, also those that expand to
/// nothing, and the includes, each in the order of the translation unit.
struct CPreprocessing {
  /// Where the conditional directives are, `#ifdef`, also after a comment,
  /// `/* Layout */ #ifdef`.
  llvm::SmallVector<SourceLocation, 8> Directives;
  /// Where macros are used in files, and where files are included, also
  /// those a guard skips.
  llvm::SmallVector<SourceLocation, 64> Expansions;
  /// Where files are included, in files.
  llvm::SmallVector<SourceLocation, 16> Includes;

  /// Clears what was recorded, at the start of a translation unit.
  void clear();
};

/// Records the preprocessing of a translation unit into a CPreprocessing.
class CPreprocessingRecorder : public PPCallbacks {
public:
  explicit CPreprocessingRecorder(CPreprocessing &TheRecord)
      : Record(TheRecord) {}
  void MacroExpands(const Token &Name, const MacroDefinition &Definition,
                    SourceRange Range, const MacroArgs *Arguments) override;
  void InclusionDirective(SourceLocation Hash, const Token &, StringRef, bool,
                          CharSourceRange, OptionalFileEntryRef, StringRef,
                          StringRef, const Module *, bool,
                          SrcMgr::CharacteristicKind) override;
  void If(SourceLocation Loc, SourceRange, ConditionValueKind) override;
  void Elif(SourceLocation Loc, SourceRange, ConditionValueKind,
            SourceLocation) override;
  void Ifdef(SourceLocation Loc, const Token &,
             const MacroDefinition &) override;
  void Ifndef(SourceLocation Loc, const Token &,
              const MacroDefinition &) override;
  void Elifdef(SourceLocation Loc, const Token &,
               const MacroDefinition &) override;
  void Elifdef(SourceLocation Loc, SourceRange, SourceLocation) override;
  void Elifndef(SourceLocation Loc, const Token &,
                const MacroDefinition &) override;
  void Elifndef(SourceLocation Loc, SourceRange, SourceLocation) override;
  void Else(SourceLocation Loc, SourceLocation) override;
  void Endif(SourceLocation Loc, SourceLocation) override;

private:
  CPreprocessing &Record;
};

/// Whether a location of \p Locations, recorded in the order of the
/// translation unit, is after \p Begin and before \p End.
bool hasLocationBetween(llvm::ArrayRef<SourceLocation> Locations,
                        SourceLocation Begin, SourceLocation End,
                        const SourceManager &SM);

/// The edits of the fixes the checks offer, every one made here: of tokens
/// written in one file, as the lexer reads them, a line splice in a token
/// being part of it, `public\<newline>:`, never a location and a length.
/// An edit is refused, which withholds the fix and keeps the diagnostic,
/// where what it covers isn't all written in that file: where a token comes
/// from a macro, or a macro is used, a file included or a conditional
/// directive is in it (Preprocessing), which another configuration can give
/// otherwise, or where it would edit a preprocessing directive of any kind,
/// which the text tells.
class CFixIts {
public:
  explicit CFixIts(const CPreprocessing &ThePreprocessing)
      : Preprocessing(ThePreprocessing) {}

  /// The characters from the start of the token at \p First through the end
  /// of the token at \p Last, as the lexer reads them, when both are written
  /// in \p File and nothing between them comes from the preprocessing
  /// (writtenBetween).
  std::optional<CharSourceRange>
  tokens(SourceLocation First, SourceLocation Last, FileID File,
         const SourceManager &SM, const LangOptions &LangOpts) const;

  /// Whether \p Range is in one file, \p File, with no use of a macro, include
  /// or conditional directive in it or at its ends, nor a preprocessing
  /// directive of any kind in its text: a line whose first token is `#`,
  /// `#define`, `#pragma` or any other, or a pragma operator, `_Pragma`.
  bool writtenBetween(CharSourceRange Range, FileID File,
                      const SourceManager &SM,
                      const LangOptions &LangOpts) const;

  /// The fix that removes \p Range, which written tokens and what lies
  /// between them, like whitespace, make up (writtenBetween), or none.
  std::optional<FixItHint> removal(CharSourceRange Range, FileID File,
                                   const SourceManager &SM,
                                   const LangOptions &LangOpts) const;

  /// The fix that removes the tokens \p Range covers and what lies between
  /// them (removal), keeping each line break among them, so that no two lines
  /// are joined: the end of a line that a splice continues, the line of a
  /// directive.
  std::optional<FixItHint> tokenRemoval(CharSourceRange Range, FileID File,
                                        const SourceManager &SM,
                                        const LangOptions &LangOpts) const;
  /// The fix that removes the statement whose tokens through its semicolon
  /// \p Range covers: with its whole lines where it is alone on them, its
  /// first line beginning a logical line (the physical line before doesn't
  /// end in a line splice) and its last line ending in a line break of its
  /// own or the end of the file; otherwise its tokens only (tokenRemoval).
  std::optional<FixItHint> statementRemoval(CharSourceRange Range,
                                            FileID File,
                                            const SourceManager &SM,
                                            const LangOptions &LangOpts) const;
  /// The fix that replaces \p Range with \p Text (writtenBetween), or none.
  std::optional<FixItHint> replacement(CharSourceRange Range, StringRef Text,
                                       FileID File, const SourceManager &SM,
                                       const LangOptions &LangOpts) const;

  /// The token at \p Loc as the compiler reads it, without the line splices
  /// it is written with, `cla\<newline>ss` read as `class`; none when \p Loc
  /// isn't in a file.
  static std::optional<std::string> spelling(SourceLocation Loc,
                                             const SourceManager &SM,
                                             const LangOptions &LangOpts);

private:
  const CPreprocessing &Preprocessing;
};

} // namespace clang::tidy::malterlib

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_FIXITS_H
