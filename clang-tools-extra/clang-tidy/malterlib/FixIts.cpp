//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "FixIts.h"
#include "clang/Lex/Lexer.h"
#include "llvm/ADT/STLExtras.h"

namespace clang::tidy::malterlib {

void CPreprocessing::clear() {
  Directives.clear();
  Expansions.clear();
  Includes.clear();
}

void CPreprocessingRecorder::MacroExpands(const Token &Name,
                                          const MacroDefinition &, SourceRange,
                                          const MacroArgs *) {
  if (Name.getLocation().isFileID())
    Record.Expansions.push_back(Name.getLocation());
}

// Also an include, which in another configuration can give what a guard
// skips here.
void CPreprocessingRecorder::InclusionDirective(
    SourceLocation Hash, const Token &, StringRef, bool, CharSourceRange,
    OptionalFileEntryRef, StringRef, StringRef, const Module *, bool,
    SrcMgr::CharacteristicKind) {
  if (!Hash.isFileID())
    return;
  Record.Expansions.push_back(Hash);
  Record.Includes.push_back(Hash);
}

void CPreprocessingRecorder::If(SourceLocation Loc, SourceRange,
                                ConditionValueKind) {
  Record.Directives.push_back(Loc);
}

void CPreprocessingRecorder::Elif(SourceLocation Loc, SourceRange,
                                  ConditionValueKind, SourceLocation) {
  Record.Directives.push_back(Loc);
}

void CPreprocessingRecorder::Ifdef(SourceLocation Loc, const Token &,
                                   const MacroDefinition &) {
  Record.Directives.push_back(Loc);
}

void CPreprocessingRecorder::Ifndef(SourceLocation Loc, const Token &,
                                    const MacroDefinition &) {
  Record.Directives.push_back(Loc);
}

void CPreprocessingRecorder::Elifdef(SourceLocation Loc, const Token &,
                                     const MacroDefinition &) {
  Record.Directives.push_back(Loc);
}

void CPreprocessingRecorder::Elifdef(SourceLocation Loc, SourceRange,
                                     SourceLocation) {
  Record.Directives.push_back(Loc);
}

void CPreprocessingRecorder::Elifndef(SourceLocation Loc, const Token &,
                                      const MacroDefinition &) {
  Record.Directives.push_back(Loc);
}

void CPreprocessingRecorder::Elifndef(SourceLocation Loc, SourceRange,
                                      SourceLocation) {
  Record.Directives.push_back(Loc);
}

void CPreprocessingRecorder::Else(SourceLocation Loc, SourceLocation) {
  Record.Directives.push_back(Loc);
}

void CPreprocessingRecorder::Endif(SourceLocation Loc, SourceLocation) {
  Record.Directives.push_back(Loc);
}

// The preprocessor reports them in the order of the translation unit, which
// a binary search finds the first at or after \p Begin in.
bool hasLocationBetween(llvm::ArrayRef<SourceLocation> Locations,
                        SourceLocation Begin, SourceLocation End,
                        const SourceManager &SM) {
  const SourceLocation *First =
      llvm::partition_point(Locations, [&](SourceLocation Location) {
        return SM.isBeforeInTranslationUnit(Location, Begin);
      });
  return First != Locations.end() && SM.isBeforeInTranslationUnit(*First, End);
}

std::optional<CharSourceRange>
CFixIts::tokens(SourceLocation First, SourceLocation Last, FileID File,
                const SourceManager &SM, const LangOptions &LangOpts) const {
  if (First.isInvalid() || Last.isInvalid() || !First.isFileID() ||
      !Last.isFileID())
    return std::nullopt;
  const SourceLocation End = Lexer::getLocForEndOfToken(Last, 0, SM, LangOpts);
  if (End.isInvalid())
    return std::nullopt;
  const CharSourceRange Range = CharSourceRange::getCharRange(First, End);
  if (!writtenBetween(Range, File, SM, LangOpts))
    return std::nullopt;
  return Range;
}

/// Whether the text from \p BeginOffset to \p EndOffset of \p File has a
/// preprocessing directive of any kind, a line whose first token is `#`, also
/// after a comment, `/* Layout */ #define`, or a pragma operator,
/// `_Pragma("pack(1)")`: by the tokens as the lexer reads them from the start
/// of the first line, not in a comment or a literal.
static bool hasDirective(FileID File, unsigned BeginOffset, unsigned EndOffset,
                         const SourceManager &SM,
                         const LangOptions &LangOpts) {
  bool Invalid = false;
  const StringRef Buffer = SM.getBufferData(File, &Invalid);
  if (Invalid || EndOffset > Buffer.size())
    return true;
  unsigned LineStart = BeginOffset;
  while (LineStart > 0 && Buffer[LineStart - 1] != '\n' &&
         Buffer[LineStart - 1] != '\r')
    --LineStart;
  Lexer Raw(SM.getLocForStartOfFile(File), LangOpts, Buffer.begin(),
            Buffer.begin() + LineStart, Buffer.end());
  Token Next;
  for (;;) {
    const bool Last = Raw.LexFromRawLexer(Next);
    if (Next.is(tok::eof) || SM.getFileOffset(Next.getLocation()) >= EndOffset)
      return false;
    if (Next.is(tok::hash) && Next.isAtStartOfLine())
      return true;
    if (Next.is(tok::raw_identifier) &&
        llvm::is_contained({"_Pragma", "__pragma"}, Next.getRawIdentifier()))
      return true;
    if (Last)
      return false;
  }
}

bool CFixIts::writtenBetween(CharSourceRange Range, FileID File,
                             const SourceManager &SM,
                             const LangOptions &LangOpts) const {
  const SourceLocation Begin = Range.getBegin();
  const SourceLocation End = Range.getEnd();
  if (!Range.isCharRange() || Begin.isInvalid() || End.isInvalid() ||
      !Begin.isFileID() || !End.isFileID() || SM.getFileID(Begin) != File ||
      SM.getFileID(End) != File ||
      SM.getFileOffset(End) < SM.getFileOffset(Begin))
    return false;
  return !hasLocationBetween(Preprocessing.Directives, Begin, End, SM) &&
         !hasLocationBetween(Preprocessing.Expansions, Begin, End, SM) &&
         !hasDirective(File, SM.getFileOffset(Begin), SM.getFileOffset(End), SM,
                       LangOpts);
}

std::optional<FixItHint> CFixIts::removal(CharSourceRange Range, FileID File,
                                          const SourceManager &SM,
                                          const LangOptions &LangOpts) const {
  if (!writtenBetween(Range, File, SM, LangOpts))
    return std::nullopt;
  return FixItHint::CreateRemoval(Range);
}

/// Whether the physical line before the one that starts at \p LineStart of
/// \p Buffer, just after its line feed, ends in a line splice, a backslash,
/// also with whitespace or a carriage return before the line feed, or the
/// trigraph of one, which makes the two one logical line.
static bool continuesLine(StringRef Buffer, unsigned LineStart,
                          const LangOptions &LangOpts) {
  if (LineStart == 0)
    return false;
  unsigned At = LineStart - 1;
  if (At > 0 && Buffer[At - 1] == '\r')
    --At;
  while (At > 0 && (Buffer[At - 1] == ' ' || Buffer[At - 1] == '\t' ||
                    Buffer[At - 1] == '\v' || Buffer[At - 1] == '\f'))
    --At;
  if (At > 0 && Buffer[At - 1] == '\\')
    return true;
  return LangOpts.Trigraphs && At >= 3 && Buffer.slice(At - 3, At) == "?\?/";
}

std::optional<FixItHint>
CFixIts::tokenRemoval(CharSourceRange Range, FileID File,
                      const SourceManager &SM,
                      const LangOptions &LangOpts) const {
  if (!writtenBetween(Range, File, SM, LangOpts))
    return std::nullopt;
  const StringRef Text =
      SM.getBufferData(File).slice(SM.getFileOffset(Range.getBegin()),
                                   SM.getFileOffset(Range.getEnd()));
  // The line breaks, not the line feeds of the splices among the tokens,
  // `public\<newline>:`, which end no line.
  std::string Breaks;
  for (size_t Index = 0; Index < Text.size(); ++Index)
    if (Text[Index] == '\n' && !continuesLine(Text, Index + 1, LangOpts))
      Breaks += Index > 0 && Text[Index - 1] == '\r' ? "\r\n" : "\n";
  if (Breaks.empty())
    return FixItHint::CreateRemoval(Range);
  return FixItHint::CreateReplacement(Range, Breaks);
}

std::optional<FixItHint>
CFixIts::statementRemoval(CharSourceRange Range, FileID File,
                          const SourceManager &SM,
                          const LangOptions &LangOpts) const {
  if (!writtenBetween(Range, File, SM, LangOpts))
    return std::nullopt;
  const StringRef Buffer = SM.getBufferData(File);
  unsigned LineStart = SM.getFileOffset(Range.getBegin());
  while (LineStart > 0 &&
         (Buffer[LineStart - 1] == ' ' || Buffer[LineStart - 1] == '\t'))
    --LineStart;
  unsigned End = SM.getFileOffset(Range.getEnd());
  while (End < Buffer.size() && (Buffer[End] == ' ' || Buffer[End] == '\t'))
    ++End;
  const bool BeginsLogicalLine =
      LineStart == 0 || (Buffer[LineStart - 1] == '\n' &&
                         !continuesLine(Buffer, LineStart, LangOpts));
  const bool EndsLine =
      End == Buffer.size() || Buffer[End] == '\n' ||
      (Buffer[End] == '\r' && End + 1 < Buffer.size() &&
       Buffer[End + 1] == '\n');
  if (BeginsLogicalLine && EndsLine) {
    if (End < Buffer.size())
      End += Buffer[End] == '\r' ? 2 : 1;
    const SourceLocation FileStart = SM.getLocForStartOfFile(File);
    return removal(
        CharSourceRange::getCharRange(FileStart.getLocWithOffset(LineStart),
                                      FileStart.getLocWithOffset(End)),
        File, SM, LangOpts);
  }
  return tokenRemoval(Range, File, SM, LangOpts);
}

std::optional<FixItHint>
CFixIts::replacement(CharSourceRange Range, StringRef Text, FileID File,
                     const SourceManager &SM,
                     const LangOptions &LangOpts) const {
  if (!writtenBetween(Range, File, SM, LangOpts))
    return std::nullopt;
  return FixItHint::CreateReplacement(Range, Text);
}

std::optional<std::string> CFixIts::spelling(SourceLocation Loc,
                                             const SourceManager &SM,
                                             const LangOptions &LangOpts) {
  if (Loc.isInvalid() || !Loc.isFileID())
    return std::nullopt;
  Token Read;
  if (Lexer::getRawToken(Loc, Read, SM, LangOpts))
    return std::nullopt;
  bool Invalid = false;
  std::string Spelling = Lexer::getSpelling(Read, SM, LangOpts, &Invalid);
  if (Invalid)
    return std::nullopt;
  return Spelling;
}

} // namespace clang::tidy::malterlib
