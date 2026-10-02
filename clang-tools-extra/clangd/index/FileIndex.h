//===--- FileIndex.h - Index for files. ---------------------------- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// FileIndex implements SymbolIndex for symbols from a set of files. Symbols are
// maintained at source-file granularity (e.g. with ASTs), and files can be
// updated dynamically.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANGD_INDEX_FILEINDEX_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANGD_INDEX_FILEINDEX_H

#include "Headers.h"
#include "clang-include-cleaner/Record.h"
#include "index/Index.h"
#include "index/Merge.h"
#include "index/Ref.h"
#include "index/Relation.h"
#include "index/Serialization.h"
#include "index/Symbol.h"
#include "support/MemoryTree.h"
#include "support/Path.h"
#include "clang/Lex/Preprocessor.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include <atomic>
#include <memory>
#include <optional>
#include <vector>

namespace clang {
class ASTContext;
namespace clangd {
class ParsedAST;

/// Select between in-memory index implementations, which have tradeoffs.
enum class IndexType {
  // MemIndex is trivially cheap to build, but has poor query performance.
  Light,
  // Dex is relatively expensive to build and uses more memory, but is fast.
  Heavy,
};

/// How to handle duplicated symbols across multiple files.
enum class DuplicateHandling {
  // Pick a random symbol. Less accurate but faster.
  PickOne,
  // Merge symbols. More accurate but slower.
  Merge,
};

/// A container of slabs associated with a key. It can be updated at key
/// granularity, replacing all slabs belonging to a key with a new set. Keys are
/// usually file paths/uris.
///
/// This implements snapshot semantics. Each update will create a new snapshot
/// for all slabs of the Key. Snapshots are managed with shared pointers that
/// are shared between this class and the users. For each key, this class only
/// stores a pointer pointing to the newest snapshot, and an outdated snapshot
/// is deleted by the last owner of the snapshot, either this class or the
/// symbol index.
///
/// The snapshot semantics keeps critical sections minimal since we only need
/// locking when we swap or obtain references to snapshots.
class FileSymbols {
public:
  FileSymbols(IndexContents IdxContents, bool SupportContainedRefs);
  /// Updates all slabs associated with the \p Key.
  /// If either is nullptr, corresponding data for \p Key will be removed.
  /// If CountReferences is true, \p Refs will be used for counting references
  /// during merging.
  /// Removes the slabs for \p Key. Returns whether there were any.
  bool remove(llvm::StringRef Key);

  /// Returns the keys that have slabs.
  std::vector<std::string> keys() const;
  /// The FileURI passed to update() for \p Key, if any.
  std::string fileURI(llvm::StringRef Key) const;

  /// \p FileURI is the URI that symbols in the file have, if it differs from
  /// \p Key, e.g. if it was opened through a symlinked directory. The index
  /// also has the file by it then, so that it takes precedence over other
  /// indexes for it, see SymbolIndex::indexedFiles().
  void update(llvm::StringRef Key, std::unique_ptr<SymbolSlab> Symbols,
              std::unique_ptr<RefSlab> Refs,
              std::unique_ptr<RelationSlab> Relations, bool CountReferences,
              llvm::StringRef FileURI = {});

  /// The index keeps the slabs alive.
  /// Will count Symbol::References based on number of references in the main
  /// files, while building the index with DuplicateHandling::Merge option.
  /// Version is populated with an increasing sequence counter.
  std::unique_ptr<SymbolIndex>
  buildIndex(IndexType,
             DuplicateHandling DuplicateHandle = DuplicateHandling::PickOne,
             size_t *Version = nullptr);

  void profile(MemoryTree &MT) const;

private:
  IndexContents IdxContents;
  bool SupportContainedRefs;

  struct RefSlabAndCountReferences {
    std::shared_ptr<RefSlab> Slab;
    bool CountReferences = false;
  };
  mutable std::mutex Mutex;

  size_t Version = 0;
  llvm::StringMap<std::shared_ptr<SymbolSlab>> SymbolsSnapshot;
  llvm::StringMap<RefSlabAndCountReferences> RefsSnapshot;
  llvm::StringMap<std::shared_ptr<RelationSlab>> RelationsSnapshot;
  // FileURI passed to update() by its key.
  llvm::StringMap<std::string> FileURIs;
};

/// This manages symbols from files and an in-memory index on all symbols.
/// FIXME: Expose an interface to remove files that are closed.
class FileIndex : public MergedIndex {
public:
  FileIndex(bool SupportContainedRefs);

  /// Update preamble symbols of file \p Path with all declarations in \p AST
  /// and macros in \p PP.
  void updatePreamble(PathRef Path, llvm::StringRef Version, ASTContext &AST,
                      Preprocessor &PP,
                      const include_cleaner::PragmaIncludes &PI);
  void updatePreamble(IndexFileIn);

  /// Update symbols and references from main file \p Path with
  /// `indexMainDecls`.
  void updateMain(PathRef Path, ParsedAST &AST);

  /// Removes what was indexed from \p Paths, both as main files and as
  /// headers, e.g. because they changed on disk while not open. Queries see
  /// the removal after the next rebuild, e.g. rebuildAfterRemovals(), which is
  /// slower and can run on another thread. Updates in between are kept.
  /// Files are also removed by the paths their symbols have, see
  /// FileSymbols::update(), unless \p KeepFile returns true for the path they
  /// were updated by, e.g. if they're open by that path.
  void removeFiles(
      llvm::ArrayRef<std::string> Paths,
      llvm::function_ref<bool(llvm::StringRef Path)> KeepFile = nullptr);
  /// Like removeFiles(), but only as main files, for the main files that
  /// \p ShouldRemove returns true for.
  void
  removeMainFiles(llvm::function_ref<bool(llvm::StringRef Path)> ShouldRemove);
  /// Rebuilds the indexes that removeFiles() removed something from.
  void rebuildAfterRemovals();

  void profile(MemoryTree &MT) const;

private:
  // Contains information from each file's preamble only. Symbols and relations
  // are sharded per declaration file to deduplicate multiple symbols and reduce
  // memory usage.
  // Missing information:
  //  - symbol refs (these are always "from the main file")
  //  - definition locations in the main file
  //
  // Note that we store only one version of a header, hence symbols appearing in
  // different PP states will be missing.
  FileSymbols PreambleSymbols;
  SwapIndex PreambleIndex;

  // Contains information from each file's main AST.
  // These are updated frequently (on file change), but are relatively small.
  // Mostly contains:
  //  - refs to symbols declared in the preamble and referenced from main
  //  - symbols declared both in the main file and the preamble
  // (Note that symbols *only* in the main file are not indexed).
  FileSymbols MainFileSymbols;
  SwapIndex MainFileIndex;

  void rebuildPreambleIndex();
  void rebuildMainIndex();
  std::atomic<bool> PreambleRemovalPending = {false};
  std::atomic<bool> MainRemovalPending = {false};

  // While both the FileIndex and SwapIndex are threadsafe, we need to track
  // versions to ensure that we don't overwrite newer indexes with older ones.
  std::mutex UpdateIndexMu;
  unsigned MainIndexVersion = 0;
  unsigned PreambleIndexVersion = 0;
};

using SlabTuple = std::tuple<SymbolSlab, RefSlab, RelationSlab>;

/// Retrieves symbols and refs of local top level decls in \p AST (i.e.
/// `AST.getLocalTopLevelDecls()`).
/// Exposed to assist in unit tests.
SlabTuple indexMainDecls(ParsedAST &AST);

/// Index declarations from \p AST and macros from \p PP that are declared in
/// included headers.
SlabTuple indexHeaderSymbols(llvm::StringRef Version, ASTContext &AST,
                             Preprocessor &PP,
                             const include_cleaner::PragmaIncludes &PI,
                             SymbolOrigin Origin);

/// Takes slabs coming from a TU (multiple files) and shards them per
/// declaration location.
struct FileShardedIndex {
  /// \p HintPath is used to convert file URIs stored in symbols into absolute
  /// paths.
  /// Symbols are stored with URIs of canonical paths, which resolve symlinked
  /// directories, while the include graph has files as they were found.
  /// \p SymbolURI maps the latter to the former, so that a file's shard has
  /// its symbols, also when it was found through a symlink. Shards are looked
  /// up by include graph URIs then.
  explicit FileShardedIndex(
      IndexFileIn Input,
      std::function<std::string(llvm::StringRef)> SymbolURI = nullptr);

  /// Returns uris for all files that has a shard.
  std::vector<llvm::StringRef> getAllSources() const;

  /// Generates index shard for the \p Uri. Note that this function results in
  /// a copy of all the relevant data.
  /// Returned index will always have Symbol/Refs/Relation Slabs set, even if
  /// they are empty.
  std::optional<IndexFileIn> getShard(llvm::StringRef Uri) const;

private:
  // Contains all the information that belongs to a single file.
  struct FileShard {
    // Either declared or defined in the file.
    llvm::DenseSet<const Symbol *> Symbols;
    // Reference occurs in the file.
    llvm::DenseSet<const Ref *> Refs;
    // Subject is declared in the file.
    llvm::DenseSet<const Relation *> Relations;
    // Contains edges for only the direct includes.
    IncludeGraph IG;
  };

  // Keeps all the information alive.
  const IndexFileIn Index;
  std::function<std::string(llvm::StringRef)> SymbolURI;
  // Mapping from URIs to slab information.
  llvm::StringMap<FileShard> Shards;
  // Used to build RefSlabs.
  llvm::DenseMap<const Ref *, SymbolID> RefToSymID;
};

} // namespace clangd
} // namespace clang

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANGD_INDEX_FILEINDEX_H
