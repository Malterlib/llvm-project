//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MalterlibOptions.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/YAMLParser.h"
#include <mutex>

namespace clang::tidy::malterlib {

namespace {

/// The scalar of \p Node, or none.
std::optional<std::string> scalarOf(llvm::yaml::Node *Node) {
  if (auto *Scalar = dyn_cast_or_null<llvm::yaml::ScalarNode>(Node)) {
    llvm::SmallString<64> Storage;
    return Scalar->getValue(Storage).str();
  }
  // Also a block scalar, `Name: >-` followed by the string.
  if (auto *Block = dyn_cast_or_null<llvm::yaml::BlockScalarNode>(Node))
    return Block->getValue().str();
  return std::nullopt;
}

/// Reads the options of \p Text, a mapping of names to strings, `Name: 'A;B'`,
/// or to sequences of strings, `Name: [A, B]`, which are joined by `;`.
std::shared_ptr<COptionsFile> parseOptionsFile(StringRef Text, StringRef Path,
                                               std::string &Error) {
  llvm::SourceMgr Manager;
  std::string Message;
  Manager.setDiagHandler(
      [](const llvm::SMDiagnostic &Diagnostic, void *Context) {
        auto &Out = *static_cast<std::string *>(Context);
        if (Out.empty())
          Out = Diagnostic.getMessage().str();
      },
      &Message);
  llvm::yaml::Stream Stream(Text, Manager);
  auto Options = std::make_shared<COptionsFile>();
  auto ParseError = [&] {
    Error = "can't parse '" + Path.str() + "'" +
            (Message.empty() ? std::string() : ": " + Message);
    return nullptr;
  };
  auto NotMapping = [&] {
    Error = "'" + Path.str() + "' is not a mapping of option names to values";
    return nullptr;
  };
  // Each document to its end, whose errors the parser finds as it reads it.
  // An empty one, or only comments, is none either: a mapping without
  // options is `{}`.
  unsigned Documents = 0;
  for (llvm::yaml::Document &Document : Stream) {
    if (++Documents > 1)
      continue;
    llvm::yaml::Node *Root = Document.getRoot();
    auto *Mapping = dyn_cast_or_null<llvm::yaml::MappingNode>(Root);
    if (!Mapping) {
      if (!Message.empty() || Stream.failed())
        return ParseError();
      return NotMapping();
    }
    for (llvm::yaml::KeyValueNode &Entry : *Mapping) {
      const std::optional<std::string> Name = scalarOf(Entry.getKey());
      if (!Name) {
        if (!Message.empty() || Stream.failed())
          return ParseError();
        Error = "an option name in '" + Path.str() + "' is not a string";
        return nullptr;
      }
      std::string Value;
      if (std::optional<std::string> Scalar = scalarOf(Entry.getValue()))
        Value = std::move(*Scalar);
      else if (auto *Sequence = dyn_cast_or_null<llvm::yaml::SequenceNode>(
                   Entry.getValue())) {
        for (llvm::yaml::Node &Item : *Sequence) {
          const std::optional<std::string> Element = scalarOf(&Item);
          if (!Element) {
            if (!Message.empty() || Stream.failed())
              return ParseError();
            Error = "the option '" + *Name + "' in '" + Path.str() +
                    "' has an element that is not a string";
            return nullptr;
          }
          if (!Value.empty())
            Value += ';';
          Value += *Element;
        }
      } else {
        // Nor none, like a stray line read as a name, `[A]\n  B`; an empty
        // list is `[]`.
        if (!Message.empty() || Stream.failed())
          return ParseError();
        Error = "the option '" + *Name + "' in '" + Path.str() +
                "' is not a string or a sequence of strings";
        return nullptr;
      }
      if (!Options->try_emplace(*Name, std::move(Value)).second) {
        if (!Message.empty() || Stream.failed())
          return ParseError();
        Error =
            "the option '" + *Name + "' is given twice in '" + Path.str() + "'";
        return nullptr;
      }
    }
  }
  if (!Message.empty() || Stream.failed())
    return ParseError();
  if (Documents > 1) {
    Error = "'" + Path.str() + "' has more than one YAML document";
    return nullptr;
  }
  if (Documents == 0)
    return NotMapping();
  return Options;
}

thread_local std::shared_ptr<const CVocabulary> CurrentVocabulary;

} // namespace

std::shared_ptr<const COptionsFile>
loadOptionsFile(StringRef Path, std::string &Error, llvm::vfs::FileSystem *FS) {
  llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> Real;
  if (!FS) {
    Real = llvm::vfs::getRealFileSystem();
    FS = Real.get();
  }
  // Each file once while it stays the same, as the many threads clangd runs
  // read the same files, and again when it changes, which clangd then sees in
  // the next parse of a file.
  struct CLoaded {
    llvm::sys::fs::UniqueID ID;
    llvm::sys::TimePoint<> Modified;
    uint64_t Size = 0;
    std::shared_ptr<const COptionsFile> Options;
    std::string Error;
  };
  static std::mutex Mutex;
  static llvm::StringMap<CLoaded> Loaded;
  llvm::ErrorOr<llvm::vfs::Status> Status = FS->status(Path);
  if (!Status) {
    Error = "can't read the Malterlib options file '" + Path.str() +
            "': " + Status.getError().message();
    return nullptr;
  }
  std::lock_guard<std::mutex> Lock(Mutex);
  auto [It, Inserted] = Loaded.try_emplace(Path);
  CLoaded &File = It->second;
  if (Inserted || File.ID != Status->getUniqueID() ||
      File.Modified != Status->getLastModificationTime() ||
      File.Size != Status->getSize()) {
    File.ID = Status->getUniqueID();
    File.Modified = Status->getLastModificationTime();
    File.Size = Status->getSize();
    File.Options = nullptr;
    File.Error.clear();
    llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> Text =
        FS->getBufferForFile(Path);
    if (!Text)
      File.Error = "can't read the Malterlib options file '" + Path.str() +
                   "': " + Text.getError().message();
    else
      File.Options = parseOptionsFile((*Text)->getBuffer(), Path, File.Error);
    // A read that failed is tried again next time.
    if (!Text)
      File.Size = ~uint64_t(0);
  }
  Error = File.Error;
  return File.Options;
}

std::string editList(StringRef Base, StringRef Edit) {
  llvm::SmallVector<StringRef, 32> Entries;
  Base.split(Entries, ';', -1, /*KeepEmpty=*/false);
  llvm::SmallVector<StringRef, 16> Edits;
  Edit.split(Edits, ';', -1, /*KeepEmpty=*/false);
  for (StringRef Entry : Edits) {
    Entry = Entry.trim();
    if (Entry.empty())
      continue;
    if (Entry == "-*")
      Entries.clear();
    else if (Entry.consume_front("-"))
      llvm::erase(Entries, Entry);
    else if (!llvm::is_contained(Entries, Entry))
      Entries.push_back(Entry);
  }
  return llvm::join(Entries, ";");
}

CNameList::CNameList(StringRef List) : Joined(List.str()) {
  llvm::SmallVector<StringRef, 32> Split;
  StringRef(Joined).split(Split, ';', -1, /*KeepEmpty=*/false);
  for (StringRef Name : Split)
    Names.push_back(Name.trim().str());
  for (const std::string &Name : Names) {
    Refs.push_back(Name);
    Set.insert(Name);
  }
}

namespace {

/// The lists of the vocabulary with the names the language and the standard
/// library have.
constexpr std::pair<llvm::StringLiteral, llvm::StringLiteral> BuiltInLists[] = {
    {"MoveFunctions", "move"},
    {"ForwardFunctions", "forward"},
    {"ExchangeFunctions", "exchange"},
    {"SwapFunctions", "swap"},
    {"CopyFunctions", ""},
    {"SafeCallees", ""},
    {"BlockingCallees", ""},
    {"ViewTypes", "std::span;std::basic_string_view;std::initializer_list"},
    {"RemovingMethods",
     "erase;clear;pop_back;pop_front;swap;extract;resize;erase_after;remove;"
     "remove_if;unique;assign;splice;splice_after"},
    {"BuiltinTypes", "bool;void;char;char8_t;char16_t;char32_t;wchar_t;short;"
                     "int;long;float;double;signed;unsigned"},
};

} // namespace

StringRef CVocabulary::builtIn(StringRef Name) {
  for (const auto &[List, Names] : BuiltInLists)
    if (List == Name)
      return Names;
  llvm_unreachable("not a list of the vocabulary");
}

CVocabulary::CVocabulary(
    llvm::function_ref<std::string(StringRef Name, StringRef BuiltIn)> Resolve)
    : MoveFunctions(Resolve("MoveFunctions", builtIn("MoveFunctions"))),
      ForwardFunctions(
          Resolve("ForwardFunctions", builtIn("ForwardFunctions"))),
      ExchangeFunctions(
          Resolve("ExchangeFunctions", builtIn("ExchangeFunctions"))),
      SwapFunctions(Resolve("SwapFunctions", builtIn("SwapFunctions"))),
      CopyFunctions(Resolve("CopyFunctions", builtIn("CopyFunctions"))),
      MoveOrForwardFunctions(
          editList(MoveFunctions.joined(), ForwardFunctions.joined())),
      SafeCallees(Resolve("SafeCallees", builtIn("SafeCallees"))),
      BlockingCallees(Resolve("BlockingCallees", builtIn("BlockingCallees"))),
      ViewTypes(Resolve("ViewTypes", builtIn("ViewTypes"))),
      RemovingMethods(Resolve("RemovingMethods", builtIn("RemovingMethods"))),
      BuiltinTypes(Resolve("BuiltinTypes", builtIn("BuiltinTypes"))) {}

void CVocabulary::forEachList(
    llvm::function_ref<void(StringRef Name, const CNameList &List)> Each)
    const {
  Each("MoveFunctions", MoveFunctions);
  Each("ForwardFunctions", ForwardFunctions);
  Each("ExchangeFunctions", ExchangeFunctions);
  Each("SwapFunctions", SwapFunctions);
  Each("CopyFunctions", CopyFunctions);
  Each("SafeCallees", SafeCallees);
  Each("BlockingCallees", BlockingCallees);
  Each("ViewTypes", ViewTypes);
  Each("RemovingMethods", RemovingMethods);
  Each("BuiltinTypes", BuiltinTypes);
}

const CVocabulary &vocabulary() {
  static const CVocabulary BuiltIn(
      [](StringRef, StringRef BuiltIn) { return BuiltIn.str(); });
  return CurrentVocabulary ? *CurrentVocabulary : BuiltIn;
}

void setVocabulary(std::shared_ptr<const CVocabulary> Vocabulary) {
  CurrentVocabulary = std::move(Vocabulary);
}

bool isMoveFunction(StringRef Name) {
  return vocabulary().MoveFunctions.contains(Name);
}
bool isForwardFunction(StringRef Name) {
  return vocabulary().ForwardFunctions.contains(Name);
}
bool isExchangeFunction(StringRef Name) {
  return vocabulary().ExchangeFunctions.contains(Name);
}
bool isSwapFunction(StringRef Name) {
  return vocabulary().SwapFunctions.contains(Name);
}
bool isCopyFunction(StringRef Name) {
  return vocabulary().CopyFunctions.contains(Name);
}
bool isMoveOrForwardFunction(StringRef Name) {
  return vocabulary().MoveOrForwardFunctions.contains(Name);
}

} // namespace clang::tidy::malterlib
