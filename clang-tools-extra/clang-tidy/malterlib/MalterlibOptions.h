//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_MALTERLIBOPTIONS_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_MALTERLIBOPTIONS_H

#include "clang/Basic/LLVM.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/VirtualFileSystem.h"
#include <memory>
#include <string>
#include <vector>

namespace clang::tidy::malterlib {

/// The option of a configuration that names the file of the Malterlib
/// options, which a configuration that enables a Malterlib check must set,
/// like `malterlib.OptionsFile: ../Core/ClangTidyOptions.yaml` in the
/// `.clang-tidy` of a module, relative to the directory of that file.
inline constexpr llvm::StringLiteral OptionsFileOption =
    "malterlib.OptionsFile";

/// The options of a Malterlib options file, by name, a list joined by `;`.
using COptionsFile = llvm::StringMap<std::string>;

/// The options of the file at \p Path, of the file system \p FS, or else of
/// the real one, read again when it changes. Null, with \p Error set, when it
/// can't be read or isn't a mapping of names to strings or sequences of
/// strings.
std::shared_ptr<const COptionsFile>
loadOptionsFile(StringRef Path, std::string &Error,
                llvm::vfs::FileSystem *FS = nullptr);

/// The list \p Base, `;`-separated, edited by \p Edit: each entry of \p Edit
/// is added unless the list has it, `-Name` removes Name, and `-*` removes all
/// the entries before it.
std::string editList(StringRef Base, StringRef Edit);

/// A list of names, kept with views of them.
class CNameList {
public:
  CNameList() = default;
  explicit CNameList(StringRef List);
  CNameList(const CNameList &) = delete;
  CNameList &operator=(const CNameList &) = delete;

  bool contains(StringRef Name) const { return Set.contains(Name); }
  llvm::ArrayRef<StringRef> names() const { return Refs; }
  /// The list joined by `;`.
  StringRef joined() const { return Joined; }

private:
  std::string Joined;
  std::vector<std::string> Names;
  llvm::SmallVector<StringRef, 16> Refs;
  llvm::StringSet<> Set;
};

/// The names that the analyses the checks share know, of the options of the
/// configuration of the translation unit: what the language and the standard
/// library have, edited by the options file and the configuration.
struct CVocabulary {
  /// Each list by \p Resolve of its option name and its built-in names.
  explicit CVocabulary(
      llvm::function_ref<std::string(StringRef Name, StringRef BuiltIn)>
          Resolve);

  /// The names the language and the standard library have of the list of
  /// the option \p Name, the base of a check's own option of that name.
  static StringRef builtIn(StringRef Name);

  /// Calls \p Each with the name of the option of each list, and the list.
  void forEachList(
      llvm::function_ref<void(StringRef Name, const CNameList &List)> Each)
      const;

  /// The functions that move what they are given, `std::move` and
  /// `fg_Move`, by unqualified name.
  CNameList MoveFunctions;
  /// Those that forward it, `std::forward` and `fg_Forward`.
  CNameList ForwardFunctions;
  /// Those that exchange a value with another and give the old one,
  /// `std::exchange` and `fg_Exchange`.
  CNameList ExchangeFunctions;
  /// Those that swap two values, `std::swap` and `fg_Swap`.
  CNameList SwapFunctions;
  /// Those that give a copy of what they are given, `fg_TempCopy`.
  CNameList CopyFunctions;
  /// MoveFunctions and ForwardFunctions.
  CNameList MoveOrForwardFunctions;
  /// The callees that keep a closure alive while it runs, SafeCallees.
  CNameList SafeCallees;
  /// The classes whose member functions run a closure to completion before
  /// they return, BlockingCallees.
  CNameList BlockingCallees;
  /// The types that refer to data they don't own, ViewTypes.
  CNameList ViewTypes;
  /// The member functions that remove entries of a collection,
  /// RemovingMethods.
  CNameList RemovingMethods;
  /// The types that are named like a language's built-in types, which the
  /// naming checks take as such, BuiltinTypes.
  CNameList BuiltinTypes;
};

/// The vocabulary of the configuration of the checks that run on this thread,
/// which each check sets when it is made: the checks of a translation unit
/// share its configuration. Built-in names until a check sets one.
const CVocabulary &vocabulary();

/// Makes \p Vocabulary the one of this thread.
void setVocabulary(std::shared_ptr<const CVocabulary> Vocabulary);

/// Whether \p Name is that of a function that moves, forwards, exchanges or
/// swaps, of the vocabulary.
bool isMoveFunction(StringRef Name);
bool isForwardFunction(StringRef Name);
bool isExchangeFunction(StringRef Name);
bool isSwapFunction(StringRef Name);
bool isCopyFunction(StringRef Name);
bool isMoveOrForwardFunction(StringRef Name);

} // namespace clang::tidy::malterlib

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_MALTERLIBOPTIONS_H
