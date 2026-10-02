//===-- Background.cpp - Build an index in a background thread ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "index/Background.h"
#include "Compiler.h"
#include "Config.h"
#include "FS.h"
#include "Headers.h"
#include "SourceCode.h"
#include "URI.h"
#include "index/BackgroundIndexLoader.h"
#include "index/FileIndex.h"
#include "index/Index.h"
#include "index/IndexAction.h"
#include "index/MemIndex.h"
#include "index/Ref.h"
#include "index/Relation.h"
#include "index/Serialization.h"
#include "index/Symbol.h"
#include "index/SymbolCollector.h"
#include "support/Context.h"
#include "support/Logger.h"
#include "support/Path.h"
#include "support/Threading.h"
#include "support/ThreadsafeFS.h"
#include "support/Trace.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/Stack.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Lex/PPCallbacks.h"
#include "clang/Lex/Preprocessor.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Threading.h"
#include "llvm/Support/xxhash.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <numeric>
#include <optional>
#include <queue>
#include <random>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace clang {
namespace clangd {
namespace {

// We cannot use vfs->makeAbsolute because Cmd.FileName is either absolute or
// relative to Cmd.Directory, which might not be the same as current working
// directory.
llvm::SmallString<128> getAbsolutePath(const tooling::CompileCommand &Cmd) {
  llvm::SmallString<128> AbsolutePath;
  if (llvm::sys::path::is_absolute(Cmd.Filename)) {
    AbsolutePath = Cmd.Filename;
  } else {
    AbsolutePath = Cmd.Directory;
    llvm::sys::path::append(AbsolutePath, Cmd.Filename);
    llvm::sys::path::remove_dots(AbsolutePath, true);
  }
  return AbsolutePath;
}

// The path the index knows a file by: its real path, as recorded when
// indexing. A deleted file is resolved through its directory.
std::string canonicalPath(PathRef File, llvm::vfs::FileSystem &FS) {
  llvm::SmallString<256> Real;
  if (!FS.getRealPath(File, Real))
    return std::string(Real);
  if (!FS.getRealPath(llvm::sys::path::parent_path(File), Real)) {
    llvm::sys::path::append(Real, llvm::sys::path::filename(File));
    return std::string(Real);
  }
  return File.str();
}

// Identifies a main file, however its path is spelled.
std::string commandChangedKey(PathRef File) {
  llvm::SmallString<128> Path(File);
  llvm::sys::path::remove_dots(Path, /*remove_dot_dot=*/true);
  return maybeCaseFoldPath(Path);
}

// Records the file names of includes that weren't found, and of files probed
// with __has_include: TUs whose results depend on whether such files exist.
class MissingIncludeCollector : public PPCallbacks {
public:
  MissingIncludeCollector(std::vector<std::string> &Names) : Names(Names) {}

  void InclusionDirective(SourceLocation, const Token &,
                          llvm::StringRef FileName, bool, CharSourceRange,
                          OptionalFileEntryRef File, llvm::StringRef,
                          llvm::StringRef, const Module *, bool,
                          SrcMgr::CharacteristicKind) override {
    if (!File)
      Names.push_back(
          llvm::StringRef(llvm::sys::path::filename(FileName)).lower());
  }

  // Unlike includes, __has_include probes depend on the file even if it's
  // found, as nothing else records them.
  void HasInclude(SourceLocation, llvm::StringRef FileName, bool,
                  OptionalFileEntryRef, SrcMgr::CharacteristicKind) override {
    Names.push_back(
        llvm::StringRef(llvm::sys::path::filename(FileName)).lower());
  }

private:
  std::vector<std::string> &Names;
};

bool shardIsStale(const LoadedShard &LS, llvm::vfs::FileSystem *FS) {
  auto Buf = FS->getBufferForFile(LS.AbsolutePath);
  if (!Buf) {
    vlog("Background-index: Couldn't read {0} to validate stored index: {1}",
         LS.AbsolutePath, Buf.getError().message());
    // There is no point in indexing an unreadable file.
    return false;
  }
  return digest(Buf->get()->getBuffer()) != LS.Digest;
}

} // namespace

BackgroundIndex::BackgroundIndex(
    const ThreadsafeFS &TFS, const GlobalCompilationDatabase &CDB,
    BackgroundIndexStorage::Factory IndexStorageFactory, Options Opts)
    : SwapIndex(std::make_unique<MemIndex>()), TFS(TFS), CDB(CDB),
      IndexingPriority(Opts.IndexingPriority),
      ContextProvider(std::move(Opts.ContextProvider)),
      IndexedSymbols(IndexContents::All, Opts.SupportContainedRefs),
      Rebuilder(this, &IndexedSymbols, Opts.ThreadPoolSize),
      OnIndexedHeaders(std::move(Opts.OnIndexedHeaders)),
      IndexStorageFactory(std::move(IndexStorageFactory)),
      Queue(std::move(Opts.OnProgress)),
      CommandsChanged(
          CDB.watch([&](const std::vector<std::string> &ChangedFiles) {
            enqueue(ChangedFiles);
          })) {
  assert(Opts.ThreadPoolSize > 0 && "Thread pool size can't be zero.");
  assert(this->IndexStorageFactory && "Storage factory can not be null!");
  for (unsigned I = 0; I < Opts.ThreadPoolSize; ++I) {
    ThreadPool.runAsync("background-worker-" + llvm::Twine(I + 1),
                        [this, Ctx(Context::current().clone())]() mutable {
                          clang::noteBottomOfStack();
                          WithContext BGContext(std::move(Ctx));
                          Queue.work([&] { Rebuilder.idle(); });
                        });
  }
}

BackgroundIndex::~BackgroundIndex() {
  stop();
  ThreadPool.wait();
}

BackgroundQueue::Task BackgroundIndex::changedFilesTask(
    const std::vector<std::string> &ChangedFiles) {
  BackgroundQueue::Task T([this, ChangedFiles] {
    trace::Span Tracer("BackgroundIndexEnqueue");

    std::optional<WithContext> WithProvidedContext;
    if (ContextProvider)
      WithProvidedContext.emplace(ContextProvider(/*Path=*/""));

    // We're doing this asynchronously, because we'll read shards here too.
    log("Enqueueing {0} commands for indexing", ChangedFiles.size());
    SPAN_ATTACH(Tracer, "files", int64_t(ChangedFiles.size()));

    auto NeedsReIndexing = loadProject(std::move(ChangedFiles));
    // Run indexing for files that need to be updated.
    std::shuffle(NeedsReIndexing.begin(), NeedsReIndexing.end(),
                 std::mt19937(std::random_device{}()));
    std::vector<BackgroundQueue::Task> Tasks;
    Tasks.reserve(NeedsReIndexing.size());
    for (const auto &File : NeedsReIndexing)
      Tasks.push_back(indexFileTask(std::move(File)));
    Queue.append(std::move(Tasks));
  });

  T.QueuePri = LoadShards;
  // Loading shards also reads every file they were built from to find stale
  // ones, which for a large project takes long enough to compete with builds.
  T.ThreadPri = IndexingPriority;
  return T;
}

static llvm::StringRef filenameWithoutExtension(llvm::StringRef Path) {
  Path = llvm::sys::path::filename(Path);
  return Path.drop_back(llvm::sys::path::extension(Path).size());
}

BackgroundQueue::Task BackgroundIndex::indexFileTask(std::string Path) {
  std::string Tag = filenameWithoutExtension(Path).str();
  // The same file can be spelled differently, e.g. with "..".
  uint64_t Key = llvm::xxh3_64bits(commandChangedKey(Path));
  BackgroundQueue::Task T([this, Path(std::move(Path))] {
    std::optional<WithContext> WithProvidedContext;
    if (ContextProvider)
      WithProvidedContext.emplace(ContextProvider(Path));
    auto Cmd = CDB.getCompileCommand(Path);
    if (!Cmd)
      return;
    if (auto Error = index(std::move(*Cmd)))
      elog("Indexing {0} failed: {1}", Path, std::move(Error));
  });
  T.QueuePri = IndexFile;
  T.ThreadPri = IndexingPriority;
  T.Tag = std::move(Tag);
  T.Key = Key;
  return T;
}

void BackgroundIndex::reindexChangedFiles(
    std::vector<std::string> ChangedFiles) {
  BackgroundQueue::Task T([this, ChangedFiles(std::move(ChangedFiles))] {
    trace::Span Tracer("BackgroundIndexChangedFiles");
    auto FS = TFS.view(/*CWD=*/std::nullopt);
    std::vector<std::string> TUs;
    llvm::StringSet<> SeenTUs;
    // Batches can run concurrently and out of order. Each acts on the current
    // state of the files, which it must not interleave with another's.
    std::unique_lock<std::mutex> EventsLock(FileEventsMu);
    for (const std::string &Reported : ChangedFiles) {
      std::optional<WithContext> WithProvidedContext;
      if (ContextProvider)
        WithProvidedContext.emplace(ContextProvider(Reported));
      // Reported paths can be spelled differently, e.g. through a symlink,
      // than the index records them, and files can be indexed by several
      // paths. Compilation databases use their own spelling, which is looked
      // up as reported.
      std::vector<std::string> Paths = indexedPaths(Reported);
      bool Exists = false, HasCommandOrIncluder = false;
      for (const std::string &File : Paths) {
        if (!FS->exists(File)) {
          // Unreadable files aren't treated as deleted elsewhere, as files can
          // exist only in a compile command's VFS overlay. Here the file is
          // known to be gone.
          {
            std::lock_guard<std::mutex> Lock(ShardVersionsMu);
            DeletedFiles.insert(maybeCaseFoldPath(File));
          }
          // Marked before looking for dependents, for TUs being indexed for the
          // first time, see the other branch.
          std::string Key = maybeCaseFoldPath(File);
          {
            std::lock_guard<std::mutex> Lock(HeaderToIndexedTUMu);
            PendingChangedHeaders.insert(Key);
          }
          // Deleting a header can change the files that included it, e.g.
          // through __has_include, though their shards don't change.
          std::string TU = indexedTUForHeader(File);
          bool HasIncluder = !TU.empty();
          if (!TU.empty()) {
            {
              std::lock_guard<std::mutex> Lock(HeaderToIndexedTUMu);
              DeletedHeaderToIndexedTU[maybeCaseFoldPath(File)] = TU;
            }
            std::lock_guard<std::mutex> Lock(ShardVersionsMu);
            ForcedTUs.insert(commandChangedKey(TU));
          }
          removeFromIndex(File);
          if (!TU.empty() && SeenTUs.insert(TU).second)
            TUs.push_back(std::move(TU));
          // TUs that only probed for the file have unchanged shards.
          std::vector<std::string> Probers;
          {
            std::lock_guard<std::mutex> Lock(MissingIncludesMu);
            auto It = MissingIncludeToTUs.find(
                llvm::StringRef(llvm::sys::path::filename(File)).lower());
            if (It != MissingIncludeToTUs.end())
              for (const auto &Prober : It->second)
                Probers.push_back(Prober.getKey().str());
          }
          if (HasIncluder || !Probers.empty()) {
            // They're reindexed now, which consumes the change.
            std::lock_guard<std::mutex> Lock(HeaderToIndexedTUMu);
            PendingChangedHeaders.erase(Key);
          }
          for (std::string &Prober : Probers) {
            {
              std::lock_guard<std::mutex> Lock(ShardVersionsMu);
              ForcedTUs.insert(commandChangedKey(Prober));
            }
            if (SeenTUs.insert(Prober).second)
              TUs.push_back(std::move(Prober));
          }
          continue;
        }
        {
          std::lock_guard<std::mutex> Lock(ShardVersionsMu);
          DeletedFiles.erase(maybeCaseFoldPath(File));
        }
        // A symlink in the path can point elsewhere since the file was indexed
        // by it, while the contents are the same. Its shard isn't replaced
        // when the contents match a version, so drop the version.
        bool Recorded = false, Moved = false;
        std::string SymbolPath;
        {
          std::lock_guard<std::mutex> Lock(ShardVersionsMu);
          Recorded = CanonicalPaths.contains(File);
          SymbolPath = SymbolPaths.lookup(File);
        }
        if (!SymbolPath.empty()) {
          auto Current = FS->status(File);
          auto Indexed = FS->status(SymbolPath);
          Moved = Current && (!Indexed || !Indexed->equivalent(*Current));
        }
        if (Moved) {
          std::lock_guard<std::mutex> Lock(ShardVersionsMu);
          ShardVersions.erase(File);
        }
        // Files with a compile command of their own are translation units.
        // Inferred commands don't count: those are used for headers. Each path
        // of the file can have a command of its own. Only paths that are known
        // are looked up: compilation databases are found by the directories of
        // the paths, which would load the same one again for another spelling.
        std::string TU;
        std::optional<tooling::CompileCommand> Cmd;
        if (Recorded || pathEqual(File, Reported))
          Cmd = CDB.getCompileCommand(File);
        if (Cmd && Cmd->Heuristic.empty()) {
          // Looked up again by the path that has the command.
          TU = File;
        } else {
          // A TU being indexed for the first time may be reading the file
          // before it's known to include it, or failing to. Mark the change
          // before looking for includers: such a TU checks the marks after
          // recording its includes, so either it sees the mark or the lookup
          // sees it.
          std::string Key = maybeCaseFoldPath(File);
          {
            std::lock_guard<std::mutex> Lock(HeaderToIndexedTUMu);
            PendingChangedHeaders.insert(Key);
          }
          TU = indexedTUForHeader(File);
          if (!TU.empty()) {
            std::lock_guard<std::mutex> Lock(HeaderToIndexedTUMu);
            PendingChangedHeaders.erase(Key);
          }
        }
        bool HasIncluder = !TU.empty();
        Exists = true;
        HasCommandOrIncluder |= HasIncluder;
        if (Moved && HasIncluder) {
          std::lock_guard<std::mutex> Lock(ShardVersionsMu);
          ForcedTUs.insert(commandChangedKey(TU));
        }
        // A TU being indexed may be reading an older version of the file. Its
        // shards can then match the disk again by the time this is checked,
        // e.g. after a revert, so make sure it's indexed again after that.
        if (HasIncluder &&
            Queue.isActive(llvm::xxh3_64bits(commandChangedKey(TU)))) {
          std::lock_guard<std::mutex> Lock(ShardVersionsMu);
          ForcedTUs.insert(commandChangedKey(TU));
        }
        if (HasIncluder && SeenTUs.insert(TU).second)
          TUs.push_back(std::move(TU));
        // A file that appears doesn't change the shards of TUs that tried to
        // include it, so those have to be reindexed regardless: the TU that
        // included a recreated header, or TUs that failed to include a file
        // with its name.
        std::vector<std::string> Includers;
        {
          std::lock_guard<std::mutex> Lock(HeaderToIndexedTUMu);
          auto It = DeletedHeaderToIndexedTU.find(maybeCaseFoldPath(File));
          if (It != DeletedHeaderToIndexedTU.end()) {
            Includers.push_back(std::move(It->second));
            DeletedHeaderToIndexedTU.erase(It);
          }
        }
        {
          std::lock_guard<std::mutex> Lock(MissingIncludesMu);
          auto It = MissingIncludeToTUs.find(
              llvm::StringRef(llvm::sys::path::filename(File)).lower());
          if (It != MissingIncludeToTUs.end())
            for (const auto &Includer : It->second)
              Includers.push_back(Includer.getKey().str());
        }
        if (!Includers.empty()) {
          // They're reindexed now, which consumes the change.
          std::lock_guard<std::mutex> Lock(HeaderToIndexedTUMu);
          PendingChangedHeaders.erase(maybeCaseFoldPath(File));
        }
        for (std::string &Includer : Includers) {
          {
            std::lock_guard<std::mutex> Lock(ShardVersionsMu);
            ForcedTUs.insert(commandChangedKey(Includer));
          }
          if (SeenTUs.insert(Includer).second)
            TUs.push_back(std::move(Includer));
        }
      }
      // Missing includes aren't stored in shards. TUs loaded with errors may
      // have failed to include a header that appears, so reindex them once to
      // find out. Not for files with a known includer or command by any path,
      // which are typically existing files that changed, to not reindex all
      // those TUs whenever a file changes.
      if (Exists && !HasCommandOrIncluder) {
        std::vector<std::string> ErrorTUs;
        {
          std::lock_guard<std::mutex> Lock(MissingIncludesMu);
          for (const auto &TU : UntrackedErrorTUs)
            ErrorTUs.push_back(TU.getKey().str());
          UntrackedErrorTUs.clear();
        }
        if (!ErrorTUs.empty()) {
          // They're reindexed now, which consumes the change.
          std::lock_guard<std::mutex> Lock(HeaderToIndexedTUMu);
          for (const std::string &File : Paths)
            PendingChangedHeaders.erase(maybeCaseFoldPath(File));
        }
        for (std::string &TU : ErrorTUs) {
          {
            std::lock_guard<std::mutex> Lock(ShardVersionsMu);
            ForcedTUs.insert(commandChangedKey(TU));
          }
          if (SeenTUs.insert(TU).second)
            TUs.push_back(std::move(TU));
        }
      }
    }
    EventsLock.unlock();
    // Checks which of the translation units are actually stale.
    if (!TUs.empty())
      Queue.push(changedFilesTask(TUs));
  });
  T.QueuePri = LoadShards;
  T.ThreadPri = IndexingPriority;
  Queue.push(std::move(T));
}

void BackgroundIndex::removeFromIndex(PathRef File) {
  {
    std::lock_guard<std::mutex> Lock(ShardVersionsMu);
    auto It = ShardVersions.find(File);
    // The file may have been indexed with a different spelling, e.g. case.
    if (It == ShardVersions.end())
      It = llvm::find_if(ShardVersions, [&](const auto &Entry) {
        return pathEqual(Entry.first(), File);
      });
    // Slabs can be published without a version until the file is indexed
    // again, e.g. if its path points elsewhere than when it was.
    std::string Indexed = File.str();
    if (It != ShardVersions.end()) {
      Indexed = It->first().str();
      ShardVersions.erase(It);
    } else if (IndexedFiles.contains(maybeCaseFoldPath(File))) {
      // Slabs are keyed by the spelling the file was recorded by.
      auto Recorded = llvm::find_if(CanonicalPaths, [&](const auto &Entry) {
        return pathEqual(Entry.first(), File);
      });
      if (Recorded != CanonicalPaths.end())
        Indexed = Recorded->first().str();
    } else {
      return;
    }
    IndexedFiles.erase(maybeCaseFoldPath(Indexed));
    IndexedSymbols.update(URI::create(Indexed).toString(), nullptr, nullptr,
                          nullptr, /*CountReferences=*/false);
  }
  // Headers that got their context from File need another one.
  std::vector<std::string> Headers;
  {
    std::lock_guard<std::mutex> Lock(HeaderToIndexedTUMu);
    auto It = IndexedTUToHeaders.find(commandChangedKey(File));
    if (It != IndexedTUToHeaders.end())
      Headers = It->second;
  }
  updateIndexedTUForHeaders(File, {});
  if (!Headers.empty() && OnIndexedHeaders)
    OnIndexedHeaders(std::move(Headers));
  Rebuilder.indexedTU();
}

void BackgroundIndex::updateMissingIncludes(PathRef MainFile,
                                            std::vector<std::string> Names) {
  std::unique_lock<std::mutex> Lock(MissingIncludesMu);
  UntrackedErrorTUs.erase(MainFile);
  IndexedThisSession.insert(MainFile);
  auto Previous = TUToMissingIncludes.find(MainFile);
  if (Previous != TUToMissingIncludes.end()) {
    for (const std::string &Name : Previous->second) {
      auto It = MissingIncludeToTUs.find(Name);
      if (It == MissingIncludeToTUs.end())
        continue;
      It->second.erase(MainFile);
      if (It->second.empty())
        MissingIncludeToTUs.erase(It);
    }
    TUToMissingIncludes.erase(Previous);
  }
  if (Names.empty())
    return;
  for (const std::string &Name : Names)
    MissingIncludeToTUs[Name].insert(MainFile);
  llvm::StringSet<> NewNames;
  for (const std::string &Name : Names)
    NewNames.insert(Name);
  TUToMissingIncludes[MainFile] = std::move(Names);
  Lock.unlock();

  // A file with one of these names may have appeared while the TU was being
  // indexed. Checked after recording the names, see reindexChangedFiles().
  bool Appeared = false;
  {
    std::lock_guard<std::mutex> PendingLock(HeaderToIndexedTUMu);
    std::vector<std::string> Matches;
    for (const auto &Pending : PendingChangedHeaders)
      if (NewNames.contains(
              llvm::StringRef(llvm::sys::path::filename(Pending.getKey()))
                  .lower()))
        Matches.push_back(Pending.getKey().str());
    for (const std::string &Match : Matches)
      PendingChangedHeaders.erase(Match);
    Appeared = !Matches.empty();
  }
  if (Appeared)
    reindexAgain(MainFile);
}

void BackgroundIndex::reindexAgain(PathRef MainFile) {
  {
    std::lock_guard<std::mutex> Lock(ShardVersionsMu);
    ForcedTUs.insert(commandChangedKey(MainFile));
  }
  Queue.push(changedFilesTask({MainFile.str()}));
}

bool BackgroundIndex::isIndexed(PathRef File) const {
  std::string Canonical = canonicalPath(File, *TFS.view(/*CWD=*/std::nullopt));
  std::lock_guard<std::mutex> Lock(ShardVersionsMu);
  if (IndexedFiles.contains(maybeCaseFoldPath(File)) ||
      IndexedFiles.contains(maybeCaseFoldPath(Canonical)))
    return true;
  auto It = PathsByCanonicalPath.find(maybeCaseFoldPath(Canonical));
  return It != PathsByCanonicalPath.end() &&
         llvm::any_of(It->second, [&](const std::string &Path) {
           return IndexedFiles.contains(maybeCaseFoldPath(Path));
         });
}

void BackgroundIndex::recordIndexedPathLocked(PathRef Path,
                                              llvm::StringRef FileURI) {
  IndexedFiles.insert(maybeCaseFoldPath(Path));
  // As of when the file was parsed, unlike the canonical path below.
  SymbolPaths.erase(Path);
  if (!FileURI.empty()) {
    if (auto SymbolPath = URI::resolve(FileURI, Path)) {
      if (*SymbolPath != Path)
        SymbolPaths[Path] = std::move(*SymbolPath);
    } else {
      llvm::consumeError(SymbolPath.takeError());
    }
  }
  // Paths can point elsewhere since they were last recorded, e.g. if a
  // symlinked directory was changed.
  std::string Canonical = canonicalPath(Path, *TFS.view(/*CWD=*/std::nullopt));
  auto [It, Inserted] = CanonicalPaths.try_emplace(Path);
  if (!Inserted) {
    if (It->second == Canonical)
      return;
    auto Old = PathsByCanonicalPath.find(maybeCaseFoldPath(It->second));
    if (Old != PathsByCanonicalPath.end()) {
      llvm::erase(Old->second, Path);
      if (Old->second.empty())
        PathsByCanonicalPath.erase(Old);
    }
  }
  It->second = Canonical;
  PathsByCanonicalPath[maybeCaseFoldPath(Canonical)].push_back(Path.str());
}

std::vector<std::string> BackgroundIndex::indexedPaths(PathRef File) {
  std::string Canonical = canonicalPath(File, *TFS.view(/*CWD=*/std::nullopt));
  std::vector<std::string> Paths = {Canonical};
  // Spellings that differ only in case are kept: compile commands can be
  // looked up by them case-sensitively.
  auto Add = [&](llvm::StringRef Path) {
    if (!llvm::is_contained(Paths, Path))
      Paths.push_back(Path.str());
  };
  Add(File);
  std::lock_guard<std::mutex> Lock(ShardVersionsMu);
  auto AddRecorded = [&](llvm::StringRef CanonicalPath) {
    auto It = PathsByCanonicalPath.find(maybeCaseFoldPath(CanonicalPath));
    if (It != PathsByCanonicalPath.end())
      for (const std::string &Path : It->second)
        Add(Path);
  };
  AddRecorded(Canonical);
  // The canonical path can't be resolved anymore if e.g. the directory it was
  // in is deleted.
  auto Recorded = CanonicalPaths.find(File);
  if (Recorded != CanonicalPaths.end() &&
      !pathEqual(Recorded->second, Canonical)) {
    Add(Recorded->second);
    AddRecorded(Recorded->second);
  }
  return Paths;
}

void BackgroundIndex::boostRelated(llvm::StringRef Path) {
  if (isHeaderFile(Path))
    Queue.boost(filenameWithoutExtension(Path), IndexBoostedFile);
}

std::string BackgroundIndex::indexedTUForHeader(PathRef Header) const {
  std::lock_guard<std::mutex> Lock(HeaderToIndexedTUMu);
  return HeaderToIndexedTU.lookup(maybeCaseFoldPath(Header));
}

std::vector<std::string> BackgroundIndex::updateIndexedTUForHeaders(
    PathRef MainFile, llvm::ArrayRef<std::string> Headers) {
  std::vector<std::string> ChangedHeaders;
  llvm::StringSet<> WasAssociatedWithMain;
  std::lock_guard<std::mutex> Lock(HeaderToIndexedTUMu);
  // A TU deleted while it was indexed provides no context. Deletions are
  // recorded before their contexts are removed under this lock, so either the
  // removal follows or this sees the deletion.
  {
    std::lock_guard<std::mutex> ShardLock(ShardVersionsMu);
    if (DeletedFiles.contains(maybeCaseFoldPath(MainFile)))
      Headers = {};
  }

  // MainFile can be spelled differently, e.g. with "..".
  std::string MainFileKey = commandChangedKey(MainFile);
  std::vector<std::string> &StoredHeaders = IndexedTUToHeaders[MainFileKey];
  for (const std::string &HeaderKey : StoredHeaders) {
    auto It = HeaderToIndexedTU.find(HeaderKey);
    if (It == HeaderToIndexedTU.end() ||
        commandChangedKey(It->second) != MainFileKey)
      continue;
    WasAssociatedWithMain.insert(HeaderKey);
    HeaderToIndexedTU.erase(It);
  }

  StoredHeaders.clear();
  StoredHeaders.reserve(Headers.size());
  std::string MainFileStorage = MainFile.str();
  for (const std::string &Header : Headers) {
    if (pathEqual(Header, MainFile))
      continue;
    std::string HeaderKey = maybeCaseFoldPath(Header);
    StoredHeaders.push_back(HeaderKey);
    if (HeaderToIndexedTU.find(HeaderKey) != HeaderToIndexedTU.end())
      continue;
    HeaderToIndexedTU[HeaderKey] = MainFileStorage;
    if (!WasAssociatedWithMain.count(HeaderKey))
      ChangedHeaders.push_back(Header);
  }

  if (Headers.empty())
    IndexedTUPaths.erase(MainFileKey);
  else
    IndexedTUPaths[MainFileKey] = MainFileStorage;

  // Headers that MainFile no longer includes get the context of another TU
  // that does, if any. This is rare, so look for one rather than keeping
  // every includer of every header. After a restart, shards are loaded once
  // each, with only the TU they were reached from, so other includers are
  // only known once they're indexed again; keeping them all would cost memory
  // for every include edge of the project.
  llvm::StringSet<> Orphans;
  for (const auto &Entry : WasAssociatedWithMain)
    if (!HeaderToIndexedTU.contains(Entry.getKey()))
      Orphans.insert(Entry.getKey());
  for (const auto &TU : IndexedTUToHeaders) {
    if (Orphans.empty())
      break;
    auto TUPath = IndexedTUPaths.find(TU.getKey());
    if (TUPath == IndexedTUPaths.end())
      continue;
    for (const std::string &HeaderKey : TU.getValue()) {
      if (!Orphans.erase(HeaderKey))
        continue;
      HeaderToIndexedTU[HeaderKey] = TUPath->second;
      ChangedHeaders.push_back(HeaderKey);
    }
  }
  // Headers left without a context need a reparse too.
  for (const auto &Orphan : Orphans)
    ChangedHeaders.push_back(Orphan.getKey().str());

  return ChangedHeaders;
}

/// Given index results from a TU, only update symbols coming from files that
/// are different or missing from than \p ShardVersionsSnapshot. Also stores new
/// index information on IndexStorage.
void BackgroundIndex::update(
    llvm::StringRef MainFile, IndexFileIn Index,
    const llvm::StringMap<ShardVersion> &ShardVersionsSnapshot, bool HadErrors,
    unsigned CommandGeneration) {
  std::vector<std::string> IndexedHeaders;
  // Keys are URIs.
  struct FileToUpdate {
    Path AbsPath;
    FileDigest Digest;
    bool IsTU;
    bool Moved;
  };
  llvm::StringMap<FileToUpdate> FilesToUpdate;
  // Note that sources do not contain any information regarding missing headers,
  // since we don't even know what absolute path they should fall in.
  // Paths whose symbols were indexed with other paths, e.g. through symlinks.
  llvm::StringMap<std::string> IndexedSymbolPaths;
  {
    std::lock_guard<std::mutex> Lock(ShardVersionsMu);
    IndexedSymbolPaths = SymbolPaths;
  }
  for (const auto &IndexIt : *Index.Sources) {
    const auto &IGN = IndexIt.getValue();
    auto AbsPath = URI::resolve(IGN.URI, MainFile);
    if (!AbsPath) {
      elog("Failed to resolve URI: {0}", AbsPath.takeError());
      continue;
    }
    // MainFile may be spelled differently, e.g. with "..".
    bool IsTU = IGN.Flags & IncludeGraphNode::SourceFlag::IsTU;
    if (!IsTU)
      IndexedHeaders.push_back(*AbsPath);
    const auto DigestIt = ShardVersionsSnapshot.find(*AbsPath);
    // File has different contents, or indexing was successful this time. The
    // main file's symbols can change without its contents changing, e.g. when
    // a header changes a macro that guards its declarations, so they're always
    // replaced.
    // Symbols also change if they have another path now, e.g. if a symlinked
    // directory points elsewhere. Their symbols are collected then, as they're
    // only skipped for unchanged versions of their paths.
    std::string SymbolPath = *AbsPath;
    if (std::string SymbolURI = Index.SymbolURIs.lookup(IGN.URI);
        !SymbolURI.empty()) {
      if (auto Resolved = URI::resolve(SymbolURI, MainFile))
        SymbolPath = std::move(*Resolved);
      else
        llvm::consumeError(Resolved.takeError());
    }
    std::string IndexedSymbolPath = IndexedSymbolPaths.lookup(*AbsPath);
    bool Moved =
        DigestIt != ShardVersionsSnapshot.end() &&
        !pathEqual(IndexedSymbolPath.empty() ? *AbsPath : IndexedSymbolPath,
                   SymbolPath);
    if (IsTU || Moved || DigestIt == ShardVersionsSnapshot.end() ||
        DigestIt->getValue().Digest != IGN.Digest ||
        (DigestIt->getValue().HadErrors && !HadErrors))
      FilesToUpdate[IGN.URI] = {std::move(*AbsPath), IGN.Digest, IsTU, Moved};
  }
  std::vector<std::string> ChangedHeaders =
      updateIndexedTUForHeaders(MainFile, IndexedHeaders);
  // Headers that changed while this TU was being indexed, before it was known
  // to include them, may have been read in their old versions. Checked after
  // publishing the includes, see reindexChangedFiles().
  bool ReadChangedHeaders = false;
  {
    std::lock_guard<std::mutex> Lock(HeaderToIndexedTUMu);
    for (const std::string &Header : IndexedHeaders)
      ReadChangedHeaders |=
          PendingChangedHeaders.erase(maybeCaseFoldPath(Header));
  }
  if (ReadChangedHeaders)
    reindexAgain(MainFile);
  if (!ChangedHeaders.empty() && OnIndexedHeaders)
    OnIndexedHeaders(std::move(ChangedHeaders));

  // Shard slabs into files. Symbols have files by their canonical paths, which
  // the include graph doesn't have for files in symlinked directories, e.g. if
  // the project is opened through a symlink.
  llvm::StringMap<std::string> SymbolURIs = std::move(Index.SymbolURIs);
  FileShardedIndex ShardedIndex(std::move(Index), [&](llvm::StringRef Uri) {
    auto It = SymbolURIs.find(Uri);
    return It == SymbolURIs.end() ? Uri.str() : It->second;
  });

  // Build and store new slabs for each updated file.
  for (const auto &FileIt : FilesToUpdate) {
    auto Uri = FileIt.first();
    auto IF = ShardedIndex.getShard(Uri);
    assert(IF && "no shard for file in Index.Sources?");
    PathRef Path = FileIt.getValue().AbsPath;
    bool IsTU = FileIt.getValue().IsTU;

    // Only store command line hash for main files of the TU, since our
    // current model keeps only one version of a header file.
    if (!IsTU)
      IF->Cmd.reset();
    // Stored, as the include graph has the file by another path, e.g. if it's
    // in a symlinked directory. Also tells shards written since apart.
    std::string FileURI = SymbolURIs.lookup(Uri);
    IF->FileURI = FileURI.empty() ? Uri.str() : FileURI;

    // We need to store shards before updating the index, since the latter
    // consumes slabs.
    // FIXME: Also skip serializing the shard if it is already up-to-date.
    if (auto Error = IndexStorageFactory(Path)->storeShard(Path, *IF))
      elog("Failed to write background-index shard for file {0}: {1}", Path,
           std::move(Error));

    // Deletions can be reported by the path the symbols have, e.g. the real
    // path of a file found through a symlinked directory.
    std::string SymbolPath;
    if (auto Resolved = URI::resolve(*IF->FileURI, Path))
      SymbolPath = std::move(*Resolved);
    else
      llvm::consumeError(Resolved.takeError());
    {
      std::lock_guard<std::mutex> Lock(ShardVersionsMu);
      // The file may have been deleted while it was indexed. Deletions are
      // recorded under this lock, so checking under it can't undo a removal.
      if (DeletedFiles.contains(maybeCaseFoldPath(Path)) ||
          (!SymbolPath.empty() &&
           DeletedFiles.contains(maybeCaseFoldPath(SymbolPath))))
        continue;
      const auto &Hash = FileIt.getValue().Digest;
      auto DigestIt = ShardVersions.try_emplace(Path);
      recordIndexedPathLocked(Path, *IF->FileURI);
      ShardVersion &SV = DigestIt.first->second;
      // Skip if another thread already stored this version of the file. The
      // main file and files left out of the snapshot are always replaced:
      // their symbols can change without their contents changing, e.g. with a
      // new compile command.
      if (!DigestIt.second && SV.Digest == Hash && SV.HadErrors == HadErrors &&
          !IsTU && !FileIt.getValue().Moved &&
          ShardVersionsSnapshot.count(Path))
        continue;
      SV.Digest = Hash;
      SV.HadErrors = HadErrors;
      unsigned &Generation = IndexedCommandGeneration[Path];
      Generation = std::max(Generation, CommandGeneration);

      // This can override a newer version that is added in another thread, if
      // this thread sees the older version but finishes later. This should be
      // rare in practice.
      // A file indexed by several paths, e.g. as a TU and as a header of
      // another TU through a symlinked directory, has slabs for each. Their
      // identical references are deduplicated when building the index. Keying
      // slabs by the file's canonical path instead would need versions,
      // filtering and removal to use it too.
      IndexedSymbols.update(
          Uri, std::make_unique<SymbolSlab>(std::move(*IF->Symbols)),
          std::make_unique<RefSlab>(std::move(*IF->Refs)),
          std::make_unique<RelationSlab>(std::move(*IF->Relations)), IsTU,
          *IF->FileURI);
    }
  }
}

llvm::Error BackgroundIndex::index(tooling::CompileCommand Cmd) {
  trace::Span Tracer("BackgroundIndex");
  SPAN_ATTACH(Tracer, "file", Cmd.Filename);
  auto AbsolutePath = getAbsolutePath(Cmd);

  auto FS = TFS.view(Cmd.Directory);
  auto Buf = FS->getBufferForFile(AbsolutePath);
  if (!Buf)
    return llvm::errorCodeToError(Buf.getError());
  auto Hash = digest(Buf->get()->getBuffer());

  // Take a snapshot of the versions to avoid locking for each file in the TU.
  // Files missing from the snapshot are indexed and stored even if their
  // contents didn't change since they were last indexed.
  llvm::StringMap<ShardVersion> ShardVersionsSnapshot;
  unsigned Generation = 0;
  {
    std::lock_guard<std::mutex> Lock(ShardVersionsMu);
    ShardVersionsSnapshot = ShardVersions;
    IndexedCommands[commandChangedKey(AbsolutePath)] = Cmd;
    auto It = CommandChangedTUs.find(commandChangedKey(AbsolutePath));
    // A task that started with an older command leaves the change to the task
    // that indexes with the new one.
    if (It != CommandChangedTUs.end() &&
        It->second.CommandLine == Cmd.CommandLine &&
        It->second.Directory == Cmd.Directory) {
      Generation = It->second.Generation;
      CommandChangedTUs.erase(It);
      // Files' symbols can depend on the compile command. Index each file once
      // after the command changes, rather than once per file including it.
      std::vector<std::string> Outdated;
      for (const auto &Entry : ShardVersionsSnapshot)
        if (IndexedCommandGeneration.lookup(Entry.first()) < Generation)
          Outdated.push_back(Entry.first().str());
      for (const std::string &File : Outdated)
        ShardVersionsSnapshot.erase(File);
    }
  }
  // Other headers' symbols can change without their contents changing too,
  // e.g. when another header changes a macro, but the index keeps one version
  // of each header: replacing them would rewrite the shard of every header a
  // file includes whenever any of them changes.

  vlog("Indexing {0} (digest:={1})", Cmd.Filename, llvm::toHex(Hash));
  ParseInputs Inputs;
  Inputs.TFS = &TFS;
  Inputs.CompileCommand = std::move(Cmd);
  IgnoreDiagnostics IgnoreDiags;
  auto CI = buildCompilerInvocation(Inputs, IgnoreDiags);
  if (!CI)
    return error("Couldn't build compiler invocation");

  auto Clang =
      prepareCompilerInstance(std::move(CI), /*Preamble=*/nullptr,
                              std::move(*Buf), std::move(FS), IgnoreDiags);
  if (!Clang)
    return error("Couldn't build compiler instance");

  SymbolCollector::Options IndexOpts;
  // Creates a filter to not collect index results from files with unchanged
  // digests.
  IndexOpts.FileFilter = [&ShardVersionsSnapshot](const SourceManager &SM,
                                                  FileID FID) {
    // The main file is always indexed, see update().
    if (FID == SM.getMainFileID())
      return true;
    const auto F = SM.getFileEntryRefForID(FID);
    if (!F)
      return false; // Skip invalid files.
    auto AbsPath = getCanonicalPath(*F, SM.getFileManager());
    if (!AbsPath)
      return false; // Skip files without absolute path.
    auto Digest = digestFile(SM, FID);
    if (!Digest)
      return false;
    auto D = ShardVersionsSnapshot.find(*AbsPath);
    if (D != ShardVersionsSnapshot.end() && D->second.Digest == Digest &&
        !D->second.HadErrors)
      return false; // Skip files that haven't changed, without errors.
    return true;
  };
  IndexOpts.CollectMainFileRefs = true;

  IndexFileIn Index;
  auto Action = createStaticIndexingAction(
      IndexOpts, [&](IndexFileIn Result) { Index = std::move(Result); });

  // We're going to run clang here, and it could potentially crash.
  // We could use CrashRecoveryContext to try to make indexing crashes nonfatal,
  // but the leaky "recovery" is pretty scary too in a long-running process.
  // If crashes are a real problem, maybe we should fork a child process.

  const FrontendInputFile &Input = Clang->getFrontendOpts().Inputs.front();
  if (!Action->BeginSourceFile(*Clang, Input))
    return error("BeginSourceFile() failed");
  std::vector<std::string> MissingIncludes;
  Clang->getPreprocessor().addPPCallbacks(
      std::make_unique<MissingIncludeCollector>(MissingIncludes));
  if (llvm::Error Err = Action->Execute())
    return Err;

  Action->EndSourceFile();

  Index.Cmd = Inputs.CompileCommand;
  assert(Index.Symbols && Index.Refs && Index.Sources &&
         "Symbols, Refs and Sources must be set.");

  log("Indexed {0} ({1} symbols, {2} refs, {3} files)",
      Inputs.CompileCommand.Filename, Index.Symbols->size(),
      Index.Refs->numRefs(), Index.Sources->size());
  SPAN_ATTACH(Tracer, "symbols", int(Index.Symbols->size()));
  SPAN_ATTACH(Tracer, "refs", int(Index.Refs->numRefs()));
  SPAN_ATTACH(Tracer, "sources", int(Index.Sources->size()));

  bool HadErrors = Clang->hasDiagnostics() &&
                   Clang->getDiagnostics().hasUncompilableErrorOccurred();
  if (HadErrors) {
    log("Failed to compile {0}, index may be incomplete", AbsolutePath);
    for (auto &It : *Index.Sources)
      It.second.Flags |= IncludeGraphNode::SourceFlag::HadErrors;
  }
  update(AbsolutePath, std::move(Index), ShardVersionsSnapshot, HadErrors,
         Generation);
  updateMissingIncludes(AbsolutePath, std::move(MissingIncludes));

  Rebuilder.indexedTU();
  return llvm::Error::success();
}

// Restores shards for \p MainFiles from index storage. Then checks staleness of
// those shards and returns a list of TUs that needs to be indexed to update
// staleness.
std::vector<std::string>
BackgroundIndex::loadProject(std::vector<std::string> MainFiles) {
  // Drop files where background indexing is disabled in config.
  if (ContextProvider)
    llvm::erase_if(MainFiles, [&](const std::string &TU) {
      // Load the config for each TU, as indexing may be selectively enabled.
      WithContext WithProvidedContext(ContextProvider(TU));
      return Config::current().Index.Background ==
             Config::BackgroundPolicy::Skip;
    });
  Rebuilder.startLoading();
  // Load shards for all of the mainfiles.
  const std::vector<LoadedShard> Result =
      loadIndexShards(MainFiles, IndexStorageFactory, CDB);
  llvm::StringMap<std::vector<std::string>> HeadersByTU;
  for (const LoadedShard &LS : Result) {
    if (!LS.Shard || LS.DependentTU.empty() ||
        pathEqual(LS.AbsolutePath, LS.DependentTU))
      continue;
    HeadersByTU[LS.DependentTU].push_back(LS.AbsolutePath);
  }
  std::vector<std::string> ChangedHeaders;
  for (const auto &It : HeadersByTU) {
    std::vector<std::string> Changed =
        updateIndexedTUForHeaders(It.first(), It.second);
    ChangedHeaders.insert(ChangedHeaders.end(), Changed.begin(), Changed.end());
  }
  if (!ChangedHeaders.empty() && OnIndexedHeaders)
    OnIndexedHeaders(std::move(ChangedHeaders));

  auto FS = TFS.view(/*CWD=*/std::nullopt);
  size_t LoadedShards = 0;
  // TUs of shards whose files have other canonical paths than when they were
  // indexed, or that were indexed by older versions in such paths.
  std::vector<std::string> StaleIdentityTUs;
  {
    // Update in-memory state.
    std::lock_guard<std::mutex> Lock(ShardVersionsMu);
    for (auto &LS : Result) {
      // Shards of deleted files stay in storage, and are reached through the
      // include graphs of files that included them. Only deletions seen in
      // this session are known: an unreadable file may still exist in a
      // compile command's VFS overlay.
      if (!LS.Shard ||
          DeletedFiles.contains(maybeCaseFoldPath(LS.AbsolutePath)))
        continue;
      // Files indexed or loaded before are at least as new in memory: shards
      // are only stored together with updating the in-memory index, and one
      // may have been stored since this was read.
      if (ShardVersions.contains(LS.AbsolutePath))
        continue;
      auto SS =
          LS.Shard->Symbols
              ? std::make_unique<SymbolSlab>(std::move(*LS.Shard->Symbols))
              : nullptr;
      auto RS = LS.Shard->Refs
                    ? std::make_unique<RefSlab>(std::move(*LS.Shard->Refs))
                    : nullptr;
      auto RelS =
          LS.Shard->Relations
              ? std::make_unique<RelationSlab>(std::move(*LS.Shard->Relations))
              : nullptr;
      // Symbols have the file by its canonical path, e.g. if it's in a
      // symlinked directory, which shards written since store. Paths are
      // compared by the files they're for: e.g. a subst drive on Windows isn't
      // resolved in the stored path. A symlink retargeted to a hard link of
      // the same file isn't noticed.
      std::string PathURI = URI::create(LS.AbsolutePath).toString();
      std::string FileURI;
      bool StaleIdentity = false;
      if (LS.Shard->FileURI) {
        FileURI = *LS.Shard->FileURI;
        // Deletions can be reported by the path the symbols have.
        if (FileURI != PathURI) {
          auto Stored = URI::resolve(FileURI, LS.AbsolutePath);
          bool Deleted =
              Stored && DeletedFiles.contains(maybeCaseFoldPath(*Stored));
          if (!Stored)
            llvm::consumeError(Stored.takeError());
          if (Deleted)
            continue;
        }
        // The symlink can point elsewhere now.
        if (FileURI != PathURI) {
          auto PathStatus = FS->status(LS.AbsolutePath);
          if (auto Stored = URI::resolve(FileURI, LS.AbsolutePath)) {
            auto StoredStatus = FS->status(*Stored);
            StaleIdentity =
                PathStatus &&
                (!StoredStatus || !StoredStatus->equivalent(*PathStatus));
          } else {
            llvm::consumeError(Stored.takeError());
          }
        }
      } else {
        // Older versions stored no symbols for files whose paths aren't
        // canonical, e.g. TUs in symlinked directories, and their shards stay
        // valid until the files change.
        llvm::StringRef Dir = llvm::sys::path::parent_path(LS.AbsolutePath);
        llvm::SmallString<256> RealDir;
        StaleIdentity =
            !FS->getRealPath(Dir, RealDir) && !pathEqual(RealDir, Dir);
      }
      if (StaleIdentity) {
        // Reindexing the TU replaces the shards that it has no versions of.
        StaleIdentityTUs.push_back(LS.DependentTU);
        recordIndexedPathLocked(LS.AbsolutePath, FileURI);
        // Published until it's replaced, also if indexing fails.
        ++LoadedShards;
        IndexedSymbols.update(PathURI, std::move(SS), std::move(RS),
                              std::move(RelS), LS.CountReferences);
        continue;
      }
      ShardVersion &SV = ShardVersions[LS.AbsolutePath];
      recordIndexedPathLocked(LS.AbsolutePath, FileURI);
      SV.Digest = LS.Digest;
      SV.HadErrors = LS.HadErrors;
      ++LoadedShards;

      IndexedSymbols.update(PathURI, std::move(SS), std::move(RS),
                            std::move(RelS), LS.CountReferences, FileURI);
    }
  }
  Rebuilder.loadedShard(LoadedShards);
  Rebuilder.doneLoading();

  llvm::DenseSet<PathRef> TUsToIndex;
  for (const std::string &TU : StaleIdentityTUs)
    TUsToIndex.insert(TU);
  // We'll accept data from stale shards, but ensure the files get reindexed
  // soon.
  for (auto &LS : Result) {
    if (!shardIsStale(LS, FS.get()))
      continue;
    PathRef TUForFile = LS.DependentTU;
    assert(!TUForFile.empty() && "File without a TU!");

    // FIXME: Currently, we simply schedule indexing on a TU whenever any of
    // its dependencies needs re-indexing. We might do it smarter by figuring
    // out a minimal set of TUs that will cover all the stale dependencies.
    // FIXME: Try looking at other TUs if no compile commands are available
    // for this TU, i.e TU was deleted after we performed indexing.
    TUsToIndex.insert(TUForFile);
  }

  {
    std::lock_guard<std::mutex> Lock(MissingIncludesMu);
    for (const LoadedShard &LS : Result)
      if (LS.Shard && LS.HadErrors &&
          pathEqual(LS.AbsolutePath, LS.DependentTU) &&
          !IndexedThisSession.contains(LS.AbsolutePath))
        UntrackedErrorTUs.insert(LS.AbsolutePath);
  }

  // Main files whose compile command changed, e.g. because the compilation
  // database was regenerated with different flags, are stale too.
  bool CommandsChanged = false;
  for (auto &LS : Result) {
    if (!pathEqual(LS.AbsolutePath, LS.DependentTU))
      continue;
    {
      std::lock_guard<std::mutex> Lock(ShardVersionsMu);
      if (ForcedTUs.erase(commandChangedKey(LS.AbsolutePath)))
        TUsToIndex.insert(LS.DependentTU);
    }
    // The command the TU was last indexed with: from this session, or from
    // its shard, if index storage is available.
    std::optional<tooling::CompileCommand> Indexed;
    {
      std::lock_guard<std::mutex> Lock(ShardVersionsMu);
      auto It = IndexedCommands.find(commandChangedKey(LS.AbsolutePath));
      if (It != IndexedCommands.end())
        Indexed = It->second;
    }
    if (!Indexed && LS.Shard && LS.Shard->Cmd)
      Indexed = *LS.Shard->Cmd;
    if (!Indexed)
      continue;
    std::optional<WithContext> WithProvidedContext;
    if (ContextProvider)
      WithProvidedContext.emplace(ContextProvider(LS.AbsolutePath));
    auto Cmd = CDB.getCompileCommand(LS.AbsolutePath);
    if (!Cmd || (Cmd->CommandLine == Indexed->CommandLine &&
                 Cmd->Directory == Indexed->Directory))
      continue;
    {
      std::lock_guard<std::mutex> Lock(ShardVersionsMu);
      if (!CommandsChanged)
        ++CommandGeneration;
      CommandChangedTUs[commandChangedKey(LS.AbsolutePath)] = {
          CommandGeneration, Cmd->CommandLine, Cmd->Directory};
    }
    CommandsChanged = true;
    TUsToIndex.insert(LS.DependentTU);
  }

  return {TUsToIndex.begin(), TUsToIndex.end()};
}

void BackgroundIndex::profile(MemoryTree &MT) const {
  IndexedSymbols.profile(MT.child("slabs"));
  // We don't want to mix memory used by index and symbols, so call base class.
  MT.child("index").addUsage(SwapIndex::estimateMemoryUsage());
}
} // namespace clangd
} // namespace clang
