//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_SUMMARIES_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_SUMMARIES_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "StackSegments.h"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace clang::tidy::malterlib {

/// The summaries the checks work out of the code, each of what it is of only
/// (its key): what a function keeps of a parameter, what a local can hold,
/// what an expression designates. One works out what it asks of others,
/// which are worked out first, so each is worked out from the summaries of
/// what it calls, bottom up, whichever is asked first.
///
/// Summaries that ask each other, a recursion, are worked out together: one
/// asked while it is worked out is answered by its approximation, first its
/// initial value, and when it was, it is worked out again with what it was
/// worked out to as its approximation, until that doesn't change; past
/// MaxPasses it is the widened value, the answer that holds of anything,
/// which what asked it is worked out once more with. What asked one while it
/// was worked out holds while its approximation does: it is worked out again
/// with it, starting from where its own last pass ended, and kept once the
/// outermost of those it asked is finished, the strongly connected
/// components of the summaries, as Tarjan's algorithm finds them. Of one
/// only a part of which is used (TCSummaries::observe), another pass is
/// needed only when that part is told otherwise.
///
/// What asks is never cut short: a summary is the same wherever it is asked,
/// however deep, on a new stack segment where the one it is on is nearly
/// exhausted (withStack).
///
/// What is worked out is a function of the key alone (TCSummaries); the
/// state a computation keeps on the side, a depth or what it is in, is its
/// own in each summary (TCSummaryLocal).
class CSummaryEntry {
public:
  enum class EState : unsigned char { None, Working, Provisional, Final };
  virtual ~CSummaryEntry() = default;

protected:
  friend class CSummaryEngine;
  EState State = EState::None;
  // Of one being worked out, the position of its frame on the stack; of a
  // provisional one, the least index of a frame it depends on.
  unsigned Frame = 0;
  unsigned LowLink = 0;
  // Its value, from a pass of a recursion it is in whose approximation
  // changed, is where it starts when it is worked out again in the same
  // outermost.
  bool Warm = false;
  // Of one warm, the passes it took while the outermost is worked out, which
  // count when it is worked out again (CSummaryEngine::MaxTotalPasses).
  unsigned Spent = 0;
  // The value is where it starts: of one warm its value, else the initial
  // one.
  virtual void start() = 0;
  // Works out the next value.
  virtual void workOut() = 0;
  // Whether the next value is the value.
  virtual bool settled() const = 0;
  // The next value is the value.
  virtual void adopt() = 0;
  // The next value is dropped.
  virtual void discard() = 0;
  // The value is the widened one.
  virtual void widen() = 0;
  // None, to be worked out again, warm; what referred to the value still
  // can.
  virtual void reset() = 0;
  // Whether the next value only moved from the value the way the values of
  // its summary grow (TCSummaries::setProgress), toward a value they settle
  // at in as many passes as the values can grow, so the pass isn't counted
  // against MaxPasses; false of a summary that doesn't tell.
  virtual bool progressed() const { return false; }
  // The value is the capped one (TCSummaries::setCapped), else the widened
  // one: it took MaxTotalPasses passes.
  virtual void cap() { widen(); }
};

/// A value a computation keeps on the side that each summary has its own of:
/// the engine sets it to its initial value where it starts working out one
/// and back where it is done.
class CSummaryLocal {
public:
  virtual ~CSummaryLocal() = default;

private:
  friend class CSummaryEngine;
  virtual void enter(size_t Depth) = 0;
  virtual void leave(size_t Depth) = 0;
};

class CSummaryEngine {
public:
  /// The engine of this thread.
  static CSummaryEngine &engine() {
    static thread_local CSummaryEngine s_Engine;
    return s_Engine;
  }

  /// Works out \p Entry, which isn't: here, or as the outermost when none
  /// is being worked out.
  void solve(CSummaryEntry &Entry) {
    if (Stack.empty()) {
      drive(Entry);
      return;
    }
    withStack([&] {
      push(Entry);
      while (!pass())
        ;
    });
  }

  /// That \p Entry, being worked out, is asked: its approximation is used.
  void readWorking(const CSummaryEntry &Entry) {
    CFrame &Of = Stack[Entry.Frame];
    Of.Read = true;
    Stack.back().LowLink = std::min(Stack.back().LowLink, Of.Index);
  }

  /// That \p Entry, being worked out, is asked through what \p fSame tells
  /// of it: its approximation is used as far as fSame tells the same of what
  /// it is worked out to, which then needs no other pass.
  void observeWorking(const CSummaryEntry &Entry,
                      std::function<bool()> fSame) {
    CFrame &Of = Stack[Entry.Frame];
    Of.Observations.push_back(std::move(fSame));
    Stack.back().LowLink = std::min(Stack.back().LowLink, Of.Index);
  }

  /// That \p Entry, provisional, is asked.
  void readProvisional(const CSummaryEntry &Entry) {
    Stack.back().LowLink = std::min(Stack.back().LowLink, Entry.LowLink);
  }

  /// Keeps \p Value, which was replaced, until the outermost is worked out:
  /// what was worked out with it can still refer to it.
  template <typename t_CValue> void bury(std::unique_ptr<t_CValue> Value) {
    if (Value)
      Buried.push_back(std::shared_ptr<const void>(std::move(Value)));
  }

  /// Runs \p fIdle where the outermost summary being worked out is worked
  /// out, or now where none is: what nothing being worked out refers to any
  /// more is known there.
  void whenIdle(std::function<void()> fIdle) {
    if (Stack.empty())
      fIdle();
    else
      Idle.push_back(std::move(fIdle));
  }

  void addLocal(CSummaryLocal *Local) { Locals.push_back(Local); }
  void removeLocal(CSummaryLocal *Local) {
    Locals.erase(std::remove(Locals.begin(), Locals.end(), Local),
                 Locals.end());
  }

private:
  /// How many times one asked while it is worked out is worked out before it
  /// is the widened value, not counting a pass whose value only grew the way
  /// its summary's values do (CSummaryEntry::progressed).
  static constexpr unsigned MaxPasses = 8;
  /// How many times it is worked out at most, also counting those, before
  /// it is the capped value (CSummaryEntry::cap): in all, while the
  /// outermost is worked out, also where a recursion it is in works it out
  /// again, so recursions within recursions take as many passes in all.
  static constexpr unsigned MaxTotalPasses = 64;

  struct CFrame {
    CSummaryEntry *Entry = nullptr;
    unsigned Index = 0;
    unsigned LowLink = 0;
    unsigned Passes = 0;
    // Each pass, also one whose value only grew (CSummaryEntry::progressed).
    unsigned TotalPasses = 0;
    // Its approximation was used, as it is or by what was observed of it.
    bool Read = false;
    std::vector<std::function<bool()>> Observations;
    bool Widened = false;
    // What was worked out in it since it started its pass that is
    // provisional on what is still being worked out.
    std::vector<CSummaryEntry *> Provisionals;
  };

  void push(CSummaryEntry &Entry) {
    Entry.start();
    Entry.State = CSummaryEntry::EState::Working;
    Entry.Frame = Stack.size();
    if (!Entry.Warm)
      Entry.Spent = 0;
    CFrame &Frame = Stack.emplace_back();
    Frame.Entry = &Entry;
    Frame.Index = Frame.LowLink = NextIndex++;
    Frame.TotalPasses = Entry.Spent;
  }

  // The outermost: works out \p Root.
  void drive(CSummaryEntry &Root) {
    NextIndex = 0;
    push(Root);
    while (!pass())
      ;
    for (CSummaryEntry *Entry : Warmed) {
      Entry->Warm = false;
      Entry->Spent = 0;
    }
    Warmed.clear();
    Buried.clear();
    std::vector<std::function<void()>> Run;
    Run.swap(Idle);
    for (const std::function<void()> &fIdle : Run)
      fIdle();
  }

  // One pass of the innermost: true once it is finished, which pops it.
  bool pass() {
    const size_t Position = Stack.size() - 1;
    for (CSummaryLocal *Local : Locals)
      Local->enter(Position);
    Stack[Position].Entry->workOut();
    for (CSummaryLocal *Local : llvm::reverse(Locals))
      Local->leave(Position);
    CFrame &Frame = Stack[Position];
    CSummaryEntry &Entry = *Frame.Entry;
    // Whether what used its approximation can be told otherwise by what it
    // is worked out to.
    const bool Used = Frame.Read || !Frame.Observations.empty();
    const bool Same = !Used || Entry.settled();
    const bool Stale =
        !Same &&
        (Frame.Read ||
         llvm::any_of(Frame.Observations,
                      [](const std::function<bool()> &fSame) {
                        return !fSame();
                      }));
    if (Stale && !Frame.Widened) {
      const bool Progressed = Entry.progressed();
      Entry.Spent = ++Frame.TotalPasses;
      if (Frame.TotalPasses >= MaxTotalPasses) {
        Entry.cap();
        Frame.Widened = true;
      } else if (Progressed || ++Frame.Passes < MaxPasses)
        Entry.adopt();
      else {
        Entry.widen();
        Frame.Widened = true;
      }
      restart(Frame);
      return false;
    }
    // What used it was worked out with the widened value, which it stays,
    // or with a value that is the same.
    if (Stale || (Used && Same))
      Entry.discard();
    else
      Entry.adopt();
    finish();
    return true;
  }

  // Another pass of \p Frame, whose approximation changed: what was worked
  // out with the one before is worked out again.
  void restart(CFrame &Frame) {
    for (CSummaryEntry *Provisional : Frame.Provisionals)
      reset(*Provisional);
    Frame.Provisionals.clear();
    Frame.Read = false;
    Frame.Observations.clear();
    Frame.LowLink = Frame.Index;
  }

  // Of one worked out with an approximation that changed: its value starts
  // it again, which keeps what its own passes found across
  // those of a recursion it is in, rather than working them out again from
  // the start with each pass of the root.
  void reset(CSummaryEntry &Entry) {
    Entry.State = CSummaryEntry::EState::None;
    Entry.reset();
    if (!Entry.Warm) {
      Entry.Warm = true;
      Warmed.push_back(&Entry);
    }
  }

  // Pops the innermost, finished: of the root of a component, it and what
  // was worked out in it are final; else provisional on what it asked.
  void finish() {
    CFrame Frame = std::move(Stack.back());
    Stack.pop_back();
    CSummaryEntry &Entry = *Frame.Entry;
    if (Frame.LowLink >= Frame.Index) {
      Entry.State = CSummaryEntry::EState::Final;
      for (CSummaryEntry *Provisional : Frame.Provisionals)
        Provisional->State = CSummaryEntry::EState::Final;
      return;
    }
    Entry.State = CSummaryEntry::EState::Provisional;
    Entry.LowLink = Frame.LowLink;
    CFrame &Parent = Stack.back();
    Parent.LowLink = std::min(Parent.LowLink, Frame.LowLink);
    Parent.Provisionals.insert(Parent.Provisionals.end(),
                               Frame.Provisionals.begin(),
                               Frame.Provisionals.end());
    Parent.Provisionals.push_back(&Entry);
  }

  std::vector<CFrame> Stack;
  unsigned NextIndex = 0;
  std::vector<std::shared_ptr<const void>> Buried;
  // Those warm (CSummaryEntry::Warm), only while the outermost is worked out.
  std::vector<CSummaryEntry *> Warmed;
  std::vector<CSummaryLocal *> Locals;
  // What runs where the outermost is worked out (whenIdle).
  std::vector<std::function<void()>> Idle;
};

/// The summaries of what \p t_CKey is, each worked out by fWorkOut from the
/// key alone (CSummaryEngine): its initial value is what it is taken as
/// while it is worked out, which what asks it again then is told, the
/// widened one what holds of anything. What get gives stays where it is
/// until the summaries are cleared.
template <typename t_CKey, typename t_CValue> class TCSummaries {
public:
  using FCompute = std::function<t_CValue(const t_CKey &)>;

  TCSummaries(FCompute fWorkOut, FCompute fInitial, FCompute fWidened)
      : fWorkOut(std::move(fWorkOut)), fInitial(std::move(fInitial)),
        fWidened(std::move(fWidened)) {}
  TCSummaries(FCompute fWorkOut, t_CValue Initial, t_CValue Widened)
      : fWorkOut(std::move(fWorkOut)),
        fInitial(
            [Initial = std::move(Initial)](const t_CKey &) { return Initial; }),
        fWidened([Widened = std::move(Widened)](const t_CKey &) {
          return Widened;
        }) {}
  TCSummaries(const TCSummaries &) = delete;
  TCSummaries &operator=(const TCSummaries &) = delete;

  /// The summary of \p Key.
  const t_CValue &get(const t_CKey &Key) {
    CEntry &Entry = entryOf(Key);
    CSummaryEngine &Engine = CSummaryEngine::engine();
    switch (Entry.state()) {
    case CSummaryEntry::EState::Final:
      return *Entry.Value;
    case CSummaryEntry::EState::Provisional:
      Engine.readProvisional(Entry);
      return *Entry.Value;
    case CSummaryEntry::EState::Working:
      Engine.readWorking(Entry);
      return *Entry.Value;
    case CSummaryEntry::EState::None:
      break;
    }
    Engine.solve(Entry);
    return *Entry.Value;
  }

  /// What \p fObserve tells of the summary of \p Key, which is all of it
  /// that is used: of one being worked out, its approximation, which is
  /// worked out again only when that tells otherwise of what it is worked
  /// out to (CSummaryEngine::observeWorking). \p fObserve is called again
  /// then, and keeps what it needs.
  template <typename tf_FObserve>
  auto observe(const t_CKey &Key, tf_FObserve fObserve)
      -> decltype(fObserve(std::declval<const t_CValue &>())) {
    CEntry &Entry = entryOf(Key);
    CSummaryEngine &Engine = CSummaryEngine::engine();
    if (Entry.state() != CSummaryEntry::EState::Working)
      return fObserve(get(Key));
    auto Observed = fObserve(*Entry.Value);
    Engine.observeWorking(Entry, [&Entry, Observed, fObserve] {
      return fObserve(*Entry.Next) == Observed;
    });
    return Observed;
  }

  /// That a pass of a recursion whose value only moved from the one before
  /// the way \p fGrew tells, given the one before and the new one, isn't
  /// counted before the summary is widened (CSummaryEntry::progressed), of
  /// values that grow only so far; and \p fCap, the value where it took
  /// as many passes as the engine takes at most.
  void setProgress(std::function<bool(const t_CValue &, const t_CValue &)> fGrew,
                   FCompute fCap) {
    fProgress = std::move(fGrew);
    fCapped = std::move(fCap);
  }

  /// The summary of \p Key once final, else null.
  const t_CValue *finished(const t_CKey &Key) const {
    const auto It = Entries.find(Key);
    return It != Entries.end() &&
                   It->second->state() == CSummaryEntry::EState::Final
               ? It->second->Value.get()
               : nullptr;
  }

  void clear() { Entries.clear(); }

  /// Whether the summary of \p Key is there, worked out or being worked out.
  bool contains(const t_CKey &Key) const { return Entries.count(Key) != 0; }

  /// Whether the summary of \p Key can be dropped (drop): none, or one that
  /// is final and isn't kept warm for a recursion the outermost still works
  /// out, which the engine refers to.
  bool droppable(const t_CKey &Key) const {
    const auto It = Entries.find(Key);
    return It == Entries.end() || It->second->droppable();
  }

  /// Drops the summary of \p Key, droppable: it is worked out again where it
  /// is asked again. What get gave of it no longer refers to it, so only one
  /// nothing refers to any more is dropped.
  void drop(const t_CKey &Key) { Entries.erase(Key); }

private:
  class CEntry;
  CEntry &entryOf(const t_CKey &Key) {
    std::unique_ptr<CEntry> &Slot = Entries[Key];
    if (!Slot)
      Slot = std::make_unique<CEntry>(*this, Key);
    return *Slot;
  }

  class CEntry final : public CSummaryEntry {
  public:
    CEntry(TCSummaries &Of, const t_CKey &Key) : Of(Of), Key(Key) {}
    EState state() const { return State; }
    bool droppable() const {
      return (State == EState::Final || State == EState::None) && !Warm;
    }

    TCSummaries &Of;
    t_CKey Key;
    std::unique_ptr<t_CValue> Value;
    std::unique_ptr<t_CValue> Next;

  private:
    void start() override {
      if (Warm && Value)
        return;
      CSummaryEngine::engine().bury(std::move(Value));
      Value = std::make_unique<t_CValue>(Of.fInitial(Key));
    }
    void workOut() override {
      Next = std::make_unique<t_CValue>(Of.fWorkOut(Key));
    }
    bool settled() const override { return *Next == *Value; }
    void adopt() override {
      CSummaryEngine::engine().bury(std::move(Value));
      Value = std::move(Next);
    }
    void discard() override { Next.reset(); }
    void widen() override {
      CSummaryEngine::engine().bury(std::move(Value));
      Value = std::make_unique<t_CValue>(Of.fWidened(Key));
      Next.reset();
    }
    void reset() override { Next.reset(); }
    bool progressed() const override {
      return Of.fProgress && Value && Next && Of.fProgress(*Value, *Next);
    }
    void cap() override {
      if (!Of.fCapped) {
        widen();
        return;
      }
      CSummaryEngine::engine().bury(std::move(Value));
      Value = std::make_unique<t_CValue>(Of.fCapped(Key));
      Next.reset();
    }
  };

  FCompute fWorkOut;
  FCompute fInitial;
  FCompute fWidened;
  std::function<bool(const t_CValue &, const t_CValue &)> fProgress;
  FCompute fCapped;
  llvm::DenseMap<t_CKey, std::unique_ptr<CEntry>> Entries;
};

/// A \p t_CValue of a computation that each summary has its own of
/// (CSummaryLocal), its initial value where one starts: a depth, or what is
/// being looked at, which bounds what is followed in the summary it is in
/// and is the same wherever that is asked.
template <typename t_CValue> class TCSummaryLocal final : public CSummaryLocal {
public:
  TCSummaryLocal() { CSummaryEngine::engine().addLocal(this); }
  ~TCSummaryLocal() override { CSummaryEngine::engine().removeLocal(this); }
  TCSummaryLocal(const TCSummaryLocal &) = delete;
  TCSummaryLocal &operator=(const TCSummaryLocal &) = delete;

  t_CValue &operator*() { return Value; }
  const t_CValue &operator*() const { return Value; }
  t_CValue *operator->() { return &Value; }
  const t_CValue *operator->() const { return &Value; }

private:
  void enter(size_t Depth) override {
    Saved.emplace_back(Depth, std::move(Value));
    Value = t_CValue();
  }
  // Not one made since that summary started, whose value is its own.
  void leave(size_t Depth) override {
    if (Saved.empty() || Saved.back().first != Depth)
      return;
    Value = std::move(Saved.back().second);
    Saved.pop_back();
  }

  t_CValue Value = t_CValue();
  std::vector<std::pair<size_t, t_CValue>> Saved;
};

} // namespace clang::tidy::malterlib

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_SUMMARIES_H
