//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MacroNamingCheck.h"
#include "NamingUtils.h"
#include "clang/Lex/Lexer.h"
#include "clang/Lex/MacroInfo.h"
#include "clang/Lex/PPCallbacks.h"
#include "clang/Lex/Preprocessor.h"
#include "llvm/ADT/StringExtras.h"

namespace clang::tidy::malterlib {
namespace {

/// Whether \p Rest is written in capitals: without lower case letters, and
/// with more capitals than the acronyms of upper camel case, like `ID`.
static bool inCapitals(StringRef Rest) {
  return llvm::none_of(Rest, llvm::isLower) &&
         llvm::count_if(Rest, llvm::isUpper) >= 3;
}

class MacroNamingCallbacks : public PPCallbacks {
public:
  MacroNamingCallbacks(MacroNamingCheck &Check, const SourceManager &SM,
                       const LangOptions &LangOpts)
      : Check(Check), SM(SM), LangOpts(LangOpts) {}

  void MacroDefined(const Token &MacroNameTok,
                    const MacroDirective *MD) override {
    const SourceLocation Loc = MacroNameTok.getLocation();
    if (Loc.isInvalid() || !Loc.isFileID() || SM.isInSystemHeader(Loc) ||
        SM.isWrittenInBuiltinFile(Loc) || SM.isWrittenInCommandLineFile(Loc))
      return;

    const StringRef Name = MacroNameTok.getIdentifierInfo()->getName();
    // Lower case names emulate language features (`inline_always`), but their
    // parameters are named like others. A name in capitals, also after the
    // prefix, `AVLKEY` or `DESKTOP_ALL`, is no upper camel case; one that
    // configures external code, `NOMINMAX` or `_GNU_SOURCE`, has a NOLINT.
    if (llvm::any_of(Name, llvm::isUpper) &&
        (!hasTypePrefix(Name, {"D"}) || inCapitals(Name.drop_front(1))))
      Check.diag(Loc, "macro '%0' should have the 'D' prefix followed by upper "
                      "camel case")
          << Name;

    const MacroInfo *Info = MD->getMacroInfo();
    if (!Info->isFunctionLike())
      return;
    for (const IdentifierInfo *Parameter : Info->params()) {
      const StringRef ParameterName = Parameter->getName();
      if (ParameterName == "__VA_ARGS__")
        continue;
      // Type prefixes (`d_CType`) are upper camel case too.
      // After a concept prefix, or a type prefix, `X` of `d_CX`.
      StringRef Named = ParameterName.starts_with("d_")
                            ? withoutConcept(ParameterName.drop_front(2))
                            : StringRef();
      for (const StringRef TypePrefix : {"TIC", "TC", "TF", "IC", "C", "F", "E"})
        if (hasTypePrefix(Named, {TypePrefix})) {
          Named = Named.drop_front(TypePrefix.size());
          break;
        }
      // But not a name in capitals after them, `d_TYPE`.
      if (ParameterName.starts_with("d_") &&
          isUpperCamelWithConcepts(ParameterName.drop_front(2)) &&
          !inCapitals(Named)) {
        if (Named.size() < Check.MinimumNameLength)
          Check.diag(parameterLocation(Loc, ParameterName),
                     "parameter '%0' of macro '%1' should have at least %2 "
                     "characters after its prefixes")
              << ParameterName << Name << Check.MinimumNameLength;
        continue;
      }
      Check.diag(parameterLocation(Loc, ParameterName), "parameter '%0' of "
                 "macro '%1' should have the 'd_' prefix followed by upper "
                 "camel case")
          << ParameterName << Name;
    }
  }

private:
  /// Where the parameter \p Parameter is declared, in the parentheses after
  /// the macro name at \p NameLoc, or the name when it can't be found. The
  /// parameters are lexed, so comments between them are skipped.
  SourceLocation parameterLocation(SourceLocation NameLoc,
                                   StringRef Parameter) const {
    const FileID File = SM.getFileID(NameLoc);
    bool Invalid = false;
    const StringRef Buffer = SM.getBufferData(File, &Invalid);
    const char *Start = SM.getCharacterData(NameLoc, &Invalid);
    if (Invalid)
      return NameLoc;
    Lexer Raw(SM.getLocForStartOfFile(File), LangOpts, Buffer.begin(), Start,
              Buffer.end());
    Token Tok;
    // The name, then the parentheses.
    Raw.LexFromRawLexer(Tok);
    if (Raw.LexFromRawLexer(Tok) || !Tok.is(tok::l_paren))
      return NameLoc;
    while (!Raw.LexFromRawLexer(Tok) && !Tok.isOneOf(tok::r_paren, tok::eof)) {
      // Also spelled across a line splice, `Wro\<newline>ng`.
      if (Tok.is(tok::raw_identifier) &&
          (Tok.getRawIdentifier() == Parameter ||
           (Tok.needsCleaning() &&
            Lexer::getSpelling(Tok, SM, LangOpts) == Parameter))) {
        // Where its name starts, after line splices before it, `(\<newline>
        // Wrong)`.
        SourceLocation Loc = Tok.getLocation();
        const char *Data = SM.getCharacterData(Loc);
        while (Data[0] == '\\') {
          unsigned Size = 1;
          while (Data[Size] == ' ' || Data[Size] == '\t')
            ++Size;
          if (Data[Size] == '\r')
            ++Size;
          if (Data[Size] != '\n')
            break;
          ++Size;
          Data += Size;
          Loc = Loc.getLocWithOffset(Size);
        }
        return Loc;
      }
    }
    return NameLoc;
  }

  MacroNamingCheck &Check;
  const SourceManager &SM;
  const LangOptions &LangOpts;
};

} // namespace

void MacroNamingCheck::registerPPCallbacks(const SourceManager &SM,
                                           Preprocessor *PP,
                                           Preprocessor *ModuleExpanderPP) {
  PP->addPPCallbacks(
      std::make_unique<MacroNamingCallbacks>(*this, SM, PP->getLangOpts()));
}

} // namespace clang::tidy::malterlib
