//===--- Background.h - Build an index in a background thread ----*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANGD_INDEX_BACKGROUND_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANGD_INDEX_BACKGROUND_H

#include "GlobalCompilationDatabase.h"
#include "SourceCode.h"
#include "index/BackgroundRebuild.h"
#include "index/FileIndex.h"
#include "index/Index.h"
#include "index/Serialization.h"
#include "support/Context.h"
#include "support/MemoryTree.h"
#include "support/Path.h"
#include "support/Threading.h"
#include "support/ThreadsafeFS.h"
#include "clang/Tooling/CompilationDatabase.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/Threading.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace clang {
namespace clangd {

// Handles storage and retrieval of index shards. Both store and load
// operations can be called from multiple-threads concurrently.
class BackgroundIndexStorage {
public:
  virtual ~BackgroundIndexStorage() = default;

  // Shards of the index are stored and retrieved independently, keyed by shard
  // identifier - in practice this is a source file name
  virtual llvm::Error storeShard(llvm::StringRef ShardIdentifier,
                                 IndexFileOut Shard) const = 0;

  // Tries to load shard with given identifier, returns nullptr if shard
  // couldn't be loaded.
  virtual std::unique_ptr<IndexFileIn>
  loadShard(llvm::StringRef ShardIdentifier) const = 0;

  // The factory provides storage for each File.
  // It keeps ownership of the storage instances, and should manage caching
  // itself. Factory must be threadsafe and never returns nullptr.
  using Factory = llvm::unique_function<BackgroundIndexStorage *(PathRef)>;

  // Creates an Index Storage that saves shards into disk. Index storage uses
  // CDBDirectory + ".cache/clangd/index/" as the folder to save shards.
  // CDBDirectory is the first directory containing a CDB in parent directories
  // of a file, or user cache directory if none was found, e.g. stdlib headers.
  static Factory createDiskBackedStorageFactory(
      std::function<std::optional<ProjectInfo>(PathRef)> GetProjectInfo);
};

// A priority queue of tasks which can be run on (external) worker threads.
class BackgroundQueue {
public:
  /// A work item on the thread pool's queue.
  struct Task {
    explicit Task(std::function<void()> Run) : Run(std::move(Run)) {}

    std::function<void()> Run;
    llvm::ThreadPriority ThreadPri = llvm::ThreadPriority::Low;
    unsigned QueuePri = 0; // Higher-priority tasks will run first.
    std::string Tag;       // Allows priority to be boosted later.
    uint64_t Key = 0;      // If the key matches a queued task, drop this one.

    bool operator<(const Task &O) const { return QueuePri < O.QueuePri; }
  };

  // Describes the number of tasks processed by the queue.
  struct Stats {
    unsigned Enqueued = 0;  // Total number of tasks ever enqueued.
    unsigned Active = 0;    // Tasks being currently processed by a worker.
    unsigned Completed = 0; // Tasks that have been finished.
    unsigned LastIdle = 0;  // Number of completed tasks when last empty.
  };

  BackgroundQueue(std::function<void(Stats)> OnProgress = nullptr)
      : OnProgress(OnProgress) {}

  // Add tasks to the queue.
  void push(Task);
  void append(std::vector<Task>);
  // Whether a task with \p Key is running.
  bool isActive(uint64_t Key);

  // Boost priority of current and new tasks with matching Tag, if they are
  // lower priority.
  // Reducing the boost of a tag affects future tasks but not current ones.
  void boost(llvm::StringRef Tag, unsigned NewPriority);

  // Process items on the queue until the queue is stopped.
  // If the queue becomes empty, OnIdle will be called (on one worker).
  void work(std::function<void()> OnIdle = nullptr);

  // Stop processing new tasks, allowing all work() calls to return soon.
  void stop();

  // Disables thread priority lowering to ensure progress on loaded systems.
  // Only affects tasks that run after the call.
  static void preventThreadStarvationInTests();
  [[nodiscard]] bool
  blockUntilIdleForTest(std::optional<double> TimeoutSeconds);

private:
  void notifyProgress() const; // Requires lock Mu
  bool adjust(Task &T);

  std::mutex Mu;
  Stats Stat;
  std::condition_variable CV;
  bool ShouldStop = false;
  std::vector<Task> Queue; // max-heap
  llvm::StringMap<unsigned> Boosts;
  std::function<void(Stats)> OnProgress;
  llvm::DenseSet<uint64_t> QueuedKeys; // Keys of tasks in Queue.
  llvm::DenseSet<uint64_t> ActiveKeys; // Keys of tasks being run.
  // Tasks pushed while a task with the same key was running, to be queued
  // when it finishes.
  llvm::DenseMap<uint64_t, Task> Deferred;
};

// Builds an in-memory index by by running the static indexer action over
// all commands in a compilation database. Indexing happens in the background.
// FIXME: it should watch for changes to files on disk.
class BackgroundIndex : public SwapIndex {
public:
  struct Options {
    // Arbitrary value to ensure some concurrency in tests.
    // In production an explicit value is specified.
    size_t ThreadPoolSize = 4;
    // Thread priority when indexing files.
    llvm::ThreadPriority IndexingPriority = llvm::ThreadPriority::Low;
    // Callback that provides notifications as indexing makes progress.
    std::function<void(BackgroundQueue::Stats)> OnProgress = nullptr;
    // Function called to obtain the Context to use while indexing the specified
    // file. Called with the empty string for other tasks.
    // (When called, the context from BackgroundIndex construction is active).
    std::function<Context(PathRef)> ContextProvider = nullptr;
    // Whether the index needs to support the containedRefs() operation.
    // May use extra memory.
    bool SupportContainedRefs = true;
    // Called when new header-to-TU context mappings become available.
    // The argument contains absolute paths of files whose context changed.
    std::function<void(std::vector<std::string>)> OnIndexedHeaders = nullptr;
  };

  /// Creates a new background index and starts its threads.
  /// The current Context will be propagated to each worker thread.
  BackgroundIndex(const ThreadsafeFS &, const GlobalCompilationDatabase &CDB,
                  BackgroundIndexStorage::Factory IndexStorageFactory,
                  Options Opts);
  ~BackgroundIndex(); // Blocks while the current task finishes.

  // Enqueue translation units for indexing.
  // The indexing happens in a background thread, so the symbols will be
  // available sometime later.
  void enqueue(const std::vector<std::string> &ChangedFiles) {
    Queue.push(changedFilesTask(ChangedFiles));
  }

  /// Reindexes translation units affected by files that changed on disk:
  /// changed source files with compile commands, and a translation unit
  /// including each changed header. Deleted files are removed from the index.
  void reindexChangedFiles(std::vector<std::string> ChangedFiles);

  /// Whether \p File has been indexed or loaded from the index storage.
  bool isIndexed(PathRef File) const;

  /// Boosts priority of indexing related to Path.
  /// Typically used to index TUs when headers are opened.
  void boostRelated(llvm::StringRef Path);

  /// Returns an indexed TU that includes Header, or an empty string.
  std::string indexedTUForHeader(PathRef Header) const;

  // Cause background threads to stop after ther current task, any remaining
  // tasks will be discarded.
  void stop() {
    Rebuilder.shutdown();
    Queue.stop();
  }

  // Wait until the queue is empty, to allow deterministic testing.
  [[nodiscard]] bool
  blockUntilIdleForTest(std::optional<double> TimeoutSeconds = 10) {
    return Queue.blockUntilIdleForTest(TimeoutSeconds);
  }

  void profile(MemoryTree &MT) const;

private:
  /// Represents the state of a single file when indexing was performed.
  struct ShardVersion {
    FileDigest Digest{{0}};
    bool HadErrors = false;
  };

  /// Given index results from a TU, only update symbols coming from files with
  /// different digests than \p ShardVersionsSnapshot. Also stores new index
  /// information on IndexStorage.
  /// \p CommandGeneration is non-zero when the TU is indexed because its
  /// compile command changed, see CommandChangedTUs.
  void update(llvm::StringRef MainFile, IndexFileIn Index,
              const llvm::StringMap<ShardVersion> &ShardVersionsSnapshot,
              bool HadErrors, unsigned CommandGeneration);

  // configuration
  const ThreadsafeFS &TFS;
  const GlobalCompilationDatabase &CDB;
  llvm::ThreadPriority IndexingPriority;
  std::function<Context(PathRef)> ContextProvider;

  llvm::Error index(tooling::CompileCommand);

  FileSymbols IndexedSymbols;
  BackgroundIndexRebuilder Rebuilder;
  llvm::StringMap<ShardVersion> ShardVersions; // Key is absolute file path.
  // Incremented when compile commands of indexed files are found to change.
  unsigned CommandGeneration = 0;
  struct CommandChange {
    // CommandGeneration when the change was found.
    unsigned Generation;
    // The new command.
    std::vector<std::string> CommandLine;
    std::string Directory;
  };
  // Main files whose compile command changed since they were last indexed.
  // Key is from commandChangedKey(). The symbols of files they include can
  // depend on the command even if their contents didn't change, so indexing
  // them with the new command replaces the shards of included files not yet
  // indexed in that generation.
  llvm::StringMap<CommandChange> CommandChangedTUs;
  // The latest CommandGeneration each file was indexed in.
  llvm::StringMap<unsigned> IndexedCommandGeneration;
  // Main files to reindex even if their shards are up to date. Key is from
  // commandChangedKey().
  llvm::StringSet<> ForcedTUs;
  // Files reported deleted in this session, case-folded.
  llvm::StringSet<> DeletedFiles;
  // Serializes processing batches of changed files.
  std::mutex FileEventsMu;
  // Case-folded keys of ShardVersions.
  llvm::StringSet<> IndexedFiles;
  // Files can be indexed by several paths, e.g. through symlinked directories,
  // so that changes to a file apply to all its paths.
  // Canonical paths of keys of ShardVersions, as of when they were recorded.
  llvm::StringMap<std::string> CanonicalPaths;
  // Keys of ShardVersions by their case-folded canonical paths.
  llvm::StringMap<llvm::SmallVector<std::string, 1>> PathsByCanonicalPath;
  // The paths that symbols in the shards of keys of ShardVersions have, see
  // IndexFileIn::FileURI, if they differ.
  llvm::StringMap<std::string> SymbolPaths;
  // Records \p Path as indexed, with ShardVersionsMu held, and \p FileURI as
  // the URI its symbols have, if known.
  void recordIndexedPathLocked(PathRef Path, llvm::StringRef FileURI);
  // Paths that \p File was indexed by, including its canonical path.
  std::vector<std::string> indexedPaths(PathRef File);
  // The commands main files were last indexed with in this session, keyed by
  // commandChangedKey(). Shards store them too, but storage may be missing.
  llvm::StringMap<tooling::CompileCommand> IndexedCommands;
  mutable std::mutex ShardVersionsMu;
  std::function<void(std::vector<std::string>)> OnIndexedHeaders;

  std::vector<std::string>
  updateIndexedTUForHeaders(PathRef MainFile,
                            llvm::ArrayRef<std::string> Headers);
  llvm::StringMap<std::string> HeaderToIndexedTU;
  // Changed files without a known includer, case-folded. Removed when a TU
  // that includes them is indexed.
  llvm::StringSet<> PendingChangedHeaders;
  // Paths of the TUs in IndexedTUToHeaders, by their case-folded keys.
  llvm::StringMap<std::string> IndexedTUPaths;
  // Deleted headers and a TU that included them, to reindex if they're
  // recreated: the TU's shards don't change when a header it includes appears.
  llvm::StringMap<std::string> DeletedHeaderToIndexedTU;

  // Main files whose last indexing failed to find includes or probed for files
  // with __has_include, keyed by the lowercased file names, and the reverse, to
  // reindex them when a file with such a name appears or disappears.
  llvm::StringMap<llvm::StringSet<>> MissingIncludeToTUs;
  llvm::StringMap<std::vector<std::string>> TUToMissingIncludes;
  // Main files loaded from shards that recorded errors, whose missing includes
  // are unknown as they weren't indexed in this session. Missing includes
  // aren't stored in shards, so files that only probed for a missing header
  // with __has_include, which isn't an error, aren't known after a restart
  // until they're indexed again.
  llvm::StringSet<> UntrackedErrorTUs;
  llvm::StringSet<> IndexedThisSession;
  std::mutex MissingIncludesMu;
  llvm::StringMap<std::vector<std::string>> IndexedTUToHeaders;
  mutable std::mutex HeaderToIndexedTUMu;

  BackgroundIndexStorage::Factory IndexStorageFactory;
  // Tries to load shards for the MainFiles and their dependencies.
  std::vector<std::string> loadProject(std::vector<std::string> MainFiles);

  BackgroundQueue::Task
  changedFilesTask(const std::vector<std::string> &ChangedFiles);
  BackgroundQueue::Task indexFileTask(std::string Path);
  void removeFromIndex(PathRef File);
  void updateMissingIncludes(PathRef MainFile, std::vector<std::string> Names);
  // Reindexes MainFile, even if its shards are up to date.
  void reindexAgain(PathRef MainFile);

  // from lowest to highest priority
  enum QueuePriority {
    IndexFile,
    IndexBoostedFile,
    LoadShards,
  };
  BackgroundQueue Queue;
  AsyncTaskRunner ThreadPool;
  GlobalCompilationDatabase::CommandChanged::Subscription CommandsChanged;
};

} // namespace clangd
} // namespace clang

#endif
